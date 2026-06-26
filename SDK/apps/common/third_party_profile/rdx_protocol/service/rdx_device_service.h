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

/* factory reset / unbind (moved from rdx_vm.c) */
int  rdx_device_service_factory_reset(void);
void rdx_device_service_user_para_reset(void);
void rdx_device_service_unbound_handle(void);
void rdx_device_service_unbound_cb(u8 result);
void rdx_device_service_choose_to_unbound_handle(int usr_para, int format_en);
void rdx_device_service_choose_to_unbound_cb(u8 result);

#endif
