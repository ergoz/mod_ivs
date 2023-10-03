/**
 * (C)2023 aks
 * https://akscf.me/
 * https://github.com/akscf/
 **/
#ifndef IVS_CURL_H
#define IVS_CURL_H

#include <mod_ivs.h>

#define CURL_FIELD_TYPE_SIMPLE  0
#define CURL_FIELD_TYPE_FILE    1

typedef struct {
    int             type;
    char            *name;
    char            *value;
    void            *next;
} xcurl_from_field_t;

typedef struct {
    switch_memory_pool_t    *pool;
    xcurl_from_field_t      *fields;
    xcurl_from_field_t      *fields_tail;
    char                    *url;
    char                    *proxy;
    char                    *proxy_credentials;
    char                    *credentials;
    char                    *content_type;
    char                    *user_agent;
    switch_byte_t           *send_buffer_ref;    // use in write handler
    switch_byte_t           *send_buffer;        // original for help
    switch_buffer_t         *recv_buffer;
    uint32_t                send_buffer_len;
    uint32_t                request_timeout;
    uint32_t                connect_timeout;
    long                    curl_auth_type; // CURLAUTH_ANY, CURLAUTH_BASIC, CURLAUTH_BEARER, ...
    uint32_t                http_error;
    uint8_t                 fl_ext_pool;
} curl_conf_t;

void curl_config_free(curl_conf_t *curl_config);
switch_status_t curl_config_alloc(curl_conf_t **curl_config, switch_memory_pool_t *pool, uint8_t with_recvbuff);
switch_status_t curl_perform(curl_conf_t *curl_config);
switch_status_t curl_field_add(curl_conf_t *curl_config, int type, char *name, char *value);

#endif
