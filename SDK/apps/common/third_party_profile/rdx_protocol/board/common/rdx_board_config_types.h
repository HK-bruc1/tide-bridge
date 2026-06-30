#ifndef __RDX_BOARD_CONFIG_TYPES_H__
#define __RDX_BOARD_CONFIG_TYPES_H__

#include "typedef.h"

#ifndef RDX_IO_INVALID
#define RDX_IO_INVALID      0xff
#endif

typedef struct {
    u32 wifi_power_io;
    u32 vdd_power_io;
    u32 led_data_io;

    u32 spi_cs_io;
    u32 spi_clk_io;
    u32 spi_mosi_io;
    u32 spi_miso_io;
    u32 spi_handshake_io;

    u8  spi_port;
    u32 spi_clk_hz;

    u32 mic_bias_ce_io;
    u32 sd_nand_data0_io;
    u32 sd_nand_clk_io;
    u32 sd_nand_cmd_io;

    u8  led_spi_instance;

    const char *chip_family;
    const char *board_name;
} rdx_board_config_t;

#endif
