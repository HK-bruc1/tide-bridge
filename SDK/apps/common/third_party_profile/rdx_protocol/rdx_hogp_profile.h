/*=====================================================================================
 HEADER NAME: rdx_hogp_profile.h
 MODULE NAME: RDX BLE HID-over-GATT keyboard profile constants.

 GENERAL DESCRIPTION:
    Single source of truth for HID Service handles, Report Map, HID Information,
    and Report Reference descriptors. Both the GATT aggregate table in
    rdx_ble_server.c and the HOGP ATT handlers in rdx_hogp_keyboard.c include
    this file.
 =======================================================================================*/

#ifndef _RDX_HOGP_PROFILE_H_
#define _RDX_HOGP_PROFILE_H_

#include "system/includes.h"
#include "rdx_hogp_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
* HID Service UUIDs and default values
******************************************************************************/
#define RDX_HOGP_UUID_HID_SERVICE                        0x1812
#define RDX_HOGP_UUID_PROTOCOL_MODE                      0x2A4E
#define RDX_HOGP_UUID_REPORT                             0x2A4D
#define RDX_HOGP_UUID_REPORT_MAP                         0x2A4B
#define RDX_HOGP_UUID_HID_INFORMATION                    0x2A4A
#define RDX_HOGP_UUID_HID_CONTROL_POINT                  0x2A4C
#define RDX_HOGP_UUID_REPORT_REFERENCE                   0x2908
#define RDX_HOGP_UUID_CLIENT_CHARACTERISTIC_CONFIGURATION 0x2902
#define RDX_HOGP_UUID_PRIMARY_SERVICE                    0x2800
#define RDX_HOGP_UUID_CHARACTERISTIC                     0x2803

#define RDX_HOGP_PROTOCOL_MODE_DEFAULT                   0x01

/******************************************************************************
* JL ATT attribute flags and characteristic properties used by Profile v1.
******************************************************************************/
#define RDX_HOGP_ATT_PROP_READ                           0x02
#define RDX_HOGP_ATT_PROP_WRITE_WITHOUT_RESPONSE         0x04
#define RDX_HOGP_ATT_PROP_WRITE                          0x08
#define RDX_HOGP_ATT_PROP_NOTIFY                         0x10

#define RDX_HOGP_ATT_FLAG_DYNAMIC                        0x0100

/* BTstack/JL ATT permission fields occupy two bits for reads (10..11) and
 * writes (12..13).  Security level 1 means an encrypted link without an
 * MITM claim, which matches the product's NoInputNoOutput Just Works policy. */
#define RDX_HOGP_ATT_SECURITY_ENCRYPTED                   0x0001
#define RDX_HOGP_ATT_READ_SECURITY_SHIFT                 10
#define RDX_HOGP_ATT_WRITE_SECURITY_SHIFT                12
#define RDX_HOGP_ATT_FLAG_READ_ENCRYPTED                 \
    (RDX_HOGP_ATT_SECURITY_ENCRYPTED << RDX_HOGP_ATT_READ_SECURITY_SHIFT)
#define RDX_HOGP_ATT_FLAG_WRITE_ENCRYPTED                \
    (RDX_HOGP_ATT_SECURITY_ENCRYPTED << RDX_HOGP_ATT_WRITE_SECURITY_SHIFT)

#if RDX_HOGP_ENCRYPTION_REQUIRED
#define RDX_HOGP_ATT_FLAG_ENCRYPTED_READ                 RDX_HOGP_ATT_FLAG_READ_ENCRYPTED
#define RDX_HOGP_ATT_FLAG_ENCRYPTED_WRITE                RDX_HOGP_ATT_FLAG_WRITE_ENCRYPTED
#else
#define RDX_HOGP_ATT_FLAG_ENCRYPTED_READ                 0
#define RDX_HOGP_ATT_FLAG_ENCRYPTED_WRITE                0
#endif

#define RDX_HOGP_CHAR_PROP_PROTOCOL_MODE                 (RDX_HOGP_ATT_PROP_READ | RDX_HOGP_ATT_PROP_WRITE_WITHOUT_RESPONSE)
#define RDX_HOGP_CHAR_PROP_INPUT_REPORT                  (RDX_HOGP_ATT_PROP_READ | RDX_HOGP_ATT_PROP_NOTIFY)
#define RDX_HOGP_CHAR_PROP_REPORT_MAP                    RDX_HOGP_ATT_PROP_READ
#define RDX_HOGP_CHAR_PROP_HID_INFORMATION               RDX_HOGP_ATT_PROP_READ
#define RDX_HOGP_CHAR_PROP_CONTROL_POINT                 RDX_HOGP_ATT_PROP_WRITE_WITHOUT_RESPONSE
#define RDX_HOGP_CHAR_PROP_OUTPUT_REPORT                 (RDX_HOGP_ATT_PROP_READ | RDX_HOGP_ATT_PROP_WRITE_WITHOUT_RESPONSE | RDX_HOGP_ATT_PROP_WRITE)

#define RDX_HOGP_ATT_FLAGS_PROTOCOL_MODE_VALUE           (RDX_HOGP_CHAR_PROP_PROTOCOL_MODE | RDX_HOGP_ATT_FLAG_DYNAMIC | RDX_HOGP_ATT_FLAG_ENCRYPTED_READ | RDX_HOGP_ATT_FLAG_ENCRYPTED_WRITE)
#define RDX_HOGP_ATT_FLAGS_INPUT_REPORT_VALUE            (RDX_HOGP_CHAR_PROP_INPUT_REPORT | RDX_HOGP_ATT_FLAG_DYNAMIC | RDX_HOGP_ATT_FLAG_ENCRYPTED_READ)
#define RDX_HOGP_ATT_FLAGS_REPORT_MAP_VALUE              (RDX_HOGP_CHAR_PROP_REPORT_MAP | RDX_HOGP_ATT_FLAG_DYNAMIC)
#define RDX_HOGP_ATT_FLAGS_HID_INFORMATION_VALUE         (RDX_HOGP_CHAR_PROP_HID_INFORMATION | RDX_HOGP_ATT_FLAG_DYNAMIC)
#define RDX_HOGP_ATT_FLAGS_CONTROL_POINT_VALUE           (RDX_HOGP_CHAR_PROP_CONTROL_POINT | RDX_HOGP_ATT_FLAG_DYNAMIC | RDX_HOGP_ATT_FLAG_ENCRYPTED_WRITE)
#define RDX_HOGP_ATT_FLAGS_OUTPUT_REPORT_VALUE           (RDX_HOGP_CHAR_PROP_OUTPUT_REPORT | RDX_HOGP_ATT_FLAG_DYNAMIC | RDX_HOGP_ATT_FLAG_ENCRYPTED_READ | RDX_HOGP_ATT_FLAG_ENCRYPTED_WRITE)
#define RDX_HOGP_ATT_FLAGS_INPUT_REPORT_CCC              (RDX_HOGP_ATT_PROP_READ | RDX_HOGP_ATT_PROP_WRITE | RDX_HOGP_ATT_FLAG_DYNAMIC | RDX_HOGP_ATT_FLAG_ENCRYPTED_READ | RDX_HOGP_ATT_FLAG_ENCRYPTED_WRITE)

#define RDX_HOGP_CCC_DEFAULT_VALUE                       0x0000
#define RDX_HOGP_OUTPUT_REPORT_DEFAULT_VALUE             0x00

/******************************************************************************
* HID Service handles (0x0016-0x0025)
******************************************************************************/
#define HID_SERVICE_HANDLE                                              0x0016
#define HID_PROTOCOL_MODE_CHARACTERISTIC_HANDLE                         0x0017
#define HID_PROTOCOL_MODE_VALUE_HANDLE                                  0x0018
#define HID_INPUT_REPORT_CHARACTERISTIC_HANDLE                          0x0019
#define HID_INPUT_REPORT_VALUE_HANDLE                                   0x001a
#define HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE                    0x001b
#define HID_INPUT_REPORT_REFERENCE_HANDLE                               0x001c
#define HID_REPORT_MAP_CHARACTERISTIC_HANDLE                            0x001d
#define HID_REPORT_MAP_VALUE_HANDLE                                     0x001e
#define HID_INFORMATION_CHARACTERISTIC_HANDLE                           0x001f
#define HID_INFORMATION_VALUE_HANDLE                                    0x0020
#define HID_CONTROL_POINT_CHARACTERISTIC_HANDLE                         0x0021
#define HID_CONTROL_POINT_VALUE_HANDLE                                  0x0022

#define HID_OUTPUT_REPORT_CHARACTERISTIC_HANDLE                         0x0023
#define HID_OUTPUT_REPORT_VALUE_HANDLE                                  0x0024
#define HID_OUTPUT_REPORT_REFERENCE_HANDLE                              0x0025

/* Handle-range helpers */
#define HID_SERVICE_START_HANDLE                                        HID_SERVICE_HANDLE
#define HID_SERVICE_END_HANDLE                                          HID_OUTPUT_REPORT_REFERENCE_HANDLE

/******************************************************************************
* Report Map (Standard 70-byte boot keyboard report descriptor)
******************************************************************************/
#define RDX_HOGP_REPORT_MAP_LEN  (70)

extern const u8 rdx_hogp_report_map[];

/******************************************************************************
* HID Information (bcdHID=1.11, country=0, flags=3)
******************************************************************************/
#define RDX_HOGP_HID_INFORMATION_LEN  (4)

extern const u8 rdx_hogp_hid_information[];

/******************************************************************************
* Report Reference descriptors
******************************************************************************/
#define RDX_HOGP_INPUT_REPORT_ID          0x01
#define RDX_HOGP_INPUT_REPORT_TYPE        0x01   /* Input */
#define RDX_HOGP_OUTPUT_REPORT_ID         0x01
#define RDX_HOGP_OUTPUT_REPORT_TYPE       0x02   /* Output */

/******************************************************************************
* ATT table byte helpers
*
* These helpers keep the HID section in rdx_profile_data[] tied to the profile
* constants above. The expanded bytes intentionally match the frozen Profile v1
* host contract.
******************************************************************************/
#define RDX_HOGP_ATT_U16_LE(value) \
    ((u8)((value) & 0xff)), ((u8)(((value) >> 8) & 0xff))

#define RDX_HOGP_ATT_HEADER(size, flags, handle, att_uuid) \
    RDX_HOGP_ATT_U16_LE(size), \
    RDX_HOGP_ATT_U16_LE(flags), \
    RDX_HOGP_ATT_U16_LE(handle), \
    RDX_HOGP_ATT_U16_LE(att_uuid)

#define RDX_HOGP_ATT_PRIMARY_SERVICE_16(handle, service_uuid) \
    RDX_HOGP_ATT_HEADER(0x000a, 0x0002, (handle), RDX_HOGP_UUID_PRIMARY_SERVICE), \
    RDX_HOGP_ATT_U16_LE(service_uuid)

#define RDX_HOGP_ATT_CHARACTERISTIC_16(handle, properties, value_handle, char_uuid) \
    RDX_HOGP_ATT_HEADER(0x000d, 0x0002, (handle), RDX_HOGP_UUID_CHARACTERISTIC), \
    ((u8)(properties)), \
    RDX_HOGP_ATT_U16_LE(value_handle), \
    RDX_HOGP_ATT_U16_LE(char_uuid)

#define RDX_HOGP_ATT_VALUE_16(handle, flags, value_uuid) \
    RDX_HOGP_ATT_HEADER(0x0008, (flags), (handle), (value_uuid))

#define RDX_HOGP_ATT_VALUE_16_U8(handle, flags, value_uuid, value) \
    RDX_HOGP_ATT_HEADER(0x0009, (flags), (handle), (value_uuid)), \
    ((u8)(value))

#define RDX_HOGP_ATT_CCC(handle, value) \
    RDX_HOGP_ATT_HEADER(0x000a, RDX_HOGP_ATT_FLAGS_INPUT_REPORT_CCC, (handle), RDX_HOGP_UUID_CLIENT_CHARACTERISTIC_CONFIGURATION), \
    RDX_HOGP_ATT_U16_LE(value)

#define RDX_HOGP_ATT_REPORT_REFERENCE(handle, report_id, report_type) \
    RDX_HOGP_ATT_HEADER(0x000a, 0x0002, (handle), RDX_HOGP_UUID_REPORT_REFERENCE), \
    ((u8)(report_id)), ((u8)(report_type))

#ifdef __cplusplus
}
#endif

#endif /* _RDX_HOGP_PROFILE_H_ */
