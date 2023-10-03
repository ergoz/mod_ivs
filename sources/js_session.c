/**
 * lightweight version of session object
 *
 * (C)2023 aks
 * https://akscf.me/
 * https://github.com/akscf/
 **/
#include "ivs_qjs.h"

#define CLASS_NAME                  "Session"
#define PROP_NAME                   0
#define PROP_UUID                   1
#define PROP_STATE                  2
#define PROP_CAUSE                  3
#define PROP_CAUSECODE              4
#define PROP_CALLER_ID_NAME         5
#define PROP_CALLER_ID_NUMBER       6
#define PROP_PROFILE_DIALPLAN       7
#define PROP_PROFILE_DESTINATION    8
#define PROP_RCODEC_NAME            9
#define PROP_WCODEC_NAME            10
#define PROP_SAMPLERATE             11
#define PROP_CHANNELS               12
#define PROP_PTIME                  13
#define PROP_IS_READY               14
#define PROP_IS_ANSWERED            15
#define PROP_IS_MEDIA_READY         16

#define SESSION_SANITY_CHECK() if (!jss || !jss->session) { \
           return JS_ThrowTypeError(ctx, "Session is not initialized"); \
        }

#define CHANNEL_SANITY_CHECK() do { \
           if(!switch_channel_ready(channel)) { \
                return JS_ThrowTypeError(ctx, "Channel is not ready"); \
           } \
           if(!((switch_channel_test_flag(channel, CF_ANSWERED) || switch_channel_test_flag(channel, CF_EARLY_MEDIA)))) { \
                return JS_ThrowTypeError(ctx, "Session is not answered!"); \
           } \
        } while(0)

#define CHANNEL_SANITY_CHECK_ANSWER() do { \
         if (!switch_channel_ready(channel)) { \
            return JS_ThrowTypeError(ctx, "Session is not active!"); \
         }                                                                                                                               \
        } while(0)

#define CHANNEL_MEDIA_SANITY_CHECK() do { \
        if(!switch_channel_media_ready(channel)) { \
            return JS_ThrowTypeError(ctx, "Session is not in media mode!"); \
        } \
    } while(0)

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
typedef struct {
    js_session_t    *jss;
    JSValue         fh_obj;
    JSValue         jss_obj;
    JSValue         arg;
    JSValue         function;
    js_session_t    *jss_a;
    js_session_t    *jss_b;
    JSValue         jss_a_obj;
    JSValue         jss_b_obj;
} input_callback_state_t;


static void js_session_finalizer(JSRuntime *rt, JSValue val);
static JSValue js_session_contructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv);
static switch_status_t sys_session_hangup_hook(switch_core_session_t *session);

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
static JSValue js_session_property_get(JSContext *ctx, JSValueConst this_val, int magic) {
    js_session_t *jss = JS_GetOpaque2(ctx, this_val, js_seesion_get_classid(ctx));
    switch_channel_t *channel = NULL;
    switch_caller_profile_t *caller_profile = NULL;
    switch_codec_implementation_t read_impl = { 0 };

    if(magic == PROP_IS_READY) {
        uint8_t x = (jss && jss->session && switch_channel_ready(switch_core_session_get_channel(jss->session)));
        return(x ? JS_TRUE : JS_FALSE);
    }

    SESSION_SANITY_CHECK();

    channel = switch_core_session_get_channel(jss->session);
    caller_profile = switch_channel_get_caller_profile(channel);

    switch(magic) {
        case PROP_NAME:
            return JS_NewString(ctx, switch_channel_get_name(channel));

        case PROP_UUID:
            return JS_NewString(ctx, switch_channel_get_uuid(channel));

        case PROP_STATE:
            return JS_NewString(ctx, switch_channel_state_name(switch_channel_get_state(channel)) );

        case PROP_CAUSE:
            return JS_NewString(ctx, switch_channel_cause2str(switch_channel_get_cause(channel)) );

        case PROP_CAUSECODE:
            return JS_NewInt32(ctx, switch_channel_get_cause(channel));

        case PROP_CALLER_ID_NAME:
            if(!caller_profile) { return JS_UNDEFINED; }
            return JS_NewString(ctx, caller_profile->caller_id_name);

        case PROP_CALLER_ID_NUMBER:
            if(!caller_profile) { return JS_UNDEFINED; }
            return JS_NewString(ctx, caller_profile->caller_id_number);

        case PROP_PROFILE_DIALPLAN:
            if(!caller_profile) { return JS_UNDEFINED; }
            return JS_NewString(ctx, caller_profile->dialplan);

        case PROP_PROFILE_DESTINATION:
            if(!caller_profile) { return JS_UNDEFINED; }
            return JS_NewString(ctx, caller_profile->destination_number);

        case PROP_SAMPLERATE:
            switch_core_session_get_read_impl(jss->session, &read_impl);
            return JS_NewInt32(ctx, read_impl.samples_per_second);

        case PROP_CHANNELS:
            switch_core_session_get_read_impl(jss->session, &read_impl);
            return JS_NewInt32(ctx, read_impl.number_of_channels);

        case PROP_PTIME:
            switch_core_session_get_read_impl(jss->session, &read_impl);
            return JS_NewInt32(ctx, read_impl.microseconds_per_packet / 1000);

        case PROP_RCODEC_NAME: {
            const char *name = switch_channel_get_variable(channel, "read_codec");
            if(!zstr(name)) { JS_NewString(ctx, name); }
            return JS_UNDEFINED;
        }

        case PROP_WCODEC_NAME: {
            const char *name = switch_channel_get_variable(channel, "write_codec");
            if(!zstr(name)) { JS_NewString(ctx, name); }
            return JS_UNDEFINED;
        }

        case PROP_IS_ANSWERED: {
            return (switch_channel_test_flag(switch_core_session_get_channel(jss->session), CF_ANSWERED) ? JS_TRUE : JS_FALSE);
        }

        case PROP_IS_MEDIA_READY: {
            return (switch_channel_media_ready(switch_core_session_get_channel(jss->session)) ? JS_TRUE : JS_FALSE);
        }
    }
    return JS_UNDEFINED;
}

static JSValue js_session_property_set(JSContext *ctx, JSValueConst this_val, JSValue val, int magic) {
    js_session_t *jss = JS_GetOpaque2(ctx, this_val, js_seesion_get_classid(ctx));

    return JS_FALSE;
}

static JSValue js_session_set_hangup_hook(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    js_session_t *jss = JS_GetOpaque2(ctx, this_val, js_seesion_get_classid(ctx));
    switch_channel_t *channel = NULL;

    SESSION_SANITY_CHECK();
    channel = switch_core_session_get_channel(jss->session);

    if(argc < 1) {
        return JS_ThrowTypeError(ctx, "Invalid argument: callback function");
    }

    if(JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) {
        jss->fl_hup_hook = SWITCH_FALSE;

    } else if(JS_IsFunction(ctx, argv[0])) {
        jss->on_hangup = argv[0];
        jss->fl_hup_hook = SWITCH_TRUE;

        switch_channel_set_private(channel, "jss", jss);
        switch_core_event_hook_add_state_change(jss->session, sys_session_hangup_hook);
    }

    return JS_TRUE;
}

static JSValue js_session_set_auto_hangup(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    js_session_t *jss = JS_GetOpaque2(ctx, this_val, js_seesion_get_classid(ctx));
    switch_channel_t *channel = NULL;

    SESSION_SANITY_CHECK();

    if(argc > 0) {
        jss->fl_hup_auto = JS_ToBool(ctx, argv[0]);
    }
    return JS_TRUE;
}

static JSValue js_session_flush_dtmf(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    js_session_t *jss = JS_GetOpaque2(ctx, this_val, js_seesion_get_classid(ctx));
    switch_channel_t *channel = NULL;

    SESSION_SANITY_CHECK();
    channel = switch_core_session_get_channel(jss->session);

    switch_channel_flush_dtmf(channel);

    return JS_TRUE;
}

static JSValue js_session_set_var(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    js_session_t *jss = JS_GetOpaque2(ctx, this_val, js_seesion_get_classid(ctx));
    switch_channel_t *channel = NULL;

    SESSION_SANITY_CHECK();

    channel = switch_core_session_get_channel(jss->session);
    if(argc >= 2) {
        const char *var, *val;

        var = JS_ToCString(ctx, argv[0]);
        val = JS_ToCString(ctx, argv[1]);

        switch_channel_set_variable_var_check(channel, var, val, SWITCH_FALSE);

        JS_FreeCString(ctx, var);
        JS_FreeCString(ctx, val);

        return JS_TRUE;
    }

    return JS_FALSE;
}

static JSValue js_session_get_var(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    js_session_t *jss = JS_GetOpaque2(ctx, this_val, js_seesion_get_classid(ctx));
    switch_channel_t *channel = NULL;

    SESSION_SANITY_CHECK();
    channel = switch_core_session_get_channel(jss->session);

    if(argc >= 1) {
        const char *var;
        const char *val;

        var = JS_ToCString(ctx, argv[0]);
        val = switch_channel_get_variable(channel, var);
        JS_FreeCString(ctx, var);

        if(val) {
            if(strcasecmp(val, "true") == 0) {
                return JS_TRUE;
            } else if(strcasecmp(val, "false") == 0) {
                return JS_FALSE;
            } else {
                return JS_NewString(ctx, val);
            }
        }
    }

    return JS_UNDEFINED;
}

static JSValue js_session_answer(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    js_session_t *jss = JS_GetOpaque2(ctx, this_val, js_seesion_get_classid(ctx));
    switch_channel_t *channel = NULL;

    SESSION_SANITY_CHECK();
    channel = switch_core_session_get_channel(jss->session);
    CHANNEL_SANITY_CHECK_ANSWER();

    switch_channel_answer(channel);

    return JS_TRUE;
}

static JSValue js_session_pre_answer(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    js_session_t *jss = JS_GetOpaque2(ctx, this_val, js_seesion_get_classid(ctx));
    switch_channel_t *channel = NULL;

    SESSION_SANITY_CHECK();
    channel = switch_core_session_get_channel(jss->session);
    CHANNEL_SANITY_CHECK_ANSWER();

    switch_channel_pre_answer(channel);

    return JS_TRUE;
}

static JSValue js_session_hangup(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    js_session_t *jss = JS_GetOpaque2(ctx, this_val, js_seesion_get_classid(ctx));
    switch_channel_t *channel = NULL;
    const char *cause_name = NULL;
    switch_call_cause_t cause = SWITCH_CAUSE_NORMAL_CLEARING;

    SESSION_SANITY_CHECK();
    channel = switch_core_session_get_channel(jss->session);

    if(switch_channel_up(channel)) {
        if(argc > 1) {
            if(JS_IsNumber(argv[0])) {
                int32_t i = 0;
                JS_ToUint32(ctx, &i, argv[0]);
                cause = i;
            } else {
                cause_name = JS_ToCString(ctx, argv[0]);
                cause = switch_channel_str2cause(cause_name);
                JS_FreeCString(ctx, cause_name);
            }
        }
        switch_channel_hangup(channel, cause);
        switch_core_session_kill_channel(jss->session, SWITCH_SIG_KILL);

        return JS_TRUE;
    }

    return JS_FALSE;
}

static JSValue js_session_execute(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    js_session_t *jss = JS_GetOpaque2(ctx, this_val, js_seesion_get_classid(ctx));
    switch_channel_t *channel = NULL;
    JSValue result = JS_FALSE;

    SESSION_SANITY_CHECK();
    channel = switch_core_session_get_channel(jss->session);

    if(argc > 0) {
        switch_application_interface_t *application_interface;
        const char *app_name = JS_ToCString(ctx, argv[0]);
        const char *app_arg = NULL;

        if(argc > 1) {
            app_arg = JS_ToCString(ctx, argv[1]);
        }

        if((application_interface = switch_loadable_module_get_application_interface(app_name))) {
            if(application_interface->application_function) {
                switch_core_session_exec(jss->session, application_interface, app_arg);
                UNPROTECT_INTERFACE(application_interface);
                result = JS_TRUE;
            }
        }
        JS_FreeCString(ctx, app_name);
        JS_FreeCString(ctx, app_arg);
    }

    return result;
}

static JSValue js_session_sleep(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    js_session_t *jss = JS_GetOpaque2(ctx, this_val, js_seesion_get_classid(ctx));
    switch_channel_t *channel = NULL;
    input_callback_state_t cb_state = { 0 };
    switch_input_callback_function_t dtmf_func = NULL;
    switch_input_args_t args = { 0 };
    int len = 0, msec = 0;
    void *bp = NULL;

    SESSION_SANITY_CHECK();
    channel = switch_core_session_get_channel(jss->session);
    CHANNEL_SANITY_CHECK();
    CHANNEL_MEDIA_SANITY_CHECK();

    if(argc > 0) {
        JS_ToUint32(ctx, &msec, argv[0]);
    }
    if(msec <= 0) {
        return JS_FALSE;
    }

    /*if(argc > 1 && JS_IsFunction(ctx, argv[1]) ) {
        memset(&cb_state, 0, sizeof(cb_state));
        cb_state.jss_obj = this_val;
        cb_state.jss = jss;
        cb_state.arg = (argc > 2 ? argv[2] : JS_UNDEFINED);
        cb_state.function = argv[1];

        dtmf_func = js_collect_input_callback;
        bp = &cb_state;
        len = sizeof(cb_state);
    }*/

    args.input_callback = dtmf_func;
    args.buf = bp;
    args.buflen = len;

    switch_ivr_sleep(jss->session, msec, SWITCH_FALSE, &args);

    return JS_TRUE;
}

static JSValue js_session_gen_tones(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    js_session_t *jss = JS_GetOpaque2(ctx, this_val, js_seesion_get_classid(ctx));
    switch_channel_t *channel = NULL;
    input_callback_state_t cb_state = { 0 };
    switch_input_callback_function_t dtmf_func = NULL;
    switch_input_args_t args = { 0 };
    const char *tone_script = NULL;
    int len = 0, loops = 0;
    char *buf = NULL, *p = NULL;
    void *bp = NULL;

    SESSION_SANITY_CHECK();
    channel = switch_core_session_get_channel(jss->session);
    CHANNEL_SANITY_CHECK();

    if(argc < 1) {
        return JS_ThrowTypeError(ctx, "Invalid arguments");
    }

    tone_script = JS_ToCString(ctx, argv[0]);
    if(zstr(tone_script)) {
        return JS_ThrowTypeError(ctx, "Invalid argument: toneScript");
    }

    buf = switch_core_session_strdup(jss->session, tone_script);
    JS_FreeCString(ctx, tone_script);

    if((p = strchr(buf, '|'))) {
        *p++ = '\0';
        loops = atoi(p);
        if(loops < 0) { loops = -1; }
    }

    /*if(argc > 1 && JS_IsFunction(ctx, argv[1]) ) {
        memset(&cb_state, 0, sizeof(cb_state));
        cb_state.jss_obj = this_val;
        cb_state.jss = jss;
        cb_state.arg = (argc > 2 ? argv[2] : JS_UNDEFINED);
        cb_state.function = argv[1];

        dtmf_func = js_collect_input_callback;
        bp = &cb_state;
        len = sizeof(cb_state);
    }*/

    args.input_callback = dtmf_func;
    args.buf = bp;
    args.buflen = len;

    switch_channel_set_variable(channel, SWITCH_PLAYBACK_TERMINATOR_USED, "");
    switch_ivr_gentones(jss->session, tone_script, loops, &args);

    return JS_TRUE;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// handlers
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
static switch_status_t sys_session_hangup_hook(switch_core_session_t *session) {
    switch_channel_t *channel = switch_core_session_get_channel(session);
    switch_channel_state_t state = switch_channel_get_state(channel);
    js_session_t *jss = NULL;
    JSContext *ctx = NULL;
    JSValue args[1] = { 0 };
    JSValue ret_val;

    if(state == CS_HANGUP || state == CS_ROUTING) {
        if((jss = switch_channel_get_private(channel, "jss"))) {
            ctx = jss->ctx;
            if(jss->fl_hup_hook && JS_IsFunction(ctx, jss->on_hangup)) {
                args[0] = JS_NewInt32(ctx, state);
                ret_val = JS_Call(ctx, jss->on_hangup, JS_UNDEFINED, 1, (JSValueConst *) args);
                if(JS_IsException(ret_val)) {
                    js_dump_error(NULL, ctx);
                    JS_ResetUncatchableError(ctx);
                }
                JS_FreeValue(ctx, args[0]);
            }
        }
    }

    return SWITCH_STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
static JSClassDef js_session_class = {
    CLASS_NAME,
    .finalizer = js_session_finalizer,
};

static const JSCFunctionListEntry js_session_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("name", js_session_property_get, js_session_property_set, PROP_NAME),
    JS_CGETSET_MAGIC_DEF("uuid", js_session_property_get, js_session_property_set, PROP_UUID),
    JS_CGETSET_MAGIC_DEF("state", js_session_property_get, js_session_property_set, PROP_STATE),
    JS_CGETSET_MAGIC_DEF("cause", js_session_property_get, js_session_property_set, PROP_CAUSE),
    JS_CGETSET_MAGIC_DEF("causecode", js_session_property_get, js_session_property_set, PROP_CAUSECODE),
    JS_CGETSET_MAGIC_DEF("dialplan", js_session_property_get, js_session_property_set, PROP_PROFILE_DIALPLAN),
    JS_CGETSET_MAGIC_DEF("destination", js_session_property_get, js_session_property_set, PROP_PROFILE_DESTINATION),
    JS_CGETSET_MAGIC_DEF("callerIdName", js_session_property_get, js_session_property_set, PROP_CALLER_ID_NAME),
    JS_CGETSET_MAGIC_DEF("callerIdNumber", js_session_property_get, js_session_property_set, PROP_CALLER_ID_NUMBER),
    JS_CGETSET_MAGIC_DEF("readCodecName", js_session_property_get, js_session_property_set, PROP_RCODEC_NAME),
    JS_CGETSET_MAGIC_DEF("writeCodecName", js_session_property_get, js_session_property_set, PROP_WCODEC_NAME),
    JS_CGETSET_MAGIC_DEF("samplerate", js_session_property_get, js_session_property_set, PROP_SAMPLERATE),
    JS_CGETSET_MAGIC_DEF("channels", js_session_property_get, js_session_property_set, PROP_CHANNELS),
    JS_CGETSET_MAGIC_DEF("ptime", js_session_property_get, js_session_property_set, PROP_PTIME),
    JS_CGETSET_MAGIC_DEF("isReady", js_session_property_get, js_session_property_set, PROP_IS_READY),
    JS_CGETSET_MAGIC_DEF("isAnswered", js_session_property_get, js_session_property_set, PROP_IS_ANSWERED),
    JS_CGETSET_MAGIC_DEF("isMediaReady", js_session_property_get, js_session_property_set, PROP_IS_MEDIA_READY),
    //
    JS_CFUNC_DEF("setHangupHook", 1, js_session_set_hangup_hook),
    JS_CFUNC_DEF("setAutoHangup", 1, js_session_set_auto_hangup),
    JS_CFUNC_DEF("flushDTMF", 1, js_session_flush_dtmf),
    JS_CFUNC_DEF("setVariable", 2, js_session_set_var),
    JS_CFUNC_DEF("getVariable", 1, js_session_get_var),
    JS_CFUNC_DEF("answer", 0, js_session_answer),
    JS_CFUNC_DEF("preAnswer", 0, js_session_pre_answer),
    JS_CFUNC_DEF("hangup", 0, js_session_hangup),
    JS_CFUNC_DEF("execute", 0, js_session_execute),
    JS_CFUNC_DEF("sleep", 1, js_session_sleep),
    JS_CFUNC_DEF("genTones", 1, js_session_gen_tones)
};

static void js_session_finalizer(JSRuntime *rt, JSValue val) {
    js_session_t *jss = JS_GetOpaque(val, js_lookup_classid(rt, CLASS_NAME));

    if(!jss) {
        return;
    }

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "js-session-finalizer: jss=%p, session=%p\n", jss, jss->session);

    if(jss->session) {
        switch_channel_t *channel = switch_core_session_get_channel(jss->session);

        switch_channel_set_private(channel, "jss", NULL);
        switch_core_event_hook_remove_state_change(jss->session, sys_session_hangup_hook);

        if(jss->fl_hup_auto) {
            switch_channel_hangup(channel, SWITCH_CAUSE_NORMAL_CLEARING);
        }

        switch_core_session_rwunlock(jss->session);
    }

    js_free_rt(rt, jss);
}

static JSValue js_session_contructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
    JSValue obj = JS_UNDEFINED;
    JSValue proto, error;
    js_session_t *jss = NULL;
    js_session_t *jss_old = NULL;
    switch_call_cause_t h_cause;
    uint8_t has_error = 0;
    const char *uuid;

    jss = js_mallocz(ctx, sizeof(js_session_t));
    if(!jss) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "mem fail\n");
        return JS_EXCEPTION;
    }

    if(argc > 0) {
        uuid = JS_ToCString(ctx, argv[0]);
        if(!strchr(uuid, '/')) {
            jss->session = switch_core_session_locate(uuid);
            jss->fl_hup_auto = SWITCH_TRUE;
        } else {
             if(argc > 1) {
                if(JS_IsObject(argv[1])) {
                    jss_old = JS_GetOpaque2(ctx, argv[1], js_seesion_get_classid(ctx));
                }
                if(switch_ivr_originate((jss_old ? jss_old->session : NULL), &jss->session, &h_cause, uuid, 60, NULL, NULL, NULL, NULL, NULL, SOF_NONE, NULL, NULL) == SWITCH_STATUS_SUCCESS) {
                    jss->fl_hup_auto = SWITCH_TRUE;
                    switch_channel_set_state(switch_core_session_get_channel(jss->session), CS_SOFT_EXECUTE);
                    switch_channel_wait_for_state_timeout(switch_core_session_get_channel(jss->session), CS_SOFT_EXECUTE, 5000);
                } else {
                    has_error = SWITCH_TRUE;
                    error = JS_ThrowTypeError(ctx, "Originate failed: %s", switch_channel_cause2str(h_cause));
                    goto fail;
                }
            }
            JS_FreeCString(ctx, uuid);
        }
    }

    proto = JS_GetPropertyStr(ctx, new_target, "prototype");
    if(JS_IsException(proto)) { goto fail; }

    obj = JS_NewObjectProtoClass(ctx, proto, js_seesion_get_classid(ctx));
    JS_FreeValue(ctx, proto);
    if(JS_IsException(obj)) { goto fail; }

    jss->ctx = ctx;
    JS_SetOpaque(obj, jss);

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "js-session-constructor: jss=%p, session=%p\n", jss, jss->session);

    return obj;
fail:
    if(jss) {
        js_free(ctx, jss);
    }
    JS_FreeValue(ctx, obj);
    return (has_error ? error : JS_EXCEPTION);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
JSClassID js_seesion_get_classid(JSContext *ctx) {
    return js_lookup_classid(JS_GetRuntime(ctx), CLASS_NAME);
}

switch_status_t js_session_class_register(JSContext *ctx, JSValue global_obj) {
    JSClassID class_id = 0;
    JSValue obj_proto, obj_class;

    class_id = js_seesion_get_classid(ctx);
    if(!class_id) {
        JS_NewClassID(&class_id);
        JS_NewClass(JS_GetRuntime(ctx), class_id, &js_session_class);

        if(js_register_classid(JS_GetRuntime(ctx), CLASS_NAME, class_id) != SWITCH_STATUS_SUCCESS) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Couldn't register class: %s (%i)\n", CLASS_NAME, (int) class_id);
        }
    }

    obj_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, obj_proto, js_session_proto_funcs, ARRAY_SIZE(js_session_proto_funcs));

    obj_class = JS_NewCFunction2(ctx, js_session_contructor, CLASS_NAME, 2, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, obj_class, obj_proto);
    JS_SetClassProto(ctx, class_id, obj_proto);

    JS_SetPropertyStr(ctx, global_obj, CLASS_NAME, obj_class);

    return SWITCH_STATUS_SUCCESS;
}

JSValue js_session_object_create(JSContext *ctx, switch_core_session_t *session) {
    js_session_t *jss = NULL;
    JSValue obj, proto;

    jss = js_mallocz(ctx, sizeof(js_session_t));
    if(!jss) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "mem fail\n");
        return JS_EXCEPTION;
    }

    proto = JS_NewObject(ctx);
    if(JS_IsException(proto)) { return proto; }
    JS_SetPropertyFunctionList(ctx, proto, js_session_proto_funcs, ARRAY_SIZE(js_session_proto_funcs));

    obj = JS_NewObjectProtoClass(ctx, proto, js_seesion_get_classid(ctx));
    JS_FreeValue(ctx, proto);

    if(JS_IsException(obj)) { return obj; }

    jss->ctx = ctx;
    jss->session = session;
    JS_SetOpaque(obj, jss);

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "js-session-obj-created: jss=%p, session=%p\n", jss, jss->session);

    return obj;
}

