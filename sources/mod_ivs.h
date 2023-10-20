/**
 * (C)2023 aks
 * https://github.com/akscf/
 **/
#ifndef MOD_IVS_H
#define MOD_IVS_H

#include <switch.h>
#include <switch_stun.h>
#include <switch_curl.h>
#include <stdint.h>
#include <string.h>
#include <quickjs.h>
#include <quickjs-libc.h>

#ifndef true
#define true SWITCH_TRUE
#endif
#ifndef false
#define false SWITCH_FALSE
#endif

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
#define BIT_SET(a,b)   ((a) |= (1UL<<(b)))
#define BIT_CLEAR(a,b) ((a) &= ~(1UL<<(b)))
#define BIT_CHECK(a,b) (!!((a) & (1UL<<(b))))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define BASE64_ENC_SZ(n) (4*(n/3))
#define BASE64_DEC_SZ(n) ((n*3)/4)

#define IVS_VERSION                     "1.0 (a52)"
#define AUDIO_BUFFER_SIZE               (8*1024) // SWITCH_RECOMMENDED_BUFFER_SIZE
#define AUDIO_QUEUE_SIZE                64
#define EVENTS_QUEUE_SIZE               128
#define VAD_STORE_FRAMES                64
#define VAD_RECOVERY_FRAMES             15

#define IVS_CHUNK_TYPE_BUFFER           0
#define IVS_CHUNK_TYPE_FILE             1
#define IVS_CHUNK_ENCODING_NONE         0 // not defined
#define IVS_CHUNK_ENCODING_WAV          1 // FILE_WAV
#define IVS_CHUNK_ENCODING_MP3          2 // FILE_MP3
#define IVS_CHUNK_ENCODING_RAW          3 // BUFFER_L16
#define IVS_CHUNK_ENCODING_B64          4 // BUFFER_BASE64

#define JID_NONE                        0x0

#define IVS_SF_PLAYBACK                 0x0

#define IVS_EVENTSQ(ivs_session)     (ivs_session->events)

typedef struct {
    switch_mutex_t          *mutex;
    switch_mutex_t          *mutex_sessions;
    switch_mutex_t          *mutex_profiles;
    switch_hash_t           *sessions;
    switch_hash_t           *profiles;
    char                    *default_tts_engine;
    char                    *default_asr_engine;
    char                    *default_language;
    uint32_t                active_threads;
    uint32_t                cfg_cng_lvl;
    uint32_t                cfg_chunk_len_sec;
    uint32_t                cfg_vad_silence_ms;
    uint32_t                cfg_vad_voice_ms;
    uint32_t                cfg_vad_threshold;
    uint8_t                 cfg_vad_debug;
    uint8_t                 fl_ready;
    uint8_t                 fl_shutdown;
} globals_t;

typedef struct {
    switch_memory_pool_t    *pool;
    switch_mutex_t          *mutex_classes_map;
    switch_hash_t           *classes_map;
    const char              *id;
    const char              *path;
    const char              *name;
    char                    *args;
    char                    *body;
    switch_size_t           body_len;
    uint8_t                 fl_interrupt;
    uint8_t                 fl_destroyed;
} ivs_script_t;

typedef struct {
    switch_core_session_t   *session;
    switch_mutex_t          *mutex;
    switch_mutex_t          *mutex_xflags;
    switch_queue_t          *au_q_in;
    switch_queue_t          *au_q_out;
    switch_queue_t          *events;
    ivs_script_t            *script;
    const char              *session_id;
    const char              *caller_number;
    const char              *called_number;
    const char              *language;
    const char              *tts_engine;
    const char              *asr_engine;
    switch_vad_state_t      vad_state;
    time_t                  start_ts;
    uint32_t                chunk_encoding;
    uint32_t                chunk_type;
    uint32_t                job_id_cnt;
    uint32_t                wlocki;
    uint32_t                samplerate;
    uint32_t                channels;
    uint32_t                ptime;
    uint32_t                encoded_bytes_per_packet;
    uint32_t                decoded_bytes_per_packet;
    uint32_t                vad_buffer_size;
    uint32_t                chunk_buffer_size;
    uint32_t                xflags;
    uint8_t                 fl_ready;
    uint8_t                 fl_do_destroy;
    uint8_t                 fl_destroyed;
} ivs_session_t;

typedef struct {
    uint32_t    samplerate;
    uint32_t    channels;
    uint32_t    len;
    uint8_t     *data;
} xdata_buffer_t;

/* utils.c */
void launch_thread(switch_memory_pool_t *pool, switch_thread_start_t fun, void *data);
void thread_finished();

ivs_session_t *ivs_session_lookup(char *name, uint8_t lock);
uint32_t ivs_session_take(ivs_session_t *session);
void ivs_session_release(ivs_session_t *session);
int ivs_session_xflags_test(ivs_session_t *ivs_session, int flag);
void ivs_session_xflags_set(ivs_session_t *ivs_session, int flag, int val);
uint32_t ivs_gen_job_id(ivs_session_t *session);

switch_status_t xdata_buffer_push(switch_queue_t *queue, switch_byte_t *data, uint32_t data_len, uint32_t samplerate, uint8_t channels);
switch_status_t xdata_buffer_alloc(xdata_buffer_t **out, switch_byte_t *data, uint32_t data_len, uint32_t samplerate, uint8_t channels);
void xdata_buffer_free(xdata_buffer_t *buf);
void xdata_buffer_queue_clean(switch_queue_t *queue);

char *audio_file_write(switch_byte_t *buf, uint32_t buf_len, uint32_t samplerate, uint32_t channels, const char *file_ext);

char *safe_pool_strdup(switch_memory_pool_t *pool, const char *str);
uint8_t *safe_pool_bufdup(switch_memory_pool_t *pool, uint8_t *buffer, switch_size_t len);

#endif
