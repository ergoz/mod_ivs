/**
 * (C)2023 aks
 * https://github.com/akscf/
 **/
#ifndef JS_IVS_HLP_H
#define JS_IVS_HLP_H

#include "mod_ivs.h"

const char *ivs_chunkType2name(uint32_t id);
uint32_t ivs_chunkType2id(const char *name);

const char *ivs_chunkEncoding2name(uint32_t id);
uint32_t ivs_chunkEncoding2id(const char *name);

const char *ivs_vadState2name(switch_vad_state_t st);
#endif

