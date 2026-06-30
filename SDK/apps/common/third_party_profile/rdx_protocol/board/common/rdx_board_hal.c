#include "rdx_board_hal.h"
#include "rdx_board_config.h"
#include "gpio_config.h"

void rdx_board_wifi_power_off(void)
{
    const rdx_board_config_t *cfg = rdx_board_get_config();
    if (cfg->wifi_power_io != RDX_IO_INVALID) {
        gpio_set_mode(IO_PORT_SPILT(cfg->wifi_power_io), PORT_HIGHZ);
    }
}

void rdx_board_vdd_power_on(void)
{
    const rdx_board_config_t *cfg = rdx_board_get_config();
    if (cfg->vdd_power_io != RDX_IO_INVALID) {
        gpio_set_mode(IO_PORT_SPILT(cfg->vdd_power_io), PORT_OUTPUT_HIGH);
    }
}

void rdx_board_vdd_power_low(void)
{
    const rdx_board_config_t *cfg = rdx_board_get_config();
    if (cfg->vdd_power_io != RDX_IO_INVALID) {
        gpio_set_mode(IO_PORT_SPILT(cfg->vdd_power_io), PORT_OUTPUT_LOW);
    }
}

void rdx_board_vdd_power_off_highz(void)
{
    const rdx_board_config_t *cfg = rdx_board_get_config();
    if (cfg->vdd_power_io != RDX_IO_INVALID) {
        gpio_set_mode(IO_PORT_SPILT(cfg->vdd_power_io), PORT_HIGHZ);
    }
}

u32 rdx_board_led_data_io(void)
{
    return rdx_board_get_config()->led_data_io;
}

u8 rdx_board_led_spi_instance(void)
{
    return rdx_board_get_config()->led_spi_instance;
}

void rdx_board_shutdown_io_state(void)
{
    const rdx_board_config_t *cfg = rdx_board_get_config();

    gpio_set_mode(IO_PORT_SPILT(cfg->led_data_io),      PORT_HIGHZ);
    if (cfg->mic_bias_ce_io != RDX_IO_INVALID) {
        gpio_set_mode(IO_PORT_SPILT(cfg->mic_bias_ce_io),    PORT_HIGHZ);
    }
    if (cfg->sd_nand_clk_io != RDX_IO_INVALID) {
        gpio_set_mode(IO_PORT_SPILT(cfg->sd_nand_clk_io),    PORT_HIGHZ);
    }
    if (cfg->sd_nand_cmd_io != RDX_IO_INVALID) {
        gpio_set_mode(IO_PORT_SPILT(cfg->sd_nand_cmd_io),    PORT_HIGHZ);
    }
}

void rdx_board_charge_poweroff_io_state(void)
{
    const rdx_board_config_t *cfg = rdx_board_get_config();

    gpio_set_mode(IO_PORT_SPILT(cfg->led_data_io),      PORT_HIGHZ);
    if (cfg->mic_bias_ce_io != RDX_IO_INVALID) {
        gpio_set_mode(IO_PORT_SPILT(cfg->mic_bias_ce_io),    PORT_HIGHZ);
    }
}

void rdx_board_sd_nand_poweroff_io_state(void)
{
    const rdx_board_config_t *cfg = rdx_board_get_config();

    if (cfg->sd_nand_clk_io != RDX_IO_INVALID) {
        gpio_set_mode(IO_PORT_SPILT(cfg->sd_nand_clk_io),    PORT_HIGHZ);
    }
    if (cfg->sd_nand_cmd_io != RDX_IO_INVALID) {
        gpio_set_mode(IO_PORT_SPILT(cfg->sd_nand_cmd_io),    PORT_HIGHZ);
    }
}

/* ---- P1: SPI handshake pin ---- */

u32 rdx_board_spi_handshake_io(void)
{
    return rdx_board_get_config()->spi_handshake_io;
}
