/*********************************************************************************************
    *   Filename        : app_connected.c

    *   Description     :

    *   Author          : Weixin Liang

    *   Email           : liangweixin@zh-jieli.com

    *   Last modifiled  : 2022-12-15 14:30

    *   Copyright:(c)JIELI  2011-2022  @ , All Rights Reserved.
*********************************************************************************************/
#include "system/includes.h"
#include "cig.h"
#include "le_connected.h"
#include "app_le_connected.h"
#include "app_config.h"
#include "app_tone.h"
#include "btstack/avctp_user.h"
#include "audio_config.h"
#include "tone_player.h"
#include "app_main.h"
#include "ble_rcsp_server.h"
#include "wireless_trans.h"
#include "bt_common.h"
#include "user_cfg.h"
#include "btstack/bluetooth.h"
#include "btstack/le/ble_api.h"
#include "btstack/le/le_user.h"
#include "multi_protocol_main.h"
#include "earphone.h"
#include "vol_sync.h"
#if TCFG_USER_TWS_ENABLE
#include "classic/tws_api.h"
#include "tws_dual_conn.h"
#else
#include "dual_conn.h"
#endif
#if (THIRD_PARTY_PROTOCOLS_SEL & RCSP_MODE_EN)
#include "ble_rcsp_server.h"
#endif
#include "vol_sync.h"

#if ((TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_UNICAST_SINK_EN | LE_AUDIO_JL_UNICAST_SINK_EN)))
struct le_audio_var {
    u8 le_audio_profile_ok;				// cig 初始化成功标志
    u8 le_audio_en_config;				// cig 功能是否使能
    u8 cig_phone_conn_status;			// cig 当前tws耳机le_audio连接状态
    u8 cig_phone_other_conn_status;		// cig 另外一个tws耳机le_audio连接状态
    u8 peer_address[6];					// cig 当前连接的设备地址
    u8 le_audio_tws_role;				// 当前tws主从角色，0:tws主机，1:tws从机
    u8 le_audio_adv_connected;			// leaudio广播连接状态，0xAA:已连上，0:已断开
};
static struct le_audio_var g_le_audio_hdl;
extern void ble_vendor_priv_cmd_handle_register(u16(*handle)(u16 hdl, u8 *cmd, u8 *rsp));
extern int ll_hci_vendor_send_priv_cmd(u16 conn_handle, u8 *data, u16 size); //通过hci命令发

/**************************************************************************************************
  Macros
**************************************************************************************************/
#define LOG_TAG             "[APP_LE_CONNECTED]"
#define LOG_ERROR_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_INFO_ENABLE
/* #define LOG_DUMP_ENABLE */
#define LOG_CLI_ENABLE
#include "debug.h"

/**************************************************************************************************
  Data Types
**************************************************************************************************/

struct app_cis_conn_info {
    u8 cis_status;						// cis连接断开的时候会被设置，详细见app_le_connected.h的APP_CONNECTED_STATUS说明
    u16 cis_hdl;						// cis句柄，cis连接成功的时候会被设置
    u16 acl_hdl;						// acl句柄，cis连接成功的时候会被设置
    u16 Max_PDU_C_To_P;
    u16 Max_PDU_P_To_C;
};

struct app_cig_conn_info {
    u8 used;															// 是否开启cig, app_connected_open
    u8 cig_hdl;															// cig句柄，cis连接成功后会被设置
    u8 cig_status;														// cig功能开关关闭的时候会被设置, 详细见app_le_connected.h的APP_CONNECTED_STATUS说明
    struct app_cis_conn_info cis_conn_info[CIG_MAX_CIS_NUMS];			// cig下的cis成员
};

/**************************************************************************************************
  Global Variables
**************************************************************************************************/
static OS_MUTEX mutex;
static u8 acl_connected_nums = 0;										// acl链路连接数
static u8 cis_connected_nums = 0;										// cis链路连接数
static struct app_cig_conn_info app_cig_conn_info[CIG_MAX_NUMS];		// cig对象

/**************************************************************************************************
  Function Declarations
**************************************************************************************************/

/* --------------------------------------------------------------------------*/
/**
 * @brief 申请互斥量，用于保护临界区代码，与app_connected_mutex_post成对使用
 *
 * @param mutex:已创建的互斥量指针变量
 */
/* ----------------------------------------------------------------------------*/
static inline void app_connected_mutex_pend(OS_MUTEX *mutex, u32 line)
{
    int os_ret;
    os_ret = os_mutex_pend(mutex, 0);
    if (os_ret != OS_NO_ERR) {
        log_error("%s err, os_ret:0x%x", __FUNCTION__, os_ret);
        ASSERT(os_ret != OS_ERR_PEND_ISR, "line:%d err, os_ret:0x%x", line, os_ret);
    }
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 释放互斥量，用于保护临界区代码，与app_connected_mutex_pend成对使用
 *
 * @param mutex:已创建的互斥量指针变量
 */
/* ----------------------------------------------------------------------------*/
static inline void app_connected_mutex_post(OS_MUTEX *mutex, u32 line)
{
    int os_ret;
    os_ret = os_mutex_post(mutex);
    if (os_ret != OS_NO_ERR) {
        log_error("%s err, os_ret:0x%x", __FUNCTION__, os_ret);
        ASSERT(os_ret != OS_ERR_PEND_ISR, "line:%d err, os_ret:0x%x", line, os_ret);
    }
}

/* --------------------------------------------------------------------------*/
/**
 * @brief CIG连接状态事件处理函数
 *
 * @param msg:状态事件附带的返回参数
 *
 * @return
 */
/* ----------------------------------------------------------------------------*/
static int app_connected_conn_status_event_handler(int *msg)
{
    u8 i, j;
    u8 find = 0;
    int ret = 0;
    cig_hdl_t *hdl;
    cis_acl_info_t *acl_info;
    int *event = msg;
    int result = 0;
    log_info("app_connected_conn_status_event_handler=%d", event[0]);

    switch (event[0]) {
    case CIG_EVENT_PERIP_CONNECT:

#if TCFG_USER_TWS_ENABLE
        tws_api_tx_unsniff_req();
#endif
        bt_cmd_prepare(USER_CTRL_ALL_SNIFF_EXIT, 0, NULL);
        //由于是异步操作需要加互斥量保护，避免connected_close的代码与其同时运行,添加的流程请放在互斥量保护区里面
        app_connected_mutex_pend(&mutex, __LINE__);

        hdl = (cig_hdl_t *)&event[1];
        g_printf("CIG_EVENT_PERIP_CONNECT 0x%x", hdl->cig_hdl);

        //记录设备的cig_hdl等信息
        for (i = 0; i < CIG_MAX_NUMS; i++) {
            if (app_cig_conn_info[i].used) {
                if (app_cig_conn_info[i].cig_hdl == hdl->cig_hdl) {
                    app_cig_conn_info[i].cig_hdl = hdl->cig_hdl;
                    for (j = 0; j < CIG_MAX_CIS_NUMS; j++) {
                        if (!app_cig_conn_info[i].cis_conn_info[j].cis_hdl) {
                            app_cig_conn_info[i].cis_conn_info[j].cis_hdl = hdl->cis_hdl;
                            app_cig_conn_info[i].cis_conn_info[j].acl_hdl = hdl->acl_hdl;
                            app_cig_conn_info[i].cis_conn_info[j].cis_status = APP_CONNECTED_STATUS_CONNECT;
                            app_cig_conn_info[i].cis_conn_info[j].Max_PDU_C_To_P = hdl->Max_PDU_C_To_P;
                            app_cig_conn_info[i].cis_conn_info[j].Max_PDU_P_To_C = hdl->Max_PDU_P_To_C;
                            find = 1;

                            log_info("Record acl hangle1:0x%x", app_cig_conn_info[i].cis_conn_info[j].acl_hdl);
                            break;
                        }
                    }
                } else if (app_cig_conn_info[i].cig_hdl == 0xFF) {
                    app_cig_conn_info[i].cig_hdl = hdl->cig_hdl;
                    for (j = 0; j < CIG_MAX_CIS_NUMS; j++) {
                        if (!app_cig_conn_info[i].cis_conn_info[j].cis_hdl) {
                            app_cig_conn_info[i].cis_conn_info[j].cis_hdl = hdl->cis_hdl;
                            app_cig_conn_info[i].cis_conn_info[j].acl_hdl = hdl->acl_hdl;
                            app_cig_conn_info[i].cis_conn_info[j].cis_status = APP_CONNECTED_STATUS_CONNECT;
                            app_cig_conn_info[i].cis_conn_info[j].Max_PDU_C_To_P = hdl->Max_PDU_C_To_P;
                            app_cig_conn_info[i].cis_conn_info[j].Max_PDU_P_To_C = hdl->Max_PDU_P_To_C;
                            find = 0;

                            log_info("Record acl hangle2:0x%x", app_cig_conn_info[i].cis_conn_info[j].acl_hdl);
                            break;
                        }
                    }
                }
            }
        }

        if (!find) {
            //释放互斥量
            app_connected_mutex_post(&mutex, __LINE__);
            break;
        }

        cis_connected_nums++;
        ASSERT(cis_connected_nums <= CIG_MAX_CIS_NUMS && cis_connected_nums >= 0, "cis_connected_nums:%d", cis_connected_nums);

        g_le_audio_hdl.cig_phone_conn_status |= APP_CONNECTED_STATUS_MUSIC;
        if (hdl->Max_PDU_P_To_C) {
            u8 now_call_vol  = vcs_server_get_volume(hdl->acl_hdl) * 15 / 255;
            //set call volume
            app_audio_set_volume(APP_AUDIO_STATE_CALL, now_call_vol, 1);
            g_le_audio_hdl.cig_phone_conn_status &= ~APP_CONNECTED_STATUS_MUSIC;
            g_le_audio_hdl.cig_phone_conn_status |= APP_CONNECTED_STATUS_PHONE_CALL;
            log_info("cis call to stop tone");
            tone_player_stop();
        }
#if TCFG_USER_TWS_ENABLE
        tws_sync_le_audio_conn_info();
#endif
        ret = connected_perip_connect_deal((void *)hdl);
        if (ret < 0) {
            log_debug("connected_perip_connect_deal fail");
        }


        //释放互斥量
        app_connected_mutex_post(&mutex, __LINE__);
        break;

    case CIG_EVENT_PERIP_DISCONNECT:
        //由于是异步操作需要加互斥量保护，避免connected_close的代码与其同时运行,添加的流程请放在互斥量保护区里面
        app_connected_mutex_pend(&mutex, __LINE__);

        hdl = (cig_hdl_t *)&event[1];
        g_printf("CIG_EVENT_PERIP_DISCONNECT 0x%x", hdl->cig_hdl);
        u8  dis_reason = hdl->flush_timeout_C_to_P;     //  复用了flush_timeout_C_to_P参数上传断开错误码
        u16 acl_handle_for_disconnect_cis = 0;
        for (i = 0; i < CIG_MAX_NUMS; i++) {
            if (app_cig_conn_info[i].used && (app_cig_conn_info[i].cig_hdl == hdl->cig_hdl)) {
                if (app_cig_conn_info[i].used) {
                    if (app_cig_conn_info[i].cig_hdl == hdl->cig_hdl) {
                        for (j = 0; j < CIG_MAX_CIS_NUMS; j++) {
                            // if (app_cig_conn_info[i].cis_conn_info[j].cis_hdl == hdl->cis_hdl) {
                            app_cig_conn_info[i].cis_conn_info[j].cis_hdl = 0;
                            acl_handle_for_disconnect_cis = app_cig_conn_info[i].cis_conn_info[j].acl_hdl;
                            /* app_cig_conn_info[i].cis_conn_info[j].acl_hdl = 0; */
                            app_cig_conn_info[i].cis_conn_info[j].cis_status = APP_CONNECTED_STATUS_DISCONNECT;
                            app_cig_conn_info[i].cig_hdl = 0xFF;
                            //break;
                            // }
                        }
                        find = 1;
                    }
                }
            }
        }

        if (!find) {
            //释放互斥量
            app_connected_mutex_post(&mutex, __LINE__);
            break;
        }

        cis_connected_nums--;
        ASSERT(cis_connected_nums <= CIG_MAX_CIS_NUMS && cis_connected_nums >= 0, "cis_connected_nums:%d", cis_connected_nums);

        ret = connected_perip_disconnect_deal((void *)hdl);
        g_le_audio_hdl.cig_phone_conn_status &= ~APP_CONNECTED_STATUS_MUSIC;
        g_le_audio_hdl.cig_phone_conn_status &= ~APP_CONNECTED_STATUS_PHONE_CALL;
#if TCFG_USER_TWS_ENABLE
        tws_sync_le_audio_conn_info();
#endif
        if (ret < 0) {
            log_debug("connected_perip_disconnect_deal fail");
        }

#if TCFG_AUTO_SHUT_DOWN_TIME
        if (!cis_connected_nums) {
            /* sys_auto_shut_down_enable();   // 恢复自动关机 */
        }
#endif

        if (dis_reason == ERROR_CODE_CONNECTION_TIMEOUT) {
            //测试播歌超距的时候，有一种状态是CIG超时了，ACL还没断开，
            //这个时候靠近手机没有重新建立CIG的。---主动断开等手机重连
            log_info("CIG disconnect for timeout\n");
            //le_audio_disconn_le_audio_link();
            ll_hci_disconnect(acl_handle_for_disconnect_cis, 0x13);
        }
        //释放互斥量
        app_connected_mutex_post(&mutex, __LINE__);
        break;

    case CIG_EVENT_ACL_CONNECT:
        log_info("CIG_EVENT_ACL_CONNECT");
#if (THIRD_PARTY_PROTOCOLS_SEL & RCSP_MODE_EN)
        rcsp_bt_ble_adv_enable(0);
#endif
        acl_info = (cis_acl_info_t *)&event[1];
        if (acl_info->conn_type) {
            log_info("connect test box ble");
            return -EPERM;
        }
        g_le_audio_hdl.cig_phone_conn_status |= APP_CONNECTED_STATUS_START;

        //由于是异步操作需要加互斥量保护，避免connected_close的代码与其同时运行,添加的流程请放在互斥量保护区里面
        app_connected_mutex_pend(&mutex, __LINE__);
        ble_op_latency_close(acl_info->acl_hdl);
        log_info("remote device addr:");
        put_buf((u8 *)&acl_info->pri_ch, sizeof(acl_info->pri_ch));
#if TCFG_JL_UNICAST_BOUND_PAIR_EN
        u8 le_com_addr_new[6];
        int ret = syscfg_read(CFG_TWS_CONNECT_AA, le_com_addr_new, 6);
        log_info("read binding common addr\n");
        put_buf(le_com_addr_new, 6);

        if (le_com_addr_new != NULL && memcmp(le_com_addr_new, "\0\0\0\0\0\0", 6) != 0) { //防止空地址触发读零异常
            if (memcmp(&acl_info->pri_ch, le_com_addr_new, 6) != 0) { //地址不匹配
                log_info("Device mismatched, connection denied!!!\n");
                ll_hci_disconnect(acl_info->acl_hdl, 0x13);
                break;
            }
            log_info("Bind information error!!!\n");
            break;
        } else {
            log_info("Never bind information!!!\n");
            break;
        }
#endif
        acl_connected_nums++;
        ASSERT(acl_connected_nums <= CIG_MAX_CIS_NUMS && acl_connected_nums >= 0, "acl_connected_nums:%d", acl_connected_nums);

        //释放互斥量
        app_connected_mutex_post(&mutex, __LINE__);
        //提前不进power down，page scan时间太少可能影响手机连接---lihaihua
#if TCFG_USER_TWS_ENABLE
        if (tws_api_get_role() == TWS_ROLE_SLAVE) {
            puts("role save bread\n");
            break;
        }
#endif
        int connect_device      = bt_get_total_connect_dev();
        log_info("connect_device=%d\n", connect_device);
        if (connect_device == 0) {
            bt_cmd_prepare(USER_CTRL_WRITE_SCAN_DISABLE, 0, NULL);
            bt_cmd_prepare(USER_CTRL_WRITE_CONN_DISABLE, 0, NULL);
            clr_device_in_page_list();
            bt_cmd_prepare(USER_CTRL_PAGE_CANCEL, 0, NULL);
            bt_cmd_prepare(USER_CTRL_WRITE_CONN_ENABLE, 0, NULL);
        }
        break;

    case CIG_EVENT_ACL_DISCONNECT:
        log_info("CIG_EVENT_ACL_DISCONNECT");
#if (THIRD_PARTY_PROTOCOLS_SEL & RCSP_MODE_EN)
        if (tws_api_get_role() != TWS_ROLE_SLAVE) {
            rcsp_bt_ble_adv_enable(1);
        }
#endif
        g_le_audio_hdl.cig_phone_conn_status = 0;
        acl_info = (cis_acl_info_t *)&event[1];

        for (i = 0; i < CIG_MAX_NUMS; i++) {
            for (j = 0; j < CIG_MAX_CIS_NUMS; j++) {
                if (app_cig_conn_info[i].cis_conn_info[j].acl_hdl == acl_info->acl_hdl) {
                    log_info("Clear acl handle...\n");
                    app_cig_conn_info[i].cis_conn_info[j].acl_hdl = 0;
                }
            }
        }

#if ((TCFG_LE_AUDIO_APP_CONFIG & ( LE_AUDIO_JL_UNICAST_SINK_EN)))
        if (get_le_audio_jl_dongle_device_type()) {
            cig_event_to_user(CIG_EVENT_JL_DONGLE_DISCONNECT, (void *)&acl_info->acl_hdl, 2);
        }
#endif
        set_le_audio_jl_dongle_device_type(0);
        if (acl_info->conn_type) {
            log_info("disconnect test box ble");
            return -EPERM;
        }

        //由于是异步操作需要加互斥量保护，避免connected_close的代码与其同时运行,添加的流程请放在互斥量保护区里面
        app_connected_mutex_pend(&mutex, __LINE__);

        if (acl_connected_nums > 0) { // TCFG_JL_UNICAST_BOUND_PAIR_EN拒绝连接，会导致实际还没设备连接就触发断言
            acl_connected_nums--;
            ASSERT(acl_connected_nums <= CIG_MAX_CIS_NUMS && acl_connected_nums >= 0, "acl_connected_nums:%d", acl_connected_nums);
        }

        //释放互斥量
        app_connected_mutex_post(&mutex, __LINE__);
#if TCFG_USER_TWS_ENABLE
        tws_dual_conn_state_handler();
#else
        dual_conn_state_handler();
#endif
        break;
#if ((TCFG_LE_AUDIO_APP_CONFIG & ( LE_AUDIO_JL_UNICAST_SINK_EN)))
    case CIG_EVENT_JL_DONGLE_CONNECT:
        bt_cmd_prepare(USER_CTRL_WRITE_SCAN_DISABLE, 0, NULL);
        bt_cmd_prepare(USER_CTRL_WRITE_CONN_DISABLE, 0, NULL);
        clr_device_in_page_list();
#endif
    case CIG_EVENT_PHONE_CONNECT:
        log_info("CIG_EVENT_PHONE_CONNECT");
        clr_device_in_page_list();
        bt_cmd_prepare(USER_CTRL_PAGE_CANCEL, 0, NULL);
        g_le_audio_hdl.cig_phone_conn_status |= APP_CONNECTED_STATUS_CONNECT;
        int ret = get_sm_peer_address(g_le_audio_hdl.peer_address);
        put_buf(g_le_audio_hdl.peer_address, 6);
        u8 *cur_conn_addr = bt_get_current_remote_addr();
        if (cur_conn_addr && memcmp(cur_conn_addr, g_le_audio_hdl.peer_address, 6)) {
            bt_cmd_prepare_for_addr(cur_conn_addr, USER_CTRL_DISCONNECTION_HCI, 0, NULL);
        }
        updata_last_link_key(g_le_audio_hdl.peer_address, get_remote_dev_info_index());

#if TCFG_AUTO_SHUT_DOWN_TIME
        sys_auto_shut_down_disable();
#endif
#if TCFG_USER_TWS_ENABLE
        tws_sync_le_audio_conn_info();
        tws_dual_conn_state_handler();
#endif
#if TCFG_USER_TWS_ENABLE
        if (tws_api_get_role() == TWS_ROLE_SLAVE) {
            break;
        }
        if (is_cig_other_music_play() || is_cig_other_phone_call_play()) {
            //主机入仓，主机出仓测试，播歌的时候又播提示音有点变调，先屏蔽。
            break;
        }
        tws_play_tone_file(get_tone_files()->bt_connect, 400);
#else
        play_tone_file(get_tone_files()->bt_connect);
#endif

#if (THIRD_PARTY_PROTOCOLS_SEL & RCSP_MODE_EN)
        if (tws_api_get_role() != TWS_ROLE_SLAVE) {
            rcsp_bt_ble_adv_enable(1);
        }
#endif

        break;
    case CIG_EVENT_JL_DONGLE_DISCONNECT:
    case CIG_EVENT_PHONE_DISCONNECT:
        log_info("CIG_EVENT_PHONE_DISCONNECT");
        g_le_audio_hdl.cig_phone_conn_status = 0;
        memset(g_le_audio_hdl.peer_address, 0xff, 6);
#if TCFG_USER_TWS_ENABLE
        tws_sync_le_audio_conn_info();
        tws_dual_conn_state_handler();
#endif
#if TCFG_USER_TWS_ENABLE
        if (tws_api_get_role() == TWS_ROLE_SLAVE) {
            break;
        }
        tws_play_tone_file(get_tone_files()->bt_disconnect, 400);
#else
        play_tone_file(get_tone_files()->bt_disconnect);
#endif
        if (!g_le_audio_hdl.le_audio_profile_ok) {
            app_connected_close_in_other_mode();
        }
        break;

    default:
        result = -ESRCH;
        break;
    }

    return result;
}
APP_MSG_PROB_HANDLER(app_le_connected_msg_entry) = {
    .owner = 0xff,
    .from = MSG_FROM_CIG,
    .handler = app_connected_conn_status_event_handler,
};


/* --------------------------------------------------------------------------*/
/**
 * @brief   le audio状态查询函数， 是否在音乐播放状态，
 *
 * @return 1：在le audio音乐播放  0：不在音乐播放
 */
/* ----------------------------------------------------------------------------*/
u8 is_cig_music_play()
{
    log_debug("is_cig_music_play=%x\n", g_le_audio_hdl.cig_phone_conn_status);
    if (g_le_audio_hdl.cig_phone_conn_status & APP_CONNECTED_STATUS_MUSIC) {
        return 1;
    }
    return 0;
}
/* --------------------------------------------------------------------------*/
/**
 * @brief   le audio状态查询函数， tws另一个耳机是否在音乐播放状态
 *
 * @return  1：在le audio音乐播放  0：不在音乐播放
 */
/* ----------------------------------------------------------------------------*/
u8 is_cig_other_music_play()
{
    log_debug("is_cig_other_music_play=%x\n", g_le_audio_hdl.cig_phone_other_conn_status);
    if (g_le_audio_hdl.cig_phone_other_conn_status & APP_CONNECTED_STATUS_MUSIC) {
        return 1;
    }
    return 0;
}
/* --------------------------------------------------------------------------*/
/**
 * @brief   le audio状态查询函数, 是否在通话状态
 *
 * @return 1：在le audio通话状态  0：不在通话状态
 */
/* ----------------------------------------------------------------------------*/
u8 is_cig_phone_call_play()
{
    log_debug("is_cig_phone_call_play=%x\n", g_le_audio_hdl.cig_phone_conn_status);
    if (g_le_audio_hdl.cig_phone_conn_status & APP_CONNECTED_STATUS_PHONE_CALL) {
        return 1;
    }
    return 0;
}
/* --------------------------------------------------------------------------*/
/**
 * @brief   le audio状态查询函数, tws另一个耳机是否在通话状态
 *
 * @return  1：在le audio通话状态  0：不在通话状态
 */
/* ----------------------------------------------------------------------------*/
u8 is_cig_other_phone_call_play()
{
    log_debug("is_cig_other_phone_call_play=%x\n", g_le_audio_hdl.cig_phone_other_conn_status);
    if (g_le_audio_hdl.cig_phone_other_conn_status & APP_CONNECTED_STATUS_PHONE_CALL) {
        return 1;
    }
    return 0;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 检测当前是否处于挂起状态
 *
 * @return true:处于挂起状态，false:处于非挂起状态
 */
/* ----------------------------------------------------------------------------*/
static bool is_need_resume_connected()
{
    u8 i;
    u8 find = 0;
    for (i = 0; i < CIG_MAX_NUMS; i++) {
        if (app_cig_conn_info[i].cig_status == APP_CONNECTED_STATUS_SUSPEND) {
            find = 1;
            break;
        }
    }
    if (find) {
        return true;
    } else {
        return false;
    }
}

/* --------------------------------------------------------------------------*/
/**
 * @brief CIG从挂起状态恢复
 */
/* ----------------------------------------------------------------------------*/
static void app_connected_resume()
{
    cig_hdl_t temp_connected_hdl;

    if (!g_bt_hdl.init_ok) {
        return;
    }

    if (!is_need_resume_connected()) {
        return;
    }

    app_connected_open();
}

/* --------------------------------------------------------------------------*/
/**
 * @brief CIG进入挂起状态
 */
/* ----------------------------------------------------------------------------*/
static void app_connected_suspend()
{
    u8 i;
    u8 find = 0;
    for (i = 0; i < CIG_MAX_NUMS; i++) {
        if (app_cig_conn_info[i].used) {
            find = 1;
            break;
        }
    }
    if (find) {
        app_connected_close_all(APP_CONNECTED_STATUS_SUSPEND);
    }
}

int tws_check_user_conn_open_quick_type()
{
    /* log_debug("tws_check_user_conn_open_quick_type=%d\n",check_le_audio_tws_conn_role() ); */
    if (is_cig_phone_conn()) {
        return g_le_audio_hdl.le_audio_tws_role;
    }
    return 0xff;
}

/* ----------------------------------------------------------------------------*/
/**
 * @brief 判断cig是否已被连接
 *
 * @return 1:已连接 2:未连接
 */
/* ----------------------------------------------------------------------------*/
u8 is_cig_phone_conn()
{
    log_debug("is_cig_phone_conn=%d,%x\n", bt_get_total_connect_dev(), g_le_audio_hdl.cig_phone_conn_status);

    if (g_le_audio_hdl.cig_phone_conn_status & APP_CONNECTED_STATUS_CONNECT) {
        return 1;
    }
    return 0;
}

/* ----------------------------------------------------------------------------*/
/**
 * @brief 判断tws另一端cig是否已被连接
 *
 * @return 1:已连接 2:未连接
 */
/* ----------------------------------------------------------------------------*/
u8 is_cig_other_phone_conn()
{
    log_debug("is_cig_other_phone_conn=%d,%x\n", bt_get_total_connect_dev(), g_le_audio_hdl.cig_phone_other_conn_status);
    if (g_le_audio_hdl.cig_phone_other_conn_status & APP_CONNECTED_STATUS_CONNECT) {
        return 1;
    }
    return 0;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 开启CIG
 */
/* ----------------------------------------------------------------------------*/
void app_connected_open(void)
{
    u8 i;
    u8 role = 0;
    u8 cig_available_num = 0;
    cig_hdl_t temp_connected_hdl;
    cig_parameter_t *params;
    int ret;
    u64 pair_addr;
    struct app_mode *mode;

    if (!g_bt_hdl.init_ok) {
        return;
    }

    for (i = 0; i < CIG_MAX_NUMS; i++) {
        if (!app_cig_conn_info[i].used) {
            cig_available_num++;
        }
    }

    if (!cig_available_num) {
        return;
    }

    log_info("connected_open");

    //初始化cig接收端参数
    params = set_cig_params();
    //打开cig，打开成功后会在函数app_connected_conn_status_event_handler做后续处理
    temp_connected_hdl.cig_hdl = connected_perip_open(params);

#if 0
    extern void wireless_trans_auto_test3_init(void);
    extern void wireless_trans_auto_test4_init(void);
    //不定时切换模式
    wireless_trans_auto_test3_init();
    //不定时暂停播放
    wireless_trans_auto_test4_init();
#endif

    if (temp_connected_hdl.cig_hdl >= 0) {
        for (i = 0; i < CIG_MAX_NUMS; i++) {
            if (!app_cig_conn_info[i].used) {
                app_cig_conn_info[i].cig_hdl = temp_connected_hdl.cig_hdl;
                app_cig_conn_info[i].cig_status = APP_CONNECTED_STATUS_START;
                app_cig_conn_info[i].used = 1;
                break;
            }
        }
    }
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 关闭对应cig_hdl的CIG连接
 *
 * @param cig_hdl:需要关闭的CIG连接对应的hdl
 * @param status:关闭后CIG进入的suspend还是stop状态
 */
/* ----------------------------------------------------------------------------*/
void app_connected_close(u8 cig_hdl, u8 status)
{
    u8 i;
    u8 find = 0;
    log_info("connected_close");
    //由于是异步操作需要加互斥量保护，避免和开启的流程同时运行,添加的流程请放在互斥量保护区里面
    app_connected_mutex_pend(&mutex, __LINE__);

    for (i = 0; i < CIG_MAX_NUMS; i++) {
        if (app_cig_conn_info[i].used && (app_cig_conn_info[i].cig_hdl == cig_hdl)) {
            memset(&app_cig_conn_info[i], 0, sizeof(struct app_cig_conn_info));
            app_cig_conn_info[i].cig_status = status;
            find = 1;
            break;
        }
    }

    if (find) {
        connected_close(cig_hdl);
        acl_connected_nums -= CIG_MAX_CIS_NUMS;
        ASSERT(acl_connected_nums <= CIG_MAX_CIS_NUMS && acl_connected_nums >= 0, "acl_connected_nums:%d", acl_connected_nums);
        cis_connected_nums -= CIG_MAX_CIS_NUMS;
        ASSERT(cis_connected_nums <= CIG_MAX_CIS_NUMS && cis_connected_nums >= 0, "cis_connected_nums:%d", cis_connected_nums);
    }


    //释放互斥量
    app_connected_mutex_post(&mutex, __LINE__);
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 关闭所有cig_hdl的CIG连接
 *
 * @param status:关闭后CIG进入的suspend还是stop状态
 */
/* ----------------------------------------------------------------------------*/
void app_connected_close_all(u8 status)
{
    log_info("connected_close");
    //由于是异步操作需要加互斥量保护，避免和开启的流程同时运行,添加的流程请放在互斥量保护区里面
    app_connected_mutex_pend(&mutex, __LINE__);

    for (u8 i = 0; i < CIG_MAX_NUMS; i++) {
        if (app_cig_conn_info[i].used && app_cig_conn_info[i].cig_hdl) {
            connected_close(app_cig_conn_info[i].cig_hdl);
            memset(&app_cig_conn_info[i], 0, sizeof(struct app_cig_conn_info));
            app_cig_conn_info[i].cig_status = status;
        }
    }

    acl_connected_nums = 0;
    cis_connected_nums = 0;

    //释放互斥量
    app_connected_mutex_post(&mutex, __LINE__);
}

/* --------------------------------------------------------------------------*/
/**
 * @brief CIG开关切换
 *
 * @return 0：操作成功
 */
/* ----------------------------------------------------------------------------*/
int app_connected_switch(void)
{
    u8 i;
    u8 find = 0;
    cig_hdl_t temp_connected_hdl;
    struct app_mode *mode;

    if (!g_bt_hdl.init_ok) {
        return -EPERM;
    }

    for (i = 0; i < CIG_MAX_NUMS; i++) {
        if (app_cig_conn_info[i].used) {
            find = 1;
            break;
        }
    }

    if (find) {
        app_connected_close_all(APP_CONNECTED_STATUS_STOP);
    } else {
        app_connected_open();
    }
    return 0;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief CIG资源初始化
 */
/* ----------------------------------------------------------------------------*/
void app_connected_init(void)
{
    int os_ret = os_mutex_create(&mutex);
    if (os_ret != OS_NO_ERR) {
        log_error("%s %d err, os_ret:0x%x", __FUNCTION__, __LINE__, os_ret);
        ASSERT(0);
    }
}

/* --------------------------------------------------------------------------*/
/**
 * @brief CIG资源卸载
 */
/* ----------------------------------------------------------------------------*/
void app_connected_uninit(void)
{
    int os_ret = os_mutex_del(&mutex, OS_DEL_NO_PEND);
    if (os_ret != OS_NO_ERR) {
        log_error("%s %d err, os_ret:0x%x", __FUNCTION__, __LINE__, os_ret);
    }
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 非蓝牙后台情况下，在其他音源模式开启CIG，前提要先开蓝牙协议栈
 */
/* ----------------------------------------------------------------------------*/
void app_connected_open_in_other_mode()
{
    app_connected_init();
    app_connected_open();
}

/* --------------------------------------------------------------------------*/
/**
 * @brief
 * @brief 非蓝牙后台情况下，在其他音源模式关闭CIG
 */
/* ----------------------------------------------------------------------------*/
void app_connected_close_in_other_mode()
{
    app_connected_close_all(APP_CONNECTED_STATUS_STOP);
    app_connected_uninit();
}

void le_audio_adv_api_enable(u8 en)
{
    if (!get_bt_le_audio_config()) {
        return;
    }
    log_debug("le_audio_adv_api_enable=%d\n", en);
#if TCFG_USER_TWS_ENABLE
    if (tws_api_get_role() == TWS_ROLE_SLAVE) {
        bt_le_audio_adv_enable(en);
    } else
#endif
    {
        bt_le_audio_adv_enable(en);
    }

}

/*提供使用参考用户设置le audio的广播包配置可连接不可发现状态*/
void le_audio_adv_close_discover_mode()
{
    le_audio_adv_api_enable(0);
    le_audio_set_discover_mode(0);
    le_audio_adv_api_enable(1);
}
void le_audio_adv_open_discover_mode()
{
    le_audio_adv_api_enable(0);
    le_audio_set_discover_mode(1);
    le_audio_adv_api_enable(1);
}

/*le audio profile lib check tws ear side to do something different*/
#define LE_AUDIO_LEFT_EAR                                       1
#define LE_AUDIO_RIGHT_EAR                                      2
#define LE_AUDIO_BOTH_EAR                                       3
/*
 * 实现le_audio_profile.a里面的weak函数，给库获取当前耳机的左右情况
 * */
u8 le_audio_get_tws_ear_side()
{
#if TCFG_USER_TWS_ENABLE
#if (TCFG_AUDIO_DAC_CONNECT_MODE==DAC_OUTPUT_LR)
    return LE_AUDIO_BOTH_EAR;
#else
    if (tws_api_get_local_channel()  == 'R') {
        return LE_AUDIO_RIGHT_EAR;
    } else {
        return LE_AUDIO_LEFT_EAR;
    }
#endif
#else
    return LE_AUDIO_BOTH_EAR;
#endif
}
/*
 * 实现le_audio_profile.a里面的weak函数，给库获取当前耳机的主从情况
 * le audio profile lib check tws role to do something different
*/
static void tws_sync_le_audio_sirk();
static void tws_sync_le_audio_adv_mac_to_slave();
static void tws_sync_le_audio_adv_mac_to_master();
/*le audio profile lib check tws role to do something different*/
u8 le_audio_get_tws_role()
{
#if TCFG_USER_TWS_ENABLE
    if (tws_api_get_role() == TWS_ROLE_SLAVE) {
        g_le_audio_hdl.le_audio_tws_role = TWS_ROLE_SLAVE;
        return 0;
    }
    g_le_audio_hdl.le_audio_tws_role = TWS_ROLE_MASTER;
#endif
    return 1;
}
/*这个是16个byte长度的值，可以是随机值再保存下来使用，
 * 主从一样的时候说明主从是一个设备组
 *tws配对之后，主机要发送这个值给从机
 * */
static u8 default_sirk[16] = {0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99};
u8 le_audio_adv_local_mac[6] = {0xff};
u8 le_audio_adv_slave_mac[6] = {0xff};
/*
 * 实现le_audio_profile.a里面的weak函数，给库获取当前耳机的sirk配置值
 * */
u8 le_audio_get_user_sirk(u8 *sirk)
{
    memcpy(sirk, default_sirk, 16);
    return 0;
}
void le_audio_adv_open_success(void *le_audio_ble_hdl, u8 *addr)
{
    log_info("le_audio_adv_open_success\n");
    memcpy(le_audio_adv_local_mac, addr, 6);
}
#define VENDOR_PRIV_DEVICE_TYPE_REQ     0x01
#define VENDOR_PRIV_DEVICE_TYPE_RSP     0x02
#define VENDOR_PRIV_LC3_INFO            0x03
#define VENDOR_PRIV_OPEN_MIC            0x04
#define VENDOR_PRIV_CLOSE_MIC           0x05
//for earphone control
#define VENDOR_PRIV_ACL_OPID_CONTORL    0x06
#define VENDOR_PRIV_ACL_MUSIC_VOLUME    0x07
#define VENDOR_PRIV_ACL_MIC_VOLUME      0x08

enum {
    UNICAST_INDXT = 1,
    UNICAST_FOR_JL_HEADSET_INDXT,
    UNICAST_FOR_JL_TWS_INDXT,

};

u8 pri_data[30];
void le_audio_send_priv_cmd(u16 conn_handle, u8 cmd, u8 *data, u8 len)
{
    int send_len = 1;
    pri_data[0] = cmd;

    memcpy(&pri_data[1], data, len);
    send_len += len;
    log_info_hexdump(pri_data, send_len);
    /* if (cmd == VENDOR_PRIV_LC3_INFO) { */
    /*     send_len += get_unicast_lc3_info(&pri_data[1]); */
    /* } */

    ll_hci_vendor_send_priv_cmd(conn_handle, pri_data, send_len);
}

static u16 get_conn_handle(void)
{
    int i, j;
    u16 con_handle = 0;
    //TODO 暂时没有添加获取实时有音频流的链路进行控制,选择刚连上的链路控制媒体行为

    for (i = 0; i < CIG_MAX_NUMS; i++) {
        if (app_cig_conn_info[i].used) {
            for (j = 0; j < CIG_MAX_CIS_NUMS; j++) {
                if (app_cig_conn_info[i].cis_conn_info[j].acl_hdl) {
                    con_handle = app_cig_conn_info[i].cis_conn_info[j].acl_hdl;
                }
            }
        }
    }

    return con_handle;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief   le audio媒体控制接口
 *
 * @return
 */
/* ----------------------------------------------------------------------------*/
void le_audio_media_control_cmd(u8 *data, u8 len)
{
    u16 con_handle = get_conn_handle();

    if (con_handle) {
        log_info("Send media control... handle:0x%x\n", con_handle);
#if TCFG_BT_VOL_SYNC_ENABLE
        switch (data[0]) {
        case CIG_EVENT_OPID_VOLUME_UP:
        case CIG_EVENT_OPID_VOLUME_DOWN:
            log_info("sync vol to master");
            le_audio_send_priv_cmd(con_handle, VENDOR_PRIV_ACL_MUSIC_VOLUME, &(app_var.opid_play_vol_sync), sizeof(app_var.opid_play_vol_sync));
            break;
        default:
            le_audio_send_priv_cmd(con_handle, VENDOR_PRIV_ACL_OPID_CONTORL, data, len);
            break;
        }
#else
        le_audio_send_priv_cmd(con_handle, VENDOR_PRIV_ACL_OPID_CONTORL, data, len);
#endif
    } else {
        log_info("No handle...,send fail");
    }
}

static u16 ble_user_priv_cmd_handle(u16 handle, u8 *cmd, u8 *rsp)
{
    u8 cmd_opcode = cmd[0];
    u16 rsp_len = 0;
    log_info("ble_user_priv_cmd_handle:%x\n", cmd[0]);
    switch (cmd_opcode) {
    case VENDOR_PRIV_DEVICE_TYPE_REQ:
        rsp[0] = VENDOR_PRIV_DEVICE_TYPE_RSP;

#if (TCFG_USER_TWS_ENABLE)
        rsp[1] = UNICAST_FOR_JL_TWS_INDXT;
#else
        rsp[1] = UNICAST_FOR_JL_HEADSET_INDXT;
#endif
#if (TCFG_USER_TWS_ENABLE)
        log_info("get tws state:%d, %d\n", tws_api_get_tws_state(), tws_api_get_role());
        //主从均可获取adv_mac
        if (tws_api_get_tws_state() & TWS_STA_SIBLING_CONNECTED) {
            /* if (tws_api_get_tws_state() & TWS_STA_SIBLING_CONNECTED && tws_api_get_role() == TWS_ROLE_MASTER) { */
            memcpy(rsp + 2, le_audio_adv_slave_mac, 6);
            put_buf(le_audio_adv_slave_mac, 6);
        } else
#endif
        {
            memset(rsp + 2, 0xff, 6);
        }
        rsp_len = 2 + 6;
        put_buf(&rsp[2], 6);
        set_le_audio_jl_dongle_device_type(1);
        cig_event_to_user(CIG_EVENT_JL_DONGLE_CONNECT, (void *)&handle, 2);

        break;
    case VENDOR_PRIV_LC3_INFO:
        set_unicast_lc3_info(&cmd[1]);
        break;
    case VENDOR_PRIV_OPEN_MIC:
        app_send_message(APP_MSG_LE_AUDIO_CALL_OPEN, handle);
        break;
    case VENDOR_PRIV_CLOSE_MIC:
        app_send_message(APP_MSG_LE_AUDIO_CALL_CLOSE, handle);
        break;
    case VENDOR_PRIV_ACL_MUSIC_VOLUME:
        log_info("Get master music vol:%d", cmd[1]);
        set_music_device_volume(cmd[1]);
        break;
    case VENDOR_PRIV_ACL_MIC_VOLUME:
        log_info("Get master mic vol:%d", cmd[1]);
        set_music_device_volume(cmd[1]);
        break;
    }
    return rsp_len;
}
/*
 * le audio功能总初始化函数
 * */
void le_audio_profile_init()
{
    if (get_bt_le_audio_config() && g_le_audio_hdl.le_audio_profile_ok == 0) {
#if (THIRD_PARTY_PROTOCOLS_SEL & RCSP_MODE_EN)
        le_audio_user_server_profile_init(rcsp_profile_data);
#endif
        g_le_audio_hdl.le_audio_profile_ok = 1;
        char le_audio_name[LOCAL_NAME_LEN] = "le_audio_";     //le_audio蓝牙名
        u8 tem_len = 0;//strlen(le_audio_name);
        memcpy(&le_audio_name[tem_len], (u8 *)bt_get_local_name(), LOCAL_NAME_LEN - tem_len);

        le_audio_name_reset((u8 *)le_audio_name, strlen(le_audio_name));
        le_audio_init(1);
        app_connected_init();
#if (TCFG_LE_AUDIO_APP_CONFIG & LE_AUDIO_JL_UNICAST_SINK_EN)
        ble_vendor_priv_cmd_handle_register(ble_user_priv_cmd_handle);
#endif
        app_connected_open();
        le_audio_ops_register(0);

        make_rand_num(default_sirk);
    }

}
/*
 * le audio功能总退出函数
 * */
void le_audio_profile_exit()
{
    g_le_audio_hdl.le_audio_profile_ok = 0;
    if (!is_cig_phone_conn()) { // 有连接就卸载否则会异常
        app_connected_close_in_other_mode();
    }
}

/*
 * 一些公共消息按需处理
 * */
static int le_audio_app_msg_handler(int *msg)
{
    u8 comm_addr[6];

    switch (msg[0]) {
    case APP_MSG_STATUS_INIT_OK:
        log_info("APP_MSG_STATUS_INIT_OK");
#if (TCFG_USER_TWS_ENABLE==0)
        le_audio_profile_init();
        le_audio_adv_open_discover_mode();
#endif
        break;
    case APP_MSG_ENTER_MODE://1
        log_info("APP_MSG_ENTER_MODE");
        break;
    case APP_MSG_BT_GET_CONNECT_ADDR://1
        log_info("APP_MSG_BT_GET_CONNECT_ADDR");
        break;
    case APP_MSG_BT_OPEN_PAGE_SCAN://1
        log_info("APP_MSG_BT_OPEN_PAGE_SCAN");
        break;
    case APP_MSG_BT_CLOSE_PAGE_SCAN://1
        log_info("APP_MSG_BT_CLOSE_PAGE_SCAN");
        break;
    case APP_MSG_BT_ENTER_SNIFF:
        break;
    case APP_MSG_BT_EXIT_SNIFF:
        break;
    case APP_MSG_TWS_PAIRED://1
        log_info("APP_MSG_TWS_PAIRED");
        le_audio_profile_init();
        break;
    case APP_MSG_TWS_UNPAIRED://1
        le_audio_profile_init();
        le_audio_adv_api_enable(1);
        log_info("APP_MSG_TWS_UNPAIRED");
        break;
    case APP_MSG_TWS_POWERON_CONN_TIMEOUT:
        puts("APP_MSG_TWS_POWERON_CONN_TIMEOUT\n");
        le_audio_adv_api_enable(1);
        break;
    case APP_MSG_TWS_PAIR_SUSS://1
        log_info("APP_MSG_TWS_PAIR_SUSS");
    case APP_MSG_TWS_CONNECTED://1
        log_info("APP_MSG_TWS_CONNECTED--in app le connect");
#if TCFG_USER_TWS_ENABLE
        tws_sync_le_audio_conn_info();
        //配对之后主从广播的情况不一样
        if (tws_api_get_role() == TWS_ROLE_MASTER) {
            tws_sync_le_audio_sirk();
            le_audio_adv_api_enable(0);
            le_audio_adv_api_enable(1);
            tws_sync_le_audio_adv_mac_to_slave();//主机同步地址给tws从机
            //TWS连上的时候，从机收到SIRK再刷新广播信息
        } else {
            //if slave cis is playing,then send vol to master
            if (is_cig_music_play() || is_cig_phone_call_play()) {
                void bt_tws_slave_sync_volume_to_master();
                bt_tws_slave_sync_volume_to_master();
            }

        }
#endif

        break;
    case APP_MSG_POWER_OFF://1
        log_info("APP_MSG_POWER_OFF");
        break;
    case APP_MSG_LE_AUDIO_MODE:
        log_debug("APP_MSG_LE_AUDIO_MODE=%d\n", msg[1]);
#if TCFG_USER_TWS_ENABLE
        if (tws_api_get_role() == TWS_ROLE_SLAVE) {
            break;
        }
#endif
        le_audio_surport_config(msg[1]);
        break;
#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_UNICAST_SINK_EN))
    case APP_MSG_LE_AUDIO_CALL_OPEN:
        connected_perip_connect_recoder(1, msg[1]);
        break;
    case APP_MSG_LE_AUDIO_CALL_CLOSE:
        connected_perip_connect_recoder(0, msg[1]);
        break;
#endif
    default:
        break;
    }
    return 0;
}
/*
 * 一些蓝牙线程消息按需处理
 * */
static int le_audio_conn_btstack_event_handler(int *_event)
{
    struct bt_event *event = (struct bt_event *)_event;
    log_info("le_audio_conn_btstack_event_handler:%d\n", event->event);
    switch (event->event) {
    case BT_STATUS_FIRST_CONNECTED:
        if (get_bt_le_audio_config()) {
            clr_device_in_page_list();
            bt_cmd_prepare(USER_CTRL_PAGE_CANCEL, 0, NULL);
#if TCFG_USER_TWS_ENABLE
            if (tws_api_get_role() == TWS_ROLE_SLAVE) {
                break;
            }
#endif
            le_audio_adv_api_enable(0);
        }
        break;
    case BT_STATUS_FIRST_DISCONNECT:
        if (get_bt_le_audio_config()) {
            le_audio_adv_api_enable(1);
        }
        break;
    case HCI_EVENT_CONNECTION_COMPLETE:
        break;
    case HCI_EVENT_DISCONNECTION_COMPLETE :
        log_info("app le connect HCI_EVENT_DISCONNECTION_COMPLETE: %0x\n", event->value);
        if (event->value ==  ERROR_CODE_CONNECTION_TIMEOUT) {
            //超时断开设置上请求回连标记
            le_audio_adv_api_enable(0);
            le_audio_set_requesting_a_connection_flag(1);
            le_audio_adv_api_enable(1);
        }
        break;
    }
    return 0;

}
#if TCFG_USER_TWS_ENABLE
/*一些tws线程消息按需处理*/
int le_audio_tws_connction_status_event_handler(int *msg)
{
    struct tws_event *evt = (struct tws_event *)msg;
    int role = evt->args[0];
    int phone_link_connection = evt->args[1];
    int reason = evt->args[2];

    log_info("le_audo tws-user: role= %d, phone_link_connection %d, reason=%d,event= %d\n",
             role, phone_link_connection, reason, evt->event);

    switch (evt->event) {
    case TWS_EVENT_CONNECTED:
        break;
    case TWS_EVENT_CONNECTION_DETACH:
        g_le_audio_hdl.cig_phone_other_conn_status = 0;
        memset(le_audio_adv_slave_mac, 0xff, 6);
        break;
    }
    return 0;
}
APP_MSG_HANDLER(le_audio_tws_msg_handler) = {
    .owner      = APP_MODE_BT,
    .from       = MSG_FROM_TWS,
    .handler    = le_audio_tws_connction_status_event_handler,
};
#endif
APP_MSG_HANDLER(le_audio_app_msg_entry) = {
    .owner      = 0xff,
    .from       = MSG_FROM_APP,
    .handler    = le_audio_app_msg_handler,
};
APP_MSG_HANDLER(conn_stack_msg_entry) = {
    .owner      = 0xff,
    .from       = MSG_FROM_BT_STACK,
    .handler    = le_audio_conn_btstack_event_handler,
};


//le audio vol control , weak function redo
/*conn_handle(2),volume_setting(1),change_step_size(1),mute(1)*/
#define LEA_VCS_SERVER_VOLUME_STATE   0x00
/*conn_handle(2),volume_flags(1)*/
#define LEA_VCS_SERVER_VOLUME_FLAGS   0x01
//Microphone Control Service
/*conn_handle(2),mute_state(1)*/
#define LEA_MICS_SERVER_MUTE_STATE    0x02

/**
 * 实现le_audio_profile.a里面的weak函数，获取到了手机配置的音量消息，音量值更新
 */
void le_audio_profile_event_to_user(u16 type, u8 *data, u16 len)
{
    log_info("le_audio_profile_event in app");
    put_buf(data, len);
    if (type == LEA_VCS_SERVER_VOLUME_STATE) {
        u8 phone_vol = data[2]; //value is (0 -> 255)
        //change to 0-127
        set_music_device_volume(phone_vol / 2);
    }
}
enum {
    LE_AUDIO_CONFIG_EN = 1,
    LE_AUDIO_CONN_STATUES,
    LE_AUDIO_CONFIG_SIRK,
    LE_AUDIO_GET_SLAVE_VOL,  //when tws connect,if slave playing cis then it will send vol to master
    LE_AUDIO_ADV_MAC_INFO,
};
/**
 * 有些手机连接过同一个地址之后会记忆耳机的服务，所以动态配置le audio的功能的时候，
 * 仅经典蓝牙功能和经典蓝牙+le audio功能的地址要不一样
 * */
void le_audio_surport_config_change_addr(u8 ramdom)
{
    u8 mac_buf[6];
    u8 comm_mac_buf[12];
    int ret = 0;
    ret = syscfg_read(CFG_TWS_COMMON_ADDR, mac_buf, 6);
    if (ret == 6) {
        memcpy(&comm_mac_buf[6], mac_buf, 6);
        for (int i = 0; i < 6; i++) {
            mac_buf[i] = mac_buf[5 - i] + ramdom;
        }
        memcpy(comm_mac_buf, mac_buf, 6);
        log_debug("le_audio_surport_config_change_addr comm=%x", ramdom);
        put_buf(comm_mac_buf, 12);
        syscfg_write(CFG_TWS_COMMON_ADDR, comm_mac_buf, 12);

    } else {
        ret = syscfg_read(CFG_BT_MAC_ADDR, mac_buf, 6);
        if (ret == 6) {
            for (int i = 0; i < 6; i++) {
                mac_buf[i] += mac_buf[5 - i] + ramdom;
            }
            log_debug("le_audio_surport_config_change_addr bt_addr");
            put_buf(mac_buf, 6);
            syscfg_write(CFG_BT_MAC_ADDR, mac_buf, 6);

        }

    }
}
/**
 *实现le_audio_profile.a里面的weak函数，这个回调表示le audio广播刚连上，加密之前，协议还没开始连
 * */
void le_audio_adv_conn_success(u8 adv_id)
{
    log_info("le_audio_adv_conn_success,adv id:%d\n", adv_id);
    g_le_audio_hdl.le_audio_adv_connected = 0xAA;
}
/**
 *实现le_audio_profile.a里面的weak函数，这个回调表示le audio的广播连接断开回调
 * */
void le_audio_adv_disconn_success(u8 adv_id)
{
    log_info("le_audio_adv_disconn_success,adv id:%d\n", adv_id);
    le_audio_adv_api_enable(0);
    g_le_audio_hdl.le_audio_adv_connected = 0;
}
/*定义接口获取le audio广播的连接状态*/
u8  le_audio_get_adv_conn_success()
{
    return !(g_le_audio_hdl.le_audio_adv_connected == 0xAA);
}
/*
 * 实现le_audio_profile.a里面的weak函数,传递出来更多参数可用于功能扩展
 * 这个回调表示le audio广播刚连上，加密之前，协议还没开始连
 * */
void le_audio_profile_connected_for_cig_peripheral(u8 status, u16 acl_handle, u8 *addr)
{
}
#if TCFG_USER_TWS_ENABLE
/*
 *有些le audio信息TWS之间的同步流程
 * */
static void tws_sync_le_audio_config_func(u8 *data, int len)
{
    log_debug("tws_sync_le_audio_config_func: %d, %d\n", data[0], data[1]);
    switch (data[0]) {
    case LE_AUDIO_CONFIG_EN:
        if (data[1]) {
            g_le_audio_hdl.le_audio_en_config = 1;
        } else {
            g_le_audio_hdl.le_audio_en_config = 0;
        }
        log_debug("set_le_audio_surport_config cpu_reset=%d\n", g_le_audio_hdl.le_audio_en_config);
        syscfg_write(CFG_LE_AUDIO_EN, &(g_le_audio_hdl.le_audio_en_config), 1);
        le_audio_surport_config_change_addr(data[2]);
        bt_cmd_prepare(USER_CTRL_DEL_ALL_REMOTE_INFO, 0, NULL);
        bt_cmd_prepare(USER_CTRL_POWER_OFF, 0, NULL);
        os_time_dly(10);
        cpu_reset();//开关le_audio之后重新reset重启,初始化相关服务
        break;
    case LE_AUDIO_CONN_STATUES:
        g_le_audio_hdl.cig_phone_other_conn_status = data[1];
#if TCFG_AUTO_SHUT_DOWN_TIME
        if (g_le_audio_hdl.cig_phone_other_conn_status & APP_CONNECTED_STATUS_CONNECT) {
            sys_auto_shut_down_disable();
        }
#endif
        clr_device_in_page_list();
        bt_cmd_prepare(USER_CTRL_PAGE_CANCEL, 0, NULL);
        if (tws_api_get_role() == TWS_ROLE_MASTER) {
            tws_dual_conn_state_handler();
        }
        break;
    case LE_AUDIO_CONFIG_SIRK:
        //单向主更新给从,此处为从机处理
        memcpy(default_sirk, &data[1], 16);
        log_info("slave get SIRK");
        put_buf(default_sirk, 16);
        le_audio_adv_api_enable(0);
        le_audio_adv_api_enable(1);
        tws_sync_le_audio_adv_mac_to_master();
        break;
    case LE_AUDIO_GET_SLAVE_VOL:
        if (is_cig_music_play() || is_cig_phone_call_play()) {
            break ;
        }
        log_info("master get slave vol while slave playing cis");
        app_audio_set_volume(APP_AUDIO_STATE_MUSIC, data[1], 1);
        app_audio_set_volume(APP_AUDIO_STATE_CALL, data[2], 1);
        break;
    case LE_AUDIO_ADV_MAC_INFO:
        puts("LE_AUDIO_ADV_MAC_INFO\n");
        memcpy(le_audio_adv_slave_mac, &data[1], 6);
        put_buf(le_audio_adv_slave_mac, 6);
        break;
    }
    free(data);

}
static void tws_sync_le_audio_info_func(void *_data, u16 len, bool rx)
{
    log_debug("tws_sync_le_audio_info_func:%d", rx);
    if (rx) {
        u8 *data = malloc(len);
        memcpy(data, _data, len);
        int msg[4] = { (int)tws_sync_le_audio_config_func, 2, (int)data, len};
        os_taskq_post_type("app_core", Q_CALLBACK, 4, msg);
    } else {
        u8 *data = _data;
        if (data[0] == LE_AUDIO_CONFIG_EN) {
            log_debug("tx set_le_audio_surport_config cpu_reset=%d\n", g_le_audio_hdl.le_audio_en_config);
            cpu_reset();//开关le_audio之后重新reset重启,初始化相关服务

        }

    }
}
REGISTER_TWS_FUNC_STUB(app_vol_sync_stub) = {
    .func_id = 0x23782C5B,
    .func    = tws_sync_le_audio_info_func,
};

/*
 * le audio的使能配置主从之间信息同步
 * */
void tws_sync_le_audio_en_info(u8 random)
{
    u8 data[3];
    data[0] = LE_AUDIO_CONFIG_EN;
    data[1] = g_le_audio_hdl.le_audio_en_config;
    data[2] = random;
    tws_api_send_data_to_slave(data, 3, 0x23782C5B);

}
/*
 * le audio的连接状态主从之间信息同步
 * */
void tws_sync_le_audio_conn_info()
{
    u8 data[2];
    data[0] = LE_AUDIO_CONN_STATUES;
    data[1] = g_le_audio_hdl.cig_phone_conn_status;
    tws_api_send_data_to_sibling(data, 2, 0x23782C5B);
}
/*
 * le audio的sirk主从之间信息同步
 * */
static void tws_sync_le_audio_sirk()
{
    u8 data[17];
    data[0] = LE_AUDIO_CONFIG_SIRK;
    memcpy(&data[1], default_sirk, 16);
    log_info("master get SIRK");
    put_buf(default_sirk, 16);
    tws_api_send_data_to_slave(data, 17, 0x23782C5B);

}
void bt_tws_slave_sync_volume_to_master()
{
    u8 data[4];
    //原从机正在播le audio，另一个出仓后是主机，
    //这个时候从不接收主机的音量，并且要同步自己的音量给主
    data[0] = LE_AUDIO_GET_SLAVE_VOL;
    data[1] = app_audio_get_volume(APP_AUDIO_STATE_MUSIC);
    data[2] = app_audio_get_volume(APP_AUDIO_STATE_CALL);

    tws_api_send_data_to_sibling(data, 3, 0x23782C5B);
}
static void tws_sync_le_audio_adv_mac_to_slave()
{
    u8 data[7];
    u16 con_handle = get_conn_handle();
    data[0] = LE_AUDIO_ADV_MAC_INFO;
    memcpy(&data[1], le_audio_adv_local_mac, 6);
    log_info("to slave le_audio_adv_mac");
    put_buf(le_audio_adv_local_mac, 6);
    tws_api_send_data_to_slave(data, 7, 0x23782C5B);

    //通知conn master to read addr
    if (con_handle) {
        log_info("req master update addr\n");
        le_audio_send_priv_cmd(con_handle, VENDOR_PRIV_DEVICE_TYPE_REQ, NULL, 0);
    }

}
static void tws_sync_le_audio_adv_mac_to_master()
{
    u8 data[7];
    u16 con_handle = get_conn_handle();
    data[0] = LE_AUDIO_ADV_MAC_INFO;
    memcpy(&data[1], le_audio_adv_local_mac, 6);
    log_info("to master le_audio_adv_mac");
    put_buf(le_audio_adv_local_mac, 6);
    tws_api_send_data_to_sibling(data, 7, 0x23782C5B);

    if (con_handle) {
        log_info("req master update addr\n");
        le_audio_send_priv_cmd(con_handle, VENDOR_PRIV_DEVICE_TYPE_REQ, NULL, 0);
    }
}
#endif
/* ----------------------------------------------------------------------------*/
/**
 * @brief  一开始设计是用于app动态配置le audio的功能开关，配置会记录在VM
 *
 * @param   le_auido_en  1-enable   0-disable
 * @return  void
 */
/* ----------------------------------------------------------------------------*/
void le_audio_surport_config(u8 le_auido_en)
{
    u8 addr[6] = {0};
#if TCFG_BT_DUAL_CONN_ENABLE
    set_dual_conn_config(addr, !le_auido_en);//le_audio en close dual_conn
#endif
#if TCFG_BT_DUAL_CONN_ENABLE
    if (le_auido_en) {
        g_le_audio_hdl.le_audio_en_config = 1;
    } else {
        g_le_audio_hdl.le_audio_en_config = 0;
    }
    syscfg_write(CFG_LE_AUDIO_EN, &(g_le_audio_hdl.le_audio_en_config), 1);
    bt_cmd_prepare(USER_CTRL_DEL_ALL_REMOTE_INFO, 0, NULL);
    bt_cmd_prepare(USER_CTRL_POWER_OFF, 0, NULL);
    log_debug("set_le_audio_surport_config=%d\n", g_le_audio_hdl.le_audio_en_config);
    u8 random = (u8)rand32();
    le_audio_surport_config_change_addr(random);
#if TCFG_USER_TWS_ENABLE
    tws_sync_le_audio_en_info(random);
#endif
#endif

}

void set_le_audio_surport_config(u8 le_auido_en)
{
    app_send_message(APP_MSG_LE_AUDIO_MODE, le_auido_en);
}

/* ----------------------------------------------------------------------------*/
/**
 * @brief 是否支持le_audio功能
 *
 * @return 1:支持 0:不支持
 */
/* ----------------------------------------------------------------------------*/
u8 get_bt_le_audio_config()
{
    return g_le_audio_hdl.le_audio_en_config;
}

/* ----------------------------------------------------------------------------*/
/**
 * @brief 从vm读取是否支持le_audio功能
 *
 * @return 1:支持 0:不支持
 */
/* ----------------------------------------------------------------------------*/
u8 get_bt_le_audio_config_for_vm()
{
#if 1//default support le_audio
    g_le_audio_hdl.le_audio_en_config = 1;
    return 1;
#else
    int ret = syscfg_read(CFG_LE_AUDIO_EN, &(g_le_audio_hdl.le_audio_en_config), 1);
    if (ret == 1) {
        log_debug("get_bt_le_audio_config_for_vm=%d\n", g_le_audio_hdl.le_audio_en_config);
        return g_le_audio_hdl.le_audio_en_config;
    }
    return 0;
#endif
}
u8 edr_conn_memcmp_filterate_for_addr(u8 *addr)
{
    if (g_le_audio_hdl.le_audio_en_config) {
        if (g_le_audio_hdl.cig_phone_conn_status & APP_CONNECTED_STATUS_CONNECT) {
            if (memcmp(addr, g_le_audio_hdl.peer_address, 6)) {
                return 1;
            }
        }
    }
    return 0;
}
//le_audio phone conn不进power down
static u8 le_audio_idle_query(void)
{
    if ((g_le_audio_hdl.cig_phone_conn_status & APP_CONNECTED_STATUS_CONNECT)
        || (g_le_audio_hdl.cig_phone_conn_status & APP_CONNECTED_STATUS_START)) {
        return 0;
    }
    return 1;
}

REGISTER_LP_TARGET(le_audio_lp_target) = {
    .name = "le_audio_play",
    .is_idle = le_audio_idle_query,
};

#endif

