/**
 * (C)2023 aks
 * https://akscf.me/
 * https://github.com/akscf/
 **/
#ifndef IVS_QJS_H
#define IVS_QJS_H

#include "mod_ivs.h"

// Curl
typedef struct {
    uint32_t                timeout;
    uint32_t                response_length;
    uint8_t                 fl_ignore_rdata;
    uint8_t                 fl_ssl_verfypeer;
    uint8_t                 fl_ssl_verfyhost;
    char                    *url;
    char                    *proxy;
    char                    *cacert;
    char                    *cacert_proxy;
    char                    *method;
    char                    *credentials;
    char                    *content_type;
    char                    *response_buffer;
    char                    *credentials_proxy;
    switch_memory_pool_t    *pool;
} js_curl_t;
JSClassID js_curl_get_classid(JSContext *ctx);
switch_status_t js_curl_class_register(JSContext *ctx, JSValue global_obj);

// File
typedef struct {
    uint8_t                 is_open;
    uint8_t                 tmp_file;
    uint8_t                 type;
    uint32_t                flags;
    switch_size_t           rdbuf_size;
    char                    *path;
    char                    *name;
    char                    *rdbuf;
    switch_file_t           *fd;
    switch_dir_t            *dir;
    switch_memory_pool_t    *pool;
} js_file_t;
JSClassID js_file_get_classid(JSContext *ctx);
switch_status_t js_file_class_register(JSContext *ctx, JSValue global_obj);

// Session
typedef struct {
    uint8_t                 fl_hup_auto;
    uint8_t                 fl_hup_hook;
    switch_core_session_t   *session;
    JSValue                 on_hangup;
    JSContext               *ctx;
} js_session_t;
JSClassID js_seesion_get_classid(JSContext *ctx);
switch_status_t js_session_class_register(JSContext *ctx, JSValue global_obj);

// IVS
typedef struct {
    uint8_t                 fl_destroyed;
    ivs_session_t           *session;
    JSContext               *ctx;
} js_ivs_t;
JSClassID js_ivs_get_classid(JSContext *ctx);
switch_status_t js_ivs_class_register(JSContext *ctx, JSValue global_obj);
JSValue js_ivs_object_create(JSContext *ctx, ivs_session_t *ivs_session);

// ChatGPT
typedef struct {
    switch_memory_pool_t    *pool;
    char                    *chat_model;
    char                    *whisper_model;
    char                    *apikey;
    char                    *role;
    char                    *proxy;
    char                    *proxy_credentials;
    char                    *user_agent;
    uint32_t                request_timeout;
    uint32_t                connect_timeout;
    uint8_t                 fl_log_http_errors;
} js_chatgpt_t;
JSClassID js_chatgpt_get_classid(JSContext *ctx);
switch_status_t js_chatgpt_class_register(JSContext *ctx, JSValue global_obj);

// -----------------------------------------------------------------------------------------------------
void *SWITCH_THREAD_FUNC script_maintenance_thread(switch_thread_t *thread, void *obj);
void js_dump_error(ivs_script_t *script, JSContext *ctx);
switch_status_t js_script_init(ivs_session_t *ivs_session, char *script_path, char *script_args);
switch_status_t js_script_destroy(ivs_session_t *ivs_session);
switch_status_t js_register_classid(JSRuntime *rt, const char *class_name, JSClassID class_id);
JSClassID js_lookup_classid(JSRuntime *rt, const char *class_name);


#endif
