/*=====================================================================================
 HEADER NAME: rdx_led_ctrl.c
 MODULE NAME: RDX LED control module.
 
 GENERAL DESCRIPTION: 
    This File implements LED control logic for RDX device.
    - BLE搜索中：1s闪一次（蓝色）
    - BLE连接后：常亮1s后熄灭（绿色）
    - BLE断开后：1s闪一次（蓝色）
    - 录音时：呼吸灯（蓝色）
    - 充电灯效：<20%红色呼吸，20-80%黄色呼吸，80-100%绿色呼吸，充满绿色常亮
    
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
#include "system/includes.h"
#include "rdx_ble_server.h"
#include "rdx_record.h"
#include "rdx_app.h"
#include "rdx_charge.h"

/******************************************************************************
* Macro Define Section
******************************************************************************/ 

#define LED_BLINK_INTERVAL_MS            (1000)
#define LED_BLINK_ON_TIME_MS             (200)
#define LED_CONNECTED_ON_TIME_MS         (1000)
#define LED_BREATH_CYCLE_MS              (4000)  // 呼吸灯周期 4s（加长一倍）
#define LED_UPDATE_INTERVAL_MS           (20)
#define LED_BREATH_TABLE_SIZE            (100)

/* LED颜色定义 */
#define LED_COLOR_BLUE_R                 (0)
#define LED_COLOR_BLUE_G                 (0)
#define LED_COLOR_BLUE_B                 (255)

#define LED_COLOR_GREEN_R                (0)
#define LED_COLOR_GREEN_G                (255)
#define LED_COLOR_GREEN_B                (0)

#define LED_COLOR_RED_R                  (255)
#define LED_COLOR_RED_G                  (0)
#define LED_COLOR_RED_B                  (0)

#define LED_COLOR_YELLOW_R               (255)
#define LED_COLOR_YELLOW_G               (255)
#define LED_COLOR_YELLOW_B               (0)

/* OTA灯效参数：3s闪两次（100ms间隔） */
#define LED_OTA_CYCLE_MS                 (3000)
#define LED_OTA_BLINK_ON_MS              (100)
#define LED_OTA_BLINK_OFF_MS             (100)

/* DUT灯效参数：黄灯1s一次闪烁 */
#define LED_DUT_BLINK_INTERVAL_MS        (1000)
#define LED_DUT_BLINK_ON_TIME_MS         (200)

/* WiFi灯效参数：黄灯500ms快闪 */
#define LED_WIFI_BLINK_INTERVAL_MS       (500)
#define LED_WIFI_BLINK_ON_TIME_MS        (200)

#define LED_MAX_BRIGHTNESS               (255)
#define LED_BLINK_BRIGHTNESS             (200)
#define LED_BREATH_MIN_BRIGHTNESS        (0)   // 呼吸灯最弱时完全灭灯

/******************************************************************************
* Local Variables Section
******************************************************************************/ 

static LedPt0807Config_t *g_led_config = NULL;
static rdx_led_state_e g_led_state = LED_STATE_OFF;
static u32 g_led_update_timer = 0;
static u32 g_led_state_time = 0;
static u8 g_led_blink_state = 0;
static u8 g_led_connected_on_done = 0;
static u8 g_led_initialized = 0;

/* 呼吸灯亮度查表 - 4s周期，100级渐变
 * 渐亮1200ms(30点) + 最亮800ms(20点) + 渐暗1200ms(30点) + 灭灯800ms(20点)
 */
static const u8 g_breath_brightness_table[LED_BREATH_TABLE_SIZE] = {
    // 渐亮阶段 (0-29): 0 -> 255, 30个点，平滑渐变
      0,   9,  18,  27,  36,  45,  54,  64,  74,  84,
     94, 105, 116, 127, 138, 150, 162, 174, 187, 200,
    213, 223, 233, 241, 247, 251, 253, 254, 255, 255,
    // 最亮保持阶段 (30-49): 255, 20个点
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    // 渐暗阶段 (50-79): 255 -> 0, 30个点，平滑渐变
    255, 255, 254, 253, 251, 247, 241, 233, 223, 213,
    200, 187, 174, 162, 150, 138, 127, 116, 105,  94,
     84,  74,  64,  54,  45,  36,  27,  18,   9,   0,
    // 灭灯保持阶段 (80-99): 0, 20个点
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0
};

/******************************************************************************
* Function Declaration Section
******************************************************************************/ 

static void rdx_led_ctrl_update_timer_cb(void *priv);
static void rdx_led_ctrl_set_rgb_brightness(u8 r, u8 g, u8 b, u8 brightness);
static void rdx_led_ctrl_off(void);
static void rdx_led_ctrl_process_blink(u8 r, u8 g, u8 b);
static void rdx_led_ctrl_process_breath(u8 r, u8 g, u8 b);
static void rdx_led_ctrl_process_ota_blink(void);
static void rdx_led_ctrl_process_dut_blink(void);

/******************************************************************************
* Function Section
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

int rdx_led_ctrl_init(LedPt0807Config_t *config)
{
    if (config == NULL || !config->initialized) {
        g_printf("RDX LED Ctrl: Init failed\r\n");
        return -1;
    }
    g_led_config = config;
    g_led_state = LED_STATE_OFF;
    g_led_state_time = 0;
    g_led_blink_state = 0;
    g_led_connected_on_done = 0;
    led_pt0807_run_enable(config, 1);
    rdx_led_ctrl_off();
    if (g_led_update_timer == 0) {
        g_led_update_timer = sys_timer_add(NULL, rdx_led_ctrl_update_timer_cb, LED_UPDATE_INTERVAL_MS);
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
        rdx_led_ctrl_off();
        led_pt0807_run_enable(g_led_config, 0);
    }
    g_led_config = NULL;
    g_led_state = LED_STATE_OFF;
    g_led_initialized = 0;
}

void rdx_led_ctrl_set_state(rdx_led_state_e state)
{
    if (g_led_config == NULL || !g_led_initialized) {
        return;
    }
    if (g_led_state == state) {
        return;
    }
    g_printf("RDX LED: %d->%d\r\n", g_led_state, state);
    g_led_state = state;
    g_led_state_time = 0;
    g_led_blink_state = 0;
    g_led_connected_on_done = 0;
    led_pt0807_run_enable(g_led_config, 1);

    switch (state) {
        case LED_STATE_OFF:
            rdx_led_ctrl_off();
            break;
        case LED_STATE_BLE_ADV_BLINK:
            g_led_blink_state = 1;
            rdx_led_ctrl_set_rgb_brightness(LED_COLOR_BLUE_R, LED_COLOR_BLUE_G, 
                                            LED_COLOR_BLUE_B, LED_BLINK_BRIGHTNESS);
            break;
        case LED_STATE_BLE_CONNECTED:
            g_led_connected_on_done = 0;
            rdx_led_ctrl_set_rgb(LED_COLOR_GREEN_R, LED_COLOR_GREEN_G, LED_COLOR_GREEN_B);
            break;
        case LED_STATE_BLE_DISCONNECTED:
            g_led_blink_state = 1;
            rdx_led_ctrl_set_rgb_brightness(LED_COLOR_BLUE_R, LED_COLOR_BLUE_G, 
                                            LED_COLOR_BLUE_B, LED_BLINK_BRIGHTNESS);
            break;
        case LED_STATE_RECORD_BREATH:
            // 录音呼吸灯改为蓝色
            rdx_led_ctrl_set_rgb_brightness(LED_COLOR_BLUE_R, LED_COLOR_BLUE_G, 
                                            LED_COLOR_BLUE_B, LED_BREATH_MIN_BRIGHTNESS);
            break;
        case LED_STATE_CHARGE_LOW_BREATH:
            // 充电中电量<20%：红色呼吸灯
            rdx_led_ctrl_set_rgb_brightness(LED_COLOR_RED_R, LED_COLOR_RED_G, 
                                            LED_COLOR_RED_B, LED_BREATH_MIN_BRIGHTNESS);
            break;
        case LED_STATE_CHARGE_MID_BREATH:
            // 充电中电量20-80%：黄色呼吸灯
            rdx_led_ctrl_set_rgb_brightness(LED_COLOR_YELLOW_R, LED_COLOR_YELLOW_G, 
                                            LED_COLOR_YELLOW_B, LED_BREATH_MIN_BRIGHTNESS);
            break;
        case LED_STATE_CHARGE_HIGH_BREATH:
            // 充电中电量80-100%：绿色呼吸灯
            rdx_led_ctrl_set_rgb_brightness(LED_COLOR_GREEN_R, LED_COLOR_GREEN_G, 
                                            LED_COLOR_GREEN_B, LED_BREATH_MIN_BRIGHTNESS);
            break;
        case LED_STATE_CHARGE_FULL:
            // 充满电：绿色常亮
            rdx_led_ctrl_set_rgb(LED_COLOR_GREEN_R, LED_COLOR_GREEN_G, LED_COLOR_GREEN_B);
            break;
        case LED_STATE_OTA_BLINK:
            g_led_blink_state = 1;
            rdx_led_ctrl_set_rgb_brightness(LED_COLOR_BLUE_R, LED_COLOR_BLUE_G, 
                                            LED_COLOR_BLUE_B, LED_BLINK_BRIGHTNESS);
            break;
        case LED_STATE_DUT_BLINK:
            g_led_blink_state = 1;
            rdx_led_ctrl_set_rgb_brightness(LED_COLOR_YELLOW_R, LED_COLOR_YELLOW_G, 
                                            LED_COLOR_YELLOW_B, LED_BLINK_BRIGHTNESS);
            break;
        case LED_STATE_WIFI_BLINK:
            g_led_blink_state = 1;
            rdx_led_ctrl_set_rgb_brightness(LED_COLOR_YELLOW_R, LED_COLOR_YELLOW_G, 
                                            LED_COLOR_YELLOW_B, LED_BLINK_BRIGHTNESS);
            break;
        default:
            rdx_led_ctrl_off();
            break;
    }
}

rdx_led_state_e rdx_led_ctrl_get_state(void)
{
    return g_led_state;
}

static void rdx_led_ctrl_process_blink(u8 r, u8 g, u8 b)
{
    if (g_led_blink_state) {
        if (g_led_state_time >= LED_BLINK_ON_TIME_MS) {
            g_led_blink_state = 0;
            rdx_led_ctrl_off();
        }
    } else {
        if (g_led_state_time >= LED_BLINK_INTERVAL_MS) {
            g_led_state_time = 0;
            g_led_blink_state = 1;
            rdx_led_ctrl_set_rgb_brightness(r, g, b, LED_BLINK_BRIGHTNESS);
        }
    }
}

static void rdx_led_ctrl_process_breath(u8 r, u8 g, u8 b)
{
    u32 cycle_pos = g_led_state_time % LED_BREATH_CYCLE_MS;
    u32 table_index = (cycle_pos * LED_BREATH_TABLE_SIZE) / LED_BREATH_CYCLE_MS;
    if (table_index >= LED_BREATH_TABLE_SIZE) {
        table_index = LED_BREATH_TABLE_SIZE - 1;
    }
    u8 brightness = g_breath_brightness_table[table_index];
    rdx_led_ctrl_set_rgb_brightness(r, g, b, brightness);
}

/**
 * @brief OTA灯效处理：3s周期内闪两次（每次亮100ms，间隔100ms）
 *        时序: 0-100ms亮, 100-200ms灭, 200-300ms亮, 300-3000ms灭
 */
static void rdx_led_ctrl_process_ota_blink(void)
{
    u32 cycle_pos = g_led_state_time % LED_OTA_CYCLE_MS;
    
    // 第一次闪烁: 0-100ms 亮
    if (cycle_pos < LED_OTA_BLINK_ON_MS) {
        if (g_led_blink_state != 1) {
            g_led_blink_state = 1;
            rdx_led_ctrl_set_rgb_brightness(LED_COLOR_BLUE_R, LED_COLOR_BLUE_G, 
                                            LED_COLOR_BLUE_B, LED_BLINK_BRIGHTNESS);
        }
    }
    // 第一次间隔: 100-200ms 灭
    else if (cycle_pos < LED_OTA_BLINK_ON_MS + LED_OTA_BLINK_OFF_MS) {
        if (g_led_blink_state != 0) {
            g_led_blink_state = 0;
            rdx_led_ctrl_off();
        }
    }
    // 第二次闪烁: 200-300ms 亮
    else if (cycle_pos < LED_OTA_BLINK_ON_MS * 2 + LED_OTA_BLINK_OFF_MS) {
        if (g_led_blink_state != 2) {
            g_led_blink_state = 2;
            rdx_led_ctrl_set_rgb_brightness(LED_COLOR_BLUE_R, LED_COLOR_BLUE_G, 
                                            LED_COLOR_BLUE_B, LED_BLINK_BRIGHTNESS);
        }
    }
    // 剩余时间: 300-3000ms 灭
    else {
        if (g_led_blink_state != 3) {
            g_led_blink_state = 3;
            rdx_led_ctrl_off();
        }
    }
}

/**
 * @brief DUT灯效处理：黄灯1s一次闪烁
 */
static void rdx_led_ctrl_process_dut_blink(void)
{
    if (g_led_blink_state) {
        if (g_led_state_time >= LED_DUT_BLINK_ON_TIME_MS) {
            g_led_blink_state = 0;
            rdx_led_ctrl_off();
        }
    } else {
        if (g_led_state_time >= LED_DUT_BLINK_INTERVAL_MS) {
            g_led_state_time = 0;
            g_led_blink_state = 1;
            rdx_led_ctrl_set_rgb_brightness(LED_COLOR_YELLOW_R, LED_COLOR_YELLOW_G, 
                                            LED_COLOR_YELLOW_B, LED_BLINK_BRIGHTNESS);
        }
    }
}

/**
 * @brief WiFi灯效处理：黄灯500ms快闪
 */
static void rdx_led_ctrl_process_wifi_blink(void)
{
    if (g_led_blink_state) {
        if (g_led_state_time >= LED_WIFI_BLINK_ON_TIME_MS) {
            g_led_blink_state = 0;
            rdx_led_ctrl_off();
        }
    } else {
        if (g_led_state_time >= LED_WIFI_BLINK_INTERVAL_MS) {
            g_led_state_time = 0;
            g_led_blink_state = 1;
            rdx_led_ctrl_set_rgb_brightness(LED_COLOR_YELLOW_R, LED_COLOR_YELLOW_G, 
                                            LED_COLOR_YELLOW_B, LED_BLINK_BRIGHTNESS);
        }
    }
}

void rdx_led_ctrl_update(void)
{
    if (g_led_config == NULL || !g_led_initialized) {
        return;
    }
    if (!g_led_config->run_en) {
        return;
    }
    g_led_state_time += LED_UPDATE_INTERVAL_MS;

    switch (g_led_state) {
        case LED_STATE_OFF:
            break;
        case LED_STATE_BLE_ADV_BLINK:
            rdx_led_ctrl_process_blink(LED_COLOR_BLUE_R, LED_COLOR_BLUE_G, LED_COLOR_BLUE_B);
            break;
        case LED_STATE_BLE_CONNECTED:
            if (!g_led_connected_on_done) {
                if (g_led_state_time >= LED_CONNECTED_ON_TIME_MS) {
                    g_led_connected_on_done = 1;
                    rdx_led_ctrl_off();
                }
            }
            break;
        case LED_STATE_BLE_DISCONNECTED:
            rdx_led_ctrl_process_blink(LED_COLOR_BLUE_R, LED_COLOR_BLUE_G, LED_COLOR_BLUE_B);
            break;
        case LED_STATE_RECORD_BREATH:
            // 录音呼吸灯改为蓝色
            rdx_led_ctrl_process_breath(LED_COLOR_BLUE_R, LED_COLOR_BLUE_G, LED_COLOR_BLUE_B);
            break;
        case LED_STATE_OTA_BLINK:
            rdx_led_ctrl_process_ota_blink();
            break;
        case LED_STATE_DUT_BLINK:
            rdx_led_ctrl_process_dut_blink();
            break;
        case LED_STATE_WIFI_BLINK:
            rdx_led_ctrl_process_wifi_blink();
            break;
        case LED_STATE_CHARGE_LOW_BREATH:
            // 充电中电量<20%：红色呼吸灯
            rdx_led_ctrl_process_breath(LED_COLOR_RED_R, LED_COLOR_RED_G, LED_COLOR_RED_B);
            break;
        case LED_STATE_CHARGE_MID_BREATH:
            // 充电中电量20-80%：黄色呼吸灯
            rdx_led_ctrl_process_breath(LED_COLOR_YELLOW_R, LED_COLOR_YELLOW_G, LED_COLOR_YELLOW_B);
            break;
        case LED_STATE_CHARGE_HIGH_BREATH:
            // 充电中电量80-100%：绿色呼吸灯
            rdx_led_ctrl_process_breath(LED_COLOR_GREEN_R, LED_COLOR_GREEN_G, LED_COLOR_GREEN_B);
            break;
        case LED_STATE_CHARGE_FULL:
            // 充满电：绿色常亮，无需处理（已在 set_state 中设置）
            break;
        default:
            break;
    }
}

void rdx_led_ctrl_refresh(void)
{
    if (g_led_config == NULL || !g_led_initialized) {
        return;
    }
    led_pt0807_update(g_led_config);
}

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

/**
 * @brief 根据电池电量设置充电灯效
 * @param battery_percent 电池电量百分比 (0-100)
 */
void rdx_led_ctrl_set_charge_state_by_battery(u8 battery_percent)
{
    if (g_led_config == NULL || !g_led_initialized) {
        return;
    }
    
    if (battery_percent < 20) {
        // 电量<20%：红色呼吸灯
        rdx_led_ctrl_set_state(LED_STATE_CHARGE_LOW_BREATH);
    } else if (battery_percent < 80) {
        // 电量20-80%：黄色呼吸灯
        rdx_led_ctrl_set_state(LED_STATE_CHARGE_MID_BREATH);
    } else if (battery_percent < 100) {
        // 电量80-100%：绿色呼吸灯
        rdx_led_ctrl_set_state(LED_STATE_CHARGE_HIGH_BREATH);
    } else {
        // 充满电：绿色常亮
        rdx_led_ctrl_set_state(LED_STATE_CHARGE_FULL);
    }
}

/**
 * @brief 恢复系统当前状态对应的灯效（充电拔出后调用）
 */
void rdx_led_ctrl_restore_system_state(void)
{
    if (g_led_config == NULL || !g_led_initialized) {
        return;
    }
    
    // 检查当前系统状态，按优先级恢复灯效
    RecordStatus* rp = rdx_record_get_status();
    
    // 1. 检查是否在录音中
    if (rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME) {
        rdx_led_ctrl_set_state(LED_STATE_RECORD_BREATH);
        return;
    }
    
    // 2. 检查是否在 DUT 模式
    if (rdx_app_get_dut_status()) {
        rdx_led_ctrl_set_state(LED_STATE_DUT_BLINK);
        return;
    }
    
    // 3. 检查是否在 OTA 中
    extern u8 get_ota_status(void);
    if (get_ota_status()) {
        rdx_led_ctrl_set_state(LED_STATE_OTA_BLINK);
        return;
    }
    
    // 4. 检查 BLE 连接状态
    rdx_ble_server_info_t *ble_info = rdx_ble_server_get_info();
    if (ble_info && ble_info->ble_conn) {
        // BLE 已连接，熄灭 LED
        rdx_led_ctrl_set_state(LED_STATE_OFF);
    } else {
        // BLE 未连接，显示广播灯效
        rdx_led_ctrl_set_state(LED_STATE_BLE_ADV_BLINK);
    }
}
