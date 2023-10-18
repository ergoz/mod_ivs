/**
 * (C)2023 aks
 * https://github.com/akscf/
 **/
#include "ivs_qjs.h"
#include "ivs_events.h"
#include "ivs_curl.h"

#define CLASS_NAME              "ChatGPT"
#define PROP_APIKEY             0
#define PROP_CHAT_MODEL         1
#define PROP_WHISPER_MODEL      2
#define PROP_PROXY              3
#define PROP_PROXY_CREDENTIALS  4
#define PROP_USER_AGENT         5
#define PROP_REQ_TIMEOUT        6
#define PROP_CON_TIMEOUT        7
#define PROP_ROLE               8
#define PROP_CHAT_API_URL       9
#define PROP_WHISPER_API_URL    10
#define PROP_LOG_HTTP_ERRORS    11

#define CHATGPT_NLP_URL         "https://api.openai.com/v1/chat/completions"
#define CHATGPT_WHISPER_URL     "https://api.openai.com/v1/audio/transcriptions"

#define CHATGPT_NLP_TYPE        "Content-Type: application/json; charset=utf-8"
#define CHATGPT_WHISPER_TYPE    "Content-Type: multipart/form-data"

#define CHATGPT_SANITY_CHECK() if (!js_chatgpt) { \
           return JS_ThrowTypeError(ctx, "ChatGPT is not initialized"); \
        }

static void js_chatgpt_finalizer(JSRuntime *rt, JSValue val);

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// helpers
typedef struct {
    switch_memory_pool_t    *pool;
    curl_conf_t             *curl_conf;
    ivs_session_t           *ivs_session_ref;
    char                    *file_to_send;
    uint8_t                 fl_log_http_errors;
    uint8_t                 fl_delete_file;
    uint32_t                jid;
} chatgpt_conf_t;

static switch_status_t chatgpt_conf_alloc(chatgpt_conf_t **conf) {
    switch_status_t status = SWITCH_STATUS_FALSE;
    switch_memory_pool_t *pool = NULL;
    chatgpt_conf_t *chatgpt_conf = NULL;
    curl_conf_t *curl_conf = NULL;

    if((status = switch_core_new_memory_pool(&pool)) != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "pool fail\n");
        switch_goto_status(SWITCH_STATUS_GENERR, out);
    }
    if((chatgpt_conf = switch_core_alloc(pool, sizeof(chatgpt_conf_t))) == NULL) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "mem fail\n");
        switch_goto_status(SWITCH_STATUS_GENERR, out);
    }
    if((status = curl_config_alloc(&curl_conf, pool, true)) != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "mem fail\n");
        switch_goto_status(SWITCH_STATUS_GENERR, out);
    }
    chatgpt_conf->pool = pool;
    chatgpt_conf->curl_conf = curl_conf;
    chatgpt_conf->jid = JID_NONE;
    chatgpt_conf->file_to_send = NULL;

    *conf = chatgpt_conf;
out:
    if(status != SWITCH_STATUS_SUCCESS) {
        if(curl_conf) {
            curl_config_free(curl_conf);
        }
        if(pool) {
            switch_core_destroy_memory_pool(&pool);
        }
    }
    return status;
}

static void chatgpt_conf_free(chatgpt_conf_t *conf) {
    switch_memory_pool_t *pool = (conf ? conf->pool : NULL);
    curl_conf_t *curl_conf = (conf ? conf->curl_conf : NULL);;

    if(curl_conf) {
        curl_config_free(curl_conf);
    }
    if(pool) {
        switch_core_destroy_memory_pool(&pool);
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
static ivs_event_payload_nlp_t *nlp_request_exec(chatgpt_conf_t *chatgpt_conf) {
    switch_status_t status = SWITCH_STATUS_FALSE;
    curl_conf_t *curl_conf = chatgpt_conf->curl_conf;
    ivs_event_payload_nlp_t *result = NULL;
    const void *http_response_ptr = NULL;
    cJSON *json = NULL;
    uint32_t recv_len = 0;

    status = curl_perform(curl_conf);

    recv_len = switch_buffer_inuse(curl_conf->recv_buffer);
    if(recv_len > 0) {
        switch_buffer_write(curl_conf->recv_buffer, "\0", 1);
        switch_buffer_peek_zerocopy(curl_conf->recv_buffer, &http_response_ptr);

        if(status == SWITCH_STATUS_SUCCESS) {
            if((json = cJSON_Parse((char *)http_response_ptr)) != NULL) {
                cJSON *jres = cJSON_GetObjectItem(json, "error");
                if(jres) {
                    if(chatgpt_conf->fl_log_http_errors) {
                        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Service-fail: jid=%i, response=%s\n", chatgpt_conf->jid, (char *)http_response_ptr);
                    }
                    status = SWITCH_STATUS_FALSE;
                } else {
                    cJSON *jres = cJSON_GetObjectItem(json, "choices");
                    if(jres) {
                        if(jres && cJSON_GetArraySize(jres) > 0) {
                            cJSON *jelem = cJSON_GetArrayItem(jres, 0);
                            cJSON *jmsg = cJSON_GetObjectItem(jelem, "message");
                            if(jmsg) {
                                cJSON *jrole = cJSON_GetObjectItem(jmsg, "role");
                                cJSON *jtext = cJSON_GetObjectItem(jmsg, "content");
                                if(jrole && jtext) {
                                    ivs_event_payload_nlp_alloc(&result, jrole->valuestring, jtext->valuestring);
                                }
                            }
                        }
                    } else {
                        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Malformed response [jid=%i]: %s\n", chatgpt_conf->jid, (char *)http_response_ptr);
                        status = SWITCH_STATUS_FALSE;
                    }
                }
            } else {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Malformed response [jid=%i]: %s\n", chatgpt_conf->jid, (char *)http_response_ptr);
                status = SWITCH_STATUS_FALSE;
            }
        } else {
            if(chatgpt_conf->fl_log_http_errors) {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "HTTP-ERROR: jid=%i, http-code=%i, http-text: %s\n", chatgpt_conf->jid, curl_conf->http_error, (char *)http_response_ptr);
            }
            status = SWITCH_STATUS_FALSE;
        }
    }
    if(json != NULL) {
        cJSON_Delete(json);
    }
    return result;
}
static void *SWITCH_THREAD_FUNC nlp_request_async_thread(switch_thread_t *thread, void *obj) {
    volatile chatgpt_conf_t *_ref = (chatgpt_conf_t *) obj;
    chatgpt_conf_t *chatgpt_conf = (chatgpt_conf_t *) _ref;
    ivs_session_t *ivs_session = chatgpt_conf->ivs_session_ref;
    ivs_event_payload_nlp_t *res = NULL;

    res = nlp_request_exec(chatgpt_conf);
    if(res) {
        if(ivs_event_push_nlp2(IVS_EVENTSQ(chatgpt_conf->ivs_session_ref), chatgpt_conf->jid, res) != SWITCH_STATUS_SUCCESS) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to emit event\n");

            ivs_event_payload_nlp_free(res);
            switch_safe_free(res);
        }
    } else {
        if(chatgpt_conf->curl_conf->http_error != 200) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Job [%i] failed, code=%i\n", chatgpt_conf->jid, chatgpt_conf->curl_conf->http_error);
        }
    }

    chatgpt_conf_free(chatgpt_conf);
    ivs_session_release(ivs_session);
    thread_finished();
    return NULL;
}
static uint32_t nlp_request_exec_async(chatgpt_conf_t *chatgpt_conf) {
    uint32_t jid = JID_NONE;

    if(ivs_session_take(chatgpt_conf->ivs_session_ref)) {
        jid = ivs_gen_job_id(chatgpt_conf->ivs_session_ref);
        chatgpt_conf->jid = jid;
        launch_thread(chatgpt_conf->pool, nlp_request_async_thread, chatgpt_conf);
    }

    return jid;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
static ivs_event_payload_transcription_t *whisper_request_exec(chatgpt_conf_t *chatgpt_conf) {
    switch_status_t status = SWITCH_STATUS_FALSE;
    curl_conf_t *curl_conf = chatgpt_conf->curl_conf;
    ivs_event_payload_transcription_t *result = NULL;
    const void *http_response_ptr = NULL;
    cJSON *json = NULL;
    uint32_t recv_len = 0;

    status = curl_perform(curl_conf);

    recv_len = switch_buffer_inuse(curl_conf->recv_buffer);
    if(recv_len > 0) {
        switch_buffer_write(curl_conf->recv_buffer, "\0", 1);
        switch_buffer_peek_zerocopy(curl_conf->recv_buffer, &http_response_ptr);

        if(status == SWITCH_STATUS_SUCCESS) {
            if((json = cJSON_Parse((char *)http_response_ptr)) != NULL) {
                cJSON *jres = cJSON_GetObjectItem(json, "error");
                if(jres) {
                    if(chatgpt_conf->fl_log_http_errors) { switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "HTTP-RESPONSE: %s\n", (char *)http_response_ptr); }
                    status = SWITCH_STATUS_FALSE;
                } else {
                    cJSON *jres = cJSON_GetObjectItem(json, "text");
                    if(jres) {
                        ivs_event_payload_transcription_alloc(&result, 0.0, jres->valuestring);
                    } else {
                        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Malformed response: %s\n", (char *)http_response_ptr);
                        status = SWITCH_STATUS_FALSE;
                    }
                }
            } else {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Malformed response: %s\n", (char *)http_response_ptr);
                status = SWITCH_STATUS_FALSE;
            }
        } else {
            if(chatgpt_conf->fl_log_http_errors) { switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "HTTP-RESPONSE: %s\n", (char *)http_response_ptr); }
            status = SWITCH_STATUS_FALSE;
        }
    }
    if(json != NULL) {
        cJSON_Delete(json);
    }
    if(chatgpt_conf->fl_delete_file) {
        if(chatgpt_conf->file_to_send) {
            unlink(chatgpt_conf->file_to_send);
        }
    }
    return result;
}
static void *SWITCH_THREAD_FUNC whisper_request_async_thread(switch_thread_t *thread, void *obj) {
    volatile chatgpt_conf_t *_ref = (chatgpt_conf_t *) obj;
    chatgpt_conf_t *chatgpt_conf = (chatgpt_conf_t *) _ref;
    ivs_session_t *ivs_session = chatgpt_conf->ivs_session_ref;
    ivs_event_payload_transcription_t *res = NULL;

    res = whisper_request_exec(chatgpt_conf);
    if(res) {
        if(ivs_event_push_transcription2(IVS_EVENTSQ(chatgpt_conf->ivs_session_ref), chatgpt_conf->jid, res) != SWITCH_STATUS_SUCCESS) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to emit event\n");

            ivs_event_payload_transcription_free(res);
            switch_safe_free(res);
        }
    } else {
        if(chatgpt_conf->curl_conf->http_error != 200) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Job [%i] failed, code=%i\n", chatgpt_conf->jid, chatgpt_conf->curl_conf->http_error);
        }
    }

    chatgpt_conf_free(chatgpt_conf);
    ivs_session_release(ivs_session);
    thread_finished();
    return NULL;
}
static uint32_t whisper_request_exec_async(chatgpt_conf_t *chatgpt_conf) {
    uint32_t jid = JID_NONE;

    if(ivs_session_take(chatgpt_conf->ivs_session_ref)) {
        jid = ivs_gen_job_id(chatgpt_conf->ivs_session_ref);
        chatgpt_conf->jid = jid;
        launch_thread(chatgpt_conf->pool, whisper_request_async_thread, chatgpt_conf);
    }

    return jid;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
static JSValue js_chatgpt_property_get(JSContext *ctx, JSValueConst this_val, int magic) {
    js_chatgpt_t *js_chatgpt = JS_GetOpaque2(ctx, this_val, js_chatgpt_get_classid(ctx));

    if(!js_chatgpt) { return JS_UNDEFINED; }

    switch(magic) {
        case PROP_APIKEY: {
            return JS_NewString(ctx, js_chatgpt->apikey);
        }
        case PROP_CHAT_MODEL: {
            return JS_NewString(ctx, js_chatgpt->chat_model);
        }
        case PROP_WHISPER_MODEL: {
            return JS_NewString(ctx, js_chatgpt->whisper_model);
        }
        case PROP_USER_AGENT: {
            if(!zstr(js_chatgpt->user_agent)) {
               return JS_NewString(ctx, js_chatgpt->user_agent);
            }
            return JS_UNDEFINED;
        }
        case PROP_ROLE: {
            if(!zstr(js_chatgpt->role)) {
               return JS_NewString(ctx, js_chatgpt->role);
            }
            return JS_UNDEFINED;
        }
        case PROP_PROXY: {
            if(!zstr(js_chatgpt->proxy)) {
               return JS_NewString(ctx, js_chatgpt->proxy);
            }
            return JS_UNDEFINED;
        }
        case PROP_PROXY_CREDENTIALS: {
            if(!zstr(js_chatgpt->proxy_credentials)) {
               return JS_NewString(ctx, js_chatgpt->proxy_credentials);
            }
            return JS_UNDEFINED;
        }
        case PROP_REQ_TIMEOUT: {
            return JS_NewInt32(ctx, js_chatgpt->request_timeout);
        }
        case PROP_CON_TIMEOUT: {
            return JS_NewInt32(ctx, js_chatgpt->connect_timeout);
        }
        case PROP_CHAT_API_URL: {
            return JS_NewString(ctx, CHATGPT_NLP_URL);
        }
        case PROP_WHISPER_API_URL: {
            return JS_NewString(ctx, CHATGPT_WHISPER_URL);
        }
        case PROP_LOG_HTTP_ERRORS: {
            return (js_chatgpt->fl_log_http_errors ? JS_TRUE : JS_FALSE);
        }
    }

    return JS_UNDEFINED;
}

static JSValue js_chatgpt_property_set(JSContext *ctx, JSValueConst this_val, JSValue val, int magic) {
    js_chatgpt_t *js_chatgpt = JS_GetOpaque2(ctx, this_val, js_chatgpt_get_classid(ctx));
    const char *str = NULL;
    int copy = 1, success = 1;

    if(!js_chatgpt) { return JS_UNDEFINED; }

    switch(magic) {
        case PROP_APIKEY: {
            if(QJS_IS_NULL(val)) { return JS_FALSE; }
            str = JS_ToCString(ctx, val);
            if(strcmp(js_chatgpt->apikey, str)) {
                js_chatgpt->apikey = switch_core_strdup(js_chatgpt->pool, str);
            }
            JS_FreeCString(ctx, str);
            return JS_TRUE;
        }
        case PROP_CHAT_MODEL: {
            if(QJS_IS_NULL(val)) { return JS_FALSE; }
            str = JS_ToCString(ctx, val);
            if(strcmp(js_chatgpt->chat_model, str)) {
                js_chatgpt->chat_model = switch_core_strdup(js_chatgpt->pool, str);
            }
            JS_FreeCString(ctx, str);
            return JS_TRUE;
        }
        case PROP_WHISPER_MODEL: {
            if(QJS_IS_NULL(val)) { return JS_FALSE; }
            str = JS_ToCString(ctx, val);
            if(strcmp(js_chatgpt->whisper_model, str)) {
                js_chatgpt->whisper_model = switch_core_strdup(js_chatgpt->pool, str);
            }
            JS_FreeCString(ctx, str);
            return JS_TRUE;
        }
        case PROP_USER_AGENT: {
            if(QJS_IS_NULL(val)) {
                js_chatgpt->user_agent = NULL;
            } else {
                str = JS_ToCString(ctx, val);
                if(!zstr(js_chatgpt->user_agent)) { copy = strcmp(js_chatgpt->user_agent, str); }
                if(copy) { js_chatgpt->user_agent = switch_core_strdup(js_chatgpt->pool, str); }
                JS_FreeCString(ctx, str);
            }
            return JS_TRUE;
        }
        case PROP_ROLE: {
            if(QJS_IS_NULL(val)) {
                js_chatgpt->role = NULL;
            } else {
                str = JS_ToCString(ctx, val);
                if(!zstr(js_chatgpt->role)) { copy = strcmp(js_chatgpt->role, str); }
                if(copy) { js_chatgpt->role = switch_core_strdup(js_chatgpt->pool, str); }
                JS_FreeCString(ctx, str);
            }
            return JS_TRUE;
        }
        case PROP_PROXY: {
            if(QJS_IS_NULL(val)) {
                js_chatgpt->proxy = NULL;
            } else {
                str = JS_ToCString(ctx, val);
                if(!zstr(js_chatgpt->proxy)) { copy = strcmp(js_chatgpt->proxy, str); }
                if(copy) { js_chatgpt->proxy = switch_core_strdup(js_chatgpt->pool, str); }
                JS_FreeCString(ctx, str);
            }
            return JS_TRUE;
        }
        case PROP_PROXY_CREDENTIALS: {
            if(QJS_IS_NULL(val)) {
                js_chatgpt->proxy_credentials = NULL;
            } else {
                str = JS_ToCString(ctx, val);
                if(!zstr(js_chatgpt->proxy_credentials)) { copy = strcmp(js_chatgpt->proxy_credentials, str); }
                if(copy) { js_chatgpt->proxy_credentials = switch_core_strdup(js_chatgpt->pool, str); }
                JS_FreeCString(ctx, str);
            }
            return JS_TRUE;
        }

        case PROP_REQ_TIMEOUT: {
            JS_ToUint32(ctx, &js_chatgpt->request_timeout, val);
            return JS_TRUE;
        }
        case PROP_CON_TIMEOUT: {
            JS_ToUint32(ctx, &js_chatgpt->connect_timeout, val);
            return JS_TRUE;
        }
        case PROP_LOG_HTTP_ERRORS: {
            js_chatgpt->fl_log_http_errors = JS_ToBool(ctx, val);
        }

        case PROP_CHAT_API_URL: {
            return JS_FALSE;
        }
        case PROP_WHISPER_API_URL: {
            return JS_FALSE;
        }
    }

    return JS_FALSE;
}

// askChatGPT("text", asyncFlag);
static JSValue js_chatgpt_do_chat_request(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    js_chatgpt_t *js_chatgpt = JS_GetOpaque2(ctx, this_val, js_chatgpt_get_classid(ctx));
    ivs_session_t *ivs_session = JS_GetRuntimeOpaque(JS_GetRuntime(ctx));
    switch_status_t status = SWITCH_STATUS_SUCCESS;
    chatgpt_conf_t *chatgpt_conf = NULL;
    const char *text_to_send = NULL;
    char *json_text = NULL;
    int fl_async = false;
    JSValue ret_obj = JS_UNDEFINED;

    CHATGPT_SANITY_CHECK();

    if(!ivs_session) {
        return JS_ThrowTypeError(ctx, "Malformed reference: ivs_session");
    }
    if(argc > 0) {
        if(QJS_IS_NULL(argv[0])) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Invalid argument: text\n");
            goto out;
        }
        text_to_send = JS_ToCString(ctx, argv[0]);
    }
    if(argc > 1) {
        fl_async = JS_ToBool(ctx, argv[1]);
    }

    if(text_to_send) {
        cJSON *jstr = cJSON_CreateString(text_to_send);
        json_text = cJSON_PrintUnformatted(jstr);
        cJSON_Delete(jstr);
    }

    if((status = chatgpt_conf_alloc(&chatgpt_conf)) != SWITCH_STATUS_SUCCESS) {
        switch_goto_status(SWITCH_STATUS_GENERR, out);
    }
    chatgpt_conf->ivs_session_ref = ivs_session;
    chatgpt_conf->fl_log_http_errors = js_chatgpt->fl_log_http_errors;
    chatgpt_conf->curl_conf->url = CHATGPT_NLP_URL;
    chatgpt_conf->curl_conf->content_type = CHATGPT_NLP_TYPE;
    chatgpt_conf->curl_conf->curl_auth_type = CURLAUTH_BEARER;
    chatgpt_conf->curl_conf->send_buffer = switch_core_sprintf(chatgpt_conf->pool, "{\"model\": \"%s\", \"messages\": [{\"role\": \"%s\", \"content\": %s}]}", js_chatgpt->chat_model, js_chatgpt->role, (json_text ? json_text : "null"));
    chatgpt_conf->curl_conf->send_buffer_len = strlen(chatgpt_conf->curl_conf->send_buffer);
    chatgpt_conf->curl_conf->request_timeout = js_chatgpt->request_timeout;
    chatgpt_conf->curl_conf->connect_timeout = js_chatgpt->connect_timeout;
    chatgpt_conf->curl_conf->credentials = safe_pool_strdup(chatgpt_conf->pool, js_chatgpt->apikey);
    chatgpt_conf->curl_conf->user_agent = safe_pool_strdup(chatgpt_conf->pool, js_chatgpt->user_agent);

    if(fl_async) {
        uint32_t jid = nlp_request_exec_async(chatgpt_conf);
        ret_obj = (jid > 0 ? JS_NewInt32(ctx, jid) : JS_FALSE);
    } else {
        ivs_event_payload_nlp_t *res = nlp_request_exec(chatgpt_conf);
        if(res) {
            ret_obj = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, ret_obj, "role", (res->role ? JS_NewString(ctx, res->role) : JS_UNDEFINED));
            JS_SetPropertyStr(ctx, ret_obj, "text", (res->text ? JS_NewString(ctx, res->text) : JS_UNDEFINED)) ;

            ivs_event_payload_nlp_free(res);
            switch_safe_free(res);
        }
        chatgpt_conf_free(chatgpt_conf);
    }
out:
    JS_FreeCString(ctx, text_to_send);
    switch_safe_free(json_text);

    if(status != SWITCH_STATUS_SUCCESS) {
        if(chatgpt_conf) {
            chatgpt_conf_free(chatgpt_conf);
        }
    }

    return ret_obj;
}


// aksWhisper(filename, deleteFileFlag, asyncFlag);
static JSValue js_chatgpt_do_whisper_request(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    js_chatgpt_t *js_chatgpt = JS_GetOpaque2(ctx, this_val, js_chatgpt_get_classid(ctx));
    ivs_session_t *ivs_session = JS_GetRuntimeOpaque(JS_GetRuntime(ctx));
    switch_status_t status = SWITCH_STATUS_SUCCESS;
    chatgpt_conf_t *chatgpt_conf = NULL;
    const char *file_to_send = NULL;
    int fl_async = false, fl_delete = false;
    JSValue ret_obj = JS_UNDEFINED;

    CHATGPT_SANITY_CHECK();

    if(!ivs_session) {
        return JS_ThrowTypeError(ctx, "Malformed reference: ivs_session");
    }
    if(argc > 0) {
        if(QJS_IS_NULL(argv[0])) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Invalid argument: filename\n");
            goto out;
        }
        file_to_send = JS_ToCString(ctx, argv[0]);
    }
    if(argc > 1) {
        fl_delete = JS_ToBool(ctx, argv[1]);
    }
    if(argc > 2) {
        fl_async = JS_ToBool(ctx, argv[2]);
    }

    if((status = switch_file_exists(file_to_send, NULL)) != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "File not found: %s\n", file_to_send);
        goto out;
    }

    if((status = chatgpt_conf_alloc(&chatgpt_conf)) != SWITCH_STATUS_SUCCESS) {
        switch_goto_status(SWITCH_STATUS_GENERR, out);
    }

    chatgpt_conf->ivs_session_ref = ivs_session;
    chatgpt_conf->file_to_send = switch_core_strdup(chatgpt_conf->pool, file_to_send);
    chatgpt_conf->fl_delete_file = fl_delete;
    chatgpt_conf->fl_log_http_errors = js_chatgpt->fl_log_http_errors;

    chatgpt_conf->curl_conf->url = CHATGPT_WHISPER_URL;
    chatgpt_conf->curl_conf->content_type = CHATGPT_WHISPER_TYPE;
    chatgpt_conf->curl_conf->curl_auth_type = CURLAUTH_BEARER;
    chatgpt_conf->curl_conf->request_timeout = js_chatgpt->request_timeout;
    chatgpt_conf->curl_conf->connect_timeout = js_chatgpt->connect_timeout;
    chatgpt_conf->curl_conf->credentials = safe_pool_strdup(chatgpt_conf->pool, js_chatgpt->apikey);
    chatgpt_conf->curl_conf->user_agent = safe_pool_strdup(chatgpt_conf->pool, js_chatgpt->user_agent);

    curl_field_add(chatgpt_conf->curl_conf, CURL_FIELD_TYPE_SIMPLE, "model", js_chatgpt->whisper_model);
    curl_field_add(chatgpt_conf->curl_conf, CURL_FIELD_TYPE_FILE, "file", (char *)file_to_send);

    if(fl_async) {
        uint32_t jid = whisper_request_exec_async(chatgpt_conf);
        ret_obj = (jid > 0 ? JS_NewInt32(ctx, jid) : JS_FALSE);
    } else {
        ivs_event_payload_transcription_t *res = whisper_request_exec(chatgpt_conf);
        if(res) {
            ret_obj = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, ret_obj, "confidence", JS_NewFloat64(ctx, res->confidence));
            JS_SetPropertyStr(ctx, ret_obj, "text", (res->text ? JS_NewString(ctx, res->text) : JS_UNDEFINED));

            ivs_event_payload_transcription_free(res);
            switch_safe_free(res);
        }
        chatgpt_conf_free(chatgpt_conf);
    }
out:
    JS_FreeCString(ctx, file_to_send);

    if(status != SWITCH_STATUS_SUCCESS) {
        if(chatgpt_conf) {
            chatgpt_conf_free(chatgpt_conf);
        }
    }
    return ret_obj;
}
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
static JSClassDef js_chatgpt_class = {
    CLASS_NAME,
    .finalizer = js_chatgpt_finalizer,
};

static const JSCFunctionListEntry js_chatgpt_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("apiKey", js_chatgpt_property_get, js_chatgpt_property_set, PROP_APIKEY),
    JS_CGETSET_MAGIC_DEF("chatApiUrl", js_chatgpt_property_get, js_chatgpt_property_set, PROP_CHAT_API_URL),
    JS_CGETSET_MAGIC_DEF("whisperApiUrl", js_chatgpt_property_get, js_chatgpt_property_set, PROP_WHISPER_API_URL),
    JS_CGETSET_MAGIC_DEF("chatModel", js_chatgpt_property_get, js_chatgpt_property_set, PROP_CHAT_MODEL),
    JS_CGETSET_MAGIC_DEF("whisperModel", js_chatgpt_property_get, js_chatgpt_property_set, PROP_WHISPER_MODEL),
    JS_CGETSET_MAGIC_DEF("requestTimeout", js_chatgpt_property_get, js_chatgpt_property_set, PROP_REQ_TIMEOUT),
    JS_CGETSET_MAGIC_DEF("connectTimeout", js_chatgpt_property_get, js_chatgpt_property_set, PROP_CON_TIMEOUT),
    JS_CGETSET_MAGIC_DEF("userAgent", js_chatgpt_property_get, js_chatgpt_property_set, PROP_USER_AGENT),
    JS_CGETSET_MAGIC_DEF("role", js_chatgpt_property_get, js_chatgpt_property_set, PROP_ROLE),
    JS_CGETSET_MAGIC_DEF("proxy", js_chatgpt_property_get, js_chatgpt_property_set, PROP_PROXY),
    JS_CGETSET_MAGIC_DEF("proxyCredentials", js_chatgpt_property_get, js_chatgpt_property_set, PROP_PROXY_CREDENTIALS),
    JS_CGETSET_MAGIC_DEF("logHttpErrors", js_chatgpt_property_get, js_chatgpt_property_set, PROP_LOG_HTTP_ERRORS),
    //
    JS_CFUNC_DEF("askChatGPT", 1, js_chatgpt_do_chat_request),
    JS_CFUNC_DEF("aksWhisper", 1, js_chatgpt_do_whisper_request),
};

static void js_chatgpt_finalizer(JSRuntime *rt, JSValue val) {
    js_chatgpt_t *js_chatgpt = JS_GetOpaque(val, js_lookup_classid(rt, CLASS_NAME));
    switch_memory_pool_t *pool = (js_chatgpt ? js_chatgpt->pool : NULL);

    if(!js_chatgpt) {
        return;
    }

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "js-chatgpt-finalizer: js_chatgpt=%p\n", js_chatgpt);

    if(pool) {
        switch_core_destroy_memory_pool(&pool);
        pool = NULL;
    }

    js_free_rt(rt, js_chatgpt);
}

static JSValue js_chatgpt_contructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
    JSValue obj = JS_UNDEFINED;
    JSValue err = JS_UNDEFINED;
    JSValue proto;
    js_chatgpt_t *js_chatgpt = NULL;
    switch_memory_pool_t *pool = NULL;
    const char *apikey = NULL, *chat_model = NULL, *whisper_model = NULL;

    if(argc < 1) {
        return JS_ThrowTypeError(ctx, "Invalid arguments");
    }

    apikey = JS_ToCString(ctx, argv[0]);
    if(zstr(apikey)) {
        err = JS_ThrowTypeError(ctx, "Invalid argument: apiKey");
        goto fail;
    }
    if(argc > 1) {
        chat_model = JS_ToCString(ctx, argv[1]);
    }
    if(argc > 2) {
        whisper_model = JS_ToCString(ctx, argv[2]);
    }

    if(switch_core_new_memory_pool(&pool) != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "pool fail\n");
        goto fail;
    }

    js_chatgpt = js_mallocz(ctx, sizeof(js_chatgpt_t));
    if(!js_chatgpt) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "mem fail\n");
        goto fail;
    }

    js_chatgpt->pool = pool;
    js_chatgpt->request_timeout = 0;
    js_chatgpt->connect_timeout = 5;
    js_chatgpt->apikey = switch_core_strdup(pool, apikey);
    js_chatgpt->role = "user";
    js_chatgpt->chat_model = chat_model ? switch_core_strdup(pool, chat_model) :  "gpt-3.5-turbo";
    js_chatgpt->whisper_model = whisper_model ? switch_core_strdup(pool, whisper_model) : "whisper-1";

    proto = JS_GetPropertyStr(ctx, new_target, "prototype");
    if(JS_IsException(proto)) { goto fail; }

    obj = JS_NewObjectProtoClass(ctx, proto, js_chatgpt_get_classid(ctx));
    JS_FreeValue(ctx, proto);
    if(JS_IsException(obj)) { goto fail; }

    JS_SetOpaque(obj, js_chatgpt);

    JS_FreeCString(ctx, apikey);
    JS_FreeCString(ctx, chat_model);
    JS_FreeCString(ctx, whisper_model);

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "js-chatgpt-constructor: js_chatgpt=%p\n", js_chatgpt);

    return obj;
fail:
    if(js_chatgpt) {
        js_free(ctx, js_chatgpt);
    }
    if(pool) {
        switch_core_destroy_memory_pool(&pool);
    }
    JS_FreeValue(ctx, obj);
    JS_FreeCString(ctx, apikey);
    JS_FreeCString(ctx, chat_model);
    JS_FreeCString(ctx, whisper_model);

    return (JS_IsUndefined(err) ? JS_EXCEPTION : err);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
JSClassID js_chatgpt_get_classid(JSContext *ctx) {
    return js_lookup_classid(JS_GetRuntime(ctx), CLASS_NAME);
}

switch_status_t js_chatgpt_class_register(JSContext *ctx, JSValue global_obj) {
    JSClassID class_id = 0;
    JSValue obj_proto, obj_class;

    class_id = js_chatgpt_get_classid(ctx);
    if(!class_id) {
        JS_NewClassID(&class_id);
        JS_NewClass(JS_GetRuntime(ctx), class_id, &js_chatgpt_class);

        if(js_register_classid(JS_GetRuntime(ctx), CLASS_NAME, class_id) != SWITCH_STATUS_SUCCESS) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Couldn't register class: %s (%i)\n", CLASS_NAME, (int) class_id);
        }
    }

    obj_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, obj_proto, js_chatgpt_proto_funcs, ARRAY_SIZE(js_chatgpt_proto_funcs));

    obj_class = JS_NewCFunction2(ctx, js_chatgpt_contructor, CLASS_NAME, 1, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, obj_class, obj_proto);
    JS_SetClassProto(ctx, class_id, obj_proto);

    JS_SetPropertyStr(ctx, global_obj, CLASS_NAME, obj_class);

    return SWITCH_STATUS_SUCCESS;
}
