#ifndef __RDX_BOARD_CONFIG_H__
#define __RDX_BOARD_CONFIG_H__

#include "typedef.h"

/* 当前芯片族 */
#ifndef RDX_CHIP_FAMILY
#define RDX_CHIP_FAMILY     "jl7018"
#endif

/* 板型标识 */
#ifndef RDX_BOARD_NAME
#define RDX_BOARD_NAME      "t2616_ep"
#endif

/* 无效 IO 占位值：当前源码中未用引脚写 0xff */
#ifndef RDX_IO_INVALID
#define RDX_IO_INVALID      0xff
#endif

/* SPI 引脚与参数 */
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

    const char *chip_family;
    const char *board_name;
} rdx_board_config_t;

const rdx_board_config_t *rdx_board_get_config(void);

#endif
