#pragma once
#include "generic/includes.h"
typedef enum {
    IFLY_SOCKET_EVT_SEND_OK = 1,	// socket发数成功。*param: 发数buf
    IFLY_SOCKET_EVT_SEND_ERROR,		// socket发数失败。*param: 发数buf

    IFLY_SOCKET_EVT_INIT_ERROR,		// socket初始化失败。*param: ifly_socket_param*
    IFLY_SOCKET_EVT_HANSHACK_ERROR,	// socket握手失败。*param: ifly_socket_param*

    IFLY_SOCKET_EVT_INIT_OK,		// socket初始化成功。*param: ifly_socket_param*

    IFLY_SOCKET_EVT_ACCIDENT_END,			//意外结束。。*param: ifly_socket_param*

    IFLY_SOCKET_EVT_END,			// 结束。socket释放资源之前。*param: ifly_socket_param*
    IFLY_SOCKET_EVT_FORCE_END,		// 强制结束。socket释放资源之前。*param: ifly_socket_param*
    IFLY_SOCKET_EVT_EXIT,			// 结束。socket释放资源之后。*param: ifly_socket_param*
} ifly_socket_event_enum ;

typedef struct {
    // 参数信息，所有参数都需要赋值
    char *task_name;	// 任务名
    char *auth;			// 鉴权buf
    u8 socket_mode;		// 是否加密。WEBSOCKET_MODE, WEBSOCKETS_MODE
    void (*recv_cb)(u8 *buf, u32 len, u8 type);	// 收数回调
    bool (*get_send)(char **buf, u32 *len);		// 获取发数buf
    int (*event_cb)(ifly_socket_event_enum evt, void *param);		// 事件回调
    // 模块内部记录信息，不需要赋值
    void *socket_hdl;	// socket句柄
} ifly_socket_param;

#define IFLY_CLIENT_TASK_NAME		"ifly_client_heart"
#define IFLY_RECV_TASK_NAME			"ifly_client_recv"

extern bool ifly_websocket_client_create(ifly_socket_param *ifly_socket);
