#ifndef _RT_ANC_APP_H_
#define _RT_ANC_APP_H_

#include "typedef.h"
#include "asm/anc.h"
#include "anc_ext_tool.h"
#include "icsd_adt_app.h"

#define AUDIO_RT_ANC_PARAM_BY_TOOL_DEBUG		1		//RT ANC 参数使用工具debug 调试
#define AUDIO_RT_ANC_EXPORT_TOOL_DATA_DEBUG		1		//支持导出工具debug数据
#define AUDIO_RT_ANC_SZ_PZ_CMP_EN				1		//RTANC SZ/PZ补偿使能

#define AUDIO_RT_ANC_TIDY_MODE_ENABLE			1		//通话播歌使用轻量级的RTANC
#define AUDIO_RT_ANC_EVERY_TIME					0		//每次切ANC都进行RTANC
#define AUDIO_RT_ANC_SELF_TALK_FLAG				0		//输出自讲标志

//RT ANC 状态
enum {
    RT_ANC_STATE_CLOSE = 0,
    RT_ANC_STATE_OPEN,
    RT_ANC_STATE_SUSPEND,
};

struct rt_anc_fade_gain_ctr {
    u16 lff_gain;
    u16 lfb_gain;
    u16 rff_gain;
    u16 rfb_gain;
};


int audio_adt_rtanc_set_infmt(void *rtanc_tool);

void audio_adt_rtanc_output_handle(void *rt_param_l, void *rt_param_r);

u8 audio_rtanc_app_func_en_get(void);

int audio_rtanc_adaptive_en(u8 en);

int audio_rtanc_fade_gain_suspend(struct rt_anc_fade_gain_ctr *ctr);

int audio_rtanc_fade_gain_resume(void);

//对外API
void audio_anc_real_time_adaptive_suspend(void);

void audio_anc_real_time_adaptive_resume(void);

u8 audio_anc_real_time_adaptive_run_busy_get(void);

u8 audio_anc_real_time_adaptive_state_get(void);

void audio_anc_real_time_adaptive_init(audio_anc_t *param);

int audio_anc_real_time_adaptive_open(void);

int audio_anc_real_time_adaptive_close(void);

int audio_anc_real_time_adaptive_tool_data_get(u8 **buf, u32 *len);

int audio_anc_real_time_adaptive_suspend_get(void);

int audio_anc_real_time_adaptive_reset(int rtanc_mode, u8 wind_close);

float *audio_rtanc_pz_cmp_get(void);

float *audio_rtanc_sz_cmp_get(void);

void audio_real_time_adaptive_ignore_switch_lock(void);

void audio_rtanc_self_talk_output(u8 flag);

int audio_rtanc_adaptive_init(u8 sync_mode);

int audio_rtanc_adaptive_exit(void);

void audio_rtanc_cmp_data_packet(void);

void audio_rtanc_cmp_update(void);

#endif


