#include "rdx_port_spi.h"
#include "gpio_config.h"

void rdx_port_spi_cs_init(u32 cs_io)
{
    gpio_set_mode(IO_PORT_SPILT(cs_io), PORT_OUTPUT_HIGH);
}

void rdx_port_spi_cs_uninit(u32 cs_io)
{
    gpio_set_mode(IO_PORT_SPILT(cs_io), PORT_HIGHZ);
}
