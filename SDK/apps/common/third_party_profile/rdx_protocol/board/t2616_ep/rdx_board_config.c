#include "rdx_board_config.h"
#include "gpio_config.h"

/*
 * 阶段 1 先硬编码当前板型的引脚。
 * 这些值来自现有 rdx_app.h / rdx_spi.h / xxpUart.h 中的宏。
 */
static const rdx_board_config_t g_rdx_board_t2616_ep = {
    /* 电源和 LED 引脚 — earphone 板型差异 */
    .wifi_power_io      = IO_PORTA_04,      /* 与 CC 共用 WiFi 模块引脚 */
    .vdd_power_io       = RDX_IO_INVALID,    /* EP 无线充电仓无 VDD_POWER 独立供电 */
    .led_data_io        = IO_PORTB_02,      /* earphone LED 引脚不同于 CC 的 PORTC_01 */

    /* SPI 引脚 — earphone 使用不同的 SPI 端口 */
    .spi_cs_io          = IO_PORTB_04,
    .spi_clk_io         = IO_PORTB_03,
    .spi_mosi_io        = IO_PORTB_05,
    .spi_miso_io        = IO_PORTB_06,
    .spi_handshake_io   = IO_PORTA_07,

    .spi_port           = 1,
    .spi_clk_hz         = 8000000,

    .chip_family        = RDX_CHIP_FAMILY,
    .board_name         = RDX_BOARD_NAME,
};

const rdx_board_config_t *rdx_board_get_config(void)
{
    return &g_rdx_board_t2616_ep;
}
