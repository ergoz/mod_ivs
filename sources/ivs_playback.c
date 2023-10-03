/**
 * (C)2023 aks
 * https://akscf.me/
 * https://github.com/akscf/
 **/
#include <ivs_playback.h>

extern globals_t globals;

typedef struct {
    switch_memory_pool_t    *pool;
    ivs_session_t           *ivs_session;
    char                    *data;
    char                    *lang;
    uint8_t                  mode; // 0-playback, 1=say
} playback_thread_params_t;

static void *SWITCH_THREAD_FUNC playback_async_thread(switch_thread_t *thread, void *obj) {
    volatile playback_thread_params_t *_ref = (playback_thread_params_t *) obj;
    playback_thread_params_t *params = (playback_thread_params_t *) _ref;
    switch_memory_pool_t *pool_local = params->pool;

    if(params->mode == 1) {
        ivs_say(params->ivs_session, params->lang, params->data, false);
    } else {
        ivs_playback(params->ivs_session, params->data, false);
    }

    if(pool_local) {
        switch_core_destroy_memory_pool(&pool_local);
    }

    thread_finished();
    return NULL;
}

static switch_status_t read_frame_callback(switch_core_session_t *session, switch_frame_t *frame, void *user_data) {
    switch_channel_t *channel = switch_core_session_get_channel(session);
    ivs_session_t *ivs_session = (ivs_session_t *) user_data;
    xdata_buffer_t *audio_buffer = NULL;

    if(frame && frame->datalen > 0) {
        xdata_buffer_push(ivs_session->au_q_in, frame->data, frame->datalen, ivs_session->samplerate, ivs_session->channels);
    }

    return SWITCH_STATUS_SUCCESS;
}

static switch_status_t on_dtmf_callback(switch_core_session_t *session, void *input, switch_input_type_t itype, void *buf, unsigned int buflen) {
    ivs_session_t *ivs_session = (ivs_session_t *) buf;

    switch (itype) {
        case SWITCH_INPUT_TYPE_DTMF: {
                switch_dtmf_t *dtmf = (switch_dtmf_t *) input;
                //
                // todo
                //
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "on_dtmf_callback: digit=%c\n", dtmf->digit);
            }
            break;
        default:
            break;
    }

    return SWITCH_STATUS_SUCCESS;
}

// --------------------------------------------------------------------------------------------------------------------------------------------------------------------
switch_status_t ivs_playback_stop(ivs_session_t *ivs_session) {
    switch_status_t  status = SWITCH_STATUS_SUCCESS;
    int x = 0;

    switch_assert(ivs_session);

    if(ivs_session_take(ivs_session)) {
        if(ivs_session_xflags_test(ivs_session, IVS_SF_PLAYBACK)) {

            switch_channel_set_flag(switch_core_session_get_channel(ivs_session->session), CF_BREAK);

            while(ivs_session_xflags_test(ivs_session, IVS_SF_PLAYBACK)) {
                if(globals.fl_shutdown || ivs_session->fl_destroyed) { break; }
                if(x > 500) {
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Couldn't stop playback (session: %s)\n", ivs_session->session_id);
                    status = SWITCH_STATUS_FALSE;
                    break;
                }
                x++;
                switch_yield(10000);
            }
        }
        done:
        ivs_session_release(ivs_session);
    }

    return status;
}

switch_status_t ivs_say(ivs_session_t *ivs_session, char *language, char *text, uint8_t async) {
    switch_status_t status = SWITCH_STATUS_FALSE;
    switch_input_args_t *ap = NULL;
    switch_input_args_t args = { 0 };
    switch_channel_t *channel = NULL;
    const char *engine = NULL;
    const char *language_local = language;
    char *expanded = NULL;

    switch_assert(ivs_session);

    if(zstr(text)) { return SWITCH_STATUS_FALSE; }

    if(async) {
        switch_memory_pool_t *pool_local = NULL;
        playback_thread_params_t *params = NULL;

        if(switch_core_new_memory_pool(&pool_local) != SWITCH_STATUS_SUCCESS) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "pool fail\n");
            return SWITCH_STATUS_FALSE;
        }
        if((params = switch_core_alloc(pool_local, sizeof(playback_thread_params_t))) == NULL) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "mem fail\n");
            switch_core_destroy_memory_pool(&pool_local);
            return SWITCH_STATUS_FALSE;
        }

        params->pool = pool_local;
        params->ivs_session = ivs_session;
        params->data = switch_core_strdup(pool_local, text);
        params->lang = (language == NULL ? NULL : switch_core_strdup(pool_local, language));
        params->mode = 1;

        launch_thread(pool_local, playback_async_thread, params);
        return SWITCH_STATUS_SUCCESS;
    }

    args.read_frame_callback = read_frame_callback;
    args.user_data = ivs_session;
    args.input_callback = on_dtmf_callback;
    args.buf = ivs_session;
    args.buflen = 1;
    ap = &args;

    if(ivs_session_take(ivs_session)) {
        channel = switch_core_session_get_channel(ivs_session->session);

        switch_mutex_lock(ivs_session->mutex);
        engine = ivs_session->tts_engine;
        language_local = (language_local == NULL ? ivs_session->language : language_local);
        switch_mutex_unlock(ivs_session->mutex);

        if(language_local == NULL) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "language not defined\n");
            ivs_session_release(ivs_session);
            goto out;
        }

        if((expanded = switch_channel_expand_variables(channel, text)) != text) {
            text = expanded;
        } else {
            expanded = NULL;
        }

        if(ivs_session_xflags_test(ivs_session, IVS_SF_PLAYBACK)) {
            if((status = ivs_playback_stop(ivs_session)) != SWITCH_STATUS_SUCCESS) {
                ivs_session_release(ivs_session);
                goto out;
            }
        }

        ivs_session_xflags_set(ivs_session, IVS_SF_PLAYBACK, true);

        if(switch_channel_test_flag(channel, CF_BREAK)) {
            switch_channel_clear_flag(channel, CF_BREAK);
        }

        for(int x = 0; x < 10; x++) {
            switch_frame_t *read_frame;
            status = switch_core_session_read_frame(ivs_session->session, &read_frame, SWITCH_IO_FLAG_NONE, 0);
            if(!SWITCH_READ_ACCEPTABLE(status)) { break; }
        }

        if(!engine) {
            engine = switch_channel_get_variable(channel, "tts_engine");
        }
        if(engine) {
            status = switch_ivr_speak_text(ivs_session->session, engine, language_local, text, &args);
        } else {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "tts-engine not defined\n");
            status = SWITCH_STATUS_FALSE;
        }

        ivs_session_xflags_set(ivs_session, IVS_SF_PLAYBACK, false);
        ivs_session_release(ivs_session);
    }

out:
    switch_safe_free(expanded);
    return status;
}

switch_status_t ivs_playback(ivs_session_t *ivs_session, char *path, uint8_t async) {
    switch_status_t status = SWITCH_STATUS_FALSE;
    switch_input_args_t *ap = NULL;
    switch_input_args_t args = { 0 };
    switch_channel_t *channel = NULL;
    const char *engine = NULL;
    const char *language_local = NULL;
    char *expanded = NULL;

    switch_assert(ivs_session);

    if(zstr(path)) { return SWITCH_STATUS_NOTFOUND; }

    if(async) {
        switch_memory_pool_t *pool_local = NULL;
        playback_thread_params_t *params = NULL;

        if(switch_core_new_memory_pool(&pool_local) != SWITCH_STATUS_SUCCESS) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "pool fail\n");
            return SWITCH_STATUS_FALSE;
        }
        if((params = switch_core_alloc(pool_local, sizeof(playback_thread_params_t))) == NULL) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "mem fail\n");
            switch_core_destroy_memory_pool(&pool_local);
            return SWITCH_STATUS_FALSE;
        }

        params->pool = pool_local;
        params->ivs_session = ivs_session;
        params->data = switch_core_strdup(pool_local, path);
        params->mode = 0;

        launch_thread(pool_local, playback_async_thread, params);
        return SWITCH_STATUS_SUCCESS;
    }

    args.read_frame_callback = read_frame_callback;
    args.user_data = ivs_session;
    args.input_callback = on_dtmf_callback;
    args.buf = ivs_session;
    args.buflen = 1;
    ap = &args;

    if(ivs_session_take(ivs_session)) {
        channel = switch_core_session_get_channel(ivs_session->session);

        switch_mutex_lock(ivs_session->mutex);
        engine = ivs_session->tts_engine;
        language_local = ivs_session->language;
        switch_mutex_unlock(ivs_session->mutex);

        if((expanded = switch_channel_expand_variables(channel, path)) != path) {
            path = expanded;
        } else {
            expanded = NULL;
        }

        if(ivs_session_xflags_test(ivs_session, IVS_SF_PLAYBACK)) {
            if((status = ivs_playback_stop(ivs_session)) != SWITCH_STATUS_SUCCESS) {
                ivs_session_release(ivs_session);
                goto out;
            }
        }

        ivs_session_xflags_set(ivs_session, IVS_SF_PLAYBACK, true);

        if(switch_channel_test_flag(channel, CF_BREAK)) {
            switch_channel_clear_flag(channel, CF_BREAK);
        }

        for(int x = 0; x < 10; x++) {
            switch_frame_t *read_frame;
            status = switch_core_session_read_frame(ivs_session->session, &read_frame, SWITCH_IO_FLAG_NONE, 0);
            if(!SWITCH_READ_ACCEPTABLE(status)) { break; }
        }

        if(strncasecmp(path, "say://", 4) == 0) {
            if(!engine) { engine = switch_channel_get_variable(channel, "tts_engine"); }
            if(engine && language_local) {
                status = switch_ivr_speak_text(ivs_session->session, engine, language_local, path + 6, &args);
            } else {
                if(!engine) { switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "tts-engine not defined\n");}
                if(!language_local) { switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "laguage not defined\n");}
                status = SWITCH_STATUS_FALSE;
            }
        } else if(strstr(path, "://") != NULL) {
            status = switch_ivr_play_file(ivs_session->session, NULL, path, ap);
        } else {
            if(switch_file_exists(path, NULL) == SWITCH_STATUS_SUCCESS) {
                status = switch_ivr_play_file(ivs_session->session, NULL, path, ap);
            } else {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "file not found: %s\n", path);
                status = SWITCH_STATUS_NOTFOUND;
            }
        }

        ivs_session_xflags_set(ivs_session, IVS_SF_PLAYBACK, false);
        ivs_session_release(ivs_session);
    }

out:
    switch_safe_free(expanded);
    return status;
}

