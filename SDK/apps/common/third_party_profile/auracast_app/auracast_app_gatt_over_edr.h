#ifndef _AURACAST_APP_GATT_OVER_EDR_H_
#define _AURACAST_APP_GATT_OVER_EDR_H_

#include "system/includes.h"

extern void auracast_app_gatt_over_edr_init(void);
extern void auracast_app_gatt_over_edr_exit(void);
extern int auracast_app_gatt_over_edr_send(u8 *data, u32 len);

#endif

