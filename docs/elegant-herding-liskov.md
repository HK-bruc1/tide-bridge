# Phase 6 C2 Plan: Lifecycle and Disabled-Profile Gaps

## Context

HOGP (HID-over-GATT keyboard) has been modularized through Phase 1-5. Phase 6 closes production-architecture gaps. C2 is the first implementation phase because it is low-risk and eliminates dangling resources (timer/handle) that C1's connection-owner state machine will rely on.

This plan addresses:
- Output Report handles `0x0028-0x002a` are present in the ATT table even when `TCFG_RDX_HOGP_ENABLE=0`.
- `rdx_hogp_key_click_usage()` creates a `sys_timeout_add()` timer but never saves/cancels it.
- `rdx_hogp_deinit()` exists but is incomplete and never called before `app_ble_hdl_free()`.
- Late timeout callbacks could touch a freed/reallocated `app_ble` handle.

## Scope

Only two source files are modified:
- `SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_server.c`
- `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_keyboard.c`

One host test file may be extended:
- `tests/host/test_hogp_profile_contract.ps1`

The frozen Profile v1 contract must not change:
- HID Service handles `0x0016-0x0022`
- Report Map 70 bytes
- Input Report 8 bytes, no Report ID prefix

## Implementation Steps

### Step 1: Gate Output Report behind `TCFG_RDX_HOGP_ENABLE` without changing attribute order

**File:** `SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_server.c`

The Output Report block stays at its current location after Device Information Service (lines 290-295). Wrap **only** those three attribute rows in a new, separate `#if TCFG_RDX_HOGP_ENABLE` gate. Do **not** move them inside the existing HID Service gate (lines 233-269), because that would change attribute order and break the Phase 5 host contract test.

```c
#if TCFG_RDX_HOGP_ENABLE
    // 0x0028 CHARACTERISTIC 0x2A4D (Output Report): Read | Write | Write Without Response
    0x0d, 0x00, 0x02, 0x00, 0x28, 0x00, 0x03, 0x28, 0x0e, 0x29, 0x00, 0x4d, 0x2a,
    // 0x0029 VALUE 0x2A4D (Output Report): Read | Write | Write Without Response, 1 byte LED state
    0x09, 0x00, 0x0e, 0x00, 0x29, 0x00, 0x4d, 0x2a, 0x00,
    // 0x002a REPORT_REFERENCE (ID=1, Type=2=Output)
    0x0a, 0x00, 0x02, 0x00, 0x2a, 0x00, 0x08, 0x29, 0x01, 0x02,
#endif /* TCFG_RDX_HOGP_ENABLE */
```

Also compile-out the entire `HID_OUTPUT_REPORT_VALUE_HANDLE` write case when HOGP is disabled:

```c
#if TCFG_RDX_HOGP_ENABLE
        case HID_OUTPUT_REPORT_VALUE_HANDLE:  // Output Report (LED state), only valid when HOGP compiled in
            if (buffer_size >= 1) {
                y_printf("[HOGP] output report write, LED=0x%02x\r", buffer[0]);
            }
            return 0;
#endif
```

### Step 2: Add two-layer cleanup: runtime cleanup and module cleanup

**File:** `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_keyboard.c`

Add two private helpers. `hogp_runtime_cleanup()` resets per-session state but keeps the wrapper handle, so a user can exit HOGP mode and re-enter later without a fresh `rdx_hogp_init()`. `hogp_module_cleanup()` performs runtime cleanup and then clears the handle; it is used only when the module is being torn down (e.g. server exit).

```c
static void hogp_runtime_cleanup(void)
{
    hogp_cancel_key_up_timer();
    memset((void *)s_hid_input_report, 0, sizeof(s_hid_input_report));
    s_hogp_mode = 0;
    s_hogp_connected = 0;
    s_hid_con_handle = 0;
    s_hid_notify_enabled = 0;
    s_hogp_encrypted = 0;
    s_hogp_generation++;
}

static void hogp_module_cleanup(void)
{
    hogp_runtime_cleanup();
    s_hogp_app_ble_hdl = NULL;
}
```

Neither helper touches advertising. Server exit already stops advertising, and `rdx_hogp_mode_set(0)` explicitly calls `hogp_adv_stop_internal()` before cleanup.

### Step 3: Track and cancel the key-release timer

**File:** `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_keyboard.c`

Add to local variables (after `s_hid_input_report`):

```c
static u16 s_hogp_key_up_timer = 0;
static u32 s_hogp_generation = 0;
static u32 s_hogp_key_generation = 0;
```

Add a cancellation helper before `hogp_key_up_timeout()`:

```c
static void hogp_cancel_key_up_timer(void)
{
    if (s_hogp_key_up_timer) {
        sys_timeout_del(s_hogp_key_up_timer);
        s_hogp_key_up_timer = 0;
    }
}
```

Harden the timeout callback:

```c
static void hogp_key_up_timeout(void *priv)
{
    u8 usage = (u8)(u32)priv;

    s_hogp_key_up_timer = 0;

    if (s_hogp_app_ble_hdl == NULL) {
        RDX_HOGP_ERROR("key_up timeout ignored: server hdl NULL");
        return;
    }
    if (s_hogp_generation != s_hogp_key_generation) {
        RDX_HOGP_ERROR("key_up timeout ignored: stale generation");
        return;
    }

    rdx_hogp_key_send_usage(usage, 0);
}
```

### Step 4: Harden init/deinit

Update `rdx_hogp_init()` to cancel any stale timer and bump generation **before** setting the new handle:

```c
void rdx_hogp_init(void *app_ble_hdl)
{
    hogp_cancel_key_up_timer();
    s_hogp_generation++;
    s_hogp_app_ble_hdl = app_ble_hdl;
    s_hogp_mode = 0;
    s_hogp_connected = 0;
    s_hid_notify_enabled = 0;
    s_hogp_encrypted = 0;
    s_hid_con_handle = 0;
    memset((void *)s_hid_input_report, 0, sizeof(s_hid_input_report));
    rdx_hogp_dump_state();
}
```

Update `rdx_hogp_deinit()` to use the advertising-free cleanup helper:

```c
void rdx_hogp_deinit(void)
{
    hogp_module_cleanup();
}
```

### Step 5: Update mode exit and disconnect cleanup

In `rdx_hogp_mode_set()` disable path, stop advertising first, then run runtime cleanup (which keeps the wrapper handle so HOGP can be re-entered later):

```c
    } else {
        hogp_adv_stop_internal();
        hogp_runtime_cleanup();
        rdx_hogp_dump_state();
    }
```

In `rdx_hogp_on_disconnected()`, cancel timer and zero connection state. Do not clear the persistent wrapper handle or bump generation on a simple disconnect:

```c
void rdx_hogp_on_disconnected(u16 con_handle)
{
    (void)con_handle;
    if (!s_hogp_mode) {
        return;
    }
    hogp_cancel_key_up_timer();
    memset((void *)s_hid_input_report, 0, sizeof(s_hid_input_report));
    s_hogp_connected = 0;
    s_hid_con_handle = 0;
    s_hid_notify_enabled = 0;
    s_hogp_encrypted = 0;
    RDX_HOGP_LOG("disconnect");
    rdx_hogp_dump_state();
}
```

### Step 6: Track timer ID only after successful key-down send

Update `rdx_hogp_key_click_usage()`:

```c
int rdx_hogp_key_click_usage(u8 usage)
{
    int ret = rdx_hogp_key_send_usage(usage, 1);

    if (ret == APP_BLE_NO_ERROR) {
        hogp_cancel_key_up_timer();
        s_hogp_key_generation = s_hogp_generation;
        s_hogp_key_up_timer = sys_timeout_add((void *)(u32)usage, hogp_key_up_timeout, RDX_HOGP_KEY_UP_DELAY_MS);
    }

    return ret;
}
```

If key-down fails, no release timer is scheduled; any previous timer continues and will release the previous key safely.

### Step 7: Guard ATT entry points against NULL handle

Add at the top of `rdx_hogp_att_read()` and `rdx_hogp_att_write()`:

```c
    if (s_hogp_app_ble_hdl == NULL) {
        return 0;
    }
```

### Step 8: Call `rdx_hogp_deinit()` from `rdx_ble_server_exit()`

**File:** `SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_server.c`

Insert before `app_ble_hdl_free()`:

```c
    rdx_hogp_deinit();

    app_ble_hdl_free(g_rdx_ble_server_info.rdx_ble_server_hdl);
    g_rdx_ble_server_info.rdx_ble_server_hdl = NULL;
```

The call is unconditional because `rdx_hogp_keyboard.c` provides empty stubs when `TCFG_RDX_HOGP_ENABLE=0`.

### Step 9: Add disabled-profile static check to host contract test

**File:** `tests/host/test_hogp_profile_contract.ps1`

Add a static check that the Output Report block (the three attribute rows at handles `0x0028-0x002a`) is wrapped inside `#if TCFG_RDX_HOGP_ENABLE` / `#endif`. This is a source-level guard; a preprocessor-based byte check can be added later if needed.

Example approach:
- Locate the three Output Report byte rows in `rdx_ble_server.c`.
- Walk backward from the first row and forward from the last row to find the nearest `#if TCFG_RDX_HOGP_ENABLE` and `#endif`.
- Assert the block is inside that gate and that no HID Service bytes appear outside it when disabled.

## Verification

1. **Host tests:**
   ```powershell
   powershell -ExecutionPolicy Bypass -File tests/host/run_host_tests.ps1
   ```
   Expected: both tests pass, including the new Output Report gating check.

2. **Build with HOGP enabled:**
   ```bash
   cd SDK
   make clean
   make
   ```
   Expected: clean build.

3. **Build with HOGP disabled:**
   - Temporarily set `TCFG_RDX_HOGP_ENABLE` to `0` in `SDK/apps/earphone/include/t2620_project_config.h`.
   - `make clean && make`.
   - Restore the macro to `1`.
   Expected: clean build; host contract test's disabled-profile check passes.

4. **Optional byte-level preprocessor verification:**
   After the disabled build, run the JL clang preprocessor on `rdx_ble_server.c` with `TCFG_RDX_HOGP_ENABLE=0` and confirm no `0x1812` or `0x2A4D` bytes remain. Exact command depends on the toolchain include paths in `SDK/Makefile`.

5. **Manual runtime check (enabled build):**
   - Enter HOGP mode, click keys rapidly, exit HOGP mode or trigger `rdx_ble_server_exit()`.
   - Confirm no crash, no stuck key reports, and logs show timer cancelled on cleanup.

## Risks and Notes

- Output Report is **not** moved into the HID Service gate; attribute order is preserved and the Phase 5 `PROFILE_ATTRIBUTE_ORDER` test should not regress.
- `hogp_module_cleanup()` is used only by `rdx_hogp_deinit()` (server exit). `hogp_runtime_cleanup()` is used by normal mode exit and keeps the wrapper handle so HOGP can be re-entered without a fresh `rdx_hogp_init()`.
- New variables and helper are placed inside the existing `#if TCFG_RDX_HOGP_ENABLE && (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN)` block; stubs remain untouched.
- `hogp_module_cleanup()` is idempotent: timer delete checks non-zero; setting fields to zero/NULL is safe to repeat.
- C2 intentionally works with the existing `hogp_mode_get()` logic; C1 will migrate cleanup to `connection_owner` state machine later.

## Commit Message

```
fix(hogp): gate Output Report and close lifecycle gaps (Phase 6 C2)

- Keep Output Report attributes (0x0028-0x002a) at their current location
  but wrap them in #if TCFG_RDX_HOGP_ENABLE, so the disabled build emits
  no HID GATT attributes without changing attribute order.
- Wrap the HID_OUTPUT_REPORT_VALUE_HANDLE write case entirely inside
  #if TCFG_RDX_HOGP_ENABLE.
- Call rdx_hogp_deinit() from rdx_ble_server_exit() before freeing app_ble;
  deinit uses a new advertising-free cleanup helper so server exit does not
  restart RDX advertising.
- Track the single auto key-release timer ID; cancel it on deinit,
  disconnect, and HOGP mode exit. Only schedule the release timer after a
  successful key-down send.
- Zero the Input Report buffer on all cleanup paths.
- Add a module-local generation counter so late timeout callbacks cannot
  touch a released or reallocated app_ble handle.
- Guard ATT read/write entry points against a NULL server handle.
- Extend the host profile contract test to verify the Output Report block
  is gated by TCFG_RDX_HOGP_ENABLE.

No change to the frozen Profile v1 contract: HID Service 0x0016-0x0022,
70-byte Report Map, 8-byte Input Report without Report ID prefix.
```
