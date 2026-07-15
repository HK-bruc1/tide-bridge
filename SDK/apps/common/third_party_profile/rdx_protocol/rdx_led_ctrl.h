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
    LED_STATE_BLE_ADV_BLINK,        /* BLE未连：蓝灯闪烁，超时后熄灭 */
    LED_STATE_BLE_CONNECTED,        /* BLE已连：蓝灯长亮5秒后熄灭 */
    LED_STATE_BLE_DISCONNECTED,     /* BLE断开：蓝灯闪烁，超时后熄灭 */
    LED_STATE_RECORD_BREATH,        /* 录音中：白灯呼吸 */
    LED_STATE_OTA_BLINK,            /* OTA升级中：3s闪两次（100ms间隔） */
    LED_STATE_DUT_BLINK,            /* DUT模式：黄灯1s一次闪烁 */
    LED_STATE_WIFI_BLINK,           /* BLE/WiFi传输：黄灯慢闪 */
    LED_STATE_CHARGE_LOW_BREATH,    /* 充电中电量<20%：红色呼吸灯 */
    LED_STATE_CHARGE_MID_BREATH,    /* 充电中电量20-80%：黄色呼吸灯 */
    LED_STATE_CHARGE_HIGH_BREATH,   /* 充电中电量80-100%：绿色呼吸灯 */
    LED_STATE_CHARGE_FULL,          /* 充满电：绿色常亮 */
} rdx_led_state_e;

/* BLE 未连接/断开后，蓝灯闪烁并维持快速广播的统一超时时间（ms） 之后同步进入慢广播与超时灯效 */
#define RDX_LED_BLE_ADV_TIMEOUT_MS      (300 * 1000)

/* LEGACY: 内部使用，业务代码请使用 rdx_led_scene_e + rdx_led_ctrl_set_scene() */

/* LED业务场景定义 — 业务层只通过场景控制灯效 */
typedef enum {
    RDX_LED_SCENE_OFF = 0,          /* 熄灭 */
    RDX_LED_SCENE_BLE_ADV_START,    /* BLE广播开始 */
    RDX_LED_SCENE_BLE_CONNECTED,    /* BLE已连接 */
    RDX_LED_SCENE_BLE_DISCONNECTED, /* BLE断开 */
    RDX_LED_SCENE_BLE_FAST_ADV,     /* BLE快速广播 */
    RDX_LED_SCENE_RECORD_START,     /* 开始录音 */
    RDX_LED_SCENE_RECORD_STOP,      /* 停止录音 */
    RDX_LED_SCENE_OTA_START,        /* OTA开始 */
    RDX_LED_SCENE_OTA_STOP,         /* OTA结束 */
    RDX_LED_SCENE_DUT_ENTER,        /* 进入DUT模式 */
    RDX_LED_SCENE_DUT_EXIT,         /* 退出DUT模式 */
    RDX_LED_SCENE_CHARGE_PLUG_IN,   /* 充电插入 */
    RDX_LED_SCENE_CHARGE_PLUG_OUT,  /* 充电拔出 */
    RDX_LED_SCENE_CHARGE_FULL,      /* 充电充满 */
    RDX_LED_SCENE_CASE_DISCHARGE,   /* 仓给耳机充电：无灯效 */
    RDX_LED_SCENE_LOW_BATTERY,      /* 充电仓低电 */
    RDX_LED_SCENE_WIFI_START,       /* WiFi传输开始 */
    RDX_LED_SCENE_WIFI_STOP,        /* WiFi传输结束 */
    RDX_LED_SCENE_MAX,
} rdx_led_scene_e;

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
 * @brief 设置LED状态 (LEGACY — 请使用 rdx_led_ctrl_set_scene)
 * @param state LED状态
 */
void rdx_led_ctrl_set_state(rdx_led_state_e state);

/**
 * @brief 设置LED业务场景（推荐使用）
 * @param scene LED业务场景
 */
void rdx_led_ctrl_set_scene(rdx_led_scene_e scene);

/**
 * @brief 获取当前LED业务场景
 * @return 当前场景
 */
rdx_led_scene_e rdx_led_ctrl_get_scene(void);

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
