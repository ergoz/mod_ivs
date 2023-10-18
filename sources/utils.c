/**
 * (C)2023 aks
 * https://github.com/akscf/
 **/
#include <mod_ivs.h>

extern globals_t globals;

void launch_thread(switch_memory_pool_t *pool, switch_thread_start_t fun, void *data) {
    switch_threadattr_t *attr = NULL;
    switch_thread_t *thread = NULL;

    switch_mutex_lock(globals.mutex);
    globals.active_threads++;
    switch_mutex_unlock(globals.mutex);

    switch_threadattr_create(&attr, pool);
    switch_threadattr_detach_set(attr, 1);
    switch_threadattr_stacksize_set(attr, SWITCH_THREAD_STACKSIZE);
    switch_thread_create(&thread, attr, fun, data, pool);

    return;
}

void thread_finished() {
    switch_mutex_lock(globals.mutex);
    if(globals.active_threads > 0) { globals.active_threads--; }
    switch_mutex_unlock(globals.mutex);
}

ivs_session_t *ivs_session_lookup(char *name, uint8_t lock) {
    ivs_session_t *session = NULL;
    uint8_t status = (lock ? false : true);

    if(!name) { return NULL; }

    switch_mutex_lock(globals.mutex_sessions);
    session = switch_core_hash_find(globals.sessions, name);
    if(lock) {
        status = (uint8_t) ivs_session_take(session);
    }
    switch_mutex_unlock(globals.mutex_sessions);
    return (status ? session : NULL);
}

int ivs_session_xflags_test(ivs_session_t *ivs_session, int flag) {
    switch_assert(ivs_session);
    return BIT_CHECK(ivs_session->xflags, flag);
}

void ivs_session_xflags_set(ivs_session_t *ivs_session, int flag, int val) {
    switch_assert(ivs_session);

    switch_mutex_lock(ivs_session->mutex_xflags);
    if(val) { BIT_SET(ivs_session->xflags, flag); }
    else { BIT_CLEAR(ivs_session->xflags, flag);  }
    switch_mutex_unlock(ivs_session->mutex_xflags);
}

uint32_t ivs_session_take(ivs_session_t *session) {
    uint32_t status = false;

    if(!session) { return false; }

    switch_mutex_lock(session->mutex);
    if(session->fl_ready) {
        status = true;
        session->wlocki++;
    }
    switch_mutex_unlock(session->mutex);

    return status;
}

void ivs_session_release(ivs_session_t *session) {
    switch_assert(session);

    switch_mutex_lock(session->mutex);
    if(session->wlocki) { session->wlocki--; }
    switch_mutex_unlock(session->mutex);
}

uint32_t ivs_gen_job_id(ivs_session_t *session) {
    uint32_t ret = 0;

    if(!session) { return false; }

    switch_mutex_lock(session->mutex);
    if(session->job_id_cnt == 0) {
        session->job_id_cnt = 1;
    }
    ret = session->job_id_cnt;
    session->job_id_cnt++;
    switch_mutex_unlock(session->mutex);

    return ret;
}

switch_status_t xdata_buffer_alloc(xdata_buffer_t **out, switch_byte_t *data, uint32_t data_len, uint32_t samplerate, uint8_t channels) {
    xdata_buffer_t *buf = NULL;

    switch_zmalloc(buf, sizeof(xdata_buffer_t));

    if(data_len) {
        switch_malloc(buf->data, data_len);
        switch_assert(buf->data);

        buf->len = data_len;
        buf->samplerate = samplerate;
        buf->channels = channels;

        memcpy(buf->data, data, data_len);
    }

    *out = buf;
    return SWITCH_STATUS_SUCCESS;
}

void xdata_buffer_free(xdata_buffer_t *buf) {
    if(buf) {
        switch_safe_free(buf->data);
        switch_safe_free(buf);
    }
}

void xdata_buffer_queue_clean(switch_queue_t *queue) {
    void *data = NULL;

    if(!queue || !switch_queue_size(queue)) { return; }

    while(switch_queue_trypop(queue, &data) == SWITCH_STATUS_SUCCESS) {
        if(data) { xdata_buffer_free((xdata_buffer_t *) data); }
    }
}

switch_status_t xdata_buffer_push(switch_queue_t *queue, switch_byte_t *data, uint32_t data_len, uint32_t samplerate, uint8_t channels) {
    xdata_buffer_t *buff = NULL;

    if(xdata_buffer_alloc(&buff, data, data_len, samplerate, channels) == SWITCH_STATUS_SUCCESS) {
        if(switch_queue_trypush(queue, buff) == SWITCH_STATUS_SUCCESS) {
            return SWITCH_STATUS_SUCCESS;
        }
        xdata_buffer_free(buff);
    }
    return SWITCH_STATUS_FALSE;
}

char *audio_file_write(switch_byte_t *buf, uint32_t buf_len, uint32_t samplerate, uint32_t channels, const char *file_ext) {
    switch_status_t status = SWITCH_STATUS_FALSE;
    switch_size_t len = buf_len;
    switch_file_handle_t fh = { 0 };
    char *file_name = NULL;
    char name_uuid[SWITCH_UUID_FORMATTED_LENGTH + 1] = { 0 };
    int flags = (SWITCH_FILE_FLAG_WRITE | SWITCH_FILE_DATA_SHORT);

    switch_uuid_str((char *)name_uuid, sizeof(name_uuid));
    file_name = switch_mprintf("%s%s%s.%s", SWITCH_GLOBAL_dirs.temp_dir, SWITCH_PATH_SEPARATOR, name_uuid, (file_ext == NULL ? "wav" : file_ext) );

    if((status = switch_core_file_open(&fh, file_name, channels, samplerate, flags, NULL)) != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Open fail: %s\n", file_name);
        goto out;
    }

    if((status = switch_core_file_write(&fh, buf, &len)) != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Write fail (%s)\n", file_name);
        goto out;
    }

    switch_core_file_close(&fh);
out:
    if(status != SWITCH_STATUS_SUCCESS) {
        if(file_name) {
            unlink(file_name);
            switch_safe_free(file_name);
        }
        return NULL;
    }
    return file_name;
}

char *safe_pool_strdup(switch_memory_pool_t *pool, const char *str) {
    switch_assert(pool);
    if(zstr(str)) { return NULL; }
    return switch_core_strdup(pool, str);
}
