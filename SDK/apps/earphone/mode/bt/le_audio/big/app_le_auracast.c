#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".app_le_auracast.data.bss")
#pragma data_seg(".app_le_auracast.data")
#pragma const_seg(".app_le_auracast.text.const")
#pragma code_seg(".app_le_auracast.text")
#endif
#include "system/includes.h"
#include "app_config.h"
#include "app_main.h"
#include "audio_config.h"
#include "a2dp_player.h"
#include "esco_player.h"
#include "btstack/le/auracast_sink_api.h"
#include "le_audio_player.h"
#include "app_le_auracast.h"
#include "auracast_app_protocol.h"
#include "a2dp_media_codec.h"
#include "le/le_user.h"
#if TCFG_USER_TWS_ENABLE
#include "classic/tws_api.h"
#endif

#if (TCFG_LE_AUDIO_APP_CONFIG & LE_AUDIO_AURACAST_SINK_EN)

struct broadcast_rx_audio_hdl {
    void *le_audio;
    void *rx_stream;
};
typedef struct {
    u16 bis_hdl;
    void *recorder;
    struct broadcast_rx_audio_hdl rx_player;
} bis_hdl_info_t ;
struct auracast_audio {
    uint8_t bis_num;
    bis_hdl_info_t audio_hdl[MAX_NUM_BIS];
};
struct auracast_audio g_rx_audio;
static u16 auracast_sink_sync_timeout_hdl = 0;
u8 a2dp_auracast_preempted_addr[6];
static auracast_sink_source_info_t *cur_listening_source_info = NULL;						// 当前正在监听广播设备播歌的信息
#if TCFG_USER_TWS_ENABLE
static u8 g_cur_auracast_is_scanning = 0;											// 当前设备是否正在扫描广播设备，0:否，非零:是
#endif

BROADCAST_STATUS le_auracast_status_get()
{
    auracast_sink_big_state_t big_state = auracast_sink_big_state_get();
    printf("le_auracast_state=%d\n", big_state);
    if (big_state > SINK_BIG_STATE_IDLE) {
        return 1;
    }
    return 0;
}

static int app_auracast_app_notify_listening_status(u8 status, u8 error)
{
#if (THIRD_PARTY_PROTOCOLS_SEL & AURACAST_APP_EN)
    return auracast_app_notify_listening_status(status, error);
#else
    return 0;
#endif
}

void le_auracast_stop(void)
{
    printf("le_auracast_stop\n");
    int ret = app_auracast_sink_big_sync_terminate();
    if (ret == 0) {
        app_auracast_app_notify_listening_status(0, 0);
    }
}

static void le_auracast_audio_open(uint8_t *packet, uint16_t length)
{
    int err = 0;
    auracast_sink_source_info_t *config = (auracast_sink_source_info_t *)packet;
    ASSERT(config, "config is NULL");
    printf("audio open\n");
    //audio open
    if (config->Num_BIS > MAX_NUM_BIS) {
        g_rx_audio.bis_num = MAX_NUM_BIS;
    } else {
        g_rx_audio.bis_num = config->Num_BIS;
    }
    for (u8 i = 0; i < g_rx_audio.bis_num; i++) {
        g_rx_audio.audio_hdl[i].bis_hdl = config->Connection_Handle[i];
    }
    struct le_audio_stream_params params;
    params.fmt.nch = LE_AUDIO_CODEC_CHANNEL;
    params.fmt.coding_type = LE_AUDIO_CODEC_TYPE;
    params.fmt.frame_dms = LE_AUDIO_CODEC_FRAME_LEN;
    params.fmt.sdu_period = config->sdu_period;
    params.fmt.isoIntervalUs = config->sdu_period;
    params.fmt.sample_rate = config->sample_rate;
    params.fmt.dec_ch_mode = LEA_DEC_OUTPUT_CHANNEL;
    params.fmt.bit_rate = config->bit_rate;
    for (u8 i = 0; i < g_rx_audio.bis_num; i++) {
        params.conn =  g_rx_audio.audio_hdl[i].bis_hdl; //使用当前路的bis handle
        g_rx_audio.audio_hdl[i].rx_player.le_audio = le_audio_stream_create(params.conn, &params.fmt);// params.reference_time);
        g_rx_audio.audio_hdl[i].rx_player.rx_stream = le_audio_stream_rx_open(g_rx_audio.audio_hdl[i].rx_player.le_audio, params.fmt.coding_type);
        err = le_audio_player_open(g_rx_audio.audio_hdl[i].rx_player.le_audio, &params);
        if (err != 0) {
            ASSERT(0, "player open fail");
        }
    }
}

static void le_auracast_iso_rx_callback(uint8_t *packet, uint16_t size)
{
    hci_iso_hdr_t hdr = {0};
    ll_iso_unpack_hdr(packet, &hdr);
    if ((hdr.pb_flag == 0b10) && (hdr.iso_sdu_length == 0)) {
        if (hdr.packet_status_flag == 0b00) {
            putchar('m');
            return;
            /* log_error("SDU empty"); */
        } else {
            putchar('s');
            return;
            /* log_error("SDU lost"); */
        }
    }
    if (((hdr.pb_flag == 0b10) || (hdr.pb_flag == 0b00)) && (hdr.packet_status_flag == 0b01)) {
        putchar('p');
        return;
        //log_error("SDU invalid, len=%d", hdr.iso_sdu_length);
    }

    /* put_buf(hdr.iso_sdu, 4); */
    for (u8 i = 0; i < g_rx_audio.bis_num; i++) {
        if ((NULL != g_rx_audio.audio_hdl[i].rx_player.le_audio) \
            && (NULL != g_rx_audio.audio_hdl[i].rx_player.rx_stream)) {
            le_audio_stream_rx_frame(g_rx_audio.audio_hdl[i].rx_player.rx_stream, (void *)hdr.iso_sdu, hdr.iso_sdu_length, hdr.time_stamp);
        }
    }
}

static void le_auracast_audio_close(void)
{
    printf("audio close\n");
    for (u8 i = 0; i < g_rx_audio.bis_num; i++) {
        if (g_rx_audio.audio_hdl[i].rx_player.le_audio && g_rx_audio.audio_hdl[i].rx_player.rx_stream) {
            le_audio_player_close(g_rx_audio.audio_hdl[i].rx_player.le_audio);
            le_audio_stream_rx_close(g_rx_audio.audio_hdl[i].rx_player.rx_stream);
            le_audio_stream_free(g_rx_audio.audio_hdl[i].rx_player.le_audio);
            g_rx_audio.audio_hdl[i].rx_player.le_audio = NULL;
            g_rx_audio.audio_hdl[i].rx_player.rx_stream = NULL;
        }
    }
}

/**
 * @brief 设备收到广播设备信息汇报给手机APP
 */
void auracast_sink_source_info_report_event_deal(uint8_t *packet, uint16_t length)
{
#if (THIRD_PARTY_PROTOCOLS_SEL & AURACAST_APP_EN)
    struct auracst_source_item_t src = {0};
    auracast_sink_source_info_t *param = (auracast_sink_source_info_t *)packet;

    u8 name_len = strlen((const char *)param->broadcast_name);
    if (0 != name_len) {
        memcpy(src.broadcast_name, param->broadcast_name, name_len);
    }
    memcpy(src.adv_address, param->source_mac_addr, 6);
    src.broadcast_features = param->feature;
    src.broadcast_id[0] = param->broadcast_id & 0xFF;
    src.broadcast_id[1] = (param->broadcast_id >> 8) & 0xFF;
    src.broadcast_id[2] = (param->broadcast_id >> 16) & 0xFF;
    auracast_app_notify_source_list(&src);
#endif
}

static void auracast_sink_event_callback(uint16_t event, uint8_t *packet, uint16_t length)
{
    // 这里监听后会一直打印
    /* printf("auracast_sink_event_callback:%d\n", event); */
    switch (event) {
    case AURACAST_SINK_SOURCE_INFO_REPORT_EVENT:
        printf("AURACAST_SINK_SOURCE_INFO_REPORT_EVENT\n");
        auracast_sink_source_info_report_event_deal(packet, length);
        break;
    case AURACAST_SINK_BLE_CONNECT_EVENT:
        printf("AURACAST_SINK_BLE_CONNECT_EVENT\n");
        break;
    case AURACAST_SINK_BIG_SYNC_CREATE_EVENT:
        printf("AURACAST_SINK_BIG_SYNC_CREATE_EVENT\n");

        ASSERT(cur_listening_source_info);
        memcpy(cur_listening_source_info, auracast_sink_listening_source_info_get(), sizeof(auracast_sink_source_info_t));

        if (auracast_sink_sync_timeout_hdl != 0) {
            sys_timeout_del(auracast_sink_sync_timeout_hdl);
            auracast_sink_sync_timeout_hdl = 0;
        }
        le_auracast_audio_open(packet, length);
        app_auracast_app_notify_listening_status(2, 0);
        break;
    case AURACAST_SINK_BIG_SYNC_TERMINATE_EVENT:
        printf("AURACAST_SINK_BIG_SYNC_TERMINATE_EVENT\n");
        break;
    case AURACAST_SINK_BIG_SYNC_FAIL_EVENT:
    case AURACAST_SINK_BIG_SYNC_LOST_EVENT:
        printf("big lost or fail\n");
        //auracast_sink_big_sync_terminate();
        le_auracast_audio_close();
        app_auracast_app_notify_listening_status(0, 2);
        break;
    case AURACAST_SINK_PERIODIC_ADVERTISING_SYNC_LOST_EVENT:
        if (cur_listening_source_info) {
            printf("AURACAST_SINK_PERIODIC_ADVERTISING_SYNC_LOST_EVENT\n");
            auracast_sink_big_sync_create(cur_listening_source_info);
        } else {
            printf("AURACAST_SINK_PERIODIC_ADVERTISING_SYNC_LOST_EVENT FAIL!\n");
        }
        break;
    case AURACAST_SINK_BIG_INFO_REPORT_EVENT:
        printf("AURACAST_SINK_BIG_INFO_REPORT_EVENT\n");
        break;
    case AURACAST_SINK_ISO_RX_CALLBACK_EVENT:
        //printf("AURACAST_SINK_ISO_RX_CALLBACK_EVENT\n");
        le_auracast_iso_rx_callback(packet, length);
        break;
    case AURACAST_SINK_EXT_SCAN_COMPLETE_EVENT:
        printf("AURACAST_SINK_EXT_SCAN_COMPLETE_EVENT\n");
        break;
    default:
        break;
    }
}

int app_auracast_bass_server_event_callback(uint8_t event, uint8_t *packet, uint16_t size)
{
    struct le_audio_bass_add_source_info_t *bass_source_info = (struct le_audio_bass_add_source_info_t *)packet;
    auracast_sink_source_info_t add_source_param = {0};
    switch (event) {
    case BASS_SERVER_EVENT_SCAN_STOPPED:
        break;
    case BASS_SERVER_EVENT_SCAN_STARTED:
        break;
    case BASS_SERVER_EVENT_SOURCE_ADDED:
        printf("BASS_SERVER_EVENT_SOURCE_ADDED\n");
        if ((bass_source_info->pa_sync == BASS_PA_SYNC_SYNCHRONIZE_TO_PA_PAST_AVAILABLE) \
            || (bass_source_info->pa_sync == BASS_PA_SYNC_SYNCHRONIZE_TO_PA_PAST_NOT_AVAILABLE)) {
            memcpy(add_source_param.source_mac_addr, bass_source_info->address, 6);
            bass_source_info->bis_sync_state = 0x03;
            app_auracast_sink_big_sync_create(&add_source_param);
        }
        break;
    case BASS_SERVER_EVENT_SOURCE_MODIFIED:
        printf("BASS_SERVER_EVENT_SOURCE_MODIFIED\n");
        if ((bass_source_info->pa_sync == BASS_PA_SYNC_SYNCHRONIZE_TO_PA_PAST_AVAILABLE) \
            || (bass_source_info->pa_sync == BASS_PA_SYNC_SYNCHRONIZE_TO_PA_PAST_NOT_AVAILABLE)) {
            app_auracast_sink_big_sync_terminate();
            memcpy(add_source_param.source_mac_addr, bass_source_info->address, 6);
            app_auracast_sink_big_sync_create(&add_source_param);
        } else {
            app_auracast_sink_big_sync_terminate();
        }
        break;
    case BASS_SERVER_EVENT_SOURCE_DELETED:
        break;
    }
    return 0;
}

void app_auracast_sink_init(void)
{
    printf("app_auracast_sink_init\n");
    auracast_sink_init();
    auracast_sink_event_callback_register(auracast_sink_event_callback);

    le_audio_bass_event_callback_register(app_auracast_bass_server_event_callback);
}

static int __app_auracast_sink_big_sync_terminate(void)
{
    if (cur_listening_source_info) {
        free(cur_listening_source_info);
        cur_listening_source_info = NULL;
    }
    int ret = auracast_sink_big_sync_terminate();
    if (0 == ret) {
        le_auracast_audio_close();
        if (auracast_sink_sync_timeout_hdl != 0) {
            sys_timeout_del(auracast_sink_sync_timeout_hdl);
            auracast_sink_sync_timeout_hdl = 0;
        }
    }
    return ret;
}

/**
 * @brief 关闭所有正在监听播歌的广播设备
 */
int app_auracast_sink_big_sync_terminate(void)
{
    printf("app_auracast_sink_big_sync_terminate\n");
#if TCFG_USER_TWS_ENABLE
    int ret = 0;
    if (!(tws_api_get_tws_state() & TWS_STA_SIBLING_CONNECTED)) {
        ret = __app_auracast_sink_big_sync_terminate();
    } else {
        tws_api_send_data_to_sibling(NULL, 0, 0x23482C5C);
        ret = 0;
    }
#else
    int ret = __app_auracast_sink_big_sync_terminate();
#endif
    return ret;
}

static int __app_auracast_sink_scan_start(void)
{
    int ret = auracast_sink_scan_start();
    printf("auracast_sink_scan_start ret:%d\n", ret);
    return ret;
}

/**
 * @brief 手机通知设备开始搜索auracast广播
 */
int app_auracast_sink_scan_start(void)
{
    printf("app_auracast_sink_scan_start\n");
#if TCFG_USER_TWS_ENABLE
    g_cur_auracast_is_scanning = 1;
    return tws_api_send_data_to_sibling((void *)&g_cur_auracast_is_scanning, sizeof(u8), 0x23482C5D);
#else
    return __app_auracast_sink_scan_start();
#endif
}

static int __app_auracast_sink_scan_stop(void)
{
    int ret = auracast_sink_scan_stop();
    printf("auracast_sink_scan_stop ret:%d\n", ret);
    return ret;
}

/**
 * @brief 手机通知设备关闭搜索auracast广播
 */
int app_auracast_sink_scan_stop(void)
{
    printf("app_auracast_sink_scan_stop\n");
#if TCFG_USER_TWS_ENABLE
    g_cur_auracast_is_scanning = 0;
    return tws_api_send_data_to_sibling((void *)&g_cur_auracast_is_scanning, sizeof(u8), 0x23482C5D);
#else
    return __app_auracast_sink_scan_stop();
#endif
}

static void auracast_sink_sync_timeout_handler(void *priv)
{
    printf("auracast_sink_sync_timeout_handler\n");
    auracast_sink_scan_stop();
    auracast_sink_big_sync_terminate();
    app_auracast_app_notify_listening_status(0, 2);
    auracast_sink_sync_timeout_hdl = 0;
}

static int __app_auracast_sink_big_sync_create(auracast_sink_source_info_t *param, u8 tws_malloc)
{
    if (cur_listening_source_info == NULL) {
        cur_listening_source_info = malloc(sizeof(auracast_sink_source_info_t));
    } else {
        printf("cur_listening_source_info already malloc!\n");
        ASSERT(0);
        return -1;
    }
    memcpy(cur_listening_source_info, param, sizeof(auracast_sink_source_info_t));
    int ret = auracast_sink_big_sync_create(cur_listening_source_info);
    if (0 == ret) {
        auracast_sink_sync_timeout_hdl = sys_timeout_add(NULL, auracast_sink_sync_timeout_handler, 15000);
    } else {
        printf("__app_auracast_sink_big_sync_create ret:%d\n", ret);
    }
    if (tws_malloc) {
        free(param);
    }

    return ret;
}

/**
 * @brief 手机选中广播设备开始播歌
 *
 * @param param 要监听的广播设备
 */
int app_auracast_sink_big_sync_create(auracast_sink_source_info_t *param)
{
    printf("app_auracast_sink_big_sync_create\n");
    u8 bt_addr[6];
    if (auracast_sink_sync_timeout_hdl != 0) {
        printf("auracast_sink_sync_timeout_hdl is not null\n");
        return -1;
    }

    if (esco_player_runing()) {
        printf("app_auracast_sink_big_sync_create esco_player_runing\n");
        // 暂停auracast的播歌
        return -1;
    }

    if (a2dp_player_get_btaddr(bt_addr)) {
#if TCFG_A2DP_PREEMPTED_ENABLE
        memcpy(a2dp_auracast_preempted_addr, bt_addr, 6);
        a2dp_player_close(bt_addr);
        a2dp_media_mute(bt_addr);
        void *device = btstack_get_conn_device(bt_addr);
        if (device) {
            btstack_device_control(device, USER_CTRL_AVCTP_OPID_PAUSE);
        }
#else
        // 不抢播, 暂停auracast的播歌
        return -1;
#endif
    }

    if (cur_listening_source_info) {
        free(cur_listening_source_info);
        cur_listening_source_info = NULL;
    }
#if TCFG_USER_TWS_ENABLE
    int ret = 0;
    if (!(tws_api_get_tws_state() & TWS_STA_SIBLING_CONNECTED)) {
        ret = __app_auracast_sink_big_sync_create(param, 0);
    } else {
        tws_api_send_data_to_sibling(param, sizeof(auracast_sink_source_info_t), 0x23482C5B);
        ret = 0;
    }
#else
    int ret = __app_auracast_sink_big_sync_create(param, 0);
#endif

    return ret;
}

static int le_auracast_app_msg_handler(int *msg)
{
    printf("le_auracast_app_msg_handler:%d\n", msg[0]);
    switch (msg[0]) {
    case APP_MSG_STATUS_INIT_OK:
        printf("APP_MSG_STATUS_INIT_OK");

#if !(TCFG_LE_AUDIO_APP_CONFIG & LE_AUDIO_UNICAST_SINK_EN)
        le_audio_init(3);
        bt_le_audio_adv_enable(1);
#endif

        app_auracast_sink_init();
        break;
    case APP_MSG_POWER_OFF://1
        //le_audio_uninit(3);
        printf("APP_MSG_POWER_OFF");
        break;
    default:
        break;
    }
    return 0;
}

APP_MSG_HANDLER(le_auracast_app_msg_entry) = {
    .owner      = 0xff,
    .from       = MSG_FROM_APP,
    .handler    = le_auracast_app_msg_handler,
};

#if TCFG_USER_TWS_ENABLE

static int le_auracast_tws_msg_handler(int *msg)
{
    struct tws_event *evt = (struct tws_event *)msg;
    u8 role = evt->args[0];
    u8 phone_link_connection = evt->args[1];
    u8 reason = evt->args[2];
    switch (evt->event) {
    case TWS_EVENT_CONNECTED:
        printf("le_auracast_tws_event_handler le_auracast role:%d, %d, %d, 0x%x\n", role, tws_api_get_role(), g_cur_auracast_is_scanning, (unsigned int)cur_listening_source_info);
        if (role != TWS_ROLE_SLAVE) {
            // tws之间配对后，主机把手机的扫描状态同步给从机
            tws_api_send_data_to_sibling((void *)&g_cur_auracast_is_scanning, sizeof(u8), 0x23482C5D);
        }
        if (cur_listening_source_info != NULL) {
            // 主机已经在监听播歌了，配对后让从机也执行相同操作
            tws_api_send_data_to_sibling(cur_listening_source_info, sizeof(auracast_sink_source_info_t), 0x23482C5B);
        }
        break;
    case TWS_EVENT_PHONE_LINK_DETACH:
        break;
    case TWS_EVENT_CONNECTION_DETACH:
        printf("le_auracast_tws_msg_handler tws detach\n");
        if (g_cur_auracast_is_scanning) {
            app_auracast_sink_scan_start();
        }
        break;
    case TWS_EVENT_ROLE_SWITCH:
        printf("le_auracast_tws_event_handler le_auracast role switch:%d, %d, %d, 0x%x\n", role, tws_api_get_role(), g_cur_auracast_is_scanning, (unsigned int)cur_listening_source_info);
        if ((role == TWS_ROLE_SLAVE) && (g_cur_auracast_is_scanning)) {
            // 新从机如果是scan状态则需要关闭自己的scan，让新主机开启scan
            tws_api_send_data_to_sibling((void *)&g_cur_auracast_is_scanning, sizeof(u8), 0x23482C5D);
        }
    default:
        break;
    }
    return 0;
}

APP_MSG_HANDLER(le_auracast_tws_msg_entry) = {
    .owner      = 0xff,
    .from       = MSG_FROM_TWS,
    .handler    = le_auracast_tws_msg_handler,
};


static void app_auracast_sink_big_sync_start_tws_in_irq(void *_data, u16 len, bool rx)
{
    printf("app_auracast_sink_big_sync_start_tws_in_irq rx:%d\n", rx);
    /* printf("%s, %s, %d\n", __FILE__, __FUNCTION__, __LINE__); */
    /* put_buf(_data, len); */
    u8 *rx_data = malloc(len);
    if (rx_data == NULL) {
        return;
    }
    memcpy(rx_data, _data, len);

    int argv[4];
    argv[0] = (int)__app_auracast_sink_big_sync_create;
    argv[1] = 1;
    argv[2] = (int)rx_data;
    argv[3] = (int)1;
    int ret = os_taskq_post_type("app_core", Q_CALLBACK, 4, argv);
    if (ret) {
        r_printf("le_auracast taskq post err %d!\n", __LINE__);
        free(rx_data);
    }
}

REGISTER_TWS_FUNC_STUB(app_auracast_sink_big_sync_start_tws) = {
    .func_id = 0x23482C5B,
    .func = app_auracast_sink_big_sync_start_tws_in_irq,
};

static void app_auracast_sink_big_sync_terminate_tws_in_irq(void *_data, u16 len, bool rx)
{
    printf("app_auracast_sink_big_sync_tws_in_irq rx:%d\n", rx);
    /* printf("%s, %s, %d\n", __FILE__, __FUNCTION__, __LINE__); */
    /* put_buf(_data, len); */

    int argv[2];
    argv[0] = (int)__app_auracast_sink_big_sync_terminate;
    argv[1] = 0;
    int ret = os_taskq_post_type("app_core", Q_CALLBACK, 2, argv);
    if (ret) {
        r_printf("le_auracast taskq post err %d!\n", __LINE__);
    }
}

REGISTER_TWS_FUNC_STUB(app_auracast_sink_big_sync_terminate_tws) = {
    .func_id = 0x23482C5C,
    .func = app_auracast_sink_big_sync_terminate_tws_in_irq,
};

static void le_auracast_scan_state_tws_sync_in_irq(void *_data, u16 len, bool rx)
{
    u8 *u8_data = (u8 *)_data;
    g_cur_auracast_is_scanning = (u8) * u8_data;
    printf("le_auracast_scan_state_tws_sync_in_irq:%d\n", g_cur_auracast_is_scanning);
    if ((tws_api_get_role() != TWS_ROLE_SLAVE) && g_cur_auracast_is_scanning) {
        int argv[2];
        argv[0] = (int)__app_auracast_sink_scan_start;
        argv[1] = 0;
        int ret = os_taskq_post_type("app_core", Q_CALLBACK, 2, argv);
        if (ret) {
            r_printf("le_auracast taskq post err %d!\n", __LINE__);
        }
    } else {
        int argv[2];
        argv[0] = (int)__app_auracast_sink_scan_stop;
        argv[1] = 0;
        int ret = os_taskq_post_type("app_core", Q_CALLBACK, 2, argv);
        if (ret) {
            r_printf("le_auracast taskq post err %d!\n", __LINE__);
        }
    }
}

REGISTER_TWS_FUNC_STUB(le_auracast_scan_state_sync) = {
    .func_id = 0x23482C5D,
    .func = le_auracast_scan_state_tws_sync_in_irq,
};

#endif // TCFG_USER_TWS_ENABLE

#if !(TCFG_LE_AUDIO_APP_CONFIG & LE_AUDIO_UNICAST_SINK_EN)
/*
 * 一些蓝牙线程消息按需处理
 * */
static int le_auracast_app_btstack_event_handler(int *_event)
{
    struct bt_event *event = (struct bt_event *)_event;
    printf("le_auracast_app_btstack_event_handler:%d\n", event->event);
    switch (event->event) {
    case BT_STATUS_FIRST_CONNECTED:
        bt_le_audio_adv_enable(0);
        break;
    case BT_STATUS_FIRST_DISCONNECT:
        bt_le_audio_adv_enable(1);
        break;
    case HCI_EVENT_CONNECTION_COMPLETE:
        break;
    case HCI_EVENT_DISCONNECTION_COMPLETE :
        printf("app le auracast HCI_EVENT_DISCONNECTION_COMPLETE: %0x\n", event->value);
        if (event->value ==  ERROR_CODE_CONNECTION_TIMEOUT) {
            //超时断开设置上请求回连标记
            bt_le_audio_adv_enable(0);
            bt_le_audio_adv_enable(1);
        }
        break;
    }
    return 0;
}

APP_MSG_HANDLER(conn_stack_msg_entry) = {
    .owner      = 0xff,
    .from       = MSG_FROM_BT_STACK,
    .handler    = le_auracast_app_btstack_event_handler,
};
#endif  // #if !(TCFG_LE_AUDIO_APP_CONFIG & LE_AUDIO_UNICAST_SINK_EN)

#endif
