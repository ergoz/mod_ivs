/**
 * (C)2023 aks
 * https://akscf.me/
 * https://github.com/akscf/
 **/
#include <ivs_events.h>

extern globals_t globals;

void ivs_event_free(ivs_event_t *event) {
    if(event) {
        if(event->payload_dh) {
            event->payload_dh(event->payload);
        }
        switch_safe_free(event->payload);
        switch_safe_free(event);
    }
}

void ivs_events_queue_clean(switch_queue_t *queue) {
    void *data = NULL;

    if(!queue || !switch_queue_size(queue)) { return; }

    while(switch_queue_trypop(queue, &data) == SWITCH_STATUS_SUCCESS) {
        if(data) { ivs_event_free((ivs_event_t *) data); }
    }
}

switch_status_t ivs_event_push_simple(switch_queue_t *queue, uint32_t type, char *payload_str) {
    ivs_event_t *event = NULL;

    switch_assert(queue);

    switch_zmalloc(event, sizeof(ivs_event_t));
    event->type = type;

    if(payload_str) {
        event->payload_len = strlen(payload_str);

        switch_malloc(event->payload, event->payload_len + 1);
        memcpy(event->payload, payload_str, event->payload_len);

        event->payload[event->payload_len] = '\0';
    }

    if(switch_queue_trypush(queue, event) == SWITCH_STATUS_SUCCESS) {
        return SWITCH_STATUS_SUCCESS;
    }

    ivs_event_free(event);
    return SWITCH_STATUS_FALSE;
}

switch_status_t ivs_event_push_dh(switch_queue_t *queue, uint32_t jid, uint32_t type, void *payload, uint32_t payload_len, mem_destroy_handler_t *payload_dh) {
    ivs_event_t *event = NULL;

    switch_assert(queue);

    switch_zmalloc(event, sizeof(ivs_event_t));
    event->jid = jid;
    event->type = type;
    event->payload_dh = payload_dh;
    event->payload_len = payload_len;

    if(payload_len) {
        switch_malloc(event->payload, payload_len);
        memcpy(event->payload, payload, payload_len);
    }

    if(switch_queue_trypush(queue, event) == SWITCH_STATUS_SUCCESS) {
        return SWITCH_STATUS_SUCCESS;
    }

    ivs_event_free(event);
    return SWITCH_STATUS_FALSE;
}


// -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------// chunk ready
static void ivs_event_payload_free_mchunk(ivs_event_payload_mchunk_t *chunk) {
    if(chunk) {
        switch_safe_free(chunk->data);
    }
}
switch_status_t ivs_event_push_chunk_ready(switch_queue_t *queue, uint32_t time, uint32_t length, switch_byte_t *data, uint32_t data_len) {
    ivs_event_payload_mchunk_t *mchunk = NULL;

    switch_zmalloc(mchunk, sizeof(ivs_event_payload_mchunk_t));
    mchunk->time = time;
    mchunk->length = length;
    mchunk->data_len = data_len;

    if(data_len) {
        switch_malloc(mchunk->data, data_len + 1);
        memcpy(mchunk->data, data, data_len);
        mchunk->data[data_len] = '\0';
    }

    return ivs_event_push_dh(queue, JID_NONE, IVS_EVENT_CHUNK_READY, mchunk, sizeof(ivs_event_payload_mchunk_t), (mem_destroy_handler_t *)ivs_event_payload_free_mchunk);
}

// -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// nlp-done
void ivs_event_payload_nlp_free(ivs_event_payload_nlp_t *payload) {
    if(payload) {
        switch_safe_free(payload->role);
        switch_safe_free(payload->text);
    }
}
switch_status_t ivs_event_payload_nlp_alloc(ivs_event_payload_nlp_t **payload, char *role, char *text) {
    ivs_event_payload_nlp_t *lpayload = NULL;

    switch_zmalloc(lpayload, sizeof(ivs_event_payload_nlp_t));
    lpayload->role = (role ? strdup(role) : NULL);
    lpayload->text = (text ? strdup(text) : NULL);

    *payload = lpayload;
    return SWITCH_STATUS_SUCCESS;
}
switch_status_t ivs_event_push_nlp(switch_queue_t *queue, uint32_t jid, char *role, char *text) {
    switch_status_t status = SWITCH_STATUS_SUCCESS;
    ivs_event_payload_nlp_t *payload = NULL;

    status = ivs_event_payload_nlp_alloc(&payload, role, text);
    if(status == SWITCH_STATUS_SUCCESS) {
        status = ivs_event_push_dh(queue, jid, IVS_EVENT_NLP_DONE, payload, sizeof(ivs_event_payload_nlp_t), (mem_destroy_handler_t *)ivs_event_payload_nlp_free);
    }

    return status;
}
switch_status_t ivs_event_push_nlp2(switch_queue_t *queue, uint32_t jid, ivs_event_payload_nlp_t *payload) {
    return ivs_event_push_dh(queue, jid, IVS_EVENT_NLP_DONE, payload, sizeof(ivs_event_payload_nlp_t), (mem_destroy_handler_t *)ivs_event_payload_nlp_free);
}

// -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// trabscription-done
void ivs_event_payload_transcription_free(ivs_event_payload_transcription_t *payload) {
    if(payload) {
        switch_safe_free(payload->text);
    }
}
switch_status_t ivs_event_payload_transcription_alloc(ivs_event_payload_transcription_t **payload, double confidence, char *text) {
    ivs_event_payload_transcription_t *lpayload = NULL;

    switch_zmalloc(lpayload, sizeof(ivs_event_payload_transcription_t));
    lpayload->confidence = confidence;
    lpayload->text = (text ? strdup(text) : NULL);

    *payload = lpayload;
    return SWITCH_STATUS_SUCCESS;
}

switch_status_t ivs_event_push_transcription(switch_queue_t *queue, uint32_t jid, double confidence, char *text) {
    switch_status_t status = SWITCH_STATUS_SUCCESS;
    ivs_event_payload_transcription_t *payload = NULL;

    status = ivs_event_payload_transcription_alloc(&payload, confidence, text);
    if(status == SWITCH_STATUS_SUCCESS) {
        status = ivs_event_push_dh(queue, jid, IVS_EVENT_TRANSCRIPTION_DONE, payload, sizeof(ivs_event_payload_transcription_t), (mem_destroy_handler_t *)ivs_event_payload_transcription_free);
    }

    return status;
}

switch_status_t ivs_event_push_transcription2(switch_queue_t *queue, uint32_t jid, ivs_event_payload_transcription_t *payload) {
    return ivs_event_push_dh(queue, jid, IVS_EVENT_TRANSCRIPTION_DONE, payload, sizeof(ivs_event_payload_transcription_t), (mem_destroy_handler_t *)ivs_event_payload_transcription_free);
}

