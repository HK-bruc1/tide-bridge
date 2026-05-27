#ifndef _AUDIO_COMMON_H_
#define _AUDIO_COMMON_H_

#include "generic/typedef.h"
#include "system/includes.h"

/************************************
             音频模块时钟
************************************/
#define	AUDIO_COMMON_CLK_DIG_SINGLE			(0)
#define	AUDIO_COMMON_CLK_DIF_XOSC			(1)

void audio_delay(int time_ms);
int audio_adc_analog_status_add_check(u8 ch_index, int add);
int audio_adc_digital_status_add_check(int add);

#endif // _AUDIO_COMMON_H_

