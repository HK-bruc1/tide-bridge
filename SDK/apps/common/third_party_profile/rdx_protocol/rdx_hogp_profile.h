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

#define RDX_HOGP_PROTOCOL_MODE_DEFAULT                   0x01

/******************************************************************************
* HID Service handles (0x0016-0x0022)
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

/* Handle-range helpers */
#define HID_SERVICE_START_HANDLE                                        HID_SERVICE_HANDLE
#define HID_SERVICE_END_HANDLE                                          HID_CONTROL_POINT_VALUE_HANDLE

/******************************************************************************
* Output Report (not restructured in Phase 1/2)
*
* Handle 0x0029 lives outside the HID Service range (0x0016-0x0022). It is
* intentionally NOT included in HID_SERVICE_END_HANDLE; write routing must
* handle it separately if it is ever forwarded to the HOGP module. In Phase 2
* it remains handled inline inside rdx_ble_server.c.
******************************************************************************/
#define HID_OUTPUT_REPORT_VALUE_HANDLE                                  0x0029

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

#ifdef __cplusplus
}
#endif

#endif /* _RDX_HOGP_PROFILE_H_ */
