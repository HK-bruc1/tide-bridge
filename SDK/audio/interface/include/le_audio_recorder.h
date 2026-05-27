/*************************************************************************************************/
/*!
*  \file      le_audio_recorder.h
*
*  \brief
*
*  Copyright (c) 2011-2023 ZhuHai Jieli Technology Co.,Ltd.
*
*/
/*************************************************************************************************/
#ifndef _LE_AUDIO_RECORDER_H_
#define _LE_AUDIO_RECORDER_H_

int le_audio_mic_recorder_open(void *params, void *le_audio, int latency);
void le_audio_mic_recorder_close(void);

#endif
