/*=====================================================================================
 HEADER NAME: rdx_ble_server.c
 MODULE NAME: rdx ble server ctrl module.
 
 PRE-INCLUDE FILES DESCRIPTION: 	
 
 GENERAL DESCRIPTION: 	
 	This File will gather functions that special handle msg(UserEvent,Timer) from 
 	Intergration.These functions don't be changed by project changed.
 =======================================================================================	
 Revision History: 
 ---------------------------------------
 Author: sheng.dong
 Date: 2024-09-24 20:34:31
 LastEditors: sheng.dong
 LastEditTime: 2024-10-16 13:50:10
 FilePath: \SDK\apps\common\third_party_profile\rdx_protocol\rdx_ble_server.c
 
 Self-documenting Code
=====================================================================================*/

#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".rdx_ble_server.data.bss")
#pragma data_seg(".rdx_ble_server.data")
#pragma const_seg(".rdx_ble_server.text.const")
#pragma code_seg(".rdx_ble_server.text")
#endif

/******************************************************************************
* Include files
******************************************************************************/
#include "sdk_config.h"
#include "app_msg.h"
#include "earphone.h"
#include "bt_tws.h"
#include "app_main.h"
#include "btstack/avctp_user.h"
#include "btstack/le/sm.h"
#include "btstack/le/le_user.h"
#include "btstack/btstack_event.h"
#include "multi_protocol_main.h"
#include "circular_buf.h"
#include "user_cfg.h"
#include "system/includes.h"
#include "app_config.h"

#include "rdx_ble_server.h"
#include "rdx_ble_session.h"
#include "rdx_hogp_config.h"
#include "rdx_hogp_keyboard.h"
#include "rdx_hogp_keymap_config.h"
#include "rdx_hogp_profile.h"
#include "rdx_input_router.h"
#include "rdx_codex_micro.h"
#include "rdx_protocol.h"
#include "poweroff.h"
#include "rdx_record.h"
#include "clock.h"
#include "rdx_app.h"
#include "rdx_util.h"
#include "rdx_commonDef.h"
#include "rdx_app_config.h"
#include "rdx_uxfile.h"
#include "rdx_led_ctrl.h"

/*******************************************************************************
* Macro Define Section
*******************************************************************************/
#if (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN)

#define LOG_TAG                                     "[rdx_ble]"
#define LOG_ERROR_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_INFO_ENABLE
/* #define LOG_DUMP_ENABLE */
#define LOG_CLI_ENABLE
#include "debug.h"

//-----------------------------------------------------------------------------------------

#define MANUFAC_DATA_LENGTH                         (50)

#define RDX_FORCE_DISCONNECT_TIMEOUT                (20 * 1000)
#define RDX_DISCONNECT_ADV_RESTART_DELAY_MS         10
#define RDX_DISCONNECT_ADV_RESTART_RETRY_MAX        20
#define RDX_LIFECYCLE_BARRIER_VALUE_SIZE             17
#define RDX_LIFECYCLE_BARRIER_PACKET_SIZE            64

#define RDX_BLE_PHASE0A_WRAPPER_MAX                   2
#define RDX_BLE_PHASE0A_INVALID_WRAPPER_INDEX        0xff
#define RDX_BLE_PHASE0A_ATT_ERR_UNLIKELY_ERROR       0x0e
#define RDX_BLE_PHASE0A_ATT_ERR_INVALID_OFFSET        0x07
#define RDX_BLE_PHASE0A_ATT_ERR_INVALID_VALUE_LEN     0x0d
#define RDX_BLE_PHASE0A_ATT_ERR_INSUFFICIENT_ENCRYPTION 0x0f
#define RDX_BLE_PHASE0A_ATT_ERR_VALUE_NOT_ALLOWED     0x13
#define RDX_BLE_PHASE0A_ADV_RESTART_DELAY_MS          10

// characteristics <--> handles
#define ATT_CHARACTERISTIC_2A00_01_VALUE_HANDLE 0x0003
#define ATT_CHARACTERISTIC_06068D1C_6B97_11EF_B864_0241AC120002_01_VALUE_HANDLE 0x0006
#define ATT_CHARACTERISTIC_06068D2C_6B97_11EF_B864_0242AC120002_01_VALUE_HANDLE 0x0008
#define ATT_CHARACTERISTIC_06068D2C_6B97_11EF_B864_0242AC120002_01_CLIENT_CONFIGURATION_HANDLE 0x0009
#define ATT_CHARACTERISTIC_06068D3C_6B97_11EF_B864_0243AC120002_01_VALUE_HANDLE 0x000b
#define ATT_CHARACTERISTIC_2A19_01_VALUE_HANDLE 0x000e
#define ATT_CHARACTERISTIC_2A19_01_CLIENT_CONFIGURATION_HANDLE 0x000f
#define ATT_CHARACTERISTIC_00239A7F_C616_89BB_3374_F15AF588A7B3_01_VALUE_HANDLE 0x0012
#define ATT_CHARACTERISTIC_00239A8F_C616_89BB_3374_F25AF588A7B3_01_VALUE_HANDLE 0x0014
#define ATT_CHARACTERISTIC_00239A8F_C616_89BB_3374_F25AF588A7B3_01_CLIENT_CONFIGURATION_HANDLE 0x0015

// Device Information Service handles (appended after the selected HID layout)
#if TCFG_RDX_CODEX_MICRO_MODE
#define DIS_SERVICE_HANDLE                                              0x002d
#define DIS_PNP_ID_CHARACTERISTIC_HANDLE                                0x002e
#define DIS_PNP_ID_VALUE_HANDLE                                         0x002f
#define DIS_MANUFACTURER_NAME_CHARACTERISTIC_HANDLE                     0x0030
#define DIS_MANUFACTURER_NAME_VALUE_HANDLE                              0x0031
#else
#define DIS_SERVICE_HANDLE                                              0x0026
#define DIS_PNP_ID_CHARACTERISTIC_HANDLE                                0x0027
#define DIS_PNP_ID_VALUE_HANDLE                                         0x0028
#define DIS_MANUFACTURER_NAME_CHARACTERISTIC_HANDLE                     0x0029
#define DIS_MANUFACTURER_NAME_VALUE_HANDLE                              0x002a
#endif


//0 ~ 5 reserved.
#define ADV_MODE_BIT_MASK_AI_MODE                       (7)
#define ADV_MODE_BIT_MASK_BOUND                         (6)

#define RDX_BLE_ADV_INTERVAL_LOW                        (800)//(2400u)
#define RDX_BLE_ADV_INTERVAL_CHANGE_TIMEOUT             RDX_LED_BLE_ADV_TIMEOUT_MS

#define RDX_SELF_MARK                                   "NV"
#define RDX_BLE_ADV_DEV_COLOR_POSITION                  (18)

/******************************************************************************
* Forward declarations
******************************************************************************/
static u8   rdx_ble_server_broadcast_suppressed(void);
static void rdx_ble_server_link_disconnected_cleanup_internal(void);
static void rdx_ble_server_rdx_disconnected_cleanup_internal(void);
static void rdx_ble_server_rdx_session_abort(void);
static void rdx_ble_server_rdx_session_reset_finalize(void);
static u8 rdx_ble_server_rdx_runtime_try_rearm(void);
static void rdx_ble_server_disconnected_adv_restart(void);
static void rdx_ble_server_disconnected_adv_restart_schedule(void);
static void rdx_ble_server_disconnected_adv_restart_cancel(void);
static void rdx_ble_server_disconnected_idle_policy_resume(void);
static int rdx_ble_server_adv_enable_on_hdl(void *hdl, u8 enable);
static void rdx_ble_server_phase0a_connect_adv_restart_cancel(void);
static void rdx_ble_server_phase0a_connect_adv_restart_schedule(void);
static void rdx_ble_server_phase2_rdx_detach(rdx_ble_link_state_t *link);
#if TCFG_RDX_HOGP_ENABLE
static u8 rdx_ble_server_phase2_hid_attach(rdx_ble_link_state_t *link);
#endif
static u8 rdx_ble_server_unified_link_connected(const u8 *packet,
                                                u16 size,
                                                u8 enhanced);
static void rdx_ble_server_unified_link_disconnected(const u8 *packet,
                                                     u16 size);
static u8 rdx_ble_server_is_rdx_capability_handle(u16 att_handle);
static u8 rdx_ble_server_rdx_attach(u16 con_handle);
#if TCFG_RDX_HOGP_ENABLE
static u8 rdx_ble_server_hogp_attach(u16 con_handle);
#endif

/******************************************************************************
* Global variable Section
******************************************************************************/


/*******************************************************************************
* Local variables Section
*******************************************************************************/
/// Global BLE server context instance
static rdx_ble_server_info_t g_rdx_ble_server_info = {
    .ble_work_state = 0,
    .ble_con_handle = 0,
    .ble_conn = FALSE,
    .rdx_ble_server_hdl = NULL,
    .ble_characteristic_value_len = 0,
    .ble_mtu_size = 0,
    .ccc_configured = FALSE,
    .stream_tx_ready = FALSE,
    .force_disconnect_timer = 0,
    .adv_interval_change_timer = 0,
    .adv_interval_min = 160,
    .adv_refresh_pending = FALSE,
    .ble_local_name = {0},
    .ble_mac_addr = {0},
};

static u16 g_syn_data_timer = 0;
static u16 g_stream_tx_ready_timer = 0;
static rdx_ble_async_token_t g_syn_data_token;
static u8 g_syn_data_token_valid;
static u16 g_disconnected_adv_restart_timer = 0;
static u8 g_disconnected_adv_restart_retry = 0;
static void *g_rdx_ble_advertising_hdl = NULL;
static u8 g_rdx_lifecycle_barrier_armed;
static char g_rdx_lifecycle_barrier_value[
    RDX_LIFECYCLE_BARRIER_VALUE_SIZE];

static void *g_rdx_ble_secondary_hdl = NULL;
static void *g_rdx_ble_phase0a_disconnect_pending_hdl = NULL;
static u16 g_rdx_ble_phase0a_connect_adv_timer = 0;
static rdx_ble_async_token_t g_rdx_ble_adv_token = {
    .slot_index = RDX_BLE_LINK_INVALID_INDEX,
};

static rdx_ble_async_token_t g_rdx_ble_send_pending_token = {
    .slot_index = RDX_BLE_LINK_INVALID_INDEX,
};
static u16 g_rdx_ble_send_pending_count = 0;

typedef struct {
    rdx_ble_async_token_t token;
    void *ble_hdl;
    u16 con_handle;
    u16 mtu_size;
} rdx_ble_rdx_transport_snapshot_t;

static void rdx_ble_server_rdx_send_pending_reset(void)
{
    g_rdx_ble_send_pending_count = 0;
    g_rdx_ble_send_pending_token.slot_index = RDX_BLE_LINK_INVALID_INDEX;
    g_rdx_ble_send_pending_token.slot_generation = 0;
    g_rdx_ble_send_pending_token.transport_epoch = 0;
}

static u8 rdx_ble_server_rdx_transport_snapshot_capture(
    rdx_ble_rdx_transport_snapshot_t *snapshot)
{
    rdx_ble_link_state_t *link;

    if (!snapshot ||
        !rdx_ble_session_rdx_token_capture(&snapshot->token, 1)) {
        return 0;
    }
    link = rdx_ble_session_rdx_token_resolve(&snapshot->token, 1);
    if (!link || app_ble_get_hdl_con_handle(link->ble_hdl) !=
                     link->con_handle) {
        return 0;
    }
    snapshot->ble_hdl = link->ble_hdl;
    snapshot->con_handle = link->con_handle;
    snapshot->mtu_size = link->mtu_size;
    return 1;
}

static u8 rdx_ble_server_rdx_transport_snapshot_is_current(
    const rdx_ble_rdx_transport_snapshot_t *snapshot)
{
    rdx_ble_link_state_t *link;

    if (!snapshot) {
        return 0;
    }
    link = rdx_ble_session_rdx_token_resolve(&snapshot->token, 1);
    return (link && link->ble_hdl == snapshot->ble_hdl &&
            link->con_handle == snapshot->con_handle &&
            app_ble_get_hdl_con_handle(snapshot->ble_hdl) ==
                snapshot->con_handle) ? 1 : 0;
}

static void rdx_ble_server_rdx_send_pending_arm(
    const rdx_ble_rdx_transport_snapshot_t *snapshot)
{
    if (!rdx_ble_server_rdx_transport_snapshot_is_current(snapshot)) {
        return;
    }
    if (g_rdx_ble_send_pending_count &&
        (g_rdx_ble_send_pending_token.slot_index !=
             snapshot->token.slot_index ||
         g_rdx_ble_send_pending_token.slot_generation !=
             snapshot->token.slot_generation ||
         g_rdx_ble_send_pending_token.transport_epoch !=
             snapshot->token.transport_epoch)) {
        rdx_ble_server_rdx_send_pending_reset();
    }
    g_rdx_ble_send_pending_token = snapshot->token;
    if (g_rdx_ble_send_pending_count != 0xffff) {
        g_rdx_ble_send_pending_count++;
    }
}

static void rdx_ble_server_rdx_send_pending_cancel(
    const rdx_ble_rdx_transport_snapshot_t *snapshot)
{
    if (!snapshot || !g_rdx_ble_send_pending_count ||
        g_rdx_ble_send_pending_token.slot_index !=
            snapshot->token.slot_index ||
        g_rdx_ble_send_pending_token.slot_generation !=
            snapshot->token.slot_generation ||
        g_rdx_ble_send_pending_token.transport_epoch !=
            snapshot->token.transport_epoch) {
        return;
    }
    g_rdx_ble_send_pending_count--;
    if (!g_rdx_ble_send_pending_count) {
        rdx_ble_server_rdx_send_pending_reset();
    }
}

static u8 rdx_ble_server_rdx_send_pending_consume(
    rdx_ble_link_state_t *event_link)
{
    rdx_ble_link_state_t *pending_link;

    if (!g_rdx_ble_send_pending_count || !event_link) {
        return 0;
    }
    pending_link = rdx_ble_session_rdx_token_resolve(
        &g_rdx_ble_send_pending_token, 1);
    if (!pending_link) {
        rdx_ble_server_rdx_send_pending_reset();
        return 0;
    }
    if (pending_link != event_link ||
        app_ble_get_hdl_con_handle(event_link->ble_hdl) !=
            event_link->con_handle) {
        return 0;
    }
    g_rdx_ble_send_pending_count--;
    if (!g_rdx_ble_send_pending_count) {
        rdx_ble_server_rdx_send_pending_reset();
    }
    return 1;
}

static void *rdx_ble_server_phase0a_wrapper_get(u8 index)
{
    if (index == 0) {
        return g_rdx_ble_server_info.rdx_ble_server_hdl;
    }
    if (index == 1) {
        return g_rdx_ble_secondary_hdl;
    }
    return NULL;
}

static u8 rdx_ble_server_phase0a_wrapper_index(void *hdl)
{
    u8 index;

    for (index = 0; index < RDX_BLE_PHASE0A_WRAPPER_MAX; index++) {
        if (rdx_ble_server_phase0a_wrapper_get(index) == hdl) {
            return index;
        }
    }
    return RDX_BLE_PHASE0A_INVALID_WRAPPER_INDEX;
}

static u8 rdx_ble_server_phase0a_connected_count(void)
{
    u8 index;
    u8 count = 0;

    for (index = 0; index < RDX_BLE_PHASE0A_WRAPPER_MAX; index++) {
        void *hdl = rdx_ble_server_phase0a_wrapper_get(index);
        if (hdl && app_ble_get_hdl_con_handle(hdl)) {
            count++;
        }
    }
    return count;
}

static void *rdx_ble_server_phase0a_idle_wrapper_get(void)
{
    u8 index;

    for (index = 0; index < RDX_BLE_PHASE0A_WRAPPER_MAX; index++) {
        void *hdl = rdx_ble_server_phase0a_wrapper_get(index);
        if (hdl && !app_ble_get_hdl_con_handle(hdl)) {
            return hdl;
        }
    }
    return NULL;
}

static rdx_ble_link_state_t *rdx_ble_server_phase0b_link_by_hdl(void *hdl)
{
    return rdx_ble_session_find_by_hdl(hdl);
}

static rdx_ble_link_state_t *rdx_ble_server_phase0b_link_find(
    void *hdl, u16 con_handle)
{
    rdx_ble_link_state_t *link = rdx_ble_session_find(hdl, con_handle);

    if (!link || app_ble_get_hdl_con_handle(hdl) != con_handle) {
        return NULL;
    }
    return link;
}

static void rdx_ble_server_phase0b_adv_token_capture(void *hdl)
{
    g_rdx_ble_adv_token = rdx_ble_session_token_capture(
        rdx_ble_session_find_by_hdl(hdl));
}

static u8 rdx_ble_server_phase0b_adv_token_is_current(void)
{
    return rdx_ble_session_idle_token_resolve(&g_rdx_ble_adv_token) ? 1 : 0;
}

static u8 rdx_ble_server_phase0a_event_matches(void *hdl,
                                               u16 event_con_handle,
                                               const char *event_name)
{
    u8 wrapper_index = rdx_ble_server_phase0a_wrapper_index(hdl);
    u16 wrapper_con_handle = hdl ? app_ble_get_hdl_con_handle(hdl) : 0;

    r_printf("[RDX_BLE_LINK] %s wrapper=%u cb_hdl=%p event_con=0x%04x wrapper_con=0x%04x\n",
             event_name,
             wrapper_index,
             hdl,
             event_con_handle,
             wrapper_con_handle);

    if (wrapper_index == RDX_BLE_PHASE0A_INVALID_WRAPPER_INDEX ||
        wrapper_con_handle != event_con_handle) {
        r_printf("[RDX_BLE_LINK] %s ignored: wrapper/connection mismatch\n",
                 event_name);
        return 0;
    }
    return 1;
}

static void rdx_ble_server_phase0a_wrapper_connection_clear(void *hdl,
                                                            u16 con_handle,
                                                            const char *reason)
{
    if (hdl && app_ble_get_hdl_con_handle(hdl) == con_handle) {
        y_printf("[RDX_BLE_LINK] clear stale wrapper=%u con=0x%04x reason=%s\n",
                 rdx_ble_server_phase0a_wrapper_index(hdl),
                 con_handle,
                 reason);
        app_ble_set_filter_con_handle(hdl, 0);
    }
}

const char *const rdx_phy_result[] = {
    "None",
    "1M",
    "2M",
    "Coded",
};

/*************************************************
                  BLE 相关内容
*************************************************/
const uint8_t rdx_profile_data[] = {
    //////////////////////////////////////////////////////
    //
    // 0x0001 PRIMARY_SERVICE  0x1800
    //
    //////////////////////////////////////////////////////
    0x0a, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x28, 0x00, 0x18,

     /* CHARACTERISTIC,  2A00, READ | DYNAMIC, */
    // 0x0002 CHARACTERISTIC 2A00 READ | DYNAMIC 
    0x0d, 0x00, 0x02, 0x00, 0x02, 0x00, 0x03, 0x28, 0x02, 0x03, 0x00, 0x00, 0x2a,
    // 0x0003 VALUE 2A00 READ | DYNAMIC  
    0x08, 0x00, 0x02, 0x01, 0x03, 0x00, 0x00, 0x2a,

    //////////////////////////////////////////////////////
    //
    // 0x0004 PRIMARY_SERVICE  06068D0C-6B97-11EF-B864-0240AC120002
    //
    //////////////////////////////////////////////////////
    0x18, 0x00, 0x02, 0x00, 0x04, 0x00, 0x00, 0x28, 0x02, 0x00, 0x12, 0xac, 0x40, 0x02, 0x64, 0xb8, 0xef, 0x11, 0x97, 0x6b, 0x0c, 0x8d, 0x06, 0x06,

     /* CHARACTERISTIC,  06068D1C-6B97-11EF-B864-0241AC120002, WRITE_WITHOUT_RESPONSE | DYNAMIC, */
    // 0x0005 CHARACTERISTIC 06068D1C-6B97-11EF-B864-0241AC120002 WRITE_WITHOUT_RESPONSE | DYNAMIC 
    0x1b, 0x00, 0x02, 0x00, 0x05, 0x00, 0x03, 0x28, 0x04, 0x06, 0x00, 0x02, 0x00, 0x12, 0xac, 0x41, 0x02, 0x64, 0xb8, 0xef, 0x11, 0x97, 0x6b, 0x1c, 0x8d, 0x06, 0x06,
    // 0x0006 VALUE 06068D1C-6B97-11EF-B864-0241AC120002 WRITE_WITHOUT_RESPONSE | DYNAMIC  
    0x16, 0x00, 0x04, 0x03, 0x06, 0x00, 0x02, 0x00, 0x12, 0xac, 0x41, 0x02, 0x64, 0xb8, 0xef, 0x11, 0x97, 0x6b, 0x1c, 0x8d, 0x06, 0x06,

     /* CHARACTERISTIC,  06068D2C-6B97-11EF-B864-0242AC120002, NOTIFY, */
    // 0x0007 CHARACTERISTIC 06068D2C-6B97-11EF-B864-0242AC120002 NOTIFY 
    0x1b, 0x00, 0x02, 0x00, 0x07, 0x00, 0x03, 0x28, 0x10, 0x08, 0x00, 0x02, 0x00, 0x12, 0xac, 0x42, 0x02, 0x64, 0xb8, 0xef, 0x11, 0x97, 0x6b, 0x2c, 0x8d, 0x06, 0x06,
    // 0x0008 VALUE 06068D2C-6B97-11EF-B864-0242AC120002 NOTIFY  
    0x16, 0x00, 0x10, 0x02, 0x08, 0x00, 0x02, 0x00, 0x12, 0xac, 0x42, 0x02, 0x64, 0xb8, 0xef, 0x11, 0x97, 0x6b, 0x2c, 0x8d, 0x06, 0x06,
    // 0x0009 CLIENT_CHARACTERISTIC_CONFIGURATION 
    0x0a, 0x00, 0x0a, 0x01, 0x09, 0x00, 0x02, 0x29, 0x00, 0x00,

     /* CHARACTERISTIC,  06068D3C-6B97-11EF-B864-0243AC120002, READ | DYNAMIC, */
    // 0x000a CHARACTERISTIC 06068D3C-6B97-11EF-B864-0243AC120002 READ | DYNAMIC 
    0x1b, 0x00, 0x02, 0x00, 0x0a, 0x00, 0x03, 0x28, 0x02, 0x0b, 0x00, 0x02, 0x00, 0x12, 0xac, 0x43, 0x02, 0x64, 0xb8, 0xef, 0x11, 0x97, 0x6b, 0x3c, 0x8d, 0x06, 0x06,
    // 0x000b VALUE 06068D3C-6B97-11EF-B864-0243AC120002 READ | DYNAMIC  
    0x16, 0x00, 0x02, 0x03, 0x0b, 0x00, 0x02, 0x00, 0x12, 0xac, 0x43, 0x02, 0x64, 0xb8, 0xef, 0x11, 0x97, 0x6b, 0x3c, 0x8d, 0x06, 0x06,

    //////////////////////////////////////////////////////
    //
    // 0x000c PRIMARY_SERVICE  0x180F
    //
    //////////////////////////////////////////////////////
    0x0a, 0x00, 0x02, 0x00, 0x0c, 0x00, 0x00, 0x28, 0x0f, 0x18,

     /* CHARACTERISTIC,  2A19, READ | NOTIFY | DYNAMIC, */
    // 0x000d CHARACTERISTIC 2A19 READ | NOTIFY | DYNAMIC 
    0x0d, 0x00, 0x02, 0x00, 0x0d, 0x00, 0x03, 0x28, 0x12, 0x0e, 0x00, 0x19, 0x2a,
    // 0x000e VALUE 2A19 READ | NOTIFY | DYNAMIC  
    0x08, 0x00, 0x12, 0x01, 0x0e, 0x00, 0x19, 0x2a,
    // 0x000f CLIENT_CHARACTERISTIC_CONFIGURATION 
    0x0a, 0x00, 0x0a, 0x01, 0x0f, 0x00, 0x02, 0x29, 0x00, 0x00,

    //////////////////////////////////////////////////////
    //
    // 0x0010 PRIMARY_SERVICE  00239A6F-C616-89BB-3374-F05AF588A7B3
    //
    //////////////////////////////////////////////////////
    0x18, 0x00, 0x02, 0x00, 0x10, 0x00, 0x00, 0x28, 0xb3, 0xa7, 0x88, 0xf5, 0x5a, 0xf0, 0x74, 0x33, 0xbb, 0x89, 0x16, 0xc6, 0x6f, 0x9a, 0x23, 0x00,

     /* CHARACTERISTIC,  00239A7F-C616-89BB-3374-F15AF588A7B3, WRITE_WITHOUT_RESPONSE | DYNAMIC, */
    // 0x0011 CHARACTERISTIC 00239A7F-C616-89BB-3374-F15AF588A7B3 WRITE_WITHOUT_RESPONSE | DYNAMIC 
    0x1b, 0x00, 0x02, 0x00, 0x11, 0x00, 0x03, 0x28, 0x04, 0x12, 0x00, 0xb3, 0xa7, 0x88, 0xf5, 0x5a, 0xf1, 0x74, 0x33, 0xbb, 0x89, 0x16, 0xc6, 0x7f, 0x9a, 0x23, 0x00,
    // 0x0012 VALUE 00239A7F-C616-89BB-3374-F15AF588A7B3 WRITE_WITHOUT_RESPONSE | DYNAMIC  
    0x16, 0x00, 0x04, 0x03, 0x12, 0x00, 0xb3, 0xa7, 0x88, 0xf5, 0x5a, 0xf1, 0x74, 0x33, 0xbb, 0x89, 0x16, 0xc6, 0x7f, 0x9a, 0x23, 0x00,

     /* CHARACTERISTIC,  00239A8F-C616-89BB-3374-F25AF588A7B3, NOTIFY, */
    // 0x0013 CHARACTERISTIC 00239A8F-C616-89BB-3374-F25AF588A7B3 NOTIFY 
    0x1b, 0x00, 0x02, 0x00, 0x13, 0x00, 0x03, 0x28, 0x10, 0x14, 0x00, 0xb3, 0xa7, 0x88, 0xf5, 0x5a, 0xf2, 0x74, 0x33, 0xbb, 0x89, 0x16, 0xc6, 0x8f, 0x9a, 0x23, 0x00,
    // 0x0014 VALUE 00239A8F-C616-89BB-3374-F25AF588A7B3 NOTIFY  
    0x16, 0x00, 0x10, 0x02, 0x14, 0x00, 0xb3, 0xa7, 0x88, 0xf5, 0x5a, 0xf2, 0x74, 0x33, 0xbb, 0x89, 0x16, 0xc6, 0x8f, 0x9a, 0x23, 0x00,
    // 0x0015 CLIENT_CHARACTERISTIC_CONFIGURATION
    0x0a, 0x00, 0x0a, 0x01, 0x15, 0x00, 0x02, 0x29, 0x00, 0x00,

    //////////////////////////////////////////////////////
    //
    // 0x0016 PRIMARY_SERVICE  0x1812 (HID)
    //
    //////////////////////////////////////////////////////
#if TCFG_RDX_HOGP_ENABLE
    RDX_HOGP_ATT_PRIMARY_SERVICE_16(HID_SERVICE_HANDLE, RDX_HOGP_UUID_HID_SERVICE),

     /* CHARACTERISTIC,  2A4E, READ | WRITE_WITHOUT_RESPONSE | DYNAMIC; value requires encryption */
    // 0x0017 CHARACTERISTIC 2A4E READ | WRITE_WITHOUT_RESPONSE | DYNAMIC
    RDX_HOGP_ATT_CHARACTERISTIC_16(HID_PROTOCOL_MODE_CHARACTERISTIC_HANDLE,
                                   RDX_HOGP_CHAR_PROP_PROTOCOL_MODE,
                                   HID_PROTOCOL_MODE_VALUE_HANDLE, RDX_HOGP_UUID_PROTOCOL_MODE),
    // 0x0018 VALUE 2A4E READ | WRITE_WITHOUT_RESPONSE | DYNAMIC | ENCRYPTED READ/WRITE
    RDX_HOGP_ATT_VALUE_16(HID_PROTOCOL_MODE_VALUE_HANDLE,
                          RDX_HOGP_ATT_FLAGS_PROTOCOL_MODE_VALUE,
                          RDX_HOGP_UUID_PROTOCOL_MODE),

#if TCFG_RDX_CODEX_MICRO_MODE != RDX_CODEX_MICRO_MODE_VENDOR_ONLY
     /* CHARACTERISTIC,  2A4D, READ | NOTIFY | DYNAMIC; value/CCC require encryption */
    // 0x0019 CHARACTERISTIC 2A4D READ | NOTIFY | DYNAMIC
    RDX_HOGP_ATT_CHARACTERISTIC_16(HID_INPUT_REPORT_CHARACTERISTIC_HANDLE,
                                   RDX_HOGP_CHAR_PROP_INPUT_REPORT,
                                   HID_INPUT_REPORT_VALUE_HANDLE, RDX_HOGP_UUID_REPORT),
    // 0x001a VALUE 2A4D READ | NOTIFY | DYNAMIC | ENCRYPTED READ
    RDX_HOGP_ATT_VALUE_16(HID_INPUT_REPORT_VALUE_HANDLE,
                          RDX_HOGP_ATT_FLAGS_INPUT_REPORT_VALUE,
                          RDX_HOGP_UUID_REPORT),
    // 0x001b CLIENT_CHARACTERISTIC_CONFIGURATION | ENCRYPTED READ/WRITE
    RDX_HOGP_ATT_CCC(HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE, RDX_HOGP_CCC_DEFAULT_VALUE),
    // 0x001c REPORT_REFERENCE, report_id=1, report_type=1 (Input)
    RDX_HOGP_ATT_REPORT_REFERENCE(HID_INPUT_REPORT_REFERENCE_HANDLE,
                                  RDX_HOGP_INPUT_REPORT_ID, RDX_HOGP_INPUT_REPORT_TYPE),
#endif

     /* CHARACTERISTIC,  2A4B, READ | DYNAMIC */
    // 0x001d CHARACTERISTIC 2A4B READ | DYNAMIC
    RDX_HOGP_ATT_CHARACTERISTIC_16(HID_REPORT_MAP_CHARACTERISTIC_HANDLE,
                                   RDX_HOGP_CHAR_PROP_REPORT_MAP,
                                   HID_REPORT_MAP_VALUE_HANDLE, RDX_HOGP_UUID_REPORT_MAP),
    // 0x001e VALUE 2A4B READ | DYNAMIC
    RDX_HOGP_ATT_VALUE_16(HID_REPORT_MAP_VALUE_HANDLE,
                          RDX_HOGP_ATT_FLAGS_REPORT_MAP_VALUE,
                          RDX_HOGP_UUID_REPORT_MAP),

     /* CHARACTERISTIC,  2A4A, READ | DYNAMIC */
    // 0x001f CHARACTERISTIC 2A4A READ | DYNAMIC
    RDX_HOGP_ATT_CHARACTERISTIC_16(HID_INFORMATION_CHARACTERISTIC_HANDLE,
                                   RDX_HOGP_CHAR_PROP_HID_INFORMATION,
                                   HID_INFORMATION_VALUE_HANDLE, RDX_HOGP_UUID_HID_INFORMATION),
    // 0x0020 VALUE 2A4A READ | DYNAMIC
    RDX_HOGP_ATT_VALUE_16(HID_INFORMATION_VALUE_HANDLE,
                          RDX_HOGP_ATT_FLAGS_HID_INFORMATION_VALUE,
                          RDX_HOGP_UUID_HID_INFORMATION),

     /* CHARACTERISTIC,  2A4C, WRITE_WITHOUT_RESPONSE | DYNAMIC; value requires encryption */
    // 0x0021 CHARACTERISTIC 2A4C WRITE_WITHOUT_RESPONSE | DYNAMIC
    RDX_HOGP_ATT_CHARACTERISTIC_16(HID_CONTROL_POINT_CHARACTERISTIC_HANDLE,
                                   RDX_HOGP_CHAR_PROP_CONTROL_POINT,
                                   HID_CONTROL_POINT_VALUE_HANDLE,
                                   RDX_HOGP_UUID_HID_CONTROL_POINT),
    // 0x0022 VALUE 2A4C WRITE_WITHOUT_RESPONSE | DYNAMIC | ENCRYPTED WRITE
    RDX_HOGP_ATT_VALUE_16(HID_CONTROL_POINT_VALUE_HANDLE,
                          RDX_HOGP_ATT_FLAGS_CONTROL_POINT_VALUE,
                          RDX_HOGP_UUID_HID_CONTROL_POINT),

#if TCFG_RDX_CODEX_MICRO_MODE != RDX_CODEX_MICRO_MODE_VENDOR_ONLY
    // 0x0023 CHARACTERISTIC 0x2A4D (Output Report)
    RDX_HOGP_ATT_CHARACTERISTIC_16(HID_OUTPUT_REPORT_CHARACTERISTIC_HANDLE,
                                   RDX_HOGP_CHAR_PROP_OUTPUT_REPORT,
                                   HID_OUTPUT_REPORT_VALUE_HANDLE, RDX_HOGP_UUID_REPORT),
    // 0x0024 VALUE 0x2A4D, 1-byte keyboard LED bitmap, DYNAMIC | ENCRYPTED READ/WRITE
    RDX_HOGP_ATT_VALUE_16(HID_OUTPUT_REPORT_VALUE_HANDLE,
                          RDX_HOGP_ATT_FLAGS_OUTPUT_REPORT_VALUE,
                          RDX_HOGP_UUID_REPORT),
    // 0x0025 REPORT_REFERENCE (ID=1, Type=2=Output)
    RDX_HOGP_ATT_REPORT_REFERENCE(HID_OUTPUT_REPORT_REFERENCE_HANDLE,
                                  RDX_HOGP_OUTPUT_REPORT_ID,
                                  RDX_HOGP_OUTPUT_REPORT_TYPE),
#endif

#if TCFG_RDX_CODEX_MICRO_MODE
    // 0x0026-0x0029 Report ID 6 Input Value, CCC, and Report Reference
    RDX_HOGP_ATT_CHARACTERISTIC_16(HID_CODEX_INPUT_REPORT_CHARACTERISTIC_HANDLE,
                                   RDX_HOGP_CHAR_PROP_INPUT_REPORT,
                                   HID_CODEX_INPUT_REPORT_VALUE_HANDLE,
                                   RDX_HOGP_UUID_REPORT),
    RDX_HOGP_ATT_VALUE_16(HID_CODEX_INPUT_REPORT_VALUE_HANDLE,
                          RDX_HOGP_ATT_FLAGS_INPUT_REPORT_VALUE,
                          RDX_HOGP_UUID_REPORT),
    RDX_HOGP_ATT_CCC(HID_CODEX_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE,
                     RDX_HOGP_CCC_DEFAULT_VALUE),
    RDX_HOGP_ATT_REPORT_REFERENCE(HID_CODEX_INPUT_REPORT_REFERENCE_HANDLE,
                                  RDX_CODEX_MICRO_REPORT_ID,
                                  RDX_CODEX_MICRO_INPUT_REPORT_TYPE),

    // 0x002a-0x002c Report ID 6 Output Value and Report Reference
    RDX_HOGP_ATT_CHARACTERISTIC_16(HID_CODEX_OUTPUT_REPORT_CHARACTERISTIC_HANDLE,
                                   RDX_HOGP_CHAR_PROP_OUTPUT_REPORT,
                                   HID_CODEX_OUTPUT_REPORT_VALUE_HANDLE,
                                   RDX_HOGP_UUID_REPORT),
    RDX_HOGP_ATT_VALUE_16(HID_CODEX_OUTPUT_REPORT_VALUE_HANDLE,
                          RDX_HOGP_ATT_FLAGS_OUTPUT_REPORT_VALUE,
                          RDX_HOGP_UUID_REPORT),
    RDX_HOGP_ATT_REPORT_REFERENCE(HID_CODEX_OUTPUT_REPORT_REFERENCE_HANDLE,
                                  RDX_CODEX_MICRO_REPORT_ID,
                                  RDX_CODEX_MICRO_OUTPUT_REPORT_TYPE),
#endif
#endif /* TCFG_RDX_HOGP_ENABLE */

    //////////////////////////////////////////////////////
    //
    // Device Information Service
    //
    //////////////////////////////////////////////////////
    RDX_HOGP_ATT_PRIMARY_SERVICE_16(DIS_SERVICE_HANDLE, 0x180a),

     /* CHARACTERISTIC,  2A50, READ, */
    // 0x0027 CHARACTERISTIC 2A50 READ
    RDX_HOGP_ATT_CHARACTERISTIC_16(DIS_PNP_ID_CHARACTERISTIC_HANDLE,
                                   RDX_HOGP_ATT_PROP_READ,
                                   DIS_PNP_ID_VALUE_HANDLE, 0x2a50),
#if TCFG_RDX_CODEX_MICRO_TEST_IDENTITY_ENABLE
    // PnP ID: source=USB-IF, VID=303A, PID=8360, version=0101 (little-endian)
    0x0f, 0x00, 0x02, 0x00, RDX_HOGP_ATT_U16_LE(DIS_PNP_ID_VALUE_HANDLE),
    0x50, 0x2a, 0x02, 0x3a, 0x30, 0x60, 0x83, 0x01, 0x01,
#else
    0x0f, 0x00, 0x02, 0x00, RDX_HOGP_ATT_U16_LE(DIS_PNP_ID_VALUE_HANDLE),
    0x50, 0x2a, 0x02, 0x34, 0x12, 0x01, 0x00, 0x01, 0x00,
#endif

     /* CHARACTERISTIC,  2A29, READ, */
    // 0x0029 CHARACTERISTIC 2A29 READ
    RDX_HOGP_ATT_CHARACTERISTIC_16(DIS_MANUFACTURER_NAME_CHARACTERISTIC_HANDLE,
                                   RDX_HOGP_ATT_PROP_READ,
                                   DIS_MANUFACTURER_NAME_VALUE_HANDLE, 0x2a29),
#if TCFG_RDX_CODEX_MICRO_TEST_IDENTITY_ENABLE
    0x13, 0x00, 0x02, 0x00, RDX_HOGP_ATT_U16_LE(DIS_MANUFACTURER_NAME_VALUE_HANDLE),
    0x29, 0x2a, 'W', 'o', 'r', 'k', ' ', 'L', 'o', 'u', 'd', 'e', 'r',
#else
    0x0d, 0x00, 0x02, 0x00, RDX_HOGP_ATT_U16_LE(DIS_MANUFACTURER_NAME_VALUE_HANDLE),
    0x29, 0x2a, 'J', 'i', 'e', 'L', 'i',
#endif

    // END
    0x00, 0x00,
};

/******************************************************************************
* Function Declaration Section
******************************************************************************/ 
extern void rdx_record_start(void* priv);
extern void sys_set_auto_off_time(u32 auto_off_time);
extern u32 sys_get_auto_off_time(void);
extern bool rdx_app_get_poweroff_flag(void);
extern void rdx_protocol_record_trigger_indicate(RecordStatus* d, bool factor);
extern void rdx_ota_stop(void);
extern void rdx_app_clk_unlock(const char *task_name);
extern RecordStatus* rdx_record_get_status(void);
extern u8 rdx_app_get_record_mode(void);
extern int bt_modify_name(u8 *new_name);
extern void rdx_protocol_record_state_indicate(void);
extern u8 get_self_battery_level(void);
extern u8 get_ota_status();
extern void rdx_uxfile_datFileInfo_sendBuf_free(void);
extern void rdx_uxfile_recordFileData_sendBuf_free(void);
extern void rdx_protocol_ble_name_set_ack_indicate(u8 result, char* ble_name);
extern u8 rdx_protocol_get_ble_sent(void);
extern void rdx_protocol_set_ble_sent(u8 d);
extern void rdx_app_emmc_poweroff_check(void);
extern void rdx_app_emmc_poweroff_check_timer_stop(void);
extern bool rdx_app_get_power_ready_flag(void);
extern void rdx_app_set_power_ready_flag(void);
extern bool rdx_app_get_dut_status(void);
extern void rdx_app_emmc_poweron(u8 check_en);
extern void rdx_app_emmc_poweroff(void);
extern void rdx_record_mode_active_check(bool show);
extern u8 rdx_battery_get_percent(void);
extern void rdx_protocol_file_sync_busy_timer_stop(void);
extern void rdx_protocol_uploadFileInfo_clean(void);
extern void rdx_protocol_clear_send_confirm_flag(void);
extern void rdx_record_stream_interrupt(void);
extern void rdx_record_stream_resume_delayed(void);

int rdx_ble_server_adv_enable(u8 enable);
void rdx_ble_server_auto_shut_down_enable(u8 enable);
void rdx_ble_server_adv_data_changed(void);
void rdx_ble_server_syn_data_after_ble_write_ready(void* priv);
void rdx_ble_server_stop_force_disconnect_timer(void);
void rdx_ble_server_bulk_data_send_para_reset(void);
void rdx_ble_server_adv_interval_change_timer_stop(void);

/******************************************************************************
* Function Section
******************************************************************************/ 

#define ATT_LOCAL_MTU_SIZE          (517) 
//ATT缓存的buffer大小,  note: need >= 20,可修改
#define ATT_SEND_CBUF_SIZE          (4096) 

//共配置的RAM
#define ATT_RAM_BUFSIZE             (ATT_CTRL_BLOCK_SIZE + ATT_LOCAL_MTU_SIZE + ATT_SEND_CBUF_SIZE)//note:
static u8 att_ram_buffer[ATT_RAM_BUFSIZE] __attribute__((aligned(4)));

// Connection parameter update request settings
// Whether to enable parameter update request, 0--disable, 1--enable
static const uint8_t connection_update_enable = 1; ///0--disable, 1--enable
// Current request parameter table index
static uint8_t connection_update_cnt = 0; //

// Parameter table //total 9.
static const struct conn_update_param_t connection_param_table[] = {
    {6,  10, 4, 600},
    {10, 14, 4, 600},
    {14, 20, 4, 600},
    {8,  20, 10, 800},
    {12, 28, 10, 800},
    {16, 24, 10, 800},
    {20, 40, 19, 2000},
    {40, 60, 19, 2000},
    {60, 80, 19, 2000}, //8
}; 

#define CONN_PARAM_TABLE_CNT      (sizeof(connection_param_table)/sizeof(struct conn_update_param_t))

/**************************************************************************
 * function: rdx_ble_server_send_request_connect_parameter
 * description: 
 * param (u8) table_index
 * return (*)
 **************************************************************************/
void rdx_ble_server_send_request_connect_parameter(u8 table_index)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    struct conn_update_param_t *param = (void *)&connection_param_table[table_index];
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    log_info("====== update_request:-%d-%d-%d-%d-\r", param->interval_min, param->interval_max, param->latency, param->timeout);
    if(g_rdx_ble_server_info.ble_con_handle) {
        ble_op_conn_param_request(g_rdx_ble_server_info.ble_con_handle, param);
    }
}

/**************************************************************************
 * function: rdx_ble_server_check_connetion_updata_deal
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
static void rdx_ble_server_check_connetion_updata_deal(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if (connection_update_enable) {
        if (connection_update_cnt < CONN_PARAM_TABLE_CNT) {
            rdx_ble_server_send_request_connect_parameter(connection_update_cnt);
        }
    }
}

static u8 rdx_ble_server_local_name_copy(char *dst, const char *src, u8 len)
{
    if (len > BLE_LOCAL_NAME_MAX_LEN) {
        len = BLE_LOCAL_NAME_MAX_LEN;
    }
    memset(dst, 0, BLE_LOCAL_NAME_MAX_LEN + 1);
    if (src && len) {
        memcpy(dst, src, len);
    }
    dst[len] = '\0';
    return len;
}

static u8 rdx_ble_server_default_local_name_build(char *name)
{
    DevBaseInfo *p = rdx_app_get_dev_base_info();
    u8 suffix[5] = {0};

    if (strlen((char *)p->auth) >= 24) {
        memcpy(suffix, p->auth + 20, 4);
    }
    if (suffix[0]) {
        snprintf(name, BLE_LOCAL_NAME_MAX_LEN + 1, "%s %s", BLE_LOCAL_NAME, suffix);
    } else {
        snprintf(name, BLE_LOCAL_NAME_MAX_LEN + 1, "%s", BLE_LOCAL_NAME);
    }
    name[BLE_LOCAL_NAME_MAX_LEN] = '\0';
    return (u8)strlen(name);
}

static int rdx_ble_server_local_name_store(const char *name, u8 len, u8 refresh_adv)
{
    int ret;

    len = rdx_ble_server_local_name_copy(g_rdx_ble_server_info.ble_local_name,
                                         name, len);
    if (len == 0) {
        return -1;
    }
    ret = syscfg_write(VM_RDX_BLE_NAME,
                       g_rdx_ble_server_info.ble_local_name,
                       len);
    if (ret <= 0) {
        log_info("%s --> write local name failed \r", __func__);
    } else {
        log_info("%s --> write local name success: %s \r",
                 __func__, g_rdx_ble_server_info.ble_local_name);
        if (refresh_adv) {
            rdx_ble_server_adv_data_changed();
        }
    }
    return ret;
}

/**************************************************************************
 * function: rdx_ble_server_reset_local_name
 * description: 
 * param (char) *name
 * param (u8) len
 * return (*)
 **************************************************************************/
int rdx_ble_server_reset_local_name(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    char name[BLE_LOCAL_NAME_MAX_LEN + 1];
    u8 len = rdx_ble_server_default_local_name_build(name);
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    return rdx_ble_server_local_name_store(name, len, 1);
}

/**************************************************************************
 * function: rdx_ble_server_get_local_name
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
char* rdx_ble_server_get_local_name(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    char tmp[BLE_LOCAL_NAME_MAX_LEN + 1] = {0};
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    int ret = syscfg_read(VM_RDX_BLE_NAME, tmp, BLE_LOCAL_NAME_MAX_LEN);
    if (ret <= 0) {
        log_info("===> %s --> local name set default! \r", __func__);
        u8 len = rdx_ble_server_default_local_name_build(tmp);
        rdx_ble_server_local_name_store(tmp, len, 0);
    } else {
        log_info("===> %s --> read local name success, current name: %s \r", __func__, tmp);
        rdx_ble_server_local_name_copy(g_rdx_ble_server_info.ble_local_name,
                                       tmp, (u8)ret);
    }
    return g_rdx_ble_server_info.ble_local_name;
}

/**************************************************************************
 * function: rdx_ble_server_set_local_name
 * description: 
 * param (char) *name
 * param (u8) len
 * return (*)
 **************************************************************************/
int rdx_ble_server_set_local_name(char *name, u8 len)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    if(name == NULL) {
        log_info("%s --> name is NULL\r", __func__);
        return -1;
    }
    return rdx_ble_server_local_name_store(name, len, 1);
}

/**************************************************************************
 * function: rdx_ble_server_local_name_handle
 * description: 
 * param (char*) name
 * param (u16) len
 * return (*)
 **************************************************************************/
int rdx_ble_server_local_name_handle(char* name, u16 len)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    int ret = rdx_ble_server_set_local_name(name, len);
    if(ret <= 0){
        log_info("%s --> ble name set error! \r", __func__);
        rdx_protocol_ble_name_set_ack_indicate(1, name);
        return E_PROTOCOL_ECODE_FAIL;
    }

    //result: 0 -> ok, 1 -> error
    log_info("%s --> ble name set ok: %s \r", __func__, name);
    rdx_protocol_ble_name_set_ack_indicate(0, name);
    return E_PROTOCOL_ECODE_SUCCESS;
}

/**************************************************************************
 * function: rdx_ble_server_bt_name_set_handle
 * description: BT 经典蓝牙名称 set/query 
 **************************************************************************/
int rdx_ble_server_bt_name_set_handle(u8 has_value, const char* in_name, char* out_name, u16 out_cap)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    int ret = 0;
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    if(!out_name || out_cap == 0) return -1;
    memset(out_name, 0, out_cap);

    if(has_value){
        if(!in_name || in_name[0] == '\0') return -1;
        size_t copy = strlen(in_name);
        if(copy >= out_cap) copy = out_cap - 1;
        memcpy(out_name, in_name, copy);
        out_name[copy] = '\0';

        ret = bt_modify_name((u8*)out_name);
        if(ret == 0){
            log_info("%s --> bt_modify_name fail \r", __func__);
            return -1;
        }
    }else{
        const char* cur = bt_get_local_name();
        if(cur){
            size_t copy = strlen(cur);
            if(copy >= out_cap) copy = out_cap - 1;
            memcpy(out_name, cur, copy);
            out_name[copy] = '\0';
        }
    }
    return 0;
}

/**************************************************************************
 * function: rdx_ble_server_ble_name_set_handle
 * description: BLE 名称 set/query
 **************************************************************************/
int rdx_ble_server_ble_name_set_handle(u8 has_value, const char* in_name, char* out_name, u16 out_cap)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    int ret = 0;
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    if(!out_name || out_cap == 0) return -1;
    memset(out_name, 0, out_cap);

    if(has_value){
        if(!in_name || in_name[0] == '\0') return -1;
        size_t copy = strlen(in_name);
        if(copy >= out_cap) copy = out_cap - 1;
        memcpy(out_name, in_name, copy);
        out_name[copy] = '\0';

        ret = rdx_ble_server_set_local_name((char*)out_name, (u8)strlen(out_name));
        if(ret <= 0){
            log_info("%s --> rdx_ble_server_set_local_name fail \r", __func__);
            return -1;
        }
    }else{
        const char* cur = rdx_ble_server_get_local_name();
        if(cur){
            size_t copy = strlen(cur);
            if(copy >= out_cap) copy = out_cap - 1;
            memcpy(out_name, cur, copy);
            out_name[copy] = '\0';
        }
    }
    return 0;
}

/**************************************************************************
 * function: rdx_ble_server_get_conn_handle
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
u16 rdx_ble_server_get_conn_handle(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    rdx_ble_rdx_transport_snapshot_t snapshot;

    return rdx_ble_server_rdx_transport_snapshot_capture(&snapshot) ?
           snapshot.con_handle : 0;
}

u8 rdx_ble_server_has_active_link(void)
{
    return rdx_ble_session_active_count() ? TRUE : FALSE;
}

/**************************************************************************
 * function: rdx_ble_server_set_conn_handle
 * description: 
 * param (u16) con
 * return (*)
 **************************************************************************/
void rdx_ble_server_set_conn_handle(u16 con)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    g_rdx_ble_server_info.ble_con_handle = con;
}

/**************************************************************************
 * function: rdx_ble_server_set_g_rdx_ble_server_info.ble_work_state
 * description: 
 * param (ble_state_e) state
 * return (*)
 **************************************************************************/
static void rdx_ble_server_set_ble_work_state(ble_state_e state)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    if (state != g_rdx_ble_server_info.ble_work_state) {
        log_info("ble_work_st:%x->%x\r", g_rdx_ble_server_info.ble_work_state, state);
        g_rdx_ble_server_info.ble_work_state = state;
    }
}

/**************************************************************************
 * function: rdx_ble_server_get_g_rdx_ble_server_info.ble_work_state
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
ble_state_e rdx_ble_server_get_ble_work_state(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    return g_rdx_ble_server_info.ble_work_state;
}

/**************************************************************************
 * function: rdx_ble_server_disconnect
 * description: 
 * param (void) *priv
 * return (*)
 **************************************************************************/
static int rdx_ble_server_disconnect(void *priv)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    y_printf("====== %s --> con_handle = %d \n", __func__, g_rdx_ble_server_info.ble_con_handle);
    if (g_rdx_ble_server_info.ble_con_handle) {
        //stop recording if is running.
        RecordStatus* rp = rdx_record_get_status();
        if(rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME){
            //disconnect actively, stop recording.
            log_info(">>>rdx ble disconnect, stop recording\r");
            rp->run = RECORD_STATE_STOP;
            //send job.
            rdx_record_process();
        }

        if (BLE_ST_SEND_DISCONN != g_rdx_ble_server_info.ble_work_state) {
            log_info(">>>rdx ble send disconnect\r");
            g_rdx_ble_server_info.ble_work_state = BLE_ST_SEND_DISCONN;
            ble_op_disconnect(g_rdx_ble_server_info.ble_con_handle);
        } else {
            log_info(">>>rdx ble wait disconnect...\n");
        }
        return 0;
    } else {
        return -1;
    }
}

/**************************************************************************
 * function: rdx_ble_server_connection_update_complete_success
 * description: 
 * param (u8) *packet
 * return (*)
 **************************************************************************/
static void rdx_ble_server_connection_update_complete_success(u8 *packet)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    int con_handle, conn_interval, conn_latency, conn_timeout;
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    con_handle = hci_subevent_le_connection_update_complete_get_connection_handle(packet);
    conn_interval = hci_subevent_le_connection_update_complete_get_conn_interval(packet);
    conn_latency = hci_subevent_le_connection_update_complete_get_conn_latency(packet);
    conn_timeout = hci_subevent_le_connection_update_complete_get_supervision_timeout(packet);

    log_info("get conn_interval = %d\n", conn_interval);
    log_info("get conn_latency = %d\n", conn_latency);
    log_info("get conn_timeout = %d\n", conn_timeout);
}

/**************************************************************************
 * function: rdx_ble_server_force_disconnect_timer_cb
 * description: 
 * param (void) *priv
 * return (*)
 **************************************************************************/
void rdx_ble_server_force_disconnect_timer_cb(void *priv)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    y_printf("---- %s ----> return \n", __func__);
    return;
    
    y_printf("====== %s --> \n", __func__);
    //delte force disconnect timer.
    rdx_ble_server_stop_force_disconnect_timer();
    
    //DO disnconnect ble.
    rdx_ble_server_disconnect(NULL);
}

/**************************************************************************
 * function: rdx_ble_server_start_force_disconnect_timer
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_ble_server_start_force_disconnect_timer(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    y_printf("====== %s \n", __func__);
    if(g_rdx_ble_server_info.force_disconnect_timer == 0){
        g_rdx_ble_server_info.force_disconnect_timer = sys_timeout_add(NULL, rdx_ble_server_force_disconnect_timer_cb, RDX_FORCE_DISCONNECT_TIMEOUT);
    }
}

/**************************************************************************
 * function: rdx_ble_server_stop_force_disconnect_timer
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_ble_server_stop_force_disconnect_timer(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    y_printf("====== %s \n", __func__);
    //delte force disconnect timer.
    if(g_rdx_ble_server_info.force_disconnect_timer){
        sys_timeout_del(g_rdx_ble_server_info.force_disconnect_timer);
        g_rdx_ble_server_info.force_disconnect_timer = 0;
    }
}

/**************************************************************************
 * function: rdx_ble_server_disconnected_delay_handle
 * description: 
 * param (void*) priv
 * return (*)
 **************************************************************************/
void rdx_ble_server_disconnected_delay_handle(void* priv)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    RdxWifiInfo* k = rdx_app_get_wifi_info();
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    /* A disconnect cleanup may run after the freed slot has already been
     * reused.  Never let that delayed callback clear the new RDX owner's
     * upload/bulk buffers.  The old session's synchronous detach path has
     * already invalidated its token; if a replacement owner is present, the
     * replacement owns all remaining runtime state. */
    if (rdx_ble_session_get_rdx_link()) {
        r_printf("[RDX_BLE_SESSION] skip stale delayed RDX cleanup: new owner active\r");
        return;
    }
    if (k->onoff == TRANSFER_BY_WIFI_ON) {
        r_printf("[BLE] disconnected_delay_handle: WiFi transfer active, skip file cleanup\n");
        return;
    }

    rdx_ble_server_rdx_session_reset_finalize();
    rdx_app_emmc_poweroff_check();
}

static void rdx_ble_server_rdx_session_abort(void)
{
    rdx_protocol_bleFileUpload_cancel();
    rdx_protocol_stop_loop_fileTransfer();
    rdx_protocol_recordFileData_sendFail_pending_stop();
    rdx_protocol_file_sync_busy_timer_stop();
    rdx_protocol_bulk_send_timer_stop();
    rdx_protocol_clear_send_confirm_flag();
}

static void rdx_ble_server_rdx_send_worker_quiesce(void)
{
    BLE_SendData *send_data = rdx_protocol_get_ble_send_data();
    BleBulkSendData *bulk_data = rdx_protocol_get_bulk_send_data();

    rdx_protocol_set_ble_sent(0);
    rdx_protocol_clear_send_confirm_flag();
    if (bulk_data) {
        bulk_data->bulk_flag = false;
    }
    if (send_data) {
        /* The immutable worker observes the cleared connection handle and
         * drains its old queue/context instead of waiting for CAN_SEND_NOW. */
        os_sem_post(&send_data->send_sem);
    }
}

static void rdx_ble_server_rdx_session_reset_finalize(void)
{
    rdx_protocol_bulk_data_send_para_reset();
    rdx_protocol_prepared_data_clean();
    rdx_protocol_uploadFileInfo_clean();
    rdx_uxfile_recordFileData_sendBuf_free();
    rdx_uxfile_datFileInfo_sendBuf_free();
    rdx_protocol_send_buffer_reinit();
}

static u8 rdx_ble_server_rdx_runtime_try_rearm(void)
{
    if (rdx_ble_session_rdx_runtime_state_get() !=
        RDX_BLE_RUNTIME_RESETTING) {
        return rdx_ble_session_rdx_runtime_state_get() ==
               RDX_BLE_RUNTIME_READY;
    }
    if (!rdx_app_rdx_rebind_is_idle()) {
        r_printf("[RDX_BLE_SESSION] runtime RESETTING: old workers still busy\r");
        return 0;
    }

    rdx_ble_server_rdx_session_reset_finalize();
    rdx_app_emmc_poweroff_check();
    if (!rdx_ble_session_rdx_runtime_rearm()) {
        rdx_ble_session_rdx_runtime_fail_closed();
        r_printf("[RDX_BLE_SESSION] runtime FAILED: peer identity unavailable\r");
        return 0;
    }
    r_printf("[RDX_BLE_SESSION] runtime READY: FIFO drained and workers idle\r");
    return 1;
}

u8 rdx_ble_server_rdx_lifecycle_barrier_match(const char *value)
{
    return (g_rdx_lifecycle_barrier_armed && value &&
            strcmp(value, g_rdx_lifecycle_barrier_value) == 0) ? 1 : 0;
}

void rdx_ble_server_rdx_lifecycle_barrier_complete(void)
{
    if (!g_rdx_lifecycle_barrier_armed ||
        !rdx_ble_session_rdx_runtime_barrier_arrive()) {
        r_printf("[RDX_BLE_SESSION] unexpected lifecycle barrier ignored\r");
        return;
    }

    g_rdx_lifecycle_barrier_armed = 0;
    memset(g_rdx_lifecycle_barrier_value, 0,
           sizeof(g_rdx_lifecycle_barrier_value));
    g_rdx_ble_server_info.ble_conn = FALSE;
    r_printf("[RDX_BLE_SESSION] receive FIFO drain barrier reached\r");

    /* Old receive packets may have started new work after the synchronous
     * disconnect cleanup. Abort once more on the FIFO consumer task before
     * testing the worker-idle boundary. */
    rdx_ble_server_rdx_session_abort();
    rdx_ble_server_rdx_runtime_try_rearm();
}
static void rdx_ble_server_link_disconnected_cleanup_internal(void)
{
    RdxWifiInfo* k = rdx_app_get_wifi_info();

    g_rdx_ble_server_info.ble_mtu_size = 0;
    if (k->onoff != TRANSFER_BY_WIFI_ON) {
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_BLE_DISCONNECTED);
    }
}

/**************************************************************************
 * FUNCTION
 *  rdx_ble_server_rdx_disconnected_cleanup_internal
 * DESCRIPTION
 *  Clean up RDX business state only when the current link activated RDX.
***************************************************************************/
static void rdx_ble_server_rdx_disconnected_cleanup_internal(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                              */
    /*----------------------------------------------------------------*/
    RecordStatus* rp = rdx_record_get_status();
    /*----------------------------------------------------------------*/
    /* Code Body                                                                  */
    /*----------------------------------------------------------------*/
    //stop force disconnect timer.
    rdx_ble_server_stop_force_disconnect_timer();  

    if(g_syn_data_timer) {
        sys_timeout_del(g_syn_data_timer);
        g_syn_data_timer = 0;
    }

    g_rdx_ble_server_info.ccc_configured = FALSE;
    g_rdx_ble_server_info.stream_tx_ready = FALSE;

    rdx_record_stream_interrupt();
    rdx_record_on_ble_conn_changed(false);

    //record stop.  //dons++ 20250326 离线录音时BLE断开后不停止录音
#if (RDX_AI_SEL_APP & APP_NINGQU_EN) || (RDX_AI_SEL_APP & APP_JMEASY_EN) || (RDX_AI_SEL_APP & APP_RAYCON_EN) || (RDX_AI_SEL_APP & APP_CDJY_EN) || (RDX_AI_SEL_APP & APP_BRANDWORKS_EN) || (RDX_AI_SEL_APP & APP_LYNSE_EN) || (RDX_AI_SEL_APP & APP_YYS_EN) || (RDX_AI_SEL_APP & APP_FINDAI_EN) || (RDX_AI_SEL_APP & APP_NEVIEW_EN) || (RDX_AI_SEL_APP & APP_SHENGLANG_EN) || (RDX_AI_SEL_APP & APP_BEANSTALK_EN) || (RDX_AI_SEL_APP & APP_ZENCHORD_EN) || (RDX_AI_SEL_APP & APP_CUSTOM_TEST_EN) || (RDX_AI_SEL_APP & APP_DEEPMINER_EN)
    // r_printf("====== %s --> orig_mode: %d, mode: %d \n", __func__, rp->orig_mode, rp->mode);
    if(rp->orig_mode != RECORD_MODE_OFFLINE){
        rp->mode = RECORD_MODE_OFFLINE;
        rp->orig_mode = RECORD_MODE_OFFLINE;
    }
#else
    r_printf("====== %s --> orig_mode: %d, mode: %d \n", __func__, rp->orig_mode, rp->mode);
    if(rp->orig_mode != RECORD_MODE_OFFLINE){
        //if not offline mode, stop recording.
        if(rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME){
            rp->run = RECORD_STATE_STOP;
        #if !(RDX_AI_SEL_APP & APP_TURING_EN)
            rp->rerun = true;
        #endif

            //time to restart.
            // sys_timeout_add(NULL, rdx_record_start, 2000);
            rdx_record_process();
        }
    }
#endif
#if (TCFG_USER_TWS_ENABLE && TCFG_APP_BT_EN) 
    //tws sibling ble connect status. 
    rdx_app_tws_bind_info_sync();
#endif

    //ota.
    if (get_ota_status()){
        rdx_ota_stop();
    }
    
    /* HOGP owns ready-drop release/timer cleanup before its link state clears. */
    rdx_hogp_keymap_config_on_disconnect();
}

/**************************************************************************
 * FUNCTION
 *  rdx_ble_server_disconnected_adv_restart
 * DESCRIPTION
 *  Restart advertising according to current RDX state.
 * PARAMETERS
 *  null
 * RETURNS
 *  null
***************************************************************************/
static void rdx_ble_server_disconnected_adv_restart(void)
{
    RdxWifiInfo* k = rdx_app_get_wifi_info();
    bool rdx_uxfile_sd_format_status_check(void);

    //adv restart.
    if(k->onoff == TRANSFER_BY_WIFI_ON){
        //wifi on, ble without adv.
        rdx_ble_server_adv_enable(0);
    }else{
        //power off, ble adv shut off.
        bool flag = rdx_app_get_dut_status();
        if(rdx_app_get_poweroff_flag() || flag == true || rdx_uxfile_sd_format_status_check()){
            //power off, shut down adv.
            rdx_ble_server_adv_enable(0);
        }else{
            //wifi off, ble with adv.
            rdx_ble_server_adv_data_changed();
        }
        if(!rdx_vm_is_unbouding()){
        #if (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_OLED) || (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_BOTH_OLED_EMMC)
            // OLED 功能已删除 // os_taskq_post_msg("oled_show_task", 1, OLED_SHOW_BLE_DISCONNECTED); 
        #endif
            r_printf("=== %s ---> do not show disconnect icon, unbounding now! \r", __FUNCTION__);
        }
    }
}

/* Connection Complete is broadcast to registered protocol callbacks. Starting
 * the idle wrapper's legacy advertiser from inside the first wrapper callback
 * lets that new advertiser observe the still-in-flight event and bind the same
 * connection handle. Defer until the complete callback fan-out has returned. */
static void rdx_ble_server_phase0a_connect_adv_restart_deferred(void *priv)
{
    (void)priv;
    g_rdx_ble_phase0a_connect_adv_timer = 0;

    if (!rdx_ble_server_phase0b_adv_token_is_current()) {
        r_printf("[RDX_BLE_LINK] stale connect ADV timer ignored\n");
        return;
    }
    if (rdx_ble_server_phase0a_connected_count() >=
            RDX_BLE_PHASE0A_WRAPPER_MAX ||
        !rdx_ble_server_phase0a_idle_wrapper_get() ||
        rdx_ble_server_broadcast_suppressed()) {
        r_printf("[RDX_BLE_LINK] delayed advertiser skipped connected=%u idle=%p suppressed=%u\n",
                 rdx_ble_server_phase0a_connected_count(),
                 rdx_ble_server_phase0a_idle_wrapper_get(),
                 rdx_ble_server_broadcast_suppressed());
        return;
    }

    r_printf("[RDX_BLE_LINK] connection fan-out done; advertise on idle wrapper\n");
    rdx_ble_server_adv_enable(1);
}

static void rdx_ble_server_phase0a_connect_adv_restart_schedule(void)
{
    void *idle_hdl = rdx_ble_server_phase0a_idle_wrapper_get();

    rdx_ble_server_phase0a_connect_adv_restart_cancel();
    rdx_ble_server_phase0b_adv_token_capture(idle_hdl);
    g_rdx_ble_phase0a_connect_adv_timer = sys_timeout_add(
        NULL,
        rdx_ble_server_phase0a_connect_adv_restart_deferred,
        RDX_BLE_PHASE0A_ADV_RESTART_DELAY_MS);
    if (!g_rdx_ble_phase0a_connect_adv_timer) {
        y_printf("[RDX_BLE_LINK] failed to defer idle-wrapper advertising\n");
    }
}

static void rdx_ble_server_phase0a_connect_adv_restart_cancel(void)
{
    if (g_rdx_ble_phase0a_connect_adv_timer) {
        sys_timeout_del(g_rdx_ble_phase0a_connect_adv_timer);
        g_rdx_ble_phase0a_connect_adv_timer = 0;
    }
}

/* The protocol packet handler runs before app_ble clears its wrapper-side
 * connection handle. Advertising from HCI_EVENT_DISCONNECTION_COMPLETE would
 * therefore race that cleanup and may be deferred forever. Restart from a
 * one-shot timer after the callback returns, with bounded cleanup retries. */
static void rdx_ble_server_disconnected_adv_restart_deferred(void *priv)
{
    (void)priv;
    g_disconnected_adv_restart_timer = 0;

    if (!rdx_ble_server_phase0b_adv_token_is_current()) {
        r_printf("[RDX_BLE_LINK] stale disconnect ADV timer ignored\n");
        g_disconnected_adv_restart_retry = 0;
        g_rdx_ble_phase0a_disconnect_pending_hdl = NULL;
        return;
    }
    if (!g_rdx_ble_phase0a_disconnect_pending_hdl) {
        g_disconnected_adv_restart_retry = 0;
        return;
    }
    if (app_ble_get_hdl_con_handle(
            g_rdx_ble_phase0a_disconnect_pending_hdl)) {
        if (g_disconnected_adv_restart_retry <
            RDX_DISCONNECT_ADV_RESTART_RETRY_MAX) {
            g_disconnected_adv_restart_retry++;
            g_disconnected_adv_restart_timer = sys_timeout_add(
                NULL,
                rdx_ble_server_disconnected_adv_restart_deferred,
                RDX_DISCONNECT_ADV_RESTART_DELAY_MS);
        } else {
            y_printf("[RDX_BLE_LINK] advertising restart timed out waiting for wrapper cleanup\n");
            g_disconnected_adv_restart_retry = 0;
            g_rdx_ble_phase0a_disconnect_pending_hdl = NULL;
        }
        return;
    }

    g_disconnected_adv_restart_retry = 0;
    g_rdx_ble_phase0a_disconnect_pending_hdl = NULL;
    g_rdx_ble_server_info.adv_refresh_pending = FALSE;
    if (rdx_ble_server_phase0a_connected_count() <
            RDX_BLE_PHASE0A_WRAPPER_MAX &&
        !rdx_ble_server_broadcast_suppressed()) {
        r_printf("[RDX_BLE_LINK] advertising restart on released wrapper\n");
        rdx_ble_server_adv_enable(1);
    }
    return;

    if (g_rdx_ble_server_info.rdx_ble_server_hdl == NULL) {
        g_rdx_ble_server_info.adv_refresh_pending = FALSE;
        g_disconnected_adv_restart_retry = 0;
        return;
    }

    /* A newer link supersedes this restart. The connection callback cancels
     * the timer too; this check closes the callback race window. */
    if (g_rdx_ble_server_info.ble_conn ||
        rdx_ble_session_get_link_state()->connected) {
        g_rdx_ble_server_info.adv_refresh_pending = FALSE;
        g_disconnected_adv_restart_retry = 0;
        return;
    }

    if (app_ble_get_hdl_con_handle(g_rdx_ble_server_info.rdx_ble_server_hdl)) {
        if (g_disconnected_adv_restart_retry < RDX_DISCONNECT_ADV_RESTART_RETRY_MAX) {
            g_disconnected_adv_restart_retry++;
            g_disconnected_adv_restart_timer = sys_timeout_add(
                NULL,
                rdx_ble_server_disconnected_adv_restart_deferred,
                RDX_DISCONNECT_ADV_RESTART_DELAY_MS);
            return;
        }

        y_printf("[BLE_SESSION] advertising restart timed out: wrapper connection still active\n");
        g_disconnected_adv_restart_retry = 0;
        return;
    }

    g_disconnected_adv_restart_retry = 0;
    g_rdx_ble_server_info.adv_refresh_pending = FALSE;
    r_printf("[BLE_SESSION] advertising restart after disconnect\n");
    rdx_ble_server_disconnected_adv_restart();
}

static void rdx_ble_server_disconnected_adv_restart_schedule(void)
{
    g_rdx_ble_server_info.adv_refresh_pending = TRUE;
    g_disconnected_adv_restart_retry = 0;

    if (g_disconnected_adv_restart_timer) {
        sys_timeout_del(g_disconnected_adv_restart_timer);
        g_disconnected_adv_restart_timer = 0;
    }

    g_disconnected_adv_restart_timer = sys_timeout_add(
        NULL,
        rdx_ble_server_disconnected_adv_restart_deferred,
        RDX_DISCONNECT_ADV_RESTART_DELAY_MS);
    if (!g_disconnected_adv_restart_timer) {
        y_printf("[BLE_SESSION] failed to schedule advertising restart\n");
    }
}

static void rdx_ble_server_disconnected_adv_restart_cancel(void)
{
    if (g_disconnected_adv_restart_timer) {
        sys_timeout_del(g_disconnected_adv_restart_timer);
        g_disconnected_adv_restart_timer = 0;
    }
    g_disconnected_adv_restart_retry = 0;
    g_rdx_ble_server_info.adv_refresh_pending = FALSE;
    g_rdx_ble_phase0a_disconnect_pending_hdl = NULL;
}

/* Resume the idle policy after link teardown and advertising recovery has
 * been scheduled. The radio restart may complete asynchronously, but the
 * disconnect must still re-arm auto-shutdown if broadcasting cannot recover.
 * Advertising identity is not an activity state: CONFIG and HOGP must obey
 * the same auto-shutdown policy.  Keep suppression policy centralized so a
 * disconnect during WiFi transfer, DUT, poweroff, or formatting cannot arm a
 * conflicting shutdown timer. */
static void rdx_ble_server_disconnected_idle_policy_resume(void)
{
    if (rdx_ble_server_broadcast_suppressed()) {
        y_printf("[POWEROFF] disconnect idle-policy resume suppressed\n");
        return;
    }

    rdx_ble_server_auto_shut_down_enable(1);
}

/**************************************************************************
 * FUNCTION
 *  rdx_ble_server_rdx_connected_handle
 * DESCRIPTION
 *  
 * PARAMETERS
 *  null
 * RETURNS
 *  null
**************************************************************************/
static void rdx_ble_server_rdx_connected_handle(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/ 
    rdx_app_emmc_poweroff_check_timer_stop();
    rdx_app_emmc_poweron(1);

    if(0 == rdx_app_get_power_ready_flag()){
        rdx_app_set_power_ready_flag();
    }

    //stop adv change timer.
    rdx_ble_server_adv_interval_change_timer_stop();

    //init connect state.
    rdx_protocol_send_buffer_reinit();

    //set connect flag.
    g_rdx_ble_server_info.ble_conn = TRUE;

    rdx_record_on_ble_conn_changed(true);

#if (TCFG_USER_TWS_ENABLE && TCFG_APP_BT_EN) 
    rdx_app_tws_bind_info_sync();
#endif

#if (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_OLED) || (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_BOTH_OLED_EMMC)
    // OLED 功能已删除 // os_taskq_post_msg("oled_show_task", 1, OLED_SHOW_BLE_CONNECTED); 
#endif

    //start force disconnect timer.
    rdx_ble_server_start_force_disconnect_timer();

    //LED控制：BLE连接后常亮1s后熄灭
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_BLE_CONNECTED);

#if (RDX_BJ_VERSION == BJ_BOARD_VERSION_02) || (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_EMMC) || (RDX_BJ_VERSION == BJ_BOARD_VERSION_03)
    RecordStatus* rp = rdx_record_get_status();
    if(rp->mode != RECORD_MODE_ONLINE){
        rp->mode = RECORD_MODE_ONLINE;
        if(rp->run == RECORD_STATE_STOP){
            rp->orig_mode = RECORD_MODE_ONLINE;
        }else{
            y_printf("offline recording now, do not change original record mode \r");
        }
    }
    rdx_record_stream_resume_delayed();
#endif
}

/* JL's BLE packet callback keeps the complete HCI event in packet, but its
 * size argument excludes the event-code byte.  Keep these limits in the
 * callback-size domain rather than using the full on-wire event lengths. */
#define RDX_LE_CONNECTION_COMPLETE_MIN_SIZE             20
#define RDX_LE_ENHANCED_CONNECTION_COMPLETE_MIN_SIZE    32
#define RDX_DISCONNECTION_COMPLETE_MIN_SIZE               5

static u8 rdx_ble_server_unified_link_connected(const u8 *packet,
                                                u16 size,
                                                u8 enhanced)
{
    u16 min_size = enhanced ?
                   RDX_LE_ENHANCED_CONNECTION_COMPLETE_MIN_SIZE :
                   RDX_LE_CONNECTION_COMPLETE_MIN_SIZE;
    u8 status;
    u16 con_handle;

    if (!packet || size < min_size) {
        r_printf("[BLE_SESSION] connection complete too short: %u/%u\n",
                 size, min_size);
        return 0;
    }

    if (enhanced) {
        status = hci_subevent_le_enhanced_connection_complete_get_status(packet);
        con_handle = hci_subevent_le_enhanced_connection_complete_get_connection_handle(packet);
    } else {
        status = hci_subevent_le_connection_complete_get_status(packet);
        con_handle = hci_subevent_le_connection_complete_get_connection_handle(packet);
    }
    if (status != 0) {
        r_printf("[BLE_SESSION] connection complete failed: status=0x%02x enhanced=%u\n",
                 status, enhanced);
        return 0;
    }

    rdx_ble_server_disconnected_adv_restart_cancel();
    rdx_ble_session_on_connected(con_handle);
    rdx_ble_server_set_conn_handle(con_handle);
    g_rdx_ble_server_info.ble_conn = TRUE;
    rdx_ble_server_auto_shut_down_enable(0);
    rdx_ble_server_set_ble_work_state(BLE_ST_CONNECT);
    rdx_ble_server_adv_interval_change_timer_stop();
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_BLE_CONNECTED);
    att_server_set_exchange_mtu(con_handle);
    r_printf("[BLE_SESSION] connected hdl=0x%04x enhanced=%u rdx=0 hid_ready=0\n",
             con_handle, enhanced);
    return 1;
}

static void rdx_ble_server_unified_link_disconnected(const u8 *packet,
                                                     u16 size)
{
    u16 disconnected_handle;
    u8 status;
    u8 reason;
    u8 rdx_active;

    if (!packet || size < RDX_DISCONNECTION_COMPLETE_MIN_SIZE) {
        r_printf("[BLE_SESSION] disconnection complete too short: %u\n", size);
        return;
    }

    disconnected_handle = hci_event_disconnection_complete_get_connection_handle(packet);
    status = hci_event_disconnection_complete_get_status(packet);
    reason = hci_event_disconnection_complete_get_reason(packet);
    if (!rdx_ble_session_is_current(disconnected_handle)) {
        r_printf("[BLE_SESSION] stale disconnect ignored: hdl=0x%04x current=0x%04x status=0x%02x reason=0x%02x\n",
                 disconnected_handle, g_rdx_ble_server_info.ble_con_handle,
                 status, reason);
        return;
    }

    rdx_active = rdx_ble_session_is_rdx_active(disconnected_handle);
    r_printf("[BLE_SESSION] disconnect hdl=0x%04x status=0x%02x reason=0x%02x rdx=%u hid_ready=%u\n",
             disconnected_handle, status, reason,
             rdx_active,
             rdx_hogp_keyboard_is_ready());

#if TCFG_RDX_HOGP_ENABLE
    rdx_hogp_on_disconnected(disconnected_handle);
#endif

    if (rdx_active) {
        rdx_ble_server_reset_send_fail_cnt();
        rdx_ble_server_rdx_disconnected_cleanup_internal();
    }
    rdx_ble_server_link_disconnected_cleanup_internal();

    rdx_ble_session_on_disconnected(disconnected_handle);
    rdx_ble_server_set_conn_handle(0);
    g_rdx_ble_server_info.ble_conn = FALSE;
    rdx_ble_server_set_ble_work_state(BLE_ST_DISCONN);
    rdx_ble_server_disconnected_adv_restart_schedule();
    rdx_ble_server_disconnected_idle_policy_resume();
}

static u8 rdx_ble_server_is_rdx_capability_handle(u16 att_handle)
{
    switch (att_handle) {
    case ATT_CHARACTERISTIC_06068D1C_6B97_11EF_B864_0241AC120002_01_VALUE_HANDLE:
    case ATT_CHARACTERISTIC_06068D2C_6B97_11EF_B864_0242AC120002_01_VALUE_HANDLE:
    case ATT_CHARACTERISTIC_06068D2C_6B97_11EF_B864_0242AC120002_01_CLIENT_CONFIGURATION_HANDLE:
    case ATT_CHARACTERISTIC_06068D3C_6B97_11EF_B864_0243AC120002_01_VALUE_HANDLE:
    case ATT_CHARACTERISTIC_00239A7F_C616_89BB_3374_F15AF588A7B3_01_VALUE_HANDLE:
    case ATT_CHARACTERISTIC_00239A8F_C616_89BB_3374_F25AF588A7B3_01_VALUE_HANDLE:
    case ATT_CHARACTERISTIC_00239A8F_C616_89BB_3374_F25AF588A7B3_01_CLIENT_CONFIGURATION_HANDLE:
        return 1;
    default:
        return 0;
    }
}

static u8 rdx_ble_server_rdx_attach(u16 con_handle)
{
    if (!rdx_ble_session_is_current(con_handle)) {
        y_printf("[RDX_SESSION] attach rejected: stale hdl=0x%04x current=0x%04x\n",
                 con_handle, g_rdx_ble_server_info.ble_con_handle);
        return 0;
    }
    if (rdx_ble_session_is_rdx_active(con_handle)) {
        return 1;
    }
    if (!rdx_ble_session_activate_rdx(con_handle)) {
        return 0;
    }

    rdx_ble_server_reset_send_fail_cnt();
    rdx_ble_server_rdx_connected_handle();
    r_printf("[RDX_SESSION] attached hdl=0x%04x\n", con_handle);
    return 1;
}

#if TCFG_RDX_HOGP_ENABLE
static u8 rdx_ble_server_hogp_attach(u16 con_handle)
{
    const rdx_ble_link_state_t *link_state;

    if (!rdx_ble_session_is_current(con_handle)) {
        y_printf("[HOGP] attach rejected: stale hdl=0x%04x current=0x%04x\n",
                 con_handle, g_rdx_ble_server_info.ble_con_handle);
        return 0;
    }
    if (rdx_hogp_keyboard_is_connected()) {
        return 1;
    }

    link_state = rdx_ble_session_get_link_state();
    rdx_hogp_on_connected(con_handle,
                          link_state ? link_state->encrypted : 0);
    return rdx_hogp_keyboard_is_connected();
}
#endif

/**************************************************************************
 * function:
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
u16 rdx_ble_server_get_mtu(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    rdx_ble_rdx_transport_snapshot_t snapshot;

    return rdx_ble_server_rdx_transport_snapshot_capture(&snapshot) ?
           snapshot.mtu_size : 0;
}

/**************************************************************************
 * function: rdx_ble_server_cbk_packet_handler
 * description: 
 * param (void) *hdl
 * param (uint8_t) packet_type
 * param (uint16_t) channel
 * param (uint8_t) *packet
 * param (uint16_t) size
 * return (*)
 **************************************************************************/

static void set_connection_data_phy(u16 con_handle, u8 tx_phy, u8 rx_phy)
{
    if (0 == con_handle) {
        return;
    }

    u8 all_phys = 0;
    u16 phy_options = CONN_SET_PHY_OPTIONS_S8;

    ble_op_set_ext_phy(con_handle, all_phys, tx_phy, rx_phy, phy_options);
}

static void rdx_ble_server_phase0a_link_state_capture(
    rdx_ble_link_state_t *link,
    const u8 *packet,
    u8 enhanced)
{
    bd_addr_t peer_addr;
    u8 peer_addr_type;
    u16 conn_interval;
    u16 conn_latency;
    u16 supervision_timeout;

    if (!link || !packet) {
        return;
    }
    if (enhanced) {
        peer_addr_type =
            hci_subevent_le_enhanced_connection_complete_get_peer_address_type(packet);
        hci_subevent_le_enhanced_connection_complete_get_peer_addresss(
            packet, peer_addr);
        conn_interval =
            hci_subevent_le_enhanced_connection_complete_get_conn_interval(packet);
        conn_latency =
            hci_subevent_le_enhanced_connection_complete_get_conn_latency(packet);
        supervision_timeout =
            hci_subevent_le_enhanced_connection_complete_get_supervision_timeout(packet);
    } else {
        peer_addr_type =
            hci_subevent_le_connection_complete_get_peer_address_type(packet);
        hci_subevent_le_connection_complete_get_peer_address(packet, peer_addr);
        conn_interval =
            hci_subevent_le_connection_complete_get_conn_interval(packet);
        conn_latency =
            hci_subevent_le_connection_complete_get_conn_latency(packet);
        supervision_timeout =
            hci_subevent_le_connection_complete_get_supervision_timeout(packet);
    }
    rdx_ble_session_link_set_peer(link, peer_addr_type, peer_addr);
    rdx_ble_session_link_set_conn_params(link, conn_interval, conn_latency,
                                         supervision_timeout);
}

static void rdx_ble_server_phase0a_link_conn_params_update(
    rdx_ble_link_state_t *link,
    const u8 *packet)
{
    if (!link || !packet) {
        return;
    }
    rdx_ble_session_link_set_conn_params(
        link,
        hci_subevent_le_connection_update_complete_get_conn_interval(packet),
        hci_subevent_le_connection_update_complete_get_conn_latency(packet),
        hci_subevent_le_connection_update_complete_get_supervision_timeout(packet));
}

static void rdx_ble_server_phase0a_link_connected(void *hdl,
                                                  const u8 *packet,
                                                  u16 size,
                                                  u8 enhanced)
{
    rdx_ble_link_state_t *link;
    u16 min_size = enhanced ?
                   RDX_LE_ENHANCED_CONNECTION_COMPLETE_MIN_SIZE :
                   RDX_LE_CONNECTION_COMPLETE_MIN_SIZE;
    u8 status;
    u8 wrapper_index;
    u16 con_handle;

    if (!packet || size < min_size) {
        r_printf("[RDX_BLE_LINK] connection complete too short: %u/%u\n",
                 size, min_size);
        return;
    }
    if (enhanced) {
        status = hci_subevent_le_enhanced_connection_complete_get_status(packet);
        con_handle = hci_subevent_le_enhanced_connection_complete_get_connection_handle(packet);
    } else {
        status = hci_subevent_le_connection_complete_get_status(packet);
        con_handle = hci_subevent_le_connection_complete_get_connection_handle(packet);
    }
    if (status != 0) {
        r_printf("[RDX_BLE_LINK] connection failed status=0x%02x enhanced=%u\n",
                 status, enhanced);
        return;
    }
    /* Only the wrapper that owned the active advertiser may accept this
     * Connection Complete. The SDK fans the HCI event out to every registered
     * wrapper, so connection-handle equality alone is not sufficient while
     * the event is still in flight. */
    if (hdl != g_rdx_ble_advertising_hdl) {
        r_printf("[RDX_BLE_LINK] connect ignored: wrapper=%u advertiser=%u event_con=0x%04x\n",
                 rdx_ble_server_phase0a_wrapper_index(hdl),
                 rdx_ble_server_phase0a_wrapper_index(
                     g_rdx_ble_advertising_hdl),
                 con_handle);
        rdx_ble_server_phase0a_wrapper_connection_clear(
            hdl, con_handle, "non-advertising wrapper saw Connection Complete");
        return;
    }
    if (!rdx_ble_server_phase0a_event_matches(hdl, con_handle, "connect")) {
        return;
    }

    wrapper_index = rdx_ble_server_phase0a_wrapper_index(hdl);
    link = rdx_ble_session_link_accept(hdl, con_handle);
    if (!link) {
        y_printf("[RDX_BLE_LINK] connect rejected: registry conflict wrapper=%u con=0x%04x\n",
                 wrapper_index, con_handle);
        return;
    }
    rdx_ble_server_phase0a_link_state_capture(link, packet, enhanced);
    multi_att_clear_ccc_config(con_handle);
    rdx_ble_server_disconnected_adv_restart_cancel();
    g_rdx_ble_phase0a_disconnect_pending_hdl = NULL;
    rdx_ble_server_adv_interval_change_timer_stop();
    r_printf("[RDX_BLE_LINK] connected_count=%u; defer idle-wrapper advertising\n",
             rdx_ble_server_phase0a_connected_count());
    rdx_ble_server_phase0a_connect_adv_restart_schedule();
}

static void rdx_ble_server_phase0a_link_disconnected(void *hdl,
                                                     const u8 *packet,
                                                     u16 size)
{
    rdx_ble_link_state_t *link;
    u16 con_handle;
    u8 status;
    u8 reason;

    if (!packet || size < RDX_DISCONNECTION_COMPLETE_MIN_SIZE) {
        r_printf("[RDX_BLE_LINK] disconnection complete too short: %u\n", size);
        return;
    }
    con_handle = hci_event_disconnection_complete_get_connection_handle(packet);
    status = hci_event_disconnection_complete_get_status(packet);
    reason = hci_event_disconnection_complete_get_reason(packet);
    if (!rdx_ble_server_phase0a_event_matches(hdl, con_handle, "disconnect")) {
        return;
    }
    link = rdx_ble_server_phase0b_link_find(hdl, con_handle);
    if (!link) {
        return;
    }

    r_printf("[RDX_BLE_LINK] disconnect accepted status=0x%02x reason=0x%02x\n",
             status, reason);
#if TCFG_RDX_HOGP_ENABLE
    if (rdx_ble_session_link_is_hid(link)) {
        /* Disconnection Complete is already too late for an ATT release
         * report.  HOGP only drops its local report/runtime state here. */
        rdx_hogp_on_disconnected(con_handle);
    }
#endif
    if (rdx_ble_session_link_is_rdx(link)) {
        rdx_ble_server_phase2_rdx_detach(link);
    }
    multi_att_clear_ccc_config(con_handle);
    rdx_ble_server_phase0a_connect_adv_restart_cancel();
    g_rdx_ble_phase0a_disconnect_pending_hdl = hdl;
    rdx_ble_session_link_release(hdl, con_handle);
    rdx_ble_server_phase0b_adv_token_capture(hdl);
    rdx_ble_server_disconnected_adv_restart_schedule();
}

static void rdx_ble_server_phase0a_packet_handler(void *hdl,
                                                  uint8_t packet_type,
                                                  uint16_t channel,
                                                  uint8_t *packet,
                                                  uint16_t size)
{
    u16 con_handle;

    (void)channel;
    if (packet_type != HCI_EVENT_PACKET || !packet) {
        return;
    }

    switch (hci_event_packet_get_type(packet)) {
    case ATT_EVENT_CAN_SEND_NOW:
        {
            rdx_codex_micro_on_can_send_now();
            rdx_ble_link_state_t *link =
                rdx_ble_server_phase0b_link_find(hdl,
                                                 app_ble_get_hdl_con_handle(hdl));
            if (!link || !rdx_ble_session_link_is_rdx(link)) {
                break;
            }
            if (!rdx_ble_server_rdx_send_pending_consume(link)) {
                r_printf("[RDX_BLE_TX] can_send_now ignored: no current pending token con=0x%04x\n",
                         link->con_handle);
                break;
            }
            {
                BleBulkSendData *bulk_send_data =
                    rdx_protocol_get_bulk_send_data();
                BLE_SendData *ble_send_data =
                    rdx_protocol_get_ble_send_data();

                rdx_protocol_set_ble_sent(0);
                rdx_protocol_clear_send_confirm_flag();
                if (bulk_send_data->bulk_flag == true) {
                    bulk_send_data->bulk_flag = false;
                }
                os_sem_post(&ble_send_data->send_sem);
                /* Normal high-rate success path; log rejected events only. */
            }
        }
        break;
    case HCI_EVENT_LE_META:
        switch (hci_event_le_meta_get_subevent_code(packet)) {
        case HCI_SUBEVENT_LE_ENHANCED_CONNECTION_COMPLETE:
            rdx_ble_server_phase0a_link_connected(hdl, packet, size, 1);
            break;
        case HCI_SUBEVENT_LE_CONNECTION_COMPLETE:
            rdx_ble_server_phase0a_link_connected(hdl, packet, size, 0);
            break;
        case HCI_SUBEVENT_LE_CONNECTION_UPDATE_COMPLETE:
            if (size >= 11) {
                con_handle = hci_subevent_le_connection_update_complete_get_connection_handle(packet);
                if (hci_subevent_le_connection_update_complete_get_status(packet) == 0 &&
                    rdx_ble_server_phase0a_event_matches(hdl, con_handle,
                                                         "connection_update")) {
                    rdx_ble_server_phase0a_link_conn_params_update(
                        rdx_ble_server_phase0b_link_find(hdl, con_handle), packet);
                }
            }
            break;
        default:
            break;
        }
        break;
    case HCI_EVENT_DISCONNECTION_COMPLETE:
        rdx_ble_server_phase0a_link_disconnected(hdl, packet, size);
        break;
    case HCI_EVENT_ENCRYPTION_CHANGE:
        con_handle = hci_event_encryption_change_get_connection_handle(packet);
        if (rdx_ble_server_phase0a_event_matches(hdl, con_handle,
                                                 "encryption_change")) {
            rdx_ble_link_state_t *link =
                rdx_ble_server_phase0b_link_find(hdl, con_handle);
            u8 status = hci_event_encryption_change_get_status(packet);
            u8 encrypted = status == 0 &&
                hci_event_encryption_change_get_encryption_enabled(packet);
            if (link) {
                rdx_ble_session_link_set_encrypted(link, encrypted);
                if (encrypted) {
                    bd_addr_t peer_identity = {0};

                    if (get_sm_peer_address(peer_identity)) {
                        rdx_ble_session_link_set_peer_identity(
                            link, peer_identity);
                    }
                }
                if (encrypted || status != 0) {
                    rdx_ble_session_link_set_hid_pairing_pending(link, 0);
                }
#if TCFG_RDX_HOGP_ENABLE
                if (rdx_ble_session_link_is_hid(link)) {
                    rdx_hogp_on_encryption_change(con_handle, encrypted,
                                                  status);
                } else if (encrypted &&
                           rdx_hogp_peer_has_persisted_subscription(
                               con_handle) &&
                           !rdx_ble_server_phase2_hid_attach(link)) {
                    rdx_hogp_on_encryption_change(con_handle, encrypted,
                                                  status);
                }
#endif
            }
            r_printf("[RDX_BLE_SEC] encryption=%u con=0x%04x\n",
                     encrypted, con_handle);
        }
        break;
    case ATT_EVENT_MTU_EXCHANGE_COMPLETE:
        con_handle = att_event_mtu_exchange_complete_get_handle(packet);
        if (rdx_ble_server_phase0a_event_matches(hdl, con_handle,
                                                 "mtu_exchange")) {
            rdx_ble_link_state_t *link =
                rdx_ble_server_phase0b_link_find(hdl, con_handle);
            u16 mtu = att_event_mtu_exchange_complete_get_MTU(packet) - 3;
            if (link) {
                rdx_ble_session_link_set_mtu(link, mtu);
                ble_op_multi_att_set_send_mtu(con_handle, mtu);
                r_printf("[RDX_BLE_LINK] mtu=%u con=0x%04x\n",
                         mtu, con_handle);
            }
        }
        break;
    default:
        break;
    }
}

static void rdx_ble_server_sm_event_callback(void *hdl, uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    rdx_ble_link_state_t *link;
    u16 con_handle;

    (void)channel;
    if (packet_type != HCI_EVENT_PACKET || !packet || !size) {
        return;
    }
    switch (hci_event_packet_get_type(packet)) {
    case SM_EVENT_JUST_WORKS_REQUEST:
        con_handle = sm_event_just_works_request_get_handle(packet);
        if (!rdx_ble_server_phase0a_event_matches(hdl, con_handle,
                                                   "sm_just_works")) {
            return;
        }
        link = rdx_ble_server_phase0b_link_find(hdl, con_handle);
        if (!link) {
            return;
        }
#if TCFG_RDX_HOGP_ENABLE
        if (rdx_ble_session_link_is_hid(link)) {
            rdx_hogp_on_sm_event(packet_type, packet, size);
            return;
        }
#endif
        if (rdx_ble_session_link_is_hid_pairing_pending(link)) {
            r_printf("[RDX_BLE_SEC] Just Works confirmed for HID candidate con=0x%04x\n",
                     con_handle);
            sm_just_works_confirm(con_handle);
            return;
        }
        if (!rdx_ble_session_get_hid_link()) {
            rdx_ble_session_link_set_hid_pairing_pending(link, 1);
            r_printf("[RDX_BLE_SEC] Just Works confirmed for provisional HID candidate con=0x%04x capability=0x%02x\n",
                     con_handle, link->capability);
            sm_just_works_confirm(con_handle);
            return;
        }
        r_printf("[RDX_BLE_SEC] Just Works rejected: no HID candidate con=0x%04x\n",
                 con_handle);
        break;
    default:
        r_printf("[RDX_BLE_SEC] sm_event wrapper=%u type=0x%02x size=%u observed\n",
                 rdx_ble_server_phase0a_wrapper_index(hdl),
                 hci_event_packet_get_type(packet), size);
        break;
    }
}

static void rdx_ble_server_cbk_packet_handler(void *hdl, uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    rdx_ble_server_phase0a_packet_handler(hdl, packet_type, channel,
                                          packet, size);
}

/**************************************************************************
 * function: rdx_ble_server_get_ble_characteristic_value
 * description: 
 * param (u8) *len
 * return (*)
 **************************************************************************/
static u8 *rdx_ble_server_get_ble_characteristic_value(u8 *len)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    *len = g_rdx_ble_server_info.ble_characteristic_value_len;
    return g_rdx_ble_server_info.ble_characteristic_value;
}

/**************************************************************************
 * function: rdx_ble_server_gatts_value_set
 * description: 
 * param (u8) *p_data
 * param (u8) length
 * return (*)
 **************************************************************************/
static int rdx_ble_server_gatts_value_set(u8 *p_data, u8 length)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    log_info("rdx_ble_gatts_value_set, len:%d", length);
    put_buf(p_data, length);
    g_rdx_ble_server_info.ble_characteristic_value_len = length;
    memset(g_rdx_ble_server_info.ble_characteristic_value, 0, sizeof(g_rdx_ble_server_info.ble_characteristic_value));
    memcpy(g_rdx_ble_server_info.ble_characteristic_value, p_data, g_rdx_ble_server_info.ble_characteristic_value_len);
    return 0;
}

/**************************************************************************
 * function: rdx_ble_server_att_read_callback
 * description: 
 * param (void) *hdl
 * param (hci_con_handle_t) connection_handle
 * param (uint16_t) att_handle
 * param (uint16_t) offset
 * param (uint8_t) *buffer
 * param (uint16_t) buffer_size
 * return (*)
 **************************************************************************/
static uint16_t rdx_ble_server_att_read_callback(void *hdl, hci_con_handle_t connection_handle, uint16_t att_handle, uint16_t offset, uint8_t *buffer, uint16_t buffer_size)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    uint16_t att_value_len = 0;
    uint16_t handle = att_handle;
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    if (!rdx_ble_server_phase0a_event_matches(hdl, connection_handle,
                                               "att_read")) {
        return 0;
    }
    r_printf("<-------------read_callback, handle= 0x%04x,buffer= %08x \r", handle, (u32)buffer);

    switch (handle) {
        case ATT_CHARACTERISTIC_2A00_01_VALUE_HANDLE:
            const char *gap_name = rdx_ble_server_get_local_name();//bt_get_local_name();
            att_value_len = strlen(gap_name);
            if ((offset >= att_value_len) || (offset + buffer_size) > att_value_len) {
                break;
            }
            if (buffer) {
                memcpy(buffer, &gap_name[offset], buffer_size);
                att_value_len = buffer_size;
                log_info("\n------read gap_name: %s \r", gap_name);
            }

            // att_value_len = 24;
            // if (buffer) {
            //     uint8_t *p_data = rdx_get_ble_characteristic_value(&att_value_len);
            //     memcpy(buffer, (void *)p_data, att_value_len);
            // }

            break;

        case ATT_CHARACTERISTIC_06068D3C_6B97_11EF_B864_0243AC120002_01_VALUE_HANDLE:
            {
            #if (RDX_AI_TRANSLATE_SUPPORT == 1)
                //readchar data read.
                char* p = rdx_app_earphone_get_readchardata();
                y_printf("%s --> readchardata: %s \r", __func__, p);
                att_value_len = BLE_READCHAR_INFO_SIZE;
                if (buffer) {
                    strncpy(buffer, p, BLE_READCHAR_INFO_SIZE);
                } 
            #else
                // if (buffer) {
                //     buffer[0] = att_get_ccc_config(handle);
                //     buffer[1] = 0;
                // }
                // att_value_len = 2;

                
                //readchar data read.
                char* p = rdx_app_earphone_get_readchardata();
                att_value_len = BLE_READCHAR_INFO_SIZE;
                if (buffer) {
                    strncpy((char*)buffer, p, BLE_READCHAR_INFO_SIZE);
                    y_printf("%s --> readchardata: %s \r", __func__, buffer);
                } 
            #endif
            }
            break;

        case ATT_CHARACTERISTIC_2A19_01_VALUE_HANDLE:
            {
                //battery read.
            #if 0
                DeviceBatInfo* pb = rdx_protocol_update_dev_battery_level();
                char d[30];
                memset(d, 0, 30);
                sprintf(d, "<%d,%d,%d>", pb->tbat_percent_L, pb->tbat_percent_R, pb->tbat_percent_C);
                att_value_len = strlen(d);
                y_printf("%s --> %s, att_value_len = %d \r", __func__, d, att_value_len);
                if(buffer){
                    strncpy(buffer, d, att_value_len);
                }
            #else
                u8 master_bat = rdx_battery_get_percent();//get_self_battery_level() * 10 + 10;
                if (master_bat > 100) {
                    master_bat = 100;
                }  
                y_printf("%s --> master_bat: %d \r", __func__, master_bat);

                att_value_len = 1;
                if (buffer) {
                    buffer[0] = master_bat;
                }   
            #endif  
            }
            break;

        case HID_PROTOCOL_MODE_VALUE_HANDLE:
        case HID_REPORT_MAP_VALUE_HANDLE:
        case HID_INFORMATION_VALUE_HANDLE:
        case HID_INPUT_REPORT_VALUE_HANDLE:
        case HID_OUTPUT_REPORT_VALUE_HANDLE:
        case HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE:
#if TCFG_RDX_CODEX_MICRO_MODE
        case HID_CODEX_INPUT_REPORT_VALUE_HANDLE:
        case HID_CODEX_OUTPUT_REPORT_VALUE_HANDLE:
        case HID_CODEX_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE:
#endif
#if TCFG_RDX_HOGP_ENABLE
            att_value_len = rdx_hogp_att_read(connection_handle, handle, offset, buffer, buffer_size);
            if (att_value_len) {
                y_printf("[HOGP] read hdl=0x%04x offset=%d len=%d\r", handle, offset, att_value_len);
            }
#endif
            break;
        default:
            break;
    }

    log_info("\natt_value_len = %d \r", att_value_len);
    return att_value_len;
}

/**************************************************************************
 * function: rdx_ble_server_gatt_receive_data
 * description: 
 * param (u8*) p_data
 * param (u16) len
 * return (*)
 **************************************************************************/
void rdx_ble_server_gatt_receive_data(u8* p_data, u16 len)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    int res = 0;
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    // y_printf("%s --> recieve data len = %d \r", __func__, len);
    // printf("------------------ before --------------\r");
    // mem_stats();
    
    // rdx_protocol_recieve_data_handle(p_data, len);
    rdx_protocol_packet_recv(p_data, len);
}

/**************************************************************************
 * function: rdx_ble_server_syn_data_after_ble_write_ready
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
static void rdx_ble_server_stream_tx_ready_cb(void* priv)
{
    rdx_ble_link_state_t *link;

    (void)priv;
    g_stream_tx_ready_timer = 0;
    link = g_syn_data_token_valid ?
           rdx_ble_session_rdx_token_resolve(&g_syn_data_token, 1) : NULL;
    if (!link || !link->rdx_ccc_configured) {
        g_syn_data_token_valid = 0;
        r_printf("[RDX_BLE_SESSION] stale stream-ready timer dropped\r");
        return;
    }
    link->rdx_stream_tx_ready = 1;
    g_rdx_ble_server_info.stream_tx_ready = TRUE;
    g_syn_data_token_valid = 0;
    y_printf("[BLE] Stream TX ready=%d (delayed after sync data)\r",
             g_rdx_ble_server_info.stream_tx_ready);
}

static void rdx_ble_server_syn_data_timers_cancel(void)
{
    if (g_syn_data_timer) {
        sys_timeout_del(g_syn_data_timer);
        g_syn_data_timer = 0;
    }
    if (g_stream_tx_ready_timer) {
        sys_timeout_del(g_stream_tx_ready_timer);
        g_stream_tx_ready_timer = 0;
    }
    g_syn_data_token_valid = 0;
}

void rdx_ble_server_syn_data_after_ble_write_ready(void* priv)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    RecordStatus* rp = rdx_record_get_status();
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    g_syn_data_timer = 0;

    if (!g_syn_data_token_valid ||
        !rdx_ble_session_rdx_token_resolve(&g_syn_data_token, 1)) {
        g_syn_data_token_valid = 0;
        r_printf("[RDX_BLE_SESSION] stale sync-data timer dropped\r");
        return;
    }

    y_printf("====== %s --> rp->run: %d, rp->mode: %d, rp->orig_mode: %d \r", __func__, rp->run, rp->mode, rp->orig_mode);

    if(rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME){
        y_printf("====== %s --> sync record state to app \r", __func__);
        rdx_protocol_record_state_indicate();
    } else {
        r_printf("====== %s --> record not running, skip sync. rp->run: %d \r", __func__, rp->run);
    }

    //check record mode.
    rdx_record_mode_active_check(0);

    g_stream_tx_ready_timer = sys_timeout_add(
        NULL, rdx_ble_server_stream_tx_ready_cb, 500);
    if (!g_stream_tx_ready_timer) {
        g_syn_data_token_valid = 0;
    }
}

static u8 rdx_ble_server_phase2_claim_to_att_error(
    rdx_ble_claim_result_t claim_result)
{
    return claim_result == RDX_BLE_CLAIM_OK ? 0 :
           RDX_BLE_PHASE0A_ATT_ERR_UNLIKELY_ERROR;
}

static u8 rdx_ble_server_phase2_rdx_attach(rdx_ble_link_state_t *link)
{
    rdx_ble_claim_result_t claim_result;
    u8 had_hid_owner;

    if (!link) {
        return RDX_BLE_PHASE0A_ATT_ERR_UNLIKELY_ERROR;
    }
    if (rdx_ble_session_rdx_runtime_state_get() ==
        RDX_BLE_RUNTIME_RESETTING) {
        rdx_ble_server_rdx_runtime_try_rearm();
    }
    if (rdx_ble_session_link_is_rdx(link) && link->rdx_runtime_active) {
        return 0;
    }
    had_hid_owner = rdx_ble_session_link_is_hid(link);
    claim_result = rdx_ble_session_claim_rdx(link, link->slot_generation);
    if (claim_result != RDX_BLE_CLAIM_OK) {
        y_printf("[RDX_BLE_SESSION] RDX claim rejected result=%u con=0x%04x\n",
                 claim_result, link->con_handle);
        return rdx_ble_server_phase2_claim_to_att_error(claim_result);
    }
    rdx_ble_server_set_conn_handle(link->con_handle);
    g_rdx_ble_server_info.ble_mtu_size = link->mtu_size;
    g_rdx_ble_server_info.ccc_configured = FALSE;
    g_rdx_ble_server_info.stream_tx_ready = FALSE;
    rdx_ble_server_reset_send_fail_cnt();
    rdx_ble_server_rdx_connected_handle();
    if (had_hid_owner) {
        r_printf("[RDX_BLE_SESSION] composite owner slot=%u con=0x%04x order=HID+RDX\n",
                 rdx_ble_session_link_index(link), link->con_handle);
    }
    r_printf("[RDX_BLE_SESSION] runtime ACTIVE con=0x%04x hdl=%p epoch=%u\n",
              link->con_handle, link->ble_hdl,
              rdx_ble_session_rdx_runtime_epoch_get());
    return 0;
}

static void rdx_ble_server_phase2_rdx_detach(rdx_ble_link_state_t *link)
{
    char barrier_packet[RDX_LIFECYCLE_BARRIER_PACKET_SIZE];
    int barrier_packet_len;

    if (!link || !rdx_ble_session_link_is_rdx(link)) {
        return;
    }
    if (!rdx_ble_session_rdx_runtime_begin_quiesce(link)) {
        return;
    }
    rdx_ble_server_syn_data_timers_cancel();
    rdx_ble_server_rdx_send_pending_reset();
    rdx_ble_server_reset_send_fail_cnt();
    rdx_ble_server_rdx_disconnected_cleanup_internal();
    if (g_rdx_ble_server_info.ble_con_handle == link->con_handle) {
        rdx_ble_server_set_conn_handle(0);
        g_rdx_ble_server_info.ble_conn = FALSE;
        g_rdx_ble_server_info.ccc_configured = FALSE;
        g_rdx_ble_server_info.stream_tx_ready = FALSE;
    }
    rdx_ble_server_rdx_session_abort();
    rdx_ble_server_rdx_send_worker_quiesce();

    sprintf(g_rdx_lifecycle_barrier_value, "%08x%08x",
            (unsigned int)rand32(), (unsigned int)rand32());
    sprintf(barrier_packet, "*APP#custom#%s#%s#",
            RDX_LIFECYCLE_CUSTOM_CMD,
            g_rdx_lifecycle_barrier_value);
    barrier_packet_len = strlen(barrier_packet) + 1;
    g_rdx_lifecycle_barrier_armed = 1;
    if (rdx_protocol_packet_recv(barrier_packet, barrier_packet_len) !=
        barrier_packet_len) {
        g_rdx_lifecycle_barrier_armed = 0;
        memset(g_rdx_lifecycle_barrier_value, 0,
               sizeof(g_rdx_lifecycle_barrier_value));
        rdx_ble_session_rdx_runtime_fail_closed();
        r_printf("[RDX_BLE_SESSION] runtime FAILED: drain barrier enqueue failed\r");
        return;
    }
    r_printf("[RDX_BLE_SESSION] runtime QUIESCING con=0x%04x hdl=%p epoch=%u; drain barrier queued\n",
             link->con_handle, link->ble_hdl,
             rdx_ble_session_rdx_runtime_epoch_get());
}

#if TCFG_RDX_HOGP_ENABLE
static u8 rdx_ble_server_phase2_hid_attach(rdx_ble_link_state_t *link)
{
    rdx_ble_claim_result_t claim_result;
    u8 had_hid_owner;
    u8 had_rdx_owner;

    if (!link) {
        return RDX_BLE_PHASE0A_ATT_ERR_UNLIKELY_ERROR;
    }
    had_hid_owner = rdx_ble_session_link_is_hid(link);
    had_rdx_owner = rdx_ble_session_link_is_rdx(link);
    claim_result = rdx_ble_session_claim_hid(link, link->slot_generation);
    if (claim_result != RDX_BLE_CLAIM_OK) {
        y_printf("[RDX_BLE_HID] claim rejected result=%u con=0x%04x\n",
                 claim_result, link->con_handle);
        return rdx_ble_server_phase2_claim_to_att_error(claim_result);
    }
    if (!rdx_hogp_keyboard_is_connected()) {
        rdx_hogp_on_connected_with_hdl(link->ble_hdl, link->con_handle,
                                       link->encrypted);
        r_printf("[RDX_BLE_HID] owner attached con=0x%04x hdl=%p\n",
                 link->con_handle, link->ble_hdl);
    }
    if (had_rdx_owner && !had_hid_owner &&
        rdx_hogp_keyboard_is_connected()) {
        r_printf("[RDX_BLE_SESSION] composite owner slot=%u con=0x%04x order=RDX+HID\n",
                 rdx_ble_session_link_index(link), link->con_handle);
    }
    return rdx_hogp_keyboard_is_connected() ? 0 :
           RDX_BLE_PHASE0A_ATT_ERR_UNLIKELY_ERROR;
}
#endif

static int rdx_ble_server_phase2_rdx_write(
    rdx_ble_link_state_t *link,
    u16 att_handle,
    u16 offset,
    u8 *buffer,
    u16 buffer_size)
{
    u16 cfg;
    u8 attach_error;

    if (!link || offset != 0 || !buffer) {
        return RDX_BLE_PHASE0A_ATT_ERR_UNLIKELY_ERROR;
    }
    if (att_handle ==
            ATT_CHARACTERISTIC_06068D2C_6B97_11EF_B864_0242AC120002_01_CLIENT_CONFIGURATION_HANDLE ||
        att_handle ==
            ATT_CHARACTERISTIC_00239A8F_C616_89BB_3374_F25AF588A7B3_01_CLIENT_CONFIGURATION_HANDLE) {
        if (buffer_size != 2) {
            return RDX_BLE_PHASE0A_ATT_ERR_INVALID_VALUE_LEN;
        }
        cfg = buffer[0] | (buffer[1] << 8);
        if (cfg != 0x0000 && cfg != 0x0001) {
            return RDX_BLE_PHASE0A_ATT_ERR_VALUE_NOT_ALLOWED;
        }
        if (cfg == 0x0000) {
            multi_att_set_ccc_config(link->con_handle, att_handle, cfg);
            if (att_handle ==
                    ATT_CHARACTERISTIC_06068D2C_6B97_11EF_B864_0242AC120002_01_CLIENT_CONFIGURATION_HANDLE &&
                rdx_ble_session_link_is_rdx(link)) {
                rdx_ble_server_syn_data_timers_cancel();
                link->rdx_ccc_configured = 0;
                link->rdx_stream_tx_ready = 0;
                g_rdx_ble_server_info.ccc_configured = FALSE;
                g_rdx_ble_server_info.stream_tx_ready = FALSE;
            }
            return 0;
        }
    }

    switch (att_handle) {
    case ATT_CHARACTERISTIC_06068D1C_6B97_11EF_B864_0241AC120002_01_VALUE_HANDLE:
        attach_error = rdx_ble_server_phase2_rdx_attach(link);
        if (attach_error) {
            return attach_error;
        }
        rdx_ble_server_gatt_receive_data(buffer, buffer_size);
        return 0;
    case ATT_CHARACTERISTIC_00239A7F_C616_89BB_3374_F15AF588A7B3_01_VALUE_HANDLE:
        attach_error = rdx_ble_server_phase2_rdx_attach(link);
        if (attach_error) {
            return attach_error;
        }
        rdx_protocol_ota_handle(buffer, buffer_size);
        return 0;
    case ATT_CHARACTERISTIC_06068D2C_6B97_11EF_B864_0242AC120002_01_CLIENT_CONFIGURATION_HANDLE:
        attach_error = rdx_ble_server_phase2_rdx_attach(link);
        if (attach_error) {
            return attach_error;
        }
        multi_att_set_ccc_config(link->con_handle, att_handle, cfg);
        link->rdx_ccc_configured = 1;
        g_rdx_ble_server_info.ccc_configured = TRUE;
        g_rdx_ble_server_info.stream_tx_ready = FALSE;
        rdx_ble_server_syn_data_timers_cancel();
        if (rdx_ble_session_rdx_token_capture(&g_syn_data_token, 1)) {
            g_syn_data_token_valid = 1;
            g_syn_data_timer = sys_timeout_add(
                NULL, rdx_ble_server_syn_data_after_ble_write_ready, 1000);
            if (!g_syn_data_timer) {
                g_syn_data_token_valid = 0;
            }
        }
        return 0;
    case ATT_CHARACTERISTIC_00239A8F_C616_89BB_3374_F25AF588A7B3_01_CLIENT_CONFIGURATION_HANDLE:
        attach_error = rdx_ble_server_phase2_rdx_attach(link);
        if (attach_error) {
            return attach_error;
        }
        multi_att_set_ccc_config(link->con_handle, att_handle, cfg);
        return 0;
    default:
        return RDX_BLE_PHASE0A_ATT_ERR_UNLIKELY_ERROR;
    }
}

/* Phase 0A still exposes the production composite database. A Windows HID
 * host must therefore be able to complete the mandatory HOGP control-plane
 * writes even though capability ownership, report sending, persistence and
 * the HOGP business singleton remain detached until later phases. */
static int rdx_ble_server_phase0a_hogp_control_write(
    void *hdl,
    hci_con_handle_t connection_handle,
    u16 att_handle,
    u16 offset,
    u8 *buffer,
    u16 buffer_size)
{
    rdx_ble_link_state_t *link =
        rdx_ble_server_phase0b_link_find(hdl, connection_handle);
    u8 encrypted = link ? link->encrypted : 0;

    if (!link) {
        return RDX_BLE_PHASE0A_ATT_ERR_UNLIKELY_ERROR;
    }
    if (offset != 0) {
        return RDX_BLE_PHASE0A_ATT_ERR_INVALID_OFFSET;
    }
    if (!buffer) {
        return RDX_BLE_PHASE0A_ATT_ERR_INVALID_VALUE_LEN;
    }

    switch (att_handle) {
    case ATT_CHARACTERISTIC_2A19_01_CLIENT_CONFIGURATION_HANDLE: {
        u16 cfg;
        u8 battery;
        if (buffer_size != 2) {
            return RDX_BLE_PHASE0A_ATT_ERR_INVALID_VALUE_LEN;
        }
        cfg = buffer[0] | (buffer[1] << 8);
        if (cfg != 0x0000 && cfg != 0x0001) {
            return RDX_BLE_PHASE0A_ATT_ERR_VALUE_NOT_ALLOWED;
        }
        multi_att_set_ccc_config(connection_handle, att_handle, cfg);
        r_printf("[RDX_BLE_GATT] Battery CCC cfg=0x%04x con=0x%04x\n",
                 cfg, connection_handle);
        if (cfg == 0x0001) {
            battery = rdx_battery_get_percent();
            if (battery > 100) {
                battery = 100;
            }
            int send_ret = ble_op_multi_att_send_data(
                connection_handle,
                ATT_CHARACTERISTIC_2A19_01_VALUE_HANDLE,
                &battery, sizeof(battery), ATT_OP_AUTO_READ_CCC);
            r_printf("[RDX_BLE_GATT] Battery test notify con=0x%04x ret=%d\n",
                     connection_handle, send_ret);
        }
        return 0;
    }
    case HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE: {
        u16 cfg;
        if (buffer_size != 2) {
            return RDX_BLE_PHASE0A_ATT_ERR_INVALID_VALUE_LEN;
        }
        cfg = buffer[0] | (buffer[1] << 8);
        if (cfg != 0x0000 && cfg != 0x0001) {
            return RDX_BLE_PHASE0A_ATT_ERR_VALUE_NOT_ALLOWED;
        }
#if RDX_HOGP_ENCRYPTION_REQUIRED
        if (cfg == 0x0001 && !encrypted) {
            r_printf("[RDX_BLE_HID] CCC pairing requested con=0x%04x\n",
                     connection_handle);
            sm_api_request_pairing(connection_handle);
            return RDX_BLE_PHASE0A_ATT_ERR_INSUFFICIENT_ENCRYPTION;
        }
#endif
        multi_att_set_ccc_config(connection_handle, att_handle, cfg);
        r_printf("[RDX_BLE_HID] CCC control-plane cfg=0x%04x con=0x%04x (no claim)\n",
                 cfg, connection_handle);
        return 0;
    }
#if TCFG_RDX_CODEX_MICRO_MODE
    case HID_CODEX_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE: {
        u16 cfg;
        if (buffer_size != 2) {
            return RDX_BLE_PHASE0A_ATT_ERR_INVALID_VALUE_LEN;
        }
        cfg = buffer[0] | (buffer[1] << 8);
        if (cfg != 0x0000 && cfg != 0x0001) {
            return RDX_BLE_PHASE0A_ATT_ERR_VALUE_NOT_ALLOWED;
        }
#if RDX_HOGP_ENCRYPTION_REQUIRED
        if (cfg == 0x0001 && !encrypted) {
            sm_api_request_pairing(connection_handle);
            return RDX_BLE_PHASE0A_ATT_ERR_INSUFFICIENT_ENCRYPTION;
        }
#endif
        multi_att_set_ccc_config(connection_handle, att_handle, cfg);
        return 0;
    }
    case HID_CODEX_OUTPUT_REPORT_VALUE_HANDLE:
        if (buffer_size != RDX_CODEX_MICRO_REPORT_BODY_LEN) {
            return RDX_BLE_PHASE0A_ATT_ERR_INVALID_VALUE_LEN;
        }
        break;
#endif
    case HID_PROTOCOL_MODE_VALUE_HANDLE:
        if (buffer_size != 1) {
            return RDX_BLE_PHASE0A_ATT_ERR_INVALID_VALUE_LEN;
        }
        if (buffer[0] != 1) {
            return RDX_BLE_PHASE0A_ATT_ERR_VALUE_NOT_ALLOWED;
        }
        break;
    case HID_CONTROL_POINT_VALUE_HANDLE:
        if (buffer_size != 1) {
            return RDX_BLE_PHASE0A_ATT_ERR_INVALID_VALUE_LEN;
        }
        if (buffer[0] != 0 && buffer[0] != 1) {
            return RDX_BLE_PHASE0A_ATT_ERR_VALUE_NOT_ALLOWED;
        }
        break;
    case HID_OUTPUT_REPORT_VALUE_HANDLE:
        if (buffer_size != 1) {
            return RDX_BLE_PHASE0A_ATT_ERR_INVALID_VALUE_LEN;
        }
        break;
    default:
        return RDX_BLE_PHASE0A_ATT_ERR_UNLIKELY_ERROR;
    }

#if RDX_HOGP_ENCRYPTION_REQUIRED
    if (!encrypted) {
        return RDX_BLE_PHASE0A_ATT_ERR_INSUFFICIENT_ENCRYPTION;
    }
#endif
    r_printf("[RDX_BLE_HID] control-plane write accepted att=0x%04x con=0x%04x (no claim)\n",
             att_handle, connection_handle);
    return 0;
}

/**************************************************************************
 * function: rdx_ble_server_att_write_callback
 * description:
 * param (void) *hdl
 * param (hci_con_handle_t) connection_handle
 * param (uint16_t) att_handle
 * param (uint16_t) transaction_mode
 * param (uint16_t) offset
 * param (uint8_t) *buffer
 * param (uint16_t) buffer_size
 * return (*)
 **************************************************************************/
static int rdx_ble_server_att_write_callback(void *hdl, hci_con_handle_t connection_handle, uint16_t att_handle, uint16_t transaction_mode, uint16_t offset, uint8_t *buffer, uint16_t buffer_size)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    int result = 0;
    u16 tmp16;
    u16 handle = att_handle;
    rdx_ble_link_state_t *link;
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    link = rdx_ble_server_phase0b_link_find(hdl, connection_handle);

    (void)tmp16;
    (void)transaction_mode;
    if (!rdx_ble_server_phase0a_event_matches(hdl, connection_handle,
                                               "att_write")) {
        return RDX_BLE_PHASE0A_ATT_ERR_UNLIKELY_ERROR;
    }
#if TCFG_RDX_HOGP_ENABLE
    if (handle == ATT_CHARACTERISTIC_2A19_01_CLIENT_CONFIGURATION_HANDLE) {
        return rdx_ble_server_phase0a_hogp_control_write(
            hdl, connection_handle, handle, offset, buffer, buffer_size);
    }
    if (handle >= HID_SERVICE_START_HANDLE &&
        handle <= HID_SERVICE_END_HANDLE) {
        u16 cfg = (buffer && buffer_size == 2) ?
                  (buffer[0] | (buffer[1] << 8)) : 0xffff;

        if ((handle == HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE
#if TCFG_RDX_CODEX_MICRO_MODE
             || handle == HID_CODEX_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE
#endif
            ) &&
            cfg == 0x0000) {
            if (rdx_ble_session_link_is_hid(link)) {
                return rdx_hogp_att_write(connection_handle, handle,
                                          transaction_mode, offset,
                                          buffer, buffer_size);
            }
            multi_att_set_ccc_config(connection_handle, handle, 0);
            return 0;
        }
        if ((handle == HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE
#if TCFG_RDX_CODEX_MICRO_MODE
             || handle == HID_CODEX_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE
#endif
            ) &&
            cfg == 0x0001) {
            if (!link || !link->encrypted) {
                if (link) {
                    rdx_ble_session_link_set_hid_pairing_pending(link, 1);
                }
                r_printf("[RDX_BLE_HID] CCC pairing requested con=0x%04x (no claim)\n",
                         connection_handle);
                sm_api_request_pairing(connection_handle);
                return RDX_BLE_PHASE0A_ATT_ERR_INSUFFICIENT_ENCRYPTION;
            }
            rdx_ble_session_link_set_hid_pairing_pending(link, 0);
            result = rdx_ble_server_phase2_hid_attach(link);
            if (result) {
                return result;
            }
#if TCFG_RDX_CODEX_MICRO_MODE
        } else if (handle == HID_CODEX_OUTPUT_REPORT_VALUE_HANDLE &&
                   !rdx_ble_session_link_is_hid(link)) {
            return RDX_BLE_PHASE0A_ATT_ERR_UNLIKELY_ERROR;
#endif
        } else if (!rdx_ble_session_link_is_hid(link)) {
            /* Windows may write Protocol Mode before it enables Input CCC.
             * Preserve the enumeration control plane without claiming HID or
             * mutating the HOGP singleton. */
            return rdx_ble_server_phase0a_hogp_control_write(
                hdl, connection_handle, handle, offset, buffer, buffer_size);
        }
        return rdx_hogp_att_write(connection_handle, handle,
                                  transaction_mode, offset,
                                  buffer, buffer_size);
    }
#else
    (void)offset;
    (void)buffer;
    (void)buffer_size;
#endif
    if (handle == ATT_CHARACTERISTIC_06068D1C_6B97_11EF_B864_0241AC120002_01_VALUE_HANDLE ||
        handle == ATT_CHARACTERISTIC_06068D2C_6B97_11EF_B864_0242AC120002_01_CLIENT_CONFIGURATION_HANDLE ||
        handle == ATT_CHARACTERISTIC_00239A7F_C616_89BB_3374_F15AF588A7B3_01_VALUE_HANDLE ||
        handle == ATT_CHARACTERISTIC_00239A8F_C616_89BB_3374_F25AF588A7B3_01_CLIENT_CONFIGURATION_HANDLE) {
        return rdx_ble_server_phase2_rdx_write(
            rdx_ble_server_phase0b_link_find(hdl, connection_handle),
            handle, offset, buffer, buffer_size);
    }
    r_printf("[RDX_BLE_GATT] write rejected att=0x%04x\n", handle);
    return RDX_BLE_PHASE0A_ATT_ERR_UNLIKELY_ERROR;
}

static u8 rdx_ble_server_adv_append_data(u8 *adv_data,
                                         u8 *offset,
                                         u8 eir_type,
                                         const void *data,
                                         u8 data_len)
{
    if (!adv_data || !offset || !data ||
        (u16)(*offset) + 2 + data_len > ADV_RSP_PACKET_MAX) {
        return 0;
    }
    *offset += make_eir_packet_data(&adv_data[*offset], *offset,
                                    eir_type, (void *)data, data_len);
    return 1;
}

/**************************************************************************
 * function: rdx_ble_server_fill_adv_data
 * description:
 * param (u8) *adv_data
 * return (*)
 **************************************************************************/
static u8 rdx_ble_server_fill_adv_data(u8 *adv_data)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    u8 offset = 0;
    const char *name_p = rdx_ble_server_get_local_name();
    u8 name_len = (u8)strlen(name_p);
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    const u8 flags[] = {0x0A};
#if TCFG_RDX_HOGP_ENABLE
    const u8 appearance[] = {
        (u8)(BLE_APPEARANCE_GENERIC_HID & 0xff),
        (u8)(BLE_APPEARANCE_GENERIC_HID >> 8)
    };
#endif
    u8 name_type = HCI_EIR_DATATYPE_COMPLETE_LOCAL_NAME;
    u8 name_capacity;

    if (!rdx_ble_server_adv_append_data(adv_data, &offset,
                                        HCI_EIR_DATATYPE_FLAGS,
                                        flags, sizeof(flags))) {
        return 0;
    }

    name_capacity = ADV_RSP_PACKET_MAX - offset - 2;
    if (name_len > name_capacity) {
        name_len = name_capacity;
        name_type = HCI_EIR_DATATYPE_SHORTENED_LOCAL_NAME;
    }
    if (!name_len ||
        !rdx_ble_server_adv_append_data(adv_data, &offset,
                                        name_type, name_p, name_len)) {
        return 0;
    }
#if TCFG_RDX_HOGP_ENABLE
    if (!rdx_ble_server_adv_append_data(adv_data, &offset,
                                        HCI_EIR_DATATYPE_APPEARANCE_DATA,
                                        appearance, sizeof(appearance))) {
        return 0;
    }
#endif

    if (offset > ADV_RSP_PACKET_MAX) {
        r_printf("***adv_data overflow!!!!!!\n");
        return 0;
    }
    return offset;
}

/**************************************************************************
 * function: rdx_ble_server_fill_rsp_data
 * description: 
 * param (u8) *rsp_data
 * return (*)
 **************************************************************************/
static u8 rdx_ble_server_fill_rsp_data(u8 *rsp_data)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    u8 offset = 0;
    u8 ble_mac[RDX_MAC_LEN];
    u8 tmp_ble_addr[RDX_MAC_LEN];
    u8 manu_data[MANUFAC_DATA_LENGTH] = {0};
    u8 hd_code[PRODUCT_CODE_SIZE] = PRODUCT_CODE;
    u8 fac[FACTORY_CODE_SIZE] = FACTORY_CODE;
    u8 len = 0;
    u8 bd = rdx_vm_get_bound_status();
#if TCFG_RDX_HOGP_ENABLE
    const u8 hid_uuid[] = {0x12, 0x18};
#endif
#if (RDX_AI_TRANSLATE_SUPPORT == 1)
    AImodeInfo* pm = rdx_app_get_AI_mode_info();
#endif
    u32 ability = RDX_DEVICE_ABILITY;
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    memset(ble_mac, 0, RDX_MAC_LEN);
    // rdx_ble_server_get_ble_mac(ble_mac);
    le_controller_get_mac(ble_mac);
    r_printf("%s -->ble mac: %02X:%02X:%02X:%02X:%02X:%02X \r", __FUNCTION__, ble_mac[5], ble_mac[4], ble_mac[3], ble_mac[2], ble_mac[1], ble_mac[0]);
    //pack mannufacture data.
    memset(manu_data, 0, sizeof(manu_data));
    //hardware code.
    strncpy((char*)manu_data, (char*)hd_code, PRODUCT_CODE_SIZE);
    len += PRODUCT_CODE_SIZE;
    //ble mac.
    memset(tmp_ble_addr, 0, RDX_MAC_LEN);
    memcpy(tmp_ble_addr, ble_mac, RDX_MAC_LEN);
    rdx_util_reverse_byte(tmp_ble_addr, RDX_MAC_LEN);
    memcpy(manu_data + len, tmp_ble_addr, RDX_MAC_LEN);
    len += RDX_MAC_LEN;
    //fac code.
    memcpy(manu_data + len, fac, FACTORY_CODE_SIZE);
    len += FACTORY_CODE_SIZE;
#if (RDX_AI_TRANSLATE_SUPPORT == 1)
    //AI mode & bound/unbound.
    manu_data[len++] = 0x00; //pm->ai_mode << 7 & 0x80;
    // y_printf("****** ai_mode = %d, manu_data[len] = 0x%02X, manu_len = %d \r", pm->ai_mode, manu_data[len - 1], len);
#else
    if(bd == 0){
        manu_data[len++] &= bd << ADV_MODE_BIT_MASK_BOUND;
    }else{
        manu_data[len++] |= bd << ADV_MODE_BIT_MASK_BOUND;
    }
    y_printf("****** bd = %d, manu_data[len] = 0x%02X, manu_len = %d, ability = %08X \r", bd, manu_data[len], len + 1, ability);
#endif
    // Add ability to manu_data
    manu_data[len++] = GET_INT_BYTE1(ability);
    manu_data[len++] = GET_INT_BYTE2(ability);
    manu_data[len++] = GET_INT_BYTE3(ability);
    manu_data[len++] = GET_INT_BYTE4(ability);
    // protocol version.
#if (BLE_FILE_TRANSFER_PACK_SIZE == BLE_SEND_SINGLE_PACK_SIZE)
    manu_data[len++] = rdx_protocol_get_version();
#endif
    // Add protocol type.
    memcpy(manu_data + len, PRODUCT_TYPE, 2);
    len += 2;
    // Add self mark.
    memcpy(manu_data + len, RDX_SELF_MARK, 2);
    len += 2;
    
    offset += make_eir_packet_data(&rsp_data[offset], offset, HCI_EIR_DATATYPE_MANUFACTURER_SPECIFIC_DATA, (void *)manu_data, len);
#if TCFG_RDX_HOGP_ENABLE
    if (!rdx_ble_server_adv_append_data(rsp_data, &offset,
                                        HCI_EIR_DATATYPE_COMPLETE_16BIT_SERVICE_UUIDS,
                                        hid_uuid, sizeof(hid_uuid))) {
        return 0;
    }
#endif
    if (offset > ADV_RSP_PACKET_MAX) {
        r_printf("***rsp_data overflow!!!!!!\n");
        return 0;
    }

    return offset;
}

void rdx_ble_server_adv_interval_change_timer_stop(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(g_rdx_ble_server_info.adv_interval_change_timer){
        sys_timeout_del(g_rdx_ble_server_info.adv_interval_change_timer);
        g_rdx_ble_server_info.adv_interval_change_timer = 0;
    }
}

/**************************************************************************
 * FUNCTION
 *  rdx_ble_server_adv_interval_change_timer_cb
 * DESCRIPTION
 *  
 * PARAMETERS
 *  null
 * RETURNS
 *  null
 * **************************************************************************/
void rdx_ble_server_adv_interval_change_timer_cb(void * priv)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    uint8_t adv_type = ADV_IND;
    uint8_t adv_channel = ADV_CHANNEL_ALL;
    u16 change_adv_interval_min = RDX_BLE_ADV_INTERVAL_LOW; //800*1.25= 1000ms
    void *adv_hdl = g_rdx_ble_advertising_hdl;
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    y_printf("=== %s --> 快速广播超时，切换到慢速广播 \r", __func__);
    rdx_ble_server_adv_interval_change_timer_stop();

    if (!adv_hdl) {
        return;
    }
    
    //check if adv is enabled.
    if (0 == app_ble_adv_state_get(adv_hdl)) {
        y_printf("=== %s --> adv is not enabled, return \r", __func__);
        return;
    }
    //change adv interval to slow mode.
    app_ble_adv_enable(adv_hdl, 0);
    app_ble_set_adv_param(adv_hdl, change_adv_interval_min, adv_type, adv_channel);
    app_ble_adv_enable(adv_hdl, 1);
    
    // 进入慢速广播后，关闭 LED 灯效（广播继续但 LED 熄灭），但WiFi传输 / 录音中不改变灯效
    RdxWifiInfo* wifi_info = rdx_app_get_wifi_info();
    RecordStatus* rp = rdx_record_get_status();
    if(wifi_info->onoff != TRANSFER_BY_WIFI_ON
       && !(rp && (rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME))){
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_OFF);
    }
}

/**************************************************************************
 * FUNCTION
 *  rdx_ble_server_adv_interval_change_timer_start
 * DESCRIPTION
 *  
 * PARAMETERS
 *  null
 * RETURNS
 *  null
 * **************************************************************************/
void rdx_ble_server_adv_interval_change_timer_start(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    y_printf("=== %s \r", __func__);
    if(g_rdx_ble_server_info.adv_interval_change_timer == 0){
        g_rdx_ble_server_info.adv_interval_change_timer = sys_timeout_add(NULL, rdx_ble_server_adv_interval_change_timer_cb, RDX_BLE_ADV_INTERVAL_CHANGE_TIMEOUT); //2 mins
    }
}

/**************************************************************************
 * FUNCTION
 *  rdx_ble_server_fast_adv_restart
 * DESCRIPTION
 *  重新进入快速广播模式并点亮 LED 灯效（按键唤醒时调用）
 * PARAMETERS
 *  null
 * RETURNS
 *  null
 * **************************************************************************/
void rdx_ble_server_fast_adv_restart(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    uint8_t adv_type = ADV_IND;
    uint8_t adv_channel = ADV_CHANNEL_ALL;
    void *adv_hdl;
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    y_printf("=== %s --> 按键唤醒，重新进入快速广播 \r", __func__);
    
    // 如果已连接，不需要重启广播
    if (
        rdx_ble_server_phase0a_connected_count() >= RDX_BLE_PHASE0A_WRAPPER_MAX
    ) {
        y_printf("=== %s --> BLE 已连接，无需重启广播 \r", __func__);
        return;
    }
    
    // 停止当前定时器
    rdx_ble_server_adv_interval_change_timer_stop();

    adv_hdl = g_rdx_ble_advertising_hdl;
    if (!adv_hdl) {
        rdx_ble_server_adv_enable(1);
        return;
    }
    
    // 检查广播是否开启
    if (0 == app_ble_adv_state_get(adv_hdl)) {
        // 广播未开启，直接开启快速广播
        rdx_ble_server_adv_enable(1);
        return;
    }
    
    // 切换回快速广播间隔
    app_ble_adv_enable(adv_hdl, 0);
    app_ble_set_adv_param(adv_hdl, g_rdx_ble_server_info.adv_interval_min, adv_type, adv_channel);
    app_ble_adv_enable(adv_hdl, 1);
    
    // 重新启动 2 分钟定时器
    rdx_ble_server_adv_interval_change_timer_start();
    
    // 点亮 BLE 广播 LED 灯效，但WiFi传输中不改变灯效
    RdxWifiInfo* wifi_info = rdx_app_get_wifi_info();
    if(wifi_info->onoff != TRANSFER_BY_WIFI_ON){
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_BLE_ADV_START);
    }
}

/**************************************************************************
 * function: rdx_ble_server_ble_is_on_adv
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
int rdx_ble_server_ble_is_on_adv(void)
{ 
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    return g_rdx_ble_advertising_hdl ?
           app_ble_adv_state_get(g_rdx_ble_advertising_hdl) : 0;
}

/**************************************************************************
 * FUNCTION
 *  rdx_ble_server_adv_enable
 * DESCRIPTION
 *  
 * PARAMETERS
 *  null
 * RETURNS
 *  null
**************************************************************************/
static int rdx_ble_server_adv_enable_on_hdl(void *hdl, u8 enable)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    uint8_t adv_type = ADV_IND;
    uint8_t adv_channel = ADV_CHANNEL_ALL;
    uint8_t advData[ADV_RSP_PACKET_MAX] = {0};
    uint8_t rspData[ADV_RSP_PACKET_MAX] = {0};
    uint8_t len = 0;

    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    if (!hdl) {
        return -1;
    }
    if (enable == app_ble_adv_state_get(hdl)) {
        if (enable) {
            g_rdx_ble_advertising_hdl = hdl;
        }
        return 0;
    }
    if (enable) {
        app_ble_set_adv_param(hdl, g_rdx_ble_server_info.adv_interval_min, adv_type, adv_channel);
        len = rdx_ble_server_fill_adv_data(advData);
        if (len) {
            put_buf(advData, len);
            app_ble_adv_data_set(hdl, advData, len);
        }
        len = rdx_ble_server_fill_rsp_data(rspData);
        if (len) {
            put_buf(rspData, len);
            app_ble_rsp_data_set(hdl, rspData, len);
        }
        g_rdx_ble_server_info.adv_refresh_pending = FALSE;
        //start adv interval change timer.
        rdx_ble_server_adv_interval_change_timer_start();
    }
    int ret = app_ble_adv_enable(hdl, enable);
    if (ret == 0) {
        if (enable) {
            g_rdx_ble_advertising_hdl = hdl;
        } else if (g_rdx_ble_advertising_hdl == hdl) {
            g_rdx_ble_advertising_hdl = NULL;
        }
    }
    g_printf("===== rdx_adv_enable hdl=%p adv_handle=%d enable=%d ret=%d\r",
             hdl, app_ble_adv_handle_get(hdl), enable, ret);
    
    //LED控制：BLE广播开启时，如果未连接则闪烁，但WiFi传输中不改变灯效
    RdxWifiInfo* wifi_info = rdx_app_get_wifi_info();
    if (enable &&
        rdx_ble_server_phase0a_connected_count() < RDX_BLE_PHASE0A_WRAPPER_MAX &&
        wifi_info->onoff != TRANSFER_BY_WIFI_ON) {
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_BLE_ADV_START);
    }

    return ret;
}

int rdx_ble_server_adv_enable(u8 enable)
{
    u8 index;
    int ret = 0;
    void *target;

    if (!enable) {
        rdx_ble_server_adv_interval_change_timer_stop();
        for (index = 0; index < RDX_BLE_PHASE0A_WRAPPER_MAX; index++) {
            void *hdl = rdx_ble_server_phase0a_wrapper_get(index);
            if (hdl && app_ble_adv_state_get(hdl)) {
                int stop_ret = rdx_ble_server_adv_enable_on_hdl(hdl, 0);
                if (stop_ret && !ret) {
                    ret = stop_ret;
                }
            }
        }
        return ret;
    }

    target = rdx_ble_server_phase0a_idle_wrapper_get();
    if (!target) {
        r_printf("[RDX_BLE_LINK] no idle wrapper; advertising remains off\n");
        return 0;
    }

    for (index = 0; index < RDX_BLE_PHASE0A_WRAPPER_MAX; index++) {
        void *hdl = rdx_ble_server_phase0a_wrapper_get(index);
        if (hdl && hdl != target && app_ble_adv_state_get(hdl)) {
            rdx_ble_server_adv_enable_on_hdl(hdl, 0);
        }
    }
    r_printf("[RDX_BLE_LINK] advertiser wrapper=%u hdl=%p\n",
             rdx_ble_server_phase0a_wrapper_index(target), target);
    return rdx_ble_server_adv_enable_on_hdl(target, 1);
}

/**************************************************************************
 * FUNCTION
 *  rdx_ble_server_adv_data_changed
 * DESCRIPTION
 *  
 * PARAMETERS
 *  null
 * RETURNS
 *  null
**************************************************************************/
void rdx_ble_server_adv_data_changed(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    u8 bt_mac[RDX_MAC_LEN];
    u8 ble_mac[RDX_MAC_LEN];
#if (RDX_AI_TRANSLATE_SUPPORT == 1)
    AImodeInfo* p = rdx_app_get_AI_mode_info();
    int st = tws_api_get_tws_state() & TWS_STA_SIBLING_CONNECTED;
#endif
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    r_printf("%s \r", __func__);

    if (g_rdx_ble_server_info.rdx_ble_server_hdl == NULL) {
        g_rdx_ble_server_info.adv_refresh_pending = TRUE;
        return;
    }
    if (
        rdx_ble_server_phase0a_connected_count() >= RDX_BLE_PHASE0A_WRAPPER_MAX
    ) {
        g_rdx_ble_server_info.adv_refresh_pending = TRUE;
        r_printf("%s --> refresh deferred until disconnect \r", __func__);
        return;
    }
    g_rdx_ble_server_info.adv_refresh_pending = FALSE;

    //adv off.
    rdx_ble_server_adv_enable(0);

    //get mac.
    memset(ble_mac, 0, RDX_MAC_LEN);
#if (RDX_AI_TRANSLATE_SUPPORT == 1)
    if(p->ai_mode == DEVICE_WORK_MODE_AI){
        u8 bt_mac[RDX_MAC_LEN];
        memset(bt_mac, 0, RDX_MAC_LEN);
        syscfg_read(CFG_BT_MAC_ADDR, bt_mac, RDX_MAC_LEN);
        bt_make_ble_address(ble_mac, (void *)bt_mac);
        bt_update_mac_addr(bt_mac);
        app_ble_set_mac_addr(rdx_ble_server_hdl, (void *)ble_mac);
        le_controller_set_mac(ble_mac);
    }else{
        if(st){
            y_printf("$$$$ tws  \r");
            memcpy(ble_mac, p->comAddr, RDX_MAC_LEN);
            bt_update_mac_addr(p->comAddr);
            app_ble_set_mac_addr(rdx_ble_server_hdl, (void *)ble_mac);
            le_controller_set_mac(ble_mac);
        }else{
            y_printf("$$$$ no tws \r");
            rdx_ble_server_get_ble_mac(ble_mac);
        }
    }
#else
    // rdx_ble_server_get_ble_mac(ble_mac);
    le_controller_get_mac(ble_mac);
#endif
    r_printf("%s -->ble mac: %02X:%02X:%02X:%02X:%02X:%02X \r", __FUNCTION__, ble_mac[5], ble_mac[4], ble_mac[3], ble_mac[2], ble_mac[1], ble_mac[0]);     
    // rdx_util_reverse_byte(ble_mac, RDX_MAC_LEN);

    //adv on.
    rdx_ble_server_adv_enable(1);
}

static u8 g_ble_send_fail_cnt = 0;
#define BLE_SEND_FAIL_THRESHOLD     5

u8 rdx_ble_server_get_send_fail_cnt(void)
{
    return g_ble_send_fail_cnt;
}

void rdx_ble_server_reset_send_fail_cnt(void)
{
    g_ble_send_fail_cnt = 0;
}

/**************************************************************************
 * FUNCTION
 *  rdx_ble_server_send
 * DESCRIPTION
 *  发送BLE数据，移除重试机制避免阻塞，增加失败计数用于检测连接异常
 * PARAMETERS
 *  data - 要发送的数据
 *  len - 数据长度
 * RETURNS
 *  成功返回0，失败返回错误码
**************************************************************************/
static int rdx_ble_server_send_internal(
    u8 *data,
    u32 len,
    const rdx_ble_async_token_t *expected_token)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    int ret = 0;
    void *send_hdl = NULL;
    rdx_ble_rdx_transport_snapshot_t snapshot;
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/

    if (!rdx_ble_server_rdx_transport_snapshot_capture(&snapshot)) {
        y_printf("[RDX_SESSION] send skipped: runtime not active\n");
        return -1;
    }
    if (expected_token &&
        (snapshot.token.slot_index != expected_token->slot_index ||
         snapshot.token.slot_generation != expected_token->slot_generation ||
         snapshot.token.transport_epoch != expected_token->transport_epoch)) {
        r_printf("[RDX_BLE_TX] drop stale token-bound RDX send\r");
        return -1;
    }
    send_hdl = snapshot.ble_hdl;
    //is data none?
    if(!data || len == 0){
        log_info("%s --> send data error! no data! \r", __func__);
        return -1;
    }

    //ble send buffer is full?
    if(!send_hdl || app_ble_att_vaild_len_get(send_hdl) < len){
        g_ble_send_fail_cnt++;
        return -1;
    }

    if (
        !rdx_ble_server_rdx_transport_snapshot_is_current(&snapshot) ||
        !send_hdl) {
        return -1;
    }
    rdx_ble_server_rdx_send_pending_arm(&snapshot);
    ret = app_ble_att_send_data(send_hdl,
                               ATT_CHARACTERISTIC_06068D2C_6B97_11EF_B864_0242AC120002_01_VALUE_HANDLE,
                               data, len, ATT_OP_AUTO_READ_CCC);
    if (ret) {
        g_ble_send_fail_cnt++;
        rdx_ble_server_rdx_send_pending_cancel(&snapshot);
    } else {
        g_ble_send_fail_cnt = 0;
    }

    return ret;
}

int rdx_ble_server_send(u8 *data, u32 len)
{
    return rdx_ble_server_send_internal(data, len, NULL);
}

int rdx_ble_server_send_for_token(u8 *data, u32 len,
                                  const rdx_ble_async_token_t *token)
{
    if (!token) {
        return -1;
    }
    return rdx_ble_server_send_internal(data, len, token);
}

/**************************************************************************
 * function: rdx_ble_server_ota_send
 * description: 
 * param (u8) *data
 * param (u32) len
 * return (*)
 **************************************************************************/
static int rdx_ble_server_ota_send_internal(
    u8 *data,
    u32 len,
    const rdx_ble_async_token_t *expected_token)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    int ret = 0;
    int i;
    void *send_hdl = NULL;
    rdx_ble_rdx_transport_snapshot_t snapshot;
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    // y_printf("---> rdx_ble_send len = %d \r", len);
    // put_buf(data, len);

    if (!rdx_ble_server_rdx_transport_snapshot_capture(&snapshot)) {
        y_printf("[RDX_SESSION] OTA send skipped: runtime not active\n");
        return -1;
    }
    if (expected_token &&
        (snapshot.token.slot_index != expected_token->slot_index ||
         snapshot.token.slot_generation != expected_token->slot_generation ||
         snapshot.token.transport_epoch != expected_token->transport_epoch)) {
        r_printf("[RDX_BLE_TX] drop stale token-bound OTA send\r");
        return -1;
    }
    send_hdl = snapshot.ble_hdl;
    if(!data || len == 0){
        r_printf("%s --> buf is null \r", __FUNCTION__);
        return 0;
    }
    if(len < 30){
        g_printf("%s ==> %s \n", __func__, data);
    }

    if (
        !rdx_ble_server_rdx_transport_snapshot_is_current(&snapshot) ||
        !send_hdl) {
        return -1;
    }
    rdx_ble_server_rdx_send_pending_arm(&snapshot);
    ret = app_ble_att_send_data(send_hdl, ATT_CHARACTERISTIC_00239A8F_C616_89BB_3374_F25AF588A7B3_01_VALUE_HANDLE, data, len, ATT_OP_AUTO_READ_CCC);
    if (ret) {
        log_info("ota data send fail\n");
        rdx_ble_server_rdx_send_pending_cancel(&snapshot);
    }
    return ret;
}

int rdx_ble_server_ota_send(u8 *data, u32 len)
{
    return rdx_ble_server_ota_send_internal(data, len, NULL);
}

int rdx_ble_server_ota_send_for_token(u8 *data, u32 len,
                                      const rdx_ble_async_token_t *token)
{
    if (!token) {
        return -1;
    }
    return rdx_ble_server_ota_send_internal(data, len, token);
}

/**************************************************************************
 * FUNCTION
 *  rdx_ble_server_app_disconnect
 * DESCRIPTION
 *  
 * PARAMETERS
 *  null
 * RETURNS
 *  null
**************************************************************************/
void rdx_ble_server_app_disconnect(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    if(g_rdx_ble_server_info.ble_conn == FALSE){
        return;
    }
    rdx_ble_server_disconnect(NULL);
}

/**************************************************************************
 * function: rdx_ble_server_auto_shut_down_enable
 * description: 
 * param (u8) enable
 * return (*)
 **************************************************************************/
void rdx_ble_server_auto_shut_down_enable(u8 enable)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    RecordStatus* rp = rdx_record_get_status();
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
#if TCFG_AUTO_SHUT_DOWN_TIME
    y_printf("rdx_ble_server_auto_shut_down_enable: %d\r", enable);
    if (enable) {
        if (bt_get_total_connect_dev() == 0 && g_rdx_ble_server_info.ble_conn == 0 && (rp->run == RECORD_STATE_STOP)) { 
            sys_auto_shut_down_enable();
        }
    } else {
        sys_auto_shut_down_disable();
    }
#endif
}

/**************************************************************************
 * function: rdx_ble_server_get_ble_mac
 * description: 
 * param (void) *addr
 * return (*)
 **************************************************************************/
int rdx_ble_server_get_ble_mac(void *addr)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    int ret = 0;
    u8 mac_buf_tmp[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    u8 mac_buf_tmp2[6] = {0, 0, 0, 0, 0, 0};
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    memset(g_rdx_ble_server_info.ble_mac_addr, 0, 6);
    ret = syscfg_read(VM_RDX_BLE_MAC, g_rdx_ble_server_info.ble_mac_addr, 6);
    if ((ret != 6) || !memcmp(g_rdx_ble_server_info.ble_mac_addr, mac_buf_tmp, 6) || !memcmp(g_rdx_ble_server_info.ble_mac_addr, mac_buf_tmp2, 6)) {
        le_controller_get_mac(g_rdx_ble_server_info.ble_mac_addr);
        syscfg_write(VM_RDX_BLE_MAC, g_rdx_ble_server_info.ble_mac_addr, 6);
    }
    memcpy(addr, g_rdx_ble_server_info.ble_mac_addr, 6);
    return 0;
}

/**************************************************************************
 * function: rdx_ble_server_get_info
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
rdx_ble_server_info_t * rdx_ble_server_get_info(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    rdx_ble_rdx_transport_snapshot_t snapshot;

    /* librdxApp.a still imports this ABI and reads only ble_conn and
     * ble_mtu_size. Keep the adjacent legacy fields coherent as a view of
     * the active RDX runtime, never as a second link-state registry. */
    if (rdx_ble_server_rdx_transport_snapshot_capture(&snapshot)) {
        rdx_ble_link_state_t *link =
            rdx_ble_session_rdx_token_resolve(&snapshot.token, 1);

        g_rdx_ble_server_info.ble_conn = TRUE;
        g_rdx_ble_server_info.ble_con_handle = snapshot.con_handle;
        g_rdx_ble_server_info.ble_mtu_size = snapshot.mtu_size;
        g_rdx_ble_server_info.ccc_configured =
            link && link->rdx_ccc_configured ? TRUE : FALSE;
        g_rdx_ble_server_info.stream_tx_ready =
            link && link->rdx_stream_tx_ready ? TRUE : FALSE;
    } else {
        g_rdx_ble_server_info.ble_conn = FALSE;
        g_rdx_ble_server_info.ble_con_handle = 0;
        g_rdx_ble_server_info.ble_mtu_size = 0;
        g_rdx_ble_server_info.ccc_configured = FALSE;
        g_rdx_ble_server_info.stream_tx_ready = FALSE;

        /* The immutable receive worker drops every queued packet while
         * ble_conn is false, including our local FIFO drain marker. Admit
         * receive parsing only until that authenticated marker arrives;
         * the transport handle remains zero so every send path stays shut. */
        if (g_rdx_lifecycle_barrier_armed &&
            rdx_ble_session_rdx_runtime_state_get() ==
                RDX_BLE_RUNTIME_QUIESCING) {
            g_rdx_ble_server_info.ble_conn = TRUE;
        }
    }
    return &g_rdx_ble_server_info;
}
/**************************************************************************
 * FUNCTION
 *  rdx_ble_server_broadcast_suppressed
 * DESCRIPTION
 *  Returns 1 when BLE broadcasting must be suppressed because the device is
 *  in DUT mode, shutting down, performing SD format, or transferring over WiFi.
 * PARAMETERS
 *  null
 * RETURNS
 *  1 if broadcast should be suppressed, 0 otherwise
***************************************************************************/
static u8 rdx_ble_server_broadcast_suppressed(void)
{
    RdxWifiInfo* k = rdx_app_get_wifi_info();
    bool rdx_uxfile_sd_format_status_check(void);

    if (k->onoff == TRANSFER_BY_WIFI_ON) {
        return 1;
    }

    if (rdx_app_get_poweroff_flag() ||
        rdx_app_get_dut_status() ||
        rdx_uxfile_sd_format_status_check()) {
        return 1;
    }

    return 0;
}

static void rdx_ble_server_wrapper_register(void *hdl, u8 *ble_addr)
{
    /* Match the SDK's dual-wrapper RCSP/Find My setup. This initializes the
     * wrapper's advertising address type before its shared public address and
     * profile are registered. */
    app_ble_adv_address_type_set(hdl, 0);
    app_ble_set_mac_addr(hdl, ble_addr);
    app_ble_profile_set(hdl, rdx_profile_data);
    app_ble_att_read_callback_register(hdl,
                                       rdx_ble_server_att_read_callback);
    app_ble_att_write_callback_register(hdl,
                                        rdx_ble_server_att_write_callback);
    app_ble_att_server_packet_handler_register(
        hdl, rdx_ble_server_cbk_packet_handler);
    app_ble_hci_event_callback_register(hdl,
                                        rdx_ble_server_cbk_packet_handler);
    app_ble_l2cap_packet_handler_register(hdl,
                                          rdx_ble_server_cbk_packet_handler);
    app_ble_sm_event_callback_register(hdl,
                                       rdx_ble_server_sm_event_callback);
    r_printf("[RDX_BLE_INIT] wrapper=%u registered hdl=%p adv_handle=%d\n",
             rdx_ble_server_phase0a_wrapper_index(hdl), hdl,
             app_ble_adv_handle_get(hdl));
}

/**************************************************************************
 * function: rdx_ble_server_init
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_ble_server_init(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    const uint8_t *edr_addr = bt_get_mac_addr();
    char temp[LOCAL_NAME_LEN];
    u8 len = 0;
    u8 bt_name_len = 0;
    u8 tmp_ble_addr[RDX_MAC_LEN];
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    log_info("====== %s\n", __func__);
    log_info("edr addr:");
    put_buf((uint8_t *)edr_addr, RDX_MAC_LEN);

    // BLE init
    if (g_rdx_ble_server_info.rdx_ble_server_hdl == NULL) {
        g_rdx_ble_server_info.rdx_ble_server_hdl = app_ble_hdl_alloc();
        if (g_rdx_ble_server_info.rdx_ble_server_hdl == NULL) {
            log_info("g_rdx_ble_server_info.rdx_ble_server_hdl alloc err !\n");
            return;
        }
        rdx_ble_server_phase0a_connect_adv_restart_cancel();
        rdx_ble_server_rdx_send_pending_reset();
        g_rdx_ble_adv_token.slot_index = RDX_BLE_LINK_INVALID_INDEX;
        g_rdx_ble_secondary_hdl = app_ble_hdl_alloc();
        if (g_rdx_ble_secondary_hdl == NULL) {
            log_error("[RDX_BLE_INIT] secondary wrapper alloc failed\n");
            app_ble_hdl_free(g_rdx_ble_server_info.rdx_ble_server_hdl);
            g_rdx_ble_server_info.rdx_ble_server_hdl = NULL;
            return;
        }
        //get ble mac.
        memset(tmp_ble_addr, 0, RDX_MAC_LEN);
        int r = le_controller_get_mac(tmp_ble_addr);
        if(r != 0 && is_invalid_ble_mac(tmp_ble_addr)) {
            rdx_ble_server_get_ble_mac(tmp_ble_addr);
            r_printf("le_controller_get_mac fail, use bt addr to make ble mac!\r");
        }else{
            g_printf("le_controller_get_mac success, ble mac: %02X:%02X:%02X:%02X:%02X:%02X \r", tmp_ble_addr[5], tmp_ble_addr[4], tmp_ble_addr[3], tmp_ble_addr[2], tmp_ble_addr[1], tmp_ble_addr[0]);
        }
        // le_controller_set_mac(tmp_ble_addr);

        rdx_ble_server_wrapper_register(
            g_rdx_ble_server_info.rdx_ble_server_hdl, tmp_ble_addr);
        rdx_ble_server_wrapper_register(g_rdx_ble_secondary_hdl,
                                        tmp_ble_addr);
        rdx_ble_session_transport_init(
            g_rdx_ble_server_info.rdx_ble_server_hdl,
            g_rdx_ble_secondary_hdl);
        r_printf("[RDX_BLE_INIT] wrappers registered primary=%p secondary=%p profile=%p\n",
                 g_rdx_ble_server_info.rdx_ble_server_hdl,
                 g_rdx_ble_secondary_hdl,
                 rdx_profile_data);

        // The dual-link registry is initialized after both wrappers exist.

        //init HOGP submodule.
#if TCFG_RDX_HOGP_ENABLE
        rdx_hogp_init(g_rdx_ble_server_info.rdx_ble_server_hdl);
        rdx_codex_micro_init();
#endif

        //init sem.
        os_mutex_create(&g_rdx_ble_server_info.ble_send_queue_mutex);

        //DUT, poweroff, WiFi transfer and SD format suppress unified advertising.
        if (rdx_ble_server_broadcast_suppressed()) {
            y_printf("[BLE_SESSION] unified advertising suppressed\n");
            rdx_ble_server_adv_enable(0);
        } else {
            rdx_ble_server_adv_enable(1);
        }
    }
}

/**************************************************************************
 * function: rdx_ble_server_exit
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_ble_server_exit(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    log_info("====== %s\n", __func__);

    rdx_ble_server_disconnected_adv_restart_cancel();
    rdx_ble_server_syn_data_timers_cancel();
    rdx_ble_server_phase0a_connect_adv_restart_cancel();
    rdx_ble_server_rdx_send_pending_reset();
    rdx_ble_session_transport_deinit();
    g_rdx_ble_adv_token.slot_index = RDX_BLE_LINK_INVALID_INDEX;

    // BLE exit
    rdx_ble_server_adv_enable(0);
    if (g_rdx_ble_server_info.rdx_ble_server_hdl &&
        app_ble_get_hdl_con_handle(
            g_rdx_ble_server_info.rdx_ble_server_hdl)) {
        app_ble_disconnect(g_rdx_ble_server_info.rdx_ble_server_hdl);
    }
    if (g_rdx_ble_secondary_hdl &&
        app_ble_get_hdl_con_handle(g_rdx_ble_secondary_hdl)) {
        app_ble_disconnect(g_rdx_ble_secondary_hdl);
    }
    
    rdx_input_router_deinit();
    rdx_codex_micro_deinit();
    rdx_hogp_deinit();
    rdx_ble_session_reset();

    if (g_rdx_ble_secondary_hdl) {
        app_ble_hdl_free(g_rdx_ble_secondary_hdl);
        g_rdx_ble_secondary_hdl = NULL;
    }
    if (g_rdx_ble_server_info.rdx_ble_server_hdl) {
        app_ble_hdl_free(g_rdx_ble_server_info.rdx_ble_server_hdl);
    }
    g_rdx_ble_server_info.rdx_ble_server_hdl = NULL;
    g_rdx_ble_advertising_hdl = NULL;
}

u8 rdx_ble_server_is_stream_tx_ready(void)
{
    rdx_ble_async_token_t token;
    rdx_ble_link_state_t *link;

    if (!rdx_ble_session_rdx_token_capture(&token, 1)) {
        return 0;
    }
    link = rdx_ble_session_rdx_token_resolve(&token, 1);
    return (link && link->rdx_ccc_configured &&
            link->rdx_stream_tx_ready) ? 1 : 0;
}

#endif
