#ifndef __RDX_PORT_SPI_H__
#define __RDX_PORT_SPI_H__

#include "typedef.h"

#ifdef __cplusplus
extern "C" {
#endif

void rdx_port_spi_cs_init(u32 cs_io);
void rdx_port_spi_cs_uninit(u32 cs_io);

#ifdef __cplusplus
}
#endif

#endif
