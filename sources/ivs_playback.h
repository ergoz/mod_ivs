/**
 * (C)2023 aks
 * https://github.com/akscf/
 **/
#ifndef IVS_PLAYBACK_H
#define IVS_PLAYBACK_H

#include <mod_ivs.h>

switch_status_t ivs_say(ivs_session_t *ivs_session, char *language, char *text, uint8_t async);
switch_status_t ivs_playback(ivs_session_t *ivs_session, char *path, uint8_t async);
switch_status_t ivs_playback_stop(ivs_session_t *ivs_session);


#endif
