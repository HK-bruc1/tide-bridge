#ifndef RDX_PROTOCOL_H
#define RDX_PROTOCOL_H

/* Host mock replacement for rdx_protocol.h */
/* Defines only the minimum needed by rdx_command_dispatch.c tests. */

#include "typedef.h"

typedef enum {
    PROTOCOL_EVENT_CMD_RESERVED = 0,
    PROTOCOL_EVENT_CMD_DEVICE_PAIR,
    PROTOCOL_EVENT_CMD_DEVICE_UNPAIR,
    PROTOCOL_EVENT_CMD_SYS_RESET,
    PROTOCOL_EVENT_CMD_BOUND,
    PROTOCOL_EVENT_CMD_UNBOUND,
    PROTOCOL_EVENT_CMD_RECORD,
    PROTOCOL_EVENT_CMD_RECMARK,
    PROTOCOL_EVENT_CMD_RECORD_MODE_QUERY,
    PROTOCOL_EVENT_CMD_BT_NAME_QUERY,
    PROTOCOL_EVENT_CMD_BLE_NAME_QUERY,
    PROTOCOL_EVENT_CMD_SD_MEM_QUERY,
    PROTOCOL_EVENT_CMD_RTC,
    PROTOCOL_EVENT_CMD_FILE_DELETE,
    PROTOCOL_EVENT_CMD_BT_NAME_SET,
    PROTOCOL_EVENT_CMD_BLE_NAME_SET,
    PROTOCOL_EVENT_CMD_OFFTIME_SET,
    PROTOCOL_EVENT_CMD_AUDIO_STREAM,
    PROTOCOL_EVENT_CMD_FLASHNOTE,
    PROTOCOL_EVENT_CMD_BATTERY_QUERY,
    PROTOCOL_EVENT_CMD_INCHARGE_QUERY,
    PROTOCOL_EVENT_CMD_VERSION_QUERY,
    PROTOCOL_EVENT_CMD_AUTH_SN,
    PROTOCOL_EVENT_CMD_OFFTIME_QUERY,
    PROTOCOL_EVENT_CMD_OS_TYPE,
    PROTOCOL_EVENT_CMD_MIC_GAIN_QUERY,
    PROTOCOL_EVENT_CMD_MIC_GAIN_SET,
    PROTOCOL_EVENT_CMD_SD_FORMAT,
    PROTOCOL_EVENT_CMD_TYPE_MAX
} ProtocolEvents;

typedef struct {
    u32 timestamp;
} ProtocolRtcParams;

typedef struct {
    void (*battery_indicate)(u8 left, u8 right, u8 chargebox);
    void (*incharge_indicate)(u8 charge_state, u8 left, u8 right, u8 chargebox);
    void (*version_indicate)(char* hv, char* sv);
    void (*record_mode_indicate)(u8 scene, u8 run);
    void (*auth_sn_indicate)(void);
    void (*bt_name_check_ack_indicate)(u8 result, const char* name);
    void (*ble_name_check_ack_indicate)(u8 result, const char* name);
    void (*offtime_check_ack_indicate)(u8 result, u32 sec);
    void (*mic_gain_check_ack_indicate)(u8 result, int mode, int gain1, int gain2);
    void (*sd_mem_indicate)(u32 left, u32 total);
    void (*os_type_ack_indicate)(void);
    void (*volume_indicate)(u8 volume);
    void (*conn_state_indicate)(u8 state);
    void (*play_status_indicate)(u8 status);
    void (*bound_result_ack_indicate)(u8 result);
    void (*unbound_ack_indicate)(u8 result, u8 is_bound);
    void (*file_delete_ack_indicate)(u8 result, int file_sn, char* file_name);
    void (*bt_name_set_ack_indicate)(u8 result, char* bt_name);
    void (*ble_name_set_ack_indicate)(u8 result, char* ble_name);
    void (*offtime_set_ack_indicate)(u8 result, u16 timeout);
    void (*rtc_set_ack_indicate)(u8 result, u32 timestamp);
    void (*mic_gain_set_ack_indicate)(u8 result, int mode, int gain1, int gain2);
    void (*sd_format_ack_indicate)(u8 result);
    void (*sys_set_default_ack_indicate)(u8 result);
    int  (*audio_stream_play)(const void* p);
} RdxProtocolIndicateOps;

const RdxProtocolIndicateOps* rdx_protocol_get_indicate_ops(void);

#endif
