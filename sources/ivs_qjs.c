/**
 * (C)2023 aks
 * https://akscf.me/
 * https://github.com/akscf/
 **/
#include "mod_ivs.h"
#include "ivs_curl.h"
#include "ivs_playback.h"
#include "ivs_events.h"
#include "ivs_qjs.h"

extern globals_t globals;

static switch_status_t script_load(ivs_script_t *script);

static JSValue js_is_interrupted(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_console_log(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_msleep(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_global_set(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_global_get(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_exit(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_include(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_api_execute(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_unlink(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);

// -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
void *SWITCH_THREAD_FUNC script_maintenance_thread(switch_thread_t *thread, void *obj) {
    volatile ivs_session_t *_ref = (ivs_session_t *) obj;
    ivs_session_t *ivs_session = (ivs_session_t *) _ref;
    switch_core_session_t *session = ivs_session->session;
    ivs_script_t *script = ivs_session->script;
    switch_memory_pool_t *pool = script->pool;
    JSValue global_obj, script_obj, ivs_obj, argc_obj, argv_obj, result;
    JSContext *ctx = NULL;
    JSRuntime *rt = NULL;

    if(!(rt = JS_NewRuntime())) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't create jsRuntime\n");
        goto out;
    }

    if(!(ctx = JS_NewContext(rt))) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't create jsCtx\n");
        goto out;
    }

    //JS_SetMemoryLimit(rt, globals.cfg_rt_mem_limit);
    //JS_SetMaxStackSize(rt, globals.cfg_rt_stk_size);

    JS_SetCanBlock(rt, 1);
    JS_SetRuntimeInfo(rt, script->name);
    JS_SetRuntimeOpaque(rt, ivs_session);
    JS_SetContextOpaque(ctx, ivs_session);

    global_obj = JS_GetGlobalObject(ctx);

    js_ivs_class_register(ctx, global_obj);
    js_session_class_register(ctx, global_obj);
    js_file_class_register(ctx, global_obj);
    js_curl_class_register(ctx, global_obj);
    js_chatgpt_class_register(ctx, global_obj);

    script_obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, script_obj, "id",   JS_NewString(ctx, ivs_session->session_id));
    JS_SetPropertyStr(ctx, script_obj, "name", JS_NewString(ctx, script->name));
    JS_SetPropertyStr(ctx, script_obj, "path", JS_NewString(ctx, script->path));
    JS_SetPropertyStr(ctx, script_obj, "isInterrupted", JS_NewCFunction(ctx, js_is_interrupted, "isInterrupted", 0));
    JS_SetPropertyStr(ctx, global_obj, "script", script_obj);

    if(!zstr(script->args)) {
        char *argv[512] = { 0 };
        int argc = 0;

        argc = switch_separate_string(script->args, ' ', argv, (sizeof(argv) / sizeof(argv[0])));
        argc_obj = JS_NewInt32(ctx, argc);
        argv_obj = JS_NewArray(ctx);

        if(argc) {
            for (int i = 0; i < argc; i++) {
                JS_SetPropertyUint32(ctx, argv_obj, (uint32_t) i, JS_NewString(ctx, argv[i]));
            }
        }
        JS_SetPropertyStr(ctx, global_obj, "argc", argc_obj);
        JS_SetPropertyStr(ctx, global_obj, "argv", argv_obj);
    } else {
        JS_SetPropertyStr(ctx, global_obj, "argc", JS_NewInt32(ctx, 0));
        JS_SetPropertyStr(ctx, global_obj, "argv", JS_NewArray(ctx));
    }

    JS_SetPropertyStr(ctx, global_obj, "consoleLog", JS_NewCFunction(ctx, js_console_log, "consoleLog", 0));
    JS_SetPropertyStr(ctx, global_obj, "include", JS_NewCFunction(ctx, js_include, "include", 1));
    JS_SetPropertyStr(ctx, global_obj, "msleep", JS_NewCFunction(ctx, js_msleep, "msleep", 1));
    JS_SetPropertyStr(ctx, global_obj, "exit", JS_NewCFunction(ctx, js_exit, "exit", 1));
    JS_SetPropertyStr(ctx, global_obj, "setGlobalVariable", JS_NewCFunction(ctx, js_global_set, "setGlobalVariable", 2));
    JS_SetPropertyStr(ctx, global_obj, "getGlobalVariable", JS_NewCFunction(ctx, js_global_get, "getGlobalVariable", 2));
    JS_SetPropertyStr(ctx, global_obj, "apiExecute", JS_NewCFunction(ctx, js_api_execute, "apiExecute", 2));
    JS_SetPropertyStr(ctx, global_obj, "unlink", JS_NewCFunction(ctx, js_unlink, "unlink", 1));

    while(!ivs_session->fl_ready) {
        switch_yield(10000);
    }
    if(globals.fl_shutdown || ivs_session->fl_do_destroy || ivs_session->fl_destroyed) {
        goto out;
    }

    // ivs
    if(ivs_session_take(ivs_session)) {
        ivs_obj = js_ivs_object_create(ctx, ivs_session);
        if(JS_IsException(ivs_obj)) {
            JS_ResetUncatchableError(ctx);
            goto out;
        }
        JS_SetPropertyStr(ctx, global_obj, "ivs", ivs_obj);
    } else {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't create IVS object\n");
        goto out;
    }

    // sip session
    if(ivs_session->session) {
        uint32_t tbuf2_len = 0;
        char *tbuf1 = NULL, *tbuf2 = NULL;

        switch_channel_t *channel = switch_core_session_get_channel(ivs_session->session);
        tbuf1 = switch_mprintf("var session = new Session('%s'); \n", switch_channel_get_uuid(channel));
        tbuf2_len = (script->body_len + strlen(tbuf1));

        switch_zmalloc(tbuf2, tbuf2_len + 1);
        memcpy(tbuf2, tbuf1, strlen(tbuf1));
        memcpy(tbuf2 + strlen(tbuf1), script->body, script->body_len);

        result = JS_Eval(ctx, tbuf2, tbuf2_len, script->name, JS_EVAL_TYPE_GLOBAL | JS_EVAL_TYPE_MODULE);
    } else {
        result = JS_Eval(ctx, script->body, script->body_len, script->name, JS_EVAL_TYPE_GLOBAL | JS_EVAL_TYPE_MODULE);
    }

    if(JS_IsException(result)) {
        js_dump_error(script, ctx);
        JS_ResetUncatchableError(ctx);
    }

    JS_FreeValue(ctx, result);
out:

    script->fl_destroyed = true;

    JS_FreeValue(ctx, global_obj);

    if(ctx) {
        JS_FreeContext(ctx);
    }
    if(rt) {
        JS_FreeRuntime(rt);
    }

    ivs_session_release(ivs_session);

    thread_finished();
    return NULL;
}

// -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
switch_status_t js_script_init(ivs_session_t *ivs_session, char *script_path, char *script_args) {
    switch_status_t status = SWITCH_STATUS_SUCCESS;
    ivs_script_t *script = ivs_session->script;

    if(!script) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "script == NULL\n");
        switch_goto_status(SWITCH_STATUS_FALSE, out);
    }

    // pool only for script maintenance thread
    if(switch_core_new_memory_pool(&script->pool) != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "mem fail (pool)\n");
        switch_goto_status(SWITCH_STATUS_GENERR, out);
    }

    script->id = ivs_session->session_id;
    script->path = switch_core_strdup(script->pool, script_path);
    script->name = basename(script->path);
    script->args = (script_args ? switch_core_strdup(script->pool, script_args) : NULL);

    switch_core_hash_init(&script->classes_map);
    switch_mutex_init(&script->mutex_classes_map, SWITCH_MUTEX_NESTED, script->pool);

    if(script_load(script) != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Couldn't load script\n");
        switch_goto_status(SWITCH_STATUS_FALSE, out);
    }

out:
    return status;
}

switch_status_t js_script_destroy(ivs_session_t *ivs_session) {
    ivs_script_t *script = (ivs_session ? ivs_session->script : NULL);

    if(script) {
        if(!script->fl_destroyed) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Waiting maintenance thread...\n");
            while(!script->fl_destroyed) {
                switch_yield(100000);
            }
        }
        if(script->classes_map){
            switch_core_hash_destroy(&script->classes_map);
        }
        if(script->pool) {
            switch_core_destroy_memory_pool(&script->pool);
        }
    }

    return SWITCH_STATUS_SUCCESS;
}

switch_status_t js_register_classid(JSRuntime *rt, const char *class_name, JSClassID class_id) {
    switch_status_t status = SWITCH_STATUS_SUCCESS;
    ivs_session_t *ivs_seesion = NULL;
    ivs_script_t *script = NULL;
    JSClassID *ptr = NULL;

    switch_assert(rt);

    ivs_seesion = JS_GetRuntimeOpaque(rt);
    script = (ivs_seesion ? ivs_seesion->script : NULL);

    switch_assert(script);

    if(!(ptr = switch_core_alloc(script->pool, sizeof(JSClassID)))) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "mem fail\n");
        switch_goto_status(SWITCH_STATUS_GENERR, out);
    }
    memcpy(ptr, &class_id, sizeof(JSClassID));

    switch_mutex_lock(script->mutex_classes_map);
    switch_core_hash_insert(script->classes_map, class_name, ptr);
    switch_mutex_unlock(script->mutex_classes_map);
out:
    return status;
}

JSClassID js_lookup_classid(JSRuntime *rt, const char *class_name) {
    JSClassID *ptr = NULL, id = 0;
    ivs_session_t *ivs_seesion = NULL;
    ivs_script_t *script = NULL;

    switch_assert(rt);

    ivs_seesion = JS_GetRuntimeOpaque(rt);
    script = (ivs_seesion ? ivs_seesion->script : NULL);

    switch_assert(script);

    switch_mutex_lock(script->mutex_classes_map);
    ptr = switch_core_hash_find(script->classes_map, class_name);
    if(ptr) { id = (JSClassID) *ptr; }
    switch_mutex_unlock(script->mutex_classes_map);

    return id;
}

// -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// js functions
// -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
static JSValue js_is_interrupted(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    ivs_session_t *ivs_session = JS_GetContextOpaque(ctx);
    uint32_t fl_srdy = false;

    if(!ivs_session || !ivs_session->script) {
        return JS_FALSE;
    }

    fl_srdy = switch_channel_ready(switch_core_session_get_channel(ivs_session->session));
    return (ivs_session->script->fl_interrupt || ivs_session->fl_do_destroy || globals.fl_shutdown || !fl_srdy ? JS_TRUE : JS_FALSE);
}

static JSValue js_console_log(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    switch_log_level_t level = SWITCH_LOG_DEBUG;
    const char *file = __FILE__;
    int line = __LINE__;

    if(argc > 1) {
        const char *lvl_str, *msg_str;
        lvl_str = JS_ToCString(ctx, argv[0]);
        if(!zstr(lvl_str)) {
            level = switch_log_str2level(lvl_str);
        }
        if(level == SWITCH_LOG_INVALID) {
            level = SWITCH_LOG_DEBUG;
        }

        msg_str = JS_ToCString(ctx, argv[1]);
        switch_log_printf(SWITCH_CHANNEL_ID_LOG, file, "consoleLog", line, NULL, level, "%s\n", msg_str);

        JS_FreeCString(ctx, lvl_str);
        JS_FreeCString(ctx, msg_str);
    } else if(argc > 0) {
        const char *msg_str;
        msg_str = JS_ToCString(ctx, argv[0]);

        if(!zstr(msg_str)) {
            switch_log_printf(SWITCH_CHANNEL_ID_LOG, file, "consoleLog", line, NULL, level, "%s\n", msg_str);
        }

        JS_FreeCString(ctx, msg_str);
    }
    return JS_UNDEFINED;
}

static JSValue js_msleep(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    uint32_t msec = 0;

    if(argc < 1) {
        return JS_ThrowTypeError(ctx, "Invalid arguments");
    }

    JS_ToUint32(ctx, &msec, argv[0]);

    if(msec) {
        switch_yield(msec * 1000);
    }

    return JS_TRUE;
}

static JSValue js_global_set(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *var_str = NULL;
    const char *val_str = NULL;

    if(argc < 2) {
        return JS_ThrowTypeError(ctx, "Invalid arguments");
    }

    var_str = JS_ToCString(ctx, argv[0]);
    val_str = JS_ToCString(ctx, argv[1]);

    switch_core_set_variable(var_str, val_str);

    JS_FreeCString(ctx, var_str);
    JS_FreeCString(ctx, val_str);

    return JS_TRUE;
}

static JSValue js_global_get(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *var_str;
    char *val = NULL;

    if(argc < 1) {
        return JS_ThrowTypeError(ctx, "Invalid arguments");
    }

    var_str = JS_ToCString(ctx, argv[0]);
    val = switch_core_get_variable(var_str);
    JS_FreeCString(ctx, var_str);

    if(val) {
        if(strcasecmp(val, "true") == 0) {
            return JS_TRUE;
        } else if(strcasecmp(val, "false") == 0) {
            return JS_FALSE;
        } else {
            return JS_NewString(ctx, val);
        }
    }

    return JS_UNDEFINED;
}

static JSValue js_exit(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    JSValue ret_val;
    const char *exit_code;

    if(!argc) {
        return JS_EXCEPTION;
    }

    exit_code = JS_ToCString(ctx, argv[0]);
    if(zstr(exit_code)) {
        return JS_EXCEPTION;
    }

    ret_val = JS_ThrowTypeError(ctx, "ERROR: %s", exit_code);
    JS_FreeCString(ctx, exit_code);

    return ret_val;
}

static JSValue js_include(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    JSValue ret_val = JS_FALSE;
    ivs_session_t *ivs_session = NULL;
    switch_memory_pool_t *pool = NULL;
    switch_file_t *fd = NULL;
    switch_size_t flen = 0;
    const char *path = NULL;
    char *path_local = NULL;
    char *buf = NULL;

    if(argc < 1) {
        return JS_ThrowTypeError(ctx, "Invalid arguments");
    }

    ivs_session = JS_GetContextOpaque(ctx);
    if(!ivs_session) { return JS_ThrowTypeError(ctx, "Invalid ctx"); }

    path = JS_ToCString(ctx, argv[0]);
    if(zstr(path)) {
        return JS_ThrowTypeError(ctx, "Invalid argument: filename");
    }

    if(switch_file_exists(path, NULL) == SWITCH_STATUS_SUCCESS) {
        path_local = strdup(path);
    } else {
        path_local = switch_mprintf("%s%s%s", SWITCH_GLOBAL_dirs.script_dir, SWITCH_PATH_SEPARATOR, path);
        if(switch_file_exists(path_local, NULL) != SWITCH_STATUS_SUCCESS) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "File not found: %s\n", path_local);
            goto out;
        }
    }

    if(switch_core_new_memory_pool(&pool) != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "pool fail\n");
        goto out;
    }

    if(switch_file_open(&fd, path_local, SWITCH_FOPEN_READ, SWITCH_FPROT_UREAD, pool) != SWITCH_STATUS_SUCCESS) {
        ret_val = JS_ThrowTypeError(ctx, "Couldn't open file: %s\n", path_local);
        goto out;
    }

    if((flen = switch_file_get_size(fd)) > 0) {
        if((buf = switch_core_alloc(pool, flen)) == NULL) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "mem fail\n");
            goto out;
        }
        if(switch_file_read(fd, buf, &flen) != SWITCH_STATUS_SUCCESS) {
            ret_val = JS_ThrowTypeError(ctx, "Couldn't read file\n");
            goto out;
        }

        ret_val = JS_Eval(ctx, buf, flen, path_local, JS_EVAL_TYPE_GLOBAL);
        if(JS_IsException(ret_val)) {
            js_dump_error(ivs_session->script, ctx);
            JS_ResetUncatchableError(ctx);
        }

        JS_FreeValue(ctx, ret_val);
        ret_val = JS_TRUE;
    }
out:
    if(fd) {
        switch_file_close(fd);
    }
    if(pool) {
        switch_core_destroy_memory_pool(&pool);
    }
    switch_safe_free(path_local);
    JS_FreeCString(ctx, path);

    return ret_val;
}

static JSValue js_api_execute(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    ivs_session_t *ivs_session = NULL;
    switch_stream_handle_t stream = { 0 };
    JSValue js_ret_val;
    const char *api_str;
    const char *arg_str;

    if(argc < 1) {
        return JS_ThrowTypeError(ctx, "Invalid arguments");
    }

    api_str = JS_ToCString(ctx, argv[0]);
    arg_str = (argc > 1 ? JS_ToCString(ctx, argv[1]) : NULL);

    if(zstr(api_str)) {
        return JS_UNDEFINED;
    }

    ivs_session = JS_GetContextOpaque(ctx);
    if(!ivs_session) {
        return JS_ThrowTypeError(ctx, "Invalid ctx");
    }

    SWITCH_STANDARD_STREAM(stream);
    switch_api_execute(api_str, arg_str, ivs_session->session, &stream);
    js_ret_val = JS_NewString(ctx, switch_str_nil((char *) stream.data));

    switch_safe_free(stream.data);
    JS_FreeCString(ctx, api_str);
    JS_FreeCString(ctx, arg_str);

    return js_ret_val;
}

static JSValue js_unlink(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    ivs_session_t *ivs_session = JS_GetContextOpaque(ctx);
    const char *path = NULL;

    if(!ivs_session) {
        return JS_ThrowTypeError(ctx, "Invalid ctx");
    }

    path = JS_ToCString(ctx, argv[0]);
    if(zstr(path)) {
        return JS_ThrowTypeError(ctx, "Invalid argument: filename");
    }

    unlink(path);

    JS_FreeCString(ctx, path);
    return JS_TRUE;
}

// -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// helper
// -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
void js_dump_error(ivs_script_t *script, JSContext *ctx) {
    JSValue exception_val = JS_GetException(ctx);

    if(JS_IsError(ctx, exception_val)) {
        JSValue stk_val = JS_GetPropertyStr(ctx, exception_val, "stack");
        const char *err_str = JS_ToCString(ctx, exception_val);
        const char *stk_str = (JS_IsUndefined(stk_val) ? NULL : JS_ToCString(ctx, stk_val));

        if(script) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "(%s:%s)\n%s %s\n", script->id, script->name, (err_str ? err_str : "[exception]"), stk_str);
        } else {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "%s %s\n", (err_str ? err_str : "[exception]"), stk_str);
        }

        if(err_str) JS_FreeCString(ctx, err_str);
        if(stk_str) JS_FreeCString(ctx, stk_str);

        JS_FreeValue(ctx, stk_val);
    }

    JS_FreeValue(ctx, exception_val);
}

static switch_status_t script_load(ivs_script_t *script) {
    switch_status_t status = SWITCH_STATUS_SUCCESS;
    switch_file_t *fd = NULL;

    if(!script) {
        status = SWITCH_STATUS_FALSE;
        goto out;
    }

    if((status = switch_file_open(&fd, script->path, SWITCH_FOPEN_READ, SWITCH_FPROT_UREAD, script->pool)) != SWITCH_STATUS_SUCCESS) {
        return status;
    }

    if((script->body_len = switch_file_get_size(fd)) == 0) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Script is empty: %s\n", script->path);
        switch_goto_status(SWITCH_STATUS_FALSE, out);
    }

    if((script->body = switch_core_alloc(script->pool, script->body_len + 2)) == NULL)  {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "mem fail\n");
        switch_goto_status(SWITCH_STATUS_MEMERR, out);
    }

    if(switch_file_read(fd, script->body, &script->body_len) != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Couldn't read file\n");
        switch_goto_status(SWITCH_STATUS_FALSE, out);
    }

    script->body[script->body_len - 1] = ' ';
    script->body[script->body_len] = '\0';
out:
    if(fd) {
        switch_file_close(fd);
    }
    return status;
}

