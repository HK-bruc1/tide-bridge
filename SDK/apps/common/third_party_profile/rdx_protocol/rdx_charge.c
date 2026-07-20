/*=====================================================================================
 HEADER NAME: rdx_charge.c
 MODULE NAME: application module.
 
 PRE-INCLUDE FILES DESCRIPTION: 	
 
 GENERAL DESCRIPTION: 	
 	This File will gather functions that special handle msg(UserEvent,Timer) from 
 	Intergration.These functions don't be changed by project changed.
 =======================================================================================	
 Revision History: 
 ---------------------------------------
 Author: sheng.dong
 Date: 2025-05-08 10:01:47
 LastEditors: sheng.dong
 LastEditTime: 2025-05-08 10:03:53
 FilePath: \SDK\apps\common\third_party_profile\rdx_protocol\rdx_charge.c
 
 Self-documenting Code
=====================================================================================*/

/******************************************************************************
* Include files
******************************************************************************/ 
#include "rdx_charge.h"
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
#include "rdx_battery.h"
#include "rdx_led_ctrl.h"
#include "rdx_default_hooks.h"
#include "rdx_board_config.h"
#include "rdx_board_hal.h"
#include "rdx_jl_osal.h"

#if (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN)


#if RDX_HAS_SK4558_CHARGER
#include "sk4558.h"
#endif


/******************************************************************************
* Macro Define Section
******************************************************************************/ 


/******************************************************************************
* Structure and Enum Section
******************************************************************************/ 


/******************************************************************************
* Global Variables Section
******************************************************************************/ 


/******************************************************************************
* Local Variables Section
******************************************************************************/ 
static u8 cur_charge_state = RDX_CHARGE_OUT;
static u8 orig_charge_state = RDX_CHARGE_OUT;

static u16 incharge_full_check_timer = 0;
static u16 incharge_full_poweroff_timer = 0;
static u16 incharge_batPercent_show_timer = 0;

static u8 orig_bat = 0;
static u8 full_confirm_count = 0;

/******************************************************************************
* Function Declaration Section
******************************************************************************/ 
extern bool rdx_app_get_dut_status(void);
extern bool rdx_app_get_dut_motor_flag(void);
extern bool rdx_app_get_dut_oled_flag(void);
extern void rdx_battery_inchargeBatPer_show(void);

extern void rdx_battery_delay_show_batLevel_timer_stop(void);
extern BatLevel rdx_battery_get_incharge_batLevel(void);
extern void rdx_battery_set_incharge_batLevel(BatLevel level);
extern void rdx_battery_incharge_batLevel_reset(void);
extern void rdx_app_emmc_poweron(u8 check_en);

void rdx_app_charge_full_poweroff_timer_stop(void);

/******************************************************************************
* Function Section
******************************************************************************/ 

/**************************************************************************
 * function: rdx_app_get_charge_state
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
u8 rdx_app_get_charge_state(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    return cur_charge_state;
}

/**************************************************************************
 * function: rdx_app_set_charge_state
 * description: 
 * param (u8) st
 * return (*)
 **************************************************************************/
void rdx_app_set_charge_state(u8 st)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    cur_charge_state = st;
    y_printf("===== %s -->cur_charge_state = %d \r", __func__, cur_charge_state);
}

/**************************************************************************
 * function: rdx_app_incharge_batPercent_show_cb
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_incharge_batPercent_show_cb(void* priv)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    bool is_in_dut;
    bool is_in_motor_test;
    bool is_in_oled_test;
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    is_in_dut = rdx_app_get_dut_status();
    is_in_motor_test = rdx_app_get_dut_motor_flag();
    is_in_oled_test = rdx_app_get_dut_oled_flag();

#if (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_OLED) || (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_BOTH_OLED_EMMC)
    if(is_in_dut == TRUE){
        RecordStatus* rp = rdx_record_get_status();
        y_printf("APP_MSG_OLED_BAT_SHOW --> in DUT mode now! \r");
        if(is_in_motor_test == TRUE){
            // OLED 功能已删除 // os_taskq_post_msg("oled_show_task", 1, OLED_SHOW_DUT_MOTOR_TEST);
        }else if(is_in_oled_test == TRUE){
            // OLED 功能已删除 // os_taskq_post_msg("oled_show_task", 1, OLED_SHOW_DUT_OLED_TEST);
        }else if(rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME){
            // OLED 功能已删除
        }else{
            // OLED 功能已删除 // os_taskq_post_msg("oled_show_task", 1, OLED_SHOW_DUT);
        }
    }else{
        // OLED 功能已删除
    }
#endif
}

/**************************************************************************
 * function: rdx_app_incharge_batPercent_show_stop
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_incharge_batPercent_show_stop(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(incharge_batPercent_show_timer){
        rdx_os_timer_del(incharge_batPercent_show_timer);
        incharge_batPercent_show_timer = 0;
    }
}

/**************************************************************************
 * function: rdx_app_incharge_batPercent_show_start
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_incharge_batPercent_show_start(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(incharge_batPercent_show_timer == 0){
        incharge_batPercent_show_timer = rdx_os_timer_add(rdx_app_incharge_batPercent_show_cb, NULL, RDX_APP_INCHARGE_BATTERY_SHOW_TIMEOUT);
    }
}

/**************************************************************************
 * function: rdx_app_incharge_full_poweroff_timer_cb
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_incharge_full_poweroff_timer_cb(void* priv)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    rdx_app_charge_full_poweroff_timer_stop();

#if (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_OLED) || (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_BOTH_OLED_EMMC)
    //oled show task free.
    // OLED 功能已删除 // os_taskq_post_msg("oled_show_task", 1, OLED_SHOW_SHUTOFF); 
#endif


    rdx_board_charge_poweroff_io_state();
    rdx_board_wifi_power_off();


    //充满电不用关机，拔掉后关机
    // os_time_dly(50);
    // power_awakeup_gpio_enable(IO_CHGFL_DET, 0);
    // //system shutoff.
    // power_set_soft_poweroff();
    // sys_enter_soft_poweroff(POWEROFF_NORMAL);
    // rdx_app_normal_poweroff();
}

/**************************************************************************
 * function: rdx_app_charge_full_poweroff_timer_stop
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_charge_full_poweroff_timer_stop(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(incharge_full_poweroff_timer){
        rdx_os_timer_del(incharge_full_poweroff_timer);
        incharge_full_poweroff_timer = 0;
    }
}

/**************************************************************************
 * function: rdx_app_charge_full_timer_to_poweroff
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_charge_full_timer_to_poweroff(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(incharge_full_poweroff_timer == 0){
        incharge_full_poweroff_timer = rdx_os_timer_add(rdx_app_incharge_full_poweroff_timer_cb, NULL, RDX_APP_INCHARGE_FULL_POWEROFF_TIMEOUT);
    }
}

/**************************************************************************
 * function: rdx_app_charge_full
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_charge_full(void)
{
#if RDX_PRODUCT_IS_CHARGE_CASE
    if(cur_charge_state == RDX_CHARGE_FULL){
        return;
    }

    bool hw_full = charge_check_is_full();
    u8 cur_bat = rdx_battery_get_percent();
    y_printf("====== %s --> hw=%d, bat=%d\r", __func__, hw_full, cur_bat);

    if(hw_full != TRUE && cur_bat < 100){
        return;
    }
    orig_bat = 0;
#else
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    BatLevel value = rdx_battery_get_incharge_batLevel();
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(cur_charge_state == RDX_CHARGE_FULL){
        return;
    }

    if(charge_check_is_full() == TRUE){
        if(value == BAT_LEVEL_4){
            return;
        }
        y_printf("====== %s --> CHARGE FULL! \r", __func__);
        if(value < BAT_LEVEL_5){
            rdx_battery_set_incharge_batLevel(BAT_LEVEL_6);
            rdx_battery_delay_show_batLevel_timer_stop();
        #if (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_OLED) || (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_BOTH_OLED_EMMC)
            // OLED 功能已删除 // os_taskq_post_msg("oled_show_task", 1, OLED_SHOW_INCHARGE_BAT_LEVEL_100);
        #endif
            return;
        }
        orig_bat = 0;
    }else{
        y_printf("====== %s --> CHARGE NOT REALLY FULL! \r", __func__);
        if(value >= BAT_LEVEL_5){
            rdx_battery_delay_show_batLevel_timer_stop();
        #if (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_OLED) || (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_BOTH_OLED_EMMC)
            // OLED 功能已删除 // os_taskq_post_msg("oled_show_task", 1, OLED_SHOW_INCHARGE_BAT_LEVEL_100);
        #endif
        }
        return;
    }

#if (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_OLED) || (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_BOTH_OLED_EMMC)
    OLED_Show_Incharge_Blink_Stop();
#endif
#endif /* RDX_PRODUCT_IS_CHARGE_CASE */

#if (RDX_BJ_VERSION == BJ_BOARD_VERSION_01) || (RDX_BJ_VERSION == BJ_BOARD_VERSION_00)
    //shut off 4558.
    charge_onoff(FALSE);
#endif
    if(incharge_full_check_timer){
        rdx_os_timer_periodic_del(incharge_full_check_timer);
        incharge_full_check_timer = 0;
    }
    rdx_app_set_charge_state(RDX_CHARGE_FULL); 
    
    // 充满电：绿色常亮
    rdx_hook_led_set_scene(RDX_LED_SCENE_CHARGE_FULL);

#if (TCFG_CHARGE_POWERON_ENABLE == 0)
    // rdx_app_charge_full_timeout_stop();
    //timer to poweroff.
    rdx_app_charge_full_timer_to_poweroff();
#endif
}

/**************************************************************************
 * function: rdx_app_incharge_full_check_timer_cb
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_incharge_full_check_timer_cb(void* priv)
{
    g_printf("==== %s ====\r", __func__);
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    bool full_check = FALSE;
    u8 cur_bat = 0;

    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    rdx_protocol_update_dev_battery_level();
    cur_bat = rdx_battery_get_percent();
    full_check = charge_check_is_full();
    if(full_check == TRUE){
        // 硬件满直接触发，无需防抖
        y_printf("====== %s --> CHARGE FULL! hw=%d, bat=%d\r", __func__, full_check, cur_bat);
        rdx_app_charge_full();
        full_confirm_count = 0;
    }else if(cur_bat >= 100){
        // 软件满需要防抖，连续20次才确认（防浮压回落，5s×60=300s）
        if(full_confirm_count < 255) full_confirm_count++;
        if(full_confirm_count >= 60){
            y_printf("====== %s --> CHARGE FULL! hw=%d, bat=%d, cnt=%d\r", __func__, full_check, cur_bat, full_confirm_count);
            rdx_app_charge_full();
            full_confirm_count = 0;
        }
    }else{
        full_confirm_count = 0;
        if(orig_bat != cur_bat){
            rdx_battery_inchargeBatPer_show();
            // 充电过程中根据电量更新灯效
            rdx_hook_led_set_charge_state_by_battery(cur_bat);
            orig_bat = cur_bat;
        }
    }
}

/**************************************************************************
 * function: rdx_app_charge_start_handle
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_charge_start_handle(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(incharge_full_check_timer == 0){
        incharge_full_check_timer = rdx_os_timer_periodic_add(rdx_app_incharge_full_check_timer_cb, NULL, RDX_APP_INCHARGE_FULL_CHECK_TIMEOUT);
    }
}

/**************************************************************************
 * function: rdx_app_charge_stop
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_charge_stop(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(cur_charge_state == RDX_CHARGE_OUT){
        return;
    }

#if (RDX_BJ_VERSION == BJ_BOARD_VERSION_01) || (RDX_BJ_VERSION == BJ_BOARD_VERSION_00)
    //shut off 4558.
    charge_onoff(FALSE);
#endif

    orig_bat = 0;

    rdx_app_set_charge_state(RDX_CHARGE_OUT); 

    rdx_app_charge_full_poweroff_timer_stop();
    
    // 充电拔出后，恢复系统当前状态对应的灯效
    rdx_hook_led_restore_system_state();

#if (TCFG_CHARGE_POWERON_ENABLE == 0)
    
#if (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_OLED) || (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_BOTH_OLED_EMMC)
    //oled show task free.
    // OLED 功能已删除 // os_taskq_post_msg("oled_show_task", 1, OLED_SHOW_SHUTOFF); 
#endif
    //dip switch deinit.

    power_set_mode(TCFG_LOWPOWER_POWER_SEL);
    // power_awakeup_gpio_enable(IO_CHGFL_DET, 0);
    // //system shutoff.
    // power_set_soft_poweroff();
    // rdx_app_normal_poweroff();
    //do reset.

#else
#if (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_OLED) || (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_BOTH_OLED_EMMC)

    //oled show task free.
    if(rdx_app_get_dut_status() == TRUE){
        RecordStatus* rp = rdx_record_get_status();
        y_printf("APP_MSG_OLED_BAT_SHOW --> in DUT mode now! \r");
        if(rdx_app_get_dut_motor_flag() == TRUE){
            // OLED 功能已删除 // os_taskq_post_msg("oled_show_task", 1, OLED_SHOW_DUT_MOTOR_TEST);
        }else if(rdx_app_get_dut_oled_flag() == TRUE){
            // OLED 功能已删除 // os_taskq_post_msg("oled_show_task", 1, OLED_SHOW_DUT_OLED_TEST);
        }else if(rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME){
            // OLED 功能已删除
        }else{
            // OLED 功能已删除 // os_taskq_post_msg("oled_show_task", 1, OLED_SHOW_DUT);
        }
    }else{
        // OLED 功能已删除，不再发送 APP_MSG_OLED_SHUTOFF 事件
    }
#endif
#endif
}

/**************************************************************************
 * function: rdx_app_charge_start
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_charge_start(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(cur_charge_state == RDX_CHARGE_IN){
        return;
    }
    //use LDO.
    power_set_mode(PWR_LDO15);

    rdx_app_set_charge_state(RDX_CHARGE_IN);
    orig_bat = 0;
    full_confirm_count = 0;

#if (RDX_BJ_VERSION == BJ_BOARD_VERSION_02) || (RDX_BJ_VERSION == BJ_BOARD_VERSION_03)
    //init charge.
    charge_task_init();
#endif

    rdx_battery_incharge_batLevel_reset();

#if (TCFG_CHARGE_POWERON_ENABLE == 0)
    //ldo gpio init.
    rdx_app_emmc_poweron(0);

#if (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_OLED) || (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_BOTH_OLED_EMMC)
    //oled show task init.
    oled_task_create();
#endif
#endif
    // motor_init(); //dons-- 20250515 do not start motor.
    vbat_check_init();

    //charge full detect.
    if (rdx_os_task_post_callback0("app_core", rdx_app_charge_start_handle) != RDX_OK) {
        r_printf("%s record taskq post err \n", __func__);
    }

    y_printf("====== %s --> INCHARGE! \r", __func__);

    // 充电开始，确保LED硬件已初始化，然后根据当前电量设置充电灯效
    rdx_led_hardware_init();
    u8 cur_bat = rdx_battery_get_percent();
    rdx_hook_led_set_charge_state_by_battery(cur_bat);
}

/**************************************************************************
 * function: rdx_app_charge_prepare
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_app_charge_prepare(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    y_printf("====== %s --> prepare charge! \r", __func__);
    //before charge start, close all running jobs.
    //ota.
    if (get_ota_status()){
        rdx_ota_stop();
    }
    RecordStatus* rp = rdx_record_get_status();
    RdxWifiInfo* pw = rdx_app_get_wifi_info();
    //close record.
    if(rp->run != RECORD_STATE_STOP){
        rp->run = RECORD_STATE_STOP;
        rdx_record_process();
    }
    //close wifi.
    if(pw->onoff == TRANSFER_BY_WIFI_ON){
        rdx_app_wifi_handle(TRANSFER_BY_WIFI_OFF);
    }

    //restore rtc for software path only, hardware path saved by poweroff uninitcall.
#if (RDX_RTC_PATH_SEL == RDX_RTC_PATH_SOFTWARE)
    rdx_rtc_store_timestamp();
    rdx_rtc_restore_timer_stop();
    rdx_rtc_restore_timer_start();
#endif
}

/**************************************************************************
 * function: rdx_app_battery_msg_handler
 * description: 
 * param (int) *msg
 * return (*)
 **************************************************************************/
int rdx_app_battery_msg_handler(int *msg)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    g_printf("rdx_app_battery_msg_handler :0x%x\n", msg[0]);

#if (TCFG_CHARGE_POWERON_ENABLE == 0)
    switch (msg[0]) {
        case CHARGE_EVENT_LDO5V_IN:
        case CHARGE_EVENT_LDO5V_KEEP:
            {
                if (!app_in_mode(APP_MODE_IDLE)){
                    g_printf("%s --> CHARGE_EVENT_LDO5V_IN, but not in idle mode \r", __func__);
                    break;
                }
                y_printf("%s --> BAT_MSG_CHARGE_START, charge in, rdx_app_get_charge_state() = %d \r", __func__, rdx_app_get_charge_state());

                rdx_app_emmc_poweroff();
                rdx_app_emmc_poweroff_check_timer_stop();

                //init charge status.
                rdx_app_charge_start();
            }
            break;

        case CHARGE_EVENT_LDO5V_OFF:
            y_printf("%s --> BAT_MSG_CHARGE_LDO5V_OFF, charge out, rdx_app_get_charge_state() = %d \r", __func__, rdx_app_get_charge_state());
#if TCFG_CHARGE_OFF_POWERON_EN
            rdx_cpu_reset();
#else
            //拔出关机
            //关机直接复用 RDX + JL 原生软关机链，避免只进入伪 idle。
            rdx_app_normal_poweroff();
#endif
            break;

        default:
            break;
    }
#else
    switch (msg[0]) {
        case CHARGE_EVENT_LDO5V_KEEP:
        case CHARGE_EVENT_LDO5V_IN:
            {
                y_printf("====== %s --> INCHARGE! \r", __func__);

                //init charge status.
                rdx_app_charge_start();

                //charge full detect.
                rdx_ble_server_auto_shut_down_enable(0);
            }
            break;

        case CHARGE_EVENT_LDO5V_OFF:
            printf("%s --> BAT_MSG_CHARGE_LDO5V_OFF, charge out, rdx_app_get_charge_state() = %d \r", __func__, rdx_app_get_charge_state());
            rdx_app_charge_stop();
            rdx_ble_server_auto_shut_down_enable(1);
            break;

        default:
            break;
    }
#endif
    return false; 
}


APP_MSG_PROB_HANDLER(rdx_app_battery_msg_entry) = {
    .owner      = 0xff,
    .from       = MSG_FROM_BATTERY,
    .handler    = rdx_app_battery_msg_handler,
};


#endif
