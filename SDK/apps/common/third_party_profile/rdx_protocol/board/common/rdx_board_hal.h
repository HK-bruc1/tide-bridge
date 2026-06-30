#ifndef __RDX_BOARD_HAL_H__
#define __RDX_BOARD_HAL_H__

#include "typedef.h"

#ifdef __cplusplus
extern "C" {
#endif

/* WiFi 电源控制（上电由 wifi transport vtable 驱动，不走 GPIO HAL） */
void rdx_board_wifi_power_off(void);

/* VDD 电源控制 */
void rdx_board_vdd_power_on(void);
void rdx_board_vdd_power_low(void);
void rdx_board_vdd_power_off_highz(void);

/* LED 数据 IO */
u32  rdx_board_led_data_io(void);
u8   rdx_board_led_spi_instance(void);

/* 分场景 IO 状态切换 */
void rdx_board_shutdown_io_state(void);
void rdx_board_charge_poweroff_io_state(void);
void rdx_board_sd_nand_poweroff_io_state(void);

/* P1: SPI handshake pin */
u32  rdx_board_spi_handshake_io(void);

#ifdef __cplusplus
}
#endif

#endif
