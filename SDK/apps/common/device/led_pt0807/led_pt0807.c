/*=====================================================================================
 HEADER NAME: led_pt0807.c
 MODULE NAME: LED PT0807-GRB driver module.
 
 PRE-INCLUDE FILES DESCRIPTION: 	
 
 GENERAL DESCRIPTION: 	
 	This File implements the driver for XR-PT0807-GRB RGB LED chip.
 	Support single-wire serial communication protocol with 256-level grayscale.
 	
 	Implementation using SPI to simulate single-wire protocol:
 	思路: 颜色数据(R G B) -> SPI数据 -> DO(数据)引脚模拟灯珠通信协议
 	
 	要点:
 	- 确保SPI的数据发送精确度，系统时钟跑最高、SPI时钟跑最高
 	- 发送数据时，用最后一个字节的第7位控制发送完数据后DO引脚的高低电平
 	
 	Protocol Specification (from 0807-GRB规格书):
 	- 24-bit data format: GRB order, MSB first
 	- Timing parameters:
 	  T0H: 0.20-0.35us (0码高电平时间)
 	  T1H: 0.65-1.0us (1码高电平时间)
 	  T0L: 1.55-30us (0码低电平时间)
 	  T1L: 1.10-30us (1码低电平时间)
 	  TRST: 100us minimum (RESET低电平时间)
 =======================================================================================	
 Revision History: 
 ---------------------------------------
 Author: sheng.dong
 Date: 2025-01-07
 LastEditors: sheng.dong
 LastEditTime: 2025-01-07
 FilePath: \SDK\apps\common\device\led_pt0807\led_pt0807.c
 
 Self-documenting Code
=====================================================================================*/

#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".led_pt0807.data.bss")
#pragma data_seg(".led_pt0807.data")
#pragma const_seg(".led_pt0807.text.const")
#pragma code_seg(".led_pt0807.text")
#endif

/******************************************************************************
* Include files
******************************************************************************/ 
#include "led_pt0807.h"
#include "gpio.h"
#include "clock.h"
#include "system/includes.h"
#include "asm/cpu.h"
#include "spi.h"

/******************************************************************************
* Macro Define Section
******************************************************************************/ 

/* HSV颜色空间常量 */
#define MAX_H       (360)       /* 色调最大值 */
#define MAX_S       (255)       /* 饱和度最大值 */
#define MAX_V       (255)       /* 明度最大值 */
#define MAX_H_F     (360.0f)    /* 色调最大浮点值 */
#define MAX_S_F     (255.0f)    /* 饱和度最大浮点值 */
#define MAX_V_F     (255.0f)    /* 明度最大浮点值 */
#define MAX_RGB_F   (255.0f)    /* RGB最大取值 */

/* 辅助宏 */
#define m_min(a, b, c)  (((a) < (b)) ? ((a) < (c) ? (a) : (c)) : ((b) < (c) ? (b) : (c)))
#define m_max(a, b, c)  (((a) > (b)) ? ((a) > (c) ? (a) : (c)) : ((b) > (c) ? (b) : (c)))

/******************************************************************************
* Local Variables Section
******************************************************************************/ 

/* 测试模式状态 */
static LedPt0807Config_t *g_test_config = NULL;
static u32 g_test_timer = 0;
static u8 g_test_color_index = 0;  /* 0=红, 1=绿, 2=蓝 */

/******************************************************************************
* Function Declaration Section
******************************************************************************/ 

/******************************************************************************
* Function Section
******************************************************************************/ 

/**
 * @brief HSV转RGB颜色
 */
int led_pt0807_hsv_to_rgb(const LedPt0807Hsv_t *hsv, LedPt0807Rgb_t *rgb)
{
    if (hsv == NULL || rgb == NULL) {
        return -1;
    }

    float h = (float)hsv->h;
    float s = (float)hsv->s / MAX_S_F;
    float v = (float)hsv->v / MAX_V_F;
    
    float c = v * s;
    float x = c * (1 - fabsf(fmodf(h / 60.0f, 2) - 1));
    float m = v - c;
    
    float r, g, b;
    
    if (h < 60) {
        r = c; g = x; b = 0;
    } else if (h < 120) {
        r = x; g = c; b = 0;
    } else if (h < 180) {
        r = 0; g = c; b = x;
    } else if (h < 240) {
        r = 0; g = x; b = c;
    } else if (h < 300) {
        r = x; g = 0; b = c;
    } else {
        r = c; g = 0; b = x;
    }
    
    rgb->r = (u8)((r + m) * MAX_RGB_F);
    rgb->g = (u8)((g + m) * MAX_RGB_F);
    rgb->b = (u8)((b + m) * MAX_RGB_F);
    
    return 0;
}

/**
 * @brief RGB转HSV颜色
 */
int led_pt0807_rgb_to_hsv(const LedPt0807Rgb_t *rgb, LedPt0807Hsv_t *hsv)
{
    if (rgb == NULL || hsv == NULL) {
        return -1;
    }

    float r = (float)rgb->r / MAX_RGB_F;
    float g = (float)rgb->g / MAX_RGB_F;
    float b = (float)rgb->b / MAX_RGB_F;
    
    float max_val = m_max(r, g, b);
    float min_val = m_min(r, g, b);
    float delta = max_val - min_val;
    
    /* 明度 */
    hsv->v = (int)(max_val * MAX_V_F);
    
    /* 饱和度 */
    if (max_val == 0) {
        hsv->s = 0;
    } else {
        hsv->s = (int)((delta / max_val) * MAX_S_F);
    }
    
    /* 色调 */
    if (delta == 0) {
        hsv->h = 0;
    } else if (max_val == r) {
        hsv->h = (int)(60 * fmodf((g - b) / delta, 6));
    } else if (max_val == g) {
        hsv->h = (int)(60 * ((b - r) / delta + 2));
    } else {
        hsv->h = (int)(60 * ((r - g) / delta + 4));
    }
    
    if (hsv->h < 0) {
        hsv->h += MAX_H;
    }
    
    return 0;
}

/**
 * @brief 将单个RGB颜色转换为SPI数据
 * 
 * 转换原理:
 * - 每个颜色分量(8bit)的每一个bit都转换为一个SPI字节
 * - bit为0时发送code_0 (0码), bit为1时发送code_1 (1码)
 * - 高位先发, 按照GRB顺序
 */
void led_pt0807_rgb_to_spi(const LedPt0807Rgb_t *rgb, LedPt0807Spi_t *spi, const LedPt0807Code_t *code)
{
    if (rgb == NULL || spi == NULL || code == NULL) {
        return;
    }

    /* 转换G分量 (高位先发) */
    for (int i = 0; i < 8; i++) {
        spi->g[i] = (rgb->g & BIT(7 - i)) ? code->code_1 : code->code_0;
    }
    
    /* 转换R分量 (高位先发) */
    for (int i = 0; i < 8; i++) {
        spi->r[i] = (rgb->r & BIT(7 - i)) ? code->code_1 : code->code_0;
    }
    
    /* 转换B分量 (高位先发) */
    for (int i = 0; i < 8; i++) {
        spi->b[i] = (rgb->b & BIT(7 - i)) ? code->code_1 : code->code_0;
    }
}

/**
 * @brief 将所有RGB颜色数据转换为SPI数据
 */
void led_pt0807_all_rgb_to_spi(LedPt0807Config_t *config)
{
    if (config == NULL || config->buff.rgb == NULL || config->buff.spi == NULL) {
        return;
    }

    for (u16 i = 0; i < config->pixel_count; i++) {
        led_pt0807_rgb_to_spi(&config->buff.rgb[i], &config->buff.spi[i], &config->code);
    }
}

/**
 * @brief SPI初始化
 */
static int led_pt0807_spi_init(LedPt0807Config_t *config, u32 data_gpio)
{
    if (config == NULL) {
        return -1;
    }

    /* SPI配置结构体 
     * 使用单线输出模式，只使用DO引脚发送数据
     */
    struct spi_platform_data spi_cfg = {
        .port = {
            NO_CONFIG_PORT,     /* CLK - 不使用 */
            data_gpio,          /* DO - 数据输出引脚 */
            NO_CONFIG_PORT,     /* DI - 不使用 */
            NO_CONFIG_PORT,     /* D2(wp) - 不使用 */
            NO_CONFIG_PORT,     /* D3(hold) - 不使用 */
            NO_CONFIG_PORT,     /* CS - 不使用 (0xff表示主机不操作cs) */
        },
        .role = SPI_ROLE_MASTER,
        .mode = SPI_MODE_BIDIR_1BIT,    /* 全双工单bit模式，只用DO发送 */
        .bit_mode = SPI_FIRST_BIT_MSB,  /* 高位先发 */
        .cpol = 0,              /* 空闲时CLK为低电平 */
        .cpha = 0,              /* 第一个边沿采样 */
        .ie_en = 0,             /* 不使能中断 */
        .irq_priority = 3,
        .spi_isr_callback = NULL,
        .clk = LED_PT0807_SPI_BAUD_HZ,  /* SPI波特率: 8MHz */
    };

    /* 设置SPI时钟源为最高 */
    clk_set_api("spi", LED_PT0807_SPI_CLK_HZ);
    config->spi_clk = clk_get("spi");
    
    /* 打开SPI */
    int ret = spi_open(config->spi_port, &spi_cfg);
    if (ret < 0) {
        g_printf("LED PT0807: SPI open failed, ret=%d\r\n", ret);
        return -2;
    }

    /* 设置SPI波特率 */
    spi_set_baud(config->spi_port, LED_PT0807_SPI_BAUD_HZ);
    
    g_printf("LED PT0807: SPI init ok, spi_clk=%d, baud=%d\r\n", 
             config->spi_clk, LED_PT0807_SPI_BAUD_HZ);

    return 0;
}

/**
 * @brief 初始化LED驱动 (SPI模式)
 */
int led_pt0807_init(LedPt0807Config_t *config, int spi_port, u32 data_gpio, u16 pixel_count)
{
    if (config == NULL || pixel_count == 0 || pixel_count > LED_PT0807_MAX_PIXELS) {
        return -1;
    }

    /* 清零配置结构体 */
    memset(config, 0, sizeof(LedPt0807Config_t));

    /* 保存配置 */
    config->data_pin = data_gpio;
    config->port = (enum gpio_port)(data_gpio / 16);
    config->pin_mask = BIT(data_gpio % 16);
    config->spi_port = spi_port;
    config->pixel_count = pixel_count;
    
    /* 设置转换码 (优化后的值)
     * 0码: 0x40 (01000000) - 1个高位 + 7个低位
     * 1码: 0x7C (01111100) - 5个高位 + 3个低位
     */
    config->code.code_0 = LED_PT0807_SPI_CODE_0;
    config->code.code_1 = LED_PT0807_SPI_CODE_1;

    /* 分配缓冲区内存 */
    config->buff.rgb = (LedPt0807Rgb_t *)malloc(sizeof(LedPt0807Rgb_t) * pixel_count);
    config->buff.spi = (LedPt0807Spi_t *)malloc(sizeof(LedPt0807Spi_t) * pixel_count);
    
    if (config->buff.rgb == NULL || config->buff.spi == NULL) {
        if (config->buff.rgb) free(config->buff.rgb);
        if (config->buff.spi) free(config->buff.spi);
        g_printf("LED PT0807: Memory alloc failed\r\n");
        return -2;
    }

    /* 清零缓冲区 */
    memset(config->buff.rgb, 0, sizeof(LedPt0807Rgb_t) * pixel_count);
    memset(config->buff.spi, 0, sizeof(LedPt0807Spi_t) * pixel_count);

    /* 初始化SPI */
    int ret = led_pt0807_spi_init(config, data_gpio);
    if (ret < 0) {
        free(config->buff.rgb);
        free(config->buff.spi);
        return -3;
    }

    config->initialized = 1;
    config->run_en = 1;

    /* 发送RESET信号，确保LED处于初始状态 */
    led_pt0807_send_reset(config);

    g_printf("LED PT0807: Init ok, pixels=%d, code_0=0x%02X, code_1=0x%02X\r\n",
             pixel_count, config->code.code_0, config->code.code_1);

    return 0;
}

/**
 * @brief 反初始化LED驱动
 */
int led_pt0807_deinit(LedPt0807Config_t *config)
{
    if (config == NULL || !config->initialized) {
        return -1;
    }

    /* 停止测试模式 */
    led_pt0807_stop_test_mode(config);

    /* 关闭所有LED */
    led_pt0807_clear_all(config);

    /* 关闭SPI */
    spi_deinit(config->spi_port);

    /* 释放缓冲区内存 */
    if (config->buff.rgb) {
        free(config->buff.rgb);
        config->buff.rgb = NULL;
    }
    if (config->buff.spi) {
        free(config->buff.spi);
        config->buff.spi = NULL;
    }
    if (config->buff.hsv) {
        free(config->buff.hsv);
        config->buff.hsv = NULL;
    }

    config->initialized = 0;
    config->run_en = 0;

    g_printf("LED PT0807: Deinit ok\r\n");
    return 0;
}

/**
 * @brief 发送RESET信号 (低电平至少100us)
 * 
 * 注意: RESET信号通过发送足够的低电平实现
 * 规格书要求: RESET低电平时间最小100us, 典型150us
 */
void led_pt0807_send_reset(LedPt0807Config_t *config)
{
    if (config == NULL || !config->initialized) {
        return;
    }

    /* 配置GPIO为输出低电平 */
    gpio_set_mode(IO_PORT_SPILT(config->data_pin), PORT_OUTPUT_LOW);

    /* 延时RESET时间 (150us typical) */
    udelay(LED_PT0807_TRST_US);
}

/**
 * @brief 发送单个LED的SPI数据
 */
void led_pt0807_send_spi_pixel(LedPt0807Config_t *config, const LedPt0807Spi_t *spi)
{
    if (config == NULL || !config->initialized || spi == NULL) {
        return;
    }

    /* 使用SPI DMA发送24字节数据 (8*3 = 24字节) */
    spi_dma_send(config->spi_port, (const void *)spi, sizeof(LedPt0807Spi_t));
}

/**
 * @brief 发送单个LED的RGB数据
 */
void led_pt0807_send_pixel(LedPt0807Config_t *config, LedPt0807Rgb_t *rgb)
{
    if (config == NULL || !config->initialized || rgb == NULL) {
        return;
    }

    LedPt0807Spi_t spi;
    led_pt0807_rgb_to_spi(rgb, &spi, &config->code);
    led_pt0807_send_spi_pixel(config, &spi);
}

/**
 * @brief 发送多个LED的数据 (使用SPI DMA)
 * 
 * 数据格式: TRST + LED1(24byte) + LED2(24byte) + ... + LEDN(24byte) + TRST
 */
void led_pt0807_send_pixels(LedPt0807Config_t *config, LedPt0807Rgb_t *rgb_array, u16 pixel_count)
{
    if (config == NULL || !config->initialized || rgb_array == NULL || pixel_count == 0) {
        return;
    }

    /* 转换RGB数据到SPI缓冲区 */
    for (u16 i = 0; i < pixel_count && i < config->pixel_count; i++) {
        led_pt0807_rgb_to_spi(&rgb_array[i], &config->buff.spi[i], &config->code);
    }

    /* 发送RESET信号 */
    led_pt0807_send_reset(config);

    /* 检查并更新SPI时钟 (确保时钟配置正确) */
    if (clk_get("spi") != config->spi_clk) {
        clk_set_api("spi", LED_PT0807_SPI_CLK_HZ);
        config->spi_clk = clk_get("spi");
        spi_set_baud(config->spi_port, LED_PT0807_SPI_BAUD_HZ);
    }

    /* 使用SPI DMA发送所有数据
     * 数据长度 = sizeof(LedPt0807Spi_t) * pixel_count = 24 * pixel_count 字节
     */
    spi_dma_transmit_for_isr(config->spi_port, config->buff.spi, 
                              sizeof(LedPt0807Spi_t) * pixel_count, 0);

    /* 发送RESET信号 (确保数据锁存) */
    /* 注意: DMA发送是异步的,这里需要等待发送完成或使用延时 */
    udelay(LED_PT0807_TRST_US);
}

/**
 * @brief 更新并发送所有LED数据 (使用内部缓冲区)
 */
void led_pt0807_update(LedPt0807Config_t *config)
{
    if (config == NULL || !config->initialized || !config->run_en) {
        return;
    }

    /* 将所有RGB数据转换为SPI数据 */
    led_pt0807_all_rgb_to_spi(config);

    /* 检查并更新SPI时钟 */
    if (clk_get("spi") != config->spi_clk) {
        clk_set_api("spi", LED_PT0807_SPI_CLK_HZ);
        config->spi_clk = clk_get("spi");
        spi_set_baud(config->spi_port, LED_PT0807_SPI_BAUD_HZ);
    }

    /* 使用SPI DMA发送所有数据 */
    spi_dma_transmit_for_isr(config->spi_port, config->buff.spi,
                              sizeof(LedPt0807Spi_t) * config->pixel_count, 0);
}

/**
 * @brief 设置单个LED的RGB颜色 (写入缓冲区,不立即发送)
 */
int led_pt0807_set_pixel_rgb(LedPt0807Config_t *config, u16 pixel_index, const LedPt0807Rgb_t *rgb)
{
    if (config == NULL || !config->initialized || rgb == NULL) {
        return -1;
    }

    if (pixel_index >= config->pixel_count) {
        return -2;
    }

    /* 写入RGB缓冲区 */
    config->buff.rgb[pixel_index] = *rgb;

    return 0;
}

/**
 * @brief 设置所有LED为同一颜色 (写入缓冲区,不立即发送)
 */
void led_pt0807_set_all_rgb(LedPt0807Config_t *config, const LedPt0807Rgb_t *rgb)
{
    if (config == NULL || !config->initialized || rgb == NULL) {
        return;
    }

    for (u16 i = 0; i < config->pixel_count; i++) {
        config->buff.rgb[i] = *rgb;
    }
}

/**
 * @brief 关闭所有LED (发送全0数据)
 */
void led_pt0807_clear_all(LedPt0807Config_t *config)
{
    if (config == NULL || !config->initialized) {
        return;
    }

    /* 清零RGB缓冲区 */
    memset(config->buff.rgb, 0, sizeof(LedPt0807Rgb_t) * config->pixel_count);

    /* 清零SPI缓冲区 */
    memset(config->buff.spi, 0, sizeof(LedPt0807Spi_t) * config->pixel_count);

    /* 转换并发送数据 (全0会显示黑色/关闭) */
    led_pt0807_all_rgb_to_spi(config);
    
    /* 发送RESET */
    led_pt0807_send_reset(config);

    /* 发送数据 */
    spi_dma_send(config->spi_port, config->buff.spi, 
                 sizeof(LedPt0807Spi_t) * config->pixel_count);

    /* 发送RESET */
    udelay(LED_PT0807_TRST_US);
}

/**
 * @brief 设置运行使能
 */
void led_pt0807_run_enable(LedPt0807Config_t *config, u8 en)
{
    if (config == NULL || !config->initialized) {
        return;
    }

    config->run_en = en;
    
    if (!en) {
        led_pt0807_clear_all(config);
    }
}

/******************************************************************************/
/* LED测试模式相关函数 */
/******************************************************************************/

/**
 * @brief LED测试模式定时器回调函数
 */
static void led_pt0807_test_timer_cb(void *priv)
{
    if (g_test_config == NULL || !g_test_config->initialized) {
        return;
    }

    /* 循环三种颜色：红->绿->蓝 */
    LedPt0807Rgb_t color = {0, 0, 0};
    switch (g_test_color_index) {
        case 0:
            color.r = 255; color.g = 0; color.b = 0;  /* 红色 (GRB: g=0,r=255,b=0) */
            g_printf("LED Test: RED\r\n");
            break;
        case 1:
            color.r = 0; color.g = 255; color.b = 0;  /* 绿色 */
            g_printf("LED Test: GREEN\r\n");
            break;
        case 2:
            color.r = 0; color.g = 0; color.b = 255;  /* 蓝色 */
            g_printf("LED Test: BLUE\r\n");
            break;
        default:
            color.r = 255; color.g = 0; color.b = 0;
            g_test_color_index = 0;
            break;
    }

    /* 设置所有LED为同一颜色 */
    led_pt0807_set_all_rgb(g_test_config, &color);

    /* 更新并发送数据 */
    led_pt0807_update(g_test_config);

    /* 切换到下一个颜色 */
    g_test_color_index = (g_test_color_index + 1) % 3;

    /* 重新启动定时器，1秒后切换 */
    if (g_test_timer) {
        sys_timer_modify(g_test_timer, 1000);  /* 1秒 */
    }
}

/**
 * @brief 启动LED测试模式（三种颜色循环：红绿蓝各1秒）
 */
int led_pt0807_start_test_mode(LedPt0807Config_t *config)
{
    if (config == NULL || !config->initialized) {
        return -1;
    }

    /* 如果已经在测试模式，先停止 */
    if (g_test_config != NULL) {
        led_pt0807_stop_test_mode(g_test_config);
    }

    g_test_config = config;
    g_test_color_index = 0;

    /* 立即显示第一种颜色（红色） */
    LedPt0807Rgb_t red = {0, 255, 0};  /* GRB顺序: g=0, r=255, b=0 = 红色 */
    g_printf("LED Test: Starting with RED\r\n");
    led_pt0807_set_all_rgb(config, &red);
    led_pt0807_update(config);

    /* 启动定时器，1秒后切换到绿色 */
    g_test_timer = sys_timer_add(NULL, led_pt0807_test_timer_cb, 1000);
    if (g_test_timer == 0) {
        g_test_config = NULL;
        g_printf("LED Test: Timer create failed\r\n");
        return -2;
    }
    g_printf("LED Test: Timer created, id=%d\r\n", g_test_timer);

    return 0;
}

/**
 * @brief 停止LED测试模式
 */
int led_pt0807_stop_test_mode(LedPt0807Config_t *config)
{
    if (config == NULL) {
        return -1;
    }

    /* 停止定时器 */
    if (g_test_timer) {
        sys_timer_del(g_test_timer);
        g_test_timer = 0;
    }

    /* 清除测试配置 */
    g_test_config = NULL;

    /* 关闭LED */
    if (config->initialized) {
        led_pt0807_clear_all(config);
    }

    g_printf("LED Test: Stopped\r\n");
    return 0;
}
