#include "ifly_socket.h"
#include "websocket_api.h"
#include "ifly_common.h"
#include "ifly_vad.h"

#if TCFG_IFLYTEK_ENABLE
typedef struct {
    struct websocket_struct socket_info;
    u8 force_stop; // 强制结束
} ifly_socket_info;

#define IFLY_SOCKET_KILL_TASK_NAME        "ifly_socket_kill"
static void websockets_callback(u8 *buf, u32 len, u8 type)
{
    printf("wbs recv msg : %s\n", buf);
}

static void websockets_client_reg(struct websocket_struct *websockets_info, ifly_socket_param *ifly_socket)
{
    memset(websockets_info, 0, sizeof(struct websocket_struct));
    websockets_info->_init           = websockets_client_socket_init;
    websockets_info->_exit           = websockets_client_socket_exit;
    websockets_info->_handshack      = webcockets_client_socket_handshack;
    websockets_info->_send           = websockets_client_socket_send;
    websockets_info->_recv_thread    = websockets_client_socket_recv_thread;
    websockets_info->_heart_thread   = websockets_client_socket_heart_thread;
    websockets_info->_recv_cb        = ifly_socket->recv_cb;
    websockets_info->_recv           = NULL;
    websockets_info->websocket_mode  = ifly_socket->socket_mode;
    websockets_info->kill_task       = IFLY_SOCKET_KILL_TASK_NAME;
}

static int websockets_client_init(struct websocket_struct *websockets_info, u8 *url, const char *origin_str, const char *user_agent_str)
{
    websockets_info->ip_or_url = url;
    websockets_info->origin_str = origin_str;
    websockets_info->user_agent_str = user_agent_str;
    websockets_info->recv_time_out = 1000;

    //应用层和库的版本检测，结构体不一样则返回出错
    int err = websockets_struct_check(sizeof(struct websocket_struct));
    if (err == FALSE) {
        return err;
    }
    return websockets_info->_init(websockets_info);
}

static int websockets_client_handshack(struct websocket_struct *websockets_info)
{
    return websockets_info->_handshack(websockets_info);
}

int websockets_client_send(struct websocket_struct *websockets_info, u8 *buf, int len, char type)
{
    //SSL加密时一次发送数据不能超过16K，用户需要自己分包
    return websockets_info->_send(websockets_info, buf, len, type);
}

static void websockets_client_exit(struct websocket_struct *websockets_info)
{
    websockets_info->_exit(websockets_info);
}


extern int task_kill(const char *name);
static void task_kill_callback_cb(char *task_name)
{
    printf("[msg]>>>>>>>>>>>*buf=%s", task_name);
    task_kill(task_name);
}

static void task_kill_callback(char *task_name)
{
    int msg[4];
    msg[0] = (int)task_kill_callback_cb;
    msg[1] = 1;
    msg[2] = (int)task_name;
    os_taskq_post_type("app_core", Q_CALLBACK, 3, msg);

}

static void ifly_socket_task_main(void *priv)
{
    int err;
    static unsigned int index = 0;
    ifly_socket_event_enum ifly_evt = IFLY_SOCKET_EVT_END;
    ifly_socket_param *ifly_socket = (ifly_socket_param *)priv;
    ifly_socket_info *info = (ifly_socket_info *)ifly_socket->socket_hdl;
    const char *origin_str = "http://coolaf.com";

    printf(" . ----------------- Client Websocket ------------------\r\n");

    /* 0 . malloc buffer */
    struct websocket_struct *websockets_info = &info->socket_info;
    /* 1 . register */
    websockets_client_reg(websockets_info, ifly_socket);

    /* 2 . init */
    err = websockets_client_init(websockets_info, (u8 *)ifly_socket->auth, origin_str, NULL);
    if (FALSE == err) {
        printf("  . ! Cilent websocket init error !!!\r\n");
        ifly_evt = IFLY_SOCKET_EVT_INIT_ERROR;
        goto exit_ws;
    }

    /* 3 . hanshack */
    err = websockets_client_handshack(websockets_info);
    if (FALSE == err) {
        printf("  . ! Handshake error !!!\r\n");
        ifly_evt = IFLY_SOCKET_EVT_HANSHACK_ERROR;
        goto exit_ws;
    }
    printf(" . Handshake success \r\n");

    /* 4 . CreateThread */
    err = os_task_create(websockets_info->_heart_thread,
                         websockets_info,
                         3,
                         1024 * 2,
                         0,
                         IFLY_CLIENT_TASK_NAME);
    if (err == 0) {
        websockets_info->ping_thread_id = 1;
    } else {
        websockets_info->ping_thread_id = 0;
    }
    err = os_task_create(websockets_info->_recv_thread,
                         websockets_info,
                         2,
                         1024,
                         0,
                         IFLY_RECV_TASK_NAME);
    if (err == 0) {
        websockets_info->recv_thread_id = 1;
    } else {
        websockets_info->recv_thread_id = 0;
    }

    ifly_socket->event_cb(IFLY_SOCKET_EVT_INIT_OK, ifly_socket);

    /* 5 . recv or send data */
    while (1) {

        putchar('l');
#if 0
        extern int send_file_via_websocket(struct websocket_struct * web_info, char type);
        int ret = send_file_via_websocket(websockets_info, WCT_TXTDATA);
        extern void ifly_tts_test(struct websocket_struct * web_info, char type);
        ifly_tts_test(websockets_info, WCT_TXTDATA);
#endif
        extern void ifly_sparkdesk_test_demo(struct websocket_struct * web_info, char type);
        ifly_sparkdesk_test_demo(websockets_info, WCT_TXTDATA);
        os_time_dly(5);
        index++;
    }


exit_ws:
    ifly_socket->event_cb(IFLY_SOCKET_EVT_EXIT, ifly_socket);
    /* 6 . exit */
    ifly_socket->event_cb(ifly_evt, ifly_socket);

    if (websockets_info->ping_thread_id) {
        websockets_info->ping_kill_flag = 1;
        /* task_kill_callback(IFLY_CLIENT_TASK_NAME); */
    }
    if (websockets_info->recv_thread_id) {
        websockets_info->recv_kill_flag = 1;
        /* task_kill_callback(IFLY_RECV_TASK_NAME); */
    }

    while (websockets_info->recv_kill_flag || websockets_info->ping_kill_flag) {
        os_time_dly(1);
    }
    websockets_client_exit(websockets_info);

    ifly_socket->event_cb(IFLY_SOCKET_EVT_EXIT, ifly_socket);

    free(ifly_socket->socket_hdl);

    /* task_kill_callback(ifly_socket->task_name); */
    /* ifly_socket->socket_hdl = NULL; */

    os_time_dly(-1);
}


void ifly_task_del()
{
    task_kill_callback("websocket_client_heart");
    task_kill_callback("websocket_client_recv");
    task_kill_callback("ifly_vad");
}

void ifly_tts_del()
{
    task_kill_callback("websocket_client_heart");
    task_kill_callback("websocket_client_recv");
    task_kill_callback("ifly_tts");
}
bool ifly_websocket_client_create(ifly_socket_param *ifly_socket)
{
    ifly_socket->socket_hdl = zalloc(sizeof(ifly_socket_info));
    if (ifly_socket->socket_hdl == NULL) {
        return false;
    }

    os_task_create(ifly_socket_task_main,
                   ifly_socket,
                   3,
                   1024 * 3,
                   0,
                   ifly_socket->task_name);

    return true;
}

#endif
