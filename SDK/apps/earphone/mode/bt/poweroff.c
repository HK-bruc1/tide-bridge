#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".poweroff.data.bss")
#pragma data_seg(".poweroff.data")
#pragma const_seg(".poweroff.text.const")
#pragma code_seg(".poweroff.text")
#endif
#include "classic/hci_lmp.h"
#include "btstack/avctp_user.h"

#include "app_config.h"
#include "app_tone.h"
#include "app_main.h"
#include "earphone.h"
#include "a2dp_player.h"
#include "esco_player.h"
#include "idle.h"
#include "app_charge.h"
#include "bt_slience_detect.h"
#include "poweroff.h"
#include "bt_background.h"
#include "usb/otg.h"
#include "btstack/le/le_user.h"
#if TCFG_AUDIO_ANC_ENABLE
#include "audio_anc.h"
#endif
#if ((TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_UNICAST_SINK_EN | LE_AUDIO_JL_UNICAST_SINK_EN)))
#include "app_le_connected.h"
#endif

#if (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN)
#include "rdx_app_config.h"
#include "rdx_app.h"
#include "rdx_ble_server.h"
#include "rdx_record.h"
#endif

#if (TCFG_USER_TWS_ENABLE == 0)

#define LOG_TAG             "[POWEROFF]"
#define LOG_ERROR_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_INFO_ENABLE
#define LOG_CLI_ENABLE
#include "debug.h"




static u16 g_poweroff_timer = 0;
static u16 g_bt_detach_timer = 0;


static void sys_auto_shut_down_deal(void *priv);


void sys_auto_shut_down_disable(void)
{
#if TCFG_AUTO_SHUT_DOWN_TIME
    log_info("=== sys_auto_shut_down_disable, g_poweroff_timer = %d \n", g_poweroff_timer);
    if (g_poweroff_timer) {
        sys_timeout_del(g_poweroff_timer);
        g_poweroff_timer = 0;
    }
#endif
}

void sys_auto_shut_down_enable(void)
{
#if TCFG_AUTO_SHUT_DOWN_TIME
#if ((TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_UNICAST_SINK_EN | LE_AUDIO_JL_UNICAST_SINK_EN)))
    if (is_cig_phone_conn() || is_cig_other_phone_conn()) {
        printf("is_cig_phone_conn not auto shut down");
        return;
    }
#endif
#if TCFG_BT_BACKGROUND_ENABLE
    if (bt_background_active()) {
        log_info("sys_auto_shut_down_enable cannot in background\n");
        return;
    }
#endif
    /*ANC打开，不支持自动关机*/
#if TCFG_AUDIO_ANC_ENABLE
#if defined(TCFG_AUDIO_ANC_ON_AUTO_SHUT_DOWN) && (TCFG_AUDIO_ANC_ON_AUTO_SHUT_DOWN == 0)
    if (anc_status_get()) {
        return;
    }
#endif
#endif

    log_info("sys_auto_shut_down_enable\n");

#if (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN)
#if RDX_NEEDS_POWER_ACTIVITY_GUARD
    // extern RdxWifiInfo* rdx_app_get_wifi_info(void);
    extern bool rdx_app_get_dut_status(void);
    // extern RecordStatus* rdx_record_get_status(void);
    RecordStatus* rp = rdx_record_get_status();
    RdxWifiInfo* k = rdx_app_get_wifi_info();
    rdx_ble_server_info_t* pd = rdx_ble_server_get_info();
    r_printf("%s --> ble_conn: %d, k->conn_state: %d, rdx_app_get_dut_status: %d, rp->run: %d\r", 
             __FUNCTION__, pd->ble_conn, k->conn_state, rdx_app_get_dut_status(), rp->run);
    if (pd->ble_conn == 1 || k->conn_state == TRANSFER_BY_WIFI_ON || rdx_app_get_dut_status() || rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME) {
        r_printf("%s --> DO NOT SHUT DOWN, BLE is still work!", __FUNCTION__);
        return;
    }
    #if (TCFG_CHARGE_POWERON_ENABLE == 1)
        //开机充电使能
    extern u8 rdx_app_get_charge_state(void);
    if(1 == rdx_app_get_charge_state()){
        r_printf("%s --> DO NOT SHUT DOWN, it is incharge now!", __FUNCTION__);
        return;
    }
    #endif
#endif
#endif

    if (g_poweroff_timer == 0) {
        y_printf("===> %s --> auto_off_time: %d\n", __FUNCTION__, app_var.auto_off_time);
        g_poweroff_timer = sys_timeout_add(NULL, sys_auto_shut_down_deal,
                                           app_var.auto_off_time * 1000);
    }
#endif
}

static void sys_auto_shut_down_deal(void *priv)
{
//-------------------------------------------------------------------  
#if (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN)  
    extern u8 rdx_app_get_charge_state(void);
    int c_state = rdx_app_get_charge_state();
    if(c_state != 1 && c_state != 2){
        // r_printf("============================================================================ \n");
        // r_printf("======= %s --> DO NOT SHUT DOWN, Enter Idle! \n", __FUNCTION__);
        // r_printf("============================================================================ \n");
        // //dons++
        // extern void rdx_app_enter_idle(void);
        // rdx_app_enter_idle();
        r_printf("============================================================================ \n");
        r_printf("======= %s --> DO NOT SHUT DOWN, Enter POWEROFF! \n", __FUNCTION__);
        r_printf("============================================================================ \n");
        // extern void rdx_app_normal_poweroff(void);
        // rdx_app_normal_poweroff();
        sys_enter_soft_poweroff(POWEROFF_NORMAL);
        return;
    }
#else
    sys_enter_soft_poweroff(POWEROFF_NORMAL);
#endif
//-------------------------------------------------------------------  

}


static int poweroff_app_event_handler(int *msg)
{
    switch (msg[0]) {
    case APP_MSG_BT_IN_PAIRING_MODE:
        if (msg[1] == 0) {
            if (bt_mode_is_try_exit()) {
                break;
            }
            sys_auto_shut_down_enable();
        }
        break;
    }
    return 0;
}
APP_MSG_HANDLER(poweroff_app_msg_entry) = {
    .owner      = 0xff,
    .from       = MSG_FROM_APP,
    .handler    = poweroff_app_event_handler,
};


static int poweroff_btstack_event_handler(int *_event)
{
    struct bt_event *bt = (struct bt_event *)_event;

    switch (bt->event) {
    case BT_STATUS_SECOND_CONNECTED:
    case BT_STATUS_FIRST_CONNECTED:
        sys_auto_shut_down_disable();
        break;
    }
    return 0;
}
APP_MSG_HANDLER(poweroff_btstack_msg_stub) = {
    .owner      = 0xff,
    .from       = MSG_FROM_BT_STACK,
    .handler    = poweroff_btstack_event_handler,
};

static void wait_exit_btstack_flag(void *_reason)
{
    int reason = (int)_reason;

    if (!a2dp_player_runing() && !esco_player_runing()) {
        lmp_hci_reset();
        os_time_dly(2);
        sys_timer_del(g_bt_detach_timer);

        switch (reason) {
        case POWEROFF_NORMAL:
            log_info("task_switch to idle...\n");
            app_send_message(APP_MSG_GOTO_MODE, APP_MODE_IDLE | (IDLE_MODE_PLAY_POWEROFF << 8));

            break;
        case POWEROFF_RESET:
            log_info("cpu_reset!!!\n");
            cpu_reset();
            break;
        case POWEROFF_POWER_KEEP:
#if TCFG_CHARGE_ENABLE
            app_charge_power_off_keep_mode();
#endif
            break;
        }
    } else {
        if (++app_var.goto_poweroff_cnt > 200) {
            log_info("cpu_reset!!!\n");
            cpu_reset();
        }
        printf("wait_poweroff_cnt: %d\n", app_var.goto_poweroff_cnt);
    }
}


void sys_enter_soft_poweroff(enum poweroff_reason reason)
{
    log_info("===> sys_enter_soft_poweroff: %d, app_var.goto_poweroff_flag: %d\n", reason, app_var.goto_poweroff_flag);

#if ((TCFG_OTG_MODE & OTG_SLAVE_MODE) && (TCFG_OTG_MODE & OTG_CHARGE_MODE))
    u32 otg_status = usb_otg_online(0);
    if (otg_status == SLAVE_MODE) {
        return;
    }
#endif

    if (app_var.goto_poweroff_flag) {
        return;
    }

    app_var.goto_poweroff_flag = 1;
    app_var.goto_poweroff_cnt = 0;
    sys_auto_shut_down_disable();

#if TCFG_APP_BT_EN
    void bt_sniff_disable();
    bt_sniff_disable();
#endif

    bt_stop_a2dp_slience_detect(NULL);

    app_send_message(APP_MSG_POWER_OFF, reason);

#if ((TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_UNICAST_SINK_EN | LE_AUDIO_JL_UNICAST_SINK_EN)))
    le_audio_disconn_le_audio_link_no_reconnect();
#endif

    bt_cmd_prepare(USER_CTRL_POWER_OFF, 0, NULL);

#if (SYS_DEFAULT_VOL == 0)
    syscfg_write(CFG_SYS_VOL, &app_var.music_volume, 2);
#endif

#if TCFG_AUDIO_ANC_ENABLE
    anc_poweroff();
#endif

    g_bt_detach_timer = sys_timer_add((void *)reason, wait_exit_btstack_flag, 50);
}

#endif // (TCFG_USER_TWS_ENABLE == 0)
