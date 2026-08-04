/*=====================================================================================
 HEADER NAME: rdx_app.c
 MODULE NAME: rdx application module.
 
 PRE-INCLUDE FILES DESCRIPTION: 	
 
 GENERAL DESCRIPTION: 	
 	This File will gather functions that special handle msg(UserEvent,Timer) from 
 	Intergration.These functions don't be changed by project changed.
 =======================================================================================	
 Revision History: 
 ---------------------------------------
 Author: sheng.dong
 Date: 2024-10-16 22:40:09
 LastEditors: sheng.dong
 LastEditTime: 2024-10-16 22:55:18
 FilePath: \SDK\apps\common\third_party_profile\rdx_protocol\rdx_app.c
 
 Self-documenting Code
=====================================================================================*/

/******************************************************************************
* Include files
******************************************************************************/ 
#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".rdx_app.data.bss")
#pragma data_seg(".rdx_app.data")
#pragma const_seg(".rdx_app.text.const")
#pragma code_seg(".rdx_app.text")
#endif

#include "app_config.h"
#include "app_msg.h"
#include "system/includes.h"
#include "earphone.h"
#include "app_main.h"
#include "3th_profile_api.h"
#include "btstack/avctp_user.h"
#include "btstack/btstack_task.h"
#include "bt_tws.h"
#include "update_tws.h"
#include "update_tws_new.h"
#include "effects/audio_eq.h"
#include "tone_player.h"
#include "user_cfg.h"
#include "key_event_deal.h"
#include "app_power_manage.h"
#include "app_tone.h"
#include "audio_config.h"
#include "effects/eq_config.h"
#include "asm/anc.h"
#include "audio_anc.h"
#include "icsd_anc_user.h"
#include "battery_manager.h"
#include "asm/charge.h"
#include "log.h"
#include "user_cfg_id.h"
#include "syscfg_id.h"
#include "time.h"
#include "clock_manager/clock_manager.h"
#include "power/power_manage.h"
#include "gpio_config.h"

#include "rdx_app_config.h"
#include "rdx_record.h"
#include "rdx_app.h"
#include "rdx_util.h"
#include "rdx_commonDef.h"
#include "rdx_ble_server.h"
#include "rdx_ble_session.h"
#include "rdx_hogp_config.h"
#include "rdx_hogp_keyboard.h"
#include "rdx_hogp_keymap_config.h"
#include "rdx_protocol.h"
#include "xxpUart.h"
#include "rdx_key.h"
#include "rdx_hogp_key_action.h"
#include "rdx_charge.h"
#include "rdx_rtc.h"
#include "rdx_uxfile.h"
#include "rdx_vm.h"
#include "rdx_spi.h"
#include "rdx_battery.h"
#include "led_pt0807.h"
#include "rdx_led_ctrl.h"
#include "rdx_dut.h"
#include "rdx_wifi_event.h"
#include "rdx_dip_switch.h"
#include "rdx_playback_config.h"
#if TCFG_RDX_LOCAL_PLAYBACK_ENABLE
#include "rdx_playback.h"
#endif


#if (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN)


#if (RDX_SEL_DEVICE == DEVICE_RDX_BJ_T2403)
#include "sk4558.h"
#endif

/*******************************************************************************
* Macro Define Section
*******************************************************************************/
#define LOG_TAG                                             "[rdx_app]"
#define LOG_ERROR_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_INFO_ENABLE
/* #define LOG_DUMP_ENABLE */
#define LOG_CLI_ENABLE
#include "debug.h"

//----------------------------------------------------------------------------------------
#define TWS_FUNC_ID_RDX_TRIPLE                              TWS_FUNC_ID('R', 'D', 'T', 'P')
#define TWS_FUNC_ID_RDX_AUTH_SYNC                           TWS_FUNC_ID('R', 'D', 'A', 'U')
#define TWS_FUNC_ID_RDX_RECORD                              TWS_FUNC_ID('R', 'D', 'R', 'D')
#define TWS_FUNC_ID_RDX_CALL                                TWS_FUNC_ID('R', 'D', 'C', 'A')
#define TWS_FUNC_ID_RDX_AI_SYNC                             TWS_FUNC_ID('R', 'D', 'A', 'I')

#define RDX_APP_MODE_SWITCH_KEEP_TIMEOUT                    (3000)

#define RDX_RECORD_CHANNAL_SINGLE                           (0)
#define RDX_RECORD_CHANNAL_DUAL                             (1)

#define EMMC_LDO_POWER_OFF_CHECK_TIMEOUT                    (10 * 1000)

#define RDX_HOLD_RECORD_RETRY_MS                             (50)

/******************************************************************************
* Global variable Section
******************************************************************************/
extern struct ble_task_param ble_task;

/*******************************************************************************
* Local variables Section
*******************************************************************************/
static bool rdx_app_init_flag = FALSE;

static RecordStatus set_rp;

/* LED PT0807 配置 */
static LedPt0807Config_t led_pt0807_config;

#if (TCFG_USER_TWS_ENABLE && TCFG_APP_BT_EN) 
static bool rdx_app_key_call_state = FALSE;
static struct RDX_SYNC_INFO rdx_sync_info;

static u16 hangup_timer = 0;
static u8 call_state = CALL_STATE_OFF;
static u8 tws_role_orig;
static u16 find_device_timer = 0;
static u16 ble_tws_master_conn_state = 0;
static u8 ble_readchar_info[BLE_READCHAR_INFO_SIZE + 1];
static bool sw_chat_mode = FALSE;
static bool sw_call_mode = FALSE;
static u8 neighbour_mac[6];
static AImodeInfo aiModeInfo;
#else
static u8 ble_readchar_info[BLE_READCHAR_INFO_SIZE + 1];

#endif

static u16 record_state_upload_timer = 0;
static rdx_ble_async_token_t record_state_upload_token;
static u8 record_state_upload_token_valid = 0;
static RdxWifiInfo wifiInfo;
static bool rdx_ble_conn = FALSE;

static u8 record_mode = RDX_RECORD_CHANNAL_SINGLE;
static bool poweroff_ready_flag = 0;
static bool poweron_ready_flag = 0;

static u16 mode_switch_keep_timer = 0;
static u8 key_press_record_ready_flag = 0;
static u8 hold_record_pressed = 0;
static u8 hold_record_session_active = 0;
static u8 hold_record_busy_wait_armed = 0;
static u8 hold_record_scene = RECORD_SCENE_CHAT;
static u16 hold_record_retry_timer = 0;
static u8 key5_online_hold_routed = 0;

static bool app_is_idle = FALSE;

//-------------------------------------------------------------------------
// DUT mode: 功能已移至 rdx_dut.c, 这里保留兼容宏
#define rdx_dut_mode        (rdx_dut_is_in_mode())
//-------------------------------------------------------------------------

static DevBaseInfo devBaseInfo;

static u8 qr_code[256];

static int orig_sys_clk;
static u16 rdx_clock_lock_timer;
static bool rdx_clock_lock_flag = FALSE;

static u16 emmc_poweroff_check_timer = 0;
static bool emmc_poweroff_flag = FALSE;

static RdxProtocolCallbacks protocol_cbs = {
    .app_select = RDX_AI_SEL_APP,
    .device_select = RDX_SEL_DEVICE,
    .fw_version = FIRMWARE_VERSION,
    .hw_version = HARDWARE_VERSION,
    .rdx_protocol_cb = NULL
};

static const RdxProtocolIndicateOps* g_protocol_ops = NULL;

/* WiFi AP 产品配置 - 在 rdx_app_tasks_init() 通过 xxp_uart_register_wifi_cfg()
 * 注入到 xxpUart 库. lib 不再直接读 WIFI_AP_SSID 等产品宏, 完全由这里传入. */
#if RDX_WIFI_ENABLE
static const RdxWifiCfg wifi_cfg = {
    .ap_ssid            = WIFI_AP_SSID,
    .ap_password        = WIFI_AP_PASSWORD,
    .dynamic_psw_enable = WIFI_AP_SSID_PSW_DYN_GENERATE,
    .ssid_suffix_mode   = WIFI_AP_SSID_SUFFIX_MODE,
};
#endif


/******************************************************************************
* Function Declaration Section
******************************************************************************/ 
extern u8 get_remote_dev_company(void);
extern void rdx_protocol_record_trigger_indicate(RecordStatus* d, bool factor);
#if RDX_WIFI_ENABLE
extern void xxp_esp32_wifi_close(void);
extern void xxp_esp32_wifi_control(void);
#endif
extern void rdx_ble_server_app_disconnect(void);
extern void sd_set_power(u8 enable);
extern void power_set_soft_poweroff();
extern void rdx_protocol_task_free(void);
extern void rdx_uxfile_task_free(void);
extern void sys_enter_soft_poweroff(enum poweroff_reason reason);
extern int rdx_ble_server_reset_local_name(void);
// OLED 功能已删除
extern void xxp_uart_set_wifi_default_flag(bool flag);
extern RecordStatus* rdx_record_get_status(void);
extern bool rdx_record_process_is_busy_check(void);
extern ReqFileInfo* rdx_protocol_get_uploadfileInfo(void);
extern int rdx_protocol_task_create(RdxProtocolCallbacks *cb);
extern int rdx_record_task_create(void);
extern void motor_init(void);
extern u32 sdfile_get_disk_capacity(void);
extern u32 sdfile_flash_addr2cpu_addr(u32 offset);
extern void rdx_ble_server_adv_data_changed(void);
#if RDX_WIFI_ENABLE
extern void xxp_esp32_wifi_open(void);
extern void xxp_esp32_wifi_close(void);
#endif
extern u16 rdx_ble_server_get_conn_handle(void);
extern void rdx_record_process(void);
extern void rdx_record_motor_state_clear(void);
extern void dual_conn_close();
extern u8 get_ota_status();
extern void sys_set_auto_off_time(u16 auto_off_time);
extern ApInfo* xxp_uart_get_wifi_AP_info(void);
extern void rdx_record_start(void* priv);
extern void init_crc32_table();
extern int rdx_uxfile_sd_format(uxfile_format_cb formatCB);

extern u8 sd_io_suspend(u8 sdx, u8 sdx_io);
extern u8 sd_io_resume(u8 sdx, u8 sdx_io);

extern void sdx_dev_detect_timer_add();
extern void sdx_dev_detect_timer_del();

extern u16 sys_get_auto_off_time(void);


#if (RDX_SUPPORT_MOTOR == 1)
extern void motor_off(void);
extern void motor_on(void);
extern void rdx_record_motor_run(void);
extern bool motor_get_run_status(void);
#endif
extern bool rdx_uxfile_sd_format_status_check(void);

extern void rdx_app_emmc_poweron(u8 check_en);
extern void rdx_ble_server_adv_interval_change_timer_stop(void);
extern void rdx_record_set_default(void);  // 用于在按键事件处理之前初始化 record_status

u8 rdx_app_get_call_wechatCall_state(void);
void rdx_app_tws_call_state_sync(void);
void rdx_app_bt_open(void);
void rdx_app_bt_shutdown(void);
void rdx_app_motor_run_once(void);
void rdx_app_charge_full_timer_to_poweroff(void);
void rdx_record_mode_active_check(bool show);
void rdx_app_wifi_handle(u8 cmd);
void rdx_app_charge_full(void);
DevBaseInfo* rdx_app_get_dev_base_info(void);
void rdx_app_enter_idle(void);
void rdx_app_tasks_init(void);
void rdx_app_record_state_upload_timer_stop(void);

void rdx_app_format_handle(void);

static void rdx_app_emmc_poweroff_check_timer_rerun(void);
static void rdx_app_emmc_poweroff_check_timer_start(void);
void rdx_app_emmc_poweroff_check(void);

void rdx_app_emmc_poweron(u8 check_en);
void rdx_app_auto_shutdown(void);
void rdx_app_emmc_poweroff_check_timer_stop(void);

void xxp_wifi_tcp_file_stop_indicate(void);

typedef struct {
    RecordStatus status;
    rdx_ble_async_token_t token;
    u8 factor;
} rdx_app_record_trigger_request_t;

static void rdx_app_record_trigger_on_app_core(
    rdx_app_record_trigger_request_t *request)
{
    if (!request) {
        return;
    }
    if (!rdx_ble_session_rdx_token_resolve(&request->token, 1)) {
        r_printf("[RDX_RECORD] drop stale record trigger indication\r");
        free(request);
        return;
    }
    rdx_protocol_record_trigger_indicate(&request->status, request->factor);
    free(request);
}

static int rdx_app_record_trigger_post(
    const RecordStatus *status,
    u8 factor,
    const rdx_ble_async_token_t *token)
{
    rdx_app_record_trigger_request_t *request;
    int msg[3];

    if (!status || !token) {
        return -1;
    }
    request = malloc(sizeof(*request));
    if (!request) {
        return -1;
    }
    request->status = *status;
    request->token = *token;
    request->factor = factor;
    msg[0] = (int)rdx_app_record_trigger_on_app_core;
    msg[1] = 1;
    msg[2] = (int)request;
    if (os_taskq_post_type("app_core", Q_CALLBACK, 3, msg)) {
        free(request);
        return -1;
    }
    return 0;
}

/******************************************************************************
* Function Section
******************************************************************************/ 

bool rdx_app_get_power_ready_flag(void)
{
    return poweron_ready_flag;
}

void rdx_app_set_power_ready_flag(void)
{
    poweron_ready_flag = true;
}

u8 rdx_pc_storage_is_busy(void)
{
    if (!rdx_app_init_flag) {
        return false;
    }

    RecordStatus *record = rdx_record_get_status();
    if (!record || record->run != RECORD_STATE_STOP) {
        r_printf("[PC-STORAGE] busy: record state=%d\n",
                 record ? record->run : -1);
        return true;
    }
    if (rdx_record_process_is_busy_check()) {
        r_printf("[PC-STORAGE] busy: record worker\n");
        return true;
    }

#if TCFG_RDX_LOCAL_PLAYBACK_ENABLE
    pb_public_info_t playback = {0};
    rdx_playback_get_info(&playback);
    if (playback.state != PB_STATE_UNREADY &&
        playback.state != PB_STATE_STOPPED) {
        r_printf("[PC-STORAGE] busy: playback state=%d\n", playback.state);
        return true;
    }
#endif

    extern u8 rdx_uxfile_is_datFileInfo_loading(void);
    extern u8 rdx_uxfile_is_scan_active(void);
    extern u8 rdx_uxfile_is_formatting(void);

    ReqFileInfo *file_info = rdx_protocol_get_uploadfileInfo();
    if ((file_info && file_info->file_send_busy) ||
        rdx_is_file_transfer_active() ||
        rdx_is_file_sync_busy()) {
        r_printf("[PC-STORAGE] busy: file transfer or sync\n");
        return true;
    }
    if (rdx_uxfile_is_datFileInfo_loading() ||
        rdx_uxfile_is_scan_active()) {
        r_printf("[PC-STORAGE] busy: file index operation\n");
        return true;
    }
    if (rdx_uxfile_is_formatting() ||
        rdx_uxfile_sd_format_status_check()) {
        r_printf("[PC-STORAGE] busy: formatting\n");
        return true;
    }
    if (get_ota_status()) {
        r_printf("[PC-STORAGE] busy: OTA\n");
        return true;
    }
#if RDX_WIFI_ENABLE
    if (wifiInfo.onoff == TRANSFER_BY_WIFI_ON) {
        r_printf("[PC-STORAGE] busy: WiFi transfer mode\n");
        return true;
    }
#endif

    return false;
}

u8 rdx_app_rdx_rebind_is_idle(void)
{
    BLE_SendData *send_data = rdx_protocol_get_ble_send_data();
    BleBulkSendData *bulk_data = rdx_protocol_get_bulk_send_data();

    if (rdx_pc_storage_is_busy()) {
        return 0;
    }
    if (!send_data || !bulk_data ||
        send_data->send_pending || send_data->bulk_sending ||
        bulk_data->busy || bulk_data->bulk_flag) {
        r_printf("[RDX_BLE_SESSION] busy: legacy send worker\r");
        return 0;
    }
    return 1;
}

/**************************************************************************
 * function: rdx_app_get_poweroff_flag
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
bool rdx_app_get_poweroff_flag(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    return poweroff_ready_flag;
}

/**************************************************************************
 * function: rdx_app_get_app_is_idle
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
bool rdx_app_get_app_is_idle(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    return app_is_idle;
}

/* DUT马达测试已移至 rdx_dut.c (rdx_dut_motor_start/stop) */

/**************************************************************************
 * FUNCTION
 *  rdx_app_is_ios_system
 * DESCRIPTION
 *  
 * PARAMETERS
 *  null
 * RETURNS
 *  null
**************************************************************************/
bool rdx_app_is_ios_system(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    y_printf("remote_dev_company :%d \n", get_remote_dev_company());
    return (get_remote_dev_company() == REMOTE_DEV_IOS);
}

/**************************************************************************
 * function: rdx_app_bt_shutdown_delay_timer_cb
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_bt_shutdown_delay_timer_cb(void* priv)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    rdx_app_bt_shutdown();
}

/**************************************************************************
 * function: rdx_app_reset_delay_cb
 * description: 
 * param (void) *priv
 * return (*)
 **************************************************************************/
void rdx_app_reset_delay_cb(void *priv)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    //do power off.
    sys_enter_soft_poweroff(POWEROFF_RESET);
}

/**************************************************************************
 * function: rdx_app_time_to_reset
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_time_to_reset(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    //do system reset. 
    // sys_timeout_add((void *)1, sys_restart, 1000);

    //do power off.
    sys_timeout_add((void *)1, rdx_app_reset_delay_cb, 1000);
}

/**************************************************************************
 * FUNCTION
 *  rdx_app_sys_restart
 * DESCRIPTION
 *  功能同步到对耳
 * PARAMETERS
 *  rdx_info:对应type的同步数据
 *   不同type的info都放到rdx_sync_info进行统一同步
 *  data_type:对应不同功能
 *   索引号对应APP_TWS_XLSW_SYNC_XXX (rdx_ble_app_demo.h)
 * RETURNS
 *  null
**************************************************************************/
void rdx_app_sync_info_send(void *rdx_info, u8 data_type)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
#if TCFG_USER_TWS_ENABLE
    rdx_sync_flag_update_before_send(&rdx_sync_info);
    if (get_bt_tws_connect_status()) {
        log_info("data_type:%d\n", data_type);
        switch (data_type) {
        case APP_TWS_RDX_SYNC_EQ:
            log_info("sync eq info!\n");
            memcpy(rdx_sync_info.eq_info, rdx_info, EQ_SECTION_MAX + 1);
            rdx_sync_info.rdx_eq_flag = 1;
            break;
        case APP_TWS_RDX_SYNC_ANC:
            log_info("sync anc info!\n");
            break;
        case APP_TWS_RDX_SYNC_VOLUME:
            rdx_sync_info.volume = *((u8 *)rdx_info);
            rdx_sync_info.volume_flag = 1;
            log_info("sync volume info!\n");
            break;
        case APP_TWS_RDX_SYNC_KEY_R1:
            rdx_sync_info.key_r1 = *((u8 *)rdx_info);
            rdx_sync_info.key_change_flag = 1;
            log_info("sync key_r1 info!\n");
            break;
        case APP_TWS_RDX_SYNC_KEY_R2:
            rdx_sync_info.key_r2 = *((u8 *)rdx_info);
            rdx_sync_info.key_change_flag = 1;
            log_info("sync key_r2 info!\n");
            break;
        case APP_TWS_RDX_SYNC_KEY_R3:
            rdx_sync_info.key_r3 = *((u8 *)rdx_info);
            rdx_sync_info.key_change_flag = 1;
            log_info("sync key_r3 info!\n");
            break;
        case APP_TWS_RDX_SYNC_KEY_L1:
            rdx_sync_info.key_l1 = *((u8 *)rdx_info);
            rdx_sync_info.key_change_flag = 1;
            log_info("sync key_l1 info!\n");
            break;
        case APP_TWS_RDX_SYNC_KEY_L2:
            rdx_sync_info.key_l2 = *((u8 *)rdx_info);
            rdx_sync_info.key_change_flag = 1;
            log_info("sync key_l2 info!\n");
            break;
        case APP_TWS_RDX_SYNC_KEY_L3:
            rdx_sync_info.key_l3 = *((u8 *)rdx_info);
            rdx_sync_info.key_change_flag = 1;
            log_info("sync key_l3 info!\n");
            break;
        case APP_TWS_RDX_SYNC_FIND_DEVICE:
            rdx_sync_info.find_device = *((u8 *)rdx_info);
            log_info("sync find_device info!\n");
            break;
        case APP_TWS_RDX_SYNC_DEVICE_CONN_FLAG:
            rdx_sync_info.device_conn_flag = *((u8 *)rdx_info);
            log_info("sync device_conn_flag info!\n");
            break;
        case APP_TWS_RDX_SYNC_DEVICE_DISCONN_FLAG:
            rdx_sync_info.device_disconn_flag = *((u8 *)rdx_info);
            log_info("sync device_disconn_flag info!\n");
            break;
        case APP_TWS_RDX_SYNC_PHONE_CONN_FLAG:
            rdx_sync_info.phone_conn_flag = *((u8 *)rdx_info);
            log_info("sync phone_conn_flag info!\n");
            break;
        case APP_TWS_RDX_SYNC_PHONE_DISCONN_FLAG:
            rdx_sync_info.phone_disconn_flag = *((u8 *)rdx_info);
            log_info("sync phone_disconn_flag info!\n");
            break;
        case APP_TWS_RDX_SYNC_BT_NAME:
            log_info("sync bt name!\n");
            memcpy(rdx_sync_info.bt_name, rdx_info, LOCAL_NAME_LEN);
            rdx_sync_info.rdx_bt_name_flag = 1;
            log_info("tuya bt name:%s\n", rdx_sync_info.bt_name);
            break;
        case APP_TWS_RDX_SYNC_KEY_RESET:
            log_info("sync key_reset!\n");
            rdx_sync_info.key_reset = 1;
            break;
        default:
            break;
        }
        /* if (tws_api_get_role() == TWS_ROLE_MASTER) { */
        log_info("this is tuya master!\n");
        u8 status = tws_api_send_data_to_sibling(&rdx_sync_info, sizeof(rdx_sync_info), TWS_FUNC_ID_RDX_STATE);
        log_info("status:%d\n", status);
        /* } */
    }
#endif
}

/**************************************************************************
 * function: rdx_app_volume_indicate
 * description: 
 * param (s8) volume
 * return (*)
 **************************************************************************/
void rdx_app_volume_indicate(s8 volume)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    u8 max_vol = app_audio_get_max_volume();
    log_info("cur_vol is:%d, max:%d\n", volume, app_audio_get_max_volume());
    u8 rdx_sync_valume = (int)(volume * 100 / max_vol);
    if(g_protocol_ops) g_protocol_ops->volume_indicate(rdx_sync_valume);
}

static int rdx_app_device_record_set(u8 scene, u8 run);
static void rdx_app_hold_record_pump(void);

static void rdx_app_hold_record_retry_cb(void *priv)
{
    int msg[2];

    (void)priv;
    hold_record_retry_timer = 0;
    msg[0] = (int)rdx_app_hold_record_pump;
    msg[1] = 0;
    if (os_taskq_post_type("app_core", Q_CALLBACK, 2, msg)) {
        hold_record_retry_timer = sys_timeout_add(
            NULL, rdx_app_hold_record_retry_cb,
            RDX_HOLD_RECORD_RETRY_MS);
    }
}

static void rdx_app_hold_record_retry_schedule(void)
{
    if (hold_record_retry_timer == 0) {
        hold_record_retry_timer = sys_timeout_add(
            NULL, rdx_app_hold_record_retry_cb,
            RDX_HOLD_RECORD_RETRY_MS);
        if (hold_record_retry_timer == 0) {
            r_printf("[RDX_HOLD_RECORD] retry timer start failed\r");
        }
    }
}

static void rdx_app_hold_record_retry_cancel(void)
{
    if (hold_record_retry_timer) {
        sys_timeout_del(hold_record_retry_timer);
        hold_record_retry_timer = 0;
    }
    hold_record_busy_wait_armed = 0;
}

static void rdx_app_hold_record_reset(void)
{
    rdx_app_hold_record_retry_cancel();
    hold_record_pressed = 0;
    hold_record_session_active = 0;
    hold_record_scene = RECORD_SCENE_CHAT;
    key5_online_hold_routed = 0;
}

static u8 rdx_app_hold_record_wait_until_ready(RecordStatus *rp)
{
    if (rp->process_state != REC_PROCESS_STATE_BUSY) {
        hold_record_busy_wait_armed = 0;
        return 0;
    }

    if (!hold_record_busy_wait_armed) {
        /* Arm the recording module's existing stuck-busy watchdog once. */
        rdx_record_process_is_busy_check();
        hold_record_busy_wait_armed = 1;
    }
    rdx_app_hold_record_retry_schedule();
    return 1;
}

static void rdx_app_hold_record_pump(void)
{
    RecordStatus *rp = rdx_record_get_status();
    int ret;

    if (!hold_record_pressed) {
        if (!hold_record_session_active) {
            rdx_app_hold_record_retry_cancel();
            return;
        }
        if (rdx_app_hold_record_wait_until_ready(rp)) {
            return;
        }

        ret = rdx_app_device_record_set(hold_record_scene, RECORD_STATE_STOP);
        if (ret != 0) {
            r_printf("[RDX_HOLD_RECORD] stop request rejected\r");
        }
        hold_record_session_active = 0;
        rdx_app_hold_record_retry_cancel();
        return;
    }

    if (hold_record_session_active) {
        return;
    }
    if (!rdx_app_init_flag || poweroff_ready_flag || rdx_dut_mode ||
        get_ota_status() || mode_switch_keep_timer ||
        app_in_mode(APP_MODE_PC) || rdx_uxfile_sd_format_status_check()) {
        hold_record_pressed = 0;
        r_printf("[RDX_HOLD_RECORD] start rejected by product state\r");
        return;
    }
    if (rp->run != RECORD_STATE_STOP) {
        hold_record_pressed = 0;
        r_printf("[RDX_HOLD_RECORD] start rejected: another recording is active\r");
        return;
    }
    if (rdx_app_hold_record_wait_until_ready(rp)) {
        return;
    }

    hold_record_scene = rp->scene;
    if (hold_record_scene != RECORD_SCENE_CALL) {
        hold_record_scene = RECORD_SCENE_CHAT;
    }
    ret = rdx_app_device_record_set(hold_record_scene, RECORD_STATE_START);
    if (ret != 0) {
        hold_record_pressed = 0;
        r_printf("[RDX_HOLD_RECORD] start request rejected\r");
        return;
    }
    hold_record_session_active = 1;
    g_printf("[RDX_HOLD_RECORD] start request, scene=%d\r", hold_record_scene);
}

/* HOGP key action execution lives in rdx_hogp_key_action.c.  Online/offline
 * routing is capability based: only HID ready consumes product key events. */

/**************************************************************************
 * function: rdx_app_earphone_key_remap
 * description:
 * param (int) *value
 * param (int) *msg
 * return (*)
 **************************************************************************/
static int rdx_app_get_scene(void);

static u8 rdx_app_rdx_key_route_ready(void)
{
    rdx_ble_async_token_t token;

    return rdx_ble_session_rdx_token_capture(&token, 1);
}

static void rdx_app_key5_remap(int *value, int index, int scene)
{
    u8 *key_table;

    /* Balance an online START even if the BLE state changes while held. */
    if (index == KEY_ACTION_UP && key5_online_hold_routed) {
        key5_online_hold_routed = 0;
        *value = key_table_record_hold[index];
        return;
    }

    if (rdx_app_rdx_key_route_ready()) {
        if (index == KEY_ACTION_LONG) {
            key5_online_hold_routed = 1;
        } else if (index == KEY_ACTION_UP) {
            key_press_record_ready_flag = 0;
        }
        *value = key_table_record_hold[index];
        return;
    }

    /* Local behavior is allowed only when both BLE wrappers are idle. */
    if (rdx_ble_server_has_active_link()) {
        if (index == KEY_ACTION_UP) {
            key_press_record_ready_flag = 0;
        }
        *value = APP_MSG_NULL;
        return;
    }

    key_table = rdx_key_get_io_num_table(4, scene);
    if (key_table) {
        *value = key_table[index];
    }
}

void rdx_app_earphone_key_remap(int *value, int *msg)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    struct key_event *key = (struct key_event *)msg;
    int index = key->event;     
    u8 *pk_l = NULL;
    u8 *pk_r = NULL;
    RecordStatus* rp = rdx_record_get_status();
#if RDX_WIFI_ENABLE
    RdxWifiInfo* p = rdx_app_get_wifi_info();
#endif
    bool format_state = rdx_uxfile_sd_format_status_check();
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    // g_printf("key_remap: 0x%x, 0x%x, 0x%x, 0x%x \r", index, msg[0], msg[1], key->value);

    // ---- 通用保护（所有键值共享） ----
    // OLED 功能已删除
    if(0){ // if(oled_get_mainpage_displaying()){
        g_printf("%s --> key invalid in main page loading! \r", __func__);
        return;
    }
    if(format_state){
        g_printf("%s --> key invalid on formatting! \r", __func__);
        return;
    }
    if (true == app_in_mode(APP_MODE_PC)) {
        g_printf("%s --> key invalid in pc mode! \r", __func__);
        return;
    }

    // ---- IO NUM 键值分发 (KEY_IO_NUM0~4) ----
    if (key->value >= KEY_IO_NUM0 && key->value <= KEY_IO_NUM4) {
        int num_idx = key->value - KEY_IO_NUM0;         // 0~4
        int scene = rdx_app_get_scene();
        rdx_key_io_num_log(num_idx, index);             // DEBUG

        /* KEY5 remains in the five-key HID keymap protocol, but its physical
         * event is reserved for RDX online hold recording for now. */
        if (num_idx == 4) {
            rdx_app_key5_remap(value, index, scene);
            return;
        }

        /* KEY1-KEY4 use HID while ready. Unsupported actions are consumed. */
#if TCFG_RDX_HOGP_ENABLE
        if (rdx_hogp_keyboard_is_ready()) {
            if (index == KEY_ACTION_CLICK) {
                int action_ret = rdx_hogp_key_action_click((u8)num_idx);
                if (action_ret != 0) {
                    y_printf("[HOGP_KEY_ACTION] connected key %d execute failed: %d\n",
                             num_idx, action_ret);
                }
            }

            /* LONG/HOLD/UP and unimplemented multi-click actions are intentionally
             * consumed while HID owns the keys. HOLD repeats, so do not log here. */
            *value = APP_MSG_NULL;
            return;
        }
#endif

        /* Any BLE ACL excludes local offline behavior, including links that
         * have not completed RDX/HID capability setup yet. */
        if (rdx_ble_server_has_active_link()) {
            *value = APP_MSG_NULL;
            return;
        }

        pk_r = rdx_key_get_io_num_table(num_idx, scene);
        if (pk_r) {
            *value = pk_r[index];
        }
        return;
    }

    // ---- KEY_POWER 原有逻辑 (TWS L/R) ----
    if(key->value != 0){
        return;
    }
	// rdx_app_emmc_poweron();

    if (app_in_mode(APP_MODE_IDLE)){
        // g_printf("%s --> in idle mode now! \r", __func__);
        pk_r = key_table_incharge_r;
    }else{
        if(app_is_idle == TRUE){
            pk_r = key_table_idle_r;
            *value = pk_r[index];
            return;
        }
#if RDX_WIFI_ENABLE
        //wifi open?
        if(p->onoff == TRANSFER_BY_WIFI_ON){
            // g_printf("%s --> in wifi mode now! \r", __func__);
            pk_r = key_table_wifi_r;
        }else
#endif
        {
            //dut?
            if(rdx_dut_mode){
                // g_printf("%s --> in dut mode now! \r", __func__);
                pk_r = key_table_dut_r;
            }else{
                //scene.
                if(rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME){
                    // y_printf("=== %s -->recording scene", __FUNCTION__);
                    //normal mode.
                    pk_r = key_table_recording_r;
                }else{
                    if (get_ota_status()){
                        // y_printf("=== %s -->ota scene", __FUNCTION__);
                        //normal mode.
                        pk_r = key_table_ota_r;
                    }else{
                        // y_printf("=== %s -->normal scene", __FUNCTION__);
                        //normal mode.
                        pk_r = key_table_normal_r;
                    }
                }
            }
        }
    }

    *value = pk_r[index];
    // g_printf("== %s -->index = %d, *value = %d \r", __FUNCTION__, index, *value);
}

/**************************************************************************
 * function: rdx_app_get_scene
 * description: 提取当前产品场景，供按键映射表查询
 **************************************************************************/
int rdx_app_get_scene(void)
{
    RecordStatus* rp = rdx_record_get_status();
#if RDX_WIFI_ENABLE
    RdxWifiInfo* p = rdx_app_get_wifi_info();
#endif

    if (app_in_mode(APP_MODE_IDLE))  return 0;   // IDLE
    if (app_is_idle == TRUE)         return 0;
    if (get_ota_status())            return 5;   // OTA
    if (rdx_dut_mode)                return 4;   // DUT
#if RDX_WIFI_ENABLE
    if (p->onoff == TRANSFER_BY_WIFI_ON) return 3;   // WIFI
#endif
    if (rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME)
                                     return 2;   // RECORDING
    return 1;   // NORMAL
}

/**************************************************************************
 * FUNCTION
 *  
 * DESCRIPTION
 *  
 * PARAMETERS
 *  null
 * RETURNS
 *  null
**************************************************************************/
int rdx_app_earphone_state_set_page_scan_enable()
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    return 0;
}

/**************************************************************************
 * FUNCTION
 *  
 * DESCRIPTION
 *  
 * PARAMETERS
 *  null
 * RETURNS
 *  null
**************************************************************************/
int rdx_app_earphone_state_get_connect_mac_addr()
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    return 0;
}

/**************************************************************************
 * FUNCTION
 *  
 * DESCRIPTION
 *  
 * PARAMETERS
 *  null
 * RETURNS
 *  null
**************************************************************************/
int rdx_app_earphone_state_cancel_page_scan()
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    return 0;
}

/**************************************************************************
 * FUNCTION
 *  rdx_app_earphone_pack_readchardata
 * DESCRIPTION
 *  
 * PARAMETERS
 *  null
 * RETURNS
 *  null
**************************************************************************/
void rdx_app_earphone_pack_readchardata(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    rdx_auth_info_t* p_authInfo = rdx_vm_get_auth_info();
#if RDX_PRODUCT_IS_CHARGE_CASE
    EarphoneInfo*    p_epInfo   = rdx_vm_get_ep_info();
    ApInfo* p = xxp_uart_get_wifi_AP_info();
    int len = 0;
    u8 local_mac[6];
    u8 ep_mac_R[6];
    u8 array_mac[6];
    u8 wifi_mac[6];
    char temp[BLE_READCHAR_INFO_SIZE + 1];
#endif
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    //pack data.
    memset(ble_readchar_info, 0, BLE_READCHAR_INFO_SIZE + 1);

#if RDX_PRODUCT_IS_CHARGE_CASE
    /* ---- 耳机仓: 多字段拼接 ---- */
    memset(temp,      0, sizeof(temp));
    memset(local_mac, 0, sizeof(local_mac));
    memset(ep_mac_R,  0, sizeof(ep_mac_R));
    memset(array_mac, 0, sizeof(array_mac));
    memset(wifi_mac,  0, sizeof(wifi_mac));

    /* case mac: 本机 BLE MAC (auth info 内已经按 hex 6 字节存好) */
    memcpy(local_mac, p_authInfo->ble_mac_hex, 6);
    /* wifi mac */
    if(p){
        memcpy(wifi_mac, p->mac_bytes, 6);
    }
    /* 已配对耳机时, ep_mac_R = ep_mac_L 末字节 +1 */
    if(memcmp(p_epInfo->ep_mac, array_mac, 6) != 0){
        memcpy(ep_mac_R, p_epInfo->ep_mac, 6);
        ep_mac_R[5] += 1;
    }

    /* role + ep_mac_L + ep_mac_R + case_mac (低字节在后, 与参考工程顺序一致) */
    sprintf(temp, "C,%02X%02X%02X%02X%02X%02X,%02X%02X%02X%02X%02X%02X,%02X%02X%02X%02X%02X%02X,",
            p_epInfo->ep_mac[5], p_epInfo->ep_mac[4], p_epInfo->ep_mac[3],
            p_epInfo->ep_mac[2], p_epInfo->ep_mac[1], p_epInfo->ep_mac[0],
            ep_mac_R[5], ep_mac_R[4], ep_mac_R[3],
            ep_mac_R[2], ep_mac_R[1], ep_mac_R[0],
            local_mac[5], local_mac[4], local_mac[3],
            local_mac[2], local_mac[1], local_mac[0]);
    len = strlen(temp);

    /* auth code (24): 空则全 0 占位 */
    if(p_authInfo->AuthKey[0] == '\0'){
        strncpy(temp + len, "000000000000000000000000", RDX_BLE_DEVICE_AUTH_KEY_SIZE);
    }else{
        strncpy(temp + len, (char *)p_authInfo->AuthKey, RDX_BLE_DEVICE_AUTH_KEY_SIZE);
    }
    len += RDX_BLE_DEVICE_AUTH_KEY_SIZE;
    temp[len++] = ',';

    /* label sn (16): 空则全 0 占位 */
    if(p_authInfo->label_sn[0] == '\0'){
        strncpy(temp + len, "0000000000000000", RDX_LABEL_SN_SIZE);
    }else{
        strncpy(temp + len, (char *)p_authInfo->label_sn, RDX_LABEL_SN_SIZE);
    }
    len += RDX_LABEL_SN_SIZE;

    /* wifi mac (高字节在前, 与参考工程顺序一致) */
    sprintf(temp + len, ",%02X%02X%02X%02X%02X%02X",
            wifi_mac[0], wifi_mac[1], wifi_mac[2],
            wifi_mac[3], wifi_mac[4], wifi_mac[5]);
    len += 13; /* "," + 12 hex chars */

    temp[len] = '\0';

    memcpy(ble_readchar_info, temp, (len > BLE_READCHAR_INFO_SIZE) ? BLE_READCHAR_INFO_SIZE : len);
#else
    /* ---- 录音卡片 / PIN: 仅写 AuthKey 24 字节 ---- */
    {
        u8 zArray[RDX_BLE_DEVICE_AUTH_KEY_SIZE + 1];
        memset(zArray, 0, RDX_BLE_DEVICE_AUTH_KEY_SIZE + 1);
        if(memcmp(p_authInfo->AuthKey, zArray, RDX_BLE_DEVICE_AUTH_KEY_SIZE) == 0){
            r_printf("AuthKey is null !!! \r");
        }
        strncpy((char *)ble_readchar_info, (char *)p_authInfo->AuthKey, RDX_BLE_DEVICE_AUTH_KEY_SIZE);
    }
#endif /* RDX_PRODUCT_IS_CHARGE_CASE */

    y_printf("===%s --> readchardata: %s \n", __func__, ble_readchar_info);
}

/**************************************************************************
 * FUNCTION
 *  rdx_app_earphone_get_readchardata
 * DESCRIPTION
 *  
 * PARAMETERS
 *  null
 * RETURNS
 *  null
**************************************************************************/
char* rdx_app_earphone_get_readchardata(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    return ble_readchar_info;
}

/**************************************************************************
 * function: rdx_app_get_dev_base_info
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
DevBaseInfo* rdx_app_get_dev_base_info(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
#if RDX_WIFI_ENABLE
    ApInfo* p = xxp_uart_get_wifi_AP_info();
#endif

    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    memset(&devBaseInfo, 0, sizeof(DevBaseInfo));
    //get data.
    devBaseInfo.dev_type = RDX_SEL_DEVICE;
    memcpy(devBaseInfo.bt_mac, bt_get_mac_addr(), 6);
    le_controller_get_mac(devBaseInfo.ble_mac);
    
    rdx_auth_info_t* p_auth = rdx_vm_get_auth_info();
#if RDX_WIFI_ENABLE
    memcpy(devBaseInfo.wifi_mac, p->mac_bytes, 6);
#endif
    memcpy(devBaseInfo.auth, p_auth->AuthKey, RDX_BLE_DEVICE_AUTH_KEY_SIZE);
    //ble mac.
    sprintf(devBaseInfo.bt_mac_str, "%02X%02X%02X%02X%02X%02X", devBaseInfo.bt_mac[5], devBaseInfo.bt_mac[4], devBaseInfo.bt_mac[3], devBaseInfo.bt_mac[2], devBaseInfo.bt_mac[1], devBaseInfo.bt_mac[0]);
    sprintf(devBaseInfo.ble_mac_str, "%02X%02X%02X%02X%02X%02X", devBaseInfo.ble_mac[5], devBaseInfo.ble_mac[4], devBaseInfo.ble_mac[3], devBaseInfo.ble_mac[2], devBaseInfo.ble_mac[1], devBaseInfo.ble_mac[0]);

    b_printf("=====> ble mac --> %02X%02X%02X%02X%02X%02X", devBaseInfo.ble_mac[5], devBaseInfo.ble_mac[4], devBaseInfo.ble_mac[3], devBaseInfo.ble_mac[2], devBaseInfo.ble_mac[1], devBaseInfo.ble_mac[0]);
#if RDX_WIFI_ENABLE
    sprintf(devBaseInfo.wifi_mac_str, "%02X%02X%02X%02X%02X%02X", p->mac_bytes[0], p->mac_bytes[1], p->mac_bytes[2], p->mac_bytes[3], p->mac_bytes[4], p->mac_bytes[5]);
#endif

    sprintf((char *)devBaseInfo.label_sn, "%s", p_auth->label_sn);
    b_printf("=====> label_sn --> %s", devBaseInfo.label_sn)

    return &devBaseInfo;
} 


/**************************************************************************
 * function: rdx_app_earphone_state_enter_soft_poweroff
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
int rdx_app_earphone_state_enter_soft_poweroff(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    extern void bt_ble_exit(void);
    bt_ble_exit();
    return 0;
}

/**************************************************************************
 * FUNCTION
 *  
 * DESCRIPTION
 *  
 * PARAMETERS
 *  null
 * RETURNS
 *  null
**************************************************************************/
static int rdx_app_bt_status_event_handler(int *msg)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    struct bt_event *bt = (struct bt_event *)msg;

    y_printf("\r====== rdx_app_bt_status_event_handler event: %d \r", bt->event);
    switch (bt->event) {
    case BT_STATUS_INIT_OK:
        sys_timeout_add(NULL, rdx_app_bt_shutdown, 1000);
        break;

    case BT_STATUS_SECOND_CONNECTED:
    case BT_STATUS_FIRST_CONNECTED:
        if (get_ota_status())
            break; 
        if(g_protocol_ops) g_protocol_ops->conn_state_indicate(1);

        break;
    case BT_STATUS_FIRST_DISCONNECT:
    case BT_STATUS_SECOND_DISCONNECT:
        if (get_ota_status())
            break; 
        if(g_protocol_ops) g_protocol_ops->conn_state_indicate(0);
        break;
    case BT_STATUS_AVRCP_VOL_CHANGE:
        {
            if (get_ota_status())
                break;
            rdx_app_volume_indicate(bt->value * 16 / 127);
        }
        break;
    case BT_STATUS_A2DP_MEDIA_START:
        {
            if (get_ota_status())
                break;
            if(g_protocol_ops){
                u8 st = (bt_a2dp_get_status() == BT_MUSIC_STATUS_STARTING) ? 1 : 0;
                g_protocol_ops->play_status_indicate(st);
            }
        }
        break;
    case BT_STATUS_A2DP_MEDIA_STOP:
        {
            if (get_ota_status())
                break; 
            if(g_protocol_ops) g_protocol_ops->play_status_indicate(0);
        }
        break;

    case BT_STATUS_SCO_STATUS_CHANGE:
        b_printf("%s --> BT_STATUS_SCO_STATUS_CHANGE", __FUNCTION__);
        break;
    case BT_STATUS_SCO_DISCON:
        b_printf("%s --> BT_STATUS_SCO_DISCON \r", __FUNCTION__);
        break;   
    case BT_STATUS_SCO_CONNECTION_REQ:
        b_printf("%s --> BT_STATUS_SCO_CONNECTION_REQ \r", __FUNCTION__);
        break;
    case BT_STATUS_PHONE_INCOME:
        b_printf("%s --> BT_STATUS_PHONE_INCOME \r", __FUNCTION__);
        break;
    case BT_STATUS_PHONE_OUT:
        b_printf("%s --> BT_STATUS_PHONE_OUT \r", __FUNCTION__);
        break;
    case BT_STATUS_PHONE_ACTIVE:
        b_printf("%s --> BT_STATUS_PHONE_ACTIVE \r", __FUNCTION__);
        break;
    case BT_STATUS_PHONE_HANGUP:
        b_printf("%s --> BT_STATUS_PHONE_HANGUP \r", __FUNCTION__);
        break;   
    case BT_STATUS_AVDTP_START:
        break;

    default:
        break;
    }

    return 0;
}

/**************************************************************************
 * FUNCTION
 *  
 * DESCRIPTION
 *  
 * PARAMETERS
 *  null
 * RETURNS
 *  null
**************************************************************************/
static int rdx_app_hci_event_handler(int *msg)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    struct bt_event *bt = (struct bt_event *)msg;

    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    log_info("\nrdx_app_hci_event_handler event:0x%x\n", bt->event);
    switch (bt->event) {
    case HCI_EVENT_CONNECTION_COMPLETE:
        switch (bt->value) {
        case ERROR_CODE_PIN_OR_KEY_MISSING:
            break;
        }
        break;

    case HCI_EVENT_DISCONNECTION_COMPLETE:
        break;
    }

    return 0;
}

/**************************************************************************
 * function: rdx_app_bt_open
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_bt_open(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    lmp_hci_write_scan_enable((1 << 1) | 1);//lmp_hci_write_scan_enable((conn_enable << 1) | scan_enable);
}

/**************************************************************************
 * function: rdx_app_bt_shutdown
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_bt_shutdown(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    extern void clr_device_in_page_list();

    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    r_printf("--------------- rdx_app_bt_shutdown \r");

    clr_device_in_page_list();

#if (TCFG_USER_TWS_ENABLE && TCFG_APP_BT_EN) 
    extern void tws_dual_conn_close();  
    tws_dual_conn_close();
#else
    dual_conn_close();
#endif
    bt_cmd_prepare(USER_CTRL_POWER_OFF, 0, NULL);

    sd_set_power(0);

    void dac_power_off(void);
    dac_power_off();

    struct lp_target *p;
    list_for_each_lp_target(p){
        if(p && p->is_idle && p->is_idle() != 1){
            printf("--------------%s %d\n", p->name, p->is_idle());
        }
    }
    printf("-------------------------------------\r");  
}

/**************************************************************************
 * function: rdx_app_normal_poweroff_cb
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_normal_poweroff_cb(void* priv)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    // rdx_protocol_task_free();
    rdx_record_task_free();
    rdx_uxfile_task_free();
#if (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_OLED) || (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_BOTH_OLED_EMMC)
    oled_task_free();
#endif

    sd_set_power(0);
    gpio_set_mode(IO_PORT_SPILT(IO_PORTC_01), PORT_HIGHZ);
    gpio_set_mode(IO_PORT_SPILT(IO_PORTC_02), PORT_HIGHZ);

    // PB4 已改为 WiFi CS 使用，不再设置为高阻态
    // PB5 已改为充满检测使用，不再设置为高阻态

    gpio_set_mode(IO_PORT_SPILT(IO_PORTC_04), PORT_HIGHZ);
    gpio_set_mode(IO_PORT_SPILT(IO_PORTC_05), PORT_HIGHZ);

#if RDX_WIFI_ENABLE
    gpio_set_mode(IO_PORT_SPILT(WIFI_POWER_PORT_IO), PORT_HIGHZ);
#endif
    gpio_set_mode(IO_PORT_SPILT(VDD_POWER_PORT_IO), PORT_HIGHZ);

    sys_enter_soft_poweroff(POWEROFF_NORMAL);
}

/**************************************************************************
 * function: rdx_app_normal_poweroff
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_normal_poweroff(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    RecordStatus* rp = rdx_record_get_status();
#if RDX_WIFI_ENABLE
    RdxWifiInfo* pw = rdx_app_get_wifi_info();
#endif
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    r_printf("------> %s \n", __func__);

    rdx_app_hold_record_reset();

    //close record.
    if(rp->run != RECORD_STATE_STOP){
        rp->run = RECORD_STATE_STOP;
        rdx_record_process();
    }

#if RDX_WIFI_ENABLE
    if(pw->onoff == TRANSFER_BY_WIFI_ON){
        rdx_app_wifi_handle(TRANSFER_BY_WIFI_OFF);
    }
#endif

    rdx_ble_server_app_disconnect();
    //stop ble.
    rdx_ble_server_exit();


#if (RDX_SUPPORT_MOTOR == 1)
    //motor.
    rdx_app_motor_run_once();
#endif

    poweroff_ready_flag = false;
    key_press_record_ready_flag = false;

    xxp_uart_set_wifi_default_flag(false);

    os_time_dly(50);

    // power_set_soft_poweroff();
    // sys_enter_soft_poweroff(POWEROFF_NORMAL);
    sys_timeout_add(NULL, rdx_app_normal_poweroff_cb, 500);
}

/**************************************************************************
 * function: rdx_app_motor_run_once
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_motor_run_once(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
#if (RDX_SUPPORT_MOTOR == 1)
    motor_run_by_time(500);
#endif
}

/**************************************************************************
 * function: rdx_app_record_state_upload_timer_cb
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_record_state_upload_timer_cb(void* priv)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    RecordStatus* rp = rdx_record_get_status();
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    g_printf("====== %s \r", __func__);

    rdx_ble_async_token_t token = record_state_upload_token;
    u8 token_valid = record_state_upload_token_valid;

    rdx_app_record_state_upload_timer_stop();

    if (!token_valid ||
        !rdx_ble_session_rdx_token_resolve(&token, 1)) {
        record_state_upload_token_valid = 0;
        r_printf("[RDX_RECORD] drop stale record state timer\r");
        return;
    }
    record_state_upload_token_valid = 0;

    if(RECORD_STATE_START == rp->run || RECORD_STATE_RESUME == rp->run){
        u16 con_hdl = rdx_ble_server_get_conn_handle();
        rp->run = RECORD_STATE_STOP;
        if(0xffff != con_hdl && 0 != con_hdl){
            //report this action to app.
            set_rp.run = rp->run;
            set_rp.formate = rp->formate;
            set_rp.scene = rp->scene;
            //report this action to app.
            u8 factor = 0;
            int ret = rdx_app_record_trigger_post(&set_rp, factor, &token);
            if(ret) {
                r_printf("%s rdx_record_state_indicate taskq post err \n", __func__);
            }
        }else{
            //send job.
            int arg[2];
            arg[0] = (int)rdx_record_process;
            arg[1] = 0;
            int r = os_taskq_post_type("app_core", Q_CALLBACK, 2, arg);
            if(r) {
                r_printf("%s record taskq post err \n", __func__);
            } 
        }
    }
}

/**************************************************************************
 * function: rdx_app_record_state_upload_timer_stop
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_record_state_upload_timer_stop(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(record_state_upload_timer){
        sys_timeout_del(record_state_upload_timer);
        record_state_upload_timer = 0;
    }
    record_state_upload_token_valid = 0;
    y_printf("====== %s \r", __func__);
}

/**************************************************************************
 * function: rdx_app_record_state_upload_timer_start
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_record_state_upload_timer_start(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    y_printf("====== %s \r", __func__);
    if (!rdx_ble_session_rdx_token_capture(&record_state_upload_token, 1)) {
        r_printf("[RDX_RECORD] skip state timer without RDX owner\r");
        return;
    }
    record_state_upload_token_valid = 1;
    if(record_state_upload_timer == 0){
        record_state_upload_timer = sys_timeout_add(NULL, rdx_app_record_state_upload_timer_cb, 3000);
    }
}

static int rdx_app_device_record_set(u8 scene, u8 run)
{
    u8 formate = 0;
    u16 con_hdl = rdx_ble_server_get_conn_handle();
    RecordStatus* rp = rdx_record_get_status();
    rdx_ble_async_token_t rdx_token = {0};
    u8 rdx_token_valid = 0;

    if (run != RECORD_STATE_START && run != RECORD_STATE_STOP) {
        return -1;
    }
    if(scene == RECORD_SCENE_CHAT){
        formate = RECORD_FORMATE_OPUS_16K_STERO; //会议模式用降噪算法，改为双声道
    }else if(scene == RECORD_SCENE_CALL){
        formate = RECORD_FORMATE_OPUS_16K_STERO;
    }else{
        return -1;
    }

    if(0xffff != con_hdl && 0 != con_hdl){
        rdx_token_valid = rdx_ble_session_rdx_token_capture(&rdx_token, 1);
        if (!rdx_token_valid) {
            r_printf("[RDX_RECORD] ignore online trigger without RDX owner\r");
            return -1;
        }
        if(get_ota_status()){
            return -1;
        }

        memset(&set_rp, 0, sizeof(RecordStatus));
        if(run == RECORD_STATE_START){
            if (rp->run != RECORD_STATE_STOP) {
                return -1;
            }
            set_rp.run = RECORD_STATE_START;
            set_rp.formate = formate;
            set_rp.scene = scene;
            set_rp.mode = rp->mode;
            g_printf("====== %s --> 在线录音开启触发，并发送开启消息, scene: %d \r", __func__, scene);

            rdx_app_record_state_upload_timer_start();
        }else{
            if(rp->run != RECORD_STATE_STOP &&
               rp->orig_mode == RECORD_MODE_OFFLINE){
                g_printf("====== %s --> offline-originated recording, direct stop + send trigger \r", __func__);
                rp->run = RECORD_STATE_STOP;
                int msg_stop[2];
                msg_stop[0] = (int)rdx_record_process;
                msg_stop[1] = 0;
                os_taskq_post_type("app_core", Q_CALLBACK, 2, msg_stop);

                set_rp.run = RECORD_STATE_STOP;
                set_rp.formate = formate;
                set_rp.scene = scene;
                set_rp.mode = rp->mode;
            }else{
                set_rp.run = RECORD_STATE_STOP;
                set_rp.formate = (rp->run == RECORD_STATE_STOP) ? formate : rp->formate;
                set_rp.scene = (rp->run == RECORD_STATE_STOP) ? scene : rp->scene;
                set_rp.mode = rp->mode;
                g_printf("====== %s --> 在线录音结束触发，并发送结束消息, scene = %d, formate = %d \r", __func__, set_rp.scene, set_rp.formate); 
            }
        }
        //report this action to app. 
        u8 factor = 0;
        int ret = rdx_app_record_trigger_post(&set_rp, factor, &rdx_token);
        if(ret) {
            r_printf("%s rdx_protocol_record_trigger_indicate taskq post err \n", __func__);
        }
        return ret;
    }else{
        if(run == RECORD_STATE_START){
            if (rp->run != RECORD_STATE_STOP) {
                return -1;
            }
#if TCFG_RDX_LOCAL_PLAYBACK_ENABLE
            /* 离线录音会立即启动，先在 app_core 结束本地回听会话。 */
            rdx_playback_stop();
#endif
            rp->run = RECORD_STATE_START;
            rp->formate = formate;
            rp->scene = scene;
            int msg[2];
            msg[0] = (int)rdx_record_process;
            msg[1] = 0;
            int ret = os_taskq_post_type("app_core", Q_CALLBACK, 2, msg);
            g_printf("====== %s --> 离线录音开启 \r", __func__);
            return ret;
        }else{
            if (rp->run == RECORD_STATE_STOP) {
                return 0;
            }
            rp->run = RECORD_STATE_STOP;
            int msg[2];
            msg[0] = (int)rdx_record_process;
            msg[1] = 0;
            int ret = os_taskq_post_type("app_core", Q_CALLBACK, 2, msg);
            g_printf("====== %s --> 离线录音结束 \r", __func__);
            return ret;
        }
    }
}

/**************************************************************************
 * function: rdx_app_device_record_handle
 * description:
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_device_record_handle(u8 scene)
{
    RecordStatus *rp = rdx_record_get_status();
    u8 run = (rp->run == RECORD_STATE_STOP) ?
             RECORD_STATE_START : RECORD_STATE_STOP;

    rdx_app_device_record_set(scene, run);
}

#if RDX_PRODUCT_IS_CHARGE_CASE
/**************************************************************************
 * function: rdx_app_device_pair_handle
 * description: 处理 *APP#devpair# 仓配对业务
 * param (char*) au_code  仓鉴权码字符串 (RDX_BLE_DEVICE_AUTH_KEY_SIZE)
 * param (char*) mac_str  耳机 BLE MAC 字符串 (RDX_BLE_MAC_STRING_SIZE)
 * param (char*) label_sn 出厂 SN (RDX_LABEL_SN_SIZE)
 * return 0 = 成功, <0 = 失败 (协议层会上报 ack=1)
 **************************************************************************/
int rdx_app_device_pair_handle(char* au_code, char* mac_str, char* label_sn)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    EarphoneInfo* p_epInfo = rdx_vm_get_ep_info();
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(au_code == NULL || mac_str == NULL){
        r_printf("%s --> param error \n", __func__);
        return -1;
    }
    if(memcmp(au_code, "0", RDX_BLE_DEVICE_AUTH_KEY_SIZE) == 0) {
        r_printf("%s --> auth code is empty! \r", __func__);
        return -1;
    }
    if(memcmp(mac_str, "0", RDX_BLE_MAC_STRING_SIZE) == 0){
        r_printf("%s --> earphone mac is empty! \r", __func__);
        return -1;
    }
    if(memcmp(label_sn, "0", RDX_LABEL_SN_SIZE) == 0){
        r_printf("%s --> earphone label sn is empty! \r", __func__);
        return -1;
    }
    //update auth info (RAM).
    rdx_auth_info_t* p_authInfo = rdx_vm_get_auth_info();
    memcpy(p_authInfo->AuthKey, au_code, RDX_BLE_DEVICE_AUTH_KEY_SIZE);
    memcpy(p_authInfo->label_sn, label_sn, RDX_LABEL_SN_SIZE);

    //update earphone info in ram.
    EarphoneInfo ep_info;
    memset(&ep_info, 0, sizeof(EarphoneInfo));
    strncpy(ep_info.ep_mac_str, mac_str, RDX_BLE_MAC_STRING_SIZE);
    rdx_util_str_hexstr2hexarray((u8 *)ep_info.ep_mac_str, strlen(ep_info.ep_mac_str), ep_info.ep_mac);
    rdx_util_reverse_byte(ep_info.ep_mac, 6);
    //update vm.
    memcpy(p_epInfo, &ep_info, sizeof(EarphoneInfo));
    int r = rdx_vm_write_ep_info_intoVM(&ep_info);
    if(r == FALSE) {
        r_printf("%s --> write earphone info into VM failed! \r", __func__);
        return -1;
    }
    g_printf("%s --> get earphone mac address success: %s \r", __func__, ep_info.ep_mac_str);

    //update read characteristic data.
    rdx_app_earphone_pack_readchardata();

    //set default ble name.
    rdx_ble_server_reset_local_name();

    return 0;
}

/**************************************************************************
 * function: rdx_app_device_unpair_handle
 * description: 处理仓解除配对.
 * param (*)
 * return 0 = 成功, <0 = 失败 (协议层会上报 ack=1)
 **************************************************************************/
int rdx_app_device_unpair_handle(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    EarphoneInfo* p_epInfo = rdx_vm_get_ep_info();
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    y_printf("rdx_app_device_unpair_handle \r");

    memset(p_epInfo, 0, sizeof(EarphoneInfo));
    rdx_vm_write_ep_info_intoVM(p_epInfo);

    //read info back from vm to sync RAM cache.
    rdx_vm_read_ep_info_fromVM();

    //update read characteristic data.
    rdx_app_earphone_pack_readchardata();

    return 0;
}
#endif /* RDX_PRODUCT_IS_CHARGE_CASE */

/**************************************************************************
 * function: rdx_app_switch_keep_timer_stop
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_switch_keep_timer_stop()
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    y_printf("------ rdx_app_switch_keep_timer_stop \r");
    if(mode_switch_keep_timer){
        sys_timeout_del(mode_switch_keep_timer);
        mode_switch_keep_timer = 0;
    }
}

/**************************************************************************
 * function: rdx_app_switch_keep_timer_cb
 * description: 
 * param (void) *priv
 * return (*)
 **************************************************************************/
void rdx_app_switch_keep_timer_cb(void *priv)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    RecordStatus* rp = rdx_record_get_status();
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    y_printf("\n------ rdx_app_switch_keep_timer_cb \r");
    rdx_app_switch_keep_timer_stop();

    //check the last mode.
    // rdx_record_mode_active_check(bool show);

    rp->is_switch = false;

    if(rp->run == RECORD_STATE_STOP){
        //switch to next mode.
        if(rp->scene == RECORD_SCENE_CHAT){
            app_send_message(APP_MSG_RECORD_CHAT_MODE, 0);
        }else{
            app_send_message(APP_MSG_RECORD_CALL_MODE, 0);
        }
    }
}

/**************************************************************************
 * function: rdx_app_switch_keep_timer_restart
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_switch_keep_timer_restart(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    y_printf("rdx_app_switch_keep_timer_restart --> mode_switch_keep_timer: %d \r", mode_switch_keep_timer);
    if(mode_switch_keep_timer){
        sys_timer_re_run(mode_switch_keep_timer);
    }
}

/**************************************************************************
 * function: rdx_app_switch_keep_timer_start
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_switch_keep_timer_start(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    y_printf("rdx_app_switch_keep_timer_start --> mode_switch_keep_timer: %d \r", mode_switch_keep_timer);
    if(mode_switch_keep_timer == 0){
        mode_switch_keep_timer = sys_timeout_add(NULL, rdx_app_switch_keep_timer_cb, RDX_APP_MODE_SWITCH_KEEP_TIMEOUT);
        y_printf("rdx_app_switch_keep_timer_start --> mode_switch_keep_timer: %d \r", mode_switch_keep_timer);
    }    
}

/* DUT状态查询和关闭功能已移至 rdx_dut.c */
bool rdx_app_get_dut_status(void)
{
    return rdx_dut_is_in_mode();
}

bool rdx_app_get_dut_motor_flag(void)
{
    return rdx_dut_motor_is_running();
}

bool rdx_app_get_dut_oled_flag(void)
{
    return rdx_dut_oled_is_running();
}

void rdx_app_dut_function_close_all(void)
{
    if(rdx_dut_is_in_mode()){
        rdx_dut_close_current_func();
    }
}

/**************************************************************************
 * function: rdx_app_custom_command_parse
 * description: custom通道指令解析（由rdx_protocol_handle_custom_cmd调用）
 *              hogpkm 交给正式 HOGP keymap 配置模块
 *              ft_ 前缀的产线测试指令交给 rdx_dut 模块处理
 * param (char*) cmd   - 指令名 (e.g. "ft_dut", "ft_oled")
 * param (char*) value - 指令值 (e.g. "1", "0")
 **************************************************************************/
void rdx_app_custom_command_parse(char* cmd, char* value)
{
    if(cmd == NULL || value == NULL){
        r_printf("%s --> param error \n", __func__);
        return;
    }
    if (strcmp(cmd, RDX_LIFECYCLE_CUSTOM_CMD) == 0 &&
        rdx_ble_server_rdx_lifecycle_barrier_match(value)) {
        rdx_ble_server_rdx_lifecycle_barrier_complete();
        return;
    }
    if (rdx_ble_session_rdx_runtime_state_get() !=
        RDX_BLE_RUNTIME_ACTIVE) {
        r_printf("[RDX_BLE_SESSION] stale custom command dropped: %s\r", cmd);
        return;
    }
    y_printf("%s --> cmd: %s, value: %s \r", __func__, cmd, value);

    if(strcmp(cmd, RDX_HOGP_KEYMAP_CUSTOM_CMD) == 0){
        rdx_hogp_keymap_config_handle_custom(value);
        return;
    }

    if(strstr((char*)cmd, "ft_") != NULL){
        rdx_dut_ble_cmd_handle(cmd, value);
        return;
    }
}

/**************************************************************************
 * function: rdx_app_single_click_handle
 * description:  dut mode --> LED test.
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_single_click_handle(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    RecordStatus* rp = rdx_record_get_status();
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(rdx_dut_mode){
        rdx_dut_key_handle(APP_MSG_SINGLE_CLICK);
    }else{
    #if TDX_HAS_RECMARK_ABILITY
        if(rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME){
#if RDX_WIFI_ENABLE
            if(!get_ota_status() && wifiInfo.onoff != TRANSFER_BY_WIFI_ON){
#else
            if(!get_ota_status()){
#endif
                rdx_record_add_mark(RDX_MARK_SOURCE_KEY);
                return;
            }
        }
    #endif
        // 非 DUT 模式下，按键单击重新唤醒快速广播
#if RDX_WIFI_ENABLE
        if(wifiInfo.onoff != TRANSFER_BY_WIFI_ON
            && rp->run != RECORD_STATE_START
            && rp->run != RECORD_STATE_RESUME
            && !get_ota_status()){
#else
        if(rp->run != RECORD_STATE_START
            && rp->run != RECORD_STATE_RESUME
            && !get_ota_status()){
#endif
            rdx_ble_server_fast_adv_restart();
        }
    }    
}

/**************************************************************************
 * function: rdx_app_double_click_handle
 * description: dut mode --> motor test. 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_double_click_handle(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(rdx_dut_mode){
        rdx_dut_key_handle(APP_MSG_DOUBLE_CLICK);
    }else{
#if RDX_WIFI_ENABLE
        if(wifiInfo.onoff == TRANSFER_BY_WIFI_ON || get_ota_status()){
#else
        if(get_ota_status()){
#endif
            r_printf("====== %s --> not on normal status, do nothing \r", __func__);
            return;
        }

        //bound status check and show.
        rdx_vm_bound_status_check();
    }    
}

/**************************************************************************
 * function: rdx_app_triple_click_handle
 * description: dut mode --> record test.
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_triple_click_handle(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    RecordStatus* rp = rdx_record_get_status();
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(rdx_dut_mode){
        rdx_dut_key_handle(APP_MSG_TRIPLE_CLICK);
    }else{
        if(rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME){
            log_info("====== %s --> 非DUT模式下录音中,三击功能无效 \r", __func__);
            return;
        }
#if RDX_WIFI_ENABLE
        if(wifiInfo.onoff == TRANSFER_BY_WIFI_ON || get_ota_status()){
#else
        if(get_ota_status()){
#endif
            r_printf("====== %s --> not on normal status, do nothing \r", __func__);
            return;
        }
    #if (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_OLED) || (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_BOTH_OLED_EMMC)
        // char tmp[20];
        // sprintf(tmp, "%s", FIRMWARE_VERSION);
FWHW_VER, FIRMWARE_VERSION, HARDWARE_VERSION);
    #endif
    }
}

/**************************************************************************
 * function: rdx_app_quadruple_click_handle
 * description:  dut mode --> wifi test.
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_quadruple_click_handle(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    RecordStatus* rp = rdx_record_get_status();

    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(rdx_dut_mode){
        rdx_dut_key_handle(APP_MSG_QUADRUPLE_CLICK);
    }else{
#if RDX_WIFI_ENABLE
        if(wifiInfo.onoff == TRANSFER_BY_WIFI_ON){
            r_printf("====== %s --> wifi is on \r", __func__);
            return;
        }
#endif

        ReqFileInfo* rf_info = rdx_protocol_get_uploadfileInfo();
        y_printf("rf_info->file_send_busy = %d \r", rf_info->file_send_busy);
        if(rf_info->file_send_busy == true){
            r_printf("====== %s --> file uploading... \r", __func__);
            return;
        }
        u8 temp[25];
        rdx_app_get_dev_base_info();
        y_printf("===%s --> dev_type: %04X \r", __func__, devBaseInfo.dev_type);
        memset(temp, 0, sizeof(temp));
        memcpy(temp, devBaseInfo.auth, RDX_BLE_DEVICE_AUTH_KEY_SIZE);
        temp[RDX_BLE_DEVICE_AUTH_KEY_SIZE] = '\0';
        y_printf("===%s --> auth: %s \r", __func__, temp);
        y_printf("===%s --> bt_mac: %s\r", __func__, devBaseInfo.bt_mac_str);
        y_printf("===%s --> ble_mac: %s \r", __func__, devBaseInfo.ble_mac_str);
#if RDX_WIFI_ENABLE
        y_printf("===%s --> wifi_mac: %s \r", __func__, devBaseInfo.wifi_mac_str);
#endif
        y_printf("===%s --->label_sn: %s \r", __func__, devBaseInfo.label_sn);

        memset(qr_code, 0, sizeof(qr_code));
#if RDX_WIFI_ENABLE
        sprintf((char *)qr_code, "%s\t%s\t%s\t%s\r", devBaseInfo.auth, devBaseInfo.ble_mac_str, devBaseInfo.wifi_mac_str, devBaseInfo.label_sn);
#else
        sprintf((char *)qr_code, "%s\t%s\t%s\r", devBaseInfo.auth, devBaseInfo.ble_mac_str, devBaseInfo.label_sn);
#endif
    #if (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_OLED) || (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_BOTH_OLED_EMMC)
QR_CODE, qr_code);
    #endif
    }    
}

/* DUT格式化回调已移至 rdx_dut.c (rdx_dut_format_cb) */

/**************************************************************************
 * function: rdx_app_quintuple_click_handle
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_quintuple_click_handle(void)
{
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(rdx_dut_mode){
        rdx_dut_key_handle(APP_MSG_BT_PAIR_SET_DEFAULT);
    }else{
        rdx_vm_sys_reset_to_defaults();
    }
}

/**************************************************************************
 * function: rdx_app_sextuple_click_handle
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_sextuple_click_handle(void)
{
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(rdx_dut_mode){
        rdx_dut_key_handle(APP_MSG_SEXTUPLE_CLICK);
    }else{
        //no-op
    }
}

/**************************************************************************
 * function: rdx_app_get_wifi_info
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
RdxWifiInfo* rdx_app_get_wifi_info(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    return &wifiInfo;
}

/**************************************************************************
 * function: rdx_app_wifi_handle
 * description: 
 * param (u8) cmd
 * return (*)
 **************************************************************************/
void rdx_app_wifi_handle(u8 cmd)
{
#if RDX_WIFI_ENABLE
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    b_printf("=== %s --> cmd = %d \r", __func__, cmd);
	if(cmd == TRUE){
		b_printf("=== %s --> wifi open \r", __func__);
        if(wifiInfo.onoff == TRANSFER_BY_WIFI_ON){
            return;
        }
        xxp_esp32_wifi_open();
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_WIFI_START);
	}else{
		b_printf("=== %s --> wifi close \r", __func__);

        extern void xxp_esp32_wifi_poweron_timer_cancel(void);
        xxp_esp32_wifi_poweron_timer_cancel();
        if(wifiInfo.onoff == TRANSFER_BY_WIFI_OFF){
            return;
        }
        //do wifi close.
		xxp_esp32_wifi_close();
        rdx_led_ctrl_restore_system_state();
	}
#else
    (void)cmd;
#endif
}

/**************************************************************************
 * function: rdx_app_dut_show
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_dut_show(void)
{
    rdx_dut_show_refresh();
}

/**************************************************************************
 * function: rdx_app_msg_handler
 * description: 
 * param (int) *msg
 * return (*)
 **************************************************************************/
int rdx_app_msg_handler(int *msg)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    u8 comm_addr[6];
    u16 con_hdl = rdx_ble_server_get_conn_handle();
    int ret = false;  //默认不拦截消息
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    y_printf("\n ====== rdx_app_msg_handler event:0x%x \r", msg[0]);
    switch (msg[0]) {
        case APP_MSG_BT_OPEN_PAGE_SCAN:
            rdx_app_earphone_state_set_page_scan_enable();
            break;
        case APP_MSG_BT_CLOSE_PAGE_SCAN:
            rdx_app_earphone_state_cancel_page_scan();
            break;
        case APP_MSG_BT_ENTER_SNIFF:
            break;
        case APP_MSG_BT_EXIT_SNIFF:
            rdx_led_ctrl_restore_system_state();
            break;

        case APP_MSG_MAIN_PAGE_DISPLAYING:
            {
                rdx_app_tasks_init();
            }
            break;

        case APP_MSG_RDX_APP_WAKEUP:
            {
                y_printf("========= APP_MSG_RDX_APP_WAKEUP ========= \r");
                // app_send_message(APP_MSG_GOTO_MODE, APP_MODE_BT);
                app_is_idle = FALSE;
                log_info("cpu_reset!!!\n");
                rdx_cpu_reset();
            }
            break;

        case APP_MSG_SINGLE_CLICK:
            {
                //single click handle.
                rdx_app_single_click_handle();
            }
            break;

        case APP_MSG_DOUBLE_CLICK:
            {
                //double click handle.
                rdx_app_double_click_handle();
            }
            break;

        case APP_MSG_TRIPLE_CLICK:
            {
                //triple click handle.
                rdx_app_triple_click_handle();
            }
            break;

        case APP_MSG_QUADRUPLE_CLICK:
            {
                //quadruple click handle.
                rdx_app_quadruple_click_handle();
            }
            break;

        case APP_MSG_POWER_OFF:
            // rdx_app_earphone_state_enter_soft_poweroff();
            break;

        case APP_MSG_POWER_OFF_READY:
            if (app_in_mode(APP_MODE_IDLE)){
                ret = TRUE;
                break;
            }
            //poweroff ready.
            ret = TRUE;
            poweroff_ready_flag = 1;
            key_press_record_ready_flag = 0;
            //关机直接复用 RDX + JL 原生软关机链，避免只进入伪 idle。
            rdx_app_normal_poweroff();
            ret = TRUE;
            break;

        case APP_MSG_RECORD_OFF:
            {
                printf("====== %s ------> APP_MSG_RECORD_OFF, con_hdl = %04X \n", __FUNCTION__, con_hdl);
                RecordStatus* rp = rdx_record_get_status();
                if(rp->run == RECORD_STATE_STOP){
                    ret = TRUE;
                    break;
                }
                rp->run = RECORD_STATE_STOP;
                //send job.
                int msg[2];
                msg[0] = (int)rdx_record_process;
                msg[1] = 0;
                int ret = os_taskq_post_type("app_core", Q_CALLBACK, 2, msg);
                if(ret) {
                    r_printf("%s record taskq post err \n", __func__);
                }
            }
            ret = TRUE;
            break;

        case APP_MSG_RECORD_HOLD_START:
            hold_record_pressed = 1;
            rdx_app_hold_record_pump();
            ret = TRUE;
            break;

        case APP_MSG_RECORD_HOLD_STOP:
            hold_record_pressed = 0;
            rdx_app_hold_record_pump();
            ret = TRUE;
            break;

        case APP_MSG_RECORD_CHAT_MODE:
            {
                if(rdx_dut_mode == TRUE || get_ota_status()){
                    ret = TRUE;
                    break; 
                }
                //do chat record job.
                rdx_app_device_record_handle(RECORD_SCENE_CHAT);
            }
            ret = TRUE;
            break;

        case APP_MSG_RECORD_CALL_MODE:
            {
                if(rdx_dut_mode == TRUE || get_ota_status()){
                    ret = TRUE;
                    break; 
                }
                //do call record job.
                rdx_app_device_record_handle(RECORD_SCENE_CALL);
            }
            ret = TRUE;
            break; 

        case APP_MSG_LONG_PRESS_HOLDUP: 
            {
	            if(poweroff_ready_flag == 1){
	                break;
	            }

                log_info("=== %s ---> APP_MSG_LONG_PRESS_HOLDUP: key_press_record_ready_flag = %d \r", __FUNCTION__, key_press_record_ready_flag);
                if(key_press_record_ready_flag == 1){
                    RecordStatus* rp = rdx_record_get_status();
                    if(rp->scene == RECORD_SCENE_CHAT){
                        app_send_message(APP_MSG_RECORD_CHAT_MODE, 0);
                    }else{
                        app_send_message(APP_MSG_RECORD_CALL_MODE, 0);
                    }   
                }
                key_press_record_ready_flag = 0;
            }
            break;

        case APP_MSG_RECORD_SWITCH:
            if(poweroff_ready_flag == 1){
                break;
            }
            if(rdx_dut_mode == TRUE || get_ota_status()){
                ret = TRUE;
                break;
            }
            // 检查录音模块是否已初始化，防止开机时 record_status 未初始化导致误触发录音
            if(rdx_app_init_flag == FALSE){
                log_info("=== %s ---> APP_MSG_RECORD_SWITCH ignored, rdx_app not initialized yet!\r", __FUNCTION__);
                ret = TRUE;
                break;
            }
            log_info("=== %s ---> APP_MSG_RECORD_SWITCH \r", __FUNCTION__);
            if(mode_switch_keep_timer != 0){
                ret = TRUE;
                break;
            }

            rdx_app_emmc_poweron(0);

            //record switch.
            RecordStatus* rp = rdx_record_get_status();
            if(rp->run == RECORD_STATE_STOP){
                rp->key_trigger = true;
                //when offline, let user knonw that they can release the key to start recording.
            #if (RDX_SUPPORT_MOTOR == 1)
                rdx_record_motor_run();
            #endif
            }else{
                RecordStatus* rp = rdx_record_get_status();
                if(rp->scene == RECORD_SCENE_CHAT){
                    app_send_message(APP_MSG_RECORD_CHAT_MODE, 0);
                }else{
                    app_send_message(APP_MSG_RECORD_CALL_MODE, 0);
                }
                ret = TRUE;
                break;        
            }

            //set flag.
            key_press_record_ready_flag = 1;
            log_info("=== %s ---> key_press_record_ready \r", __FUNCTION__);
            break;

        case APP_MSG_REC_PREV:
            log_info("=== %s ---> APP_MSG_REC_PREV \r", __FUNCTION__);
#if TCFG_RDX_LOCAL_PLAYBACK_ENABLE
            rdx_playback_prev();
#endif
            ret = TRUE;
            break;

        case APP_MSG_REC_NEXT:
            log_info("=== %s ---> APP_MSG_REC_NEXT \r", __FUNCTION__);
#if TCFG_RDX_LOCAL_PLAYBACK_ENABLE
            rdx_playback_next();
#endif
            ret = TRUE;
            break;

        case APP_MSG_REC_FR:
            log_info("=== %s ---> APP_MSG_REC_FR \r", __FUNCTION__);
#if TCFG_RDX_LOCAL_PLAYBACK_ENABLE
            rdx_playback_fr();
#endif
            ret = TRUE;
            break;

        case APP_MSG_REC_FF:
            log_info("=== %s ---> APP_MSG_REC_FF \r", __FUNCTION__);
#if TCFG_RDX_LOCAL_PLAYBACK_ENABLE
            rdx_playback_ff();
#endif
            ret = TRUE;
            break;

        case APP_MSG_REC_PLAY:
            log_info("=== %s ---> APP_MSG_REC_PLAY \r", __FUNCTION__);
#if TCFG_RDX_LOCAL_PLAYBACK_ENABLE
            rdx_playback_play();
#endif
            ret = TRUE;
            break;

        case APP_MSG_REC_PAUSE:
            log_info("=== %s ---> APP_MSG_REC_PAUSE \r", __FUNCTION__);
#if TCFG_RDX_LOCAL_PLAYBACK_ENABLE
            rdx_playback_pause();
#endif
            ret = TRUE;
            break;

        // OLED 相关事件已删除

        case APP_MSG_TWS_START_PAIR:
            log_info("====== %s ------> APP_MSG_TWS_START_PAIR \n", __FUNCTION__);
            break; 

        case APP_MSG_BT_PAIR_SET_DEFAULT:
            {
                //quintuple click handle.
                rdx_app_quintuple_click_handle();
            }
            break;

        case APP_MSG_SEXTUPLE_CLICK:
            {
                // sextuple click handle.
                rdx_app_sextuple_click_handle();
            }
            break;

    #if (RDX_SEL_DEVICE == DEVICE_RDX_BJ_T2403) || (RDX_SEL_DEVICE == DEVICE_DACOM_CC_T2401) || (RDX_SEL_DEVICE == DEVICE_1MORE_CC_T2402) || (RDX_SEL_DEVICE == DEVICE_ZENCORD_CC_T2616) || (RDX_SEL_DEVICE == DEVICE_BEANSTALK_RKB_T2620)
        case APP_MSG_DUT:
            {
                if(rdx_dut_is_key_dut_disabled() && !rdx_dut_is_in_mode()){
                    g_printf("\n====== %s --> Key DUT disabled, blocked! \r", __func__);
                    break;
                }

                ReqFileInfo* rf_info = rdx_protocol_get_uploadfileInfo();
                if(get_ota_status()){
                    r_printf("====== %s --> busy on ota! \r", __func__);
                    break;
                }
                RecordStatus* rp = rdx_record_get_status();
                if(rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME){
                    r_printf("%s --> busy on recording! \r", __func__);
                    break;
                }
                y_printf("rf_info->file_send_busy = %d \r", rf_info->file_send_busy);
                if(rf_info->file_send_busy == true){
                    r_printf("====== %s --> file uploading... \r", __func__);
                    rdx_protocol_file_cmd_handle(RDX_APP_FILE_CMD_STOP);
                }
                rdx_dut_msg_handle();
            }
            break;
    #endif

        case APP_MSG_PC_MODE_ON:
            {
                //webusb on.
                // rdx_app_charge_start();
            #if (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_OLED) || (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_BOTH_OLED_EMMC)
                oled_task_create();
            #endif
                rdx_app_emmc_poweron(0);
                rdx_ble_server_adv_interval_change_timer_stop();
            #if (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_OLED) || (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_BOTH_OLED_EMMC)
PC_MODE); 
            #endif
            }
            break;

        case APP_MSG_PC_MODE_OFF:
            {
            #if (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_OLED) || (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_BOTH_OLED_EMMC)
SHUTOFF); 
            #endif
                rdx_app_emmc_poweroff_check();
            }
            break;

        default:
            break;
    }
    return ret;
}

/**************************************************************************
 * function: rdx_app_key_msg_handler
 * description: 
 * param (int) *msg
 * return (*)
 **************************************************************************/
int rdx_app_key_msg_handler(int *msg)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    int key_msg = 0;
    struct key_event *key = (struct key_event *)msg;
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/

    
    rdx_app_earphone_key_remap(&key_msg, msg);
    // log_info("key_msg:%d\n", key_msg);

    if(key_msg){
    #if TCFG_USER_TWS_ENABLE
        bt_tws_key_msg_sync(key_msg);
    #else
        app_send_message(key_msg, 0);
    #endif
    }
    return true;  //中断消息分发
}

/*****************rdx demo api*******************/

APP_MSG_HANDLER(rdx_app_bthci_msg_entry) = {
    .owner      = 0xff,
    .from       = MSG_FROM_BT_HCI,
    .handler    = rdx_app_hci_event_handler,
};

APP_MSG_HANDLER(rdx_app_btstack_msg_entry) = {
    .owner      = 0xff,
    .from       = MSG_FROM_BT_STACK,
    .handler    = rdx_app_bt_status_event_handler,
};

#if (TCFG_USER_TWS_ENABLE && TCFG_APP_BT_EN) 
APP_MSG_HANDLER(rdx_app_tws_msg_entry) = {
    .owner      = 0xff,
    .from       = MSG_FROM_TWS,
    .handler    = rdx_app_bt_tws_event_handler,
};
#endif

APP_MSG_PROB_HANDLER(rdx_app_app_msg_entry) = {
    .owner      = 0xff,
    .from       = MSG_FROM_APP,
    .handler    = rdx_app_msg_handler,
};

APP_MSG_PROB_HANDLER(rdx_app_key_msg_entry) = {
    .owner      = 0xff,
    .from       = MSG_FROM_KEY,
    .handler    = rdx_app_key_msg_handler,
};

/**************************************************************************
 * function: rdx_app_format_cb
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
#if TCFG_RDX_LOCAL_PLAYBACK_ENABLE
void rdx_app_playback_content_changed(void)
{
    /* A reused SN must not resolve through UXFILE's last-query metadata cache. */
    rdx_uxfile_invalidate_dat_cache();
    rdx_playback_invalidate_playlist(PB_PLAYLIST_CONTENT_CHANGED);
}
#endif

void rdx_app_format_cb(u8 result)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(result == MEM_FORMAT_RESULT_OK){
        y_printf("rdx_app_format_cb --> format sd card ok! \r");
    }else{
        y_printf("rdx_app_format_cb --> format sd card fail! \r");
    }
}

/**************************************************************************
 * function: rdx_app_format_handle
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_format_handle(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    //format sd card.
#if TCFG_RDX_LOCAL_PLAYBACK_ENABLE
    rdx_playback_invalidate_playlist(PB_PLAYLIST_FORMATTING);
#endif
    rdx_uxfile_sd_format(NULL);
}

/**************************************************************************
 * function: rdx_app_get_record_mode
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
u8 rdx_app_get_record_mode(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    return record_mode;
}

/**************************************************************************
 * function: 
 * description: 
 * param (u8) d
 * return (*)
 **************************************************************************/
void rdx_app_set_record_mode(u8 d)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    record_mode = d;
}

/**************************************************************************
 * function: rdx_record_mode_active_check
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_record_mode_active_check(bool show)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    RecordStatus* rp = rdx_record_get_status();
    u16 con_hdl = rdx_ble_server_get_conn_handle();
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(rp->run == RECORD_STATE_STOP){
        record_mode = RDX_RECORD_CHANNAL_DUAL;
        rp->orig_scene = rp->scene;
    }
    if(0xffff != con_hdl && 0 != con_hdl && g_protocol_ops){
        u8 scene = (rp->scene == RECORD_SCENE_CALL) ? 1 : 0;
        g_protocol_ops->record_mode_indicate(scene, rp->run);
    }
}

/**************************************************************************
 * function: rdx_app_record_switch
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_record_switch(u8 orig_scene)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    u16 con_hdl = rdx_ble_server_get_conn_handle();
    u8 orignal_scene = orig_scene;
    rdx_ble_async_token_t rdx_token = {0};
    u8 rdx_token_valid = 0;
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    y_printf("%s --> record switch: orig_scene = %d \r", __func__, orig_scene);

    if(rdx_dut_mode == TRUE || get_ota_status()){
        return; 
    }
    //record mode switch.
    rdx_token_valid = rdx_ble_session_rdx_token_capture(&rdx_token, 1);
    if(g_protocol_ops && rdx_token_valid){
        RecordStatus* rp_cur = rdx_record_get_status();
        u8 scene = (rp_cur->scene == RECORD_SCENE_CALL) ? 1 : 0;
        g_protocol_ops->record_mode_indicate(scene, rp_cur->run);
    }

    //mode switch keep timer restart.
    rdx_app_switch_keep_timer_restart();

    RecordStatus* rp = rdx_record_get_status();
    if(rp->run != RECORD_STATE_STOP){
        //do stop current recording.
        rp->noshow = 1;
        if(0xffff == con_hdl || 0 == con_hdl){
            rp->run = RECORD_STATE_STOP;
            rdx_record_process();
        }

        rp->is_switch = true;
        rp->switch_orig_scene = orignal_scene;

        //report this action to app.
        set_rp.run = RECORD_STATE_STOP;
        set_rp.formate = rp->formate;
        set_rp.scene = orignal_scene; //正在录音的，则上报停止上一次的模式

        //report this action to app.
        u8 factor = 0;
        int ret = rdx_app_record_trigger_post(&set_rp, factor, &rdx_token);
        if(ret) {
            r_printf("%s rdx_protocol_record_trigger_indicate taskq post err \n", __func__);
        }

        //wait 3s to avoid switching too fast. Do post a message to app.
        int msg1[2];
        msg1[0] = (int)rdx_app_switch_keep_timer_start;
        msg1[1] = 0;
        int r = os_taskq_post_type("app_core", Q_CALLBACK, 2, msg1);
        if(r) {
            r_printf("%s rdx_app_switch_keep_timer_start taskq post err \n", __func__);
        }
    }
    
}


/**************************************************************************
 * function: rdx_app_clk_is_locked
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
bool rdx_app_clk_is_locked(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    return rdx_clock_lock_flag;
}

/**************************************************************************
 * function: rdx_app_clk_unlock
 * description: 
 * param (*) task_name
 * return (*)
 **************************************************************************/
void rdx_app_clk_unlock(const char *task_name)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    int ret;
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(rdx_clock_lock_flag){
        rdx_clock_lock_flag = false;
        ret = clock_unlock(task_name);
        log_info("====== %s, ret = %d \n", __func__, ret);
    }else{
        log_info("====== %s, rdx_clock_lock_flag = %d, no need to unlock! \n", __func__, rdx_clock_lock_flag);
    }
}

/**************************************************************************
 * function: rdx_app_clk_lock
 * description: 
 * param (*) task_name, clock
 * return (*)
 **************************************************************************/
void rdx_app_clk_lock(const char *task_name, int clk)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    int ret;
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(rdx_clock_lock_flag == false){
        rdx_clock_lock_flag = true;
        ret = clock_lock(task_name, clk);
        log_info("====== %s, ret = %d \n", __func__, ret);
    }else{
        log_info("====== %s, rdx_clock_lock_flag = %d, clk has been locked already! \n", __func__, rdx_clock_lock_flag);
    }
}

/**************************************************************************
 * function: rdx_app_clk_unlock_with_timer
 * description: 
 * param (*) task_name
 * return (*)
 **************************************************************************/
void rdx_app_clk_unlock_with_timer(const char *task_name)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    int ret;
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if (rdx_clock_lock_timer != 0) {
        ret = clock_unlock(task_name);
        sys_timeout_del(rdx_clock_lock_timer);
        rdx_clock_lock_timer = 0;
    }
    log_info("====== %s, ret = %d \n", __func__, ret);
}

/**************************************************************************
 * function: rdx_app_clk_lock_with_timer
 * description: 
 * param (*) task_name, clock
 * return (*)
 **************************************************************************/
void rdx_app_clk_lock_with_timer(const char *task_name, int clk)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    int ret;
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if (rdx_clock_lock_timer) {
        sys_timer_re_run(rdx_clock_lock_timer);
        return;
    }
    ret = clock_lock(task_name, clk);
    rdx_clock_lock_timer = sys_timeout_add(NULL, rdx_app_clk_unlock_with_timer, 5000);
    log_info("====== %s, ret = %d \r", __func__, ret);
}

/**************************************************************************
 * function: rdx_app_do_emmc_reset
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_do_emmc_reset(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    gpio_set_mode(IO_PORT_SPILT(VDD_POWER_PORT_IO), PORT_OUTPUT_LOW);
    os_time_dly(50);
    gpio_set_mode(IO_PORT_SPILT(VDD_POWER_PORT_IO), PORT_OUTPUT_HIGH);
}

/**************************************************************************
 * function: rdx_app_emmc_poweron
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_emmc_poweron(u8 check_en)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    y_printf("=====> %s --> emmc_poweroff_flag = %d \n", __func__, emmc_poweroff_flag);
    if(emmc_poweroff_flag == TRUE){
        //power on vdd.
        gpio_set_mode(IO_PORT_SPILT(VDD_POWER_PORT_IO), PORT_OUTPUT_HIGH);

        //sd power on.
        sd_set_power(1);

        // //sd io resume.
        // sd_io_resume(0, 0);

        // //add detect timer.
        // sdx_dev_detect_timer_add();

        //oled init.
#if (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_OLED) || (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_BOTH_OLED_EMMC)
        OLED_Init();
#endif

        if (check_en) {
            rdx_app_emmc_poweroff_check_timer_start();
        }
        emmc_poweroff_flag = false;
    }
}

void rdx_app_emmc_poweroff(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    y_printf("=====> %s --> emmc_poweroff_flag = %d \r", __func__, emmc_poweroff_flag);

    if(emmc_poweroff_flag == false){
        rdx_app_emmc_poweroff_check_timer_stop();

        // sdx_dev_detect_timer_del();
        // sd_io_suspend("sd0", 0);
        sd_set_power(0);

        // PB4 已改为 WiFi CS 使用，不再设置为高阻态
        // PB5 已改为充满检测使用，不再设置为高阻态

        gpio_set_mode(IO_PORT_SPILT(IO_PORTC_04), PORT_HIGHZ);
        gpio_set_mode(IO_PORT_SPILT(IO_PORTC_05), PORT_HIGHZ);

        gpio_set_mode(IO_PORT_SPILT(VDD_POWER_PORT_IO), PORT_OUTPUT_LOW);
        gpio_set_mode(IO_PORT_SPILT(VDD_POWER_PORT_IO), PORT_HIGHZ);

        emmc_poweroff_flag = true;
    }
}

void rdx_app_emmc_poweroff_check_timer_stop(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    // y_printf("=====> %s \r", __func__);
    if (emmc_poweroff_check_timer) {
        sys_timeout_del(emmc_poweroff_check_timer);
        emmc_poweroff_check_timer = 0;
    }
}

static void rdx_app_emmc_poweroff_check_timer_cb(void* priv)
{ 
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    RecordStatus* rp = rdx_record_get_status();
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    y_printf("=====> %s --> rp->run = %d \r", __func__, rp->run);
    //check emmc state?
    if(rp->orig_mode == RECORD_MODE_OFFLINE && (rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME)){
        y_printf("rdx_app_emmc_poweroff_check_timer_cb --> emmc is busy, do not power off \r");
        EXCEPTION_THROW();
    }

    extern u8 rdx_uxfile_is_sync_in_progress(void);
    if(rdx_uxfile_is_sync_in_progress()){
        y_printf("rdx_app_emmc_poweroff_check_timer_cb --> DAT sync in progress, do not power off \r");
        EXCEPTION_THROW();
    }

    extern u8 rdx_uxfile_is_datFileInfo_loading(void);
    if(rdx_uxfile_is_datFileInfo_loading()){
        y_printf("rdx_app_emmc_poweroff_check_timer_cb --> datFileInfo loading, do not power off \r");
        EXCEPTION_THROW();
    }

    extern u8 rdx_is_file_transfer_active(void);
    if(rdx_is_file_transfer_active()){
        y_printf("rdx_app_emmc_poweroff_check_timer_cb --> file transfer active, do not power off \r");
        EXCEPTION_THROW();
    }

    extern u8 rdx_is_file_sync_busy(void);
    if(rdx_is_file_sync_busy()){
        y_printf("rdx_app_emmc_poweroff_check_timer_cb --> file sync busy, do not power off \r");
        EXCEPTION_THROW();
    }

    extern u8 rdx_uxfile_is_scan_active(void);
    if(rdx_uxfile_is_scan_active()){
        y_printf("rdx_app_emmc_poweroff_check_timer_cb --> async scan active, do not power off \r");
        EXCEPTION_THROW();
    }

    extern u8 rdx_uxfile_is_formatting(void);
    if(rdx_uxfile_is_formatting()){
        y_printf("rdx_app_emmc_poweroff_check_timer_cb --> SD formatting, do not power off \r");
        EXCEPTION_THROW();
    }

    //check motor state?
#if (RDX_SUPPORT_MOTOR == 1)
    if(true == motor_get_run_status()){
        y_printf("rdx_app_emmc_poweroff_check_timer_cb --> motor is working, do not power off \r");
        EXCEPTION_THROW();
    }
#endif
    //do power off.
    y_printf("rdx_app_emmc_poweroff_check_timer_cb --> emmc power off \r");
    rdx_app_emmc_poweroff();

    return;
    
EXCEPTION_POINTER()
    rdx_app_emmc_poweroff_check_timer_stop();
    rdx_app_emmc_poweroff_check_timer_start();

}

static void rdx_app_emmc_poweroff_check_timer_rerun(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    y_printf("=====> %s \r", __func__);
    if (emmc_poweroff_check_timer) {
        sys_timer_re_run(emmc_poweroff_check_timer);
    }
}

static void rdx_app_emmc_poweroff_check_timer_start(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    RecordStatus* rp = rdx_record_get_status();
    u16 con_hdl = rdx_ble_server_get_conn_handle();
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    // y_printf("=====> %s \r", __func__);
    if(true == app_in_mode(APP_MODE_PC)){
        r_printf("=====> %s --> APP_MODE_PC, do not start poweroff timer\r", __func__);
        return;
    }
    //when ble connected, recording, wifi run, do not start timer.  
    if(rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME){
        return;
    }
    // if(0xffff != con_hdl && 0 != con_hdl){
    //     return; //ble connected, do not start timer.
    // }
#if RDX_WIFI_ENABLE
    if(wifiInfo.onoff == TRANSFER_BY_WIFI_ON){
        return;
    }
#endif
    if(emmc_poweroff_check_timer == 0){
        emmc_poweroff_check_timer = sys_timeout_add(NULL, rdx_app_emmc_poweroff_check_timer_cb, EMMC_LDO_POWER_OFF_CHECK_TIMEOUT);
    }
}

void rdx_app_emmc_poweroff_check(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    return;
    y_printf("=====> %s \r", __func__);
    rdx_app_emmc_poweroff_check_timer_start();
}

/**************************************************************************
 * function: rdx_app_wifi_event_handle
 * description: Sole consumer of the WiFi event bus (rdx_wifi_event.h).
 *   Threading note: callback may run on protocol send task / sys timer task
 *   / app_core depending on producer. Heavy work (record / BLE / UI) must be
 *   re-posted to app_core via os_taskq_post_type.
 **************************************************************************/
#if RDX_WIFI_ENABLE
static void rdx_app_wifi_event_handle(RdxWifiEvent event, void *data, u32 len)
{
    switch (event) {
        case RDX_WIFI_EVENT_CTRL: {
            if (!data || len < 1) break;
            u8 cmd = *(u8 *)data;
            int msg[3];
            msg[0] = (int)rdx_app_wifi_handle;
            msg[1] = 1;
            msg[2] = (int)cmd;
            int ret = os_taskq_post_type("app_core", Q_CALLBACK, 3, msg);
            if (ret) {
                r_printf("%s wifi ctrl taskq post err \r", __func__);
            }
            break;
        }
        case RDX_WIFI_EVENT_OPEN: {
            r_printf("[APP WIFI] OPEN (onoff flipped to ON)\r");
            break;
        }
        case RDX_WIFI_EVENT_CLOSE: {
            r_printf("[APP WIFI] CLOSE (onoff flipped to OFF)\r");
            break;
        }
        case RDX_WIFI_EVENT_AP_CONNECT_TIMEOUT: {
            r_printf("[APP WIFI] AP connect timeout, sdk cleanup done\r");
            break;
        }
        case RDX_WIFI_EVENT_TCP_CONNECT_TIMEOUT: {
            r_printf("[APP WIFI] TCP connect timeout, sdk cleanup done\r");
            break;
        }
        case RDX_WIFI_EVENT_DATA_TRANSFER_TIMEOUT: {
            r_printf("[APP WIFI] data transfer timeout (20s), sdk cleanup done\r");
            break;
        }
        default:
            break;
    }
}
#endif


typedef struct {
    Record_info info;
    rdx_ble_async_token_t token;
} rdx_app_record_cmd_request_t;

static void rdx_app_record_cmd_on_app_core(rdx_app_record_cmd_request_t *request)
{
    Record_info info;

    if (!request) {
        return;
    }
    info = request->info;

    if (!rdx_ble_session_rdx_token_resolve(&request->token, 1)) {
        r_printf("[RDX_RECORD] drop stale app_core command\r");
        free(request);
        return;
    }

#if TCFG_RDX_LOCAL_PLAYBACK_ENABLE
    if (info.cmd == (RECORD_STATE_START + 0x30) ||
        info.cmd == (RECORD_STATE_RESUME + 0x30)) {
        rdx_playback_stop();
    }
#endif
    rdx_record_cmd_handle_from_rdx(&info, &request->token);
    free(request);
}

/**
 * 协议层 → app 业务统一事件回调入口
 *   @param event 协议事件类型 (rdx_protocol.h 中 ProtocolEvents)
 *   @param data  事件数据指针, 具体类型见 ProtocolEvents 各项注释
 *   @param len   事件数据长度 (字节)
 *
 */
static void rdx_app_protocol_handle(ProtocolEvents event, void* data, u32 len)
{
    const RdxProtocolIndicateOps* ops = g_protocol_ops;
    if(!ops) return;
    if (rdx_ble_session_rdx_runtime_state_get() !=
        RDX_BLE_RUNTIME_ACTIVE) {
        r_printf("[RDX_BLE_SESSION] stale protocol event dropped: %u\r", event);
        return;
    }

    switch(event){
        /* ============== 主动查询/上报类: data == NULL ============== */
        case PROTOCOL_EVENT_CMD_BATTERY_QUERY: {
            DeviceBatInfo* pb = rdx_protocol_update_dev_battery_level();
            g_printf("[APP CMD] battery (L=%d, R=%d, C=%d)\r",
                     pb->tbat_percent_L, pb->tbat_percent_R, pb->tbat_percent_C);
            ops->battery_indicate(pb->tbat_percent_C, pb->tbat_percent_R, pb->tbat_percent_L);
            break;
        }

        case PROTOCOL_EVENT_CMD_INCHARGE_QUERY: {
            DeviceBatInfo* pb = rdx_protocol_update_dev_battery_level();
            u8 charge_state = rdx_app_get_charge_state();
            g_printf("[APP CMD] incharge state=%d (C=%d, R=%d, L=%d)\r",
                     charge_state, pb->tbat_percent_C, pb->tbat_percent_R, pb->tbat_percent_L);
            /* 历史顺序 (charge, C, R, L); _rdx_protocol_incharge_indicate 形参为
             * (charge_state, left, right, chargebox), 这里按既有约定填. */
            ops->incharge_indicate(charge_state, pb->tbat_percent_C, pb->tbat_percent_R, pb->tbat_percent_L);
            break;
        }

        case PROTOCOL_EVENT_CMD_VERSION_QUERY: {
            char* hv = rdx_protocol_get_hardware_version();
            char* sv = rdx_protocol_get_firmware_version();
            g_printf("[APP CMD] version (fw=%s, hw=%s)\r", sv ? sv : "", hv ? hv : "");
            ops->version_indicate(hv, sv);
            break;
        }

        case PROTOCOL_EVENT_CMD_RECORD_MODE_QUERY: {
            RecordStatus* rp_sw = rdx_record_get_status();
            u8 scene = (rp_sw->scene == RECORD_SCENE_CALL) ? 1 : 0;
            g_printf("[APP CMD] record_mode (scene=%d, run=%d)\r", scene, rp_sw->run);
            ops->record_mode_indicate(scene, rp_sw->run);
            break;
        }

        case PROTOCOL_EVENT_CMD_AUTH_SN: {
            ops->auth_sn_indicate();
            break;
        }

        case PROTOCOL_EVENT_CMD_BT_NAME_QUERY: {
            char bt_name[64];
            int ret = rdx_ble_server_bt_name_set_handle(0, NULL, bt_name, sizeof(bt_name));
            g_printf("[APP CMD] bt name = %s\r", bt_name);
            ops->bt_name_check_ack_indicate((u8)(ret ? 1 : 0), bt_name);
            break;
        }

        case PROTOCOL_EVENT_CMD_BLE_NAME_QUERY: {
            char ble_name[64];
            int ret = rdx_ble_server_ble_name_set_handle(0, NULL, ble_name, sizeof(ble_name));
            g_printf("[APP CMD] ble name = %s\r", ble_name);
            ops->ble_name_check_ack_indicate((u8)(ret ? 1 : 0), ble_name);
            break;
        }

        case PROTOCOL_EVENT_CMD_OFFTIME_QUERY: {
            u32 sec = sys_get_auto_off_time();
            g_printf("[APP CMD] offtime = %u\r", (unsigned)sec);
            ops->offtime_check_ack_indicate(0, sec);
            break;
        }

        case PROTOCOL_EVENT_CMD_SD_MEM_QUERY: {
            /* 先回 0/0 占位 ack, 真实容量查询需走 uxfile 任务异步执行,
             * 由 sdk 内部 sd_mem 查询路径完成后再次 indicate. */
            ops->sd_mem_indicate(0, 0);
            rdx_uxfile_device_sd_mem_check();
            break;
        }

        case PROTOCOL_EVENT_CMD_SD_FORMAT: {
            RecordStatus* rp = rdx_record_get_status();
            ReqFileInfo* rf_info = rdx_protocol_get_uploadfileInfo();
            if(get_ota_status() ||
               rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME ||
               (rf_info && rf_info->file_send_busy == true)){
                y_printf("[APP CMD] sd_format rejected: busy\r");
                ops->sd_format_ack_indicate(1);
                break;
            }
            ops->sd_format_ack_indicate(0);
#if TCFG_RDX_LOCAL_PLAYBACK_ENABLE
            rdx_playback_invalidate_playlist(PB_PLAYLIST_FORMATTING);
#endif
            rdx_uxfile_sd_format(NULL);
            break;
        }

        case PROTOCOL_EVENT_CMD_SYS_RESET: {
            RecordStatus* rp = rdx_record_get_status();
            ReqFileInfo* rf_info = rdx_protocol_get_uploadfileInfo();
            if(get_ota_status() ||
               rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME ||
               (rf_info && rf_info->file_send_busy == true)){
                y_printf("[APP CMD] sys_reset rejected: busy\r");
                ops->sys_set_default_ack_indicate(1);
                break;
            }
            ops->sys_set_default_ack_indicate(0);
            int msg[2];
            msg[0] = (int)rdx_vm_sys_reset_to_defaults;
            msg[1] = 0;
            if(os_taskq_post_type("app_core", Q_CALLBACK, 2, msg)){
                log_info("[APP CMD] sys_reset taskq post err\r");
            }
            break;
        }

        /* ============== 下行命令类: data 是 ProtocolXxxParams* ============== */
        case PROTOCOL_EVENT_CMD_RTC: {
            if(!data || len < sizeof(ProtocolRtcParams)) break;
            ProtocolRtcParams* p = (ProtocolRtcParams*)data;
            if(p->timestamp > 0){
                time_t old_rtc = rdx_rtc_get();
                int result = rdx_rtc_set_timestamp(p->timestamp);
                if(result == 0 && old_rtc > 0){
                    int32_t delta = (int32_t)((time_t)p->timestamp - old_rtc);
                    RecordStatus *rp = rdx_record_get_status();
                    if(rp && (rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME)){
                        uxfile_data_t *op = rdx_uxfile_get_operateFile_info();
                        if(op && op->start_time > 0){
                            u32 corrected = (u32)((int32_t)op->start_time + delta);
                            y_printf("[RTC_SYNC] Recording active, fix start_time: %u -> %u (delta=%d)\r",
                                     op->start_time, corrected, delta);
                            op->start_time = corrected;
                        }
                    }
                }
                ops->rtc_set_ack_indicate((u8)result, p->timestamp);
            } else {
                ops->rtc_set_ack_indicate(1, p->timestamp);
            }
            break;
        }

        case PROTOCOL_EVENT_CMD_BOUND: {
            if(!data || len < sizeof(ProtocolBoundParams)) break;
            ProtocolBoundParams* p = (ProtocolBoundParams*)data;
            g_printf("[APP CMD] bound cmd=%d\r", p->cmd);
            if(p->cmd == 1){
                rdx_vm_set_bound_status(1, 1);
                ops->bound_result_ack_indicate(0);
            }else{
                ops->bound_result_ack_indicate(0);
                rdx_vm_unbound_handle();
            }
            break;
        }

        case PROTOCOL_EVENT_CMD_UNBOUND: {
            if(!data || len < sizeof(ProtocolUnboundParams)) break;
            ProtocolUnboundParams* p = (ProtocolUnboundParams*)data;
            RecordStatus* rp = rdx_record_get_status();
            ReqFileInfo* rf_info = rdx_protocol_get_uploadfileInfo();
            if(get_ota_status() ||
               rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME ||
               (rf_info && rf_info->file_send_busy == true)){
                y_printf("[APP CMD] unbound rejected: busy\r");
                ops->unbound_ack_indicate(1, rdx_vm_get_bound_status());
                break;
            }
            g_printf("[APP CMD] unbound user=%d format=%d\r", p->user_para, p->format_en);
            rdx_vm_choose_to_unbound_handle(p->user_para, p->format_en);
            break;
        }

        case PROTOCOL_EVENT_CMD_FILE_DELETE: {
            if(!data || len < sizeof(ProtocolFileDeleteParams)) break;
            ProtocolFileDeleteParams* p = (ProtocolFileDeleteParams*)data;
            g_printf("[APP CMD] file_delete sn=%d name=%s\r", p->file_sn, p->file_name);
#if TCFG_RDX_LOCAL_PLAYBACK_ENABLE
            pb_public_info_t playback_info;
            rdx_playback_get_info(&playback_info);
            if(playback_info.current_sn == (u32)p->file_sn){
                rdx_playback_stop();
            }
#endif
            int ret = rdx_uxfile_recordFile_delete_handle(p->file_sn, p->file_name);
#if TCFG_RDX_LOCAL_PLAYBACK_ENABLE
            if(ret >= 0){
                /* Deletion is asynchronous: ret only confirms that UXFILE
                 * accepted the work item.  Its worker still needs the DAT
                 * cache to remove and persist the entry, so this layer must
                 * not invalidate/free that cache before completion.  The
                 * playback cache is independent and can be conservatively
                 * invalidated as soon as the delete request is accepted. */
                rdx_playback_on_file_deleted((u32)p->file_sn);
            }
#endif
            ops->file_delete_ack_indicate((ret < 0) ? 1 : 0, p->file_sn, p->file_name);
            break;
        }

        case PROTOCOL_EVENT_CMD_BT_NAME_SET: {
            if(!data || len < sizeof(ProtocolNameParams)) break;
            ProtocolNameParams* p = (ProtocolNameParams*)data;
            char bt_name[64];
            int ret = rdx_ble_server_bt_name_set_handle(p->has_value, p->name,
                                                       bt_name, sizeof(bt_name));
            ops->bt_name_set_ack_indicate((u8)(ret ? 1 : 0), bt_name);
            break;
        }

        case PROTOCOL_EVENT_CMD_BLE_NAME_SET: {
            if(!data || len < sizeof(ProtocolNameParams)) break;
            ProtocolNameParams* p = (ProtocolNameParams*)data;
            char ble_name[64];
            int ret = rdx_ble_server_ble_name_set_handle(p->has_value, p->name,
                                                        ble_name, sizeof(ble_name));
            ops->ble_name_set_ack_indicate((u8)(ret ? 1 : 0), ble_name);
            break;
        }

        case PROTOCOL_EVENT_CMD_OFFTIME_SET: {
            if(!data || len < sizeof(ProtocolOfftimeParams)) break;
            ProtocolOfftimeParams* p = (ProtocolOfftimeParams*)data;
            u32 sec = p->offtime;
            if(p->has_value){
                if(sec >= 1){
                    sys_set_auto_off_time((u16)sec);
                }else{
                    ops->offtime_set_ack_indicate(1, (u16)sec);
                    break;
                }
            }else{
                sec = sys_get_auto_off_time();
            }
            ops->offtime_set_ack_indicate(0, (u16)sec);
            break;
        }

        case PROTOCOL_EVENT_CMD_MIC_GAIN_QUERY: {
            if(!data || len < sizeof(ProtocolMicGainQueryParams)) break;
            ProtocolMicGainQueryParams* p = (ProtocolMicGainQueryParams*)data;
            int g1 = 0, g2 = 0;
            int ret = rdx_record_mic_gain_query(p->mode, &g1, &g2);
            ops->mic_gain_check_ack_indicate((u8)(ret ? 1 : 0), p->mode, g1, g2);
            break;
        }

        case PROTOCOL_EVENT_CMD_MIC_GAIN_SET: {
            if(!data || len < sizeof(ProtocolMicGainSetParams)) break;
            ProtocolMicGainSetParams* p = (ProtocolMicGainSetParams*)data;
            int g1 = p->mic1_gain, g2 = p->mic2_gain;
            int ret = rdx_record_mic_gain_set(p->mode, &g1, &g2);
            ops->mic_gain_set_ack_indicate((u8)(ret ? 1 : 0), p->mode, g1, g2);
            break;
        }

        case PROTOCOL_EVENT_CMD_OS_TYPE: {
            if(!data || len < sizeof(ProtocolOsTypeParams)) break;
            ProtocolOsTypeParams* p = (ProtocolOsTypeParams*)data;
            g_printf("[APP CMD] os_type = %d\r", p->os_type);
            ops->os_type_ack_indicate();
            break;
        }

#if RDX_PRODUCT_IS_CHARGE_CASE
        case PROTOCOL_EVENT_CMD_DEVICE_PAIR: {
            if(!data || len < sizeof(ProtocolDevicePairParams)) break;
            ProtocolDevicePairParams* p = (ProtocolDevicePairParams*)data;
            g_printf("[APP CMD] device pair auth=%s ep_mac=%s case_mac=%s sn=%s\r",
                     p->auth_code, p->ep_mac, p->case_mac, p->label_sn);
            int r = rdx_app_device_pair_handle(p->auth_code, p->ep_mac, p->label_sn);
            ops->device_pair_ack_indicate((u8)((r < 0) ? 1 : 0));
            break;
        }

        case PROTOCOL_EVENT_CMD_DEVICE_UNPAIR: {
            g_printf("[APP CMD] device unpair\r");
            int r = rdx_app_device_unpair_handle();
            ops->device_unpair_ack_indicate((u8)((r < 0) ? 1 : 0));
            break;
        }
#endif /* RDX_PRODUCT_IS_CHARGE_CASE */

        case PROTOCOL_EVENT_CMD_AUDIO_STREAM: {
            if(!data || len < sizeof(ProtocolAudioStreamParams)) break;
            ops->audio_stream_play((const ProtocolAudioStreamParams*)data);
            break;
        }

        case PROTOCOL_EVENT_CMD_RECORD: {
            rdx_app_record_cmd_request_t *request;
            if(!data || len < sizeof(Record_info)) break;
            Record_info *info = (Record_info *)data;
            request = malloc(sizeof(*request));
            if (!request) {
                r_printf("[RDX_RECORD] command allocation failed\r");
                break;
            }
            memcpy(&request->info, info, sizeof(request->info));
            if (!rdx_ble_session_rdx_token_capture(&request->token, 1)) {
                r_printf("[RDX_RECORD] drop command without active RDX owner\r");
                free(request);
                break;
            }
            int msg[3];
            msg[0] = (int)rdx_app_record_cmd_on_app_core;
            msg[1] = 1;
            msg[2] = (int)request;
            if(os_taskq_post_type("app_core", Q_CALLBACK, 3, msg)){
                r_printf("record cmd app_core post err\r");
                free(request);
            }
            break;
        }

#if TDX_HAS_FLASHNOTE_ABILITY
        /* V24 闪记开/停: 本项目不实现闪记业务, 仅打印事件用于通道联调 */
        case PROTOCOL_EVENT_CMD_FLASHNOTE: {
            if(!data || len < 1) break;
            u8 fn_cmd = *(u8*)data;
            r_printf("[APP CMD] flashnote cmd=%u (no app impl, swallowed)\r", fn_cmd);
            break;
        }
#endif

#if TDX_HAS_RECMARK_ABILITY
        /* V24 录音标记: 单字节 source, 直接调 record 层加 mark */
        case PROTOCOL_EVENT_CMD_RECMARK: {
            if(!data || len < 1) break;
            u8 src = *(u8*)data;
            rdx_record_add_mark(src);
            break;
        }
#endif

        default:
            break;
    }
}

/**************************************************************************
 * function: rdx_app_tasks_init
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_tasks_init(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
#if defined(__UUX_FILE__)
	rdx_uxfile_init();
#endif

    //rdx ble server initial.
    rdx_ble_server_init();

    //key action executor initial (after BLE server / HOGP submodule).
    rdx_hogp_key_action_init();

    //formal APP keymap protocol and persisted active keymap.
    rdx_hogp_keymap_config_init();

    //ble send task init.
    protocol_cbs.rdx_protocol_cb = rdx_app_protocol_handle;
    rdx_protocol_task_create(&protocol_cbs);
    g_protocol_ops = rdx_protocol_get_indicate_ops();
	
	//do wifi regist.
#if RDX_WIFI_ENABLE
    xxp_uart_register_wifi_cfg(&wifi_cfg);
    rdx_wifi_event_register(rdx_app_wifi_event_handle);
#endif

#if (TCFG_CHARGE_POWERON_ENABLE == 1)
    if (get_charge_online_flag()) {
        rdx_app_charge_start();
        rdx_ble_server_auto_shut_down_enable(0);
    }
#endif
    //do sd mem first check.
    // rdx_uxfile_device_sd_mem_check();

    rdx_dut_init();

    rdx_app_init_flag = true;
}

/**************************************************************************
 * function: rdx_led_hardware_init
 * description: 独立的LED硬件初始化，可在充电模式/关机充电等非BT模式下调用
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_led_hardware_init(void)
{
    if (led_pt0807_config.initialized) {
        return;
    }
    if (led_pt0807_init(&led_pt0807_config, LED_PT0807_SPI1, LED_PT0807_DATA_PORT_IO, 1) == 0) {
        g_printf("===== %s --> LED PT0807 init success\r", __func__);
        if (rdx_led_ctrl_init(&led_pt0807_config) == 0) {
            g_printf("===== %s --> LED Ctrl init success\r", __func__);
        } else {
            g_printf("===== %s --> LED Ctrl init failed\r", __func__);
        }
    } else {
        g_printf("===== %s --> LED PT0807 init failed\r", __func__);
    }
}

/**************************************************************************
 * function: rdx_app_all_init
 * description:
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_all_init(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    g_printf("-----------------------------------------------------------\r");
    g_printf("====== %s --> protocol version = %d \r", __func__, rdx_protocol_get_version());
    g_printf("-----------------------------------------------------------\r");
    rdx_app_init_flag = false;
    poweron_ready_flag = false;
    emmc_poweroff_check_timer = 0;
    rdx_app_hold_record_reset();

    // 在按键事件处理之前初始化 record_status.
    rdx_record_set_default();

    rdx_init_crc32_table();

    //reset auto shutdown.
    sys_get_auto_off_time();
    rdx_ble_server_auto_shut_down_enable(1);

    //vm init.
    rdx_vm_init();

    //auth info init.
    rdx_vm_auth_info_init();

#if RDX_PRODUCT_IS_CHARGE_CASE
    //load paired earphone info from VM (耳机仓 配对, 603 专用).
    rdx_vm_read_ep_info_fromVM();
    //ep_info loaded, repack readchardata with correct ep_mac.
    rdx_app_earphone_pack_readchardata();
#endif

    //record task init.
	rdx_record_task_create();
#if TCFG_RDX_LOCAL_PLAYBACK_ENABLE
    rdx_playback_init();
#endif

    //wifi init.
#if RDX_WIFI_ENABLE
    memset(&wifiInfo, 0, sizeof(wifiInfo));
#endif

#if (RDX_AI_TRANSLATE_SUPPORT == 1)
    memset(&aiModeInfo, 0, sizeof(AImodeInfo));
#endif

    //wifi power shutoff.
#if RDX_WIFI_ENABLE
    gpio_set_mode(IO_PORT_SPILT(WIFI_POWER_PORT_IO), PORT_HIGHZ);
#endif

    //power on vdd.
    gpio_set_mode(IO_PORT_SPILT(VDD_POWER_PORT_IO), PORT_OUTPUT_HIGH);

    //rtc init.
    rdx_rtc_init();
    
    //spi irq init.
#if RDX_WIFI_ENABLE
    rdx_spi_init_irq();
#endif

    //dip switch power init.
#if TCFG_DIP_SWITCH_POWER_ENABLE
    rdx_dip_switch_init();
#endif

    u8 err_boot = rdx_record_err_reboot_flag_read_from_vm();
    if(err_boot == 1){
        //unnormal.
        RecordStatus *rp = rdx_record_get_status();

        rdx_app_tasks_init();

        //restart record.
        if(rp->scene == RECORD_SCENE_CHAT){
            app_send_message(APP_MSG_RECORD_CHAT_MODE, 0);
        }else{
            app_send_message(APP_MSG_RECORD_CALL_MODE, 0);
        }

        rdx_record_err_reboot_flag_write_into_vm(0);        
    }else{
        //normal.
    #if (RDX_SUPPORT_MOTOR == 1)
        motor_init();
    #endif

        //LED PT0807 init and test.
        rdx_led_hardware_init();
        rdx_app_emmc_poweroff_check();

        rdx_app_tasks_init();
    }
}

/**************************************************************************
 * function: rdx_app_all_exit
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_all_exit(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    log_info("rdx_app_all_exit\n");

    // BLE exit
    rdx_ble_server_exit();
}

/**************************************************************************
 * function: sdmmc_set_power
 * description: 
 * param (u8) enable
 * return (*)
 **************************************************************************/
void sdmmc_set_power(u8 enable)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    sd_set_power(enable);
    if (enable) {
        // Enable power to the SD card
        printf("SD card power on\n");
    } else {
        // Disable power to the SD card
        printf("SD card power off\n");
        // gpio_set_mode(IO_PORT_SPILT(VDD_POWER_PORT_IO), PORT_HIGHZ);
    }
}

/**************************************************************************
 * function: rdx_app_idle_handle
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
static void rdx_app_idle_handle(void* priv)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    // RecordStatus* rp = rdx_record_get_status();
    // RdxWifiInfo* pw = rdx_app_get_wifi_info();
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    y_printf("====== %s \r", __func__);

    rdx_app_emmc_poweroff_check_timer_stop();

#if (RDX_RTC_PATH_SEL == RDX_RTC_PATH_SOFTWARE)
    //store rtc timestamp before entering idle, jiffies will be lost during idle.
    rdx_rtc_store_timestamp();
    rdx_rtc_restore_timer_stop();
    rdx_rtc_restore_timer_start();
#endif

    //stop ble.
    rdx_ble_server_exit();

    //close peripheral.

    poweroff_ready_flag = false;
    key_press_record_ready_flag = false;
    rdx_app_hold_record_reset();

    xxp_uart_set_wifi_default_flag(false);

    // rdx_protocol_task_free();
    rdx_record_task_free();
    rdx_uxfile_task_free();

    sd_set_power(0);
    gpio_set_mode(IO_PORT_SPILT(IO_PORTC_01), PORT_HIGHZ);
    gpio_set_mode(IO_PORT_SPILT(IO_PORTC_02), PORT_HIGHZ);

    // PB4 已改为 WiFi CS 使用，不再设置为高阻态
    // PB5 已改为充满检测使用，不再设置为高阻态

    gpio_set_mode(IO_PORT_SPILT(IO_PORTC_04), PORT_HIGHZ);
    gpio_set_mode(IO_PORT_SPILT(IO_PORTC_05), PORT_HIGHZ);
    
#if RDX_WIFI_ENABLE
    gpio_set_mode(IO_PORT_SPILT(WIFI_POWER_PORT_IO), PORT_HIGHZ);
#endif
    gpio_set_mode(IO_PORT_SPILT(VDD_POWER_PORT_IO), PORT_HIGHZ);
}

/**************************************************************************
 * function: rdx_app_enter_idle
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_enter_idle(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    RecordStatus* rp = rdx_record_get_status();
#if RDX_WIFI_ENABLE
    RdxWifiInfo* pw = rdx_app_get_wifi_info();
#endif
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(app_is_idle == TRUE){
        y_printf("rdx_app_enter_idle: app is idle now! \r");
        return;
    }
    app_is_idle = TRUE;

        //close record.
    if(rp->run != RECORD_STATE_STOP){
        rp->run = RECORD_STATE_STOP;
        rdx_record_process();
    }

    rdx_protocol_file_cmd_handle(RDX_APP_FILE_CMD_STOP);

#if RDX_WIFI_ENABLE
    if(pw->onoff == TRANSFER_BY_WIFI_ON){
        rdx_app_wifi_handle(TRANSFER_BY_WIFI_OFF);
    }
#endif

    rdx_app_idle_handle(0);
}

/**************************************************************************
 * function: rdx_app_auto_shutdown
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_auto_shutdown(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    y_printf("=== %s \r", __func__);
    
    rdx_app_emmc_poweron(1);

#if (RDX_SUPPORT_MOTOR == 1)
    //motor.
    rdx_app_motor_run_once();
#endif
    sys_timeout_add(NULL, rdx_app_enter_idle, 1500);    
}


#endif
