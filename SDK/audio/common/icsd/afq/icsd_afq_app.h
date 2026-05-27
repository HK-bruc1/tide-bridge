#ifndef _ICSD_AFQ_APP_H
#define _ICSD_AFQ_APP_H

#include "typedef.h"
#include "icsd_afq.h"

#define ICSD_AFQ_EN_MODE_NEXT_SW		1	/*提示音播完才允许下一次切换*/

//ICSD AFQ 状态
enum {
    ICSD_AFQ_STATE_CLOSE = 0,
    ICSD_AFQ_STATE_OPEN,
};

//ICSD AFQ TONE状态
enum {
    ICSD_AFQ_STATE_TONE_STOP = 0,
    ICSD_AFQ_STATE_TONE_START,
};

//查询afq是否活动中
u8 audio_icsd_afq_is_running(void);

void icsd_afq_output(__afq_output *_AFQ_OUT);

int audio_icsd_afq_open(u8 tone_en);

int audio_icsd_afq_init(void);

int audio_icsd_afq_output(__afq_output *out);

int audio_icsd_afq_close();

#endif
