#ifndef __RDX_PLAYBACK_CONFIG_H__
#define __RDX_PLAYBACK_CONFIG_H__

#include "app_config.h"

#ifndef TCFG_RDX_LOCAL_PLAYBACK_ENABLE
#define TCFG_RDX_LOCAL_PLAYBACK_ENABLE            0
#endif

#if TCFG_RDX_LOCAL_PLAYBACK_ENABLE && !TCFG_SOURCE_DEV0_NODE_ENABLE
#error "RDX local playback requires TCFG_SOURCE_DEV0_NODE_ENABLE"
#endif

#if TCFG_RDX_LOCAL_PLAYBACK_ENABLE && !TCFG_DEC_STENC_OPUS_ENABLE
#error "RDX local playback requires the stereo Opus decoder"
#endif

#if TCFG_RDX_LOCAL_PLAYBACK_ENABLE && !(THIRD_PARTY_PROTOCOLS_SEL & RDX_EN)
#error "RDX local playback requires RDX protocol support"
#endif

#endif
