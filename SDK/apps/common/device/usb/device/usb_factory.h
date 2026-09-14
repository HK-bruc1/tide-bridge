#ifndef T2620_USB_FACTORY_H
#define T2620_USB_FACTORY_H

#include "app_config.h"

#if TCFG_T2620_FACTORY_USB_CDC_ENABLE
/* app_core publishes policy; usb_stack alone owns class resources. */
void usb_factory_service(void);
int usb_factory_msc_started(void);
int usb_factory_cdc_started(void);
int usb_factory_shutdown(void);
/* PC owner publishes only after successful SD takeover. */
int pc_storage_usb_ready(void);
#endif

#endif
