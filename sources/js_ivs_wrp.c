/**
 * (C)2023 aks
 * https://github.com/akscf/
 **/
#include "js_ivs_wrp.h"

typedef struct {
    uint32_t                jid;
    uint8_t                 fl_delete_file;
    uint8_t                 mode; // 0 - playback, 1-say
    char                    *data;
    char                    *lang;
    ivs_session_t           *ivs_session;
    switch_memory_pool_t    *pool;
} js_ivs_async_playback_param_t;

static void *SWITCH_THREAD_FUNC js_ivs_async_playback_thread(switch_thread_t *thread, void *obj) {
    volatile js_ivs_async_playback_param_t *_ref = (js_ivs_async_playback_param_t *) obj;
    js_ivs_async_playback_param_t *params = (js_ivs_async_playback_param_t *) _ref;
    switch_memory_pool_t *pool_local = params->pool;

    if(params->mode == 1) {
        ivs_event_push(IVS_EVENTSQ(params->ivs_session), params->jid, IVS_EVENT_PLAYBACK_STARTED, "SAY", 3);
        ivs_say(params->ivs_session, params->lang, params->data, false);
        ivs_event_push(IVS_EVENTSQ(params->ivs_session), params->jid, IVS_EVENT_PLAYBACK_FINISHED, "SAY", 3);
    } else {
        ivs_event_push(IVS_EVENTSQ(params->ivs_session), params->jid, IVS_EVENT_PLAYBACK_STARTED, params->data, strlen(params->data));
        ivs_playback(params->ivs_session, params->data, false);
        ivs_event_push(IVS_EVENTSQ(params->ivs_session), params->jid, IVS_EVENT_PLAYBACK_FINISHED, params->data, strlen(params->data));
    }

    // relese sem
    ivs_session_release(params->ivs_session);

    if(params->fl_delete_file) {
        unlink(params->data);
    }

    if(pool_local) {
        switch_core_destroy_memory_pool(&pool_local);
    }

    thread_finished();
    return NULL;
}

uint32_t js_ivs_async_playback(ivs_session_t *ivs_session, const char *path, uint8_t delete_file) {
    uint32_t jid = JID_NONE;
    js_ivs_async_playback_param_t *params = NULL;
    switch_memory_pool_t *pool_local = NULL;

    if(switch_core_new_memory_pool(&pool_local) != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "pool fail\n");
        goto out;
    }
    if((params = switch_core_alloc(pool_local, sizeof(js_ivs_async_playback_param_t))) == NULL) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "mem fail\n");
        goto out;
    }

    params->jid = ivs_gen_job_id(ivs_session);
    params->pool = pool_local;
    params->ivs_session = ivs_session;
    params->data = switch_core_strdup(pool_local, path);
    params->fl_delete_file = delete_file;
    params->mode = 0;

    if(ivs_session_take(params->ivs_session)) {
        jid = params->jid;
        launch_thread(pool_local, js_ivs_async_playback_thread, params);
    }
out:
    if(jid == JID_NONE) {
        if(pool_local) { switch_core_destroy_memory_pool(&pool_local); }
    }
    return jid;
}

uint32_t js_ivs_async_say(ivs_session_t *ivs_session, const char *lang, const char *text) {
    uint32_t jid = JID_NONE;
    js_ivs_async_playback_param_t *params = NULL;
    switch_memory_pool_t *pool_local = NULL;

    if(switch_core_new_memory_pool(&pool_local) != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "pool fail\n");
        return JID_NONE;
    }
    if((params = switch_core_alloc(pool_local, sizeof(js_ivs_async_playback_param_t))) == NULL) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "mem fail\n");
        switch_core_destroy_memory_pool(&pool_local);
        return JID_NONE;
    }

    params->jid = ivs_gen_job_id(ivs_session);
    params->pool = pool_local;
    params->ivs_session = ivs_session;
    params->data = switch_core_strdup(pool_local, text);
    params->lang = safe_pool_strdup(pool_local, lang);
    params->mode = 1;

    if(ivs_session_take(params->ivs_session)) {
        jid = params->jid;
        launch_thread(pool_local, js_ivs_async_playback_thread, params);
    }
out:
    if(jid == JID_NONE) {
        if(pool_local) { switch_core_destroy_memory_pool(&pool_local); }
    }
    return jid;
}


