#ifndef __RDX_BLE_SERVICE_H__
#define __RDX_BLE_SERVICE_H__

#include "typedef.h"
#include "rdx_err.h"

void      rdx_ble_service_init(void);
void      rdx_ble_service_on_connected(void);
void      rdx_ble_service_on_disconnected(void);
rdx_err_t rdx_ble_service_stop_recording(void);
rdx_err_t rdx_ble_service_cleanup_protocol_state(void);
int       rdx_ble_send_data(const u8 *data, u32 len);
u8        rdx_ble_is_connected(void);
u16       rdx_ble_get_mtu(void);

#endif
