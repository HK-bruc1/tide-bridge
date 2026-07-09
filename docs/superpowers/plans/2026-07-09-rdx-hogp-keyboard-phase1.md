# RDX HOGP Keyboard Phase 1 — Code Relocation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move the HOGP keyboard state, ATT handlers, SM Just-Works handling, advertising construction, and key-send logic out of `rdx_ble_server.c` into a new `rdx_hogp_keyboard.c/h` submodule, while keeping the observable BLE behavior byte-identical to the current MVP.

**Architecture:** HOGP remains a submodule of the RDX GATT Server. `rdx_hogp_keyboard.c` owns all HOGP state and behavior; `rdx_ble_server.c` is reduced to BLE lifecycle, the aggregate `rdx_profile_data[]` table, and thin forwarding calls. Legacy `hogp_*` entry points are preserved as wrappers during this phase so `rdx_app.c` does not change yet.

**Tech Stack:** C (JL AC701N / BR28 TWS firmware), BTstack GATT/ATT APIs, `app_ble_*` wrapper, `y_printf` logging, `sys_timeout_add`, GNU make.

## Global Constraints

- `config_le_gatt_server_num` remains `1`; HOGP does not allocate its own BLE Server handle.
- GATT handle layout `0x0016`–`0x0022` for HID Service must remain byte-for-byte identical.
- Report Map bytes and length must remain byte-for-byte identical.
- Input Report payload remains 8 bytes: `[modifier, reserved, key1..key6]`.
- Advertising payload remains: Flags `0x06`, Service UUID `0x1812`, Appearance `0x03C1`, same name source.
- HOGP and RDX advertising remain mutually exclusive.
- Pairing flow remains: auto pairing request + Just Works confirm on HOGP connection.
- Phase 1 does **not** create `rdx_hogp_profile.h` or `rdx_hogp_config.h`; those are Phase 2 and Phase 3.
- Phase 1 does **not** restructure Output Report (`0x0028`–`0x002a`).
- `rdx_app.c` is **not** modified in Phase 1; legacy `hogp_*` symbols must remain available.

---

## File Map

| File | Responsibility after Phase 1 |
|---|---|
| `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_keyboard.h` | Public HOGP API consumed by `rdx_ble_server.c` and `rdx_app.c`. |
| `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_keyboard.c` | HOGP state, ATT read/write, SM Just-Works, advertising, key sending. |
| `SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_server.c` | RDX BLE Server lifecycle, profile data aggregate table, forwarding to HOGP. |
| `SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_server.h` | Includes `rdx_hogp_keyboard.h`; keeps legacy `hogp_*` declarations as thin wrappers. |
| `SDK/apps/earphone/include/app_config.h` / `t2620_project_config.h` | `TCFG_RDX_HOGP_ENABLE` is **not** added yet; defer to Phase 3. |
| `SDK/Makefile` | Adds `rdx_hogp_keyboard.c` to the source list. |

---

### Task 1: Scaffold `rdx_hogp_keyboard.h` with the public API

**Files:**
- Create: `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_keyboard.h`
- Modify: none

**Interfaces:**
- Consumes: nothing yet.
- Produces: declarations for all HOGP functions that `rdx_ble_server.c` will call.

- [ ] **Step 1.1: Create header with include guards and standard includes**

```c
#ifndef _RDX_HOGP_KEYBOARD_H_
#define _RDX_HOGP_KEYBOARD_H_

#include "system/includes.h"
#include "ble_user.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Lifecycle */
void rdx_hogp_init(void *app_ble_hdl);
void rdx_hogp_deinit(void);

/* Mode */
u8   rdx_hogp_mode_get(void);
void rdx_hogp_mode_set(u8 enable);

/* ATT routing */
u8   rdx_hogp_is_handle(u16 att_handle);

u16  rdx_hogp_att_read(hci_con_handle_t connection_handle,
                        u16 att_handle,
                        u16 offset,
                        u8 *buffer,
                        u16 buffer_size);

int  rdx_hogp_att_write(hci_con_handle_t connection_handle,
                         u16 att_handle,
                         u16 transaction_mode,
                         u16 offset,
                         u8 *buffer,
                         u16 buffer_size);

/* Connection / security events */
void rdx_hogp_on_connected(u16 con_handle);
void rdx_hogp_on_disconnected(u16 con_handle);
void rdx_hogp_on_encryption_change(u16 con_handle, u8 enabled, u8 status);
void rdx_hogp_on_sm_event(u8 packet_type, u8 *packet, u16 size);

/* Advertising */
int  rdx_hogp_fill_adv_data(u8 *adv_data, u8 max_len);
void rdx_hogp_adv_start(void);
void rdx_hogp_adv_stop(void);

/* Key input */
int  rdx_hogp_key_send_usage(u8 usage, u8 pressed);
int  rdx_hogp_key_click_usage(u8 usage);
int  rdx_hogp_key_click_index(u8 key_index);
int  rdx_hogp_on_io_num_key(u8 num_idx, u8 action);

/* Legacy compatibility wrappers (remove in later phase once rdx_app.c migrates) */
void hogp_mode_set(u8 enable);
u8   hogp_mode_get(void);
void hogp_key_send(u8 key_index, u8 pressed);
void hogp_key_click_send(u8 key_index);

#ifdef __cplusplus
}
#endif

#endif /* _RDX_HOGP_KEYBOARD_H_ */
```

- [ ] **Step 1.2: Verify header compiles standalone**

Run: `cd SDK && make -j1 2>&1 | head -n 200`
Expected: Build proceeds until it hits missing `rdx_hogp_keyboard.c` symbols (acceptable at this point).

---

### Task 2: Create `rdx_hogp_keyboard.c` with private HOGP state and helpers

**Files:**
- Create: `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_keyboard.c`
- Modify: none

**Interfaces:**
- Consumes: `app_ble_*` APIs, `sm_*` APIs, `sys_timeout_add`, `make_eir_packet_*`, `rdx_ble_server_get_local_name()`, `rdx_ble_server_adv_enable()`, `multi_att_*` CCC helpers.
- Produces: internal state + all API functions declared in Task 1.

- [ ] **Step 2.1: Add file header, section pragmas, and includes**

```c
/*=====================================================================================
 HEADER NAME: rdx_hogp_keyboard.c
 MODULE NAME: RDX BLE HID-over-GATT keyboard submodule.
 ======================================================================================*/

#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".rdx_hogp_keyboard.data.bss")
#pragma data_seg(".rdx_hogp_keyboard.data")
#pragma const_seg(".rdx_hogp_keyboard.text.const")
#pragma code_seg(".rdx_hogp_keyboard.text")
#endif

#include "sdk_config.h"
#include "app_config.h"
#include "system/includes.h"
#include "ble_user.h"
#include "btstack/le/sm.h"
#include "btstack/le/le_user.h"
#include "btstack/btstack_event.h"
#include "multi_protocol_main.h"

#include "rdx_hogp_keyboard.h"
#include "rdx_ble_server.h"
#include "rdx_app.h"
#include "rdx_util.h"
#include "rdx_commonDef.h"

#if (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN)

#define LOG_TAG                                     "[rdx_hogp]"
#define LOG_ERROR_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_INFO_ENABLE
/* #define LOG_DUMP_ENABLE */
#define LOG_CLI_ENABLE
#include "debug.h"
```

- [ ] **Step 2.2: Add temporary private HID handle macros (duplicated from rdx_ble_server.c; removed in Phase 2)**

```c
/* Temporary handle macros — will be centralized in rdx_hogp_profile.h in Phase 2 */
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
#define HID_OUTPUT_REPORT_VALUE_HANDLE                                  0x0029
```

- [ ] **Step 2.3: Add module state variables (moved from rdx_ble_server.c)**

```c
/*=====================================================================================
 * Local variables
 *=====================================================================================*/
static void *s_hogp_app_ble_hdl = NULL;
static volatile u8 s_hogp_mode = 0;
static volatile u8 s_hogp_connected = 0;
static volatile u8 s_hid_notify_enabled = 0;
static volatile u8 s_hogp_encrypted = 0;
static u16 s_hid_con_handle = 0;
static u8 s_hid_input_report[8] = {0};

/* Default 5-key keymap (A, B, C, D, E) — will become configurable in Phase 3 */
static const u8 key_to_hid_usage[5] = {
    0x04,  // A
    0x05,  // B
    0x06,  // C
    0x07,  // D
    0x08,  // E
};
```

- [ ] **Step 2.4: Add Report Map, HID Information, and Protocol Mode constants**

```c
static const u8 hid_report_map[] = {
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

static const u8 hid_information[] = {0x11, 0x01, 0x00, 0x03};
static u8 hid_protocol_mode = 1;  // Report Protocol
```

- [ ] **Step 2.5: Add read helper**

```c
static uint16_t hid_read_helper(const u8 *data, u16 data_len,
                                u16 offset, u8 *buffer,
                                u16 buffer_size)
{
    if (offset >= data_len) {
        return 0;
    }
    uint16_t len = data_len - offset;
    if (buffer) {
        if (len > buffer_size) {
            len = buffer_size;
        }
        memcpy(buffer, data + offset, len);
    }
    return len;
}
```

- [ ] **Step 2.6: Build once to catch syntax errors**

Run: `cd SDK && make -j1 2>&1 | tail -n 100`
Expected: Failures only due to missing functions from later steps.

---

### Task 3: Implement HOGP ATT read/write handlers

**Files:**
- Modify: `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_keyboard.c`

**Interfaces:**
- Consumes: module state from Task 2.
- Produces: `rdx_hogp_att_read()`, `rdx_hogp_att_write()`.

- [ ] **Step 3.1: Implement `rdx_hogp_att_read()`**

```c
u16 rdx_hogp_att_read(hci_con_handle_t connection_handle,
                      u16 att_handle,
                      u16 offset,
                      u8 *buffer,
                      u16 buffer_size)
{
    (void)connection_handle;

    switch (att_handle) {
    case HID_PROTOCOL_MODE_VALUE_HANDLE:
        return hid_read_helper(&hid_protocol_mode, 1, offset, buffer, buffer_size);
    case HID_REPORT_MAP_VALUE_HANDLE:
        return hid_read_helper(hid_report_map, sizeof(hid_report_map), offset, buffer, buffer_size);
    case HID_INFORMATION_VALUE_HANDLE:
        return hid_read_helper(hid_information, sizeof(hid_information), offset, buffer, buffer_size);
    case HID_INPUT_REPORT_VALUE_HANDLE:
        return hid_read_helper(s_hid_input_report, sizeof(s_hid_input_report), offset, buffer, buffer_size);
    default:
        return 0;
    }
}
```

- [ ] **Step 3.2: Implement `rdx_hogp_att_write()`**

```c
int rdx_hogp_att_write(hci_con_handle_t connection_handle,
                        u16 att_handle,
                        u16 transaction_mode,
                        u16 offset,
                        u8 *buffer,
                        u16 buffer_size)
{
    (void)transaction_mode;
    (void)offset;

    switch (att_handle) {
    case HID_CONTROL_POINT_VALUE_HANDLE:
        if (buffer_size >= 1) {
            y_printf("[HOGP] ctrl point hdl=0x%04x val=0x%02x\r", att_handle, buffer[0]);
        }
        return 0;
    case HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE:
        if (buffer_size >= 2) {
            u16 cfg = buffer[0] | (buffer[1] << 8);
            s_hid_notify_enabled = (cfg & 0x01);
            multi_att_set_ccc_config(connection_handle, att_handle, cfg);
            y_printf("[HOGP] CCC write hdl=0x%04x cfg=0x%04x notify=%d\r", att_handle, cfg, s_hid_notify_enabled);
        }
        return 0;
    case HID_INPUT_REPORT_VALUE_HANDLE:
        y_printf("[HOGP] input report write hdl=0x%04x len=%d data[0]=0x%02x\r",
                 att_handle, buffer_size, buffer_size ? buffer[0] : 0);
        return 0;
    case HID_OUTPUT_REPORT_VALUE_HANDLE:
        if (buffer_size >= 1) {
            y_printf("[HOGP] output report write, LED=0x%02x\r", buffer[0]);
        }
        return 0;
    default:
        y_printf("[HOGP] write default hdl=0x%04x len=%d data[0]=0x%02x\r",
                 att_handle, buffer_size, buffer_size ? buffer[0] : 0);
        return 0;
    }
}
```

- [ ] **Step 3.3: Implement `rdx_hogp_is_handle()`**

```c
u8 rdx_hogp_is_handle(u16 att_handle)
{
    return (att_handle >= HID_SERVICE_HANDLE && att_handle <= HID_CONTROL_POINT_VALUE_HANDLE) ? 1 : 0;
}
```

- [ ] **Step 3.4: Build check**

Run: `cd SDK && make -j1 2>&1 | tail -n 100`
Expected: Link errors for missing functions from Tasks 4–6, no compile errors.

---

### Task 4: Implement mode control, advertising, and connection/security events

**Files:**
- Modify: `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_keyboard.c`
- Modify: `SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_server.c` (forwarding calls)

**Interfaces:**
- Consumes: `s_hogp_app_ble_hdl`, `s_hogp_mode`, `app_ble_*`, `rdx_ble_server_get_local_name()`, `rdx_ble_server_adv_enable()`.
- Produces: `rdx_hogp_init()`, `rdx_hogp_deinit()`, `rdx_hogp_mode_get()`, `rdx_hogp_mode_set()`, `rdx_hogp_fill_adv_data()`, `rdx_hogp_adv_start()`, `rdx_hogp_adv_stop()`, `rdx_hogp_on_connected()`, `rdx_hogp_on_disconnected()`, `rdx_hogp_on_encryption_change()`, `rdx_hogp_on_sm_event()`.

- [ ] **Step 4.1: Add forward references to RDX helpers used by HOGP**

```c
extern char *rdx_ble_server_get_local_name(void);
extern int rdx_ble_server_adv_enable(u8 enable);
extern void rdx_ble_server_app_disconnect(void);
extern rdx_ble_server_info_t *rdx_ble_server_get_info(void);
```

- [ ] **Step 4.2: Implement `rdx_hogp_init()` / `rdx_hogp_deinit()`**

```c
void rdx_hogp_init(void *app_ble_hdl)
{
    s_hogp_app_ble_hdl = app_ble_hdl;
    s_hogp_mode = 0;
    s_hogp_connected = 0;
    s_hid_notify_enabled = 0;
    s_hogp_encrypted = 0;
    s_hid_con_handle = 0;
    memset((void *)s_hid_input_report, 0, sizeof(s_hid_input_report));
}

void rdx_hogp_deinit(void)
{
    rdx_hogp_mode_set(0);
    s_hogp_app_ble_hdl = NULL;
}
```

- [ ] **Step 4.3: Implement advertising payload and control**

```c
int rdx_hogp_fill_adv_data(u8 *adv_data, u8 max_len)
{
    u8 offset = 0;

    offset += make_eir_packet_val(&adv_data[offset], offset,
                                  HCI_EIR_DATATYPE_FLAGS, 0x06, 1);

    u8 hid_uuid[] = {0x12, 0x18};
    offset += make_eir_packet_data(&adv_data[offset], offset,
                                   HCI_EIR_DATATYPE_COMPLETE_16BIT_SERVICE_UUIDS,
                                   hid_uuid, sizeof(hid_uuid));

    offset += make_eir_packet_val(&adv_data[offset], offset,
                                  HCI_EIR_DATATYPE_APPEARANCE_DATA, 0x03C1, 2);

    const char *name = rdx_ble_server_get_local_name();
    u8 name_len = (u8)strlen(name);
    if (name_len > max_len - offset - 2) {
        name_len = max_len - offset - 2;
    }
    offset += make_eir_packet_data(&adv_data[offset], offset,
                                   HCI_EIR_DATATYPE_COMPLETE_LOCAL_NAME,
                                   (u8 *)name, name_len);

    (void)max_len; /* safety clamp above uses it */
    return offset;
}

static void hogp_adv_start_internal(void)
{
    u8 advData[ADV_RSP_PACKET_MAX];
    u8 len;

    if (s_hogp_app_ble_hdl == NULL) {
        return;
    }

    app_ble_adv_enable(s_hogp_app_ble_hdl, 0);
    app_ble_rsp_data_set(s_hogp_app_ble_hdl, NULL, 0);

    len = rdx_hogp_fill_adv_data(advData, sizeof(advData));
    app_ble_set_adv_param(s_hogp_app_ble_hdl,
                          rdx_ble_server_get_info()->adv_interval_min,
                          APP_ADV_IND, APP_ADV_CHANNEL_ALL);
    app_ble_adv_data_set(s_hogp_app_ble_hdl, advData, len);
    app_ble_adv_enable(s_hogp_app_ble_hdl, 1);

    y_printf("[HOGP] HID advertising started\r");
}

static void hogp_adv_stop_internal(void)
{
    if (s_hogp_app_ble_hdl == NULL) {
        return;
    }

    app_ble_adv_enable(s_hogp_app_ble_hdl, 0);
    rdx_ble_server_adv_enable(1);

    y_printf("[HOGP] HID advertising stopped, restore RDX advertising\r");
}

void rdx_hogp_adv_start(void)
{
    if (s_hogp_mode) {
        hogp_adv_start_internal();
    }
}

void rdx_hogp_adv_stop(void)
{
    hogp_adv_stop_internal();
}
```

- [ ] **Step 4.4: Implement mode get/set**

```c
u8 rdx_hogp_mode_get(void)
{
    return s_hogp_mode;
}

void rdx_hogp_mode_set(u8 enable)
{
    u8 new_mode = enable ? 1 : 0;

    if (new_mode == s_hogp_mode) {
        return;
    }

    if (rdx_ble_server_get_info()->ble_conn) {
        y_printf("[HOGP] active ble conn, disconnect before mode switch\r");
        rdx_ble_server_app_disconnect();
    }

    if (new_mode) {
        s_hogp_mode = 1;
        hogp_adv_start_internal();
    } else {
        hogp_adv_stop_internal();
        s_hogp_mode = 0;
        s_hogp_connected = 0;
        s_hid_con_handle = 0;
        s_hid_notify_enabled = 0;
        s_hogp_encrypted = 0;
    }
}
```

- [ ] **Step 4.5: Implement connection / encryption / SM events**

```c
void rdx_hogp_on_connected(u16 con_handle)
{
    if (!s_hogp_mode) {
        return;
    }
    s_hogp_connected = 1;
    s_hid_con_handle = con_handle;
    s_hogp_encrypted = 0;
    y_printf("[HOGP] conn complete hdl=0x%04x\r", con_handle);
    sm_api_request_pairing(con_handle);
}

void rdx_hogp_on_disconnected(u16 con_handle)
{
    (void)con_handle;
    if (!s_hogp_mode) {
        return;
    }
    s_hogp_connected = 0;
    s_hid_con_handle = 0;
    s_hid_notify_enabled = 0;
    s_hogp_encrypted = 0;
    y_printf("[HOGP] disconnect\r");
}

void rdx_hogp_on_encryption_change(u16 con_handle, u8 enabled, u8 status)
{
    y_printf("[HOGP] encryption_change hdl=0x%04x enabled=%d status=%d\r",
             con_handle, enabled, status);
    if (s_hogp_mode && con_handle == s_hid_con_handle && enabled && status == 0) {
        s_hogp_encrypted = 1;
        y_printf("[HOGP] link encrypted\r");
    }
}

void rdx_hogp_on_sm_event(u8 packet_type, u8 *packet, u16 size)
{
    (void)size;

    if (packet_type != HCI_EVENT_PACKET) {
        return;
    }

    switch (hci_event_packet_get_type(packet)) {
    case SM_EVENT_JUST_WORKS_REQUEST:
        if (s_hogp_mode) {
            y_printf("[HOGP] Just Works pairing request, confirm\r");
            sm_just_works_confirm(sm_event_just_works_request_get_handle(packet));
        }
        break;
    default:
        break;
    }
}
```

- [ ] **Step 4.6: Build check**

Run: `cd SDK && make -j1 2>&1 | tail -n 100`
Expected: Only link errors from key-send functions and Makefile omission.

---

### Task 5: Implement key-send functions and legacy wrappers

**Files:**
- Modify: `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_keyboard.c`

**Interfaces:**
- Consumes: module state, keymap, `app_ble_att_send_data()`.
- Produces: `rdx_hogp_key_send_usage()`, `rdx_hogp_key_click_usage()`, `rdx_hogp_key_click_index()`, `rdx_hogp_on_io_num_key()`, plus legacy `hogp_key_send()` / `hogp_key_click_send()`.

- [ ] **Step 5.1: Implement key-send core**

```c
int rdx_hogp_key_send_usage(u8 usage, u8 pressed)
{
    u8 report[8] = {0};

    if (pressed) {
        report[2] = usage;
    }

    y_printf("[HOGP] key_send usage=0x%02x pressed=%d report=%02x %02x %02x %02x %02x %02x %02x %02x conn=%d notify=%d encrypted=%d\r",
             usage, pressed,
             report[0], report[1], report[2], report[3],
             report[4], report[5], report[6], report[7],
             s_hogp_connected, s_hid_notify_enabled, s_hogp_encrypted);

    if (!s_hogp_connected) {
        y_printf("[HOGP] key_send skipped: not connected\r");
        return -1;
    }
    if (s_hogp_app_ble_hdl == NULL) {
        y_printf("[HOGP] key_send skipped: server hdl NULL\r");
        return -1;
    }
    if (!s_hid_notify_enabled) {
        y_printf("[HOGP] key_send skipped: notify not enabled\r");
        return -1;
    }
    if (!s_hogp_encrypted) {
        y_printf("[HOGP] key_send skipped: not encrypted\r");
        return -1;
    }

    int ret = app_ble_att_send_data(s_hogp_app_ble_hdl,
                                    HID_INPUT_REPORT_VALUE_HANDLE,
                                    report, sizeof(report),
                                    ATT_OP_NOTIFY);
    y_printf("[HOGP] key_send ret=%d\r", ret);
    return ret;
}

static void hogp_key_up_timeout(void *priv)
{
    u8 usage = (u8)(u32)priv;
    rdx_hogp_key_send_usage(usage, 0);
}

int rdx_hogp_key_click_usage(u8 usage)
{
    int ret = rdx_hogp_key_send_usage(usage, 1);
    sys_timeout_add((void *)(u32)usage, hogp_key_up_timeout, 20);
    return ret;
}

int rdx_hogp_key_click_index(u8 key_index)
{
    if (key_index >= 5) {
        return -1;
    }
    return rdx_hogp_key_click_usage(key_to_hid_usage[key_index]);
}
```

- [ ] **Step 5.2: Implement `rdx_hogp_on_io_num_key()` (used starting Phase 3, but safe to add now)**

```c
int rdx_hogp_on_io_num_key(u8 num_idx, u8 action)
{
    if (!s_hogp_mode) {
        if (num_idx == 0 && action == KEY_ACTION_CLICK) {
            rdx_hogp_mode_set(1);
            y_printf("[HOGP] enter HOGP mode\r");
            return 0;
        }
        return -1;
    }

    if (num_idx == 0) {
        if (action == KEY_ACTION_LONG) {
            rdx_hogp_mode_set(0);
            y_printf("[HOGP] exit HOGP mode\r");
            return 0;
        }
        return -1;
    }

    if (action == KEY_ACTION_CLICK) {
        return rdx_hogp_key_click_index(num_idx - 1);
    }

    return -1;
}
```

- [ ] **Step 5.3: Implement legacy wrappers for backward compatibility**

```c
void hogp_mode_set(u8 enable)
{
    rdx_hogp_mode_set(enable);
}

u8 hogp_mode_get(void)
{
    return rdx_hogp_mode_get();
}

void hogp_key_send(u8 key_index, u8 pressed)
{
    if (key_index >= 5) {
        y_printf("[HOGP] err: key_index %d out of range\r", key_index);
        return;
    }
    rdx_hogp_key_send_usage(key_to_hid_usage[key_index], pressed);
}

void hogp_key_click_send(u8 key_index)
{
    if (key_index >= 5) {
        return;
    }
    rdx_hogp_key_click_index(key_index);
}
```

- [ ] **Step 5.4: Add closing `#endif` and build check**

```c
#endif /* (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN) */
```

Run: `cd SDK && make -j1 2>&1 | tail -n 100`
Expected: Only link errors because Makefile has not yet added the new file.

---

### Task 6: Update `rdx_ble_server.c` to forward to the HOGP module

**Files:**
- Modify: `SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_server.c`

**Interfaces:**
- Consumes: `rdx_hogp_keyboard.h` API.
- Produces: thin forwarding calls; removal of HOGP state and internals from `rdx_ble_server.c`.

- [ ] **Step 6.1: Include the new header**

Add after `#include "rdx_ble_server.h"`:

```c
#include "rdx_hogp_keyboard.h"
```

- [ ] **Step 6.2: Remove HOGP state and internal variables from `rdx_ble_server.c`**

Delete from `rdx_ble_server.c` the following block (currently around lines 155–206):

```c
/*=====================================================================================
 * HOGP (HID over GATT Profile) extension
 *=====================================================================================*/
static volatile u8 hogp_mode = 0;
static volatile u8 hogp_connected = 0;
static volatile u8 hid_notify_enabled = 0;
static volatile u8 hogp_encrypted = 0;
static u16 hid_con_handle = 0;

// Standard 8-byte boot keyboard Report Map
static const u8 hid_report_map[] = { ... };

static const u8 hid_information[] = {0x11, 0x01, 0x00, 0x03};
static u8 hid_protocol_mode = 1;
static u8 hid_input_report[8] = {0};
```

Keep the HID handle macros in `rdx_ble_server.c` for Phase 1 (they move in Phase 2).

- [ ] **Step 6.3: Remove `hid_read_helper()` and `hid_att_read()` / `hid_att_write()` local implementations**

Delete the local functions:
- `static uint16_t hid_read_helper(...)`
- `static uint16_t hid_att_read(...)`
- `static int hid_att_write(...)`
- `static int hogp_fill_adv_data(...)`
- `static void hogp_adv_start(...)`
- `static void hogp_adv_stop(...)`
- `hogp_mode_set()` / `hogp_mode_get()` / `key_to_hid_usage[]` / `hogp_key_send()` / `hogp_key_up_timeout()` / `hogp_key_click_send()`

These now live in `rdx_hogp_keyboard.c`.

- [ ] **Step 6.4: Replace SM event callback body with forward**

Change `rdx_ble_server_sm_event_callback()` to:

```c
static void rdx_ble_server_sm_event_callback(void *hdl, uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    (void)hdl;
    (void)channel;

    rdx_hogp_on_sm_event(packet_type, packet, size);
}
```

- [ ] **Step 6.5: Replace HOGP connection logic with forwards**

In `rdx_ble_server_cbk_packet_handler()` under `HCI_SUBEVENT_LE_ENHANCED_CONNECTION_COMPLETE`:

```c
if (hogp_mode_get()) {
    rdx_hogp_on_connected(con_handle);
}
```

Under `HCI_SUBEVENT_LE_CONNECTION_COMPLETE`, replace the HOGP block:

```c
if (hogp_mode_get()) {
    rdx_hogp_on_connected(con_handle);
    break;  // 阻止后续 RDX 连接初始化
}
```

- [ ] **Step 6.6: Replace HOGP disconnect logic with forward**

Under `HCI_EVENT_DISCONNECTION_COMPLETE`:

```c
if (hogp_mode_get()) {
    rdx_hogp_on_disconnected(con_handle);
    break;
}
```

- [ ] **Step 6.7: Replace encryption change logic with forward**

Under `HCI_EVENT_ENCRYPTION_CHANGE`:

```c
rdx_hogp_on_encryption_change(enc_handle, enc_enabled, enc_status);
```

Remove the old inline `hogp_encrypted = 1` logic.

- [ ] **Step 6.8: Replace ATT read callback HOGP branch**

In `rdx_ble_server_att_read_callback()`:

```c
case HID_PROTOCOL_MODE_VALUE_HANDLE:
case HID_REPORT_MAP_VALUE_HANDLE:
case HID_INFORMATION_VALUE_HANDLE:
case HID_INPUT_REPORT_VALUE_HANDLE:
    att_value_len = rdx_hogp_att_read(connection_handle, handle, offset, buffer, buffer_size);
    if (att_value_len) {
        y_printf("[HOGP] read hdl=0x%04x offset=%d len=%d\r", handle, offset, att_value_len);
    }
    break;

case HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE:
    att_value_len = rdx_hogp_att_read(connection_handle, handle, offset, buffer, buffer_size);
    if (att_value_len) {
        y_printf("[HOGP] CCC read hdl=0x%04x cfg=0x%02x%02x\r", handle, buffer[0], buffer[1]);
    }
    break;
```

Note: `rdx_hogp_att_read()` does not currently handle CCC reads. Either extend `rdx_hogp_att_read()` to return CCC config, or keep the old CCC read inline and only forward the other handles. For Phase 1, the simplest behavior-preserving approach is to keep CCC read inline in `rdx_ble_server.c` and forward only value handles. Choose one consistent path and implement it.

Recommended: keep CCC read inline in `rdx_ble_server.c` for Phase 1 to minimize risk:

```c
case HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE:
    att_value_len = 2;
    if (buffer) {
        buffer[0] = multi_att_get_ccc_config(connection_handle, handle) & 0xFF;
        buffer[1] = 0;
        y_printf("[HOGP] CCC read hdl=0x%04x cfg=0x%02x%02x\r", handle, buffer[0], buffer[1]);
    }
    break;
```

- [ ] **Step 6.9: Replace ATT write callback HOGP branch**

In `rdx_ble_server_att_write_callback()`:

```c
// In HOGP mode, route all HID Service writes to the HOGP handler
if (hogp_mode_get() && handle >= HID_SERVICE_HANDLE && handle <= HID_CONTROL_POINT_VALUE_HANDLE) {
    return rdx_hogp_att_write(connection_handle, handle, transaction_mode, offset, buffer, buffer_size);
}
```

And remove the old inline `hid_att_write()` calls for `HID_CONTROL_POINT_VALUE_HANDLE`, `HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE`, and `HID_OUTPUT_REPORT_VALUE_HANDLE`.

- [ ] **Step 6.10: Initialize HOGP in `rdx_ble_server_init()`**

After `g_rdx_ble_server_info.rdx_ble_server_hdl` is allocated and before `rdx_ble_server_adv_enable(1)`:

```c
rdx_hogp_init(g_rdx_ble_server_info.rdx_ble_server_hdl);
```

- [ ] **Step 6.11: Build check**

Run: `cd SDK && make -j1 2>&1 | tail -n 100`
Expected: Link errors only due to Makefile not including new file.

---

### Task 7: Update `rdx_ble_server.h`

**Files:**
- Modify: `SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_server.h`

**Interfaces:**
- Consumes: `rdx_hogp_keyboard.h`.
- Produces: legacy `hogp_*` declarations remain available for `rdx_app.c`.

- [ ] **Step 7.1: Include new header and remove duplicate HOGP declarations**

Replace the HOGP block (currently lines 130–135):

```c
/* HOGP (HID over GATT Profile) extension APIs */
#include "rdx_hogp_keyboard.h"
```

This makes all `rdx_hogp_*` and legacy `hogp_*` functions visible to consumers of `rdx_ble_server.h`.

- [ ] **Step 7.2: Build check**

Run: `cd SDK && make -j1 2>&1 | tail -n 100`
Expected: Link errors only from Makefile omission.

---

### Task 8: Add `rdx_hogp_keyboard.c` to the Makefile

**Files:**
- Modify: `SDK/Makefile`

- [ ] **Step 8.1: Add source file after `rdx_ble_server.c`**

Find the line:

```makefile
apps/common/third_party_profile/rdx_protocol/rdx_ble_server.c \
```

Add immediately after:

```makefile
apps/common/third_party_profile/rdx_protocol/rdx_hogp_keyboard.c \
```

- [ ] **Step 8.2: Build check**

Run: `cd SDK && make -j1 2>&1 | tail -n 100`
Expected: Build succeeds.

---

### Task 9: Static verification and host test

**Files:**
- Verify: `SDK/apps/common/third_party_profile/rdx_protocol/rdx_profile_data[]` unchanged.
- Run: `tests/host/test_t2620_config_overlay.ps1`

- [ ] **Step 9.1: Confirm profile data table is byte-identical**

Run:

```bash
git diff -- SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_server.c
```

Expected: The `rdx_profile_data[]` bytes and comments are unchanged. If they changed, revert.

- [ ] **Step 9.2: Confirm Report Map bytes moved unchanged**

Run:

```bash
git diff -- SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_keyboard.c | grep -E "^\+|^\-" | grep -v "^\+\+\+|^\-\-\-"
```

Expected: The `hid_report_map[]` bytes in the new file exactly match the old bytes from `rdx_ble_server.c`.

- [ ] **Step 9.3: Run host config overlay test**

Run: `powershell -ExecutionPolicy Bypass -File tests/host/test_t2620_config_overlay.ps1`
Expected: PASS.

- [ ] **Step 9.4: Build with verbose log once more**

Run: `cd SDK && make clean && make -j1 2>&1 | tail -n 50`
Expected: Clean build succeeds.

---

### Task 10: Phase 1 review handoff

- [ ] **Step 10.1: Summarize changes for user review**

Prepare a summary covering:
1. New files created and what they contain.
2. Functions removed from `rdx_ble_server.c`.
3. Forwarding points added in `rdx_ble_server.c`.
4. Makefile change.
5. Build result and host test result.
6. Known temporary items: duplicated handle macros (to be removed in Phase 2), legacy `hogp_*` wrappers (to be removed when `rdx_app.c` migrates in Phase 3).

- [ ] **Step 10.2: Wait for user approval before starting Phase 2.**

Do not proceed to Phase 2 until the user explicitly confirms Phase 1 is accepted.

---

## Self-Review

**Spec coverage:**
- Add `rdx_hogp_keyboard.c/.h` → Tasks 1, 2.
- Move HOGP state variables and `hogp_key_send()` / `hogp_key_click_send()` → Tasks 2, 5.
- Move HID ATT read/write handlers → Task 3.
- Move SM Just-Works handling → Task 4.
- Move HOGP advertising construction → Task 4.
- Leave `rdx_ble_server.c` with forwarding + aggregate profile data → Tasks 6, 7.
- Byte-identical behavior constraints → Global Constraints + Task 9.

**Placeholder scan:** No TBD/TODO placeholders. Code snippets are concrete.

**Type consistency:** All `rdx_hogp_*` signatures match `rdx_hogp_keyboard.h`. Legacy wrappers keep original signatures.
