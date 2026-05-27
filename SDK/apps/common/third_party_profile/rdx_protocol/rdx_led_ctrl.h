/*=====================================================================================
 HEADER NAME: rdx_led_ctrl.h
 MODULE NAME: RDX LED control module headfile.
 
 PRE-INCLUDE FILES DESCRIPTION: 	
 
 GENERAL DESCRIPTION: 	
 	This File implements LED control logic for RDX device.
 	- BLE搜索中：1s闪一次
 	- BLE连接后：常亮1s后熄灭
 	- BLE断开后：1s闪一次
 	- 录音时：呼吸灯
 	- 录音结束后：如果BLE连着则熄灭，未连则闪灯
 =======================================================================================	
 Revision History: 
 ---------------------------------------
 Author: Auto Generated
 Date: 2025-01-XX
 LastEditors: Auto Generated
 LastEditTime: 2025-01-XX
 FilePath: \SDK\apps\common\third_party_profile\rdx_protocol\rdx_led_ctrl.h
 
 Self-documenting Code
=====================================================================================*/

#ifndef __RDX_LED_CTRL_H__
#define __RDX_LED_CTRL_H__

/******************************************************************************
* Include files
******************************************************************************/ 
#include "typedef.h"
#include "led_pt0807.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
* Macro Define Section
******************************************************************************/ 

/* LED状态定义 */
typedef enum {
    LED_STATE_OFF = 0,              /* 熄灭 */
    LED_STATE_BLE_ADV_BLINK,        /* BLE搜索中闪烁（1s一次，蓝色） */
    LED_STATE_BLE_CONNECTED,        /* BLE连接后常亮1s后熄灭（绿色） */
    LED_STATE_BLE_DISCONNECTED,     /* BLE断开后闪烁（1s一次，蓝色） */
    LED_STATE_RECORD_BREATH,        /* 录音时呼吸灯（蓝色） */
    LED_STATE_OTA_BLINK,            /* OTA升级中：3s闪两次（100ms间隔） */
    LED_STATE_DUT_BLINK,            /* DUT模式：黄灯1s一次闪烁 */
    LED_STATE_WIFI_BLINK,           /* WiFi传输中：黄灯快闪（500ms一次） */
    LED_STATE_CHARGE_LOW_BREATH,    /* 充电中电量<20%：红色呼吸灯 */
    LED_STATE_CHARGE_MID_BREATH,    /* 充电中电量20-80%：黄色呼吸灯 */
    LED_STATE_CHARGE_HIGH_BREATH,   /* 充电中电量80-100%：绿色呼吸灯 */
    LED_STATE_CHARGE_FULL,          /* 充满电：绿色常亮 */
} rdx_led_state_e;

/******************************************************************************
* Function Section
******************************************************************************/ 

/**
 * @brief 初始化LED控制模块
 * @param config LED配置结构体指针（已初始化的led_pt0807配置）
 * @return 0: 成功, <0: 失败
 */
int rdx_led_ctrl_init(LedPt0807Config_t *config);

/**
 * @brief 反初始化LED控制模块
 */
void rdx_led_ctrl_deinit(void);

/**
 * @brief 设置LED状态
 * @param state LED状态
 */
void rdx_led_ctrl_set_state(rdx_led_state_e state);

/**
 * @brief 获取当前LED状态
 * @return 当前LED状态
 */
rdx_led_state_e rdx_led_ctrl_get_state(void);

/**
 * @brief 更新LED显示（需要在定时器中调用）
 */
void rdx_led_ctrl_update(void);

/**
 * @brief 立即刷新LED显示
 */
void rdx_led_ctrl_refresh(void);

/**
 * @brief 设置自定义颜色 (不改变状态)
 * @param r 红色分量 (0-255)
 * @param g 绿色分量 (0-255)
 * @param b 蓝色分量 (0-255)
 */
void rdx_led_ctrl_set_custom_color(u8 r, u8 g, u8 b);

/**
 * @brief 设置自定义颜色带亮度 (不改变状态)
 * @param r 红色分量 (0-255)
 * @param g 绿色分量 (0-255)
 * @param b 蓝色分量 (0-255)
 * @param brightness 亮度 (0-255)
 */
void rdx_led_ctrl_set_custom_color_brightness(u8 r, u8 g, u8 b, u8 brightness);

/**
 * @brief 根据电池电量设置充电灯效
 * @param battery_percent 电池电量百分比 (0-100)
 */
void rdx_led_ctrl_set_charge_state_by_battery(u8 battery_percent);

/**
 * @brief 恢复系统当前状态对应的灯效（充电拔出后调用）
 */
void rdx_led_ctrl_restore_system_state(void);

#ifdef __cplusplus
}
#endif

#endif /* __RDX_LED_CTRL_H__ */

