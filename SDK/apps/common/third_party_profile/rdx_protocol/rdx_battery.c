/*=====================================================================================
 HEADER NAME: rdx_battery.c
 MODULE NAME: application module.
 
 PRE-INCLUDE FILES DESCRIPTION: 	
 
 GENERAL DESCRIPTION: 	
 	This File will gather functions that special handle msg(UserEvent,Timer) from 
 	Intergration.These functions don't be changed by project changed.
 =======================================================================================	
 Revision History: 
 ---------------------------------------
 Author: sheng.dong
 Date: 2025-10-12 13:19:43
 LastEditors: sheng.dong
 LastEditTime: 2025-10-12 13:21:03
 FilePath: \SDK\apps\common\third_party_profile\rdx_protocol\rdx_battery.c
 
 Self-documenting Code
=====================================================================================*/

#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".rdx_ota.data.bss")
#pragma data_seg(".rdx_ota.data")
#pragma const_seg(".rdx_ota.text.const")
#pragma code_seg(".rdx_ota.text")
#endif

/******************************************************************************
* Include files
******************************************************************************/ 
#include "stdlib.h"
#include "sdk_config.h"
#include "app_msg.h"
#include "earphone.h"
#include "app_main.h"

#include "rdx_charge.h"
#include "rdx_battery.h"
#include "rdx_jl_osal.h"

/******************************************************************************
* Macro Define Section
******************************************************************************/ 
// APP_NINGQU_EN
//
// #define RDX_BAT_VOLT_THRESHOLD_100                          (4300)
// #define RDX_BAT_VOLT_THRESHOLD_90                           (4180)
// #define RDX_BAT_VOLT_THRESHOLD_80                           (4090)
// #define RDX_BAT_VOLT_THRESHOLD_70                           (3980)
// #define RDX_BAT_VOLT_THRESHOLD_60                           (3900)
// #define RDX_BAT_VOLT_THRESHOLD_50                           (3850)
// #define RDX_BAT_VOLT_THRESHOLD_40                           (3800)
// #define RDX_BAT_VOLT_THRESHOLD_30                           (3760)
// #define RDX_BAT_VOLT_THRESHOLD_20                           (3720)
// #define RDX_BAT_VOLT_THRESHOLD_10                           (3660)
// #define RDX_BAT_VOLT_THRESHOLD_0                            (3200)

// //in charge.
// #define RDX_BAT_VOLT_CHRG_THRESHOLD_100                     (4300)
// #define RDX_BAT_VOLT_CHRG_THRESHOLD_90                      (4270)
// #define RDX_BAT_VOLT_CHRG_THRESHOLD_80                      (4250)
// #define RDX_BAT_VOLT_CHRG_THRESHOLD_70                      (4150)
// #define RDX_BAT_VOLT_CHRG_THRESHOLD_60                      (4100)
// #define RDX_BAT_VOLT_CHRG_THRESHOLD_50                      (4000)
// #define RDX_BAT_VOLT_CHRG_THRESHOLD_40                      (3950)
// #define RDX_BAT_VOLT_CHRG_THRESHOLD_30                      (3900)
// #define RDX_BAT_VOLT_CHRG_THRESHOLD_20                      (3850)
// #define RDX_BAT_VOLT_CHRG_THRESHOLD_10                      (3200)
// #define RDX_BAT_VOLT_CHRG_THRESHOLD_0                       (3100)


//normal. (notta)
#define RDX_BAT_VOLT_THRESHOLD_100                          (4300)
#define RDX_BAT_VOLT_THRESHOLD_90                           (4130)
#define RDX_BAT_VOLT_THRESHOLD_80                           (4005)
#define RDX_BAT_VOLT_THRESHOLD_70                           (3925)
#define RDX_BAT_VOLT_THRESHOLD_60                           (3846)
#define RDX_BAT_VOLT_THRESHOLD_50                           (3800)
#define RDX_BAT_VOLT_THRESHOLD_40                           (3756)
#define RDX_BAT_VOLT_THRESHOLD_30                           (3720)
#define RDX_BAT_VOLT_THRESHOLD_20                           (3696)
#define RDX_BAT_VOLT_THRESHOLD_10                           (3505)
#define RDX_BAT_VOLT_THRESHOLD_0                            (3210)

//in charge.
#define RDX_BAT_VOLT_CHRG_THRESHOLD_100                     (4220)
#define RDX_BAT_VOLT_CHRG_THRESHOLD_90                      (4180)
#define RDX_BAT_VOLT_CHRG_THRESHOLD_80                      (4100)
#define RDX_BAT_VOLT_CHRG_THRESHOLD_70                      (4070)
#define RDX_BAT_VOLT_CHRG_THRESHOLD_60                      (4040)
#define RDX_BAT_VOLT_CHRG_THRESHOLD_50                      (4000)
#define RDX_BAT_VOLT_CHRG_THRESHOLD_40                      (3960)
#define RDX_BAT_VOLT_CHRG_THRESHOLD_30                      (3835)
#define RDX_BAT_VOLT_CHRG_THRESHOLD_20                      (3710)
#define RDX_BAT_VOLT_CHRG_THRESHOLD_10                      (3560)
#define RDX_BAT_VOLT_CHRG_THRESHOLD_0                       (3210)


#define INCHARGE_SHOW_DELAY_TIME_BAT_LEVEL3_4                   (20 * 60 * 1000)
#define INCHARGE_SHOW_DELAY_TIME_BAT_LEVEL4_5                   (30 * 60 * 1000)
#define INCHARGE_SHOW_DELAY_TIME_BAT_LEVEL5_6                   (15 * 60 * 1000)


/******************************************************************************
* Structure and Enum Section
******************************************************************************/ 

/******************************************************************************
* Global Variables Section
******************************************************************************/ 

/******************************************************************************
* Local Variables Section
******************************************************************************/ 
static u16 delay_show_batLevel_timer_id;

static BatteryInfo incharge_bat_Info;
static BatteryInfo discharge_bat_Info;


/******************************************************************************
* Function Declaration Section
******************************************************************************/ 
extern u8 rdx_app_get_charge_state(void);
extern u16 get_vbat_value(void);
extern u8 get_vbat_percent(void);
extern void rdx_app_charge_full(void);

void rdx_battery_delay_show_batLevel_timer_start(u32 time);

/******************************************************************************
* Function Section
******************************************************************************/ 
BatLevel rdx_battery_get_incharge_batLevel(void)
{ 
    return incharge_bat_Info.level;
}

void rdx_battery_set_incharge_batLevel(BatLevel level)
{
    incharge_bat_Info.level = level;
}

BatLevel rdx_battery_get_discharge_batLevel(void)
{
    return discharge_bat_Info.level;
}

void rdx_battery_set_discharge_batLevel(BatLevel level)
{
    discharge_bat_Info.level = level;
}

void rdx_battery_incharge_batLevel_reset(void)
{ 
    incharge_bat_Info.level = BAT_LEVEL_UNKNOWN;
    incharge_bat_Info.orig_level = BAT_LEVEL_UNKNOWN;
}


/**************************************************************************
 * function: rdx_battery_incharge_batLevel_indicator
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_battery_incharge_batLevel_indicator(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    u32 delay_time = 0;
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    b_printf("APP_MSG_OLED_INCHARGE_BAT_SHOW --> delay_show_batLevel_timer_id: %d, incharge_bat_Info.level: %d \r", delay_show_batLevel_timer_id, incharge_bat_Info.level);

    if(delay_show_batLevel_timer_id){
        return;
    }
    // OLED 功能已删除
    switch(incharge_bat_Info.level){
        case BAT_LEVEL_0: 
            // delay_time = INCHARGE_SHOW_DELAY_TIME_BAT_LEVEL0_1;
            return;

        case BAT_LEVEL_1:
            // delay_time = INCHARGE_SHOW_DELAY_TIME_BAT_LEVEL1_2;
            return;

        case BAT_LEVEL_2:
            // delay_time = INCHARGE_SHOW_DELAY_TIME_BAT_LEVEL2_3;
            return;

        case BAT_LEVEL_3:
            // delay_time = INCHARGE_SHOW_DELAY_TIME_BAT_LEVEL3_4;
            return;

        case BAT_LEVEL_4:
            delay_time = INCHARGE_SHOW_DELAY_TIME_BAT_LEVEL4_5;
            break;

        case BAT_LEVEL_5:
            delay_time = INCHARGE_SHOW_DELAY_TIME_BAT_LEVEL5_6;
            break;

        case BAT_LEVEL_6:
            rdx_app_charge_full();
            return;

        default:
            //do not change anything.
            return;
    }
    //start delay timer for next level show.
    rdx_battery_delay_show_batLevel_timer_start(delay_time);
}

/**************************************************************************
 * function: rdx_battery_delay_show_batLevel_timer_stop
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_battery_delay_show_batLevel_timer_stop(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    y_printf("===> %s \r", __func__);
    if(delay_show_batLevel_timer_id){
        rdx_os_timer_del(delay_show_batLevel_timer_id);
        delay_show_batLevel_timer_id = 0;
    }
}

/**************************************************************************
 * function: rdx_battery_delay_show_batLevel_finish_cb
 * description: 
 * param (void*) priv
 * return (*)
 **************************************************************************/
void rdx_battery_delay_show_batLevel_finish_cb(void* priv)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    y_printf("===> %s \r", __func__);
    rdx_battery_delay_show_batLevel_timer_stop();

    incharge_bat_Info.level++;
    rdx_battery_incharge_batLevel_indicator();
}

/**************************************************************************
 * function: rdx_battery_delay_show_batLevel_timer_start
 * description: 
 * param (u32) time
 * return (*)
 **************************************************************************/
void rdx_battery_delay_show_batLevel_timer_start(u32 time)
{ 
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    b_printf("===> %s --> delay_show_batLevel_timer_id: %d, time: %d \r", __func__, delay_show_batLevel_timer_id, time);
    if(delay_show_batLevel_timer_id == 0){
        delay_show_batLevel_timer_id = rdx_os_timer_add(rdx_battery_delay_show_batLevel_finish_cb, NULL, time);
    }
}

/**************************************************************************
 * function: rdx_battery_get_percent
 * description: 结合电压和充电时间计算电量百分比
 * param (*)
 * return (*)
 **************************************************************************/
u8 rdx_battery_get_percent(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    u8 chr_state = rdx_app_get_charge_state();
    u16 voltage = get_vbat_value();

    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    y_printf("===> %s--> chr_state: %d, voltage = %d \r", __func__, chr_state, voltage);

    extern u8  battery_value_to_phone_level(void);
    return ((battery_value_to_phone_level()+1)*10);//使用JL原生电量曲线接口
    
    if(chr_state == RDX_CHARGE_IN || chr_state == RDX_CHARGE_FULL){
        //charging.
        if(voltage >= RDX_BAT_VOLT_CHRG_THRESHOLD_100){
            return 100;
        }else if(voltage >= RDX_BAT_VOLT_CHRG_THRESHOLD_90 && voltage < RDX_BAT_VOLT_CHRG_THRESHOLD_100){
            return 90;
        }else if(voltage >= RDX_BAT_VOLT_CHRG_THRESHOLD_80 && voltage < RDX_BAT_VOLT_CHRG_THRESHOLD_90){
            return 80;
        }else if(voltage >= RDX_BAT_VOLT_CHRG_THRESHOLD_70 && voltage < RDX_BAT_VOLT_CHRG_THRESHOLD_80){
            return 70;
        }else if(voltage >= RDX_BAT_VOLT_CHRG_THRESHOLD_60 && voltage < RDX_BAT_VOLT_CHRG_THRESHOLD_70){
            return 60;
        }else if(voltage >= RDX_BAT_VOLT_CHRG_THRESHOLD_50 && voltage < RDX_BAT_VOLT_CHRG_THRESHOLD_60){
            return 50;
        }else if(voltage >= RDX_BAT_VOLT_CHRG_THRESHOLD_40 && voltage < RDX_BAT_VOLT_CHRG_THRESHOLD_50){
            return 40;
        }else if(voltage >= RDX_BAT_VOLT_CHRG_THRESHOLD_30 && voltage < RDX_BAT_VOLT_CHRG_THRESHOLD_40){
            return 30;
        }else if(voltage >= RDX_BAT_VOLT_CHRG_THRESHOLD_20 && voltage < RDX_BAT_VOLT_CHRG_THRESHOLD_30){
            return 20;
        }else if(voltage >= RDX_BAT_VOLT_CHRG_THRESHOLD_10 && voltage < RDX_BAT_VOLT_CHRG_THRESHOLD_20){
            return 10;
        }else{
            return 0;
        }
    }else{
        if(voltage >= RDX_BAT_VOLT_THRESHOLD_100){
            return 100;
        }else if(voltage > RDX_BAT_VOLT_THRESHOLD_90 && voltage < RDX_BAT_VOLT_THRESHOLD_100){
            return 90;
        }else if(voltage > RDX_BAT_VOLT_THRESHOLD_80 && voltage <= RDX_BAT_VOLT_THRESHOLD_90){
            return 80;
        }else if(voltage > RDX_BAT_VOLT_THRESHOLD_70 && voltage <= RDX_BAT_VOLT_THRESHOLD_80){
            return 70;
        }else if(voltage > RDX_BAT_VOLT_THRESHOLD_60 && voltage <= RDX_BAT_VOLT_THRESHOLD_70){
            return 60;
        }else if(voltage > RDX_BAT_VOLT_THRESHOLD_50 && voltage <= RDX_BAT_VOLT_THRESHOLD_60){
            return 50;
        }else if(voltage > RDX_BAT_VOLT_THRESHOLD_40 && voltage <= RDX_BAT_VOLT_THRESHOLD_50){
            return 40;
        }else if(voltage > RDX_BAT_VOLT_THRESHOLD_30 && voltage <= RDX_BAT_VOLT_THRESHOLD_40){
            return 30;
        }else if(voltage > RDX_BAT_VOLT_THRESHOLD_20 && voltage <= RDX_BAT_VOLT_THRESHOLD_30){
            return 20;
        }else if(voltage > RDX_BAT_VOLT_THRESHOLD_10 && voltage <= RDX_BAT_VOLT_THRESHOLD_20){
            return 10;
        }else{
            return 0;
        }
    }
}

/**************************************************************************
 * function: rdx_battery_curBatPer_show
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_battery_curBatPer_show(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    u8 cur_bat = rdx_battery_get_percent(); //get_self_battery_level() * 10 + 10;
    u16 voltage = get_vbat_value();
    u8 vb_per = get_vbat_percent();
    u8 chr_state = rdx_app_get_charge_state();
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    y_printf("APP_MSG_OLED_BAT_SHOW --> battery_level: %d, voltage: %d, vb_per: %d \r", cur_bat, voltage, vb_per);
    if (cur_bat > 100) {
        cur_bat = 100;
    }
    
    // OLED 功能已删除 
}

/**************************************************************************
 * function: rdx_battery_inchargeBatPer_show
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_battery_inchargeBatPer_show(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    u8 cur_bat = rdx_battery_get_percent(); //get_self_battery_level() * 10 + 10;
    u16 voltage = get_vbat_value();
    u8 vb_per = get_vbat_percent();
    u8 chr_state = rdx_app_get_charge_state();
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    y_printf("APP_MSG_OLED_BAT_SHOW --> battery_level: %d, voltage: %d, vb_per: %d, chr_state: %d \r", cur_bat, voltage, vb_per, chr_state);
    if(chr_state == RDX_CHARGE_FULL){
        incharge_bat_Info.level = BAT_LEVEL_6;
        return;
    }
    if(chr_state != RDX_CHARGE_IN){
        return;
    }
    if (cur_bat > 100) {
        cur_bat = 100;
    }
    //oled show.
    if(cur_bat >= 0 && cur_bat < 20){
        incharge_bat_Info.level = BAT_LEVEL_0;
    }else if(cur_bat >= 20 && cur_bat < 40){
        incharge_bat_Info.level = BAT_LEVEL_1;
    }else if(cur_bat >= 40 && cur_bat < 60){
        incharge_bat_Info.level = BAT_LEVEL_2;
    }else if(cur_bat >= 60 && cur_bat < 80){
        incharge_bat_Info.level = BAT_LEVEL_3;
    }else if(cur_bat >= 80 && cur_bat < 100){
        incharge_bat_Info.level = BAT_LEVEL_4;
    }else{
        incharge_bat_Info.level = BAT_LEVEL_5;
    }
    if(incharge_bat_Info.level > incharge_bat_Info.orig_level){
        incharge_bat_Info.orig_level = incharge_bat_Info.level;
        rdx_battery_incharge_batLevel_indicator();
    }
}
