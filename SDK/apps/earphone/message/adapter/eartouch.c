#include "app_msg.h"
#include "eartouch_manage.h"
#include "system/timer.h"
#include "sdk_config.h"
#include "app_config.h"
#include "bt_tws.h"

#if TCFG_LP_EARTCH_KEY_ENABLE || TCFG_OUTSIDE_EARTCH_ENABLE

eartouch_state_handle eartouch_state_hdl = {
    .local_st  = EARTOUCH_STATE_OUT_EAR,
    .remote_st = EARTOUCH_STATE_OUT_EAR,
};

#define __this (&eartouch_state_hdl)

eartouch_state_handle *get_eartouch_state_hdl(void)
{
    return __this;
}

eartouch_state get_local_eartouch_st(void)
{
    return __this->local_st;
}

eartouch_state  get_remote_eartouch_st(void)
{
    return __this->remote_st;
}

#if TCFG_USER_TWS_ENABLE
/* ***************************************************************************/
/**
 * \Brief :        TWS同步更新入耳状态,发送入耳消息
 *
 * \Param :        msg：入耳状态
 * \Param :        ret：同步调用发起方/接收方
 */
/* ***************************************************************************/
static void tws_eartouch_msg_sync_callback(int msg, int ret)
{
    line_inf
    printf("msg: %d, ret: %d", msg, ret);
    u8 state = msg;
    int event[2];
    event[0] = (state == EARTOUCH_STATE_IN_EAR) ? APP_MSG_EARTCH_IN_EAR : APP_MSG_EARTCH_OUT_EAR;
    if (ret == TWS_SYNC_CALL_RX) {
        __this->remote_st = state;
        event[1] = APP_EARTCH_MSG_FROM_TWS;
    } else {
        __this->local_st = state;
        event[1] = 0;
    }
    r_printf("CURRENT HEADSET %s REMOTE HEADSET %s", (__this->local_st) ? "OUT" : "IN", (__this->remote_st) ? "OUT" : "IN");
    os_taskq_post_type("app_core", MSG_FROM_EARTCH, 2, event);
}

TWS_SYNC_CALL_REGISTER(tws_eartouch_msg_sync_entry) = {
    .uuid = 0X2BAA5810,
    .func = tws_eartouch_msg_sync_callback,
};

void bt_tws_eartouch_msg_sync(int eartch_msg)
{
    tws_api_sync_call_by_uuid(0X2BAA5810, eartch_msg, (20 << 16) | 200);
}
#endif

/* --------------------------------------------------------------------------*/
/**
 * @brief 更新入耳状态,发送入耳检测事件
 *
 * @param state：本地入耳状态 EARTOUCH_STATE_IN_EAR / EARTOUCH_STATE_OUT_EAR
 */
/* ----------------------------------------------------------------------------*/
void eartouch_event_handler(u8 state)
{
#if TCFG_USER_TWS_ENABLE
    bt_tws_eartouch_msg_sync(state);
#else
    __this->local_st = state;
    r_printf("EARTCH STA: %s ", (__this->local_st) ? "OUT" : "IN");
    int msg = (state == EARTOUCH_STATE_IN_EAR) ? APP_MSG_EARTCH_IN_EAR : APP_MSG_EARTCH_OUT_EAR;
    os_taskq_post_type("app_core", MSG_FROM_EARTCH, 1, &msg);
#endif
}

#endif/* #if TCFG_LP_EARTCH_KEY_ENABLE || TCFG_OUTSIDE_EARTCH_ENABLE */

