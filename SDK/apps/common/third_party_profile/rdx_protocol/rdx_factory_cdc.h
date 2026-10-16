#ifndef RDX_FACTORY_CDC_H
#define RDX_FACTORY_CDC_H
#include "app_config.h"
#if TCFG_T2620_FACTORY_USB_CDC_ENABLE && !TCFG_T2620_FACTORY_USB_CDC_TEST_ENABLE
void rdx_factory_cdc_init(void);
void rdx_factory_cdc_wake(void);
/* poll 仅在 app_core 调用；needs_poll 只读状态，供恢复桥接和休眠准入查询。 */
void rdx_factory_cdc_poll(void);
int rdx_factory_cdc_needs_poll(void);
#else
static inline void rdx_factory_cdc_init(void) {}
static inline void rdx_factory_cdc_poll(void) {}
static inline int rdx_factory_cdc_needs_poll(void) { return 0; }
#endif
#endif
