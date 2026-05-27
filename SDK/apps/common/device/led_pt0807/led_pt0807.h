/*=====================================================================================
 HEADER NAME: led_pt0807.h
 MODULE NAME: LED PT0807-GRB driver headfile.
 
 PRE-INCLUDE FILES DESCRIPTION: 	
 
 GENERAL DESCRIPTION: 	
 	This File implements the driver for XR-PT0807-GRB RGB LED chip.
 	Support single-wire serial communication protocol with 256-level grayscale.
 	
 	Implementation using SPI to simulate single-wire protocol:
 	- RGB data -> SPI data -> DO pin simulates LED communication protocol
 	- Each color bit (0 or 1) is represented by one SPI byte
 	- SPI DO pin outputs the timing waveform for LED communication
 =======================================================================================	
 Revision History: 
 ---------------------------------------
 Author: Refactored based on 可视化-jl701n_soundbox_1.0.0-P2 reference
 Date: 2025-01-XX
 LastEditors: Auto Generated
 LastEditTime: 2025-01-XX
 FilePath: \SDK\apps\common\device\led_pt0807\led_pt0807.h
 
 Self-documenting Code
=====================================================================================*/

#ifndef __LED_PT0807_H__
#define __LED_PT0807_H__

/******************************************************************************
* Include files
******************************************************************************/ 
#include "typedef.h"
#include "gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
* Macro Define Section
******************************************************************************/ 

/* LED PT0807-GRB 时序参数 (来自规格书) 
 * 通信信号传输定义:
 * 参数名称          参数符号   最小值   典型值   最大值   单位
 * 输入0码高电平时间  Tin0h     0.20    0.28    0.35    us
 * 输入1码高电平时间  Tin1h     0.65    0.9     1.0     us
 * 输入0码低电平时间  T0L       1.55    1.72    30      us
 * 输入1码低电平时间  T1L       1.10    1.10    30      us
 * 0码/1码周期       T0/T1     1.75    -       35      us
 * RESET码低电平时间  reset     100     150     -       us
 *
 * 注: 高电平时间介于 200ns~410ns,IC判断为"0"码
 *     高电平时间介于 640ns~1000ns,判断为"1"码
 */
#define LED_PT0807_T0H_US              (0.28f)     /* 0码高电平时间: 0.28us typical */
#define LED_PT0807_T1H_US              (0.9f)      /* 1码高电平时间: 0.9us typical */
#define LED_PT0807_T0L_US              (1.72f)     /* 0码低电平时间: 1.72us typical */
#define LED_PT0807_T1L_US              (1.10f)     /* 1码低电平时间: 1.10us typical */
#define LED_PT0807_TRST_US             (150)       /* RESET码低电平时间: 150us typical (min 100us) */
#define LED_PT0807_T_MIN_US            (1.75f)     /* 码元周期: 1.75us minimum */

/* 数据格式定义 */
#define LED_PT0807_DATA_BITS           (24)        /* 每个LED 24位数据 */
#define LED_PT0807_GRAY_LEVELS         (256)       /* 灰度等级: 256级 */
#define LED_PT0807_DATA_ORDER_GRB      (1)         /* 数据顺序: GRB (Green-Red-Blue) */

/* 电源参数 */
#define LED_PT0807_VDD_MIN             (3.5f)      /* 最小工作电压: 3.5V */
#define LED_PT0807_VDD_MAX             (5.5f)      /* 最大工作电压: 5.5V */
#define LED_PT0807_VDD_TYP             (4.5f)      /* 典型工作电压: 4.5V */

/* SPI 配置参数 
 * 思路: 使用一个SPI字节来模拟0码/1码的时序波形
 * 字符bit中1/0控制0码/1码时序波形的高和低
 * 字符bit中连续的1/0控制0码/1码时序波形的高和低持续时间
 *
 * 转换码计算 (优化后):
 * - 发送每个字符第一bit和0bit为0，可以把字符数据整体右移一个bit优化
 * - 0码: 0x40 (原来的0x80右移一位) - 模拟短高电平
 * - 1码: 0x7C (原来的0xF8右移一位) - 模拟长高电平
 *
 * 最后一个字节的bit7控制发送完数据后DO引脚的电平:
 * - bit7为1: 发送完数据后DO引脚为高电平
 * - bit7为0: 发送完数据后DO引脚为低电平
 */
#define LED_PT0807_SPI_CLK_HZ          (64000000UL) /* SPI 时钟源: 64MHz */
#define LED_PT0807_SPI_BAUD_HZ         (8000000UL)  /* SPI 波特率: 8MHz */
#define LED_PT0807_SPI_CODE_0          (0x40)       /* 0码的SPI数据 (优化后) */
#define LED_PT0807_SPI_CODE_1          (0x7C)       /* 1码的SPI数据 (优化后) */

/* 最大支持的LED数量 */
#define LED_PT0807_MAX_PIXELS          (256)

/******************************************************************************
* Structure and Enum Section
******************************************************************************/ 

/**
 * @brief SPI转换码结构体 - 0码和1码对应的SPI字节值
 */
typedef struct {
    u8 code_0;  /* 0码对应的SPI字节 */
    u8 code_1;  /* 1码对应的SPI字节 */
} LedPt0807Code_t;

/**
 * @brief SPI颜色数据结构体 - 每个颜色分量8位转换为8个SPI字节
 * 数据顺序: GRB (Green-Red-Blue), 高位先发
 */
typedef struct {
    u8 g[8];    /* 绿色分量的SPI数据 (G7-G0, 各对应一个SPI字节) */
    u8 r[8];    /* 红色分量的SPI数据 (R7-R0, 各对应一个SPI字节) */
    u8 b[8];    /* 蓝色分量的SPI数据 (B7-B0, 各对应一个SPI字节) */
} LedPt0807Spi_t;

/**
 * @brief LED RGB颜色结构体
 */
typedef struct {
    u8 g;      /* 绿色分量 (0-255) - GRB顺序,G在前 */
    u8 r;      /* 红色分量 (0-255) */
    u8 b;      /* 蓝色分量 (0-255) */
} LedPt0807Rgb_t;

/**
 * @brief HSV颜色结构体 (用于颜色转换)
 */
typedef struct {
    int h;     /* 色调 Hue (0-360) */
    int s;     /* 饱和度 Saturation (0-255) */
    int v;     /* 明度 Value (0-255) */
} LedPt0807Hsv_t;

/**
 * @brief 数据缓冲区结构体
 */
typedef struct {
    LedPt0807Rgb_t *rgb;   /* RGB颜色数据缓冲区 */
    LedPt0807Spi_t *spi;   /* SPI发送数据缓冲区 */
    LedPt0807Hsv_t *hsv;   /* HSV颜色数据缓冲区 (可选) */
} LedPt0807Buff_t;

/**
 * @brief LED 驱动配置结构体
 */
typedef struct {
    /* GPIO配置 */
    u32 data_pin;           /* SPI DO数据引脚 (GPIO) */
    enum gpio_port port;    /* GPIO端口 */
    u32 pin_mask;           /* 引脚掩码 */
    
    /* SPI配置 */
    int spi_port;           /* SPI端口号 (HW_SPI0/HW_SPI1/HW_SPI2) */
    u32 spi_clk;            /* 当前SPI时钟频率 */
    
    /* 数据配置 */
    u16 pixel_count;        /* LED灯珠数量 */
    LedPt0807Code_t code;   /* 0码/1码的SPI字节值 */
    LedPt0807Buff_t buff;   /* 数据缓冲区 */
    
    /* 状态标志 */
    u8 initialized;         /* 初始化标志 */
    u8 run_en;              /* 运行使能标志 */
} LedPt0807Config_t;

/**
 * @brief SPI硬件端口枚举
 */
enum {
    LED_PT0807_SPI0 = 0,    /* SPI0 (系统可能已使用) */
    LED_PT0807_SPI1,        /* SPI1 */
    LED_PT0807_SPI2,        /* SPI2 */
    LED_PT0807_SPI_MAX,
};

/******************************************************************************
* Global Variables Section
******************************************************************************/ 

/******************************************************************************
* Function Section
******************************************************************************/ 

/**
 * @brief 初始化LED驱动 (SPI模式)
 * @param config LED配置结构体指针
 * @param spi_port SPI端口号 (LED_PT0807_SPI0/SPI1/SPI2)
 * @param data_gpio SPI DO数据引脚GPIO编号
 * @param pixel_count LED灯珠数量
 * @return 0: 成功, <0: 失败
 */
int led_pt0807_init(LedPt0807Config_t *config, int spi_port, u32 data_gpio, u16 pixel_count);

/**
 * @brief 反初始化LED驱动
 * @param config LED配置结构体指针
 * @return 0: 成功, <0: 失败
 */
int led_pt0807_deinit(LedPt0807Config_t *config);

/**
 * @brief 发送RESET信号 (低电平至少100us)
 * @param config LED配置结构体指针
 */
void led_pt0807_send_reset(LedPt0807Config_t *config);

/**
 * @brief 将单个RGB颜色转换为SPI数据
 * @param rgb RGB颜色数据
 * @param spi SPI数据输出
 * @param code 0码/1码配置
 */
void led_pt0807_rgb_to_spi(const LedPt0807Rgb_t *rgb, LedPt0807Spi_t *spi, const LedPt0807Code_t *code);

/**
 * @brief 将所有RGB颜色数据转换为SPI数据
 * @param config LED配置结构体指针
 */
void led_pt0807_all_rgb_to_spi(LedPt0807Config_t *config);

/**
 * @brief 发送单个LED的24位数据 (使用SPI DMA)
 * @param config LED配置结构体指针
 * @param spi SPI数据
 */
void led_pt0807_send_spi_pixel(LedPt0807Config_t *config, const LedPt0807Spi_t *spi);

/**
 * @brief 发送单个LED的RGB数据
 * @param config LED配置结构体指针
 * @param rgb RGB颜色数据
 */
void led_pt0807_send_pixel(LedPt0807Config_t *config, LedPt0807Rgb_t *rgb);

/**
 * @brief 发送多个LED的数据 (使用SPI DMA)
 * @param config LED配置结构体指针
 * @param rgb_array RGB颜色数组
 * @param pixel_count LED数量
 */
void led_pt0807_send_pixels(LedPt0807Config_t *config, LedPt0807Rgb_t *rgb_array, u16 pixel_count);

/**
 * @brief 更新并发送所有LED数据 (使用内部缓冲区)
 * @param config LED配置结构体指针
 */
void led_pt0807_update(LedPt0807Config_t *config);

/**
 * @brief 设置单个LED的RGB颜色 (写入缓冲区,不立即发送)
 * @param config LED配置结构体指针
 * @param pixel_index LED索引 (从0开始)
 * @param rgb RGB颜色数据
 * @return 0: 成功, <0: 失败
 */
int led_pt0807_set_pixel_rgb(LedPt0807Config_t *config, u16 pixel_index, const LedPt0807Rgb_t *rgb);

/**
 * @brief 设置所有LED为同一颜色 (写入缓冲区,不立即发送)
 * @param config LED配置结构体指针
 * @param rgb RGB颜色数据
 */
void led_pt0807_set_all_rgb(LedPt0807Config_t *config, const LedPt0807Rgb_t *rgb);

/**
 * @brief 关闭所有LED (发送全0数据)
 * @param config LED配置结构体指针
 */
void led_pt0807_clear_all(LedPt0807Config_t *config);

/**
 * @brief 设置运行使能
 * @param config LED配置结构体指针
 * @param en 使能标志
 */
void led_pt0807_run_enable(LedPt0807Config_t *config, u8 en);

/**
 * @brief 启动LED测试模式（三种颜色循环：红绿蓝各1秒）
 * @param config LED配置结构体指针
 * @return 0: 成功, <0: 失败
 */
int led_pt0807_start_test_mode(LedPt0807Config_t *config);

/**
 * @brief 停止LED测试模式
 * @param config LED配置结构体指针
 * @return 0: 成功, <0: 失败
 */
int led_pt0807_stop_test_mode(LedPt0807Config_t *config);

/**
 * @brief HSV转RGB颜色
 * @param hsv HSV颜色
 * @param rgb RGB颜色输出
 * @return 0: 成功, <0: 失败
 */
int led_pt0807_hsv_to_rgb(const LedPt0807Hsv_t *hsv, LedPt0807Rgb_t *rgb);

/**
 * @brief RGB转HSV颜色
 * @param rgb RGB颜色
 * @param hsv HSV颜色输出
 * @return 0: 成功, <0: 失败
 */
int led_pt0807_rgb_to_hsv(const LedPt0807Rgb_t *rgb, LedPt0807Hsv_t *hsv);

#ifdef __cplusplus
}
#endif

#endif /* __LED_PT0807_H__ */
