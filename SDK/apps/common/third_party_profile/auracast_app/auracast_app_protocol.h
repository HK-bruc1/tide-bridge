#ifndef _AURACAST_APP_PROTOCOL_H_
#define _AURACAST_APP_PROTOCOL_H_

#include "system/includes.h"
#include "auracast_app_ble.h"
#include "auracast_app_source_api.h"
#include "auracast_app_sink_api.h"
#include "auracast_app_gatt_over_edr.h"

#define AURACAST_APP_OPCODE_RECV_GET_DEVICE_INFO             (0x01)
#define AURACAST_APP_OPCODE_RECV_LOGIN_AUTHENTICATION        (0x20)
#define AURACAST_APP_OPCODE_RECV_LOGIN_PASSWORD_SET          (0x21)
#define AURACAST_APP_OPCODE_RECV_BROADCAST_SETTING           (0x22)
#define AURACAST_APP_OPCODE_RECV_DEVICE_RESET                (0x23)


#define AURACAST_APP_OPCODE_RECV_SCAN_CONTROL                (0x02)
#define AURACAST_APP_OPCODE_NOTIFY_SOURCE_LIST               (0x03)
#define AURACAST_APP_OPCODE_RECV_DEVICE_STATUS               (0x04)
#define AURACAST_APP_OPCODE_RECV_LISTENING_CONTROL           (0x05)
#define AURACAST_APP_OPCODE_NOTIFY_LISTENING_STATUS          (0x06)



extern void auracast_app_all_init(void);
extern void auracast_app_all_exit(void);

extern int auracast_app_packet_receive(u8 *data, u32 len);
extern int auracast_app_packet_response(u8 state, u8 opcode, u8 sn, u8 *payload, u32 payload_len);
extern int auracast_app_packet_cmd(u8 opcode, u8 flag, u8 *payload, u32 payload_len);

#endif


