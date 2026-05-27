#ifndef _AURACAST_APP_BLE_H_
#define _AURACAST_APP_BLE_H_

#include "system/includes.h"

#define ATT_CHARACTERISTIC_2a00_01_VALUE_HANDLE 0x0003
#define ATT_CHARACTERISTIC_ae03_01_VALUE_HANDLE 0x0006
#define ATT_CHARACTERISTIC_ae04_01_VALUE_HANDLE 0x0008
#define ATT_CHARACTERISTIC_ae04_01_CLIENT_CONFIGURATION_HANDLE 0x0009

extern const uint8_t auracast_app_ble_profile_data[];

extern void auracast_app_ble_init(void);
extern void auracast_app_ble_exit(void);
extern int auracast_app_ble_adv_enable(u8 enable);
extern int auracast_app_ble_send(u8 *data, u32 len);
extern int auracast_app_ble_disconnect(void);

extern uint16_t auracast_app_att_read_callback(void *hdl, hci_con_handle_t connection_handle, uint16_t att_handle, uint16_t offset, uint8_t *buffer, uint16_t buffer_size);
extern int auracast_app_att_write_callback(void *hdl, hci_con_handle_t connection_handle, uint16_t att_handle, uint16_t transaction_mode, uint16_t offset, uint8_t *buffer, uint16_t buffer_size);
extern void auracast_app_cbk_packet_handler(void *hdl, uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);

#endif

