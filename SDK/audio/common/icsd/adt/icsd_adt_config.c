#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".icsd_adt.data.bss")
#pragma data_seg(".icsd_adt.data")
#pragma const_seg(".icsd_adt.text.const")
#pragma code_seg(".icsd_adt.text")
#endif

#include "app_config.h"
#if ((defined TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN) && TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN && \
	 TCFG_AUDIO_ANC_ENABLE)
#include "icsd_adt.h"
#include "icsd_adt_app.h"

//==============================================//
//    功能使能
//==============================================//
#if TCFG_AUDIO_SPEAK_TO_CHAT_ENABLE
const u8 ICSD_VDT_EN        =  1;//智能免摘使能
#else
const u8 ICSD_VDT_EN        =  0;
#endif

#if TCFG_AUDIO_ANC_WIND_NOISE_DET_ENABLE
const u8 ICSD_WIND_EN 		=  1;//风噪检测使能
#else
const u8 ICSD_WIND_EN 		=  0;
#endif

#if TCFG_AUDIO_WIDE_AREA_TAP_ENABLE
const u8 ICSD_WAT_EN        =  1;//广域点击使能
#else
const u8 ICSD_WAT_EN        =  0;
#endif

#if TCFG_AUDIO_ANC_REAL_TIME_ADAPTIVE_ENABLE
const u8 ICSD_RTANC_EN		=  1;//实时自适应ANC
#else
const u8 ICSD_RTANC_EN		=  0;
#endif

#if TCFG_AUDIO_ANC_ENV_NOISE_DET_ENABLE
const u8 ICSD_AVC_EN  		=  1;//自适应音量
#else
const u8 ICSD_AVC_EN  		=  0;//自适应音量
#endif

#if TCFG_AUDIO_ADAPTIVE_DCC_ENABLE
const u8 ICSD_ADJDCC_EN     =  1;
#else
const u8 ICSD_ADJDCC_EN     =  0;
#endif

#if TCFG_AUDIO_ANC_HOWLING_DET_ENABLE
const u8 ICSD_HOWL_EN 		=  1;//啸叫检测
#else
const u8 ICSD_HOWL_EN 		=  0;//啸叫检测
#endif

const u8 ICSD_ENVNL_EN		=  0;//环境声检测使能
const u8 ICSD_RTAEQ_EN		=  0;//实时自适应EQ
const u8 ICSD_EIN_EN  		=  0;//入耳检测
const u8 ICSD_46KOUT_EN     =  0;//ANC 48K数据输出
const u8 ICSD_ADT_EP_TYPE   =  ADT_TWS;

const u8 VDT_DATA_CHECK     = 0;//串口查看免摘数据通路
const u8 WIND_DATA_CHECK    = 0;//串口查看风噪数据通路
const u8 BT_ADT_INF_EN	    = 0;//查看ADT启动信息
const u8 BT_ADT_DP_STATE_EN = 0;//查看ADT数据通路状态
const u8 mic_input_v2       = 1;//使用新MIC数据接口
const u8 ICSD_WDT_V2        = 1;//风噪快速输出
const u8 ICSD_HOWL_REF_EN   = 0;//使能后使用REF实现HOWL，该模式只支持同时打开风噪检测

const u8 avc_run_interval   = 32;//实际运行间隔为  avc_run_interval * 11ms
const u8 tidy_avc_run_interval = 16;//实际运行间隔为  tidy_avc_run_interval * 16ms
//==============================================//
//    环境声参数配置
//==============================================//
const float env_alpha 		= 0.25;//0 ~ 1之间,越大越快

#endif/*TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN*/
