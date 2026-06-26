#ifndef __RDX_DEVICE_SERVICE_H__
#define __RDX_DEVICE_SERVICE_H__

#include "typedef.h"

void rdx_device_service_init(void);
int  rdx_device_factory_reset(void);
int  rdx_device_unpair(void);
int  rdx_device_soft_poweroff(void);
int  rdx_device_reboot(void);

#endif
