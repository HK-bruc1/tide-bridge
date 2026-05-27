
#ifndef _AUDIO_ANC_COMMON_PLUG_H_
#define _AUDIO_ANC_COMMON_PLUG_H_

#include "generic/typedef.h"

/********************************************************
	   				ANC 防破音
********************************************************/

/*ANC增益调节 音乐响度检测*/
void audio_anc_music_dynamic_gain_det(void *data, int len);

/*ANC增益调节 音乐响度初始化*/
void audio_anc_music_dynamic_gain_open(int sr, s16 thr);

/*ANC增益调节 音乐响度复位*/
void audio_anc_music_dynamic_gain_reset(void);

/*ANC增益调节 音乐响度挂起*/
void audio_anc_music_dynamic_gain_suspend(void);

void audio_anc_music_dynamic_fb_gain_reset(void);

void audio_anc_music_dynamic_gain_process(void);

void audio_anc_music_dynamic_gain_init(void);

/********************************************************
	   				场景自适应
********************************************************/
void audio_anc_power_adaptive_init(audio_anc_t *param);

void audio_anc_power_adaptive_tone_play(int ref_lvl, int err_lvl);

/*切换ANC自适应模式*/
void audio_anc_power_adaptive_mode_set(u8 mode, u8 lvl);

//用于功能互斥需要挂起场景自适应
void audio_anc_power_adaptive_suspend(void);

//用于功能互斥结束后恢复场景自适应
void audio_anc_power_adaptive_resume(void);


/********************************************************
  		   			产测ANC MIC增益补偿
********************************************************/
void audio_anc_mic_gain_cmp_init(void *mic_cmp);

int audio_anc_mic_gain_cmp_cmd_process(u8 cmd, u8 *buf, int len);

float audio_anc_mic_gain_cmp_get(u8 id);


#endif/*_AUDIO_ANC_COMMON_PLUG_H_*/

