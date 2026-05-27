#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".bt_tws.data.bss")
#pragma data_seg(".bt_tws.data")
#pragma const_seg(".bt_tws.text.const")
#pragma code_seg(".bt_tws.text")
#endif
#include "system/includes.h"
#include "media/includes.h"
#include "device/vm.h"
#include "app_tone.h"
#include "a2dp_player.h"

#include "app_config.h"
#include "earphone.h"

#include "btstack/avctp_user.h"
#include "classic/hci_lmp.h"
#include "user_cfg.h"
#include "bt_tws.h"
#include "asm/charge.h"
#include "app_charge.h"

#include "app_testbox.h"
#include "app_online_cfg.h"
#include "app_main.h"
#include "app_power_manage.h"
#include "battery_manager.h"
#include "audio_config.h"
#include "bt_slience_detect.h"
#include "esco_recoder.h"
#include "bt_common.h"
#include "tws_dual_conn.h"
#include "update.h"
#include "in_ear_detect/in_ear_manage.h"
#include "multi_protocol_main.h"
#include "phone_call.h"

#include "multi_protocol_main.h"
#if ((TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_UNICAST_SINK_EN | LE_AUDIO_JL_UNICAST_SINK_EN)))
#include "app_le_connected.h"
#endif
#if TCFG_AUDIO_ANC_ENABLE
#include "audio_anc.h"
#endif

#if TCFG_ANC_BOX_ENABLE
#include "app_ancbox.h"
#endif

#if TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN
#include "icsd_adt_app.h"
#endif /*TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN*/

#if (TCFG_USER_TWS_ENABLE && TCFG_APP_BT_EN)

#define LOG_TAG             "[BT-TWS]"
#define LOG_ERROR_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_INFO_ENABLE
#define LOG_CLI_ENABLE
#include "debug.h"

#if TCFG_DEC2TWS_ENABLE
#define    CONFIG_BT_TWS_SNIFF                  0       //[WIP]
#else
#define    CONFIG_BT_TWS_SNIFF                  1       //[WIP]
#endif

#define    BT_TWS_UNPAIRED                      0x0001
#define    BT_TWS_PAIRED                        0x0002
#define    BT_TWS_WAIT_SIBLING_SEARCH           0x0004
#define    BT_TWS_SEARCH_SIBLING                0x0008
#define    BT_TWS_CONNECT_SIBLING               0x0010
#define    BT_TWS_SIBLING_CONNECTED             0x0020
#define    BT_TWS_PHONE_CONNECTED               0x0040
#define    BT_TWS_POWER_ON                      0x0080
#define    BT_TWS_TIMEOUT                       0x0100
#define    BT_TWS_AUDIO_PLAYING                 0x0200
#define    BT_TWS_DISCON_DLY_TIMEOUT            0x0400
#define    BT_TWS_REMOVE_PAIRS                  0x0800


struct tws_user_var {
    u8 addr[6];
    u16 state;
    u8  device_role;  //tws 记录那个是active device 活动设备，音源控制端
    u16 sniff_timer;
};

struct tws_user_var  gtws;

static u8 tone_together_by_systime = 0;
static u32 tws_tone_together_time = 0;
extern const u8 adt_profile_support;

void tws_sniff_controle_check_enable(void);
extern u8 tws_edr_tx_att_data_to_peer(void);
extern u8 get_edr_att_link_num();

u8 tws_network_audio_was_started(void)
{
    if (gtws.state & BT_TWS_AUDIO_PLAYING) {
        return 1;
    }

    return 0;
}

void tws_network_local_audio_start(void)
{
    gtws.state &= ~BT_TWS_AUDIO_PLAYING;
}


/*
 * 主从同步调用函数处理
 */
static void tws_sync_call_fun(int cmd, int err)
{
    log_d("TWS_EVENT_SYNC_FUN_CMD: %d\n", cmd);

    switch (cmd) {
    case SYNC_CMD_EARPHONE_CHAREG_START:
        if (bt_a2dp_get_status() != BT_MUSIC_STATUS_STARTING) {
            bt_cmd_prepare(USER_CTRL_AVCTP_OPID_PLAY, 0, NULL);
        }
        break;
    case SYNC_CMD_IRSENSOR_EVENT_NEAR:
        if (bt_a2dp_get_status() != BT_MUSIC_STATUS_STARTING) {
            bt_cmd_prepare(USER_CTRL_AVCTP_OPID_PLAY, 0, NULL);
        }
        break;
    case SYNC_CMD_IRSENSOR_EVENT_FAR:
        if (bt_a2dp_get_status() == BT_MUSIC_STATUS_STARTING) {
            bt_cmd_prepare(USER_CTRL_AVCTP_OPID_PAUSE, 0, NULL);
        }
        break;
    }
}

TWS_SYNC_CALL_REGISTER(tws_tone_sync) = {
    .uuid = 'T',
    .task_name = "app_core",
    .func = tws_sync_call_fun,
};

u8 tws_tone_together_without_bt(void)
{
    return tone_together_by_systime;
}

void tws_tone_together_clean(void)
{
    tone_together_by_systime = 0;
}

u32 tws_tone_local_together_time(void)
{
    return tws_tone_together_time;
}


u16 tws_host_get_battery_voltage()
{
    return get_vbat_value();
}
bool tws_host_role_switch_by_power_balance(u16 m_voltage, u16 s_voltage)
{
    if (m_voltage + 100 <= s_voltage) {
        return 1;
    }
    return 0;
}

int tws_host_role_switch_by_power_update_time()
{
    return (60 * 1000);
}

int tws_host_channel_match(char remote_channel)
{
    /*r_printf("tws_host_channel_match: %c, %c\n", remote_channel,
             bt_tws_get_local_channel());*/
#if CONFIG_TWS_CHANNEL_SELECT == CONFIG_TWS_START_PAIR_AS_LEFT || \
    CONFIG_TWS_CHANNEL_SELECT == CONFIG_TWS_START_PAIR_AS_RIGHT || \
    CONFIG_TWS_CHANNEL_SELECT == CONFIG_TWS_MASTER_AS_LEFT || \
    CONFIG_TWS_CHANNEL_SELECT == CONFIG_TWS_MASTER_AS_RIGHT || \
    CONFIG_TWS_CHANNEL_SELECT == CONFIG_TWS_CHANNEL_SELECT_BY_BOX
    return 1;
#else
    if (remote_channel != bt_tws_get_local_channel()) {
        return 1;
    }
#endif

    return 0;
}

u8 get_tws_soft_version()
{
    u8 soft_version = 0x20;
    u8 cpu_info = 0;
#if defined(CONFIG_CPU_BR28)
    cpu_info = 28;
#elif defined(CONFIG_CPU_BR36)
    cpu_info = 36;
#elif defined(CONFIG_CPU_BR27)
    cpu_info = 27;
#elif defined(CONFIG_CPU_BR50)
    cpu_info = 50;
#elif defined(CONFIG_CPU_BR52)
    cpu_info = 52;
#elif defined(CONFIG_CPU_BR56)
    cpu_info = 56;
#elif defined(CONFIG_CPU_BR42)
    cpu_info = 42;
#else
#error "not define cpu tws soft version"
#endif
    return (soft_version + cpu_info);
}

char tws_host_get_local_channel()
{
    char channel;

#if (CONFIG_TWS_CHANNEL_SELECT == CONFIG_TWS_START_PAIR_AS_RIGHT)
    if (gtws.state & BT_TWS_SEARCH_SIBLING) {
        return 'R';
    }
#elif (CONFIG_TWS_CHANNEL_SELECT == CONFIG_TWS_START_PAIR_AS_LEFT)
    if (gtws.state & BT_TWS_SEARCH_SIBLING) {
        return 'L';
    }
#endif
    channel = bt_tws_get_local_channel();
#if CONFIG_TWS_CHANNEL_SELECT == CONFIG_TWS_MASTER_AS_LEFT
    if (channel != 'R') {
        channel = 'L';
    }
#else
    if (channel != 'L') {
        channel = 'R';
    }
#endif
    /*y_printf("tws_host_get_local_channel: %c\n", channel);*/

    return channel;
}

#if CONFIG_TWS_COMMON_ADDR_SELECT != CONFIG_TWS_COMMON_ADDR_AUTO
void tws_host_get_common_addr(u8 *remote_mac_addr, u8 *common_addr, char channel)
{
#if CONFIG_TWS_COMMON_ADDR_SELECT == CONFIG_TWS_COMMON_ADDR_USED_LEFT
    if (channel == 'L') {
        memcpy(common_addr, bt_get_mac_addr(), 6);
    } else {
        memcpy(common_addr, remote_mac_addr, 6);
    }
#elif CONFIG_TWS_COMMON_ADDR_SELECT == CONFIG_TWS_COMMON_ADDR_USED_RIGHT
    if (channel == 'R') {
        memcpy(common_addr, bt_get_mac_addr(), 6);
    } else {
        memcpy(common_addr, remote_mac_addr, 6);
    }
#elif CONFIG_TWS_COMMON_ADDR_SELECT == CONFIG_TWS_COMMON_ADDR_USED_MASTER
    memcpy(common_addr, bt_get_mac_addr(), 6);
#endif
}
#endif

#if TCFG_DEC2TWS_ENABLE
int tws_host_get_local_role()
{
    if (app_var.have_mass_storage) {
        return TWS_ROLE_MASTER;
    }

    return TWS_ROLE_SLAVE;
}
#endif



bool bt_tws_is_paired()
{
    return gtws.state & BT_TWS_PAIRED;
}


u8 tws_get_sibling_addr(u8 *addr, int *result)
{
    u8 all_ff[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    int len = syscfg_read(CFG_TWS_REMOTE_ADDR, addr, 6);
    if (len != 6 || !memcmp(addr, all_ff, 6)) {
        *result = len;
        return -ENOENT;
    }
    return 0;
}


/*
 * 获取左右耳信息
 * 'L': 左耳
 * 'R': 右耳
 * 'U': 未知
 */
char bt_tws_get_local_channel()
{
    char channel = 'U';

    syscfg_read(CFG_TWS_CHANNEL, &channel, 1);

    return channel;
}

int get_bt_tws_connect_status()
{
    if (gtws.state & BT_TWS_SIBLING_CONNECTED) {
        return 1;
    }

    return 0;
}

static u8 set_channel_by_code_or_res(void)
{
    u8 count = 0;
    char channel = 0;
    char last_channel = 0;
#if (CONFIG_TWS_CHANNEL_SELECT == CONFIG_TWS_EXTERN_UP_AS_LEFT) || \
    (CONFIG_TWS_CHANNEL_SELECT == CONFIG_TWS_EXTERN_UP_AS_RIGHT)
    gpio_set_mode(IO_PORT_SPILT(CONFIG_TWS_CHANNEL_CHECK_IO), PORT_INPUT_PULLDOWN_10K);
    for (int i = 0; i < 5; i++) {
        os_time_dly(2);
        if (gpio_read(CONFIG_TWS_CHANNEL_CHECK_IO)) {
            count++;
        }
    }
    gpio_set_mode(IO_PORT_SPILT(CONFIG_TWS_CHANNEL_CHECK_IO), PORT_HIGHZ);
#if (CONFIG_TWS_CHANNEL_SELECT == CONFIG_TWS_EXTERN_UP_AS_LEFT)
    channel = (count >= 3) ? 'L' : 'R';
#else
    channel = (count >= 3) ? 'R' : 'L';
#endif
#elif (CONFIG_TWS_CHANNEL_SELECT == CONFIG_TWS_EXTERN_DOWN_AS_LEFT) || \
    (CONFIG_TWS_CHANNEL_SELECT == CONFIG_TWS_EXTERN_DOWN_AS_RIGHT)
    gpio_set_mode(IO_PORT_SPILT(CONFIG_TWS_CHANNEL_CHECK_IO), PORT_INPUT_PULLUP_10K);
    for (int i = 0; i < 5; i++) {
        os_time_dly(2);
        if (gpio_read(CONFIG_TWS_CHANNEL_CHECK_IO)) {
            count++;
        }
    }
    gpio_set_mode(IO_PORT_SPILT(CONFIG_TWS_CHANNEL_CHECK_IO), PORT_HIGHZ);
#if (CONFIG_TWS_CHANNEL_SELECT == CONFIG_TWS_EXTERN_DOWN_AS_LEFT)
    channel = (count >= 3) ? 'R' : 'L';
#else
    channel = (count >= 3) ? 'L' : 'R';
#endif
#elif (CONFIG_TWS_CHANNEL_SELECT == CONFIG_TWS_AS_LEFT)
    channel = 'L';
#elif (CONFIG_TWS_CHANNEL_SELECT == CONFIG_TWS_AS_RIGHT)
    channel = 'R';
#elif (CONFIG_TWS_CHANNEL_SELECT == CONFIG_TWS_CHANNEL_SELECT_BY_BOX)
    syscfg_read(CFG_CHARGESTORE_TWS_CHANNEL, &channel, 1);
#endif

#if CONFIG_TWS_SECECT_CHARGESTORE_PRIO
    syscfg_read(CFG_CHARGESTORE_TWS_CHANNEL, &channel, 1);
#endif

    if (channel) {
        syscfg_read(CFG_TWS_CHANNEL, &last_channel, 1);
        if (channel != last_channel) {
            syscfg_write(CFG_TWS_CHANNEL, &channel, 1);
        }
        tws_api_set_local_channel(channel);
        return 1;
    }
    return 0;
}
#if 0//a2dp播放根据信号强度主从切换重写函数，可改变范围
int a2dp_role_switch_check_rssi(char master_rssi, char slave_rssi)
{
    if (master_rssi < -58 && slave_rssi > -70 && master_rssi + 12 <= slave_rssi) {
        return 1;
    }
    return 0;
}
#endif

#if 0//a2dp播放根据信号强度主从切换重写函数，可改变范围
bool tws_esco_rs_rssi_check(char m_rssi, char s_rssi)
{
    static char old_s_rssi, old_m_rssi;

    if (((old_s_rssi - s_rssi) < 20 && (old_s_rssi - s_rssi) > -20) && ((old_m_rssi - m_rssi) < 20 && (old_m_rssi - m_rssi) > -20)) {
        if (m_rssi < -60 && s_rssi > -70 && m_rssi + 25 <= s_rssi) {
            return TRUE;
        }
    }
    old_s_rssi = s_rssi;
    old_m_rssi = m_rssi;

    return FALSE;
}
#endif

/*
 * 开机tws初始化
 */
int bt_tws_poweron()
{
    int err;
    u8 addr[6];
    char channel;

#if (TCFG_BT_BACKGROUND_ENABLE == 0)
    if (g_bt_hdl.wait_exit) { //非后台正在退出不进行初始化,底层资源已经释放
        return 0;
    }
#endif

#if TCFG_TEST_BOX_ENABLE
    if (testbox_get_ex_enter_dut_flag()) {
        log_info("tws poweron enter dut case\n");
        bt_cmd_prepare(USER_CTRL_WRITE_SCAN_ENABLE, 0, NULL);
        bt_cmd_prepare(USER_CTRL_WRITE_CONN_ENABLE, 0, NULL);
        return 0;
    }
#endif

    /*支持ANC训练快速连接，不连接tws*/
#if TCFG_ANC_BOX_ENABLE && TCFG_AUDIO_ANC_ENABLE
    if (ancbox_get_status()) {
        bt_cmd_prepare(USER_CTRL_WRITE_SCAN_ENABLE, 0, NULL);
        bt_cmd_prepare(USER_CTRL_WRITE_CONN_ENABLE, 0, NULL);
        return 0;
    }
#endif
    if (gtws.state) {
        return 0;
    }

#if TCFG_NORMAL_SET_DUT_MODE
    bt_cmd_prepare(USER_CTRL_WRITE_SCAN_ENABLE, 0, NULL);
    bt_cmd_prepare(USER_CTRL_WRITE_CONN_ENABLE, 0, NULL);
    return 0;
#endif

#if CONFIG_TWS_COMMON_ADDR_SELECT == CONFIG_TWS_COMMON_ADDR_USED_MASTER
    tws_api_common_addr_en(0);
#endif

    gtws.state = BT_TWS_POWER_ON;

    u16 pair_code = 0xAABB;
    syscfg_read(CFG_TWS_PAIR_CODE_ID, &pair_code, 2);
    tws_api_set_pair_code(pair_code);

#if TCFG_TWS_AUTO_ROLE_SWITCH_ENABLE
    tws_api_auto_role_switch_enable();
#else
    tws_api_auto_role_switch_disable();
#endif

    int result = 0;
    err = tws_get_sibling_addr(addr, &result);
    if (err == 0) {
        /* 获取到对方地址, 开始连接 */
        printf("\n---------have tws info----------\n");
        put_buf(addr, 6);
        gtws.state |= BT_TWS_PAIRED;

        tws_api_set_sibling_addr(addr);

        if (set_channel_by_code_or_res() == 0) {
            channel = bt_tws_get_local_channel();
            tws_api_set_local_channel(channel);
        }

#if TCFG_TEST_BOX_ENABLE
        if (testbox_get_status()) {
            return 0;
        }
#endif
        u8 conn_addr[6];
        bt_get_tws_local_addr(conn_addr);
        for (int i = 0; i < 6; i++) {
            conn_addr[i] += addr[i];
        }
        tws_api_set_quick_connect_addr(conn_addr);
#if TCFG_TWS_POWERON_AUTO_PAIR_ENABLE
        app_send_message(APP_MSG_TWS_PAIRED, 0);
#endif
    } else {
        printf("\n ---------no tws info----------\n");
        gtws.state |= BT_TWS_UNPAIRED;
        if (set_channel_by_code_or_res() == 0) {
            tws_api_set_local_channel('U');
        }
#if TCFG_TEST_BOX_ENABLE
        if (testbox_get_status()) {
            return 0;
        }
#endif
#if TCFG_TWS_POWERON_AUTO_PAIR_ENABLE
        app_send_message(APP_MSG_TWS_UNPAIRED, 0);
#endif
    }

    return 0;
}


/*
 * 手机开始连接
 */
void bt_tws_hci_event_connect()
{
    printf("bt_tws_hci_event_connect: %x\n", gtws.state);

    gtws.state &= ~BT_TWS_POWER_ON;
    sys_auto_shut_down_disable();
    tws_api_tx_unsniff_req();
}

int bt_tws_phone_connected()
{
    printf("bt_tws_phone_connected: %x\n", gtws.state);

#if TCFG_TEST_BOX_ENABLE
    if (testbox_get_status()) {
        return 1;
    }
#endif

    gtws.state |= BT_TWS_PHONE_CONNECTED;
    app_send_message(APP_MSG_BT_CLOSE_PAGE_SCAN, 0);
    return 0;
}

void bt_tws_phone_disconnected()
{
    gtws.state &= ~BT_TWS_PHONE_CONNECTED;
    printf("bt_tws_phone_disconnected: %x\n", gtws.state);
    app_send_message(APP_MSG_BT_OPEN_PAGE_SCAN, 0);
}

void bt_tws_phone_page_timeout()
{
    printf("bt_tws_phone_page_timeout: %x\n", gtws.state);
    bt_tws_phone_disconnected();
    app_send_message(APP_MSG_BT_OPEN_PAGE_SCAN, 0);
}

void bt_tws_phone_connect_timeout()
{
    log_d("bt_tws_phone_connect_timeout: %x\n", gtws.state);
    gtws.state &= ~BT_TWS_PHONE_CONNECTED;
    app_send_message(APP_MSG_BT_OPEN_PAGE_SCAN, 0);
}

void bt_get_vm_mac_addr(u8 *addr);

void bt_page_scan_for_test(u8 inquiry_en)
{
    u8 local_addr[6];

    log_info("\n\n\n\n -------------bt test page scan\n");

    tws_api_cancle_wait_pair();
    tws_api_cancle_create_connection();
    bt_cmd_prepare(USER_CTRL_PAGE_CANCEL, 0, NULL);

    tws_api_detach(TWS_DETACH_BY_POWEROFF, 5000);

    bt_cmd_prepare(USER_CTRL_POWER_OFF, 0, NULL);

    if (0 == bt_get_total_connect_dev()) {
        bt_get_vm_mac_addr(local_addr);
        lmp_hci_write_local_address(local_addr);
        if (inquiry_en) {
            bt_cmd_prepare(USER_CTRL_WRITE_SCAN_ENABLE, 0, NULL);
        }
        bt_cmd_prepare(USER_CTRL_WRITE_CONN_ENABLE, 0, NULL);
    }

    sys_auto_shut_down_disable();
    sys_auto_shut_down_enable();

    gtws.state = 0;
}

int bt_tws_poweroff()
{
    log_info("bt_tws_poweroff\n");
    if (adt_profile_support && get_edr_att_link_num() && tws_api_get_role() == TWS_ROLE_MASTER) {
        tws_edr_tx_att_data_to_peer();
    }

#if (THIRD_PARTY_PROTOCOLS_SEL & (RCSP_MODE_EN | GFPS_EN | MMA_EN | FMNA_EN | REALME_EN | SWIFT_PAIR_EN | DMA_EN | ONLINE_DEBUG_EN | CUSTOM_DEMO_EN | XIMALAYA_EN | AURACAST_APP_EN | RDX_EN)) && !TCFG_THIRD_PARTY_PROTOCOLS_SIMPLIFIED
    multi_protocol_bt_tws_poweroff_handler();
#endif

    if (tws_api_get_role() == TWS_ROLE_MASTER) {
        send_page_device_addr_2_sibling();
    }
    tws_api_detach(TWS_DETACH_BY_POWEROFF, 8000);

    tws_profile_exit();

    gtws.state = 0;

    if (tws_api_get_tws_state() & TWS_STA_SIBLING_DISCONNECTED) {
        return 1;
    }

    return 0;
}

void tws_page_scan_deal_by_esco(u8 esco_flag)
{
    if (gtws.state & BT_TWS_UNPAIRED) {
        return;
    }

    if (esco_flag) {
        gtws.state &= ~BT_TWS_CONNECT_SIBLING;
        tws_api_cancle_create_connection();
        tws_api_connect_in_esco();
        puts("close scan\n");
    }

    if (!esco_flag && !(gtws.state & BT_TWS_SIBLING_CONNECTED)) {
        puts("open scan22\n");
        tws_api_cancle_connect_in_esco();
        tws_dual_conn_state_handler();
    }
}

/*
 * 解除配对，清掉对方地址信息和本地声道信息
 */
void bt_tws_remove_pairs()
{
    u8 mac_addr[6];

    gtws.state &= ~BT_TWS_REMOVE_PAIRS;
    gtws.state &= ~BT_TWS_PAIRED;
    gtws.state |= BT_TWS_UNPAIRED;

    memset(mac_addr, 0xFF, 6);
    syscfg_write(CFG_TWS_COMMON_ADDR, mac_addr, 6);
    syscfg_write(CFG_TWS_REMOTE_ADDR, mac_addr, 6);
    syscfg_read(CFG_BT_MAC_ADDR, mac_addr, 6);
    lmp_hci_write_local_address(mac_addr);

#if (CONFIG_TWS_CHANNEL_SELECT == CONFIG_TWS_START_PAIR_AS_LEFT) ||\
    (CONFIG_TWS_CHANNEL_SELECT == CONFIG_TWS_START_PAIR_AS_RIGHT) ||\
    (CONFIG_TWS_CHANNEL_SELECT == CONFIG_TWS_MASTER_AS_LEFT) || \
    (CONFIG_TWS_CHANNEL_SELECT == CONFIG_TWS_MASTER_AS_RIGHT) && \
    CONFIG_TWS_SECECT_CHARGESTORE_PRIO
    char channel = 'U';
    char tws_channel = 0;
    syscfg_read(CFG_CHARGESTORE_TWS_CHANNEL, &tws_channel, 1);
    if ((tws_channel != 'L') && (tws_channel != 'R')) {
        syscfg_write(CFG_TWS_CHANNEL, &channel, 1);
        tws_api_set_local_channel(channel);
    }
#endif
}




#define TWS_FUNC_ID_VOL_SYNC    TWS_FUNC_ID('V', 'O', 'L', 'S')

static void bt_tws_vol_sync_in_task(int vol)
{
    app_audio_set_volume(APP_AUDIO_STATE_MUSIC, vol & 0xff, 1);
    app_audio_set_volume(APP_AUDIO_STATE_CALL, vol >> 8, 1);
    r_printf("vol_sync: %d, %d\n", vol & 0xff, vol >> 8);
}

static void bt_tws_vol_sync_in_irq(void *_data, u16 len, bool rx)
{
    u8 *data = (u8 *)_data;
    if (rx) {
        int msg[3];
        msg[0] = (int)bt_tws_vol_sync_in_task;
        msg[1] = 1;
        msg[2] = data[0] | (data[1] << 8);
        os_taskq_post_type("app_core", Q_CALLBACK, 3, msg);
    }
}
REGISTER_TWS_FUNC_STUB(app_vol_sync_stub) = {
    .func_id = TWS_FUNC_ID_VOL_SYNC,
    .func    = bt_tws_vol_sync_in_irq,
};

void bt_tws_sync_volume()
{
    u8 data[2];

    data[0] = app_audio_get_volume(APP_AUDIO_STATE_MUSIC);
    data[1] = app_audio_get_volume(APP_AUDIO_STATE_CALL);

    tws_api_send_data_to_slave(data, 2, TWS_FUNC_ID_VOL_SYNC);
}

#if TCFG_AUDIO_ANC_ENABLE
#define TWS_FUNC_ID_ANC_SYNC    TWS_FUNC_ID('A', 'N', 'C', 'S')
static void bt_tws_anc_sync(void *_data, u16 len, bool rx)
{
    if (rx) {
        u8 *data = (u8 *)_data;
        //r_printf("[slave]anc_sync: %d, %d\n", data[0], data[1]);
        /*先同步adt的状态，然后在切anc里面跑同步adt的动作*/
#if TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN
        audio_anc_icsd_adt_state_sync(data);
#else
        anc_mode_sync(data);
#endif /*TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN*/
    }
}

REGISTER_TWS_FUNC_STUB(app_anc_sync_stub) = {
    .func_id = TWS_FUNC_ID_ANC_SYNC,
    .func    = bt_tws_anc_sync,
};

void bt_tws_sync_anc(void)
{
    u8 data[5];
#if TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN
    /* 处理tws配对前一瞬间，在anc off开adt和
     * 进入免摘通透同步anc mode的情况*/
    data[0] = get_icsd_adt_anc_mode();
#else
    data[0] = anc_mode_get();
#endif /*TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN*/
#if ANC_EAR_ADAPTIVE_EN
    data[1] = anc_ear_adaptive_seq_get();
#endif/*ANC_EAR_ADAPTIVE_EN*/
#if ANC_MULT_ORDER_ENABLE
    data[2] = audio_anc_mult_scene_get();
#endif/*ANC_MULT_ORDER_ENABLE*/
#if TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN
    data[3] = get_icsd_adt_mode();
    data[4] = get_speak_to_chat_state();
#endif /*TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN*/
    //r_printf("[master]anc_sync: %d, %d\n", data[0], data[1]);
    tws_api_send_data_to_slave(data, 5, TWS_FUNC_ID_ANC_SYNC);
}

#endif /*TCFG_AUDIO_ANC_ENABLE*/

#if ((defined TCFG_AUDIO_SPATIAL_EFFECT_ENABLE) && TCFG_AUDIO_SPATIAL_EFFECT_ENABLE)
#include "spatial_effects_process.h"
#define TWS_FUNC_ID_SPATIAL_EFFECT_STATE_SYNC    TWS_FUNC_ID('A', 'S', 'E', 'S')
static void bt_tws_spatial_effect_state_sync(void *_data, u16 len, bool rx)
{
    if (rx) {
        u8 *data = (u8 *)_data;
        //r_printf("[slave]spatial_sync: %d, %d\n", data[0], data[1]);
        set_a2dp_spatial_audio_mode(data[0]);
    }
}

REGISTER_TWS_FUNC_STUB(app_spatial_effect_state_sync_stub) = {
    .func_id = TWS_FUNC_ID_SPATIAL_EFFECT_STATE_SYNC,
    .func    = bt_tws_spatial_effect_state_sync,
};

void bt_tws_sync_spatial_effect_state(void)
{
    u8 data[2];
    data[0] = get_a2dp_spatial_audio_mode();
    data[1] = 0;
    //r_printf("[master]spatial_sync: %d, %d\n", data[0], data[1]);
    tws_api_send_data_to_slave(data, 2, TWS_FUNC_ID_SPATIAL_EFFECT_STATE_SYNC);
}
#endif

static void bt_tws_tx_jiffies_offset()
{
    int clkn = (jiffies_msec() + log_get_time_offset()) / 5 * 8;
    int offset = tws_api_get_mclkn() - clkn;
    tws_api_send_data_to_sibling(&offset, 4, 0x1E782CEB);
}

static void bt_tws_jiffies_sync(void *_data, u16 len, bool rx)
{
    if (!rx) {
        return;
    }

    int offset = *(int *)_data;
    int a_jiffies = (tws_api_get_mclkn() - offset) / 8 * 5;
    int b_jiffies = jiffies_msec() + log_get_time_offset();

    if (time_after(a_jiffies, b_jiffies)) {
        log_set_time_offset(a_jiffies - jiffies_msec());
    } else {
        bt_tws_tx_jiffies_offset();
    }
}
REGISTER_TWS_FUNC_STUB(jiffies_sync_stub) = {
    .func_id = 0x1E782CEB,
    .func    = bt_tws_jiffies_sync,
};

int tws_host_get_phone_income_state()
{
    if (bt_get_call_status() == BT_CALL_INCOMING) {
        return 1;
    }
    return 0;
}
/*
 * tws事件状态处理函数
 */
int bt_tws_connction_status_event_handler(int *msg)
{
    struct tws_event *evt = (struct tws_event *)msg;
    u8 addr[4][6];
    int pair_suss = 0;
    int role = evt->args[0];
    int phone_link_connection = evt->args[1];
    int reason = evt->args[2];
    u16 random_num = 0;
    char channel = 0;
    u8 mac_addr[6];
    int work_state = 0;

    log_info("tws-user: role= %d, phone_link_connection %d, reason=%d,event= %d\n",
             role, phone_link_connection, reason, evt->event);

    switch (evt->event) {
    case TWS_EVENT_CONNECTED:
        log_info("tws_event_pair_suss: %x\n", gtws.state);

        g_bt_hdl.phone_ring_sync_tws = 0;
        syscfg_read(CFG_TWS_REMOTE_ADDR, addr[0], 6);
        syscfg_read(CFG_TWS_COMMON_ADDR, addr[1], 6);
        tws_api_get_sibling_addr(addr[2]);
        tws_api_get_local_addr(addr[3]);

        /* 记录对方地址 */
        if (memcmp(addr[0], addr[2], 6)) {
            syscfg_write(CFG_TWS_REMOTE_ADDR, addr[2], 6);
            pair_suss = 1;
            log_info("rec tws addr\n");
            put_buf(addr[2], 6);
        }
        u8 comm_mac_addr_memcmp = 1;
#if ((TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_UNICAST_SINK_EN | LE_AUDIO_JL_UNICAST_SINK_EN)))
        u8 comm_mac_addr[12];
        int ret = syscfg_read(CFG_TWS_COMMON_ADDR, comm_mac_addr, 12);
        if (ret == 12) {
            /* r_printf("comm_mac_addr_memcmp\n"); */
            /* put_buf(comm_mac_addr, 12); */
            /* put_buf(addr[3], 6); */
            if (memcmp(&comm_mac_addr[6], addr[3], 6) == 0) {
                comm_mac_addr_memcmp = 0;
                lmp_hci_write_local_address(comm_mac_addr);
                g_printf("reset lmp_hci_write_local_address\n");
            }

        }
#endif
        if (comm_mac_addr_memcmp && memcmp(addr[1], addr[3], 6)) {
            syscfg_write(CFG_TWS_COMMON_ADDR, addr[3], 6);
            pair_suss = 1;
            log_info("rec comm addr\n");
            put_buf(addr[3], 6);
        }
        /* 记录左右声道 */
        channel = tws_api_get_local_channel();
        if (channel != bt_tws_get_local_channel()) {
            syscfg_write(CFG_TWS_CHANNEL, &channel, 1);
        }
        r_printf("tws_local_channel: %c\n", channel);

        u8 conn_addr[6];
        bt_get_tws_local_addr(conn_addr);
        for (int i = 0; i < 6; i++) {
            conn_addr[i] += addr[2][i];
        }
        tws_api_set_quick_connect_addr(conn_addr);


        if (pair_suss) {
            gtws.state = BT_TWS_PAIRED;
#if CONFIG_TWS_COMMON_ADDR_SELECT != CONFIG_TWS_COMMON_ADDR_USED_MASTER
            bt_update_mac_addr((void *)addr[3]);
#endif
            app_send_message(APP_MSG_TWS_PAIR_SUSS, 0);
        } else {
            app_send_message(APP_MSG_TWS_CONNECTED, 0);
        }
        EARPHONE_STATE_TWS_CONNECTED(pair_suss, addr[3]);


        if (role == TWS_ROLE_MASTER) {
            bt_tws_tx_jiffies_offset();
        }
        if (reason & (TWS_STA_ESCO_OPEN | TWS_STA_SBC_OPEN)) {
            if (role == TWS_ROLE_SLAVE) {
                gtws.state |= BT_TWS_AUDIO_PLAYING;
            }
        }

        gtws.state &= ~BT_TWS_TIMEOUT;
        gtws.state |= BT_TWS_SIBLING_CONNECTED;

#if TCFG_AUDIO_ANC_ENABLE
        bt_tws_sync_anc();
#endif

#if ((defined TCFG_AUDIO_SPATIAL_EFFECT_ENABLE) && TCFG_AUDIO_SPATIAL_EFFECT_ENABLE)
        bt_tws_sync_spatial_effect_state();
#endif

        tws_sync_bat_level(); //同步电量到对耳
#if ((TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_UNICAST_SINK_EN | LE_AUDIO_JL_UNICAST_SINK_EN)))
        u32 slave_info =  evt->args[3] | (evt->args[4] << 8) | (evt->args[5] << 16) | (evt->args[6] << 24) ;
        printf("====slave_info:%x\n", slave_info);
        if (!(slave_info & TWS_STA_LE_AUDIO_PLAYING)) {
            //only set to slave while slave not playing
            bt_tws_sync_volume();
        }
#else
        bt_tws_sync_volume();
#endif
        tws_sync_dual_conn_info();

#if TCFG_EAR_DETECT_ENABLE
        tws_sync_ear_detect_state(0);
#endif

        tws_sniff_controle_check_enable();

        break;
    case TWS_EVENT_CONNECTION_DETACH:
        /*
         * TWS连接断开
         */
        if (app_var.goto_poweroff_flag) {
            break;
        }
        g_bt_hdl.phone_ring_sync_tws = 0;
        work_state = evt->args[3];
        log_info("tws_event_connection_detach: state: %x,%x\n", gtws.state, work_state);
#if CONFIG_TWS_COMMON_ADDR_SELECT == CONFIG_TWS_COMMON_ADDR_USED_MASTER
        lmp_hci_write_local_address(bt_get_mac_addr());
#endif

        app_power_set_tws_sibling_bat_level(0xff, 0xff);

#if TCFG_TEST_BOX_ENABLE
        if (testbox_get_status()) {
            break;
        }
#endif

        if (phone_link_connection) {
            //对耳断开后如果手机还连着，主动推一次电量给手机
            batmgr_send_msg(POWER_EVENT_POWER_CHANGE, 0);
            bt_cmd_prepare(USER_CTRL_HFP_CMD_UPDATE_BATTARY, 0, NULL);
        }

        tws_sniff_controle_check_disable();

        gtws.state &= ~BT_TWS_SIBLING_CONNECTED;

        if (reason == TWS_DETACH_BY_REMOVE_PAIRS) {
            gtws.state = BT_TWS_UNPAIRED;
            puts("<<<< tws detach by remove pairs >>>>\n");
            break;
        }

        if (bt_get_esco_coder_busy_flag()) {
            tws_api_connect_in_esco();
            break;
        }

        //非测试盒在仓测试，直连蓝牙
        if (reason & TWS_DETACH_BY_TESTBOX_CON) {
            puts("<<<<< TWS_DETACH_BY_TESTBOX_CON >>>>n");
            gtws.state &= ~BT_TWS_PAIRED;
            gtws.state |= BT_TWS_UNPAIRED;
            if (!phone_link_connection) {
                get_random_number(mac_addr, 6);
                lmp_hci_write_local_address(mac_addr);
            }
            break;
        }
        break;
    case TWS_EVENT_PHONE_LINK_DETACH:
        /*
         * 跟手机的链路LMP层已完全断开, 只有tws在连接状态才会收到此事件
         */
        printf("tws_event_phone_link_detach: %x\n", gtws.state);
        if (app_var.goto_poweroff_flag) {
            break;
        }

#if TCFG_TEST_BOX_ENABLE
        if (testbox_get_status()) {
            break;
        }
#endif
        tws_sniff_controle_check_enable();
        break;
    case TWS_EVENT_REMOVE_PAIRS:
        log_info("tws_event_remove_pairs\n");
        bt_tws_remove_pairs();
        app_power_set_tws_sibling_bat_level(0xff, 0xff);
        break;
    case TWS_EVENT_ROLE_SWITCH:
        r_printf("TWS_EVENT_ROLE_SWITCH=%d\n", role);
        u8 *esco_addr = lmp_get_esco_link_addr();
        if (esco_addr) {
            bt_phone_esco_play(esco_addr);
        }
#if TCFG_TWS_POWER_BALANCE_ENABLE
        if (role == TWS_ROLE_SLAVE) {
            esco_recoder_switch(0);
        } else {
            esco_recoder_switch(1);
        }

#endif
        if (!(tws_api_get_tws_state() & TWS_STA_PHONE_CONNECTED)) {
            if (role == TWS_ROLE_MASTER) {
                os_time_dly(2);
                tws_sniff_controle_check_enable();
            }
        }
        EARPHONE_STATE_ROLE_SWITCH(role);
        break;
    case TWS_EVENT_ESCO_ROLE_SWITCH_START:
        r_printf("TWS_EVENT_ESCO_ROLE_SWITCH_START=%d\n", role);
        u8 *esco_addr1 = lmp_get_esco_link_addr();
        if (esco_addr1) {
            bt_phone_esco_play(esco_addr1);
        }
#if TCFG_TWS_POWER_BALANCE_ENABLE
        if (role == TWS_ROLE_SLAVE) {
            esco_recoder_switch(1);
        }

#endif
        break;
    case TWS_EVENT_ESCO_ADD_CONNECT:
        bt_tws_sync_volume();
        break;

    case TWS_EVENT_MODE_CHANGE:
        log_info("TWS_EVENT_MODE_CHANGE : %d", evt->args[0]);
        if (evt->args[0] == 0) {
            tws_sniff_controle_check_enable();
        }
        break;
    case TWS_EVENT_TONE_TEST:
        log_info("TWS_EVENT_TEST : %d", evt->args[0]);
        /*play_tone_file(get_tone_files()->num[evt->args[0]]);*/
        break;
    }

    a2dp_player_tws_event_handler((int *)evt);
    return 0;
}
APP_MSG_HANDLER(tws_msg_handler) = {
    .owner      = APP_MODE_BT,
    .from       = MSG_FROM_TWS,
    .handler    = bt_tws_connction_status_event_handler,
};


void tws_cancle_all_noconn()
{
    tws_api_cancle_wait_pair();
    tws_api_cancle_create_connection();
    bt_cmd_prepare(USER_CTRL_PAGE_CANCEL, 0, NULL);
}


bool get_tws_sibling_connect_state(void)
{
    if (gtws.state & BT_TWS_SIBLING_CONNECTED) {
        return TRUE;
    }
    return FALSE;
}

#define TWS_SNIFF_CNT_TIME      5000    //ms

static void bt_tws_enter_sniff(void *parm)
{
    int interval;
#if ((TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_UNICAST_SINK_EN | LE_AUDIO_JL_UNICAST_SINK_EN)))
    if (is_cig_phone_conn() || is_cig_other_phone_conn()) {
        goto __exit;
    }
#endif
#if (TCFG_LE_AUDIO_APP_CONFIG & LE_AUDIO_AURACAST_SINK_EN)
    if (check_local_not_accept_sniff_by_remote()) {
        goto __exit;
    }
#endif

    int state = tws_api_get_tws_state();
    if (state & TWS_STA_PHONE_DISCONNECTED) {
        interval = 400;
    } else if (state & TWS_STA_PHONE_SNIFF) {
        interval = 800;
    } else {
        goto __exit;
    }

    int err = tws_api_tx_sniff_req(interval, 12);
    if (err == 0) {
        sys_timer_del(gtws.sniff_timer);
        gtws.sniff_timer = 0;
        return;
    }

__exit:
    sys_timer_modify(gtws.sniff_timer, TWS_SNIFF_CNT_TIME);
}

void tws_sniff_controle_check_reset(void)
{
    if (gtws.sniff_timer) {
        sys_timer_modify(gtws.sniff_timer, TWS_SNIFF_CNT_TIME);
    }
}

void tws_sniff_controle_check_enable(void)
{
#if (CONFIG_BT_TWS_SNIFF == 0)
    return;
#endif

    if (update_check_sniff_en() == 0) {
        return;
    }

    if (gtws.sniff_timer == 0) {
        gtws.sniff_timer = sys_timer_add(NULL, bt_tws_enter_sniff, TWS_SNIFF_CNT_TIME);
    }
    printf("tws_sniff_check_enable\n");
}

void tws_sniff_controle_check_disable(void)
{
#if (CONFIG_BT_TWS_SNIFF == 0)
    return;
#endif

    if (gtws.sniff_timer) {
        sys_timer_del(gtws.sniff_timer);
        gtws.sniff_timer = 0;
    }
    puts("tws_sniff_check_disable\n");
}

static u16 ble_try_switch_timer;
void tws_phone_ble_link_try_switch(void *p)
{
    //等待退出tws sniff
    if (!tws_in_sniff_state()) {
        //开始启动ble controller检查,满足切换条件进行链路切换;
        sys_timeout_del(ble_try_switch_timer);
        ble_try_switch_timer = 0;
    }
}

void bt_tws_ble_link_switch(void)
{
#if CONFIG_BT_TWS_SNIFF
    tws_sniff_controle_check_disable();
    tws_api_tx_unsniff_req();
#endif
    ble_try_switch_timer = sys_timer_add(NULL, tws_phone_ble_link_try_switch, 20);
}

//未连接手机时的TWS主从切换
void bt_tws_role_switch()
{
    if (!(gtws.state & BT_TWS_SIBLING_CONNECTED)) {
        return;
    }
    if (tws_api_get_role() == TWS_ROLE_SLAVE) {
        return;
    }
    puts("bt_tws_role_switch\n");
    tws_api_cancle_wait_pair();
#if CONFIG_BT_TWS_SNIFF
    tws_sniff_controle_check_disable();
    tws_api_tx_unsniff_req();
    for (int i = 0; i < 20; i++) {
        if (!tws_in_sniff_state()) {
            tws_api_role_switch();
            break;
        }
        os_time_dly(4);
    }
#else
    tws_api_role_switch();
#endif
}

static int bt_tws_msg_handler_to_ble(int *msg)
{
    return TWS_EVENT_MASSAGE_HANDLER(msg);
}

APP_MSG_HANDLER(tws_msg_handler_user_ble) = {
    .owner      = 0xff,
    .from       = MSG_FROM_TWS,
    .handler    = bt_tws_msg_handler_to_ble,
};

#else

int bt_tws_poweroff()
{
    return FALSE;
}

void bt_tws_play_tone_at_same_time(int tone_name, int msec)
{
}

void bt_tws_connect_sibling(int timeout)
{

}

void bt_tws_sync_volume()
{

}

bool get_tws_sibling_connect_state(void)
{
    return FALSE;
}

bool bt_tws_is_paired()
{
    return 0 ;
}

u8 get_bt_tws_discon_dly_state()
{
    return 0;
}
int bt_tws_phone_connected()
{
    return 0;
}
void bt_tws_phone_disconnected()
{
}
void bt_tws_phone_connect_timeout()
{

}

int bt_tws_sync_phone_num(void *priv)
{
    return 0;
}

void tws_page_scan_deal_by_esco(u8 esco_flag)
{

}
void bt_tws_hci_event_connect()
{

}
static int bt_tws_connction_status_event_handler(int *priv)
{
    return 0;
}

void tws_cancle_all_noconn()
{

}

void bt_tws_phone_page_timeout()
{

}

#endif
