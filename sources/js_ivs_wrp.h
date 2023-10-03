/**
 * (C)2023 aks
 * https://akscf.me/
 * https://github.com/akscf/
 **/
#ifndef JS_IVS_WRP_H
#define JS_IVS_WRP_H

#include "mod_ivs.h"
#include "ivs_events.h"
#include "ivs_playback.h"

uint32_t js_ivs_async_playback(ivs_session_t *ivs_session, const char *path, uint8_t delete_file);
uint32_t js_ivs_async_say(ivs_session_t *ivs_session, const char *lang, const char *text);

#endif

