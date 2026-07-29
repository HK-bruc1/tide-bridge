#ifndef _RDX_GATT_PROFILE_H_
#define _RDX_GATT_PROFILE_H_

#include "system/includes.h"
#include "rdx_hogp_config.h"

/* GAP, RDX private GATT, Battery and OTA handles. */
#define RDX_GATT_GAP_NAME_VALUE_HANDLE                         0x0003
#define RDX_GATT_COMMAND_VALUE_HANDLE                          0x0006
#define RDX_GATT_NOTIFY_VALUE_HANDLE                           0x0008
#define RDX_GATT_NOTIFY_CCC_HANDLE                             0x0009
#define RDX_GATT_READ_VALUE_HANDLE                             0x000b
#define RDX_GATT_BATTERY_VALUE_HANDLE                          0x000e
#define RDX_GATT_BATTERY_CCC_HANDLE                            0x000f
#define RDX_GATT_OTA_COMMAND_VALUE_HANDLE                      0x0012
#define RDX_GATT_OTA_NOTIFY_VALUE_HANDLE                       0x0014
#define RDX_GATT_OTA_NOTIFY_CCC_HANDLE                         0x0015

/* HID Service handles. The aggregate profile owns the complete ATT layout. */
#define HID_SERVICE_HANDLE                                     0x0016
#define HID_PROTOCOL_MODE_CHARACTERISTIC_HANDLE                0x0017
#define HID_PROTOCOL_MODE_VALUE_HANDLE                         0x0018
#define HID_INPUT_REPORT_CHARACTERISTIC_HANDLE                 0x0019
#define HID_INPUT_REPORT_VALUE_HANDLE                          0x001a
#define HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE           0x001b
#define HID_INPUT_REPORT_REFERENCE_HANDLE                      0x001c
#define HID_REPORT_MAP_CHARACTERISTIC_HANDLE                   0x001d
#define HID_REPORT_MAP_VALUE_HANDLE                            0x001e
#define HID_INFORMATION_CHARACTERISTIC_HANDLE                  0x001f
#define HID_INFORMATION_VALUE_HANDLE                           0x0020
#define HID_CONTROL_POINT_CHARACTERISTIC_HANDLE                0x0021
#define HID_CONTROL_POINT_VALUE_HANDLE                         0x0022
#define HID_OUTPUT_REPORT_CHARACTERISTIC_HANDLE                0x0023
#define HID_OUTPUT_REPORT_VALUE_HANDLE                         0x0024
#define HID_OUTPUT_REPORT_REFERENCE_HANDLE                     0x0025

/* Codex Micro Report ID 6 attributes. These are present only in V1/C1. */
#define HID_CODEX_INPUT_REPORT_CHARACTERISTIC_HANDLE           0x0026
#define HID_CODEX_INPUT_REPORT_VALUE_HANDLE                    0x0027
#define HID_CODEX_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE     0x0028
#define HID_CODEX_INPUT_REPORT_REFERENCE_HANDLE                0x0029
#define HID_CODEX_OUTPUT_REPORT_CHARACTERISTIC_HANDLE          0x002a
#define HID_CODEX_OUTPUT_REPORT_VALUE_HANDLE                   0x002b
#define HID_CODEX_OUTPUT_REPORT_REFERENCE_HANDLE               0x002c

#define HID_SERVICE_START_HANDLE                               HID_SERVICE_HANDLE
#if TCFG_RDX_CODEX_MICRO_MODE
#define HID_SERVICE_END_HANDLE                                 HID_CODEX_OUTPUT_REPORT_REFERENCE_HANDLE
#else
#define HID_SERVICE_END_HANDLE                                 HID_OUTPUT_REPORT_REFERENCE_HANDLE
#endif

/* DIS follows the selected immutable HID personality. */
#if TCFG_RDX_CODEX_MICRO_MODE
#define DIS_SERVICE_HANDLE                                    0x002d
#define DIS_PNP_ID_CHARACTERISTIC_HANDLE                      0x002e
#define DIS_PNP_ID_VALUE_HANDLE                               0x002f
#define DIS_MANUFACTURER_NAME_CHARACTERISTIC_HANDLE           0x0030
#define DIS_MANUFACTURER_NAME_VALUE_HANDLE                    0x0031
#else
#define DIS_SERVICE_HANDLE                                    0x0026
#define DIS_PNP_ID_CHARACTERISTIC_HANDLE                      0x0027
#define DIS_PNP_ID_VALUE_HANDLE                               0x0028
#define DIS_MANUFACTURER_NAME_CHARACTERISTIC_HANDLE           0x0029
#define DIS_MANUFACTURER_NAME_VALUE_HANDLE                    0x002a
#endif

typedef struct {
    void *ble_hdl;
    u16 connection_handle;
    u16 att_handle;
    u16 offset;
    u8 *buffer;
    u16 buffer_size;
} rdx_gatt_read_context_t;

typedef struct {
    void *ble_hdl;
    u16 connection_handle;
    u16 att_handle;
    u16 transaction_mode;
    u16 offset;
    u8 *buffer;
    u16 buffer_size;
} rdx_gatt_write_context_t;

typedef u16 (*rdx_gatt_read_provider_t)(
    const rdx_gatt_read_context_t *context);
typedef int (*rdx_gatt_write_provider_t)(
    const rdx_gatt_write_context_t *context);

typedef struct {
    rdx_gatt_read_provider_t read_gap_name;
    rdx_gatt_read_provider_t read_private_value;
    rdx_gatt_read_provider_t read_battery;
    rdx_gatt_read_provider_t read_hid;
    rdx_gatt_write_provider_t write_battery_ccc;
    rdx_gatt_write_provider_t write_private;
    rdx_gatt_write_provider_t write_hid;
    rdx_gatt_write_provider_t write_rejected;
} rdx_gatt_profile_ops_t;

extern const u8 rdx_profile_data[];

u16 rdx_gatt_profile_dispatch_read(
    const rdx_gatt_read_context_t *context,
    const rdx_gatt_profile_ops_t *ops);
int rdx_gatt_profile_dispatch_write(
    const rdx_gatt_write_context_t *context,
    const rdx_gatt_profile_ops_t *ops);
u8 rdx_gatt_profile_is_rdx_capability_handle(u16 att_handle);

#endif /* _RDX_GATT_PROFILE_H_ */
