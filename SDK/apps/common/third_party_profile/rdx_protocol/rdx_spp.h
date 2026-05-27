#ifndef _RDX_SPP_H_
#define _RDX_SPP_H_

#include "system/includes.h"

void rdx_spp_init(void);
void rdx_spp_exit(void);
int rdx_spp_send(u8 *data, u32 len);

#endif

