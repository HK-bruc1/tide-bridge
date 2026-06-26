#include "board/t2616_cc/rdx_board_config.h"
#include "gpio_config.h"

/*
 * 阶段 1 先硬编码当前板型的引脚。
 * 这些值来自现有 rdx_app.h / rdx_spi.h / xxpUart.h 中的宏。
 */
static const rdx_board_config_t g_rdx_board_t2616_cc = {
    /* 电源和 LED 引脚 — 来自 rdx_app.h / xxpUart.h */
    .wifi_power_io      = IO_PORTA_04,      /* WIFI_POWER_PORT_IO, xxpUart.h:39 */
    .vdd_power_io       = IO_PORTA_00,      /* VDD_POWER_PORT_IO,  rdx_app.h:42 */
    .led_data_io        = IO_PORTC_01,      /* LED_PT0807_DATA_PORT_IO, rdx_app.h:46 */

    /* SPI 引脚 — 来自 rdx_spi.c */
    .spi_cs_io          = IO_PORTE_05,      /* ESP8684_CS_PORT_IO */
    .spi_clk_io         = IO_PORTA_05,      /* ESP8684_SCLK_PORT_IO */
    .spi_mosi_io        = IO_PORTA_06,      /* ESP8684_MOSI_PORT_IO */
    .spi_miso_io        = RDX_IO_INVALID,    /* 无 MISO */
    .spi_handshake_io   = IO_PORTA_03,      /* ESP8684_HANDSHAKE_PORT_IO */

    .spi_port           = 0,
    .spi_clk_hz         = 4000000,

    .chip_family        = RDX_CHIP_FAMILY,
    .board_name         = RDX_BOARD_NAME,
};

const rdx_board_config_t *rdx_board_get_config(void)
{
    return &g_rdx_board_t2616_cc;
}
