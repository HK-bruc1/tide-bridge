# RDX HOGP Keyboard Phase 4 — Logging and Diagnostics Standardization Implementation Plan

> **For agentic workers:** Use `superpowers:subagent-driven-development` or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Standardize HOGP diagnostics so a single serial log can answer whether the module entered keyboard mode, connected, paired, subscribed, and successfully sent an Input Report. Replace ad-hoc `y_printf("[HOGP] ...")` calls with a controlled logging macro, add a state dump API, and keep the default log volume low enough that it does not disturb key-send timing.

**Architecture:** HOGP keeps its own lightweight logging layer in `rdx_hogp_config.h` / `rdx_hogp_keyboard.c`. It does **not** depend on `log_info`/`log_debug` from `debug.h` because those log channels may be filtered independently and the existing `[HOGP]` output already uses `y_printf`. The new layer provides an on/off switch and consistent prefix/format so future regressions can be diagnosed from user logs without guessing.

**Tech Stack:** C (JL AC701N / BR28 TWS firmware), BTstack GATT/ATT APIs, JL SDK `y_printf`.
- Phase 4 does **not** change the GATT Profile, Report Map, handle layout, or Input Report payload.
- Phase 4 does **not** add host-side contract tests — that is Phase 5.
- Must build clean for **both** `TCFG_RDX_HOGP_ENABLE=0` and `=1`.

---

## Global Constraints

- Phase 4 must not change any runtime behavior except the text/format of HOGP logs.
- GATT handle layout, Report Map, and Input Report payload remain byte-identical to Phase 3.
- Default logging must remain enabled so ordinary test sessions capture the critical path.
- Logging must compile away entirely when `TCFG_RDX_HOGP_ENABLE=0`.
- A single malformed CCC write or unexpected disconnect must be visible in the log without extra instrumentation.

---

## File Map

| File | Responsibility after Phase 4 |
|---|---|
| `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_config.h` (update) | Adds `RDX_HOGP_LOG_ENABLE` and optional `RDX_HOGP_VERBOSE_LOG` tunables. Keeps all existing Phase 3 config macros. |
| `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_keyboard.c` (update) | Replaces raw `y_printf("[HOGP] ...")` with `RDX_HOGP_LOG(...)` / `RDX_HOGP_ERROR(...)` macros. Adds `rdx_hogp_dump_state()` and calls it at mode change, connect, disconnect, encryption change, CCC write, and key-send failure. |
| `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_keyboard.h` (update) | Declares `rdx_hogp_dump_state(void)`. No other API changes. |
| `SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_server.c` (no change expected) | Already forwards events. Any future `[HOGP]` logs should come from the module, not from `rdx_ble_server.c`. |

---

## Task 1: Add logging control macros to `rdx_hogp_config.h`

**Files:**
- Modify: `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_config.h`

**Interfaces:**
- Consumes: nothing new.
- Produces: `RDX_HOGP_LOG_ENABLE`, `RDX_HOGP_VERBOSE_LOG` macros consumed by `rdx_hogp_keyboard.c`.

- [ ] **Step 1.1: Add log-level tunables after the existing keymap section**

```c
/******************************************************************************
* Logging
******************************************************************************/
#ifndef RDX_HOGP_LOG_ENABLE
#define RDX_HOGP_LOG_ENABLE                   1   /* 0 = strip all HOGP logs */
#endif

#ifndef RDX_HOGP_VERBOSE_LOG
#define RDX_HOGP_VERBOSE_LOG                  0   /* 0 = only state changes and errors; 1 = per-packet read/write */
#endif
```

Rationale:
- `RDX_HOGP_LOG_ENABLE=0` removes every HOGP log string from the binary for ultra-tight products.
- `RDX_HOGP_VERBOSE_LOG=0` keeps only mode/connect/disconnect/encryption/CCC/key-send-failure/state-dump logs, suppressing per-handle read/write traffic.

---

## Task 2: Add logging macros to `rdx_hogp_keyboard.c`

**Files:**
- Modify: `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_keyboard.c`

**Interfaces:**
- Consumes: `RDX_HOGP_LOG_ENABLE`, `RDX_HOGP_VERBOSE_LOG` from `rdx_hogp_config.h`.
- Produces: internal-only `RDX_HOGP_LOG`, `RDX_HOGP_ERROR`, `RDX_HOGP_VERBOSE` macros.

- [ ] **Step 2.1: Define macros immediately inside the real-implementation section**

Place these right after the `#include "debug.h"` line (still inside `#if TCFG_RDX_HOGP_ENABLE && (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN)`):

```c
#if RDX_HOGP_LOG_ENABLE
#define RDX_HOGP_LOG(fmt, ...)    y_printf("[HOGP] " fmt "\r", ##__VA_ARGS__)
#define RDX_HOGP_ERROR(fmt, ...)  y_printf("[HOGP_ERR] " fmt "\r", ##__VA_ARGS__)
#if RDX_HOGP_VERBOSE_LOG
#define RDX_HOGP_VERBOSE(fmt, ...) y_printf("[HOGP] " fmt "\r", ##__VA_ARGS__)
#else
#define RDX_HOGP_VERBOSE(fmt, ...) /* no verbose log */
#endif
#else
#define RDX_HOGP_LOG(fmt, ...)     /* no log */
#define RDX_HOGP_ERROR(fmt, ...)   /* no log */
#define RDX_HOGP_VERBOSE(fmt, ...) /* no log */
#endif
```

- [ ] **Step 2.2: Replace raw `y_printf` calls with the new macros**

Use the mapping below. All replacements happen only inside `#if TCFG_RDX_HOGP_ENABLE && (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN)`.

| Current line (approx.) | Replacement |
|---|---|
| `y_printf("[HOGP] HID advertising started\r");` | `RDX_HOGP_LOG("HID advertising started");` |
| `y_printf("[HOGP] HID advertising stopped, restore RDX advertising\r");` | `RDX_HOGP_LOG("HID advertising stopped, restore RDX advertising");` |
| `y_printf("[HOGP] active ble conn, disconnect before mode switch\r");` | `RDX_HOGP_LOG("active ble conn, disconnect before mode switch");` |
| `y_printf("[HOGP] CCC read hdl=0x%04x ...", ...);` | `RDX_HOGP_VERBOSE("CCC read hdl=0x%04x cfg=0x%02x%02x", att_handle, buffer[0], buffer[1]);` |
| `y_printf("[HOGP] ctrl point hdl=0x%04x val=0x%02x\r", ...);` | `RDX_HOGP_VERBOSE("ctrl point hdl=0x%04x val=0x%02x", att_handle, buffer[0]);` |
| `y_printf("[HOGP] CCC write hdl=0x%04x cfg=0x%04x notify=%d\r", ...);` | `RDX_HOGP_LOG("CCC write hdl=0x%04x cfg=0x%04x notify=%d", att_handle, cfg, s_hid_notify_enabled);` |
| `y_printf("[HOGP] input report write ...");` | `RDX_HOGP_VERBOSE("input report write hdl=0x%04x len=%d data[0]=0x%02x", att_handle, buffer_size, buffer_size ? buffer[0] : 0);` |
| `y_printf("[HOGP] write default hdl=...");` | `RDX_HOGP_VERBOSE("write default hdl=0x%04x len=%d data[0]=0x%02x", att_handle, buffer_size, buffer_size ? buffer[0] : 0);` |
| `y_printf("[HOGP] conn complete hdl=0x%04x\r", con_handle);` | `RDX_HOGP_LOG("conn complete hdl=0x%04x", con_handle);` |
| `y_printf("[HOGP] disconnect\r");` | `RDX_HOGP_LOG("disconnect");` |
| `y_printf("[HOGP] encryption_change hdl=0x%04x enabled=%d status=%d\r", ...);` | `RDX_HOGP_LOG("encryption_change hdl=0x%04x enabled=%d status=%d", con_handle, enabled, status);` |
| `y_printf("[HOGP] link encrypted\r");` | `RDX_HOGP_LOG("link encrypted");` |
| `y_printf("[HOGP] Just Works pairing request, confirm\r");` | `RDX_HOGP_LOG("Just Works pairing request, confirm");` |
| `y_printf("[HOGP] key_send usage=... report=...", ...);` | Keep as `RDX_HOGP_LOG` because it shows the actual report bytes. |
| `y_printf("[HOGP] key_send skipped: not connected\r");` | `RDX_HOGP_ERROR("key_send skipped: not connected");` |
| `y_printf("[HOGP] key_send skipped: server hdl NULL\r");` | `RDX_HOGP_ERROR("key_send skipped: server hdl NULL");` |
| `y_printf("[HOGP] key_send skipped: notify not enabled\r");` | `RDX_HOGP_ERROR("key_send skipped: notify not enabled");` |
| `y_printf("[HOGP] key_send skipped: not encrypted\r");` | `RDX_HOGP_ERROR("key_send skipped: not encrypted");` |
| `y_printf("[HOGP] key_send ret=%d\r", ret);` | `RDX_HOGP_LOG("key_send ret=%d", ret);` |
| `y_printf("[HOGP] enter HOGP mode\r");` | `RDX_HOGP_LOG("enter HOGP mode");` |
| `y_printf("[HOGP] exit HOGP mode\r");` | `RDX_HOGP_LOG("exit HOGP mode");` |
| `y_printf("[HOGP] err: key_index %d out of range\r", key_index);` | `RDX_HOGP_ERROR("key_index %d out of range", key_index);` |

- [ ] **Step 2.3: Clean up the legacy wrapper warning**

In `hogp_key_send()` the current warning string stays useful; convert it to `RDX_HOGP_ERROR`.

---

## Task 3: Add `rdx_hogp_dump_state()`

**Files:**
- Modify: `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_keyboard.c`
- Modify: `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_keyboard.h`

**Interfaces:**
- Consumes: module static state (`s_hogp_mode`, `s_hogp_connected`, `s_hid_con_handle`, `s_hid_notify_enabled`, `s_hogp_encrypted`, `s_hogp_app_ble_hdl`).
- Produces: `void rdx_hogp_dump_state(void);`

- [ ] **Step 3.1: Declare the API in `rdx_hogp_keyboard.h`**

Add after the existing lifecycle declarations:

```c
/******************************************************************************
* Diagnostics
******************************************************************************/
void rdx_hogp_dump_state(void);
```

- [ ] **Step 3.2: Implement the dump function**

Place it near the connection/security event handlers in `rdx_hogp_keyboard.c`:

```c
void rdx_hogp_dump_state(void)
{
    RDX_HOGP_LOG("state mode=%d conn=%d con=0x%04x ccc=%d enc=%d hdl=%p",
                 s_hogp_mode,
                 s_hogp_connected,
                 s_hid_con_handle,
                 s_hid_notify_enabled,
                 s_hogp_encrypted,
                 s_hogp_app_ble_hdl);
}
```

- [ ] **Step 3.3: Provide a stub when HOGP is disabled**

In the `#else /* !(TCFG_RDX_HOGP_ENABLE && (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN)) — stubs */` branch, add:

```c
void rdx_hogp_dump_state(void) {}
```

This matches the Phase 3 pattern: ungated header declaration + stub implementation in the disabled branch, so other translation units can safely call it without link errors.

- [ ] **Step 3.4: Call `rdx_hogp_dump_state()` at state transitions**

Add one-line calls at the end of the following functions (inside the real-implementation block):

| Function | Where to add |
|---|---|
| `rdx_hogp_init()` | Optional: after zeroing all static state, to confirm a clean baseline. |
| `rdx_hogp_mode_set()` | After `hogp_adv_start_internal()` when entering mode; after `hogp_adv_stop_internal()` when exiting mode. |
| `rdx_hogp_on_connected()` | End of function. |
| `rdx_hogp_on_disconnected()` | End of function. |
| `rdx_hogp_on_encryption_change()` | End of function (after setting/clearing `s_hogp_encrypted`). |
| `rdx_hogp_att_write()` | After CCC write handling, only if `att_handle == HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE`. |
| `rdx_hogp_key_send_usage()` | In every `key_send skipped` branch. |

Rationale: when a user reports "PC connects but no letters", a single log will show whether the failure is "not in HOGP mode", "not connected", "CCC not subscribed", or "not encrypted".

---

## Task 4: Build and regression check

**Files:**
- No new files.

- [ ] **Step 4.1: Build with HOGP enabled**

```bash
cd SDK
make clean && make
```

Expected: clean build, no warnings about `y_printf` format strings.

- [ ] **Step 4.2: Build with HOGP disabled**

Temporarily set `TCFG_RDX_HOGP_ENABLE 0` in `SDK/apps/earphone/include/t2620_project_config.h`:

```bash
cd SDK
make clean && make
```

Expected: clean build. The new `RDX_HOGP_LOG` macros must not be referenced when the real implementation is stubbed out.

- [ ] **Step 4.3: Run host config overlay test**

```powershell
.\tests\host\test_t2620_config_overlay.ps1
```

Expected: `T2620 config overlay structure is valid.`

- [ ] **Step 4.4: Re-enable HOGP before finishing**

Restore `TCFG_RDX_HOGP_ENABLE 1` in `t2620_project_config.h`.

---

## Task 5: Hardware regression log check

- [ ] **Step 5.1: Flash the firmware and capture a complete HOGP session**

Perform the hardware regression checklist and verify the log now contains:

```text
[HOGP] enter HOGP mode
[HOGP] state mode=1 conn=0 con=0x0000 ccc=0 enc=0 hdl=0x...
[HOGP] HID advertising started
[HOGP] conn complete hdl=0x0050
[HOGP] state mode=1 conn=1 con=0x0050 ccc=0 enc=0 hdl=0x...
[HOGP] Just Works pairing request, confirm
[HOGP] encryption_change hdl=0x0050 enabled=1 status=0
[HOGP] link encrypted
[HOGP] state mode=1 conn=1 con=0x0050 ccc=0 enc=1 hdl=0x...
[HOGP] CCC write hdl=0x001b cfg=0x0001 notify=1
[HOGP] state mode=1 conn=1 con=0x0050 ccc=1 enc=1 hdl=0x...
[HOGP] key_send usage=0x04 pressed=1 report=00 00 04 00 00 00 00 00 conn=1 notify=1 encrypted=1
[HOGP] key_send ret=0
...
[HOGP] disconnect
[HOGP] state mode=1 conn=0 con=0x0000 ccc=0 enc=0 hdl=0x...
```

- [ ] **Step 5.2: Verify RDX mode still works**

1. Reboot, do **not** enter HOGP mode.
2. Connect with the RDX App.
3. Confirm RDX data channel works and no `[HOGP]` state logs appear.

---

## Self-Review

**Spec coverage:**
- Add logging control macros to `rdx_hogp_config.h` → Task 1.
- Replace ad-hoc `y_printf` with `RDX_HOGP_LOG`/`RDX_HOGP_ERROR`/`RDX_HOGP_VERBOSE` → Task 2.
- Add and trigger `rdx_hogp_dump_state()` → Task 3.
- Build clean for enabled and disabled states → Task 4.
- Hardware regression log check → Task 5.

**Behavior preservation:**
- No GATT handle changes.
- No Report Map changes.
- No Input Report payload changes.
- No mode-switch or pairing semantics changes.
- Only log text/format and the addition of `state` lines change.

**Recommended commit message:**

```text
feat(hogp): standardize Phase 4 diagnostics

- Add RDX_HOGP_LOG_ENABLE / RDX_HOGP_VERBOSE_LOG config macros
- Replace raw y_printf("[HOGP] ...") with RDX_HOGP_LOG/ERROR/VERBOSE
- Add rdx_hogp_dump_state() and trigger it on state transitions
- No functional change; GATT/Report Map/payload remain identical
```

---

## Next Phase

Phase 5 will add host-side HOGP profile contract tests (`tests/host/test_hogp_profile_contract.ps1`) to catch handle/Report Map regressions without flashing hardware.
