# RDX HOGP Keyboard Phase 2 — Profile Constants Centralization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Centralize all HID Service profile constants (handle macros, Report Map, HID Information, Report Reference) into `rdx_hogp_profile.h`, replace magic numbers in both `rdx_hogp_keyboard.c` and `rdx_ble_server.c`, complete the CCC read boundary by moving it into the HOGP module, and verify the resulting firmware is byte-identical to Phase 1.

**Architecture:** `rdx_hogp_profile.h` becomes the single source of truth for everything the GATT handle table and the HOGP ATT handlers agree on. `rdx_hogp_profile.c` owns the one and only definition of the profile byte arrays (`rdx_hogp_report_map[]`, `rdx_hogp_hid_information[]`) to avoid duplicate storage when the header is included by multiple translation units. `rdx_hogp_keyboard.c` consumes the header for state machine and ATT routing; `rdx_ble_server.c` consumes it for the forwarding range check and `rdx_profile_data[]` comments. The module boundary is completed by handling the Input Report CCC read inside `rdx_hogp_att_read()`.

**Tech Stack:** C (JL AC701N / BR28 TWS firmware), BTstack GATT/ATT APIs, existing `multi_att_*` CCC helpers.

## Global Constraints

- GATT handle layout `0x0016`–`0x0022` for HID Service must remain byte-for-byte identical.
- Report Map bytes and length must remain byte-for-byte identical.
- Input Report payload remains 8 bytes: `[modifier, reserved, key1..key6]`.
- Advertising payload and HOGP/RDX mutual exclusion remain unchanged.
- Phase 2 does **not** add `rdx_hogp_config.h` or `TCFG_RDX_HOGP_ENABLE`; those are Phase 3.
- Phase 2 does **not** restructure Output Report (`0x0028`–`0x002a`); it remains handled inline in `rdx_ble_server.c` because `0x0029` is outside the HID Service handle range.
- `rdx_app.c` is **not** modified in Phase 2.

---

## File Map

| File | Responsibility after Phase 2 |
|---|---|
| `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_profile.h` (new) | Single source of truth for HID handle macros, Report Map/HID Information `extern` declarations, length macros, and Report Reference constants. |
| `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_profile.c` (new) | Single definition site for `rdx_hogp_report_map[]` and `rdx_hogp_hid_information[]`; includes JL section pragmas. |
| `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_keyboard.c` | Includes `rdx_hogp_profile.h`; removes temporary private handle macros and local arrays; handles CCC read inside `rdx_hogp_att_read()`. |
| `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_keyboard.h` | No change (already exposes API). |
| `SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_server.c` | Includes `rdx_hogp_profile.h`; replaces private HID handle macros with shared ones; forwards CCC read to module without duplicating the log. |
| `SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_server.h` | No change. |
| `SDK/Makefile` | Add `rdx_hogp_profile.c` to the source list. |

---

### Task 1: Create `rdx_hogp_profile.h` and `rdx_hogp_profile.c`

**Files:**
- Create: `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_profile.h`
- Create: `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_profile.c`
- Modify: `SDK/Makefile`

**Interfaces:**
- Consumes: nothing.
- Produces: constants and array symbols consumed by `rdx_hogp_keyboard.c` and `rdx_ble_server.c`.

- [ ] **Step 1.1: Create header with include guards and standard includes**

```c
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
```

- [ ] **Step 1.2: Add HID Service handle macros**

```c
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
```

- [ ] **Step 1.3: Add Report Map extern declaration and length macro**

```c
/******************************************************************************
* Report Map (Standard 70-byte boot keyboard report descriptor)
******************************************************************************/
#define RDX_HOGP_REPORT_MAP_LEN  (70)

extern const u8 rdx_hogp_report_map[];
```

- [ ] **Step 1.4: Add HID Information and Report Reference constants**

```c
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
```

**Important:** `RDX_HOGP_REPORT_MAP_LEN` is 70, not 63. The array is defined without an explicit size in `rdx_hogp_profile.c`; a compile-time `typedef` check guarantees `sizeof(rdx_hogp_report_map) == RDX_HOGP_REPORT_MAP_LEN`.

- [ ] **Step 1.5: Create `rdx_hogp_profile.c` with the single array definitions and length checks**

```c
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

#include "rdx_hogp_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 1.6: Add `rdx_hogp_profile.c` to `SDK/Makefile`**

Locate the existing RDX source list (near `apps/common/third_party_profile/rdx_protocol/rdx_hogp_keyboard.c`, added in Phase 1) and append:

```makefile
apps/common/third_party_profile/rdx_protocol/rdx_hogp_profile.c \
```

- [ ] **Step 1.7: Build check**

Run:
```bash
export PATH="/c/Users/31933/Desktop/t2620-firmware/SDK/tools/utils:$PATH"
cd SDK
make -j1 2>&1 | tail -n 100
```

Expected: Build succeeds or fails only because consumers in Tasks 2/3 still use old symbols.

---

### Task 2: Update `rdx_hogp_keyboard.c` to consume `rdx_hogp_profile.h`

**Files:**
- Modify: `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_keyboard.c`

**Interfaces:**
- Consumes: `rdx_hogp_profile.h` constants and `extern` array symbols defined in `rdx_hogp_profile.c`.
- Produces: `rdx_hogp_att_read()` that also handles CCC read; `rdx_hogp_is_handle()` using range helpers.

- [ ] **Step 2.1: Include profile header and remove temporary private handle macros**

After `#include "rdx_hogp_keyboard.h"`, add:

```c
#include "rdx_hogp_profile.h"
```

Remove the entire block (currently lines 50–67):

```c
/******************************************************************************
* Local Macro Define Section
******************************************************************************/
/* Temporary handle macros — will be centralized in rdx_hogp_profile.h in Phase 2 */
#define HID_SERVICE_HANDLE                                              0x0016
...
#define HID_OUTPUT_REPORT_VALUE_HANDLE                                  0x0029
```

- [ ] **Step 2.2: Replace local Report Map and HID Information arrays with profile constants**

Delete the local definitions:

```c
static const u8 hid_report_map[] = { ... };
static const u8 hid_information[] = {0x11, 0x01, 0x00, 0x03};
```

Replace usages in `rdx_hogp_att_read()`:

```c
    case HID_REPORT_MAP_VALUE_HANDLE:
        return hid_read_helper(rdx_hogp_report_map, RDX_HOGP_REPORT_MAP_LEN, offset, buffer, buffer_size);
    case HID_INFORMATION_VALUE_HANDLE:
        return hid_read_helper(rdx_hogp_hid_information, RDX_HOGP_HID_INFORMATION_LEN, offset, buffer, buffer_size);
```

- [ ] **Step 2.3: Move CCC read handling into `rdx_hogp_att_read()`**

Extend `rdx_hogp_att_read()` to handle `HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE`. `connection_handle` is now actually used, so remove the `(void)connection_handle;` cast. Keep the CCC read log here; `rdx_ble_server.c` will only forward and will not log CCC details again.

```c
u16 rdx_hogp_att_read(hci_con_handle_t connection_handle,
                      u16 att_handle,
                      u16 offset,
                      u8 *buffer,
                      u16 buffer_size)
{
    switch (att_handle) {
    case HID_PROTOCOL_MODE_VALUE_HANDLE:
        return hid_read_helper(&hid_protocol_mode, 1, offset, buffer, buffer_size);
    case HID_REPORT_MAP_VALUE_HANDLE:
        return hid_read_helper(rdx_hogp_report_map, RDX_HOGP_REPORT_MAP_LEN, offset, buffer, buffer_size);
    case HID_INFORMATION_VALUE_HANDLE:
        return hid_read_helper(rdx_hogp_hid_information, RDX_HOGP_HID_INFORMATION_LEN, offset, buffer, buffer_size);
    case HID_INPUT_REPORT_VALUE_HANDLE:
        return hid_read_helper(s_hid_input_report, sizeof(s_hid_input_report), offset, buffer, buffer_size);
    case HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE:
        if (buffer && buffer_size >= 2) {
            buffer[0] = multi_att_get_ccc_config(connection_handle, att_handle) & 0xFF;
            buffer[1] = 0;
            y_printf("[HOGP] CCC read hdl=0x%04x cfg=0x%02x%02x\r", att_handle, buffer[0], buffer[1]);
        }
        return 2;
    default:
        return 0;
    }
}
```

- [ ] **Step 2.4: Use range helper in `rdx_hogp_is_handle()`**

```c
u8 rdx_hogp_is_handle(u16 att_handle)
{
    return (att_handle >= HID_SERVICE_START_HANDLE && att_handle <= HID_SERVICE_END_HANDLE) ? 1 : 0;
}
```

Note: `HID_OUTPUT_REPORT_VALUE_HANDLE` (0x0029) is intentionally outside this range because it is not part of the HID Service and is not readable.

- [ ] **Step 2.5: Build check**

Run the build.
Expected: `rdx_hogp_keyboard.c` compiles successfully.

---

### Task 3: Update `rdx_ble_server.c` to consume `rdx_hogp_profile.h`

**Files:**
- Modify: `SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_server.c`

**Interfaces:**
- Consumes: `rdx_hogp_profile.h` handle macros.
- Produces: forwarding logic using centralized constants; CCC read forwarded to module without duplicate logging.

- [ ] **Step 3.1: Include profile header after HOGP keyboard header**

After `#include "rdx_hogp_keyboard.h"`, add:

```c
#include "rdx_hogp_profile.h"
```

- [ ] **Step 3.2: Remove duplicated HID handle macros from `rdx_ble_server.c`**

Delete the block (currently lines 91–112):

```c
// HOGP HID Service handles (appended after RDX services)
#define HID_SERVICE_HANDLE                                              0x0016
...
#define HID_OUTPUT_REPORT_VALUE_HANDLE                                  0x0029
```

- [ ] **Step 3.3: Replace ATT read CCC handling with forward to module**

In `rdx_ble_server_att_read_callback()`, remove the inline CCC case and forward it through `rdx_hogp_att_read()`. Do **not** print CCC details here; `rdx_hogp_att_read()` already logs them inside the module.

```c
        case HID_PROTOCOL_MODE_VALUE_HANDLE:
        case HID_REPORT_MAP_VALUE_HANDLE:
        case HID_INFORMATION_VALUE_HANDLE:
        case HID_INPUT_REPORT_VALUE_HANDLE:
        case HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE:
            att_value_len = rdx_hogp_att_read(connection_handle, handle, offset, buffer, buffer_size);
            if (att_value_len) {
                y_printf("[HOGP] read hdl=0x%04x offset=%d len=%d\r", handle, offset, att_value_len);
            }
            break;
```

- [ ] **Step 3.4: Use range helpers in ATT write routing**

The existing write routing already excludes `HID_OUTPUT_REPORT_VALUE_HANDLE` (0x0029) because that handle is outside the HID Service range. Keep that behavior, but use the shared range macros. Output Report writes continue to be handled inline in the `rdx_ble_server.c` switch (unchanged in Phase 2).

```c
    if (hogp_mode_get() && att_handle >= HID_SERVICE_START_HANDLE && att_handle <= HID_SERVICE_END_HANDLE) {
        return rdx_hogp_att_write(connection_handle, att_handle, transaction_mode, offset, buffer, buffer_size);
    }
```

- [ ] **Step 3.5: Build check**

Run the build.
Expected: `rdx_ble_server.c` compiles successfully.

---

### Task 4: Byte-identical verification

**Files:**
- Verify: `SDK/apps/common/third_party_profile/rdx_protocol/rdx_profile_data[]`
- Verify: Report Map bytes in `rdx_hogp_profile.c` vs Phase 1

**Baseline note:** Phase 1 changes are not yet committed on the current branch. Before Task 2 deletes the old `static const u8 hid_report_map[]` from `rdx_hogp_keyboard.c`, you must either:

- **Option A (recommended):** Commit Phase 1 now, then use `git show <phase1-commit>:SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_keyboard.c` as the baseline; or
- **Option B:** Run the snapshot script in Step 4.2 *before* Task 2 Step 2.2 to save the Phase 1 Report Map bytes to a file, then compare against that file.

- [ ] **Step 4.1: Verify `rdx_profile_data[]` is unchanged**

Run:
```bash
git -C .. diff -- SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_server.c
```

Expected: No changes to the `rdx_profile_data[]` byte array or handle comments.

- [ ] **Step 4.2: Verify Report Map bytes are unchanged**

**If Phase 1 is committed**, run:
```bash
python3 -c "
import re, subprocess

with open('apps/common/third_party_profile/rdx_protocol/rdx_hogp_profile.c', 'r', encoding='utf-8', errors='ignore') as f:
    new = [b for b in re.findall(r'0x[0-9a-fA-F]{2}', re.search(r'const u8 rdx_hogp_report_map\[\] = \{([^}]+)\};', f.read(), re.DOTALL).group(1))]
print('new Report Map bytes:', len(new))

orig = subprocess.check_output(['git', '-C', '..', 'show', 'HEAD:SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_keyboard.c'], text=True, encoding='utf-8', errors='ignore')
old = [b for b in re.findall(r'0x[0-9a-fA-F]{2}', re.search(r'static const u8 hid_report_map\[\] = \{([^}]+)\};', orig, re.DOTALL).group(1))]
print('old Report Map bytes:', len(old))
print('match:', new == old)
"
```

**If Phase 1 is NOT committed**, capture the snapshot before deleting the old array:
```bash
python3 -c "
import re
with open('apps/common/third_party_profile/rdx_protocol/rdx_hogp_keyboard.c', 'r', encoding='utf-8', errors='ignore') as f:
    old = [b for b in re.findall(r'0x[0-9a-fA-F]{2}', re.search(r'static const u8 hid_report_map\[\] = \{([^}]+)\};', f.read(), re.DOTALL).group(1))]
with open('/tmp/phase1_report_map.txt', 'w') as f:
    f.write('\n'.join(old))
print('saved', len(old), 'bytes')
"
```

Then after creating `rdx_hogp_profile.c`, compare:
```bash
python3 -c "
import re
with open('apps/common/third_party_profile/rdx_protocol/rdx_hogp_profile.c', 'r', encoding='utf-8', errors='ignore') as f:
    new = [b for b in re.findall(r'0x[0-9a-fA-F]{2}', re.search(r'const u8 rdx_hogp_report_map\[\] = \{([^}]+)\};', f.read(), re.DOTALL).group(1))]
with open('/tmp/phase1_report_map.txt', 'r') as f:
    old = [b.strip() for b in f.readlines()]
print('new:', len(new), 'old:', len(old), 'match:', new == old)
"
```

Expected in both cases: lengths equal 70 and `match: True`.

- [ ] **Step 4.3: Run full clean build**

```bash
export PATH="/c/Users/31933/Desktop/t2620-firmware/SDK/tools/utils:$PATH"
cd SDK
make clean && make -j1 2>&1 | tail -n 50
```

Expected: Build succeeds.

- [ ] **Step 4.4: Run host test**

```bash
powershell -ExecutionPolicy Bypass -File "C:\Users\31933\Desktop\t2620-firmware\tests\host\test_t2620_config_overlay.ps1"
```

Expected: `T2620 config overlay structure is valid.`

---

### Task 5: Phase 2 review handoff

- [ ] **Step 5.1: Summarize changes for user review**

Prepare a summary covering:
1. New `rdx_hogp_profile.h`/`rdx_hogp_profile.c` and what they centralize.
2. `rdx_hogp_keyboard.c` now consumes profile constants and handles CCC read.
3. `rdx_ble_server.c` no longer duplicates HID handle macros and forwards CCC read without duplicate logging.
4. Build result, host test result, and byte-identical verification result.

- [ ] **Step 5.2: Wait for user approval before starting Phase 3.**

Do not proceed to Phase 3 until the user explicitly confirms Phase 2 is accepted.

---

## Self-Review

**Spec coverage:**
- Add `rdx_hogp_profile.h` and `rdx_hogp_profile.c` → Task 1.
- Move handle macros, Report Map, HID Information into header → Tasks 1, 2, 3.
- Use `extern` + single definition for byte arrays with compile-time length checks → Task 1.
- Remove duplicated macros → Tasks 2, 3.
- Move CCC read into module without duplicate server-side logging → Tasks 2, 3.
- Byte-identical verification with Phase 1 baseline handling → Task 4.

**Placeholder scan:** No TBD/TODO placeholders. Code snippets are concrete.

**Type consistency:** All `rdx_hogp_*` signatures remain unchanged. `rdx_hogp_att_read()` returns `u16`; CCC read returns 2 on success, 0 in default case.
