#ifndef __RDX_DEVICE_SERVICE_H__
#define __RDX_DEVICE_SERVICE_H__

#include "typedef.h"

void rdx_device_service_init(void);

/* power management (moved from rdx_app.c) */
void rdx_device_service_soft_poweroff(void);
void rdx_device_service_poweroff_cb(void *priv);
void rdx_device_service_reboot(void);

/* device pair / unpair (charge case only) */
int  rdx_device_service_pair(char *au_code, char *mac_str, char *label_sn);
int  rdx_device_service_unpair(void);

/* factory reset / unbind shells — extracted from rdx_vm.c next */
int  rdx_device_service_factory_reset(void);

#endif
