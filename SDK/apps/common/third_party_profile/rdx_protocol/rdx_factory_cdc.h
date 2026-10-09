#ifndef RDX_FACTORY_CDC_H
#define RDX_FACTORY_CDC_H
#include "app_config.h"
#if TCFG_T2620_FACTORY_USB_CDC_ENABLE && !TCFG_T2620_FACTORY_USB_CDC_TEST_ENABLE
void rdx_factory_cdc_init(void);
void rdx_factory_cdc_wake(void);
#else
static inline void rdx_factory_cdc_init(void) {}
#endif
#endif
