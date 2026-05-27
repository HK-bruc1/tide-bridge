#pragma once
#include "ifly_common.h"
#include "generic/includes.h"
#include "circular_buf.h"


#define TTS_AUDIO_SAVE_TEST     0
#if TTS_AUDIO_SAVE_TEST
static FILE *save_file = NULL;
#endif

#ifdef TCFG_IFLYTEK_APP_ID
#define APP_ID       			TCFG_IFLYTEK_APP_ID
#else
#define APP_ID       			"123"
#endif

#define RECV_LAST_FRAME  		2

#define IFLY_TTS_PKG_MAX        (1024 * 10)   //经测试，最长一包下发数据的长度为8500bytes左右
#define IFLY_TTS_CBUF_LEN       (IFLY_TTS_PKG_MAX + 4096)
#define TTS_TIMEOUT_TIME  		8  //防止tts在播放过程中收不到消息而卡住，8s没接收消息，就关闭任务

#define MAX_SPARKDESK_LEN   2000
typedef enum {
    IFLY_TTS_EVT_PLAY_START = 1,	// 播放开始。*param: ifly_tts_param*
    IFLY_TTS_EVT_PLAY_STOP,			// 播放结束。*param: ifly_tts_param*
    IFLY_TTS_EVT_PLAY_FAIL_STOP,	// 播放非正常结束。例如超时、强制结束等。*param: ifly_tts_param*
    IFLY_TTS_EVT_EXIT,				// 结束。*param: ifly_tts_param*
} ifly_tts_event_enum ;

typedef struct {
    // 参数信息，所有参数都需要赋值
    char *text_res;		// 文本数据
    int (*event_cb)(ifly_tts_event_enum evt, void *param);		// 事件回调
} ifly_tts_param;

typedef struct {
    ifly_tts_param tts_param;
    char ai_text[MAX_SPARKDESK_LEN];	// 对话文本。远端
} ifly_tts_struct;

typedef enum {
    IFLY_TTS_STATUS_NULL = 0,
    IFLY_TTS_STATUS_START,	// 启动
    IFLY_TTS_STATUS_SEND,	// 数据已经发给socket
    IFLY_TTS_STATUS_RECV,	// 有接受到数据
    IFLY_TTS_STATUS_RECV_END,// 接受完成
    IFLY_TTS_STATUS_PLAY_END,// 播放完
    IFLY_TTS_STATUS_RECV_ERROR,// 接受错误
    IFLY_TTS_STATUS_EXIT,	// 已经退出
} ifly_tts_status;

typedef struct {
    u8 force_stop; 			// 强制结束
    u8 recv_finish;         // 接受完毕
    u8  tts_to_cnt;			// 超时计数
    u16 tts_timer;			// 超时用的timer ID
    cbuffer_t dec_cbuf;     // 用于tts音频播放的cbuf
    char *dec_out_buf;
    ifly_tts_status status;
    ifly_tts_param *param;
} tts_info_t;
