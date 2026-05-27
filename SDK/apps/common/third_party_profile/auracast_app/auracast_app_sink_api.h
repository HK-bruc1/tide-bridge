#ifndef _AURACAST_APP_SINK_API_H_
#define _AURACAST_APP_SINK_API_H_

#include "system/includes.h"

struct auracst_source_item_t {
    char broadcast_name[32];
    u8   broadcast_id[3];
    u8   broadcast_features;
    u8   adv_address[6];
    u8   listening_state;   // 0:idle  1:to listen  2:listenig
    u8   listening_state_error;
};

extern int auracast_app_notify_source_list(struct auracst_source_item_t *src);
extern int auracast_app_recv_scan_control_deal(u8 opcode, u8 sn, u8 *payload, u32 payload_len);
extern int auracast_app_recv_device_status_deal(u8 opcode, u8 sn, u8 *payload, u32 payload_len);
extern int auracast_app_recv_listening_control_deal(u8 opcode, u8 sn, u8 *payload, u32 payload_len);
extern int auracast_app_notify_listening_status(u8 status, u8 error);

#endif

