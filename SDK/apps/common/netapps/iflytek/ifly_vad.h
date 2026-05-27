#pragma once
#include "generic/includes.h"
#include "circular_buf.h"
#define MAX_VAD_LEN  1000

typedef enum {
    IFLY_VAD_EVT_AUDIO_START = 1,	// 音频启动。*param: ifly_vad_param*
    IFLY_VAD_EVT_RECV_OK,		// VAD接受数据完毕。*param: ifly_vad_param*
    IFLY_VAD_EVT_NETWORK_FAIL,  // 网络错误。*param: ifly_ai_param*
    IFLY_VAD_EVT_NETWORK_RECV_ERROR,  // 网络接收错误。*param: ifly_ai_param*
    IFLY_VAD_EVT_EXIT,			// 结束。*param: ifly_ai_param*
} ifly_vad_event_enum ;

typedef struct {
    // 参数信息，所有参数都需要赋值
    char *vad_res;		// 输出数据
    u32 vad_res_len;		// 输出数据buf长度，最大MAX_VAD_LEN
    int (*event_cb)(ifly_vad_event_enum evt, void *param);		// 事件回调
} ifly_vad_param;


#ifdef TCFG_IFLYTEK_APP_ID
#define APP_ID       				TCFG_IFLYTEK_APP_ID
#else
#define APP_ID       				"123"
#endif

#if TCFG_ENC_SPEEX_ENABLE
#define AI_AUDIO_CODING_TYPE    	AUDIO_CODING_SPEEX // 编码格式
#else
/* #error "ONLY SUPPORT SPEEX" */
#endif

#define AI_AUDIO_CODING_SR          16000 // 采样率。和audio_mic_enc_open()函数中的对应

#define PCM_OUT_BUF_LEN				(AI_AUDIO_CODING_SR*2/1000 * 30)

#define SPEEX_SIZE   				42

#define AUDIO_LEN    				168
#define BASE63_AUDIO_LEN    		256

#define STATUS_FIRST_FRAME      	0
#define STATUS_CONTINUE_FRAME   	1
#define STATUS_LAST_FRAME       	2
#define STATUS_NED_FRAME       		3

#define HEART_BEAT_REQ              "client ping"    // 服务器下发的心跳保持请求

#define IFLY_VAD_TASK_NAME			"ifly_vad"
typedef enum {
    IFLY_VAD_STATUS_NULL = 0,
    IFLY_VAD_STATUS_START,	// 启动
    IFLY_VAD_STATUS_PCM_START,	// 音频启动发数
    IFLY_VAD_STATUS_SENDING,	// 音频数据发数中
    IFLY_VAD_STATUS_SEND_END,	// 音频数据发数完毕
    IFLY_VAD_STATUS_RECV,		// 有接受到数据
    IFLY_VAD_STATUS_RECV_END,	// 接受完成
    IFLY_VAD_STATUS_RECV_ERROR,	// 接受错误
    IFLY_VAD_STATUS_EXIT,		// 已经退出
} ifly_vad_status;

typedef struct {
    u8 force_stop; 			// 强制结束
    u8 recv_finish;         // 接收结束
    u8 frame_status;
    char *pcm_out_buf;
    cbuffer_t pcm_cbuf;
    ifly_vad_status status;
    ifly_vad_param *param;
} vad_info_t;



extern bool ifly_vad_start(ifly_vad_param *param);

extern void ifly_vad_stop(u8 force_stop, u32 to_ms);
