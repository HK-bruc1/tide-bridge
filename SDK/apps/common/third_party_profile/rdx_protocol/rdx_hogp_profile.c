/*=====================================================================================
 HEADER NAME: rdx_hogp_profile.c
 MODULE NAME: RDX BLE HID-over-GATT keyboard profile data.

 GENERAL DESCRIPTION:
    Single definition site for profile byte arrays declared in
    rdx_hogp_profile.h. Keeping the definitions in one .c file avoids duplicate
    storage when the header is included by multiple translation units.
 =======================================================================================*/

#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".rdx_hogp_profile.data.bss")
#pragma data_seg(".rdx_hogp_profile.data")
#pragma const_seg(".rdx_hogp_profile.text.const")
#pragma code_seg(".rdx_hogp_profile.text")
#endif

#include "app_config.h"
#include "rdx_hogp_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

#if (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN)

/******************************************************************************
* Report Map (Standard 70-byte boot keyboard report descriptor)
******************************************************************************/
const u8 rdx_hogp_report_map[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x05, 0x07,        //   Usage Page (Key Codes)
    0x19, 0xE0,        //   Usage Minimum (224)
    0x29, 0xE7,        //   Usage Maximum (231)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x08,        //   Report Count (8)
    0x81, 0x02,        //   Input (Data, Variable, Absolute)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x01,        //   Input (Constant)
    0x95, 0x06,        //   Report Count (6)
    0x75, 0x08,        //   Report Size (8)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x05, 0x07,        //   Usage Page (Key Codes)
    0x19, 0x00,        //   Usage Minimum (0)
    0x29, 0xFF,        //   Usage Maximum (255)
    0x81, 0x00,        //   Input (Data, Array)
    0x05, 0x08,        //   Usage Page (LEDs)
    0x19, 0x01,        //   Usage Minimum (Num Lock)
    0x29, 0x03,        //   Usage Maximum (Scroll Lock)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x95, 0x03,        //   Report Count (3)
    0x75, 0x01,        //   Report Size (1)
    0x91, 0x02,        //   Output (Data, Variable, Absolute)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x05,        //   Report Size (5)
    0x91, 0x01,        //   Output (Constant)
    0xC0               // End Collection
};

typedef char rdx_hogp_report_map_len_check[
    (sizeof(rdx_hogp_report_map) == RDX_HOGP_REPORT_MAP_LEN) ? 1 : -1
];

/******************************************************************************
* HID Information (bcdHID=1.11, country=0, flags=3)
******************************************************************************/
const u8 rdx_hogp_hid_information[] = {
    0x11, 0x01, 0x00, 0x03
};

typedef char rdx_hogp_hid_information_len_check[
    (sizeof(rdx_hogp_hid_information) == RDX_HOGP_HID_INFORMATION_LEN) ? 1 : -1
];

#endif /* (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN) */

#ifdef __cplusplus
}
#endif
