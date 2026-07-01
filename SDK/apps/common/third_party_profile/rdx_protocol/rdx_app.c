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

/*
 * Stage 4 checkpoint: 2242 lines / 94 functions / 0 handler registrations.
 * Stage 4 target: <=2000 lines / <=50 functions. Gap: 242 lines / 44 functions.
 * Remaining: ABI wrappers + lifecycle orchestration + key state machine.
 * See docs/7.RDX框架阶段4实施文档 for deferral to Stage 5.
 */

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

#include "power/power_manage.h"
#include "gpio_config.h"

#include "rdx_app_config.h"
#include "rdx_record.h"
#include "rdx_app.h"
#include "rdx_util.h"
#include "rdx_commonDef.h"
#include "rdx_ble_server.h"
#include "rdx_protocol.h"
#include "xxpUart.h"
#include "rdx_key.h"
#include "rdx_charge.h"
#include "rdx_rtc.h"
#include "rdx_uxfile.h"
#include "rdx_vm.h"
#include "rdx_log.h"
#include "rdx_jl_osal.h"
#include "rdx_board_config.h"
#include "rdx_board_hal.h"
#include "rdx_spi.h"
#include "rdx_battery.h"
#include "led_pt0807.h"
#include "rdx_led_ctrl.h"
#include "rdx_dut.h"
#include "rdx_wifi_event.h"
#include "rdx_event_bus.h"
#include "rdx_command_dispatch.h"
#include "rdx_wifi_service.h"
#include "rdx_ble_service.h"
#include "rdx_device_service.h"
#include "rdx_info_service.h"
#include "rdx_storage_service.h"
#include "rdx_record_service.h"
#include "rdx_time_service.h"
#include "rdx_clock_service.h"
#include "rdx_default_hooks.h"
#include "rdx_ops.h"
#include "rdx_jl_lifecycle.h"

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


/*******************************************************************************
* Structure and Enum Section
*******************************************************************************/

/******************************************************************************
* Global variable Section
******************************************************************************/
extern struct ble_task_param ble_task;

/*******************************************************************************
* Local variables Section
*******************************************************************************/
static bool rdx_app_init_flag = FALSE;

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

static bool rdx_ble_conn = FALSE;
static bool poweroff_ready_flag = 0;
static bool poweron_ready_flag = 0;

static u16 mode_switch_keep_timer = 0;
static u8 key_press_record_ready_flag = 0;

static bool app_is_idle = FALSE;

//-------------------------------------------------------------------------
// DUT mode: 功能已移至 rdx_dut.c, 这里保留兼容宏
#define rdx_dut_mode        (rdx_dut_is_in_mode())
//-------------------------------------------------------------------------

static DevBaseInfo devBaseInfo;

static u8 qr_code[256];

static int orig_sys_clk;

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
static const RdxWifiCfg wifi_cfg = {
    .ap_ssid            = WIFI_AP_SSID,
    .ap_password        = WIFI_AP_PASSWORD,
    .dynamic_psw_enable = WIFI_AP_SSID_PSW_DYN_GENERATE,
    .ssid_suffix_mode   = WIFI_AP_SSID_SUFFIX_MODE,
};

/******************************************************************************
* Function Declaration Section
******************************************************************************/ 
extern u8 get_remote_dev_company(void);
extern void rdx_protocol_record_trigger_indicate(RecordStatus* d, bool factor);
extern void rdx_ble_server_app_disconnect(void);
extern void sd_set_power(u8 enable);
extern void power_set_soft_poweroff();
extern void rdx_protocol_task_free(void);
extern void rdx_uxfile_task_free(void);
extern void sys_enter_soft_poweroff(enum poweroff_reason reason);
extern int rdx_ble_server_reset_local_name(void);
extern void xxp_uart_set_wifi_default_flag(bool flag);
extern RecordStatus* rdx_record_get_status(void);
extern int rdx_protocol_task_create(RdxProtocolCallbacks *cb);
extern int rdx_record_task_create(void);
extern void motor_init(void);
extern u32 sdfile_get_disk_capacity(void);
extern u32 sdfile_flash_addr2cpu_addr(u32 offset);
extern void rdx_ble_server_adv_data_changed(void);
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

void rdx_app_emmc_poweroff_check(void);

void rdx_app_emmc_poweron(u8 check_en);
void rdx_app_auto_shutdown(void);
void rdx_app_emmc_poweroff_check_timer_stop(void);

void xxp_wifi_tcp_file_stop_indicate(void);

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

/**************************************************************************
 * function: rdx_app_get_poweroff_flag
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
bool rdx_app_get_poweroff_flag(void)
{
    
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
    (void)priv;
    rdx_app_bt_shutdown();
}

static void rdx_app_enter_idle_timer_cb(void *priv)
{
    (void)priv;
    rdx_app_enter_idle();
}

/**************************************************************************
 * function: rdx_app_reset_delay_cb
 * description: 
 * param (void) *priv
 * return (*)
 **************************************************************************/
void rdx_app_reset_delay_cb(void *priv)
{

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
    rdx_device_service_reboot();
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
    
    u8 max_vol = app_audio_get_max_volume();
    log_info("cur_vol is:%d, max:%d\n", volume, app_audio_get_max_volume());
    u8 rdx_sync_valume = (int)(volume * 100 / max_vol);
    if(g_protocol_ops) g_protocol_ops->volume_indicate(rdx_sync_valume);
}

/**************************************************************************
 * function: rdx_app_earphone_key_remap
 * description: 
 * param (int) *value
 * param (int) *msg
 * return (*)
 **************************************************************************/
void rdx_app_earphone_key_remap(int *value, int *msg)
{
    struct key_event *key = (struct key_event *)msg;
    int index = key->event;     
    u8 *pk_l = NULL;
    u8 *pk_r = NULL;
    RecordStatus* rp = rdx_record_get_status();
    RdxWifiInfo* p = rdx_app_get_wifi_info();
    bool format_state = rdx_uxfile_sd_format_status_check();
    // g_printf("key_remap: 0x%x, 0x%x, 0x%x, 0x%x \r", index, msg[0], msg[1], key->value);
    if(key->value != 0){
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

    if (app_in_mode(APP_MODE_IDLE)){
        // g_printf("%s --> in idle mode now! \r", __func__);
        pk_r = key_table_incharge_r;
    }else{
        if(app_is_idle == TRUE){
            pk_r = key_table_idle_r;
            *value = pk_r[index];
            return;
        }
        //wifi open?
        if(p->onoff == TRANSFER_BY_WIFI_ON){
            // g_printf("%s --> in wifi mode now! \r", __func__);
            pk_r = key_table_wifi_r;
        }else{
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
    ApInfo* p = xxp_uart_get_wifi_AP_info();
    
    memset(&devBaseInfo, 0, sizeof(DevBaseInfo));
    //get data.
    devBaseInfo.dev_type = RDX_SEL_DEVICE;
    memcpy(devBaseInfo.bt_mac, bt_get_mac_addr(), 6);
    le_controller_get_mac(devBaseInfo.ble_mac);
    
    rdx_auth_info_t* p_auth = rdx_vm_get_auth_info();
    memcpy(devBaseInfo.wifi_mac, p->mac_bytes, 6);
    memcpy(devBaseInfo.auth, p_auth->AuthKey, RDX_BLE_DEVICE_AUTH_KEY_SIZE);
    //ble mac.
    sprintf(devBaseInfo.bt_mac_str, "%02X%02X%02X%02X%02X%02X", devBaseInfo.bt_mac[5], devBaseInfo.bt_mac[4], devBaseInfo.bt_mac[3], devBaseInfo.bt_mac[2], devBaseInfo.bt_mac[1], devBaseInfo.bt_mac[0]);
    sprintf(devBaseInfo.ble_mac_str, "%02X%02X%02X%02X%02X%02X", devBaseInfo.ble_mac[5], devBaseInfo.ble_mac[4], devBaseInfo.ble_mac[3], devBaseInfo.ble_mac[2], devBaseInfo.ble_mac[1], devBaseInfo.ble_mac[0]);

    b_printf("=====> ble mac --> %02X%02X%02X%02X%02X%02X", devBaseInfo.ble_mac[5], devBaseInfo.ble_mac[4], devBaseInfo.ble_mac[3], devBaseInfo.ble_mac[2], devBaseInfo.ble_mac[1], devBaseInfo.ble_mac[0]);
    sprintf(devBaseInfo.wifi_mac_str, "%02X%02X%02X%02X%02X%02X", p->mac_bytes[0], p->mac_bytes[1], p->mac_bytes[2], p->mac_bytes[3], p->mac_bytes[4], p->mac_bytes[5]);

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

    struct bt_event *bt = (struct bt_event *)msg;

    y_printf("\r====== rdx_app_bt_status_event_handler event: %d \r", bt->event);
    switch (bt->event) {
    case BT_STATUS_INIT_OK:
        rdx_os_timer_add(rdx_app_bt_shutdown_delay_timer_cb, NULL, 1000);
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
    struct bt_event *bt = (struct bt_event *)msg;

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
    extern void clr_device_in_page_list();

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
    rdx_device_service_poweroff_cb(priv);
}

void rdx_app_normal_poweroff(void)
{
    poweroff_ready_flag = false;
    key_press_record_ready_flag = false;
    rdx_device_service_soft_poweroff();
}

/**************************************************************************
 * function: rdx_app_motor_run_once
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_motor_run_once(void)
{
    
rdx_hook_motor_start(500);
}

/**************************************************************************
 * function: rdx_app_record_state_upload_timer_cb
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_record_state_upload_timer_cb(void* priv)
{
    rdx_record_service_upload_timer_cb(priv);
}

/**************************************************************************
 * function: rdx_app_record_state_upload_timer_stop
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_record_state_upload_timer_stop(void)
{
    rdx_record_service_upload_timer_stop();
}

void rdx_app_record_state_upload_timer_start(void)
{
    rdx_record_service_upload_timer_start();
}

/**************************************************************************
 * function: rdx_app_device_record_handle
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_device_record_handle(u8 scene)
{
    rdx_record_service_device_record_handle(scene);
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
    rdx_err_t err = rdx_device_service_pair(au_code, mac_str, label_sn);
    return RDX_IS_ERR(err) ? -1 : 0;
}

/**************************************************************************
 * function: rdx_app_device_unpair_handle
 * description: 处理仓解除配对.
 * param (*)
 * return 0 = 成功, <0 = 失败 (协议层会上报 ack=1)
 **************************************************************************/
int rdx_app_device_unpair_handle(void)
{
    rdx_err_t err = rdx_device_service_unpair();
    return RDX_IS_ERR(err) ? -1 : 0;
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
    
    y_printf("------ rdx_app_switch_keep_timer_stop \r");
    if(mode_switch_keep_timer){
        rdx_os_timer_del(mode_switch_keep_timer);
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
    RecordStatus* rp = rdx_record_get_status();
    y_printf("\n------ rdx_app_switch_keep_timer_cb \r");
    rdx_app_switch_keep_timer_stop();

    //check the last mode.

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
    
    y_printf("rdx_app_switch_keep_timer_restart --> mode_switch_keep_timer: %d \r", mode_switch_keep_timer);
    if(mode_switch_keep_timer){
        rdx_os_timer_re_run(mode_switch_keep_timer);
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
    
    y_printf("rdx_app_switch_keep_timer_start --> mode_switch_keep_timer: %d \r", mode_switch_keep_timer);
    if(mode_switch_keep_timer == 0){
        mode_switch_keep_timer = rdx_os_timer_add(rdx_app_switch_keep_timer_cb, NULL, RDX_APP_MODE_SWITCH_KEEP_TIMEOUT);
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
    y_printf("%s --> cmd: %s, value: %s \r", __func__, cmd, value);

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
    RecordStatus* rp = rdx_record_get_status();
    if(rdx_dut_mode){
        rdx_dut_key_handle(APP_MSG_SINGLE_CLICK);
    }else{
    #if TDX_HAS_RECMARK_ABILITY
        if(rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME){
            if(!get_ota_status() && rdx_app_get_wifi_info()->onoff != TRANSFER_BY_WIFI_ON){
                rdx_record_add_mark(RDX_MARK_SOURCE_KEY);
                return;
            }
        }
    #endif
        // 非 DUT 模式下，按键单击重新唤醒快速广播
        if(rdx_app_get_wifi_info()->onoff != TRANSFER_BY_WIFI_ON
            && rp->run != RECORD_STATE_START
            && rp->run != RECORD_STATE_RESUME
            && !get_ota_status()){
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
    
    if(rdx_dut_mode){
        rdx_dut_key_handle(APP_MSG_DOUBLE_CLICK);
    }else{
        if(rdx_app_get_wifi_info()->onoff == TRANSFER_BY_WIFI_ON || get_ota_status()){
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
    RecordStatus* rp = rdx_record_get_status();
    if(rdx_dut_mode){
        rdx_dut_key_handle(APP_MSG_TRIPLE_CLICK);
    }else{
        if(rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME){
            log_info("====== %s --> 非DUT模式下录音中,三击功能无效 \r", __func__);
            return;
        }
        if(rdx_app_get_wifi_info()->onoff == TRANSFER_BY_WIFI_ON || get_ota_status()){
            r_printf("====== %s --> not on normal status, do nothing \r", __func__);
            return;
        }
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
    RecordStatus* rp = rdx_record_get_status();

    if(rdx_dut_mode){
        rdx_dut_key_handle(APP_MSG_QUADRUPLE_CLICK);
    }else{
        if(rdx_app_get_wifi_info()->onoff == TRANSFER_BY_WIFI_ON){
            r_printf("====== %s --> wifi is on \r", __func__);
            return;
        }

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
        y_printf("===%s --> wifi_mac: %s \r", __func__, devBaseInfo.wifi_mac_str);
        y_printf("===%s --->label_sn: %s \r", __func__, devBaseInfo.label_sn);
    
        memset(qr_code, 0, sizeof(qr_code));
        sprintf((char *)qr_code, "%s\t%s\t%s\t%s\r", devBaseInfo.auth, devBaseInfo.ble_mac_str, devBaseInfo.wifi_mac_str, devBaseInfo.label_sn);
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
    return rdx_wifi_service_get_wifi_info();
}

/**************************************************************************
 * function: rdx_app_wifi_handle
 * description: 
 * param (u8) cmd
 * return (*)
 **************************************************************************/
void rdx_app_wifi_handle(u8 cmd)
{
    b_printf("=== %s --> cmd = %d \r", __func__, cmd);
    if (cmd == TRUE) {
        rdx_wifi_power_on();
    } else {
        rdx_wifi_power_off();
    }
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
    u8 comm_addr[6];
    u16 con_hdl = rdx_ble_server_get_conn_handle();
    int ret = false;  //默认不拦截消息
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
            rdx_hook_led_restore_system_state();
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
                if (rdx_os_task_post_callback0("app_core", rdx_record_process) != RDX_OK) {
                    r_printf("%s record taskq post err \n", __func__);
                }
            }
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
            rdx_hook_motor_start(200);
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

    #if (RDX_SEL_DEVICE == DEVICE_RDX_BJ_T2403) || (RDX_SEL_DEVICE == DEVICE_DACOM_CC_T2401) || (RDX_SEL_DEVICE == DEVICE_1MORE_CC_T2402) || (RDX_SEL_DEVICE == DEVICE_ZENCORD_CC_T2616)
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
    int key_msg = 0;
    // g_printf("rdx app key msg receive:0x%x\n", msg[1]);
    
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
void rdx_app_format_cb(u8 result)
{
    rdx_storage_service_format_cb(result);
}

void rdx_app_format_handle(void)
{
    rdx_storage_service_format_handle();
}

/**************************************************************************
 * function: rdx_app_get_record_mode
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
u8 rdx_app_get_record_mode(void)
{
    return rdx_record_service_get_mode();
}

/**************************************************************************
 * function: 
 * description: 
 * param (u8) d
 * return (*)
 **************************************************************************/
void rdx_app_set_record_mode(u8 d)
{
    rdx_record_service_set_mode(d);
}

void rdx_record_mode_active_check(bool show)
{
    rdx_record_service_mode_active_check(show);
}

/**************************************************************************
 * function: rdx_app_record_switch
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_record_switch(u8 orig_scene)
{
    rdx_record_service_switch(orig_scene);
}

/* ---- clock wrappers (impl migrated to rdx_clock_service, Stage 5) ---- */

bool rdx_app_clk_is_locked(void)
{
    return rdx_clock_service_is_locked();
}

void rdx_app_clk_unlock(const char *task_name)
{
    rdx_clock_service_unlock(task_name);
}

void rdx_app_clk_lock(const char *task_name, int clk)
{
    rdx_clock_service_lock(task_name, clk);
}

void rdx_app_clk_unlock_with_timer(const char *task_name)
{
    rdx_clock_service_unlock_with_timer(task_name);
}

void rdx_app_clk_lock_with_timer(const char *task_name, int clk)
{
    rdx_clock_service_lock_with_timer(task_name, clk);
}

/* ---- eMMC power wrappers (impl migrated to rdx_device_service, Stage 3) ---- */

void rdx_app_do_emmc_reset(void)
{
    rdx_device_service_do_emmc_reset();
}

void rdx_app_emmc_poweron(u8 check_en)
{
    rdx_device_service_emmc_poweron(check_en);
}

void rdx_app_emmc_poweroff(void)
{
    rdx_device_service_emmc_poweroff();
}

void rdx_app_emmc_poweroff_check_timer_stop(void)
{
    rdx_device_service_emmc_poweroff_check_timer_stop();
}

void rdx_app_emmc_poweroff_check(void)
{
    rdx_device_service_emmc_poweroff_check();
}

/**************************************************************************
 * function: rdx_app_wifi_event_handle
 * description: Sole consumer of the WiFi event bus (rdx_wifi_event.h).
 *   Threading note: callback may run on protocol send task / sys timer task
 *   / app_core depending on producer. Heavy work (record / BLE / UI) must be
 *   re-posted to app_core via os_taskq_post_type.
 **************************************************************************/
static void rdx_app_wifi_event_handle(RdxWifiEvent event, void *data, u32 len)
{
    switch (event) {
        case RDX_WIFI_EVENT_CTRL: {
            if (!data || len < 1) break;
            u8 cmd = *(u8 *)data;
            if (rdx_os_task_post_callback1("app_core",
                 (void (*)(void *))rdx_app_wifi_handle, (void *)(int)cmd) != RDX_OK) {
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

/**
 * 协议层 → app 业务统一事件回调入口
 *   @param event 协议事件类型 (rdx_protocol.h 中 ProtocolEvents)
 *   @param data  事件数据指针, 具体类型见 ProtocolEvents 各项注释
 *   @param len   事件数据长度 (字节)
 *
 */
static void rdx_app_protocol_handle(ProtocolEvents event, void* data, u32 len)
{
	rdx_cmd_dispatch(event, data, len);
}

/**************************************************************************
 * function: rdx_app_tasks_init
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_tasks_init(void)
{
    
#if defined(__UUX_FILE__)
	rdx_uxfile_init();
#endif

    //rdx ble server initial.
    rdx_ble_server_init();

    //ble send task init.
    protocol_cbs.rdx_protocol_cb = rdx_app_protocol_handle;
    rdx_protocol_task_create(&protocol_cbs);
    g_protocol_ops = rdx_protocol_get_indicate_ops();
	
	//do wifi regist.
    xxp_uart_register_wifi_cfg(&wifi_cfg);
    rdx_wifi_event_register(rdx_app_wifi_event_handle);

#if (TCFG_CHARGE_POWERON_ENABLE == 1)
    if (get_charge_online_flag()) {
        rdx_app_charge_start();
        rdx_ble_server_auto_shut_down_enable(0);
    }
#endif

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
    if (rdx_hook_led_init() != RDX_OK) {
        return; /* product hook handled LED init */
    }
    if (led_pt0807_init(&led_pt0807_config, rdx_board_led_spi_instance(), rdx_board_led_data_io(), 1) == 0) {
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

/*
 * 启动诊断日志 — 阶段 1 新增。
 * 上电时打印 product/board/chip/sdk/transport/storage/RTC path 及关键引脚，
 * 便于问题定位和板型确认。
 */
static void rdx_print_startup_info(void)
{
    const rdx_board_config_t *cfg = rdx_board_get_config();

    /*
     * FIRMWARE_NAME 来自 rdx_app_config.h。
     * RTC path 通过 rdx_time_ops_t.is_hw_rtc 查询，不再直接访问 RDX_RTC_PATH_SEL。
     * sdk_version_info_get() 由 JL SDK 提供，运行时返回 SDK 版本字符串。
     */
    const rdx_time_ops_t *to_startup = rdx_time_ops_get();
    const char *sdk_ver = sdk_version_info_get();
    RDX_LOGI("startup: product=%s board=%s chip=%s sdk=%s",
             FIRMWARE_NAME, cfg->board_name, cfg->chip_family,
             (sdk_ver && sdk_ver[0]) ? sdk_ver : "unknown");
    RDX_LOGI("startup: transport=spi storage=syscfg rtc_path=%s",
             (to_startup && to_startup->is_hw_rtc) ? "hardware" : "software");
    RDX_LOGI("startup: pins wifi_power=%x vdd_power=%x led=%x spi_cs=%x",
             cfg->wifi_power_io, cfg->vdd_power_io,
             cfg->led_data_io, cfg->spi_cs_io);
}

void rdx_app_all_init(void)
{
    
    g_printf("-----------------------------------------------------------\r");
    g_printf("====== %s --> protocol version = %d \r", __func__, rdx_protocol_get_version());
    g_printf("-----------------------------------------------------------\r");

    rdx_print_startup_info();

    rdx_app_init_flag = false;
    poweron_ready_flag = false;

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

    //wifi init.
    rdx_wifi_service_reset_state();

#if (RDX_AI_TRANSLATE_SUPPORT == 1)
    memset(&aiModeInfo, 0, sizeof(AImodeInfo));
#endif

    //wifi power shutoff.
    rdx_board_wifi_power_off();

    //power on vdd.
    rdx_board_vdd_power_on();

    //rtc init.
    rdx_rtc_init();

    //spi irq init: moved to rdx_spi_init_master_hd() so the handshake pin
    //is initialized from board config at the moment WiFi is powered on.

        /* Phase 3: lifecycle ops validate + early_init */
    bool vtable_all_ok = true;
    {
        const rdx_lifecycle_ops_t *lc = rdx_lifecycle_ops_get();
        if (rdx_lifecycle_ops_validate(lc) != RDX_OK) {
            RDX_LOGE("lifecycle ops validate failed");
            vtable_all_ok = false;
        } else {
            RDX_LOGI("startup: vtable lifecycle_ops=ok");
        }
        if (lc && lc->early_init) {
            lc->early_init();
        }
    }
    {
        const rdx_time_ops_t *to = rdx_time_ops_get();
        if (rdx_time_ops_validate(to) != RDX_OK) {
            RDX_LOGE("time ops validate failed");
            vtable_all_ok = false;
        } else {
            RDX_LOGI("startup: vtable time_ops=ok");
        }
    }
    {
        const rdx_wifi_transport_ops_t *wo = rdx_wifi_transport_ops_get();
        if (rdx_wifi_transport_ops_validate(wo) != RDX_OK) {
            RDX_LOGE("wifi transport ops validate failed");
            vtable_all_ok = false;
        } else {
            RDX_LOGI("startup: vtable wifi_transport_ops=ok");
        }
    }

    if (!vtable_all_ok) {
        RDX_LOGE("startup: vtable validation failed, abort rdx_app_all_init");
        return;
    }

    /* Phase 2: service layer init — must run before any task starts */
    rdx_event_bus_init();
    rdx_cmd_dispatch_init();
    rdx_info_service_init();
    rdx_wifi_service_init();
    rdx_ble_service_init();
    rdx_device_service_init();
    rdx_storage_service_init();
    rdx_record_service_init();
    rdx_time_service_init();

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
    rdx_hook_motor_start(0);  /* init if present */

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
    
    log_info("rdx_app_all_exit\n");

    // BLE exit
    rdx_ble_server_exit();
}

/* ---- sdmmc_set_power wrapper (impl migrated to rdx_storage_service, Stage 5) ---- */

void sdmmc_set_power(u8 enable)
{
    rdx_storage_service_sdmmc_set_power(enable);
}

/**************************************************************************
 * function: rdx_app_idle_handle
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
static void rdx_app_idle_handle(void* priv)
{
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

    xxp_uart_set_wifi_default_flag(false);

    rdx_record_task_free();
    rdx_uxfile_task_free();

    sd_set_power(0);
    rdx_board_shutdown_io_state();
    rdx_board_wifi_power_off();
    rdx_board_vdd_power_off_highz();
}

/**************************************************************************
 * function: rdx_app_enter_idle
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_enter_idle(void)
{
    RecordStatus* rp = rdx_record_get_status();
    RdxWifiInfo* pw = rdx_app_get_wifi_info();
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

    if(pw->onoff == TRANSFER_BY_WIFI_ON){
        rdx_app_wifi_handle(TRANSFER_BY_WIFI_OFF);
    }

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
    
    y_printf("=== %s \r", __func__);
    
    rdx_app_emmc_poweron(1);

    rdx_hook_motor_start(500);
    rdx_os_timer_add(rdx_app_enter_idle_timer_cb, NULL, 1500);
}

#endif
