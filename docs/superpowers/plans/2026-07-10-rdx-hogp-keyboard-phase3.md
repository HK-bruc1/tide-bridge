# RDX HOGP Keyboard Phase 3 — Configuration and Compile-Time Gating Implementation Plan

> **For agentic workers:** Use `superpowers:subagent-driven-development` or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Centralize all HOGP-configurable parameters into `rdx_hogp_config.h`, add a master `TCFG_RDX_HOGP_ENABLE` compile gate that can strip the entire HOGP module at build time, conditionally include/exclude HID Service attributes from `rdx_profile_data[]`, and migrate `rdx_app.c` from legacy `hogp_*` wrappers to the canonical `rdx_hogp_on_io_num_key()` API.

**Architecture:** `rdx_hogp_config.h` becomes the single source of truth for every tunable parameter in the HOGP module. The compile gate `TCFG_RDX_HOGP_ENABLE` follows the existing `TCFG_*` project-override pattern established by `TCFG_DIP_SWITCH_POWER_ENABLE`. When disabled, the entire `rdx_hogp_keyboard.c` compiles to empty stubs, and the HID Service byte block is omitted from `rdx_profile_data[]`. The RDX protocol layer (`RDX_EN`) and HOGP are decoupled: you can build with RDX enabled but HOGP disabled.

**Tech Stack:** C (JL AC701N / BR28 TWS firmware), BTstack GATT/ATT APIs, JL SDK `TCFG_*` config macro convention.
- Phase 3 does **not** add host-side contract tests — that is Phase 5.
- Must build clean for **both** `TCFG_RDX_HOGP_ENABLE=0` and `=1`, with `RDX_EN` always on.

When `TCFG_RDX_HOGP_ENABLE=0`, the HID Service byte block is omitted from
`rdx_profile_data[]` and all subsequent handles (DIS `0x0023`–`0x0027`,
Output Report `0x0028`–`0x002a`) shift down. This is acceptable for
products that never ship HOGP, but clients must not hardcode handles
across the two layouts.

---

## Global Constraints

- Phase 3 must not change any runtime behavior when `TCFG_RDX_HOGP_ENABLE=1`.
- GATT handle layout and Report Map must remain byte-identical to Phase 2 when enabled.
- Advertising payload and HOGP/RDX mutual exclusion remain unchanged.
- Phase 3 does **not** restructure the Output Report (`0x0029`) handling — it remains inline in `rdx_ble_server.c`.
- Phase 3 does **not** add logging macros — that is Phase 4.
- Phase 3 does **not** add host-side contract tests — that is Phase 5.
- Must build clean for **both** `TCFG_RDX_HOGP_ENABLE=0` and `=1`, with `RDX_EN` always on.

---

## File Map

| File | Responsibility after Phase 3 |
|---|---|
| `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_config.h` (new) | Single source of truth for all HOGP tunables: master enable, security, timing, advertising, keymap. Uses `#ifndef` guards so `t2620_project_config.h` can override. |
| `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_profile.c` (update) | Single definition site for `rdx_hogp_report_map[]`/`rdx_hogp_hid_information[]`; wraps arrays in `#if TCFG_RDX_HOGP_ENABLE` so disabled builds do not link the 74 bytes of const profile data. Includes `rdx_hogp_config.h`. |
| `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_profile.h` (no change) | Continues to expose HID Service handle macros and array `extern` declarations. |
| `SDK/apps/earphone/include/t2620_project_config.h` | Defines `TCFG_RDX_HOGP_ENABLE 1` (the only project-level toggle). |
| `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_keyboard.c` | Entire file gated on `#if TCFG_RDX_HOGP_ENABLE` with empty stubs in `#else`. Includes `rdx_hogp_config.h`. |
| `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_keyboard.h` | Legacy `hogp_*` wrappers gated on `TCFG_RDX_HOGP_ENABLE`; stub versions provided when disabled. Must include `rdx_hogp_config.h`. |
| `SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_server.c` | `rdx_profile_data[]` wraps HID Service block in `#if TCFG_RDX_HOGP_ENABLE`; `rdx_ble_server_init()` and event callbacks gated. |
| `SDK/apps/common/third_party_profile/rdx_protocol/rdx_app.c` | Replaces ~26 lines of `hogp_*` mode-switch logic with single `rdx_hogp_on_io_num_key()` call, gated on `TCFG_RDX_HOGP_ENABLE`. |

---

## Parameters to Centralize

### Master Enable

| Macro | Default | Rationale |
|---|---|---|
| `TCFG_RDX_HOGP_ENABLE` | `0` | Master kill switch. Default-off so non-HOGP products don't pay code size. Defined in `t2620_project_config.h`. |

### Security

| Macro | Default | Rationale |
|---|---|---|
| `RDX_HOGP_ENCRYPTION_REQUIRED` | `1` | Current behavior: `sm_api_request_pairing()` on connect then block notify until encrypted. Set to `0` for no-security test builds. |
| `RDX_HOGP_PAIRING_MODE` | `0` | `0` = Just Works (current), `1` = Passkey display, `2` = numeric comparison. Phase 3 only implements mode `0`; other values trigger `#error`. |

### Timing

| Macro | Default | Rationale |
|---|---|---|
| `RDX_HOGP_KEY_UP_DELAY_MS` | `20` | Current hardcoded `sys_timeout_add(…, 20)`. Makes key click duration tunable. |

### Advertising

| Macro | Default | Rationale |
|---|---|---|
| `RDX_HOGP_APPEARANCE` | `0x03C1` | BLE Appearance: Keyboard (961). Hardcoded in `rdx_hogp_fill_adv_data()`. |
| `RDX_HOGP_NAME_SOURCE` | `0` | `0` = use `rdx_ble_server_get_local_name()` (current, e.g. "VibeKeyboard"), `1` = use `RDX_HOGP_CUSTOM_NAME` literal. |
| `RDX_HOGP_CUSTOM_NAME` | `"VibeKeyboard"` | Used only when `RDX_HOGP_NAME_SOURCE=1`. |

### Default Keymap

| Macro | Default | Rationale |
|---|---|---|
| `RDX_HOGP_KEYMAP_A` | `0x04` | Keyboard Usage ID for key index 0 (NUM1). Default: A. |
| `RDX_HOGP_KEYMAP_B` | `0x05` | Key index 1 (NUM2). Default: B. |
| `RDX_HOGP_KEYMAP_C` | `0x06` | Key index 2 (NUM3). Default: C. |
| `RDX_HOGP_KEYMAP_D` | `0x07` | Key index 3 (NUM4). Default: D. |
| `RDX_HOGP_KEYMAP_E` | `0x08` | Key index 4 (NUM5 — unused in current IO layout, but available). Default: E. |

Combined into a static const array: `key_to_hid_usage[5]` is initialized from the five macros.

---

### Task 1: Create `rdx_hogp_config.h`

**Files:**
- Create: `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_config.h`

**Interfaces:**
- Consumes: nothing (pure macro definitions).
- Produces: `TCFG_RDX_HOGP_ENABLE` fallback, security/timing/advertising/keymap macros consumed by `rdx_hogp_keyboard.c`, `rdx_hogp_keyboard.h`, `rdx_ble_server.c`, and `rdx_hogp_profile.c`.

- [ ] **Step 1.1: Create header with include guards**

```c
/*=====================================================================================
 HEADER NAME: rdx_hogp_config.h
 MODULE NAME: RDX BLE HID-over-GATT keyboard compile-time configuration.

 GENERAL DESCRIPTION:
    Centralized tunables for the HOGP keyboard module. Every macro uses an
    #ifndef guard so project-level overrides (t2620_project_config.h) take
    precedence. This file is included by rdx_hogp_keyboard.c, rdx_hogp_keyboard.h,
    rdx_hogp_profile.c, and rdx_ble_server.c. This ensures all four files see the
    same fallback defaults.

=======================================================================================*/

#ifndef _RDX_HOGP_CONFIG_H_
#define _RDX_HOGP_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif
```

- [ ] **Step 1.2: Master enable with default-off safety**

```c
/******************************************************************************
* Master Enable
*
* TCFG_RDX_HOGP_ENABLE is the project-level kill switch. Define it in
* t2620_project_config.h. Defaults to 0 (disabled) if not defined, so
* non-HOGP products never pay code size.
*
* Override in t2620_project_config.h or board-specific config:
*   #define TCFG_RDX_HOGP_ENABLE 1

******************************************************************************/
#ifndef TCFG_RDX_HOGP_ENABLE
#define TCFG_RDX_HOGP_ENABLE                  0
#endif
```

- [ ] **Step 1.3: Security parameters**

```c
/******************************************************************************
* Security
******************************************************************************/
#ifndef RDX_HOGP_ENCRYPTION_REQUIRED
#define RDX_HOGP_ENCRYPTION_REQUIRED          1
#endif

#ifndef RDX_HOGP_PAIRING_MODE
#define RDX_HOGP_PAIRING_MODE                 0   /* 0=Just Works, 1/2 reserved */
#endif

#if RDX_HOGP_PAIRING_MODE != 0
#error "RDX_HOGP_PAIRING_MODE: only mode 0 (Just Works) is implemented in Phase 3"
#endif
```

- [ ] **Step 1.4: Timing**

```c
/******************************************************************************
* Timing
******************************************************************************/
#ifndef RDX_HOGP_KEY_UP_DELAY_MS
#define RDX_HOGP_KEY_UP_DELAY_MS             20
#endif
```

- [ ] **Step 1.5: Advertising**

```c
/******************************************************************************
* Advertising
******************************************************************************/
#ifndef RDX_HOGP_APPEARANCE
#define RDX_HOGP_APPEARANCE                  0x03C1   /* Keyboard */
#endif

#ifndef RDX_HOGP_NAME_SOURCE
#define RDX_HOGP_NAME_SOURCE                 0   /* 0=server local name, 1=custom */
#endif

#ifndef RDX_HOGP_CUSTOM_NAME
#define RDX_HOGP_CUSTOM_NAME                 "VibeKeyboard"
#endif
```

- [ ] **Step 1.6: Default keymap**

```c
/******************************************************************************
* Default Keymap (USB HID Keyboard Usage IDs)
******************************************************************************/
#ifndef RDX_HOGP_KEYMAP_A
#define RDX_HOGP_KEYMAP_A                    0x04   /* A */
#endif
#ifndef RDX_HOGP_KEYMAP_B
#define RDX_HOGP_KEYMAP_B                    0x05   /* B */
#endif
#ifndef RDX_HOGP_KEYMAP_C
#define RDX_HOGP_KEYMAP_C                    0x06   /* C */
#endif
#ifndef RDX_HOGP_KEYMAP_D
#define RDX_HOGP_KEYMAP_D                    0x07   /* D */
#endif
#ifndef RDX_HOGP_KEYMAP_E
#define RDX_HOGP_KEYMAP_E                    0x08   /* E */
#endif

#ifdef __cplusplus
}
#endif

#endif /* _RDX_HOGP_CONFIG_H_ */
```

---

### Task 2: Add `TCFG_RDX_HOGP_ENABLE` to `t2620_project_config.h`

**Files:**
- Modify: `SDK/apps/earphone/include/t2620_project_config.h`

- [ ] **Step 2.1: Append the HOGP enable macro before the final `#endif`**

```c
#ifndef TCFG_RDX_HOGP_ENABLE
#define TCFG_RDX_HOGP_ENABLE                  1
#endif
```

When adding a new product variant that doesn't need HOGP, set `TCFG_RDX_HOGP_ENABLE 0` in its project config. For T2620, enable it.

---

### Task 3: Compile-gate `rdx_hogp_keyboard.c` and consume config

**Files:**
- Modify: `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_keyboard.c`
- Modify: `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_keyboard.h`

**Interfaces:**
- Consumes: `rdx_hogp_config.h` for all tunable parameters.
- Produces: same public API; all functions become `return 0;` or `return;` stubs when disabled.

- [ ] **Step 3.1: Include config header and wrap entire .c file in `#if TCFG_RDX_HOGP_ENABLE`**

Ensure `app_config.h` is included **before** `rdx_hogp_keyboard.h`/`rdx_hogp_config.h` so `TCFG_RDX_HOGP_ENABLE` is resolved from `t2620_project_config.h` first; otherwise `rdx_hogp_config.h`'s `#ifndef` fallback will lock it to 0.

```c
/* Include project config first so TCFG_RDX_HOGP_ENABLE is resolved from
 * t2620_project_config.h before rdx_hogp_config.h applies its default. */
#include "sdk_config.h"
#include "app_config.h"

#include "rdx_hogp_keyboard.h"
#include "rdx_hogp_profile.h"
#include "rdx_hogp_config.h"

#if TCFG_RDX_HOGP_ENABLE

#if (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN)
// ... all existing code ...
#endif /* (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN) */

#else /* !TCFG_RDX_HOGP_ENABLE — stubs */
/* Stub functions — order matches rdx_hogp_keyboard.h declaration order. */


void rdx_hogp_init(void *app_ble_hdl) { (void)app_ble_hdl; }
void rdx_hogp_deinit(void) {}
u8   rdx_hogp_mode_get(void) { return 0; }
void rdx_hogp_mode_set(u8 enable) { (void)enable; }
u8   rdx_hogp_is_handle(u16 att_handle) { (void)att_handle; return 0; }
u16  rdx_hogp_att_read(hci_con_handle_t ch, u16 h, u16 o, u8 *b, u16 bs) {
    (void)ch; (void)h; (void)o; (void)b; (void)bs; return 0;
}
int  rdx_hogp_att_write(hci_con_handle_t ch, u16 h, u16 tm, u16 o, u8 *b, u16 bs) {
    (void)ch; (void)h; (void)tm; (void)o; (void)b; (void)bs; return 0;
}
void rdx_hogp_on_connected(u16 con_handle) { (void)con_handle; }
void rdx_hogp_on_disconnected(u16 con_handle) { (void)con_handle; }
void rdx_hogp_on_encryption_change(u16 ch, u8 en, u8 st) { (void)ch; (void)en; (void)st; }
void rdx_hogp_on_sm_event(u8 pt, u8 *pk, u16 sz) { (void)pt; (void)pk; (void)sz; }
int  rdx_hogp_fill_adv_data(u8 *adv_data, u8 max_len) { (void)adv_data; (void)max_len; return 0; }
void rdx_hogp_adv_start(void) {}
void rdx_hogp_adv_stop(void) {}
int  rdx_hogp_key_send_usage(u8 usage, u8 pressed) { (void)usage; (void)pressed; return -1; }
int  rdx_hogp_key_click_usage(u8 usage) { (void)usage; return -1; }
int  rdx_hogp_key_click_index(u8 key_index) { (void)key_index; return -1; }
int  rdx_hogp_on_io_num_key(u8 num_idx, u8 action) { (void)num_idx; (void)action; return -1; }

/* Legacy wrappers — stubs */
void hogp_mode_set(u8 enable) { (void)enable; }
u8   hogp_mode_get(void) { return 0; }
void hogp_key_send(u8 key_index, u8 pressed) { (void)key_index; (void)pressed; }
void hogp_key_click_send(u8 key_index) { (void)key_index; }

#endif /* TCFG_RDX_HOGP_ENABLE */
```

**Note on stub overhead:** The `#else` branch still compiles ~18 empty stub functions. This leaves a small residual code-size cost when `TCFG_RDX_HOGP_ENABLE=0` but avoids Makefile complexity. For this chip the overhead is negligible; if a future product needs true zero-cost exclusion, move the file out of the Makefile source list instead.

**Note on legacy wrappers:** The `hogp_*` stubs in the `#else` branch become unreachable once Task 3.3 removes their declarations from the header and Task 5 removes the only caller. They are kept as a defensive safety net; an optimizing linker will normally discard them.

- [ ] **Step 3.2: Replace hardcoded values with config macros**

In `rdx_hogp_keyboard.c`:

| Hardcoded value | Replace with |
|---|---|
| `sys_timeout_add(…, hogp_key_up_timeout, 20)` | `sys_timeout_add(…, hogp_key_up_timeout, RDX_HOGP_KEY_UP_DELAY_MS)` |
| `0x03C1` in `make_eir_packet_val(…, HCI_EIR_DATATYPE_APPEARANCE_DATA, 0x03C1, 2)` | `RDX_HOGP_APPEARANCE` |
| `const char *name = rdx_ble_server_get_local_name();` | Use `RDX_HOGP_NAME_SOURCE` to decide: `0` = server name, `1` = `RDX_HOGP_CUSTOM_NAME` |
| `static const u8 key_to_hid_usage[5] = {0x04, 0x05, 0x06, 0x07, 0x08}` | `{RDX_HOGP_KEYMAP_A, RDX_HOGP_KEYMAP_B, RDX_HOGP_KEYMAP_C, RDX_HOGP_KEYMAP_D, RDX_HOGP_KEYMAP_E}` |
| `sm_api_request_pairing(con_handle);` in `rdx_hogp_on_connected` | Wrap in `#if RDX_HOGP_ENCRYPTION_REQUIRED` |
| `if (!s_hogp_encrypted)` check in `rdx_hogp_key_send_usage` | Wrap in `#if RDX_HOGP_ENCRYPTION_REQUIRED` |

- [ ] **Step 3.3: Update `rdx_hogp_keyboard.h` — gate legacy wrappers**

The `hogp_*` wrappers in the header should be guarded so callers in `rdx_app.c` compile correctly:

```c
#if TCFG_RDX_HOGP_ENABLE
void hogp_mode_set(u8 enable);
u8   hogp_mode_get(void);
void hogp_key_send(u8 key_index, u8 pressed);
void hogp_key_click_send(u8 key_index);
#endif
```

This ensures `rdx_app.c` gets a compile error if it references `hogp_*` after migration, rather than silently calling stubs.

- [ ] **Step 3.4: Build check**

```bash
cd SDK
make clean && make -j1 2>&1 | tail -50
```

Expected: build succeeds, all HOGP functions resolve, config macros compile correctly.

- [ ] **Step 3.5: Gate `rdx_hogp_profile.c` array definitions**

**Files:**
- Modify: `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_profile.c`

Add `#include "app_config.h"` and `#include "rdx_hogp_config.h"`, then wrap the array definitions and compile-time length checks in `#if TCFG_RDX_HOGP_ENABLE`:

```c
#include "app_config.h"
#include "rdx_hogp_config.h"
#include "rdx_hogp_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

#if (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN) && TCFG_RDX_HOGP_ENABLE

const u8 rdx_hogp_report_map[] = { ... };
typedef char rdx_hogp_report_map_len_check[ ... ];

const u8 rdx_hogp_hid_information[] = { ... };
typedef char rdx_hogp_hid_information_len_check[ ... ];

#endif /* (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN) && TCFG_RDX_HOGP_ENABLE */

#ifdef __cplusplus
}
#endif
```

**Rationale:** `rdx_hogp_profile.c` is an independent compilation unit in `SDK/Makefile`. Without this gate, the 70-byte Report Map and 4-byte HID Information constants are still linked when `TCFG_RDX_HOGP_ENABLE=0`, directly violating the Phase 3 goal of zero code-size cost for non-HOGP products. The existing `RDX_EN` gate is kept so the file remains consistent with `rdx_hogp_keyboard.c` and `rdx_ble_server.c`.

- [ ] **Step 3.6: Build check with HOGP disabled**

Temporarily set `TCFG_RDX_HOGP_ENABLE 0` in `t2620_project_config.h`:

```bash
cd SDK
make clean && make -j1 2>&1 | tail -50
```

Expected: build succeeds and `rdx_hogp_report_map`/`rdx_hogp_hid_information` symbols are not linked. Restore `TCFG_RDX_HOGP_ENABLE 1` after verification.

---

### Task 4: Conditional `rdx_profile_data[]` HID Service

**Files:**
- Modify: `SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_server.c`

**Interfaces:**
- Consumes: `TCFG_RDX_HOGP_ENABLE`.
- Produces: GATT table with or without HID Service attributes.

- [ ] **Step 4.1: Wrap HID Service block in `rdx_profile_data[]`**

The HID Service block (handles `0x0016`–`0x0022`) in `rdx_profile_data[]` is wrapped:

```c
#if TCFG_RDX_HOGP_ENABLE
    //////////////////////////////////////////////////////
    //
    // 0x0016 PRIMARY_SERVICE  0x1812 (HID)
    //
    //////////////////////////////////////////////////////
    0x0a, 0x00, 0x02, 0x00, 0x16, 0x00, 0x00, 0x28, 0x12, 0x18,
    // ... (all HID Service bytes unchanged from Phase 2) ...
    0x08, 0x00, 0x04, 0x01, 0x22, 0x00, 0x4c, 0x2a,
#endif /* TCFG_RDX_HOGP_ENABLE */
```

The Device Information Service (`0x0023`–`0x0027`) and Output Report (`0x0028`–`0x002a`) are **not** gated — they remain unconditional (DIS is always present, Output Report is outside HID Service range).

**Note:** When `TCFG_RDX_HOGP_ENABLE=0`, the Output Report (`0x0028`–`0x002a`) remains in the GATT table without an owning HID Service, which is semantically incomplete. For T2620 this has no impact (HOGP is always enabled). A future phase may gate or remove it for disabled builds.

**Note:** `app_ble_profile_set(hdl, rdx_profile_data)` receives only a pointer and parses the table using the trailing `0x00, 0x00` sentinel (`rdx_profile_data[]` line 296). Removing the HID Service block shortens the table but leaves the sentinel intact, so runtime parsing remains correct.


- [ ] **Step 4.2: Gate `rdx_ble_server_init()` HOGP init call**

```c
#if TCFG_RDX_HOGP_ENABLE
    rdx_hogp_init(g_rdx_ble_server_info.rdx_ble_server_hdl);
#endif
```

- [ ] **Step 4.3: Gate HOGP event forwarders in `rdx_ble_server_cbk_packet_handler()`**

Each `if (hogp_mode_get()) { rdx_hogp_on_*(); }` block is wrapped:

```c
#if TCFG_RDX_HOGP_ENABLE
    if (hogp_mode_get()) {
        rdx_hogp_on_connected(con_handle);
    }
#endif
```

Same pattern for `on_disconnected`, `on_encryption_change`, and `on_sm_event`.

- [ ] **Step 4.4: Gate ATT read/write forwarding**

The read forwarding in `rdx_ble_server_att_read_callback()` already falls through to `rdx_hogp_att_read()` — which returns 0 as a stub when disabled. Adding the gate avoids the call overhead:

```c
#if TCFG_RDX_HOGP_ENABLE
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
#endif
```

Same for the write forwarding `if (hogp_mode_get() && …)` block.

- [ ] **Step 4.5: Build check with HOGP enabled**

```bash
cd SDK
make clean && make -j1 2>&1 | tail -50
```

Expected: build succeeds, GATT table unchanged from Phase 2.

- [ ] **Step 4.6: Build check with HOGP disabled**

Temporarily set `TCFG_RDX_HOGP_ENABLE 0` in `t2620_project_config.h`:

```bash
cd SDK
make clean && make -j1 2>&1 | tail -50
```

Expected: build succeeds with no HOGP code linked. Restore `TCFG_RDX_HOGP_ENABLE 1` after verification.

---

### Task 5: Migrate `rdx_app.c` from `hogp_*` to `rdx_hogp_on_io_num_key()`

**Files:**
- Modify: `SDK/apps/common/third_party_profile/rdx_protocol/rdx_app.c`

**Interfaces:**
- Consumes: `rdx_hogp_on_io_num_key()` (already exists in `rdx_hogp_keyboard.h`, already implemented in `.c`).
- Produces: cleaner IO key dispatch — same runtime behavior, ~26 fewer lines.

- [ ] **Step 5.1: Replace the HOGP mode-switch block in `rdx_app_earphone_key_remap()`**

The current block at `rdx_app.c:605–635`:

```c
        // HOGP 模式测试入口：
        //   IO NUM0 短按 → 进入 HOGP 模式
        //   IO NUM0 长按 → 退出 HOGP 模式
        //   IO NUM1~4 短按 → 发送字母 A~D
        if (hogp_mode_get()) {
            if (num_idx == 0) {
                if (index == KEY_ACTION_LONG) {
                    hogp_mode_set(0);
                    y_printf("[HOGP] exit HOGP mode\r");
                }
            } else {
                if (index == KEY_ACTION_CLICK) {
                    hogp_key_click_send(num_idx - 1);   // NUM1=A, NUM2=B, NUM3=C, NUM4=D
                }
            }
            *value = APP_MSG_NULL;
            return;
        } else {
            if (num_idx == 0 && index == KEY_ACTION_CLICK) {
                hogp_mode_set(1);
                y_printf("[HOGP] enter HOGP mode\r");
                *value = APP_MSG_NULL;
                return;
            }
        }
```

Is replaced with:

```c
#if TCFG_RDX_HOGP_ENABLE
        if (rdx_hogp_on_io_num_key(num_idx, index) == 0) {
            *value = APP_MSG_NULL;
            return;
        }
#endif
```

The `rdx_hogp_on_io_num_key()` function already contains the identical mode-switch, key-send, logging, and error handling — no behavior is lost.

- [ ] **Step 5.2: Remove `#include "rdx_hogp_keyboard.h"` if only used for `hogp_*`**

`rdx_app.c` already includes `rdx_ble_server.h`, which transitively includes `rdx_hogp_keyboard.h`. The `hogp_*` symbols are no longer referenced. If `rdx_app.c` has a direct `#include "rdx_hogp_keyboard.h"`, remove it.

Before removing `#include "rdx_hogp_keyboard.h"`, verify that `rdx_app.c` no longer depends on any macros or types defined only in that header. (`KEY_ACTION_CLICK` and `KEY_ACTION_LONG` are defined in `key_event_deal.h`, which `rdx_app.c` already includes directly; they are not sourced from `rdx_hogp_keyboard.h`.)


- [ ] **Step 5.3: Build check**

```bash
cd SDK
make clean && make -j1 2>&1 | tail -50
```

Expected: build succeeds with no `hogp_*` references in `rdx_app.c`. At runtime, NUM0 click enters HOGP, NUM0 long exits, NUM1–4 click sends keys — identical behavior.

---

### Task 6: Full verification

- [ ] **Step 6.1: Build with `TCFG_RDX_HOGP_ENABLE=1`**

```bash
cd SDK
make clean && make -j1 2>&1 | tail -50
```

Expected: clean build.

- [ ] **Step 6.2: Build with `TCFG_RDX_HOGP_ENABLE=0`**

Temporarily change `t2620_project_config.h` to `#define TCFG_RDX_HOGP_ENABLE 0`, build, then restore.

Expected: clean build with no HOGP symbols in the link. `rdx_profile_data[]` shorter by the HID Service block.

Additional verification that `rdx_hogp_profile.c` data is not linked:

```bash
nm SDK/cpu/br28/tools/download/earphone/sdk.elf | grep -E "rdx_hogp_report_map|rdx_hogp_hid_information"
```

Expected: no output.

- [ ] **Step 6.3: Verify GATT table byte-identical when enabled**

```bash
python3 -c "
import re, subprocess

def extract_hid_bytes(content):
    # Find hex bytes from HID Service start (0x0016) through 0x0022 value end
    m = re.search(
        r"//\s*0x0016 PRIMARY_SERVICE\s+0x1812.*?"
        r"//\s*0x0022 VALUE 0x2A4C.*?0x4c, 0x2a,",
        content, re.DOTALL
    )
    if not m:
        print("ERROR: HID block not found")
        return None
    return re.findall(r"0x[0-9a-fA-F]{2}", m.group(0))

orig = subprocess.check_output(
    ["git", "-C", "..", "show",
     "HEAD:SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_server.c"],
    text=True
)
old = extract_hid_bytes(orig)
with open("apps/common/third_party_profile/rdx_protocol/rdx_ble_server.c") as f:
    new = extract_hid_bytes(f.read())

print("old HID bytes:", len(old) if old else "not found")
print("new HID bytes:", len(new) if new else "not found")
print("byte-identical when enabled:", old == new)
"
```

This extracts the hex bytes from the HID Service segment (0x0016-0x0022) in
both the Phase 2 baseline and current working tree, comparing byte-for-byte.

- [ ] **Step 6.4: Run host test**

```powershell
powershell -ExecutionPolicy Bypass -File "tests\host\test_t2620_config_overlay.ps1"
```

Expected: `T2620 config overlay structure is valid.`

- [ ] **Step 6.5: Verify no remaining `hogp_*` callers outside the HOGP module**

First verify `rdx_app.c`:

```bash
grep -n "hogp_\|rdx_hogp_on_io_num_key" apps/common/third_party_profile/rdx_protocol/rdx_app.c
```

Expected: only `rdx_hogp_on_io_num_key` appears (inside the `#if TCFG_RDX_HOGP_ENABLE` guard). No `hogp_mode_get`, `hogp_mode_set`, `hogp_key_click_send` references.

Then scan the whole repo to ensure no other file references the legacy wrappers:

```bash
grep -RIn "hogp_mode_get\|hogp_mode_set\|hogp_key_send\|hogp_key_click_send" SDK/apps SDK/tests --include="*.c" --include="*.h" --include="*.py" --include="*.ps1"
```

Expected: no output (or only matches inside `rdx_hogp_keyboard.c/h` themselves).

---

### Task 7: Phase 3 review handoff

- [ ] **Step 7.1: Summarize changes for user review**

Prepare a summary covering:
1. New `rdx_hogp_config.h` and what it centralizes.
2. `TCFG_RDX_HOGP_ENABLE` compile gate and its placement in `t2620_project_config.h`.
3. Stub functions for `TCFG_RDX_HOGP_ENABLE=0`.
4. Conditional HID Service in `rdx_profile_data[]`.
5. `rdx_app.c` migration from `hogp_*` to `rdx_hogp_on_io_num_key()`.
6. Both build results (enabled and disabled).

- [ ] **Step 7.2: Wait for user approval before starting Phase 4.**

---

## Self-Review

**Spec coverage:**
- Create `rdx_hogp_config.h` with master enable, security, timing, advertising, keymap → Task 1.
- Add `TCFG_RDX_HOGP_ENABLE` to `t2620_project_config.h` → Task 2.
- Compile-gate `rdx_hogp_keyboard.c` with stubs → Task 3.
- Compile-gate `rdx_hogp_profile.c` array definitions → Task 3.5.
- Conditional `rdx_profile_data[]` HID block → Task 4.
- Migrate `rdx_app.c` to `rdx_hogp_on_io_num_key()` → Task 5.
- Both build states verified → Task 6.

**Placeholder scan:** No TBD/TODO placeholders. Code snippets are concrete.

**Configuration precedence chain:**
```
t2620_project_config.h  (defines TCFG_RDX_HOGP_ENABLE)
        ↓
app_config.h            (brings t2620_project_config.h to all app files)
        ↓
rdx_hogp_config.h       (#ifndef fallback defaults)
        ↓
rdx_hogp_keyboard.c
rdx_hogp_keyboard.h
rdx_ble_server.c        (via rdx_hogp_keyboard.h transitively)
rdx_hogp_profile.c      (all consume TCFG_RDX_HOGP_ENABLE)
```


**Type consistency:** All stub signatures match the public API in `rdx_hogp_keyboard.h` exactly. `(void)` casts suppress unused-parameter warnings.

**Compatibility:** The `hogp_*` header declarations are removed from `rdx_hogp_keyboard.h` when `TCFG_RDX_HOGP_ENABLE=0`, causing a compile error in `rdx_app.c` if migration is incomplete — this is intentional and forces clean migration.
