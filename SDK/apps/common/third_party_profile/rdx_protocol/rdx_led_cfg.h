/*=====================================================================================
 HEADER NAME: rdx_led_cfg.h
 MODULE NAME: RDX LED 全局灯效配置入口 (PRIVATE — 仅由 rdx_led_ctrl.c 包含)

 ============================================================================
 一、架构定位 — 三层 LED 系统的配置层
 ============================================================================

   业务层 (10+ 文件)              配置层 (本文件)              引擎层 (rdx_led_ctrl.c)
   ┌──────────────────┐       ┌──────────────────┐        ┌──────────────────────┐
   │ rdx_ble_server.c │       │ Scene→Effect 映射 │        │ 6 种模式处理器        │
   │ rdx_record.c     │──┐    │ Effect 参数表     │    ┌─→ │ OFF / SOLID /         │
   │ rdx_dut.c        │  │    │ 呼吸亮度查表      │    │   │ SOLID_TIMEOUT /       │
   │ rdx_ota.c        │  └───→│ 充电灯效配置      │────┘   │ BLINK / BREATH /       │
   │ rdx_charge.c     │set_   │ 引擎更新间隔      │        │ DOUBLE_BLINK      │
   │ rdx_app.c        │scene()└──────────────────┘        └──────────────────────┘
   └──────────────────┘

   流程: 业务层调用 set_scene(RDX_LED_SCENE_*) → 本文件查表映射到 effect → 引擎执行

 ============================================================================
 二、公共 API（声明在 rdx_led_ctrl.h，业务代码只通过这些入口控制灯效）
 ============================================================================

   rdx_led_ctrl_set_scene(scene)               — 主入口：指定业务场景
      典型调用: rdx_ble_server.c / rdx_record.c / rdx_dut.c / rdx_ota.c

   rdx_led_ctrl_set_charge_state_by_battery(%)  — 充电中绿色呼吸，电量参数仅保留兼容
      典型调用: rdx_charge.c:418/584

   rdx_led_ctrl_restore_system_state()          — 拔出充电后恢复系统灯效
      典型调用: rdx_charge.c:478 / rdx_app.c:1708 / rdx_record.c:1566

 ============================================================================
 三、文件结构速查
 ============================================================================

   Engine update interval       (第95-96行)   引擎定时器周期 20ms
   Breath table size            (第107-108行) 呼吸亮度表尺寸 100 级
   rdx_led_mode_e               (第110-118行) 6 种灯效执行模式
   rdx_led_effect_cfg_t         (第120-129行) 效果参数结构体 (RGB/时序/亮度)
   rdx_led_effect_e             (第139-155行) 内部效果标识 (12 种, +SMART sentinel)
 ★ Scene→Effect 映射            (第157-179行) 场景→效果查表 (增加场景/效果时改)
★★ Effect 参数表                (第181-262行) 所有灯效颜色/时序/亮度 (改参数时只改这里)
   呼吸亮度查表                 (第276-293行) 100 级呼吸曲线

 ============================================================================
 四、常见操作
 ============================================================================

   [调整现有灯效] → 改 Effect 参数表对应条目即可，不碰引擎代码。
      例: 把 BLE 广播闪烁从蓝色改成绿色，间隔从 1s 改成 500ms：
      [RDX_LED_EFFECT_BLE_ADV_BLINK] = {
          .r = 0, .g = 255, .b = 0,        // 蓝色→绿色
          .interval_ms = 500,               // 1000→500, 闪烁快一倍
      },

   [增加新灯效] → 三步, 不改引擎:
      1. rdx_led_effect_e 加新效果标识
      2. Effect 参数表 加新效果参数行
      3. Scene→Effect 映射 加场景→效果关联

   [增加新场景] → 两步:
      1. rdx_led_ctrl.h 的 rdx_led_scene_e 加新场景值
      2. 本文件 Scene→Effect 映射 加该场景→效果关联

   Charging: pure green breathing; fully charged: solid green.

   [增加新运行模式] → 需要动引擎 (当前 6 种已覆盖所有产品需求)
      1. rdx_led_mode_e 加新模式
      2. rdx_led_ctrl.c _rdx_led_apply_effect() 加初始设置
      3. rdx_led_ctrl.c 新写 _rdx_led_engine_xxx() 处理器
      4. rdx_led_ctrl_update() dispatch 加 switch-case

 ============================================================================
 五、设计约束
 ============================================================================

   - 本文件仅由 rdx_led_ctrl.c 包含 (static const 表编译进 flash, 零 RAM)
   - rdx_led_ctrl.c 不硬编码任何产品级颜色/时序/亮度 —— 全部来自本文件
   - 所有参数编译期常量, 不允许运行时动态分配
======================================================================================*/

#ifndef __RDX_LED_CFG_H__
#define __RDX_LED_CFG_H__

#include "typedef.h"
#include "rdx_led_ctrl.h"

/* ===== Engine update interval ===== */
#define RDX_LED_UPDATE_INTERVAL_MS          (20)

/* Battery indication policy: >= threshold is green, otherwise red. */
#define RDX_LED_LOW_BATTERY_PERCENT         (10)
#define RDX_LED_LOW_BATTERY_REMINDER_MS     (10 * 60 * 1000UL)
#define RDX_LED_BATTERY_INDICATION_MS       (2000)

/* ===== Breath brightness table size ===== */
#define RDX_LED_BREATH_TABLE_SIZE           (80)

/* ===== LED effect execution modes ===== */
typedef enum {
    RDX_LED_MODE_OFF = 0,
    RDX_LED_MODE_SOLID,
    RDX_LED_MODE_SOLID_TIMEOUT,
    RDX_LED_MODE_BLINK,
    RDX_LED_MODE_BREATH,
    RDX_LED_MODE_RAINBOW_BREATH,
    RDX_LED_MODE_DOUBLE_BLINK,
} rdx_led_mode_e;

/* ===== Effect parameter entry ===== */
typedef struct {
    rdx_led_mode_e mode;
    u8  r, g, b;            /* RGB颜色 (0-255) */
    u8  brightness;         /* 亮度 (0-255) */
    u16 on_ms;              /* 脉冲宽度: BLINK单次亮灯 / DOUBLE_BLINK每次脉冲 / SOLID_TIMEOUT亮灯 */
    u16 interval_ms;        /* 周期: BLINK间隔 / DOUBLE_BLINK总周期(脉冲1→间隙→脉冲2→灭灯) */
    u16 cycle_ms;           /* 呼吸周期，用于BREATH / RAINBOW_BREATH */
    u32 timeout_ms;         /* 自动灭灯超时，用于BLINK / SOLID_TIMEOUT (OTA不设超时) */
} rdx_led_effect_cfg_t;

typedef struct {
    u8 r;
    u8 g;
    u8 b;
} rdx_led_rgb_t;

/* ===== Internal effect identifiers ===== */
typedef enum {
    RDX_LED_EFFECT_OFF = 0,
    RDX_LED_EFFECT_BLE_ADV_BLINK,
    RDX_LED_EFFECT_BLE_CONNECTED,
    RDX_LED_EFFECT_RECORD_BREATH,
    RDX_LED_EFFECT_OTA_YELLOW_SOLID,
    RDX_LED_EFFECT_DUT_BLINK,
    RDX_LED_EFFECT_TRANSFER_YELLOW_BLINK,
    RDX_LED_EFFECT_CHARGE_GREEN_BREATH,
    RDX_LED_EFFECT_CHARGE_FULL,
    RDX_LED_EFFECT_LOW_BATTERY_SOLID,
    RDX_LED_EFFECT_RECORD_MARK_YELLOW,
    RDX_LED_EFFECT_BATTERY_GREEN,
    RDX_LED_EFFECT_MAX,
    RDX_LED_EFFECT_SMART = 0xFF,     /* 场景由 set_scene() 内部逻辑处理，不查表 */
} rdx_led_effect_e;

/* ===== Scene to Effect Mapping =====
   每个 rdx_led_scene_e 在此显式映射到 rdx_led_effect_e。
   值为 RDX_LED_EFFECT_SMART 表示该场景由 set_scene() 内部 switch 处理。 */
static const u8 rdx_led_scene_to_effect[RDX_LED_SCENE_MAX] = {
    [RDX_LED_SCENE_BATTERY_QUERY]    = RDX_LED_EFFECT_SMART,
    [RDX_LED_SCENE_OFF]              = RDX_LED_EFFECT_OFF,
    [RDX_LED_SCENE_BLE_ADV_START]    = RDX_LED_EFFECT_BLE_ADV_BLINK,
    [RDX_LED_SCENE_BLE_CONNECTED]    = RDX_LED_EFFECT_BLE_CONNECTED,
    [RDX_LED_SCENE_BLE_DISCONNECTED] = RDX_LED_EFFECT_BLE_ADV_BLINK,
    [RDX_LED_SCENE_BLE_FAST_ADV]     = RDX_LED_EFFECT_BLE_ADV_BLINK,
    [RDX_LED_SCENE_RECORD_START]     = RDX_LED_EFFECT_RECORD_BREATH,
    [RDX_LED_SCENE_RECORD_MARK]      = RDX_LED_EFFECT_RECORD_MARK_YELLOW,
    [RDX_LED_SCENE_RECORD_STOP]      = RDX_LED_EFFECT_SMART,   /* restore_system_state() */
    [RDX_LED_SCENE_OTA_START]        = RDX_LED_EFFECT_OTA_YELLOW_SOLID,
    [RDX_LED_SCENE_OTA_STOP]         = RDX_LED_EFFECT_BLE_ADV_BLINK,
    [RDX_LED_SCENE_DUT_ENTER]        = RDX_LED_EFFECT_DUT_BLINK,
    [RDX_LED_SCENE_DUT_EXIT]         = RDX_LED_EFFECT_OFF,
    [RDX_LED_SCENE_CHARGE_PLUG_IN]   = RDX_LED_EFFECT_SMART,   /* set_charge_effect_by_battery() */
    [RDX_LED_SCENE_CHARGE_PLUG_OUT]  = RDX_LED_EFFECT_SMART,   /* restore_system_state() */
    [RDX_LED_SCENE_CHARGE_FULL]      = RDX_LED_EFFECT_CHARGE_FULL,
    [RDX_LED_SCENE_CASE_DISCHARGE]   = RDX_LED_EFFECT_OFF,
    [RDX_LED_SCENE_LOW_BATTERY]      = RDX_LED_EFFECT_LOW_BATTERY_SOLID,
    [RDX_LED_SCENE_WIFI_START]       = RDX_LED_EFFECT_TRANSFER_YELLOW_BLINK,
    [RDX_LED_SCENE_WIFI_STOP]        = RDX_LED_EFFECT_OFF,
};

/* ===== Effect Parameters Table ===== */
/* 所有产品级LED颜色、时序、亮度都在此定义。
   rdx_led_ctrl.c中的引擎不硬编码任何这些值。 */
static const rdx_led_effect_cfg_t rdx_led_effect_cfg[RDX_LED_EFFECT_MAX] = {
    [RDX_LED_EFFECT_BATTERY_GREEN] = {
        .mode = RDX_LED_MODE_SOLID,
        .r = 0, .g = 255, .b = 0,
        .brightness = 200,
        .timeout_ms = RDX_LED_BATTERY_INDICATION_MS,
    },
    [RDX_LED_EFFECT_OFF] = {
        .mode = RDX_LED_MODE_OFF,
    },
    [RDX_LED_EFFECT_BLE_ADV_BLINK] = {
        .mode        = RDX_LED_MODE_BLINK,
        .r = 0, .g = 0, .b = 255,                /* 蓝色 */
        .brightness  = 200,
        .on_ms       = 200,
        .interval_ms = 1000,
        .timeout_ms  = RDX_LED_BLE_ADV_TIMEOUT_MS,
    },
    [RDX_LED_EFFECT_BLE_CONNECTED] = {
        .mode        = RDX_LED_MODE_SOLID_TIMEOUT,
        .r = 0, .g = 0, .b = 255,                /* 蓝色 */
        .brightness  = 200,
        .on_ms       = 5000,                     /* 长亮5秒后熄灭 */
    },
    [RDX_LED_EFFECT_RECORD_BREATH] = {
        .mode        = RDX_LED_MODE_BREATH,
        .r = 181, .g = 82, .b = 255,            /* 白色：按 800:1500:600 最大光强反比校准 */
        .brightness  = 255,
        .cycle_ms    = 4000,
    },
    [RDX_LED_EFFECT_RECORD_MARK_YELLOW] = {
        .mode        = RDX_LED_MODE_SOLID_TIMEOUT,
        .r = 255, .g = 160, .b = 0,               /* 暖黄色，补偿绿光偏强 */
        .brightness  = 255,
        .on_ms       = 2000,
        .timeout_ms  = 2000,
    },
    /* OTA升级: 黄色常亮，直到升级结束 */
    [RDX_LED_EFFECT_OTA_YELLOW_SOLID] = {
        .mode        = RDX_LED_MODE_SOLID,
        .r = 255, .g = 160, .b = 0,             /* 暖黄色，补偿绿光偏强 */
        .brightness  = 200,
        .timeout_ms  = 0,
    },
    [RDX_LED_EFFECT_DUT_BLINK] = {
        .mode        = RDX_LED_MODE_SOLID,
        .r = 0, .g = 0, .b = 255,             /* 蓝色常亮 */
        .brightness  = 200,
    },
    [RDX_LED_EFFECT_TRANSFER_YELLOW_BLINK] = {
        .mode        = RDX_LED_MODE_BLINK,
        .r = 255, .g = 255, .b = 0,             /* 黄色 */
        .brightness  = 200,
        .on_ms       = 200,
        .interval_ms = 1000,
    },
    [RDX_LED_EFFECT_CHARGE_GREEN_BREATH] = {
        .mode        = RDX_LED_MODE_BREATH,
        .r = 0, .g = 255, .b = 0,               /* Pure green */
        .brightness  = 255,
        .cycle_ms    = 4000,
    },
    [RDX_LED_EFFECT_CHARGE_FULL] = {
        .mode        = RDX_LED_MODE_SOLID,
        .r = 0, .g = 255, .b = 0,               /* 绿色 */
        .brightness  = 255,
    },
    [RDX_LED_EFFECT_LOW_BATTERY_SOLID] = {
        .mode        = RDX_LED_MODE_SOLID,
        .r = 255, .g = 0, .b = 0,               /* 红色 */
        .brightness  = 200,
        .timeout_ms  = RDX_LED_BATTERY_INDICATION_MS,
    },
};

/* ===== Rainbow Color Table ===== */
#define RDX_LED_RAINBOW_COLOR_COUNT       (7)
static const rdx_led_rgb_t rdx_led_rainbow_color_table[RDX_LED_RAINBOW_COLOR_COUNT] = {
    {255,   0,   0},   /* red */
    {255, 128,   0},   /* orange */
    {255, 255,   0},   /* yellow */
    {  0, 255,   0},   /* green */
    {  0, 255, 255},   /* cyan */
    {  0,   0, 255},   /* blue */
    {128,   0, 128},   /* purple */
};

/* ===== Breath Brightness Lookup Table (no full-off tail) ===== */
/* 4s周期，80级正弦呼吸曲线: 0 -> 255 -> 0，循环时正好接上起点，避免熄灭间隙 */
static const u8 rdx_led_breath_brightness_table[RDX_LED_BREATH_TABLE_SIZE] = {
    /* 渐亮阶段 (0-39): 0 -> 255 */
      0,   5,  12,  20,  29,  39,  50,  62,  75,  88,
    102, 116, 131, 145, 159, 172, 185, 196, 206, 215,
    223, 230, 236, 240, 244, 247, 249, 251, 252, 253,
    254, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    /* 渐暗阶段 (40-79): 255 -> 0 */
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    254, 253, 252, 251, 249, 247, 244, 240, 236, 230,
    223, 215, 206, 196, 185, 172, 159, 145, 131, 116,
    102,  88,  75,  62,  50,  39,  29,  20,  12,   5,
};

#endif /* __RDX_LED_CFG_H__ */
