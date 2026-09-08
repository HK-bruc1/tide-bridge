/*=====================================================================================
 HEADER NAME: rdx_led_ctrl.c
 MODULE NAME: RDX LED control module.

 GENERAL DESCRIPTION:
    This File implements LED control logic for RDX device.
    - BLE搜索中：1s闪一次（蓝色）
    - BLE连接后：常亮1s后熄灭（青色）
    - BLE断开后：1s闪一次（蓝色）
    - 录音时：呼吸灯（红色）
    - 充电灯效：纯绿色呼吸灯，充满绿色常亮

    架构：表驱动引擎
    - 所有产品级颜色/时序/亮度参数在 rdx_led_cfg.h 中配置
    - rdx_led_ctrl.c 只包含执行引擎，不硬编码任何产品级颜色常量
    - 业务层通过 rdx_led_ctrl_set_scene(RDX_LED_SCENE_*) 控制灯效

    适配新的SPI模式LED PT0807驱动:
    - 使用 led_pt0807_set_all_rgb() + led_pt0807_update() 更新显示
    - 支持亮度调节，实现平滑呼吸效果
=====================================================================================*/

#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".rdx_led_ctrl.data.bss")
#pragma data_seg(".rdx_led_ctrl.data")
#pragma const_seg(".rdx_led_ctrl.text.const")
#pragma code_seg(".rdx_led_ctrl.text")
#endif

/******************************************************************************
* Include files
******************************************************************************/
#include "rdx_led_ctrl.h"
#include "rdx_peripheral_power.h"
#include "rdx_led_cfg.h"
#include "system/includes.h"
#include "rdx_ble_server.h"
#include "rdx_record.h"
#include "rdx_app.h"
#include "rdx_charge.h"
#include "app_main.h"
#include "app_power_manage.h"
#include "asm/charge.h"

extern bool rdx_app_get_dut_status(void);
extern u8 get_ota_status(void);

/******************************************************************************
* Macro Define Section
******************************************************************************/

#define LED_MAX_BRIGHTNESS               (255)

/******************************************************************************
* Local Variables Section
******************************************************************************/

static LedPt0807Config_t *g_led_config = NULL;
static const rdx_led_effect_cfg_t *g_active_effect = NULL;
static rdx_led_scene_e g_current_scene = RDX_LED_SCENE_OFF;
static u32 g_led_update_timer = 0;
static u32 g_effect_elapsed_ms = 0;   /* 从不重置 — 用于超时检查 */
static u32 g_phase_elapsed_ms = 0;    /* 每周期重置 — 用于引擎处理 */
static u8 g_led_blink_state = 0;
static u8 g_led_connected_on_done = 0;
static u8 g_led_initialized = 0;
/* Survives LED deinit while the shared rail sleeps between reminders. */
static u16 g_low_battery_timer = 0;

/******************************************************************************
* Function Declaration Section
******************************************************************************/

static void rdx_led_ctrl_update_timer_cb(void *priv);
static void rdx_led_ctrl_set_rgb_brightness(u8 r, u8 g, u8 b, u8 brightness);
static void rdx_led_ctrl_off(void);

/* 引擎处理器 */
static void _rdx_led_engine_solid_timeout(const rdx_led_effect_cfg_t *cfg);
static void _rdx_led_engine_blink(const rdx_led_effect_cfg_t *cfg);
static void _rdx_led_engine_breath(const rdx_led_effect_cfg_t *cfg);
static void _rdx_led_engine_rainbow_breath(const rdx_led_effect_cfg_t *cfg);
static void _rdx_led_engine_double_blink(const rdx_led_effect_cfg_t *cfg);

/* 内部辅助函数 */
static void _rdx_led_apply_effect(rdx_led_effect_e effect);
static void _rdx_led_set_charge_effect_by_battery(u8 battery_percent);
static void _rdx_led_restore_system_state(void);
static bool _rdx_led_is_transfer_active(void);
static bool _rdx_led_can_show_transfer_effect(void);
static bool _rdx_led_refresh_transfer_scene(void);

static bool _rdx_led_low_battery_reminder_needed(void)
{
    return !app_var.goto_poweroff_flag && !get_vbat_need_shutdown() &&
           !get_charge_online_flag() &&
           rdx_app_get_charge_state() == RDX_CHARGE_OUT &&
           get_vbat_percent() < RDX_LED_LOW_BATTERY_PERCENT;
}

/* Both manual queries and periodic reminders yield to these scenes. */
static bool _rdx_led_can_show_battery(void)
{
    return !app_var.goto_poweroff_flag && !get_vbat_need_shutdown() &&
           !get_charge_online_flag() &&
           rdx_app_get_charge_state() == RDX_CHARGE_OUT &&
           !get_ota_status() && !rdx_app_get_dut_status() &&
           g_current_scene != RDX_LED_SCENE_OTA_START &&
           g_current_scene != RDX_LED_SCENE_DUT_ENTER &&
           g_current_scene != RDX_LED_SCENE_CHARGE_PLUG_IN &&
           g_current_scene != RDX_LED_SCENE_CHARGE_FULL;
}

void rdx_led_ctrl_show_battery(void)
{
    if (!_rdx_led_can_show_battery()) {
        return;
    }
    if (g_current_scene == RDX_LED_SCENE_LOW_BATTERY) {
        /* Extend the visible red reminder without changing its repeat timer. */
        g_effect_elapsed_ms = 0;
        return;
    }
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_BATTERY_QUERY);
}

static void _rdx_led_low_battery_timer_cb(void *priv)
{
    if (!_rdx_led_low_battery_reminder_needed()) {
        rdx_led_ctrl_update_low_battery();
        return;
    }
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_LOW_BATTERY);
}

void rdx_led_ctrl_update_low_battery(void)
{
    if (!_rdx_led_low_battery_reminder_needed()) {
        if (g_low_battery_timer) {
            sys_timer_del(g_low_battery_timer);
            g_low_battery_timer = 0;
        }
        if (g_current_scene == RDX_LED_SCENE_LOW_BATTERY) {
            /* Release the temporary priority lock before restoring a scene. */
            g_current_scene = RDX_LED_SCENE_OFF;
            if (app_var.goto_poweroff_flag || get_vbat_need_shutdown()) {
                rdx_led_ctrl_set_scene(RDX_LED_SCENE_OFF);
            } else {
                _rdx_led_restore_system_state();
            }
        }
        return;
    }
    if (!g_low_battery_timer) {
        g_low_battery_timer = sys_timer_add(NULL,
                _rdx_led_low_battery_timer_cb, RDX_LED_LOW_BATTERY_REMINDER_MS);
        if (g_low_battery_timer) {
            rdx_led_ctrl_set_scene(RDX_LED_SCENE_LOW_BATTERY);
        }
    }
}

/******************************************************************************
* Function Section — 基础LED操作
******************************************************************************/

static void rdx_led_ctrl_set_rgb_brightness(u8 r, u8 g, u8 b, u8 brightness)
{
    if (g_led_config == NULL || !g_led_config->initialized) {
        return;
    }
    u8 adj_r = (u8)(((u32)r * brightness) / 255);
    u8 adj_g = (u8)(((u32)g * brightness) / 255);
    u8 adj_b = (u8)(((u32)b * brightness) / 255);
    LedPt0807Rgb_t rgb = {.g = adj_g, .r = adj_r, .b = adj_b};
    led_pt0807_set_all_rgb(g_led_config, &rgb);
    led_pt0807_update(g_led_config);
}

static void rdx_led_ctrl_set_rgb(u8 r, u8 g, u8 b)
{
    rdx_led_ctrl_set_rgb_brightness(r, g, b, LED_MAX_BRIGHTNESS);
}

static void rdx_led_ctrl_off(void)
{
    if (g_led_config == NULL || !g_led_config->initialized) {
        return;
    }
    LedPt0807Rgb_t rgb = {.g = 0, .r = 0, .b = 0};
    led_pt0807_set_all_rgb(g_led_config, &rgb);
    led_pt0807_update(g_led_config);
}

static void rdx_led_ctrl_update_timer_cb(void *priv)
{
    rdx_led_ctrl_update();
}

static bool _rdx_led_is_transfer_active(void)
{
    /* Only Wi-Fi transfer requests this effect. BLE file downloads are silent;
     * firmware upgrades use the independent OTA scene and get_ota_status(). */
    RdxWifiInfo* wifi_info = rdx_app_get_wifi_info();
    return wifi_info && wifi_info->onoff == TRANSFER_BY_WIFI_ON;
}

static bool _rdx_led_can_show_transfer_effect(void)
{
    if (g_current_scene == RDX_LED_SCENE_LOW_BATTERY ||
        g_current_scene == RDX_LED_SCENE_BATTERY_QUERY) {
        return false;
    }
    if (rdx_app_get_dut_status() || get_ota_status()) {
        return false;
    }

    RecordStatus* rp = rdx_record_get_status();
    if (rp && (rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME)) {
        return false;
    }

    return true;
}

static bool _rdx_led_refresh_transfer_scene(void)
{
    bool transfer_active = _rdx_led_is_transfer_active();

    if (g_current_scene == RDX_LED_SCENE_WIFI_START) {
        if (!transfer_active || !_rdx_led_can_show_transfer_effect()) {
            _rdx_led_restore_system_state();
            return true;
        }
        return false;
    }

    if (transfer_active && _rdx_led_can_show_transfer_effect()) {
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_WIFI_START);
        return true;
    }

    return false;
}

/******************************************************************************
* Function Section — 内部辅助函数
******************************************************************************/

static void _rdx_led_apply_effect(rdx_led_effect_e effect)
{
    if (effect >= RDX_LED_EFFECT_MAX) {
        return;
    }
    g_active_effect = &rdx_led_effect_cfg[effect];
    g_printf("RDX LED effect: id=%u mode=%u rgb=%u,%u,%u cycle=%u\r\n",
             (unsigned)effect, (unsigned)g_active_effect->mode,
             (unsigned)g_active_effect->r, (unsigned)g_active_effect->g,
             (unsigned)g_active_effect->b, (unsigned)g_active_effect->cycle_ms);
    g_effect_elapsed_ms = 0;
    g_phase_elapsed_ms = 0;
    g_led_blink_state = 0;
    g_led_connected_on_done = 0;
    led_pt0807_run_enable(g_led_config, 1);

    switch (g_active_effect->mode) {
    case RDX_LED_MODE_OFF:
        rdx_led_ctrl_off();
        break;
    case RDX_LED_MODE_SOLID:
        rdx_led_ctrl_set_rgb_brightness(g_active_effect->r, g_active_effect->g,
                                        g_active_effect->b, g_active_effect->brightness);
        break;
    case RDX_LED_MODE_SOLID_TIMEOUT:
        rdx_led_ctrl_set_rgb_brightness(g_active_effect->r, g_active_effect->g,
                                        g_active_effect->b, g_active_effect->brightness);
        break;
    case RDX_LED_MODE_BLINK:
        g_led_blink_state = 1;
        rdx_led_ctrl_set_rgb_brightness(g_active_effect->r, g_active_effect->g,
                                        g_active_effect->b, g_active_effect->brightness);
        break;
    case RDX_LED_MODE_BREATH:
        rdx_led_ctrl_set_rgb_brightness(g_active_effect->r, g_active_effect->g,
                                        g_active_effect->b, 0);
        break;
    case RDX_LED_MODE_RAINBOW_BREATH:
        rdx_led_ctrl_set_rgb_brightness(rdx_led_rainbow_color_table[0].r,
                                        rdx_led_rainbow_color_table[0].g,
                                        rdx_led_rainbow_color_table[0].b, 0);
        break;
    case RDX_LED_MODE_DOUBLE_BLINK:
        g_led_blink_state = 1;
        rdx_led_ctrl_set_rgb_brightness(g_active_effect->r, g_active_effect->g,
                                        g_active_effect->b, g_active_effect->brightness);
        break;
    }
}

static void _rdx_led_set_charge_effect_by_battery(u8 battery_percent)
{
    (void)battery_percent;
    _rdx_led_apply_effect(RDX_LED_EFFECT_CHARGE_GREEN_BREATH);

    g_current_scene = RDX_LED_SCENE_CHARGE_PLUG_IN;
}

static void _rdx_led_restore_system_state(void)
{
    RecordStatus* rp = rdx_record_get_status();

    if (rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME) {
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_RECORD_START);
        return;
    }

    if (rdx_app_get_dut_status()) {
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_DUT_ENTER);
        return;
    }

    if (get_ota_status()) {
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_OTA_START);
        return;
    }

    if (_rdx_led_is_transfer_active() && _rdx_led_can_show_transfer_effect()) {
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_WIFI_START);
        return;
    }

    u8 charger_status = rdx_app_get_charge_state();
    if (charger_status == RDX_CHARGE_IN) {
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_CHARGE_PLUG_IN);
        return;
    }
    if (charger_status == RDX_CHARGE_FULL) {
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_CHARGE_FULL);
        return;
    }

    /* A record/transfer completion may restore the system LED after the
     * fast-to-slow transition already happened. Slow advertising must remain
     * visually off; otherwise RGB_REQUIRED would keep the shared rail awake. */
    if (rdx_ble_server_has_active_link() ||
        rdx_peripheral_power_vdd_is_slow_adv()) {
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_OFF);
    } else {
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_BLE_ADV_START);
    }
}

/******************************************************************************
* Function Section — 引擎处理器（表驱动，参数来自cfg）
******************************************************************************/

static void _rdx_led_engine_solid_timeout(const rdx_led_effect_cfg_t *cfg)
{
    if (!g_led_connected_on_done) {
        if (g_effect_elapsed_ms >= cfg->on_ms) {
            g_led_connected_on_done = 1;
            rdx_led_ctrl_off();
        }
    }
}

static void _rdx_led_engine_blink(const rdx_led_effect_cfg_t *cfg)
{
    if (g_led_blink_state) {
        if (g_phase_elapsed_ms >= cfg->on_ms) {
            g_led_blink_state = 0;
            rdx_led_ctrl_off();
        }
    } else {
        if (g_phase_elapsed_ms >= cfg->interval_ms) {
            g_phase_elapsed_ms = 0;
            g_led_blink_state = 1;
            rdx_led_ctrl_set_rgb_brightness(cfg->r, cfg->g, cfg->b, cfg->brightness);
        }
    }
}

static void _rdx_led_engine_breath(const rdx_led_effect_cfg_t *cfg)
{
    u32 cycle_pos = g_phase_elapsed_ms % cfg->cycle_ms;
    u32 table_index = (cycle_pos * RDX_LED_BREATH_TABLE_SIZE) / cfg->cycle_ms;
    if (table_index >= RDX_LED_BREATH_TABLE_SIZE) {
        table_index = RDX_LED_BREATH_TABLE_SIZE - 1;
    }
    u8 brightness = rdx_led_breath_brightness_table[table_index];
    rdx_led_ctrl_set_rgb_brightness(cfg->r, cfg->g, cfg->b, brightness);
}

static void _rdx_led_engine_rainbow_breath(const rdx_led_effect_cfg_t *cfg)
{
    if (cfg->cycle_ms == 0) {
        return;
    }

    u32 cycle_pos = g_phase_elapsed_ms % cfg->cycle_ms;
    u32 table_index = (cycle_pos * RDX_LED_BREATH_TABLE_SIZE) / cfg->cycle_ms;
    if (table_index >= RDX_LED_BREATH_TABLE_SIZE) {
        table_index = RDX_LED_BREATH_TABLE_SIZE - 1;
    }

    /* 呼吸亮度 0~255 */
    u8 breath = rdx_led_breath_brightness_table[table_index];
    u8 brightness = (u8)(((u32)breath * cfg->brightness) / 255);

    /* 沿七色平滑过渡，色相覆盖 0~255，当前颜色在 4s 内变化。
       相邻颜色在灭灯/亮灯切换时亮度为 0，实现无缝衔接。 */
    u8 hue = (u8)((g_effect_elapsed_ms * 256) / cfg->cycle_ms);
    const rdx_led_rgb_t *from = &rdx_led_rainbow_color_table[(hue / 37) % RDX_LED_RAINBOW_COLOR_COUNT];
    const rdx_led_rgb_t *to   = &rdx_led_rainbow_color_table[((hue / 37) + 1) % RDX_LED_RAINBOW_COLOR_COUNT];
    u8 step = hue % 37;

    u8 r = (u8)(((u16)(from->r) * (37 - step) + (u16)(to->r) * step) / 37);
    u8 g = (u8)(((u16)(from->g) * (37 - step) + (u16)(to->g) * step) / 37);
    u8 b = (u8)(((u16)(from->b) * (37 - step) + (u16)(to->b) * step) / 37);

    rdx_led_ctrl_set_rgb_brightness(r, g, b, brightness);
}

/* 双闪引擎: 每个 interval_ms 周期内产生两次短脉冲(on_ms宽, on_ms间隔),
   然后灭灯直到周期结束。时序: [脉冲1 ON]→[间隙 OFF]→[脉冲2 ON]→[灭灯至周期末] */
static void _rdx_led_engine_double_blink(const rdx_led_effect_cfg_t *cfg)
{
    u32 cycle_pos = g_phase_elapsed_ms % cfg->interval_ms;
    u16 on_ms = cfg->on_ms;
    u16 gap_ms = cfg->on_ms;

    /* 第一次脉冲: 0 到 on_ms */
    if (cycle_pos < on_ms) {
        if (g_led_blink_state != 1) {
            g_led_blink_state = 1;
            rdx_led_ctrl_set_rgb_brightness(cfg->r, cfg->g, cfg->b, cfg->brightness);
        }
    }
    /* 脉冲间隔: on_ms 到 on_ms+gap_ms */
    else if (cycle_pos < on_ms + gap_ms) {
        if (g_led_blink_state != 0) {
            g_led_blink_state = 0;
            rdx_led_ctrl_off();
        }
    }
    /* 第二次脉冲: on_ms+gap_ms 到 on_ms*2+gap_ms */
    else if (cycle_pos < on_ms * 2 + gap_ms) {
        if (g_led_blink_state != 2) {
            g_led_blink_state = 2;
            rdx_led_ctrl_set_rgb_brightness(cfg->r, cfg->g, cfg->b, cfg->brightness);
        }
    }
    /* 灭灯直到周期结束 */
    else {
        if (g_led_blink_state != 3) {
            g_led_blink_state = 3;
            rdx_led_ctrl_off();
        }
    }
}

/******************************************************************************
* Function Section — 公共API
******************************************************************************/

int rdx_led_ctrl_init(LedPt0807Config_t *config)
{
    if (config == NULL || !config->initialized) {
        g_printf("RDX LED Ctrl: Init failed\r\n");
        return -1;
    }
    g_led_config = config;
    g_current_scene = RDX_LED_SCENE_OFF;
    g_active_effect = NULL;
    g_effect_elapsed_ms = 0;
    g_phase_elapsed_ms = 0;
    g_led_blink_state = 0;
    g_led_connected_on_done = 0;
    led_pt0807_run_enable(config, 1);
    rdx_led_ctrl_off();
    if (g_led_update_timer == 0) {
        g_led_update_timer = sys_timer_add(NULL, rdx_led_ctrl_update_timer_cb, RDX_LED_UPDATE_INTERVAL_MS);
        if (g_led_update_timer == 0) {
            return -2;
        }
    }
    g_led_initialized = 1;
    g_printf("RDX LED Ctrl: Init ok\r\n");
    return 0;
}

void rdx_led_ctrl_deinit(void)
{
    if (g_led_update_timer) {
        sys_timer_del(g_led_update_timer);
        g_led_update_timer = 0;
    }
    if (g_led_config && g_led_config->initialized) {
        /* run_en is cleared before the synchronous black frame is sent. */
        led_pt0807_run_enable(g_led_config, 0);
    }
    g_led_config = NULL;
    g_current_scene = RDX_LED_SCENE_OFF;
    g_active_effect = NULL;
    g_led_initialized = 0;
}

void rdx_led_ctrl_set_scene(rdx_led_scene_e scene)
{
    if (scene >= RDX_LED_SCENE_MAX) {
        return;
    }
    /* Gate before waking the shared rail. Skipping a reminder does not
     * restart its ten-minute schedule. */
    if ((scene == RDX_LED_SCENE_LOW_BATTERY ||
         scene == RDX_LED_SCENE_BATTERY_QUERY) &&
        !_rdx_led_can_show_battery()) {
        return;
    }
    if (g_led_config == NULL || !g_led_initialized) {
        if (scene == RDX_LED_SCENE_OFF) {
            rdx_peripheral_power_vdd_business_changed_notify();
            return;
        }
        if (rdx_peripheral_power_vdd_ensure_on(
                RDX_SHARED_VDD_WAKE_BUSINESS)) {
            return;
        }
        if (g_led_config == NULL || !g_led_initialized) {
            return;
        }
    }

    /* The temporary warning must yield immediately to critical status LEDs. */
    if (g_current_scene == RDX_LED_SCENE_LOW_BATTERY
        && scene != RDX_LED_SCENE_OFF
        && scene != RDX_LED_SCENE_CHARGE_PLUG_IN
        && scene != RDX_LED_SCENE_CHARGE_FULL
        && scene != RDX_LED_SCENE_OTA_START
        && scene != RDX_LED_SCENE_DUT_ENTER) {
        return;
    }

    g_printf("RDX LED Scene: %d->%d\r\n", g_current_scene, scene);
    g_current_scene = scene;

    switch (scene) {
    case RDX_LED_SCENE_BATTERY_QUERY:
        _rdx_led_apply_effect(get_vbat_percent() < RDX_LED_LOW_BATTERY_PERCENT ?
                RDX_LED_EFFECT_LOW_BATTERY_SOLID : RDX_LED_EFFECT_BATTERY_GREEN);
        return;

    case RDX_LED_SCENE_RECORD_STOP:
    case RDX_LED_SCENE_CHARGE_PLUG_OUT:
        _rdx_led_restore_system_state();
        return;

    case RDX_LED_SCENE_CHARGE_PLUG_IN:
        /* 充电中统一绿色呼吸，电量参数仅保留兼容。 */
        _rdx_led_set_charge_effect_by_battery(80);
        return;

    default:
        break;
    }

    u8 effect_idx = rdx_led_scene_to_effect[scene];
    if (effect_idx == RDX_LED_EFFECT_SMART) {
        /* SMART 场景应在上面 switch 中处理并 return，不应到达此处 */
        return;
    }
    _rdx_led_apply_effect((rdx_led_effect_e)effect_idx);
    if (scene == RDX_LED_SCENE_OFF) {
        rdx_peripheral_power_vdd_business_changed_notify();
    }
}

rdx_led_scene_e rdx_led_ctrl_get_scene(void)
{
    return g_current_scene;
}

/* LEGACY: 将旧的 LED_STATE_* 映射到新的场景API。新代码请使用 set_scene()。 */
void rdx_led_ctrl_set_state(rdx_led_state_e state)
{
    if (g_led_config == NULL || !g_led_initialized) {
        return;
    }
    g_printf("RDX LED (legacy set_state): %d\r\n", state);

    switch (state) {
    case LED_STATE_OFF:
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_OFF);
        break;

    case LED_STATE_BLE_ADV_BLINK:
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_BLE_ADV_START);
        break;
    case LED_STATE_BLE_CONNECTED:
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_BLE_CONNECTED);
        break;
    case LED_STATE_BLE_DISCONNECTED:
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_BLE_DISCONNECTED);
        break;
    case LED_STATE_RECORD_BREATH:
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_RECORD_START);
        break;
    case LED_STATE_OTA_BLINK:
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_OTA_START);
        break;
    case LED_STATE_DUT_BLINK:
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_DUT_ENTER);
        break;
    case LED_STATE_WIFI_BLINK:
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_WIFI_START);
        break;
    case LED_STATE_CHARGE_LOW_BREATH:
    case LED_STATE_CHARGE_MID_BREATH:
    case LED_STATE_CHARGE_HIGH_BREATH:
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_CHARGE_PLUG_IN);
        break;
    case LED_STATE_CHARGE_FULL:
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_CHARGE_FULL);
        break;
    default:
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_OFF);
        break;
    }
}

/******************************************************************************
* Function Section — 更新与刷新
******************************************************************************/

void rdx_led_ctrl_update(void)
{
    if (g_led_config == NULL || !g_led_initialized) {
        return;
    }
    if (!g_led_config->run_en) {
        return;
    }
    if (_rdx_led_refresh_transfer_scene()) {
        return;
    }
    if (g_active_effect == NULL) {
        return;
    }

    g_effect_elapsed_ms += RDX_LED_UPDATE_INTERVAL_MS;
    g_phase_elapsed_ms += RDX_LED_UPDATE_INTERVAL_MS;

    switch (g_active_effect->mode) {
    case RDX_LED_MODE_OFF:
        break;
    case RDX_LED_MODE_SOLID:
        break;
    case RDX_LED_MODE_SOLID_TIMEOUT:
        _rdx_led_engine_solid_timeout(g_active_effect);
        break;
    case RDX_LED_MODE_BLINK:
        _rdx_led_engine_blink(g_active_effect);
        break;
    case RDX_LED_MODE_BREATH:
        _rdx_led_engine_breath(g_active_effect);
        break;
    case RDX_LED_MODE_RAINBOW_BREATH:
        _rdx_led_engine_rainbow_breath(g_active_effect);
        break;
    case RDX_LED_MODE_DOUBLE_BLINK:
        _rdx_led_engine_double_blink(g_active_effect);
        break;
    }

    /* 检查超时 — 使用g_effect_elapsed_ms（不被引擎处理器重置）。
       通过set_scene()保持g_current_scene同步。 */
    if (g_active_effect->timeout_ms > 0
        && g_effect_elapsed_ms >= g_active_effect->timeout_ms) {
        if (g_current_scene == RDX_LED_SCENE_LOW_BATTERY ||
            g_current_scene == RDX_LED_SCENE_BATTERY_QUERY) {
            g_current_scene = RDX_LED_SCENE_OFF;
            _rdx_led_restore_system_state();
        } else if (g_current_scene == RDX_LED_SCENE_RECORD_MARK) {
            _rdx_led_restore_system_state();
        } else {
            rdx_led_ctrl_set_scene(RDX_LED_SCENE_OFF);
        }
    }
}

void rdx_led_ctrl_refresh(void)
{
    if (g_led_config == NULL || !g_led_initialized) {
        return;
    }
    led_pt0807_update(g_led_config);
}

/******************************************************************************
* Function Section — 自定义颜色
******************************************************************************/

void rdx_led_ctrl_set_custom_color(u8 r, u8 g, u8 b)
{
    if (g_led_config == NULL || !g_led_initialized) {
        return;
    }
    rdx_led_ctrl_set_rgb(r, g, b);
}

void rdx_led_ctrl_set_custom_color_brightness(u8 r, u8 g, u8 b, u8 brightness)
{
    if (g_led_config == NULL || !g_led_initialized) {
        return;
    }
    rdx_led_ctrl_set_rgb_brightness(r, g, b, brightness);
}

/******************************************************************************
* Function Section — 充电与状态恢复（公共API，委托给内部辅助函数）
******************************************************************************/

/**
 * @brief 设置充电中绿色呼吸灯效（保留电量参数兼容调用者）
 * @param battery_percent 电池电量百分比 (0-100)
 */
void rdx_led_ctrl_set_charge_state_by_battery(u8 battery_percent)
{
    if (g_led_config == NULL || !g_led_initialized) {
        return;
    }
    _rdx_led_set_charge_effect_by_battery(battery_percent);
}

/**
 * @brief 恢复系统当前状态对应的灯效（充电拔出后调用）
 */
void rdx_led_ctrl_restore_system_state(void)
{
    if (g_led_config == NULL || !g_led_initialized) {
        return;
    }
    _rdx_led_restore_system_state();
}
