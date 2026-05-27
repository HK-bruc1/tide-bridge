#pragma once
#include "generic/includes.h"
#include "ifly_common.h"
#ifdef TCFG_IFLYTEK_APP_ID
#define APP_ID       			TCFG_IFLYTEK_APP_ID
#else
#define APP_ID       			"123"
#endif

#define MAX_SPARKDESK_TOKENS   	((MAX_SPARKDESK_LEN - 100) / 6) //1个tokens相当于1.5个汉字，1个汉字最大4个byte，即1个tokens约6个byte
#define RECV_LAST_FRAME   		2


#define MAX_SPARKDESK_LEN   2000

typedef enum {
    IFLY_AI_EVT_RECV_OK = 1,	// AI对话完成。*param: ifly_ai_param*
    IFLY_AI_EVT_NETWORK_FAIL,			// 结束。*param: ifly_ai_param*
    IFLY_AI_EVT_EXIT,			// 结束。*param: ifly_ai_param*
} ifly_ai_event_enum ;

typedef struct {
    // 参数信息，所有参数都需要赋值
    char *content;		// 输入对话数据
    char *ai_res;		// 输出数据
    u32 ai_res_len;		// 输出数据buf长度，最大MAX_SPARKDESK_LEN
    int (*event_cb)(ifly_ai_event_enum evt, void *param);		// 事件回调
} ifly_ai_param;


typedef enum {
    IFLY_AI_STATUS_NULL = 0,
    IFLY_AI_STATUS_START,	// 启动
    IFLY_AI_STATUS_SEND,	// 数据已经发给socket
    IFLY_AI_STATUS_RECV,	// 有接受到数据
    IFLY_AI_STATUS_RECV_END,// 接受完成
    IFLY_AI_STATUS_RECV_ERROR,// 接受错误
    IFLY_AI_STATUS_EXIT,	// 已经退出
} ifly_ai_status;

typedef struct {
    u8 force_stop;
    u8 recv_finish;
    ifly_ai_status status;
    ifly_ai_param *param;
} sparkdesk_info_t;


typedef struct {
    ifly_ai_param ai_param;
    char ai_text[MAX_SPARKDESK_LEN];	// 对话文本。远端
} ifly_ai_struct;
