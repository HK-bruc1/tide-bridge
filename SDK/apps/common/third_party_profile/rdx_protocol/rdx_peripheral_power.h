#ifndef RDX_PERIPHERAL_POWER_H
#define RDX_PERIPHERAL_POWER_H

#include "generic/typedef.h"

#ifdef __cplusplus
extern "C" {
#endif

/* PE5 is active high. This is the only runtime write entry for the amplifier. */
void rdx_peripheral_power_amp_set(u8 enable);
u8 rdx_peripheral_power_amp_is_enabled(void);

#ifdef __cplusplus
}
#endif

#endif /* RDX_PERIPHERAL_POWER_H */
