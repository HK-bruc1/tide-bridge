
#ifndef _AUDIO_ANC_DEBUG_TOOL_H_
#define _AUDIO_ANC_DEBUG_TOOL_H_

#include "app_anctool.h"

#define AUDIO_ANC_DEBUG_CMD_RTANC_EN		0		//RTANC相关验证、测试命令debug使能

enum {
    ANC_DEBUG_STA_CLOSE = 0,
    ANC_DEBUG_STA_OPEN,
    ANC_DEBUG_STA_RUN,
};

enum {
    ANC_DEBUG_APP_CMD_AEQ = 0,
    ANC_DEBUG_APP_CMD_RTANC,
};

//debug 启动
void audio_anc_debug_tool_open(void);

//debug 关闭
void audio_anc_debug_tool_close(void);

/*
   debug 数据发送
   return len表示写入成功，0则写入失败
*/
int audio_anc_debug_send_data(u8 *buf, int len);

//APP层数据写入
void audio_anc_debug_app_send_data(u8 cmd, u8 cmd_2nd, u8 *buf, int len);

u8 audio_anc_debug_busy_get(void);

//自定义命令处理函数
int audio_anc_debug_user_cmd_process(u8 *data, int len);

#endif/*_AUDIO_ANC_DEBUG_TOOL_H_*/
