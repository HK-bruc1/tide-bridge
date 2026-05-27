#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".tws_a2dp_play.data.bss")
#pragma data_seg(".tws_a2dp_play.data")
#pragma const_seg(".tws_a2dp_play.text.const")
#pragma code_seg(".tws_a2dp_play.text")
#endif
#include "btstack/avctp_user.h"
#include "btstack/btstack_task.h"
#include "classic/tws_api.h"
#include "os/os_api.h"
#include "bt_slience_detect.h"
#include "a2dp_player.h"
#include "esco_player.h"
#include "app_tone.h"
#include "app_main.h"
#include "vol_sync.h"
#include "audio_config.h"
#include "btstack/a2dp_media_codec.h"
#include "earphone.h"
#include "audio_manager.h"
#include "tws_a2dp_play.h"
#include "clock_manager/clock_manager.h"
#include "tws_dual_conn.h"
#include "dac_node.h"
#include "tws_dual_share.h"
#if (TCFG_LE_AUDIO_APP_CONFIG & LE_AUDIO_AURACAST_SINK_EN)
#include "le_audio_player.h"
#include "app_le_auracast.h"
#endif


#if TCFG_BT_DUAL_CONN_ENABLE

enum {
    CMD_A2DP_PLAY = 1,
    CMD_A2DP_SLIENCE_DETECT,
    CMD_A2DP_CLOSE,
    CMD_SET_A2DP_VOL,
    CMD_A2DP_SWITCH,
    CMD_A2DP_MUTE,
    CMD_A2DP_MUTE_BY_CALL,
};

static u8 g_play_addr[6];
static u8 g_slience_addr[6];
static u8 g_closing_addr[6];
static u8 a2dp_preempted_addr[6];
static u8 a2dp_energy_detect_addr[6];
static u8 a2dp_drop_packet_detect_addr[6];
static u8 g_a2dp_slience_detect;
static u32 g_a2dp_slience_time;
static u32 g_a2dp_play_time;

int g_avrcp_vol_chance_timer = 0;
u8 g_avrcp_vol_chance_data[8];

#if TCFG_A2DP_PREEMPTED_ENABLE
u8 a2dp_avrcp_play_cmd_addr[6] = {0};
#else
u8 g_a2dp_phone_call_addr[6] = {0};
#endif

void tws_a2dp_play_send_cmd(u8 cmd, u8 *data, u8 len, u8 tx_do_action);

void tws_a2dp_player_close(u8 *bt_addr)
{
    puts("tws_a2dp_player_close\n");
    put_buf(bt_addr, 6);
    a2dp_player_close(bt_addr);
    bt_stop_a2dp_slience_detect(bt_addr);
    a2dp_media_unmute(bt_addr);
    a2dp_media_close(bt_addr);
}

void a2dp_energy_detect_handler(int *arg)
{
    int energy = arg[0];
    int time = arg[1];  // msec * 10

    g_a2dp_play_time += time;

    /*
     * 恢复被通话打断的a2dp,前面100ms做能量检测, 如果没有能量可能是播放器已暂停,
     * 需要发送播放命令
     */
    if (a2dp_player_is_playing(a2dp_energy_detect_addr)) {
        if (energy < 10) {
            g_a2dp_slience_time += time;
        } else {
            g_a2dp_slience_time = 0;
        }
        if (g_a2dp_slience_time >= 1000) {
            int msg[4];
            msg[0] = APP_MSG_SEND_A2DP_PLAY_CMD;
            memcpy(msg + 1, a2dp_energy_detect_addr, 6);
            memset(a2dp_energy_detect_addr, 0xff, 6);
            app_send_message_from(MSG_FROM_APP, 12, msg);
        } else if (g_a2dp_play_time >= 1000) {
            memset(a2dp_energy_detect_addr, 0xff, 6);
        }
    }
#if !TCFG_A2DP_PREEMPTED_ENABLE
    if (bt_a2dp_slience_detect_num() < 1) {
        // 无后台待播放设备
        return;
    }
#endif
    if (g_a2dp_slience_detect == 0 && g_a2dp_play_time >= 10000) {
        /* 播放1s后开启静音检测 */
        g_a2dp_slience_detect = 1;
        g_a2dp_slience_time = 0;
    }
    if (energy < 10) {
        if (g_a2dp_slience_detect == 1) {
            g_a2dp_slience_time += time;
            if (g_a2dp_slience_time >= 50000) {
                /* 静音超过5s, 切换到后台静音设备播放,
                 * 时间设置过短容易导致切歌过程中切换到后台设备播放
                 * */
                int msg[4];
                msg[0] = APP_MSG_A2DP_NO_ENERGY;
                a2dp_player_get_btaddr((u8 *)(msg + 1));
                app_send_message_from(MSG_FROM_APP, 12, msg);
                g_a2dp_slience_detect = 2;
            }
        }
    } else {
        if (g_a2dp_slience_detect == 1) {
            g_a2dp_slience_time = 0;
        }
    }
}

static void tws_a2dp_play_in_task(u8 *data)
{
    u8 btaddr[6];
    u8 dev_vol;
    u8 *bt_addr = data + 2;

    switch (data[0]) {
    case CMD_A2DP_SLIENCE_DETECT:
        puts("CMD_A2DP_SLIENCT_DETECE\n");
        put_buf(bt_addr, 6);
        a2dp_player_close(bt_addr);
        bt_start_a2dp_slience_detect(bt_addr, 50);
        memset(g_slience_addr, 0xff, 6);
        break;
    case CMD_A2DP_MUTE:
        puts("CMD_A2DP_MUTE\n");
        put_buf(bt_addr, 6);
        a2dp_player_close(bt_addr);
        memset(g_slience_addr, 0xff, 6);
        a2dp_media_mute(bt_addr);
        bt_start_a2dp_slience_detect(bt_addr, 0);
        break;
    case CMD_A2DP_MUTE_BY_CALL:
        puts("CMD_A2DP_MUTE_BY_CALL\n");
        put_buf(bt_addr, 6);
        a2dp_player_close(bt_addr);
        a2dp_media_mute(bt_addr);
        bt_start_a2dp_slience_detect(bt_addr, 0);
        memcpy(a2dp_preempted_addr, bt_addr, 6);
        memset(a2dp_energy_detect_addr, 0xff, 6);
        break;
    case CMD_A2DP_PLAY:
        puts("app_msg_bt_a2dp_play\n");
        put_buf(bt_addr, 6);
#if (TCFG_BT_A2DP_PLAYER_ENABLE == 0)
        break;
#endif
        dev_vol = data[8];
        //更新一下音量再开始播放
        if (!a2dp_player_runing() || a2dp_player_is_playing(bt_addr)) {
            app_audio_state_switch(APP_AUDIO_STATE_MUSIC,
                                   app_audio_volume_max_query(AppVol_BT_MUSIC), NULL);
            set_music_device_volume(dev_vol);
        }
        a2dp_media_unmute(bt_addr);
        bt_stop_a2dp_slience_detect(bt_addr);
        a2dp_player_low_latency_enable(tws_api_get_low_latency_state());
        if (memcmp(a2dp_preempted_addr, bt_addr, 6) == 0) {
            memset(a2dp_preempted_addr, 0xff, 6);
        }
        if (memcmp(a2dp_drop_packet_detect_addr, bt_addr, 6) == 0) {
            memset(a2dp_drop_packet_detect_addr, 0xff, 6);
        }

        g_a2dp_play_time = 0;
        g_a2dp_slience_detect = 0;
        int err = a2dp_player_open(bt_addr);
        if (err == -EBUSY) {
            bt_start_a2dp_slience_detect(bt_addr, 50);
        }
#if TCFG_TWS_AUDIO_SHARE_ENABLE
        check_phone_a2dp_play_share_auto_slave_to_master(bt_addr);
        share_a2dp_slave_clr_preempted_flag(bt_addr, 0);
#endif
        memset(g_play_addr, 0xff, 6);
        int msg[4];
        msg[0] = APP_MSG_BT_A2DP_OPEN_MEDIA_SUCCESS;
        memcpy(msg + 1, bt_addr, 6);
        app_send_message_from(MSG_FROM_APP, 12, msg);
        break;
    case CMD_A2DP_CLOSE:
        puts("CMD_A2DP_CLOSE\n");
        tws_a2dp_player_close(bt_addr);
        /*
         * 如果后台有A2DP数据,关闭检测和MEDIA_START状态,
         * 等待协议栈重新发送MEDIA_START消息
         */
        if (bt_slience_get_detect_addr(btaddr)) {
            bt_stop_a2dp_slience_detect(btaddr);
            a2dp_media_unmute(btaddr);
            a2dp_media_close(btaddr);
        }
        if (memcmp(bt_addr, g_closing_addr, 6) == 0) {
            memset(g_closing_addr, 0xff, 6);
        }
#if TCFG_TWS_AUDIO_SHARE_ENABLE
        share_a2dp_preempted_resume(bt_addr);
#endif
        break;
    case CMD_A2DP_SWITCH:
        puts("CMD_A2DP_SWITCH\n");
        if (!bt_slience_get_detect_addr(btaddr)) {
            break;
        }
        a2dp_player_close(bt_addr);
        a2dp_media_mute(bt_addr);
        bt_start_a2dp_slience_detect(bt_addr, 0);

        bt_stop_a2dp_slience_detect(btaddr);
        a2dp_media_unmute(btaddr);
        a2dp_media_close(btaddr);
        break;
    case CMD_SET_A2DP_VOL:
        dev_vol = data[8];
        set_music_device_volume(dev_vol);
        break;
    }
    if (data[1] != 2) {
        free(data);
    }
}

#if TCFG_USER_TWS_ENABLE
static void tws_a2dp_play_callback(u8 *data, u8 len)
{
    int msg[4];

    u8 *buf = malloc(len);
    if (!buf) {
        return;
    }
    memcpy(buf, data, len);

    msg[0] = (int)tws_a2dp_play_in_task;
    msg[1] = 1;
    msg[2] = (int)buf;

    os_taskq_post_type("app_core", Q_CALLBACK, 3, msg);
}

static void tws_a2dp_player_data_in_irq(void *_data, u16 len, bool rx)
{
    u8 *data = (u8 *)_data;

    if (data[1] == 0 && !rx) {
        return;
    }
    tws_a2dp_play_callback(data, len);
}
REGISTER_TWS_FUNC_STUB(tws_a2dp_player_stub) = {
    .func_id = 0x076AFE82,
    .func = tws_a2dp_player_data_in_irq,
};

void tws_a2dp_play_send_cmd(u8 cmd, u8 *_data, u8 len, u8 tx_do_action)
{
    u8 data[16];
    data[0] = cmd;
    data[1] = tx_do_action;
    memcpy(data + 2, _data, len);
    int err = tws_api_send_data_to_sibling(data, len + 2, 0x076AFE82);
    if (err) {
        data[1] = 2;
        tws_a2dp_play_in_task(data);
    } else {
        if (cmd == CMD_A2DP_PLAY) {
            memcpy(g_play_addr, _data, 6);
        } else if (cmd == CMD_A2DP_SLIENCE_DETECT || cmd == CMD_A2DP_MUTE) {
            memcpy(g_slience_addr, _data, 6);
        } else if (cmd == CMD_A2DP_CLOSE) {
            memcpy(g_closing_addr, _data, 6);
        }
    }
}
#else
void tws_a2dp_play_send_cmd(u8 cmd, u8 *_data, u8 len, u8 tx_do_action)
{
    u8 data[16];

    if (!tx_do_action) {
        return;
    }
    data[0] = cmd;
    data[1] = 2;
    memcpy(data + 2, _data, len);
    tws_a2dp_play_in_task(data);
}
#endif

void tws_a2dp_sync_play(u8 *bt_addr, bool tx_do_action)
{
    u8 data[8];
    memcpy(data, bt_addr, 6);
    data[6] = bt_get_music_volume(bt_addr);
    if (data[6] > 127) {
        data[6] = app_audio_bt_volume_update(bt_addr, APP_AUDIO_STATE_MUSIC);
    }
    tws_a2dp_play_send_cmd(CMD_A2DP_PLAY, data, 7, tx_do_action);
}

void tws_a2dp_slience_detect(u8 *bt_addr, bool tx_do_action)
{
    tws_a2dp_play_send_cmd(CMD_A2DP_SLIENCE_DETECT, bt_addr, 6, tx_do_action);
}
static void avrcp_vol_chance_timeout(void *priv)
{
    g_avrcp_vol_chance_timer = 0;
    tws_a2dp_play_send_cmd(CMD_SET_A2DP_VOL, g_avrcp_vol_chance_data, 7, 1);
#if TCFG_TWS_AUDIO_SHARE_ENABLE
    bt_tws_share_master_sync_vol_to_share_slave();
#endif

}

void try_play_preempted_a2dp(void *p)
{
    void *device = btstack_get_conn_device(a2dp_preempted_addr);
    if (!device) {
        memset(a2dp_preempted_addr, 0xff, 6);
        return;
    }
    if (bt_get_call_status() != BT_CALL_HANGUP) {
        sys_timeout_add(NULL, try_play_preempted_a2dp, 500);
        return;
    }
    if (!a2dp_player_is_playing(a2dp_preempted_addr)) {
        memset(a2dp_preempted_addr, 0xff, 6);
        btstack_device_control(device, USER_CTRL_AVCTP_OPID_PLAY);
    }
}

static void a2dp_suspend_by_call(u8 *play_addr, void *play_device)
{
    if (tws_api_get_role() == TWS_ROLE_SLAVE) {
        return;
    }
    if (play_addr && play_device && a2dp_player_is_playing(play_addr)) {
        puts("--a2dp_mute\n");
        put_buf(play_addr, 6);
        memcpy(a2dp_preempted_addr, play_addr, 6);
        memset(a2dp_energy_detect_addr, 0xff, 6);
        btstack_device_control(play_device, USER_CTRL_AVCTP_OPID_PAUSE);
        tws_a2dp_play_send_cmd(CMD_A2DP_MUTE_BY_CALL, play_addr, 6, 1);
    }
}

static int a2dp_bt_status_event_handler(int *event)
{
    int ret;
    u8 data[8];
    u8 btaddr[6];
    struct bt_event *bt = (struct bt_event *)event;
    void *device_a, *device_b;

    bt_get_btstack_device(bt->args, &device_a, &device_b);
    u8 *addr_b = device_b ? btstack_get_device_mac_addr(device_b) : NULL;


    switch (bt->event) {
    case BT_STATUS_A2DP_MEDIA_START:
        g_printf("BT_STATUS_A2DP_MEDIA_START\n");
        put_buf(bt->args, 6);
        if (app_var.goto_poweroff_flag) {
            break;
        }
#if (TCFG_LE_AUDIO_APP_CONFIG & LE_AUDIO_AURACAST_SINK_EN)
        if (le_audio_player_is_playing()) {
            le_auracast_stop();
        }
#endif
        if (tws_api_get_role() == TWS_ROLE_MASTER) {
#if TCFG_A2DP_PREEMPTED_ENABLE
            if (device_b &&
                (btstack_get_call_esco_status(device_b) == BT_ESCO_STATUS_OPEN)) {
#else
            if (device_b &&
                ((btstack_get_call_esco_status(device_b) == BT_ESCO_STATUS_OPEN) ||
                 (memcmp(g_a2dp_phone_call_addr, addr_b, 6) == 0))) {
#endif
                puts("---mute_a\n");
                a2dp_media_mute(bt->args);
                bt_start_a2dp_slience_detect(bt->args, 0);
                btstack_device_control(device_a, USER_CTRL_AVCTP_OPID_PAUSE);
                break;
            }
        }
        dac_try_power_on_thread();//dac初始化耗时有120ms,此处提前将dac指定到独立任务内做初始化，优化蓝牙通路启动的耗时，减少时间戳超时的情况
        if (tws_api_get_role() == TWS_ROLE_MASTER &&
            bt_get_call_status_for_addr(bt->args) == BT_CALL_INCOMING) {
            //小米11来电挂断偶现没有hungup过来，hfp链路异常，重新断开hfp再连接
            puts("<<<<<<<<waring a2dp start hfp_incoming\n");
            bt_cmd_prepare_for_addr(bt->args, USER_CTRL_HFP_DISCONNECT, 0, NULL);
            bt_cmd_prepare_for_addr(bt->args, USER_CTRL_HFP_CMD_CONN, 0, NULL);
        }
        if (esco_player_runing()) {
            r_printf("esco_player_runing");
            a2dp_media_close(bt->args);
            break;
        }
#if defined(CONFIG_CPU_BR52)
        if (CONFIG_AES_CCM_FOR_EDR_ENABLE) {
            clock_alloc("aes_a2dp_play", 64 * 1000000UL);
        }
#endif
        if (tws_api_get_role() == TWS_ROLE_SLAVE) {
            break;
        }
        if (memcmp(a2dp_drop_packet_detect_addr, bt->args, 6) == 0) {
            g_printf("bt_action_is_drop_flag");
            if (addr_b && ((memcmp(a2dp_energy_detect_addr, addr_b, 6) == 0) ||
                           (memcmp(a2dp_preempted_addr, addr_b, 6) == 0))) {
                tws_a2dp_play_send_cmd(CMD_A2DP_MUTE, bt->args, 6, 1);
                break;
            }
            memset(a2dp_drop_packet_detect_addr, 0xff, 6);
        }

        if (a2dp_player_get_btaddr(btaddr)) {
            if (memcmp(btaddr, bt->args, 6) == 0) {
                tws_a2dp_sync_play(bt->args, 1);
            } else {
#if TCFG_A2DP_PREEMPTED_ENABLE
                tws_a2dp_slience_detect(bt->args, 1);
#else
                if (memcmp(btaddr, g_closing_addr, 6) == 0) {
                    a2dp_media_close(bt->args);
                } else {
                    tws_a2dp_play_send_cmd(CMD_A2DP_MUTE, bt->args, 6, 1);
                }
#endif
            }
        } else {
            tws_a2dp_sync_play(bt->args, 1);
        }
        break;
    case BT_STATUS_A2DP_MEDIA_STOP:
        g_printf("BT_STATUS_A2DP_MEDIA_STOP\n");
#if defined(CONFIG_CPU_BR52)
        if (CONFIG_AES_CCM_FOR_EDR_ENABLE) {
            clock_free("aes_a2dp_play");
        }
#endif
        put_buf(bt->args, 6);
        if (tws_api_get_role() == TWS_ROLE_SLAVE) {
            break;
        }
        if (memcmp(a2dp_energy_detect_addr, bt->args, 6) == 0) {
            memcpy(a2dp_preempted_addr, bt->args, 6);
            memset(a2dp_energy_detect_addr, 0xff, 6);
            sys_timeout_add(NULL, try_play_preempted_a2dp, 500);
        }
        tws_a2dp_play_send_cmd(CMD_A2DP_CLOSE, bt->args, 6, 1);
        break;
    case BT_STATUS_AVRCP_VOL_CHANGE:
        puts("BT_STATUS_AVRCP_VOL_CHANGE\n");
        //判断是当前地址的音量值才更新
        clock_refurbish();
        if (tws_api_get_role() == TWS_ROLE_SLAVE) {
            break;
        }
        ret = a2dp_player_get_btaddr(data);
        if (ret && memcmp(data, bt->args, 6) == 0) {
            data[6] = bt->value;

            memcpy(g_avrcp_vol_chance_data, data, 7);
            if (g_avrcp_vol_chance_timer) {
                sys_timer_modify(g_avrcp_vol_chance_timer, 100);
            } else {
                g_avrcp_vol_chance_timer = sys_timeout_add(NULL, avrcp_vol_chance_timeout, 100);
            }
        }
        break;
    case BT_STATUS_AVDTP_START:
        g_printf("BT_STATUS_AVDTP_START\n");
        put_buf(bt->args, 6);
        break;
    case BT_STATUS_AVDTP_SUSPEND:
        g_printf("BT_STATUS_AVDTP_SUSPEND\n");
        put_buf(bt->args, 6);
        a2dp_media_unmute(bt->args);
        break;
    case BT_STATUS_AVRCP_INCOME_OPID:
        printf("BT_STATUS_AVRCP_INCOME_OPID: %x\n", bt->value);
        put_buf(bt->args, 6);
#if TCFG_A2DP_PREEMPTED_ENABLE
        if (bt->value == AVC_PLAY) {
            memcpy(a2dp_avrcp_play_cmd_addr, bt->args, 6);
        }
#endif
        if (tws_api_get_role() == TWS_ROLE_SLAVE) {
            break;
        }
        if (bt->value != AVC_PLAY) {
            break;
        }
        if (device_b &&
            btstack_get_call_esco_status(device_b) == BT_ESCO_STATUS_OPEN) {
            puts("--mute_a\n");
            a2dp_media_mute(bt->args);
            btstack_device_control(device_a, USER_CTRL_AVCTP_OPID_PAUSE);
            break;
        }
        break;
    case BT_STATUS_SCO_CONNECTION_REQ:
        puts("A2DP BT_STATUS_SCO_CONNECTION_REQ\n");
        put_buf(bt->args, 6);
        a2dp_suspend_by_call(addr_b, device_b);
        break;
    case BT_STATUS_PHONE_INCOME:
    case BT_STATUS_PHONE_OUT:
        printf("A2DP BT_STATUS_PHONE:%d\n", bt->event);
        put_buf(bt->args, 6);
#if !TCFG_A2DP_PREEMPTED_ENABLE
        memset(g_a2dp_phone_call_addr, bt->args, 6);
#endif
        break;
    case BT_STATUS_PHONE_HANGUP:
    case BT_STATUS_SCO_DISCON:
        printf("A2DP BT_STATUS_SCO_DISCON:%d\n", bt->event);
#if !TCFG_A2DP_PREEMPTED_ENABLE
        memset(g_a2dp_phone_call_addr, 0xff, 6);
#endif
        if (tws_api_get_role() == TWS_ROLE_SLAVE) {
            break;
        }
        if (addr_b && device_b && bt_not_in_phone_call_state(device_b)) {
            if (memcmp(a2dp_preempted_addr, addr_b, 6) == 0) {
                if (a2dp_media_is_mute(addr_b)) {
                    puts("--a2dp_unmute-b\n");
                    tws_api_role_switch_lock_msec(1500);
                    a2dp_media_unmute(addr_b);
                    a2dp_media_close(addr_b);
                    memcpy(a2dp_energy_detect_addr, addr_b, 6);
                    memset(a2dp_preempted_addr, 0xff, 6);
                } else {
                    sys_timeout_add(NULL, try_play_preempted_a2dp, 500);
                }
                memcpy(a2dp_drop_packet_detect_addr, bt->args, 6);
                break;
            }
            /* 手机A通话中,点击2次手机B的音频播放或打开抖音导致无法暂停
             * 这里取消静音和能量检查，等待协议栈重新发送MEDIA_START消息
             * 此处无法区分是否为没播放完的提示音，所以不走上面的流程，
             * 防止发送PLAY命令把手机B的播放器拉起
             */
            if (a2dp_media_is_mute(addr_b)) {
                bt_stop_a2dp_slience_detect(addr_b);
                a2dp_media_unmute(addr_b);
                a2dp_media_close(addr_b);
            }
        }
        if (memcmp(a2dp_preempted_addr, bt->args, 6) == 0) {
            puts("--a2dp_unmute-a\n");
            sys_timeout_add(NULL, try_play_preempted_a2dp, 500);
        }
        break;
    case BT_STATUS_SCO_STATUS_CHANGE:
        printf("A2DP BT_STATUS_SCO_STATUS_CHANGE len:%d, type:%d\n",
               (bt->value >> 16), (bt->value & 0x0000ffff));
        if (bt->value != 0xff) {
            a2dp_suspend_by_call(addr_b, device_b);
        }
        break;
    case BT_STATUS_FIRST_CONNECTED:
    case BT_STATUS_SECOND_CONNECTED:
        if (memcmp(a2dp_preempted_addr, bt->args, 6) == 0) {
            memset(a2dp_preempted_addr, 0xff, 6);
        }
        break;
    }
    return 0;
}
APP_MSG_HANDLER(a2dp_stack_msg_handler) = {
    .owner      = 0xff,
    .from       = MSG_FROM_BT_STACK,
    .handler    = a2dp_bt_status_event_handler,
};


static int a2dp_bt_hci_event_handler(int *event)
{
    struct bt_event *bt = (struct bt_event *)event;

    switch (bt->event) {
    case HCI_EVENT_DISCONNECTION_COMPLETE:
        tws_a2dp_player_close(bt->args);
        break;
    }

    return 0;
}
APP_MSG_HANDLER(a2dp_hci_msg_handler) = {
    .owner      = 0xff,
    .from       = MSG_FROM_BT_HCI,
    .handler    = a2dp_bt_hci_event_handler,
};

static int a2dp_app_msg_handler(int *msg)
{
    u8 *bt_addr = (u8 *)(msg +  1);
    u8 addr[6];

    switch (msg[0]) {
    case APP_MSG_BT_A2DP_START:
#if TCFG_A2DP_PREEMPTED_ENABLE
        if (a2dp_player_get_btaddr(addr)) {
            /* 后台设备a2dp有能量,转为前台播放,
             * 前台设备转为后台静音, 不做能量检测, 防止抖音这种无法暂停的播放器又抢回来
             */
            void *device = btstack_get_conn_device(addr);
            if (device) {
                btstack_device_control(device, USER_CTRL_AVCTP_OPID_PAUSE);
            }
            tws_a2dp_play_send_cmd(CMD_A2DP_MUTE, addr, 6, 1);
        }
        tws_a2dp_sync_play(bt_addr, 1);
#endif
        break;
    case APP_MSG_BT_A2DP_PAUSE:
        puts("app_msg_bt_a2dp_pause\n");
        if (a2dp_player_is_playing(bt_addr)) {
            tws_a2dp_play_send_cmd(CMD_A2DP_MUTE, bt_addr, 6, 1);
        }
        break;
    case APP_MSG_BT_A2DP_STOP:
        puts("APP_MSG_BT_A2DP_STOP\n");
        tws_a2dp_play_send_cmd(CMD_A2DP_CLOSE, bt_addr, 6, 1);
        break;
    case APP_MSG_BT_A2DP_PLAY:
        puts("APP_MSG_BT_A2DP_PLAY\n");
        if (memcmp(a2dp_energy_detect_addr, bt_addr, 6) == 0) {
            memset(a2dp_energy_detect_addr, 0xff, 6);
        }
        tws_a2dp_sync_play(bt_addr, 1);
        break;
    case APP_MSG_SEND_A2DP_PLAY_CMD:
        if (tws_api_get_role() == TWS_ROLE_SLAVE) {
            break;
        }
        void *device = btstack_get_conn_device(bt_addr);
        if (device) {
            btstack_device_control(device, USER_CTRL_AVCTP_OPID_PLAY);
        }
        break;
    case APP_MSG_A2DP_NO_ENERGY:
        puts("APP_MSG_A2DP_NO_ENERGY\n");
        if (tws_api_get_role() == TWS_ROLE_SLAVE) {
            break;
        }
        if (a2dp_player_is_playing(bt_addr)) {
            tws_a2dp_play_send_cmd(CMD_A2DP_SWITCH, bt_addr, 6, 1);
        }
        break;
    }
    return 0;
}
APP_MSG_HANDLER(a2dp_app_msg_handler_stub) = {
    .owner      = 0xff,
    .from       = MSG_FROM_APP,
    .handler    = a2dp_app_msg_handler,
};

#if TCFG_USER_TWS_ENABLE
static int a2dp_tws_msg_handler(int *msg)
{
    struct tws_event *evt = (struct tws_event *)msg;
    u8 role = evt->args[0];
    u8 state = evt->args[1];
    u8 *bt_addr = evt->args + 3;
    u8 addr[6];

    switch (evt->event) {
    case TWS_EVENT_MONITOR_START:
        if (role == TWS_ROLE_SLAVE) {
            break;
        }
        if (a2dp_player_is_playing(bt_addr)) {
            if (memcmp(g_slience_addr, bt_addr, 6)) {
                puts("--monitor_start_play\n");
                tws_a2dp_sync_play(bt_addr, 0);
            }
        } else {
            int result = bt_slience_detect_get_result(bt_addr);
            if (result != BT_SLIENCE_NO_DETECTING) {
                if (memcmp(g_play_addr, bt_addr, 6)) {
                    puts("--monitor_start_slience_detect\n");
                    tws_a2dp_slience_detect(bt_addr,  0);
                }
            }
        }
        break;
    case TWS_EVENT_PHONE_LINK_DETACH:
        tws_a2dp_player_close(evt->args + 3);
        break;
    case TWS_EVENT_ROLE_SWITCH:
        while (bt_slience_get_detect_addr(addr)) {
            bt_stop_a2dp_slience_detect(addr);
            a2dp_media_unmute(addr);
            a2dp_media_close(addr);
        }
        break;
    }
    return 0;
}
APP_MSG_HANDLER(a2dp_tws_msg_handler_stub) = {
    .owner      = 0xff,
    .from       = MSG_FROM_TWS,
    .handler    = a2dp_tws_msg_handler,
};
#endif


#endif


