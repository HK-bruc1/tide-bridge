#ifndef __RDX_DIP_SWITCH_H__
#define __RDX_DIP_SWITCH_H__

#include "app_config.h"
#include "rdx_commonDef.h"
#include "gpio.h"
#include "power/power_wakeup.h"

#if TCFG_DIP_SWITCH_POWER_ENABLE

// P33 interrupt callback, registered directly by key_wakeup.c
// key_wakeup.c: port0.callback = rdx_dip_switch_p33_irq;
void rdx_dip_switch_p33_irq(P33_IO_WKUP_EDGE edge);

// Init: register deferred handler to app_core, read initial level
// Called by rdx_app_all_init()
void rdx_dip_switch_init(void);

#endif // TCFG_DIP_SWITCH_POWER_ENABLE

#endif // __RDX_DIP_SWITCH_H__
