/**
 * (C)2023 aks
 * https://github.com/akscf/
 **/
#ifndef IVS_EVENTS_H
#define IVS_EVENTS_H

#include <mod_ivs.h>

#define IVS_EVENT_NOP                       0x00
#define IVS_EVENT_SPEAKING_START            0x01
#define IVS_EVENT_SPEAKING_STOP             0x02
#define IVS_EVENT_CHUNK_READY               0x03
#define IVS_EVENT_PLAYBACK_STARTED          0x04
#define IVS_EVENT_PLAYBACK_FINISHED         0x05
#define IVS_EVENT_TRANSCRIPTION_DONE        0x06
#define IVS_EVENT_NLP_DONE                  0x07
#define IVS_EVENT_CURL_DONE                 0x08


typedef void (mem_destroy_handler_t)(void *data);

// -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
typedef struct {
    uint32_t                type;
    uint32_t                jid;
    uint32_t                payload_len;
    uint8_t                 *payload;
    mem_destroy_handler_t   *payload_dh;
} ivs_event_t;

void ivs_event_free(ivs_event_t *event);
void ivs_events_queue_clean(switch_queue_t *queue);

#define ivs_event_push(queue, jid, type, payload, payload_len) ivs_event_push_dh(queue, jid, type, payload, payload_len, NULL);
switch_status_t ivs_event_push_simple(switch_queue_t *queue, uint32_t type, char *payload_str);
switch_status_t ivs_event_push_dh(switch_queue_t *queue, uint32_t jid, uint32_t type, void *payload, uint32_t payload_len, mem_destroy_handler_t *payload_dh);

// -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
/* chunk ready , L16 codec */
typedef struct {
    uint32_t        samplerate;
    uint32_t        channels;
    uint32_t        time;       // chunk sec
    uint32_t        length;     // chunk bytes
    uint32_t        data_len;
    uint8_t         *data;      // filename or raw data
} ivs_event_payload_mchunk_t;
switch_status_t ivs_event_push_chunk_ready(switch_queue_t *queue, uint32_t samplerate, uint32_t channels, uint32_t time, uint32_t length, switch_byte_t *data, uint32_t data_len);

// -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
/* nlp result */
typedef struct {
    char        *role;
    char        *text;
} ivs_event_payload_nlp_t;
void ivs_event_payload_nlp_free(ivs_event_payload_nlp_t *payload);
switch_status_t ivs_event_payload_nlp_alloc(ivs_event_payload_nlp_t **payload, char *role, char *text);
switch_status_t ivs_event_push_nlp(switch_queue_t *queue, uint32_t jid, char *role, char *text);
switch_status_t ivs_event_push_nlp2(switch_queue_t *queue, uint32_t jid, ivs_event_payload_nlp_t *payload);

// -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
/* transcript result */
typedef struct {
    double      confidence;
    char        *text;
} ivs_event_payload_transcription_t;
void ivs_event_payload_transcription_free(ivs_event_payload_transcription_t *payload);
switch_status_t ivs_event_payload_transcription_alloc(ivs_event_payload_transcription_t **payload, double confidence, char *text);
switch_status_t ivs_event_push_transcription(switch_queue_t *queue, uint32_t jid, double confidence, char *text);
switch_status_t ivs_event_push_transcription2(switch_queue_t *queue, uint32_t jid, ivs_event_payload_transcription_t *payload);

// -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
/* curl result */
typedef struct {
    char        *body;
    uint32_t    body_len;
    uint32_t    http_code;
} ivs_event_payload_curl_t;
void ivs_event_payload_curl_free(ivs_event_payload_curl_t *payload);
switch_status_t ivs_event_payload_curl_alloc(ivs_event_payload_curl_t **payload, uint32_t http_code, char *body, uint32_t body_len);
switch_status_t ivs_event_push_curl(switch_queue_t *queue, uint32_t jid, uint32_t http_code, char *body, uint32_t body_len);
switch_status_t ivs_event_push_curl2(switch_queue_t *queue, uint32_t jid, ivs_event_payload_curl_t *payload);

#endif
