/**
 * (C)2023 aks
 * https://github.com/akscf/
 **/
#include "mod_ivs.h"
#include "ivs_playback.h"
#include "ivs_events.h"
#include "ivs_qjs.h"

globals_t globals;

SWITCH_MODULE_LOAD_FUNCTION(mod_ivs_load);
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_ivs_shutdown);
SWITCH_MODULE_DEFINITION(mod_ivs, mod_ivs_load, mod_ivs_shutdown, NULL);

static void *SWITCH_THREAD_FUNC audio_processing_thread(switch_thread_t *thread, void *obj);

// ---------------------------------------------------------------------------------------------------------------------------------------------
// CMD/APP API
// ---------------------------------------------------------------------------------------------------------------------------------------------
#define CMD_SYNTAX "\n"\
        "list       - show active sessions\n" \
        "kill [sid] - terminate session\n" \
        "playback [sid] [filePaht] - playback a file\n"

SWITCH_STANDARD_API(ivs_cmd_api) {
    char *mycmd = NULL;
    char *argv[10] = { 0 };
    int argc = 0;

    if (!zstr(cmd)) {
        mycmd = strdup(cmd);
        switch_assert(mycmd);
        argc = switch_separate_string(mycmd, ' ', argv, (sizeof(argv) / sizeof(argv[0])));
    }
    if(argc == 0) {
        goto usage;
    }

    if(argc == 1) {
        if(strcasecmp(argv[0], "list") == 0) {
            switch_hash_index_t *hidx = NULL;

            stream->write_function(stream, "ivs-sessions: \n");
            switch_mutex_lock(globals.mutex_sessions);
            for (hidx = switch_core_hash_first_iter(globals.sessions, hidx); hidx; hidx = switch_core_hash_next(&hidx)) {
                ivs_session_t *ivs_session = NULL;
                const void *hkey = NULL; void *hval = NULL;

                switch_core_hash_this(hidx, &hkey, NULL, &hval);
                ivs_session = (ivs_session_t *)hval;

                if(ivs_session_take(ivs_session)) {
                    stream->write_function(stream, "%s [script:%s / caller-nuber: %s / called-number=%s / start-ts=%d]\n",
                        ivs_session->session_id, ivs_session->script->name, ivs_session->caller_number, ivs_session->called_number, ivs_session->start_ts
                    );
                    ivs_session_release(ivs_session);
                }
            }
            switch_mutex_unlock(globals.mutex_sessions);
            goto out;
        }
        goto usage;
    }
    if(strcasecmp(argv[0], "kill") == 0) {
        char *sid = (argc >= 2 ? argv[1] : NULL);
        ivs_session_t *ivs_session = NULL;

        if(!sid) { goto usage; }

        ivs_session = ivs_session_lookup(sid, true);
        if(ivs_session) {
            ivs_session->fl_do_destroy = true;
            ivs_session_release(ivs_session);
            stream->write_function(stream, "+OK\n");
        } else {
            stream->write_function(stream, "-ERR: session not found\n");
        }
        goto out;
    }
    if(strcasecmp(argv[0], "playback") == 0) {
        char *sid = (argc >= 2 ? argv[1] : NULL);
        char *path = (argc >= 3 ? argv[2] : NULL);
        ivs_session_t *ivs_session = NULL;

        if(!sid || !path) { goto usage; }

        ivs_session = ivs_session_lookup(sid, true);
        if(ivs_session) {
            ivs_playback(ivs_session, path, true);
            ivs_session_release(ivs_session);
            stream->write_function(stream, "+OK\n");
        } else {
            stream->write_function(stream, "-ERR: session not found\n");
        }
        goto out;
    }
usage:
    stream->write_function(stream, "-USAGE: %s\n", CMD_SYNTAX);
out:
    switch_safe_free(mycmd);
    return SWITCH_STATUS_SUCCESS;
}

#define APP_SYNTAX "scriptName [opts]"
SWITCH_STANDARD_APP(ivs_dp_app) {
    switch_status_t status = SWITCH_STATUS_SUCCESS;
    switch_channel_t *channel = switch_core_session_get_channel(session);
    ivs_session_t *ivs_session = NULL;
    switch_codec_implementation_t read_impl = { 0 };
    char *mycmd = NULL, *argv[10] = { 0 }; int argc = 0;
    char *script_name = NULL, *script_path_local = NULL, *script_args = NULL;
    //
    switch_vad_t *vad = NULL;
    switch_vad_state_t vad_state = 0;
    switch_timer_t timer = { 0 };
    switch_frame_t write_frame = { 0 };
    switch_frame_t *read_frame = NULL;
    switch_codec_t *session_write_codec = switch_core_session_get_write_codec(session);
    switch_codec_t *session_read_codec = switch_core_session_get_read_codec(session);
    switch_byte_t *audio_io_buffer = NULL, *audio_tmp_buffer = NULL, *vad_buffer = NULL;
    int32_t vad_buffer_offs = 0, vad_stored_frames = 0;
    uint32_t audio_io_buffer_data_len = 0, audio_tmp_buffer_data_len = 0;
    uint32_t enc_samplerate = 0, enc_flags = 0, dec_samplerate = 0, dec_flags = 0;
    uint8_t fl_capture_on = false, fl_has_audio = false, fl_skip_cng = false;
    void *pop = NULL;

    if(!zstr(data)) {
        mycmd = strdup(data);
        switch_assert(mycmd);
        argc = switch_separate_string(mycmd, ' ', argv, (sizeof(argv) / sizeof(argv[0])));
    }
    if(argc < 1) { goto usage; }
    if(globals.fl_shutdown) { goto out; }

    script_name = argv[0];
    script_args = (argc > 1 ? ((char *)data + (strlen(argv[0]) + 1)) : NULL);

    if(!script_name) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Script not defined\n");
        switch_goto_status(SWITCH_STATUS_FALSE, out);
    }

    if(switch_file_exists(script_name, NULL) == SWITCH_STATUS_SUCCESS) {
        script_path_local = switch_core_session_strdup(session, script_name);
    } else {
        script_path_local = switch_core_session_sprintf(session, "%s%s%s", SWITCH_GLOBAL_dirs.script_dir, SWITCH_PATH_SEPARATOR, script_name);
        if(switch_file_exists(script_path_local, NULL) != SWITCH_STATUS_SUCCESS) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Script not found: %s\n", script_path_local);
            switch_goto_status(SWITCH_STATUS_FALSE, out);
        }
    }

    if((ivs_session = switch_core_session_alloc(session, sizeof(ivs_session_t))) == NULL) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "mem fail\n");
        switch_goto_status(SWITCH_STATUS_GENERR, out);
    }
    if((ivs_session->script = switch_core_session_alloc(session, sizeof(ivs_script_t))) == NULL) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "mem fail\n");
        switch_goto_status(SWITCH_STATUS_GENERR, out);
    }

    if(switch_mutex_init(&ivs_session->mutex, SWITCH_MUTEX_NESTED, switch_core_session_get_pool(session)) != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "mem fail\n");
        switch_goto_status(SWITCH_STATUS_GENERR, out);
    }
    if(switch_mutex_init(&ivs_session->mutex_xflags, SWITCH_MUTEX_NESTED, switch_core_session_get_pool(session)) != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "mem fail\n");
        switch_goto_status(SWITCH_STATUS_GENERR, out);
    }

    switch_queue_create(&ivs_session->au_q_out, AUDIO_QUEUE_SIZE, switch_core_session_get_pool(session));
    switch_queue_create(&ivs_session->au_q_in, AUDIO_QUEUE_SIZE, switch_core_session_get_pool(session));
    switch_queue_create(&ivs_session->events, EVENTS_QUEUE_SIZE, switch_core_session_get_pool(session));

    switch_core_session_get_read_impl(session, &read_impl);

    ivs_session->session = session;
    ivs_session->session_id = switch_core_session_get_uuid(session);
    ivs_session->caller_number = switch_channel_get_variable(channel, "caller_id_number");
    ivs_session->called_number = switch_channel_get_variable(channel, "destination_number");
    ivs_session->ptime = (read_impl.microseconds_per_packet / 1000);
    ivs_session->channels = read_impl.number_of_channels;
    ivs_session->samplerate = read_impl.actual_samples_per_second; // samples_per_second
    ivs_session->encoded_bytes_per_packet = read_impl.encoded_bytes_per_packet;
    ivs_session->decoded_bytes_per_packet = read_impl.decoded_bytes_per_packet;
    ivs_session->chunk_buffer_size = ((globals.cfg_chunk_len_sec * read_impl.actual_samples_per_second) * sizeof(int16_t));
    ivs_session->vad_buffer_size = (ivs_session->decoded_bytes_per_packet * VAD_STORE_FRAMES);
    //
    ivs_session->chunk_file_ext = "mp3";
    ivs_session->chunk_type = IVS_CHUNK_TYPE_BUFFER;
    ivs_session->language = globals.default_language;
    ivs_session->tts_engine = globals.default_tts_engine;
    ivs_session->asr_engine = globals.default_asr_engine;
    //
    ivs_session->start_ts = switch_epoch_time_now(NULL);

    if(js_script_init(ivs_session, script_path_local, script_args) != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Couldn't init script engine\n");
        switch_goto_status(SWITCH_STATUS_FALSE, out);
    }

    switch_mutex_lock(globals.mutex_sessions);
    switch_core_hash_insert(globals.sessions, ivs_session->session_id, ivs_session);
    switch_mutex_unlock(globals.mutex_sessions);

    // ---------------------------------------------------------------------------------
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "CLINET_JOINED: session=%s, script=%s, caller_number=%s, called_number=%s, ptime=%i, channels=%i, samplerate=%i, " \
                      "encoded_bytes_per_packet=%u, decoded_bytes_per_packet=%u, cfg_vad_silence_ms=%i, cfg_vad_voice_ms=%i, cfg_vad_threshold=%i, cfg_cng_lvl=%i, chunk_buffer_size=%u\n",
        ivs_session->session_id, ivs_session->script->name, ivs_session->caller_number, ivs_session->called_number,
        ivs_session->ptime, ivs_session->channels, ivs_session->samplerate, ivs_session->encoded_bytes_per_packet, ivs_session->decoded_bytes_per_packet,
        globals.cfg_vad_silence_ms, globals.cfg_vad_voice_ms, globals.cfg_vad_threshold, globals.cfg_cng_lvl, ivs_session->chunk_buffer_size
    );

    // ---------------------------------------------------------------------------------
    if((write_frame.data = switch_core_session_alloc(session, AUDIO_BUFFER_SIZE)) == NULL) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "mem fail\n");
        switch_goto_status(SWITCH_STATUS_GENERR, out);
    }
    if((audio_io_buffer = switch_core_session_alloc(session, AUDIO_BUFFER_SIZE)) == NULL) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "mem fail\n");
        switch_goto_status(SWITCH_STATUS_GENERR, out);
    }
    if((audio_tmp_buffer = switch_core_session_alloc(session, AUDIO_BUFFER_SIZE)) == NULL) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "mem fail\n");
        switch_goto_status(SWITCH_STATUS_GENERR, out);
    }
    if((vad_buffer = switch_core_session_alloc(session, ivs_session->vad_buffer_size)) == NULL) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "mem fail\n");
        switch_goto_status(SWITCH_STATUS_GENERR, out);
    }
    if(switch_core_timer_init(&timer, "soft", ivs_session->ptime, ivs_session->samplerate, switch_core_session_get_pool(session)) != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "timer fail\n");
        switch_goto_status(SWITCH_STATUS_FALSE, out);
    }

    if((vad = switch_vad_init(ivs_session->samplerate, ivs_session->channels)) == NULL) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "vad fail\n");
        switch_goto_status(SWITCH_STATUS_FALSE, out);
    }
    switch_vad_set_mode(vad, -1);
    switch_vad_set_param(vad, "debug", globals.cfg_vad_debug);
    if(globals.cfg_vad_silence_ms > 0)  { switch_vad_set_param(vad, "silence_ms", globals.cfg_vad_silence_ms); }
    if(globals.cfg_vad_voice_ms > 0)    { switch_vad_set_param(vad, "voice_ms", globals.cfg_vad_voice_ms); }
    if(globals.cfg_vad_threshold > 0)   { switch_vad_set_param(vad, "thresh", globals.cfg_vad_threshold); }

    ivs_session->fl_ready = true;

    if(ivs_session_take(ivs_session)) {
        launch_thread(switch_core_session_get_pool(session), audio_processing_thread, ivs_session);
    } else {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "ivs_session_take() fail\n");
        goto out;
    }
    if(ivs_session_take(ivs_session)) {
        launch_thread(switch_core_session_get_pool(session), script_maintenance_thread, ivs_session);
    } else {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "ivs_session_take() fail\n");
        goto out;
    }

    while(true)  {
        if(!switch_channel_ready(channel) || globals.fl_shutdown || !ivs_session->fl_ready || ivs_session->fl_do_destroy) {
            break;
        }

        fl_skip_cng = false;
        fl_has_audio = false;

        if(switch_queue_trypop(ivs_session->au_q_in, &pop) == SWITCH_STATUS_SUCCESS) {
            xdata_buffer_t *au_buffer = (xdata_buffer_t *)pop;
            if(au_buffer && au_buffer->len > 0) {
                dec_flags = 0;
                dec_samplerate = ivs_session->samplerate;
                audio_io_buffer_data_len = AUDIO_BUFFER_SIZE;

                status = switch_core_codec_decode(session_read_codec, NULL, au_buffer->data, au_buffer->len, au_buffer->samplerate, audio_io_buffer, &audio_io_buffer_data_len, &dec_samplerate, &dec_flags);
                if(status == SWITCH_STATUS_SUCCESS && audio_io_buffer_data_len > 0) {
                    fl_has_audio = true;
                    fl_skip_cng = true;
                }
            }
            xdata_buffer_free(au_buffer);
        }

        if(fl_has_audio) {
            goto audio_produce;
        }
        if(ivs_session_xflags_test(ivs_session, IVS_SF_PLAYBACK)) {
            goto timer_next;
        }

        status = switch_core_session_read_frame(session, &read_frame, SWITCH_IO_FLAG_NONE, 0);
        if(SWITCH_READ_ACCEPTABLE(status) && !switch_test_flag(read_frame, SFF_CNG) && read_frame->samples > 0) {
            if(switch_core_codec_ready(session_read_codec)) {
                dec_flags = 0;
                dec_samplerate = ivs_session->samplerate;
                audio_io_buffer_data_len = AUDIO_BUFFER_SIZE;

                status = switch_core_codec_decode(session_read_codec, NULL, read_frame->data, read_frame->datalen, ivs_session->samplerate, audio_io_buffer, &audio_io_buffer_data_len, &dec_samplerate, &dec_flags);
                if(status == SWITCH_STATUS_SUCCESS && audio_io_buffer_data_len > 0) {
                    fl_has_audio = true;
                    fl_skip_cng = false;
                }
            }
        }

        audio_produce:
        if(fl_has_audio) {
            if(ivs_session->vad_state == SWITCH_VAD_STATE_STOP_TALKING || (ivs_session->vad_state == vad_state && vad_state == SWITCH_VAD_STATE_NONE)) {
                if(audio_io_buffer_data_len <= ivs_session->decoded_bytes_per_packet) {
                    if(vad_buffer_offs >= ivs_session->vad_buffer_size ) { vad_buffer_offs = 0; vad_stored_frames = 0;}
                    memcpy((void *)(vad_buffer + vad_buffer_offs), audio_io_buffer, MIN(audio_io_buffer_data_len, ivs_session->decoded_bytes_per_packet));
                    vad_buffer_offs += ivs_session->decoded_bytes_per_packet;
                    vad_stored_frames++;
                }
            }

            vad_state = switch_vad_process(vad, (int16_t *)audio_io_buffer, audio_io_buffer_data_len / sizeof(int16_t));
            if(vad_state == SWITCH_VAD_STATE_START_TALKING) {
                if(vad_state != ivs_session->vad_state) {
                    ivs_event_push_simple(IVS_EVENTSQ(ivs_session), IVS_EVENT_SPEAKING_START, NULL);
                }
                ivs_session->vad_state = vad_state;
                fl_capture_on = true;
            } else if (vad_state == SWITCH_VAD_STATE_STOP_TALKING) {
                if(vad_state != ivs_session->vad_state) {
                    ivs_event_push_simple(IVS_EVENTSQ(ivs_session), IVS_EVENT_SPEAKING_STOP, NULL);
                }
                ivs_session->vad_state = vad_state;
                fl_capture_on = false;
                switch_vad_reset(vad);
            } else if (vad_state == SWITCH_VAD_STATE_TALKING) {
                if(vad_state != ivs_session->vad_state) {
                    /* nothing */
                }
                ivs_session->vad_state = vad_state;
                fl_capture_on = true;
            }

            if(fl_capture_on) {
                if(vad_state == SWITCH_VAD_STATE_START_TALKING && vad_buffer_offs > 0) {
                    xdata_buffer_t *tau_buf = NULL;
                    uint32_t tdata_len = 0;

                    if(vad_stored_frames >= VAD_RECOVERY_FRAMES) { vad_stored_frames = VAD_RECOVERY_FRAMES; }

                    vad_buffer_offs -= (vad_stored_frames * ivs_session->decoded_bytes_per_packet);
                    if(vad_buffer_offs < 0 ) { vad_buffer_offs = 0; }

                    tdata_len = (vad_stored_frames * ivs_session->decoded_bytes_per_packet) + audio_io_buffer_data_len;

                    // fill-in xdata buffer
                    switch_zmalloc(tau_buf, sizeof(xdata_buffer_t));
                    switch_malloc(tau_buf->data, tdata_len);

                    tau_buf->len = tdata_len;
                    tau_buf->samplerate = ivs_session->samplerate;
                    tau_buf->channels = ivs_session->channels;

                    tdata_len = (vad_stored_frames * ivs_session->decoded_bytes_per_packet);
                    memcpy(tau_buf->data, vad_buffer + vad_buffer_offs, tdata_len);
                    memcpy(tau_buf->data + tdata_len, audio_io_buffer, audio_io_buffer_data_len);

                    if(switch_queue_trypush(ivs_session->au_q_out, tau_buf) != SWITCH_STATUS_SUCCESS) {
                        xdata_buffer_free(tau_buf);
                    }

                    vad_stored_frames = 0;
                    vad_buffer_offs = 0;
                } else {
                    xdata_buffer_push(ivs_session->au_q_out, audio_io_buffer, audio_io_buffer_data_len, ivs_session->samplerate, ivs_session->channels);
                }

                fl_capture_on = false;
            }
        }

        if(!fl_skip_cng && globals.cfg_cng_lvl > 0) {
            audio_tmp_buffer_data_len = (ivs_session->encoded_bytes_per_packet * sizeof(int16_t));
            switch_generate_sln_silence((int16_t *) audio_tmp_buffer, ivs_session->encoded_bytes_per_packet, ivs_session->channels, globals.cfg_cng_lvl);
            if(switch_core_codec_ready(session_write_codec)) {
                enc_flags = 0;
                enc_samplerate = ivs_session->samplerate;
                audio_io_buffer_data_len = AUDIO_BUFFER_SIZE;

                status = switch_core_codec_encode(session_write_codec, NULL, audio_tmp_buffer, audio_tmp_buffer_data_len, ivs_session->samplerate, audio_io_buffer, &audio_io_buffer_data_len, &enc_samplerate, &enc_flags);
                if(status == SWITCH_STATUS_SUCCESS && audio_io_buffer_data_len > 0)  {
                    write_frame.codec = session_write_codec;
                    write_frame.buflen = AUDIO_BUFFER_SIZE;
                    write_frame.datalen = audio_io_buffer_data_len;
                    write_frame.samples = audio_io_buffer_data_len;

                    memcpy(write_frame.data, audio_io_buffer, audio_io_buffer_data_len);
                    switch_core_session_write_frame(session, &write_frame, SWITCH_IO_FLAG_NONE, 0);
                }
            }
        }

        timer_next:
        switch_core_timer_next(&timer);
    }
    goto out;

usage:
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "%s\n", APP_SYNTAX);

out:
    if(timer.interval) {
        switch_core_timer_destroy(&timer);
    }
    if(vad) {
        switch_vad_destroy(&vad);
    }

    if(ivs_session) {
        ivs_session->fl_ready = false;
        ivs_session->fl_destroyed = true;

        if(ivs_session->wlocki > 0) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Waiting for unlock (sid=%s, wlock=%i)\n", ivs_session->session_id, ivs_session->wlocki);
            while(ivs_session->wlocki > 0) {
                switch_yield(100000);
            }
        }

        if(ivs_session->au_q_in) {
            xdata_buffer_queue_clean(ivs_session->au_q_in);
            switch_queue_term(ivs_session->au_q_in);
        }

        if(ivs_session->au_q_out) {
            xdata_buffer_queue_clean(ivs_session->au_q_out);
            switch_queue_term(ivs_session->au_q_out);
        }

        if(ivs_session->events) {
            ivs_events_queue_clean(ivs_session->events);
            switch_queue_term(ivs_session->events);
        }

        js_script_destroy(ivs_session);

        switch_mutex_lock(globals.mutex_sessions);
        switch_core_hash_delete(globals.sessions, ivs_session->session_id);
        switch_mutex_unlock(globals.mutex_sessions);
    }

    switch_core_session_reset(session, SWITCH_TRUE, SWITCH_TRUE);
    switch_safe_free(mycmd);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------
static void *SWITCH_THREAD_FUNC audio_processing_thread(switch_thread_t *thread, void *obj) {
    volatile ivs_session_t *_ref = (ivs_session_t *) obj;
    ivs_session_t *ivs_session = (ivs_session_t *) _ref;
    switch_core_session_t *session = ivs_session->session;
    switch_memory_pool_t *pool = NULL;
    switch_buffer_t *chunk_buffer = NULL;
    char *file_ext_local = NULL;
    uint32_t chunk_type_local = 0;
    uint8_t fl_chunk_ready = false;
    void *pop = NULL;

    if(switch_core_new_memory_pool(&pool) != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "pool fail\n");
        goto out;
    }
    if(switch_buffer_create(pool, &chunk_buffer, ivs_session->chunk_buffer_size) != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "mem fail\n");
        goto out;
    }
    switch_buffer_zero(chunk_buffer);

    while(true) {
        if(globals.fl_shutdown || ivs_session->fl_do_destroy || ivs_session->fl_destroyed ) {
            break;
        }
        if(!ivs_session->fl_ready) {
            goto timer_next;
        }

        if(switch_queue_trypop(ivs_session->au_q_out, &pop) == SWITCH_STATUS_SUCCESS) {
            xdata_buffer_t *au_data = (xdata_buffer_t *)pop;
            if(au_data->len > 0) {
                uint32_t sz = switch_buffer_write(chunk_buffer, au_data->data, au_data->len);
                if(sz >= ivs_session->chunk_buffer_size) { fl_chunk_ready = true; }
            }
            xdata_buffer_free(au_data);
        }

        if(!fl_chunk_ready) {
            fl_chunk_ready = (switch_buffer_inuse(chunk_buffer) > 0 && ivs_session->vad_state == SWITCH_VAD_STATE_STOP_TALKING);
        }
        if(fl_chunk_ready) {
            const void *ptr = NULL;
            uint32_t buf_len = switch_buffer_peek_zerocopy(chunk_buffer, &ptr);
            uint32_t buf_time = (buf_len / ivs_session->samplerate);

            switch_mutex_lock(ivs_session->mutex);
            chunk_type_local = ivs_session->chunk_type;
            if(file_ext_local == NULL) {
                if(ivs_session->chunk_file_ext) { file_ext_local = switch_core_strdup(pool, ivs_session->chunk_file_ext); }
            } else {
                if(ivs_session->chunk_file_ext && ivs_session->fl_chunk_file_ext_changed) {
                    file_ext_local = switch_core_strdup(pool, ivs_session->chunk_file_ext);
                    ivs_session->fl_chunk_file_ext_changed = false;
                }
            }
            switch_mutex_unlock(ivs_session->mutex);

            if(chunk_type_local == IVS_CHUNK_TYPE_FILE) {
                char *out_fname = audio_file_write((switch_byte_t *)ptr, buf_len, ivs_session->samplerate, ivs_session->channels, file_ext_local);
                if(out_fname == NULL) {
                    switch_buffer_zero(chunk_buffer);
                    fl_chunk_ready = false;
                    goto timer_next;
                }
                ivs_event_push_chunk_ready(IVS_EVENTSQ(ivs_session), ivs_session->samplerate, ivs_session->channels, buf_time, buf_len, out_fname, strlen(out_fname));
                switch_safe_free(out_fname);
            } else if(chunk_type_local == IVS_CHUNK_TYPE_BUFFER) {
                ivs_event_push_chunk_ready(IVS_EVENTSQ(ivs_session), ivs_session->samplerate, ivs_session->channels, buf_time, buf_len, (switch_byte_t *)ptr, buf_len);
            }

            switch_buffer_zero(chunk_buffer);
            fl_chunk_ready = false;
        }

        timer_next:
        switch_yield(10000);
    }
out:
    if(chunk_buffer) {
        switch_buffer_destroy(&chunk_buffer);
    }
    if(pool) {
        switch_core_destroy_memory_pool(&pool);
    }

    ivs_session_release(ivs_session);

    thread_finished();

    return NULL;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------------------------------------------------------------------------
#define CONFIG_NAME "ivs.conf"
SWITCH_MODULE_LOAD_FUNCTION(mod_ivs_load) {
    switch_status_t status = SWITCH_STATUS_SUCCESS;
    switch_xml_t cfg, xml, settings, param;
    switch_api_interface_t *commands_interface;
    switch_application_interface_t *app_interface;

    memset(&globals, 0, sizeof (globals));

    switch_mutex_init(&globals.mutex, SWITCH_MUTEX_NESTED, pool);
    switch_mutex_init(&globals.mutex_sessions, SWITCH_MUTEX_NESTED, pool);
    switch_core_hash_init(&globals.sessions);

    if((xml = switch_xml_open_cfg(CONFIG_NAME, &cfg, NULL)) == NULL) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't open: %s\n", CONFIG_NAME);
        switch_goto_status(SWITCH_STATUS_GENERR, done);
    }
    if((settings = switch_xml_child(cfg, "settings"))) {
        for(param = switch_xml_child(settings, "param"); param; param = param->next) {
            char *var = (char *) switch_xml_attr_soft(param, "name");
            char *val = (char *) switch_xml_attr_soft(param, "value");

            if(!strcasecmp(var, "cng-level")) {
                if(val) globals.cfg_cng_lvl = atoi (val);
            } else if(!strcasecmp(var, "chunk-len-sec")) {
                if(val) globals.cfg_chunk_len_sec = atoi (val);
            } else if(!strcasecmp(var, "vad-voice-ms")) {
                if(val) globals.cfg_vad_voice_ms = atoi (val);
            } else if(!strcasecmp(var, "vad-silence-ms")) {
                if(val) globals.cfg_vad_silence_ms = atoi (val);
            } else if(!strcasecmp(var, "vad-threshold")) {
                if(val) globals.cfg_vad_threshold = atoi (val);
            } else if(!strcasecmp(var, "vad-debug")) {
                if(val) globals.cfg_vad_debug = switch_true(val);
            } else if(!strcasecmp(var, "default-asr-engine")) {
                if(val) globals.default_asr_engine = switch_core_strdup(pool, val);
            } else if(!strcasecmp(var, "default-tts-engine")) {
                if(val) globals.default_tts_engine = switch_core_strdup(pool, val);
            } else if(!strcasecmp(var, "default-language")) {
                if(val) globals.default_language = switch_core_strdup(pool, val);
            }
        }
    }

    globals.cfg_chunk_len_sec = (globals.cfg_chunk_len_sec ? globals.cfg_chunk_len_sec : 15);

    // --------------------------------
    *module_interface = switch_loadable_module_create_module_interface(pool, modname);
    SWITCH_ADD_API(commands_interface, "ivs", "console api", ivs_cmd_api, CMD_SYNTAX);
    SWITCH_ADD_APP(app_interface, "ivs", "dialplan app", "dialplan app", ivs_dp_app, APP_SYNTAX, SAF_NONE);

    globals.fl_ready = true;
    globals.fl_shutdown = false;

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "IVS (%s)\n", IVS_VERSION);

done:
    if(xml) {
        switch_xml_free(xml);
    }
    if(status != SWITCH_STATUS_SUCCESS) {
        globals.fl_shutdown = true;
        if(globals.sessions) { switch_core_hash_destroy(&globals.sessions); }
    }
    return status;
}

SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_ivs_shutdown) {
    switch_hash_index_t *hi = NULL;
    ivs_session_t *ivs_session = NULL;
    void *hval = NULL;

    globals.fl_shutdown = true;
    while(globals.active_threads > 0) {
        switch_yield(100000);
    }

    switch_mutex_lock(globals.mutex_sessions);
    for(hi = switch_core_hash_first_iter(globals.sessions, hi); hi; hi = switch_core_hash_next(&hi)) {
        switch_core_hash_this(hi, NULL, NULL, &hval);
        ivs_session = (ivs_session_t *) hval;
        if(ivs_session_take(ivs_session)) {
            ivs_session->fl_do_destroy = true;
            ivs_session_release(ivs_session);
        }
    }
    switch_safe_free(hi);
    switch_core_hash_destroy(&globals.sessions);
    switch_mutex_unlock(globals.mutex_sessions);

    return SWITCH_STATUS_SUCCESS;
}

