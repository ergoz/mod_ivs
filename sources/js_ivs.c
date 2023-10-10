/**
 * (C)2023 aks
 * https://akscf.me/
 * https://github.com/akscf/
 **/
#include "ivs_qjs.h"
#include "js_ivs_wrp.h"

#define CLASS_NAME                  "IVS"
#define PROP_SID                    0
#define PROP_LANGUAGE               1
#define PROP_TTS_ENGINE             2
#define PROP_ASR_ENGINE             3
#define PROP_VAD_STATE              4
#define PROP_CHUNK_TYPE             5
#define PROP_CHUNK_FILE_TYPE         6

#define IVS_SESSION_SANITY_CHECK() if (!js_ivs || !js_ivs->session) { \
           return JS_ThrowTypeError(ctx, "Session is not initialized"); \
        }


static void js_ivs_finalizer(JSRuntime *rt, JSValue val);

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
static JSValue js_ivs_property_get(JSContext *ctx, JSValueConst this_val, int magic) {
    js_ivs_t *js_ivs = JS_GetOpaque2(ctx, this_val, js_ivs_get_classid(ctx));
    ivs_session_t *ivs_session = js_ivs->session;
    JSValue ret_val = JS_UNDEFINED;

    if(!js_ivs || !ivs_session) {
        return JS_UNDEFINED;
    }

    switch(magic) {
        case PROP_SID: {
            return JS_NewString(ctx, ivs_session->session_id);
        }
        case PROP_LANGUAGE: {
            ret_val = JS_NewString(ctx, ivs_session->language);
            return ret_val;
        }
        case PROP_TTS_ENGINE: {
            ret_val = JS_NewString(ctx, ivs_session->tts_engine);
            return ret_val;
        }
        case PROP_ASR_ENGINE: {
            ret_val = JS_NewString(ctx, ivs_session->asr_engine);
            return ret_val;
        }
        case PROP_CHUNK_TYPE: {
            if(ivs_session->chunk_type == IVS_CHUNK_TYPE_FILE) {
                return JS_NewString(ctx, "file");
            }
            if(ivs_session->chunk_type == IVS_CHUNK_TYPE_BUFFER) {
                return JS_NewString(ctx, "buffer");
            }
            return JS_UNDEFINED;
        }
        case PROP_CHUNK_FILE_TYPE: {
            return (ivs_session->chunk_file_ext ? JS_NewString(ctx, ivs_session->chunk_file_ext) : JS_UNDEFINED);
        }

        case PROP_VAD_STATE: {
            switch_vad_state_t vad_state = 0;
            vad_state = ivs_session->vad_state;

            if(vad_state == SWITCH_VAD_STATE_NONE) {
                return JS_NewString(ctx, "none");
            }
            if(vad_state == SWITCH_VAD_STATE_START_TALKING) {
                return JS_NewString(ctx, "start-talking");
            }
            if(vad_state == SWITCH_VAD_STATE_STOP_TALKING) {
                return JS_NewString(ctx, "stop-talking");
            }
            if(vad_state == SWITCH_VAD_STATE_TALKING) {
                return JS_NewString(ctx, "talking");
            }
            return JS_UNDEFINED;
        }
    }
    return JS_UNDEFINED;
}

static JSValue js_ivs_property_set(JSContext *ctx, JSValueConst this_val, JSValue val, int magic) {
    js_ivs_t *js_ivs = JS_GetOpaque2(ctx, this_val, js_ivs_get_classid(ctx));
    ivs_session_t *ivs_session = js_ivs->session;
    const char *str = NULL;
    int copy = 1, success = 0;

    if(!js_ivs || !ivs_session) {
        return JS_UNDEFINED;
    }

    switch(magic) {
        case PROP_LANGUAGE: {
            str = JS_ToCString(ctx, val);
            if(zstr(str)) {
                ivs_session->language = NULL;
            } else {
                if(!zstr(ivs_session->language)) { copy = strcmp(ivs_session->language, str); }
                if(copy) {
                    switch_mutex_lock(ivs_session->mutex);
                    ivs_session->language = switch_core_strdup(ivs_session->script->pool, str);
                    switch_mutex_unlock(ivs_session->mutex);
                }
            }
            JS_FreeCString(ctx, str);
            return JS_TRUE;
        }
        case PROP_TTS_ENGINE: {
            str = JS_ToCString(ctx, val);
            if(zstr(str)) {
                ivs_session->tts_engine = NULL;
            } else {
                if(!zstr(ivs_session->tts_engine)) { copy = strcmp(ivs_session->tts_engine, str); }
                if(copy) {
                    switch_mutex_lock(ivs_session->mutex);
                    ivs_session->tts_engine = switch_core_strdup(ivs_session->script->pool, str);
                    switch_mutex_unlock(ivs_session->mutex);
                }
            }
            JS_FreeCString(ctx, str);
            return JS_TRUE;
        }
        case PROP_ASR_ENGINE: {
            str = JS_ToCString(ctx, val);
            if(zstr(str)) {
                ivs_session->asr_engine = NULL;
            } else {
                if(!zstr(ivs_session->asr_engine)) { copy = strcmp(ivs_session->asr_engine, str); }
                if(copy) {
                    switch_mutex_lock(ivs_session->mutex);
                    ivs_session->asr_engine = switch_core_strdup(ivs_session->script->pool, str);
                    switch_mutex_unlock(ivs_session->mutex);
                }
            }
            JS_FreeCString(ctx, str);
            return JS_TRUE;
        }
        case PROP_CHUNK_TYPE: {
            str = JS_ToCString(ctx, val);
            if(!zstr(str)) {
                if(strcasecmp(str, "file") == 0) {
                    switch_mutex_lock(ivs_session->mutex);
                    ivs_session->chunk_type = IVS_CHUNK_TYPE_FILE;
                    switch_mutex_unlock(ivs_session->mutex);
                    success = 1;
                } else if(strcasecmp(str, "buffer") == 0) {
                    switch_mutex_lock(ivs_session->mutex);
                    ivs_session->chunk_type = IVS_CHUNK_TYPE_BUFFER;
                    switch_mutex_unlock(ivs_session->mutex);
                    success = 1;
                }
            }
            JS_FreeCString(ctx, str);
            return (success ? JS_TRUE : JS_FALSE);
        }
        case PROP_CHUNK_FILE_TYPE: {
            str = JS_ToCString(ctx, val);
            if(zstr(str)) {
                ivs_session->chunk_file_ext = NULL;
            } else {
                if(!zstr(ivs_session->chunk_file_ext)) { copy = strcmp(ivs_session->chunk_file_ext, str); }
                if(copy) {
                    switch_mutex_lock(ivs_session->mutex);
                    ivs_session->chunk_file_ext = switch_core_strdup(ivs_session->script->pool, str);
                    ivs_session->fl_chunk_file_ext_changed = true;
                    switch_mutex_unlock(ivs_session->mutex);
                }
            }
            JS_FreeCString(ctx, str);
            return (success ? JS_TRUE : JS_FALSE);
        }
    }
    return JS_FALSE;
}

// say("test", [async: true/false])
static JSValue js_ivs_say(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    js_ivs_t *js_ivs = JS_GetOpaque2(ctx, this_val, js_ivs_get_classid(ctx));
    ivs_session_t *ivs_session = js_ivs->session;
    const char *text = NULL;
    int fl_async = false;
    JSValue ret_val = JS_UNDEFINED;

    IVS_SESSION_SANITY_CHECK();

    if(!ivs_session) {
        return JS_ThrowTypeError(ctx, "Invalid ctx");
    }
    if(argc < 1) {
        return JS_ThrowTypeError(ctx, "Invalid arguments");
    }

    text = JS_ToCString(ctx, argv[0]);
    if(zstr(text)) {
        return JS_ThrowTypeError(ctx, "Invalid argument: text");
    }
    if(argc > 1) {
        fl_async = JS_ToBool(ctx, argv[1]);
    }

    if(fl_async) {
        uint32_t jid = js_ivs_async_say(ivs_session, NULL, text);
        ret_val = (jid > 0 ? JS_NewInt32(ctx, jid) : JS_FALSE);
    } else {
        switch_status_t st = ivs_say(ivs_session, NULL, (char *)text, false);
        ret_val = (st == SWITCH_STATUS_SUCCESS ? JS_TRUE : JS_FALSE);
    }

    JS_FreeCString(ctx, text);
    return ret_val;
}

// playback("file_to_play", [async: true/false], [delete_after_paly: true/false])
static JSValue js_ivs_playback(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    js_ivs_t *js_ivs = JS_GetOpaque2(ctx, this_val, js_ivs_get_classid(ctx));
    ivs_session_t *ivs_session = js_ivs->session;
    const char *path = NULL;
    int fl_async = false, fl_delete = false;
    JSValue ret_val = JS_UNDEFINED;

    IVS_SESSION_SANITY_CHECK();

    if(argc < 1) {
        return JS_ThrowTypeError(ctx, "Invalid arguments");
    }

    path = JS_ToCString(ctx, argv[0]);
    if(zstr(path)) {
        return JS_ThrowTypeError(ctx, "Invalid argument: filename");
    }
    if(argc > 1) {
        fl_async = JS_ToBool(ctx, argv[1]);
    }
    if(argc > 2) {
        fl_delete = JS_ToBool(ctx, argv[2]);
    }

    if(fl_async) {
        uint32_t jid = js_ivs_async_playback(ivs_session, path, fl_delete);
        ret_val = (jid > 0 ? JS_NewInt32(ctx, jid) : JS_FALSE);
    } else {
        switch_status_t st = ivs_playback(ivs_session, (char *)path, false);
        if(fl_delete) { unlink(path); }
        ret_val = (st == SWITCH_STATUS_SUCCESS ? JS_TRUE : JS_FALSE);
    }

    JS_FreeCString(ctx, path);
    return ret_val;
}

static JSValue js_ivs_playback_stop(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    js_ivs_t *js_ivs = JS_GetOpaque2(ctx, this_val, js_ivs_get_classid(ctx));
    ivs_session_t *ivs_session = js_ivs->session;

    IVS_SESSION_SANITY_CHECK();

    ivs_playback_stop(ivs_session);

    return JS_TRUE;
}

static JSValue js_ivs_get_event(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    js_ivs_t *js_ivs = JS_GetOpaque2(ctx, this_val, js_ivs_get_classid(ctx));
    ivs_session_t *ivs_session = js_ivs->session;
    JSValue ret_val = JS_FALSE;
    JSValue edata_obj = JS_FALSE;
    uint8_t fl_found = false;
    void *pop = NULL;

    IVS_SESSION_SANITY_CHECK();

    if(switch_queue_trypop(ivs_session->events, &pop) == SWITCH_STATUS_SUCCESS) {
        ivs_event_t *event = (ivs_event_t *)pop;
        if(event) {
            fl_found = true;
            ret_val = JS_NewObject(ctx);

            JS_SetPropertyStr(ctx, ret_val, "class",   JS_NewString(ctx, "IvsEvent"));
            JS_SetPropertyStr(ctx, ret_val, "jid",     JS_NewInt32(ctx, event->jid));
            switch(event->type) {
                case IVS_EVENT_NOP: {
                    JS_SetPropertyStr(ctx, ret_val, "type", JS_NewString(ctx, "nop"));
                    break;
                }
                case IVS_EVENT_SPEAKING_START: {
                    JS_SetPropertyStr(ctx, ret_val, "type", JS_NewString(ctx, "speaking-start"));
                    break;
                }
                case IVS_EVENT_SPEAKING_STOP: {
                    JS_SetPropertyStr(ctx, ret_val, "type", JS_NewString(ctx, "speaking-stop"));
                    break;
                }
                case IVS_EVENT_PLAYBACK_STARTED: {
                    edata_obj = JS_NewObject(ctx);
                    JS_SetPropertyStr(ctx, ret_val, "type", JS_NewString(ctx, "playback-started"));
                    JS_SetPropertyStr(ctx, ret_val, "data", edata_obj);
                    JS_SetPropertyStr(ctx, edata_obj, "file", JS_NewStringLen(ctx, event->payload, event->payload_len));
                    break;
                }
                case IVS_EVENT_PLAYBACK_FINISHED: {
                    edata_obj = JS_NewObject(ctx);
                    JS_SetPropertyStr(ctx, ret_val, "type", JS_NewString(ctx, "playback-finished"));
                    JS_SetPropertyStr(ctx, ret_val, "data", edata_obj);
                    JS_SetPropertyStr(ctx, edata_obj, "file", JS_NewStringLen(ctx, event->payload, event->payload_len));
                    break;
                }
                case IVS_EVENT_CHUNK_READY: {
                    ivs_event_payload_mchunk_t *payload = (ivs_event_payload_mchunk_t *)event->payload;
                    edata_obj = JS_NewObject(ctx);
                    JS_SetPropertyStr(ctx, ret_val, "type", JS_NewString(ctx, "chunk-ready"));
                    JS_SetPropertyStr(ctx, ret_val, "data", edata_obj);

                    if(payload) {
                        JS_SetPropertyStr(ctx, edata_obj, "time", JS_NewInt32(ctx, payload->time));
                        JS_SetPropertyStr(ctx, edata_obj, "length", JS_NewInt32(ctx, payload->length));
                        JS_SetPropertyStr(ctx, edata_obj, "samplerate", JS_NewInt32(ctx, payload->samplerate));
                        JS_SetPropertyStr(ctx, edata_obj, "channels", JS_NewInt32(ctx, payload->channels));
                        if(ivs_session->chunk_type == IVS_CHUNK_TYPE_FILE) {
                            JS_SetPropertyStr(ctx, edata_obj, "format", JS_NewString(ctx, "file"));
                            JS_SetPropertyStr(ctx, edata_obj, "file", JS_NewStringLen(ctx, payload->data, payload->data_len));
                        } else if(ivs_session->chunk_type == IVS_CHUNK_TYPE_BUFFER) {
                            JS_SetPropertyStr(ctx, edata_obj, "format", JS_NewString(ctx, "buffer"));
                            JS_SetPropertyStr(ctx, edata_obj, "buffer", JS_NewArrayBufferCopy(ctx, payload->data, payload->data_len));
                        }
                    }
                    break;
                }
                case IVS_EVENT_TRANSCRIPTION_DONE: {
                    ivs_event_payload_transcription_t *payload = (ivs_event_payload_transcription_t *)event->payload;
                    edata_obj = JS_NewObject(ctx);
                    JS_SetPropertyStr(ctx, ret_val, "type", JS_NewString(ctx, "transcription-done"));
                    JS_SetPropertyStr(ctx, ret_val, "data", edata_obj);

                    if(payload) {
                        JS_SetPropertyStr(ctx, edata_obj, "text", JS_NewString(ctx, payload->text));
                        JS_SetPropertyStr(ctx, edata_obj, "confidence", JS_NewFloat64(ctx, payload->confidence));
                    }
                    break;
                }
                case IVS_EVENT_NLP_DONE: {
                    ivs_event_payload_nlp_t *payload = (ivs_event_payload_nlp_t *)event->payload;
                    edata_obj = JS_NewObject(ctx);
                    JS_SetPropertyStr(ctx, ret_val, "type", JS_NewString(ctx, "nlp-done"));
                    JS_SetPropertyStr(ctx, ret_val, "data", edata_obj);

                    if(payload) {
                        JS_SetPropertyStr(ctx, edata_obj, "role", JS_NewString(ctx, payload->role));
                        JS_SetPropertyStr(ctx, edata_obj, "text", JS_NewString(ctx, payload->text));
                    }
                    break;
                }

                default:
                    JS_SetPropertyStr(ctx, ret_val, "type", JS_NewString(ctx, "unknown"));
                    break;
            }
        }
        ivs_event_free(event);
    }

    return (fl_found ? ret_val : JS_UNDEFINED);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
static JSClassDef js_ivs_class = {
    CLASS_NAME,
    .finalizer = js_ivs_finalizer,
};

static const JSCFunctionListEntry js_ivs_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("sessionId", js_ivs_property_get, js_ivs_property_set, PROP_SID),
    JS_CGETSET_MAGIC_DEF("language", js_ivs_property_get, js_ivs_property_set, PROP_LANGUAGE),
    JS_CGETSET_MAGIC_DEF("ttsEngine", js_ivs_property_get, js_ivs_property_set, PROP_TTS_ENGINE),
    JS_CGETSET_MAGIC_DEF("asrEngine", js_ivs_property_get, js_ivs_property_set, PROP_ASR_ENGINE),
    JS_CGETSET_MAGIC_DEF("vadState", js_ivs_property_get, js_ivs_property_set, PROP_VAD_STATE),
    JS_CGETSET_MAGIC_DEF("chunkType", js_ivs_property_get, js_ivs_property_set, PROP_CHUNK_TYPE),
    JS_CGETSET_MAGIC_DEF("chunkFileType", js_ivs_property_get, js_ivs_property_set, PROP_CHUNK_FILE_TYPE),
    //
    JS_CFUNC_DEF("say", 1, js_ivs_say),
    JS_CFUNC_DEF("playback", 1, js_ivs_playback),
    JS_CFUNC_DEF("playbackStop", 0, js_ivs_playback_stop),
    JS_CFUNC_DEF("getEvent", 0, js_ivs_get_event),
};

static void js_ivs_finalizer(JSRuntime *rt, JSValue val) {
    js_ivs_t *js_ivs = JS_GetOpaque(val, js_lookup_classid(rt, CLASS_NAME));

    if(!js_ivs) { return; }

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "js-ivs-finalizer: js-ivs=%p\n", js_ivs);

    if(!js_ivs->fl_destroyed) {
        js_ivs->fl_destroyed = false;

        if(js_ivs->session) {
            ivs_session_release(js_ivs->session);
        }
    }

    js_free_rt(rt, js_ivs);
}

static JSValue js_ivs_contructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
    JSValue obj = JS_UNDEFINED;
    JSValue err = JS_UNDEFINED;
    JSValue proto;
    js_ivs_t *js_ivs = NULL;
    ivs_session_t *ivs_session = NULL;
    const char *sid = NULL;

    if(argc < 1) {
        return JS_ThrowTypeError(ctx, "Invalid arguments");
    }

    sid = JS_ToCString(ctx, argv[0]);
    if(zstr(sid)) {
        return JS_ThrowTypeError(ctx, "Invalid argument: sid");
    }

    ivs_session = ivs_session_lookup((char *)sid, true);
    if(!ivs_session) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Session not found: %s\n", sid);
        goto fail;
    }

    js_ivs = js_mallocz(ctx, sizeof(js_ivs_t));
    if(!js_ivs) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "mem fail\n");
        goto fail;
    }

    js_ivs->session = ivs_session;

    proto = JS_GetPropertyStr(ctx, new_target, "prototype");
    if(JS_IsException(proto)) { goto fail; }

    obj = JS_NewObjectProtoClass(ctx, proto, js_ivs_get_classid(ctx));
    JS_FreeValue(ctx, proto);

    if(JS_IsException(obj)) { goto fail; }

    JS_SetOpaque(obj, js_ivs);
    JS_FreeCString(ctx, sid);

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "js-ivs-constructor: js-ivs=%p\n", js_ivs);

    return obj;
fail:
    if(js_ivs) {
        js_free(ctx, js_ivs);
    }
    JS_FreeValue(ctx, obj);
    JS_FreeCString(ctx, sid);
    return (JS_IsUndefined(err) ? JS_EXCEPTION : err);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
JSClassID js_ivs_get_classid(JSContext *ctx) {
    return js_lookup_classid(JS_GetRuntime(ctx), CLASS_NAME);
}

switch_status_t js_ivs_class_register(JSContext *ctx, JSValue global_obj) {
    JSClassID class_id = 0;
    JSValue obj_proto, obj_class;

    class_id = js_ivs_get_classid(ctx);
    if(!class_id) {
        JS_NewClassID(&class_id);
        JS_NewClass(JS_GetRuntime(ctx), class_id, &js_ivs_class);

        if(js_register_classid(JS_GetRuntime(ctx), CLASS_NAME, class_id) != SWITCH_STATUS_SUCCESS) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Couldn't register class: %s (%i)\n", CLASS_NAME, (int) class_id);
        }
    }

    obj_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, obj_proto, js_ivs_proto_funcs, ARRAY_SIZE(js_ivs_proto_funcs));

    obj_class = JS_NewCFunction2(ctx, js_ivs_contructor, CLASS_NAME, 1, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, obj_class, obj_proto);
    JS_SetClassProto(ctx, class_id, obj_proto);

    JS_SetPropertyStr(ctx, global_obj, CLASS_NAME, obj_class);

    return SWITCH_STATUS_SUCCESS;
}

JSValue js_ivs_object_create(JSContext *ctx, ivs_session_t *ivs_session) {
    js_ivs_t *js_ivs = NULL;
    JSValue obj, proto;

    js_ivs = js_mallocz(ctx, sizeof(js_ivs_t));
    if(!js_ivs) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "mem fail\n");
        return JS_EXCEPTION;
    }

    proto = JS_NewObject(ctx);
    if(JS_IsException(proto)) { return proto; }
    JS_SetPropertyFunctionList(ctx, proto, js_ivs_proto_funcs, ARRAY_SIZE(js_ivs_proto_funcs));

    obj = JS_NewObjectProtoClass(ctx, proto, js_ivs_get_classid(ctx));
    JS_FreeValue(ctx, proto);

    if(JS_IsException(obj)) { return obj; }

    js_ivs->ctx = ctx;
    js_ivs->session = ivs_session;
    JS_SetOpaque(obj, js_ivs);

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "js-ivs-obj-created: js_ivs=%p\n", js_ivs);

    return obj;
}
