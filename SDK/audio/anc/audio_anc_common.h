
#ifndef _AUDIO_ANC_COMMON_H_
#define _AUDIO_ANC_COMMON_H_

#include "generic/typedef.h"
#include "app_config.h"
#include "audio_config_def.h"

//ANC进入产测模式
int audio_anc_production_enter(void);

//ANC退出产测模式
int audio_anc_production_exit(void);


#if TCFG_AUDIO_ANC_ENABLE

/*------------------------------------------------------------------*/
/*                         ANC_EXT 功能限制处理                     */
/*------------------------------------------------------------------*/

#if ((TCFG_AUDIO_ANC_ADAPTIVE_CMP_EN || TCFG_AUDIO_ANC_REAL_TIME_ADAPTIVE_ENABLE) && \
	(!TCFG_AUDIO_ANC_EAR_ADAPTIVE_EN))
#error "ANC自适应CMP、实时自适应必须先开启ANC耳道自适应"
#endif/*TCFG_AUDIO_ANC_EAR_ADAPTIVE_EN*/

#if TCFG_AUDIO_ANC_EAR_ADAPTIVE_EN && (TCFG_AUDIO_ANC_TRAIN_MODE != ANC_HYBRID_EN)
#error "ANC自适应，仅支持ANC HYBRID方案"
#endif/*TCFG_AUDIO_ANC_EAR_ADAPTIVE_EN*/

#if TCFG_AUDIO_FIT_DET_ENABLE && (TCFG_AUDIO_ANC_TRAIN_MODE == ANC_FF_EN)
#error "贴合度检测，仅支持带ANC FB的方案"
#endif/*TCFG_AUDIO_ANC_EAR_ADAPTIVE_EN*/

#if (TCFG_AUDIO_ANC_EXT_VERSION == ANC_EXT_V2) && \
	 (TCFG_AUDIO_ANC_EAR_ADAPTIVE_EN || \
	 TCFG_AUDIO_ANC_REAL_TIME_ADAPTIVE_ENABLE)
#define TCFG_AUDIO_ANC_EXT_TOOL_ENABLE		1
#else
#define TCFG_AUDIO_ANC_EXT_TOOL_ENABLE		0
#endif

#if TCFG_AUDIO_ADAPTIVE_EQ_ENABLE || TCFG_AUDIO_FIT_DET_ENABLE
//AUDIO(ANC)频响获取使能 - AFQ
#define TCFG_AUDIO_FREQUENCY_GET_ENABLE		1
#endif

#ifndef TCFG_AUDIO_FREQUENCY_GET_ENABLE
#define TCFG_AUDIO_FREQUENCY_GET_ENABLE		0
#endif

#ifndef TCFG_AUDIO_ANC_BASE_DEBUG_ENABLE
#define TCFG_AUDIO_ANC_BASE_DEBUG_ENABLE	1	//ANC调试模式 支持基础ANC调试，若flash不足，生产可关闭
#elif TCFG_ANC_BOX_ENABLE || TCFG_AUDIO_ANC_EAR_ADAPTIVE_EN || TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN
#undef TCFG_AUDIO_ANC_BASE_DEBUG_ENABLE
#define TCFG_AUDIO_ANC_BASE_DEBUG_ENABLE	1	// ANC调试模式 ANC扩展功能及串口调试必须打开
#endif

#endif/*TCFG_AUDIO_ANC_ENABLE*/

#endif/*_AUDIO_ANC_COMMON_H_*/
