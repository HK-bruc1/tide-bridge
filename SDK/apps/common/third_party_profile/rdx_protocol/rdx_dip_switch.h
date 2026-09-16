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
// Called after dev_manager_init(), before selecting the first mode.
void rdx_dip_switch_init(void);
int rdx_dip_switch_pc_allowed(void);
int rdx_dip_switch_cold_service(void);
void rdx_dip_switch_note_business_mode(void);

#endif // TCFG_DIP_SWITCH_POWER_ENABLE

#endif // __RDX_DIP_SWITCH_H__
