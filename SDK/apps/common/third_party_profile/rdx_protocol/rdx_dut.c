/*=====================================================================================
 HEADER NAME: rdx_dut.c
 MODULE NAME: DUT (Device Under Test) application module.
 
 PRE-INCLUDE FILES DESCRIPTION: 	
 
 GENERAL DESCRIPTION: 	
 	DUT mode functions for factory testing (PIN project).
    Ported from BJ Tide project, adapted for PIN hardware (LED + motor, no OLED).
    
    PIN DUT测试项目:
    - 单击: LED灯光测试 (ft_oled / 兼容命名)
    - 双击: 马达震动测试 (ft_motor)
    - 三击: 录音测试     (ft_rec)
    - 四击: WiFi测试      (ft_wifi)
    - 五击: 格式化存储     (ft_format)
    - 六击: 关机          (ft_poweroff)
 =======================================================================================	
 Revision History: 
 ---------------------------------------
 Author: auto-ported from BJ Tide project
 Date: 2026-02-24
 FilePath: \SDK\apps\common\third_party_profile\rdx_protocol\rdx_dut.c
 
 Self-documenting Code
=====================================================================================*/

/******************************************************************************
* Include files
******************************************************************************/ 
#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".rdx_dut.data.bss")
#pragma data_seg(".rdx_dut.data")
#pragma const_seg(".rdx_dut.text.const")
#pragma code_seg(".rdx_dut.text")
#endif

#include "rdx_dut.h"
#include "rdx_record.h"
#include "service/rdx_record_service.h"
#include "rdx_protocol.h"
#include "rdx_ble_server.h"
#include "rdx_uxfile.h"
#include "rdx_app.h"
#include "rdx_app_config.h"
#include "rdx_charge.h"
#include "motor.h"
#include "app_msg.h"
#include "app_main.h"
#include "poweroff.h"
#include "rdx_led_ctrl.h"
#include "rdx_default_hooks.h"
#include "rdx_board_config.h"
#include "rdx_jl_osal.h"
#include "rdx_board_hal.h"
#include "rdx_jl_storage.h"

/******************************************************************************
* Macro Define Section
******************************************************************************/ 
#define DUT_LOG(str, ...)   y_printf("[DUT]%s,L%d--> "str,__FUNCTION__,__LINE__,##__VA_ARGS__)

/******************************************************************************
* External Function Declaration
******************************************************************************/ 
extern void rdx_app_bt_open(void);
extern void rdx_app_bt_shutdown(void);
extern void rdx_app_motor_run_once(void);
extern void rdx_app_wifi_handle(u8 cmd);
extern void rdx_app_device_record_handle(u8 scene);
extern void rdx_app_emmc_poweron(u8 check_en);
extern void rdx_app_emmc_poweroff_check_timer_stop(void);
extern void rdx_app_emmc_poweroff_check(void);
extern void rdx_spp_init(void);
extern void rdx_spp_exit(void);
extern void rdx_ble_server_app_disconnect(void);
extern void rdx_ble_server_adv_data_changed(void);
extern void rdx_ble_server_auto_shut_down_enable(u8 enable);
extern int rdx_ble_server_adv_enable(u8 enable);
extern void bt_bredr_enter_dut_mode(u8 a, u8 b);
extern void bt_bredr_exit_dut_mode(void);
extern u8 get_ota_status(void);
extern bool client_file_is_transfer_in_progress(void);
extern void rdx_app_normal_poweroff(void);

/******************************************************************************
* Function Declaration Section
******************************************************************************/ 
static void rdx_dut_motor_timer_cb(void *priv);
static void rdx_dut_show(void);
static void rdx_dut_format_stop(void);
static void rdx_dut_normal_poweroff_timer_cb(void *priv);

/******************************************************************************
* Local Variables Section
******************************************************************************/ 
static rdx_dut_info_t rdx_dut_info = {
    .dut_mode = FALSE,
    .key_dut_disabled = FALSE,
    .current_func = DUT_FUNC_NONE,
    .motor_timer = 0,
    .motor_run = FALSE,
};

/******************************************************************************
* DUT命令类型定义（用于异步处理）
******************************************************************************/ 
typedef enum {
    DUT_CMD_DUT_MODE = 0,
    DUT_CMD_OLED,
    DUT_CMD_MOTOR,
    DUT_CMD_REC,
    DUT_CMD_WIFI,
    DUT_CMD_FORMAT,
    DUT_CMD_POWEROFF,
    DUT_CMD_FINALPACK_END,
    DUT_CMD_KEY_DUT_ENABLE,
} DUT_CMD_TYPE;

/******************************************************************************
* Function Section - 基础接口
******************************************************************************/ 

/**************************************************************************
 * function: rdx_dut_cmd_async_handle
 * description: DUT命令异步处理函数（在app_core任务中执行）
 **************************************************************************/
static void rdx_dut_cmd_async_handle(u8 cmd_type, u8 onoff)
{
    DUT_LOG("Async handle: cmd_type=%d, onoff=%d\r", cmd_type, onoff);
    
    switch(cmd_type) {
        case DUT_CMD_DUT_MODE:
            rdx_dut_msg_handle();
            break;
            
        case DUT_CMD_OLED:
            if(onoff == 1) {
                rdx_dut_oled_start();
            } else {
                rdx_dut_oled_stop();
            }
            break;
            
        case DUT_CMD_MOTOR:
            if(onoff == 1) {
                rdx_dut_motor_start();
            } else {
                rdx_dut_motor_stop();
            }
            break;
            
        case DUT_CMD_REC:
            if(onoff == 1) {
                rdx_dut_rec_start();
            } else {
                rdx_dut_rec_stop();
            }
            break;
            
        case DUT_CMD_WIFI:
            if(onoff == 1) {
                rdx_dut_wifi_start();
            } else {
                rdx_dut_wifi_stop();
            }
            break;
            
        case DUT_CMD_FORMAT:
            if(onoff == 1) {
                rdx_dut_format_start();
            } else {
                rdx_dut_format_stop();
            }
            break;
            
        case DUT_CMD_POWEROFF:
            rdx_dut_poweroff();
            break;
            
        case DUT_CMD_FINALPACK_END:
            rdx_dut_finalpack_end();
            break;
            
        case DUT_CMD_KEY_DUT_ENABLE:
            rdx_dut_key_dut_enable();
            break;
            
        default:
            DUT_LOG("Unknown cmd_type: %d\r", cmd_type);
            break;
    }
}

/**************************************************************************
 * function: rdx_dut_init
 * description: DUT模块初始化，从VM读取掉电保存的配置
 **************************************************************************/
void rdx_dut_init(void)
{
    u8 vm_value = 0xFF;
    rdx_err_t ret = rdx_storage_read(RDX_STORAGE_KEY_DUT_DISABLED,
                                     &vm_value, sizeof(vm_value));
    
    DUT_LOG("Init: VM read ret=%d, vm_value=0x%02X\r", ret, vm_value);
    
    if(ret == RDX_OK && vm_value == KEY_DUT_DISABLED_FLAG){
        rdx_dut_info.key_dut_disabled = true;
        DUT_LOG("Init: key_dut_disabled = 1 (disabled)\r");
    }else{
        rdx_dut_info.key_dut_disabled = false;
        DUT_LOG("Init: key_dut_disabled = 0 (enabled)\r");
    }
}

/**************************************************************************
 * function: rdx_dut_get_info
 **************************************************************************/
rdx_dut_info_t* rdx_dut_get_info(void)
{
    return &rdx_dut_info;
}

/**************************************************************************
 * function: rdx_dut_is_in_mode
 **************************************************************************/
bool rdx_dut_is_in_mode(void)
{
    return rdx_dut_info.dut_mode;
}

/**************************************************************************
 * function: rdx_dut_get_current_func_name
 **************************************************************************/
static const char* rdx_dut_get_current_func_name(void)
{
    switch(rdx_dut_info.current_func){
        case DUT_FUNC_OLED:     return "LED";
        case DUT_FUNC_MOTOR:    return "MOTOR";
        case DUT_FUNC_REC:      return "REC";
        case DUT_FUNC_WIFI:     return "WIFI";
        case DUT_FUNC_FORMAT:   return "FORMAT";
        default:                return "NONE";
    }
}

/**************************************************************************
 * function: rdx_dut_close_current_func
 **************************************************************************/
void rdx_dut_close_current_func(void)
{
    switch(rdx_dut_info.current_func){
        case DUT_FUNC_OLED:
            rdx_dut_oled_stop();
            break;
        case DUT_FUNC_MOTOR:
            rdx_dut_motor_stop();
            break;
        case DUT_FUNC_REC:
            rdx_dut_rec_stop();
            break;
        case DUT_FUNC_WIFI:
            rdx_dut_wifi_stop();
            break;
        case DUT_FUNC_FORMAT:
            break;
        default:
            break;
    }
    rdx_dut_info.current_func = DUT_FUNC_NONE;
}

/**************************************************************************
 * function: rdx_dut_show
 * description: DUT模式显示（PIN: LED灯效）
 **************************************************************************/
static void rdx_dut_show(void)
{
    DUT_LOG("in DUT mode now!\r");
    rdx_hook_led_set_scene(RDX_LED_SCENE_DUT_ENTER);
}

/******************************************************************************
* Function Section - 1. LED灯光测试（兼容BJ的OLED测试命名）
******************************************************************************/ 

/**************************************************************************
 * function: rdx_dut_oled_start
 * description: PIN: 开始LED灯光测试（常亮白色测试灯效）
 **************************************************************************/
void rdx_dut_oled_start(void)
{
    DUT_LOG("LED test START\r");
    
    if(rdx_dut_info.current_func != DUT_FUNC_NONE){
        DUT_LOG("Blocked! Current test: [%s]\r", rdx_dut_get_current_func_name());
        return;
    }
    
    rdx_dut_info.current_func = DUT_FUNC_OLED;
    
    /* PIN: LED全亮白色作为灯光测试 */
    rdx_hook_led_set_scene(RDX_LED_SCENE_BLE_CONNECTED);
}

/**************************************************************************
 * function: rdx_dut_oled_stop
 **************************************************************************/
void rdx_dut_oled_stop(void)
{
    DUT_LOG("LED test STOP\r");
    
    if(rdx_dut_info.current_func == DUT_FUNC_OLED){
        rdx_dut_info.current_func = DUT_FUNC_NONE;
    }
    
    rdx_dut_show();
}

/**************************************************************************
 * function: rdx_dut_oled_is_running
 **************************************************************************/
bool rdx_dut_oled_is_running(void)
{
    return (rdx_dut_info.current_func == DUT_FUNC_OLED);
}

/******************************************************************************
* Function Section - 2. 马达震动测试
******************************************************************************/ 

/**************************************************************************
 * function: rdx_dut_motor_timer_cb
 * description: 马达震动定时器回调（震动2秒，停1秒，循环）
 **************************************************************************/
static void rdx_dut_motor_timer_cb(void *priv)
{
    if(rdx_dut_info.current_func != DUT_FUNC_MOTOR){
        return;
    }
    
    if(rdx_dut_info.motor_run == TRUE){
        motor_off();
        rdx_dut_info.motor_run = FALSE;
        rdx_os_timer_periodic_modify(rdx_dut_info.motor_timer, 1000);
    }else{
        motor_on();
        rdx_dut_info.motor_run = TRUE;
        rdx_os_timer_periodic_modify(rdx_dut_info.motor_timer, 2000);
    }
}

/**************************************************************************
 * function: rdx_dut_motor_start
 **************************************************************************/
void rdx_dut_motor_start(void)
{
    DUT_LOG("Motor test START\r");
    
    if(rdx_dut_info.current_func != DUT_FUNC_NONE){
        DUT_LOG("Blocked! Current test: [%s]\r", rdx_dut_get_current_func_name());
        return;
    }
    
    rdx_dut_info.current_func = DUT_FUNC_MOTOR;
    
    if(rdx_dut_info.motor_timer == 0){
        rdx_dut_info.motor_timer = rdx_os_timer_periodic_add(rdx_dut_motor_timer_cb, NULL, 2000);
        motor_on();
        rdx_dut_info.motor_run = TRUE;
    }
}

/**************************************************************************
 * function: rdx_dut_motor_stop
 **************************************************************************/
void rdx_dut_motor_stop(void)
{
    DUT_LOG("Motor test STOP\r");
    
    if(rdx_dut_info.motor_timer){
        rdx_os_timer_periodic_del(rdx_dut_info.motor_timer);
        rdx_dut_info.motor_timer = 0;
    }
    
    motor_off();
    rdx_dut_info.motor_run = FALSE;
    
    if(rdx_dut_info.current_func == DUT_FUNC_MOTOR){
        rdx_dut_info.current_func = DUT_FUNC_NONE;
    }
    
    rdx_dut_show();
}

/**************************************************************************
 * function: rdx_dut_motor_is_running
 **************************************************************************/
bool rdx_dut_motor_is_running(void)
{
    return (rdx_dut_info.current_func == DUT_FUNC_MOTOR);
}

/******************************************************************************
* Function Section - 3. 录音测试
******************************************************************************/ 

/**************************************************************************
 * function: rdx_dut_rec_start
 **************************************************************************/
void rdx_dut_rec_start(void)
{
    DUT_LOG("Record test START\r");
    
    if(rdx_dut_info.current_func != DUT_FUNC_NONE){
        DUT_LOG("Blocked! Current test: [%s]\r", rdx_dut_get_current_func_name());
        return;
    }
    
    rdx_dut_info.current_func = DUT_FUNC_REC;
    
    rdx_record_activity_t activity = RDX_RECORD_ACTIVITY_PAUSED;
    rdx_record_scene_t scene = RDX_RECORD_SCENE_CHAT;
    (void)rdx_record_service_get_activity(&activity);
    (void)rdx_record_service_get_scene(&scene);
    DUT_LOG("Record activity = %d, scene = %d\r", activity, scene);
    if(activity == RDX_RECORD_ACTIVITY_IDLE){
        u8 legacy_scene = (scene == RDX_RECORD_SCENE_CALL) ? RECORD_SCENE_CALL : RECORD_SCENE_CHAT;
        rdx_app_device_record_handle(legacy_scene);
    }
}

/**************************************************************************
 * function: rdx_dut_rec_stop
 **************************************************************************/
void rdx_dut_rec_stop(void)
{
    DUT_LOG("Record test STOP\r");

    (void)rdx_record_service_stop_now(RDX_RECORD_STOP_DUT);
    
    if(rdx_dut_info.current_func == DUT_FUNC_REC){
        rdx_dut_info.current_func = DUT_FUNC_NONE;
    }
    
    rdx_dut_show();
}

/**************************************************************************
 * function: rdx_dut_rec_is_running
 **************************************************************************/
bool rdx_dut_rec_is_running(void)
{
    return (rdx_dut_info.current_func == DUT_FUNC_REC);
}

/******************************************************************************
* Function Section - 4. WiFi测试
******************************************************************************/ 

/**************************************************************************
 * function: rdx_dut_wifi_start
 **************************************************************************/
void rdx_dut_wifi_start(void)
{
    DUT_LOG("WiFi test START\r");
    
    if(rdx_dut_info.current_func != DUT_FUNC_NONE){
        DUT_LOG("Blocked! Current test: [%s]\r", rdx_dut_get_current_func_name());
        return;
    }
    
    rdx_dut_info.current_func = DUT_FUNC_WIFI;
    
    rdx_app_wifi_handle(TRANSFER_BY_WIFI_ON);
    DUT_LOG("WiFi module power on\r");
}

/**************************************************************************
 * function: rdx_dut_wifi_stop
 **************************************************************************/
void rdx_dut_wifi_stop(void)
{
    DUT_LOG("WiFi test STOP\r");
    
    rdx_app_wifi_handle(TRANSFER_BY_WIFI_OFF);
    DUT_LOG("WiFi module power off\r");
    
    if(rdx_dut_info.current_func == DUT_FUNC_WIFI){
        rdx_dut_info.current_func = DUT_FUNC_NONE;
    }
    
    rdx_dut_show();
}

/**************************************************************************
 * function: rdx_dut_wifi_is_running
 **************************************************************************/
bool rdx_dut_wifi_is_running(void)
{
    return (rdx_dut_info.current_func == DUT_FUNC_WIFI);
}

/******************************************************************************
* Function Section - 5. 格式化存储
******************************************************************************/ 

/**************************************************************************
 * function: rdx_dut_format_stop
 **************************************************************************/
static void rdx_dut_format_stop(void)
{
    if(rdx_dut_info.current_func == DUT_FUNC_FORMAT){
        rdx_dut_info.current_func = DUT_FUNC_NONE;
    }
    
    rdx_dut_show();
    DUT_LOG("Format stopped, DUT display restored\r");
}

/**************************************************************************
 * function: rdx_dut_format_cb
 **************************************************************************/
static void rdx_dut_format_cb(u8 result)
{
    DUT_LOG("Format callback, result: %d\r", result);
    
    if(rdx_dut_info.current_func == DUT_FUNC_FORMAT){
        rdx_dut_info.current_func = DUT_FUNC_NONE;
    }
    
    rdx_dut_show();
}

/**************************************************************************
 * function: rdx_dut_format_start
 **************************************************************************/
void rdx_dut_format_start(void)
{
    DUT_LOG("=== Format storage START ===\r");
    
    if(rdx_dut_info.current_func != DUT_FUNC_NONE){
        DUT_LOG("Blocked! Current test: [%s]\r", rdx_dut_get_current_func_name());
        return;
    }
    
    rdx_dut_info.current_func = DUT_FUNC_FORMAT;
    
    rdx_uxfile_device_sd_format(rdx_dut_format_cb);
}

/******************************************************************************
* Function Section - 6. 关机（船运模式）
******************************************************************************/ 

/**************************************************************************
 * function: rdx_dut_poweroff
 **************************************************************************/
void rdx_dut_poweroff(void)
{
    DUT_LOG("Power off (ship mode)\r");
    
    rdx_dut_close_current_func();
    rdx_dut_info.dut_mode = FALSE;
    
    rdx_hook_led_set_scene(RDX_LED_SCENE_DUT_EXIT);

    rdx_os_timer_add(rdx_dut_normal_poweroff_timer_cb, NULL, 200);
}

static void rdx_dut_normal_poweroff_timer_cb(void *priv)
{
    (void)priv;
    rdx_app_normal_poweroff();
}

/******************************************************************************
* Function Section - 7. 包装测试 / 按键进DUT禁用
******************************************************************************/ 

static bool g_finalpack_end_pending = false;

/**************************************************************************
 * function: rdx_dut_finalpack_end_poweroff
 **************************************************************************/
static void rdx_dut_finalpack_end_poweroff(void *priv)
{
    DUT_LOG("Finalpack end: Power off now\r");
    rdx_dut_poweroff();
}

/**************************************************************************
 * function: rdx_dut_finalpack_end_disconnect_ble
 **************************************************************************/
static void rdx_dut_finalpack_end_disconnect_ble(void *priv)
{
    DUT_LOG("Finalpack end: Disconnect BLE\r");
    rdx_ble_server_app_disconnect();
    rdx_os_timer_add(rdx_dut_finalpack_end_poweroff, NULL, 500);
}

/**************************************************************************
 * function: rdx_dut_finalpack_end_format_cb
 **************************************************************************/
void rdx_dut_finalpack_end_format_cb(u8 result)
{
    DUT_LOG("Finalpack end format cb, result: %d\r", result);
    
    if(!g_finalpack_end_pending){
        return;
    }
    g_finalpack_end_pending = false;
    
    rdx_os_timer_add(rdx_dut_finalpack_end_disconnect_ble, NULL, 500);
}

/**************************************************************************
 * function: rdx_dut_finalpack_end
 * description: 包装测试结束
 *              流程：禁止按键进DUT → 重置用户参数 → 格式化存储 → 断开BLE → 关机
 **************************************************************************/
void rdx_dut_finalpack_end(void)
{
    DUT_LOG("=== Finalpack End ===\r");
    
    rdx_dut_info.key_dut_disabled = true;
    
    rdx_vm_sys_reset_to_defaults();
    
    {
        u8 vm_value = KEY_DUT_DISABLED_FLAG;
        rdx_storage_write(RDX_STORAGE_KEY_DUT_DISABLED,
                          &vm_value, sizeof(vm_value));
        DUT_LOG("Disable key entry DUT, flag=0x%02X saved to VM\r", vm_value);
    }
    
    g_finalpack_end_pending = true;
    
    rdx_uxfile_device_sd_format(rdx_dut_finalpack_end_format_cb);
}

/**************************************************************************
 * function: rdx_dut_key_dut_enable
 **************************************************************************/
void rdx_dut_key_dut_enable(void)
{
    DUT_LOG("Key DUT ENABLED\r");
    
    rdx_dut_info.key_dut_disabled = false;
    
    u8 vm_value = 0xff;
    rdx_storage_write(RDX_STORAGE_KEY_DUT_DISABLED,
                      &vm_value, sizeof(vm_value));
    DUT_LOG("Saved to VM: key_dut_disabled = 0 (cleared flag)\r");
}

/**************************************************************************
 * function: rdx_dut_is_key_dut_disabled
 **************************************************************************/
bool rdx_dut_is_key_dut_disabled(void)
{
    return rdx_dut_info.key_dut_disabled;
}

/******************************************************************************
* Function Section - BLE指令处理
******************************************************************************/ 

/**************************************************************************
 * function: rdx_dut_ble_cmd_handle
 * description: DUT BLE指令处理入口
 *              命令异步发送到app_core任务执行
 **************************************************************************/
void rdx_dut_ble_cmd_handle(const char* cmd, const char* value)
{
    char* p = NULL;
    int ret;
    
    p = strstr(cmd, FT_DUT);
    if(p){
        u8 onoff = atoi(value);
        DUT_LOG("DUT cmd, onoff: %d, current_mode: %d\r", onoff, rdx_dut_info.dut_mode);
        
        if((onoff == 1 && !rdx_dut_info.dut_mode) || (onoff == 0 && rdx_dut_info.dut_mode)){
            ret = rdx_os_task_post_callback2("app_core", (void (*)(void *, void *))rdx_dut_cmd_async_handle, (void *)DUT_CMD_DUT_MODE, (void *)onoff);
            if(ret) {
                DUT_LOG("DUT taskq post err: %d\r", ret);
            }
        }else{
            DUT_LOG("DUT state already %s, skip\r", onoff ? "ON" : "OFF");
        }
        rdx_protocol_custom_msg_indicate(FT_DUT, (char*)value);
        return;
    }
    
    if(!rdx_dut_info.dut_mode){
        DUT_LOG("Not in DUT mode, ignore cmd: %s\r", cmd);
        return;
    }
    
    p = strstr(cmd, FT_OLED);
    if(p){
        u8 onoff = atoi(value);
        DUT_LOG("LED cmd, onoff: %d\r", onoff);
        
        ret = rdx_os_task_post_callback2("app_core", (void (*)(void *, void *))rdx_dut_cmd_async_handle, (void *)DUT_CMD_OLED, (void *)onoff);
        if(ret) {
            DUT_LOG("LED taskq post err: %d\r", ret);
        }
        rdx_protocol_custom_msg_indicate(FT_OLED, (char*)value);
        return;
    }
    
    p = strstr(cmd, FT_MOTOR);
    if(p){
        u8 onoff = atoi(value);
        DUT_LOG("Motor cmd, onoff: %d\r", onoff);
        
        ret = rdx_os_task_post_callback2("app_core", (void (*)(void *, void *))rdx_dut_cmd_async_handle, (void *)DUT_CMD_MOTOR, (void *)onoff);
        if(ret) {
            DUT_LOG("Motor taskq post err: %d\r", ret);
        }
        rdx_protocol_custom_msg_indicate(FT_MOTOR, (char*)value);
        return;
    }
    
    p = strstr(cmd, FT_REC);
    if(p){
        u8 onoff = atoi(value);
        DUT_LOG("Record cmd, onoff: %d\r", onoff);
        
        ret = rdx_os_task_post_callback2("app_core", (void (*)(void *, void *))rdx_dut_cmd_async_handle, (void *)DUT_CMD_REC, (void *)onoff);
        if(ret) {
            DUT_LOG("Record taskq post err: %d\r", ret);
        }
        rdx_protocol_custom_msg_indicate(FT_REC, (char*)value);
        return;
    }
    
    p = strstr(cmd, FT_WIFI);
    if(p){
        u8 onoff = atoi(value);
        DUT_LOG("WiFi cmd, onoff: %d\r", onoff);
        
        ret = rdx_os_task_post_callback2("app_core", (void (*)(void *, void *))rdx_dut_cmd_async_handle, (void *)DUT_CMD_WIFI, (void *)onoff);
        if(ret) {
            DUT_LOG("WiFi taskq post err: %d\r", ret);
        }
        rdx_protocol_custom_msg_indicate(FT_WIFI, (char*)value);
        return;
    }
    
    p = strstr(cmd, FT_FORMAT);
    if(p){
        DUT_LOG("Format cmd\r");
        
        ret = rdx_os_task_post_callback2("app_core", (void (*)(void *, void *))rdx_dut_cmd_async_handle, (void *)DUT_CMD_FORMAT, (void *)1);
        if(ret) {
            DUT_LOG("Format taskq post err: %d\r", ret);
        }
        rdx_protocol_custom_msg_indicate(FT_FORMAT, "0");
        return;
    }
    
    p = strstr(cmd, FT_POWEROFF);
    if(p){
        DUT_LOG("Poweroff cmd\r");
        rdx_protocol_custom_msg_indicate(FT_POWEROFF, "0");
        
        ret = rdx_os_task_post_callback2("app_core", (void (*)(void *, void *))rdx_dut_cmd_async_handle, (void *)DUT_CMD_POWEROFF, (void *)0);
        if(ret) {
            DUT_LOG("Poweroff taskq post err: %d\r", ret);
        }
        return;
    }
    
    p = strstr(cmd, FT_FINALPACK_END);
    if(p){
        DUT_LOG("Finalpack end cmd\r");
        rdx_protocol_custom_msg_indicate(FT_FINALPACK_END, "0");
        
        ret = rdx_os_task_post_callback2("app_core", (void (*)(void *, void *))rdx_dut_cmd_async_handle, (void *)DUT_CMD_FINALPACK_END, (void *)0);
        if(ret) {
            DUT_LOG("Finalpack end taskq post err: %d\r", ret);
        }
        return;
    }
    
    p = strstr(cmd, FT_KEY_DUT_ENABLE);
    if(p){
        DUT_LOG("Key DUT enable cmd\r");
        rdx_protocol_custom_msg_indicate(FT_KEY_DUT_ENABLE, "0");
        
        ret = rdx_os_task_post_callback2("app_core", (void (*)(void *, void *))rdx_dut_cmd_async_handle, (void *)DUT_CMD_KEY_DUT_ENABLE, (void *)0);
        if(ret) {
            DUT_LOG("Key DUT enable taskq post err: %d\r", ret);
        }
        return;
    }
}

/******************************************************************************
* Function Section - 按键处理
******************************************************************************/ 

/**************************************************************************
 * function: rdx_dut_key_handle
 * description: DUT按键事件处理
 * 单击 - LED灯光测试
 * 双击 - 马达震动测试
 * 三击 - 录音测试
 * 四击 - WiFi测试
 * 五击 - 格式化存储 (PIN: APP_MSG_BT_PAIR_SET_DEFAULT)
 * 六击 - 关机 (PIN: APP_MSG_SEXTUPLE_CLICK)
 **************************************************************************/
void rdx_dut_key_handle(int key_msg)
{
    if(!rdx_dut_info.dut_mode){
        return;
    }
    
    DUT_LOG("Key msg: 0x%x, current_func: %d\r", key_msg, rdx_dut_info.current_func);
    
    switch(key_msg){
        case APP_MSG_SINGLE_CLICK:
            if(rdx_dut_oled_is_running()){
                rdx_dut_oled_stop();
            }else{
                if(rdx_dut_info.current_func != DUT_FUNC_NONE){
                    DUT_LOG("Blocked! Current test: [%s]\r", rdx_dut_get_current_func_name());
                    break;
                }
                rdx_dut_oled_start();
            }
            break;
            
        case APP_MSG_DOUBLE_CLICK:
            if(rdx_dut_motor_is_running()){
                rdx_dut_motor_stop();
            }else{
                if(rdx_dut_info.current_func != DUT_FUNC_NONE){
                    DUT_LOG("Blocked! Current test: [%s]\r", rdx_dut_get_current_func_name());
                    break;
                }
                rdx_dut_motor_start();
            }
            break;
            
        case APP_MSG_TRIPLE_CLICK:
            if(rdx_dut_rec_is_running()){
                rdx_dut_rec_stop();
            }else{
                if(rdx_dut_info.current_func != DUT_FUNC_NONE){
                    DUT_LOG("Blocked! Current test: [%s]\r", rdx_dut_get_current_func_name());
                    break;
                }
                rdx_dut_rec_start();
            }
            break;
            
        case APP_MSG_QUADRUPLE_CLICK:
            if(rdx_dut_wifi_is_running()){
                rdx_dut_wifi_stop();
            }else{
                if(rdx_dut_info.current_func != DUT_FUNC_NONE){
                    DUT_LOG("Blocked! Current test: [%s]\r", rdx_dut_get_current_func_name());
                    break;
                }
                rdx_dut_wifi_start();
            }
            break;
            
        case APP_MSG_BT_PAIR_SET_DEFAULT:
            if(rdx_dut_info.current_func != DUT_FUNC_NONE){
                DUT_LOG("Blocked! Current test: [%s]\r", rdx_dut_get_current_func_name());
                break;
            }
            rdx_dut_format_start();
            break;
            
        case APP_MSG_SEXTUPLE_CLICK:
            if(rdx_dut_info.current_func != DUT_FUNC_NONE){
                DUT_LOG("Blocked! Current test running: [%s], poweroff denied\r", rdx_dut_get_current_func_name());
                break;
            }
            rdx_dut_poweroff();
            break;
            
        default:
            break;
    }
}

/******************************************************************************
* Function Section - DUT消息处理（APP_MSG_DUT）
******************************************************************************/ 

/**************************************************************************
 * function: rdx_dut_msg_handle
 * description: APP_MSG_DUT消息处理入口（进入/退出DUT模式）
 **************************************************************************/
void rdx_dut_msg_handle(void)
{
    bool record_running = rdx_record_service_is_running();

    if(rdx_dut_info.dut_mode == FALSE){
        /*--- 进入DUT模式 ---*/
        DUT_LOG("=== Enter DUT mode request ===\r");
        
        if(record_running) {
            DUT_LOG("Recording in progress, blocked!\r");
            return;
        }
        
        if(get_ota_status()) {
            DUT_LOG("OTA in progress, blocked!\r");
            return;
        }
        
        DUT_LOG("【 Enter DUT mode! 】\r");
        rdx_hook_dut_enter();
        
        RdxWifiInfo* pw = rdx_app_get_wifi_info();
        if(pw->onoff == TRANSFER_BY_WIFI_ON) {
            DUT_LOG("Closing WiFi transfer...\r");
            rdx_app_wifi_handle(TRANSFER_BY_WIFI_OFF);
        }
        
        rdx_dut_info.dut_mode = TRUE;
        rdx_dut_info.current_func = DUT_FUNC_NONE;
        
        rdx_app_emmc_poweron(0);
        rdx_app_emmc_poweroff_check_timer_stop();
        
        rdx_app_bt_open();
        bt_bredr_enter_dut_mode(0, 0);
        
        rdx_hook_led_set_scene(RDX_LED_SCENE_DUT_ENTER);
        
        rdx_ble_server_app_disconnect();
        rdx_ble_server_adv_enable(0);
        
        rdx_ble_server_auto_shut_down_enable(0);
        DUT_LOG("Auto shutdown disabled!\r");
        
        rdx_board_vdd_power_on();
        
        rdx_spp_init();
        
    }else{
        /*--- 退出DUT模式 ---*/
        DUT_LOG("【 Exit DUT mode! 】\r");
        rdx_hook_dut_exit();
        
        rdx_dut_info.dut_mode = FALSE;
        
        rdx_dut_close_current_func();
        
        bt_bredr_exit_dut_mode();
        rdx_spp_exit();
        rdx_app_bt_shutdown();
        
        rdx_ble_server_adv_data_changed();
        
        rdx_hook_led_set_scene(RDX_LED_SCENE_BLE_ADV_START);
        
        rdx_ble_server_auto_shut_down_enable(1);
        DUT_LOG("Auto shutdown enabled!\r");
        
        u8 s = rdx_app_get_charge_state();
        if(s == RDX_CHARGE_IN){
            /* PIN: 充电中恢复充电灯效，由rdx_charge模块自动管理 */
        }
    }
    
#if (RDX_SUPPORT_MOTOR == 1)    
    rdx_app_motor_run_once();
#endif
}

/******************************************************************************
* Function Section - DUT显示刷新
******************************************************************************/ 

/**************************************************************************
 * function: rdx_dut_show_refresh
 * description: DUT模式下的显示刷新（PIN: 仅打印日志，LED由各功能自行管理）
 **************************************************************************/
void rdx_dut_show_refresh(void)
{
    DUT_LOG("DUT show refresh, current_func=%d\r", rdx_dut_info.current_func);
    
    switch(rdx_dut_info.current_func){
        case DUT_FUNC_MOTOR:
        case DUT_FUNC_OLED:
        case DUT_FUNC_REC:
        case DUT_FUNC_WIFI:
            break;
        default:
            rdx_hook_led_set_scene(RDX_LED_SCENE_DUT_ENTER);
            break;
    }
}
