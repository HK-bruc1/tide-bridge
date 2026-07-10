# RDX HOGP Keyboard Phase 5 — Host-Side Profile Contract Test Implementation Plan

> **For agentic workers:** Use `superpowers:subagent-driven-development` or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a lightweight host-side regression test that fails loudly if anyone accidentally changes the frozen HOGP external contract: GATT handle layout, Report Map bytes, Input Report payload length, or attribute order inside `rdx_profile_data[]`. Phase 5 does **not** change firmware behavior; it only adds a safety net for future maintenance.

**Architecture:** The test is a PowerShell script that reads the C source/header files under `SDK/apps/common/third_party_profile/rdx_protocol/` and asserts contract invariants. It runs without building or flashing, so it can be executed on every commit and in CI.

**Tech Stack:** PowerShell 5.1+, plain text parsing of C source. No new firmware code, no new dependencies.

**Scope:**
- Phase 5 does **not** change the GATT Profile, Report Map, handle layout, or Input Report payload.
- Phase 5 does **not** add new HOGP features.
- Phase 5 does **not** modify `rdx_ble_server.c`, `rdx_hogp_keyboard.c`, `rdx_hogp_profile.c`, or `rdx_hogp_profile.h` except where noted to expose testable contract anchors.
- Must build clean for **both** `TCFG_RDX_HOGP_ENABLE=0` and `=1` after any source change.

---

## Global Constraints

- Phase 5 must not change any runtime behavior.
- GATT handle layout, Report Map, and Input Report payload remain byte-identical to Phase 4.
- The new test must fail with a clear message when any frozen contract item changes.
- The test must run in under 5 seconds on a developer laptop.
- Existing `tests/host/test_t2620_config_overlay.ps1` must continue to pass.

---

## File Map

| File | Responsibility after Phase 5 |
|---|---|
| `tests/host/test_hogp_profile_contract.ps1` (new) | Host-side contract test. Reads C sources and asserts frozen HOGP invariants. |
| `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_profile.h` (read-only for test) | Source of truth for HID handle macros and Report Map length. Test asserts these values. |
| `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_profile.c` (read-only for test) | Contains `rdx_hogp_report_map[]`. Test asserts byte sequence and length. |
| `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_keyboard.c` (read-only for test) | Contains Input Report send logic. Test asserts payload length is 8 and no Report ID byte is prepended. |
| `SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_server.c` (read-only for test) | Contains `rdx_profile_data[]`. Test asserts HID Service attribute order. |

---

## Frozen Contract Snapshot (Phase 4 Baseline)

The following values are the baseline that Phase 5 will freeze. They must match the current codebase.

### Handle layout

```text
HID_SERVICE_HANDLE                              0x0016
HID_PROTOCOL_MODE_CHARACTERISTIC_HANDLE         0x0017
HID_PROTOCOL_MODE_VALUE_HANDLE                  0x0018
HID_INPUT_REPORT_CHARACTERISTIC_HANDLE          0x0019
HID_INPUT_REPORT_VALUE_HANDLE                   0x001a
HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE    0x001b
HID_INPUT_REPORT_REFERENCE_HANDLE               0x001c
HID_REPORT_MAP_CHARACTERISTIC_HANDLE            0x001d
HID_REPORT_MAP_VALUE_HANDLE                     0x001e
HID_INFORMATION_CHARACTERISTIC_HANDLE           0x001f
HID_INFORMATION_VALUE_HANDLE                    0x0020
HID_CONTROL_POINT_CHARACTERISTIC_HANDLE         0x0021
HID_CONTROL_POINT_VALUE_HANDLE                  0x0022
```

### Report Map

```text
Length: 70 bytes
```

```c
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
```

### Input Report payload

```text
Length: 8 bytes
Format: [modifier, reserved, key1, key2, key3, key4, key5, key6]
No Report ID byte prefix in the ATT notify payload.
```

### Profile data attribute order

The HID Service block inside `rdx_profile_data[]` must contain the following attribute declarations in order:

```text
0x0016 PRIMARY_SERVICE 0x1812
0x0017 CHARACTERISTIC 0x2A4E
0x0018 VALUE 0x2A4E
0x0019 CHARACTERISTIC 0x2A4D
0x001a VALUE 0x2A4D
0x001b CLIENT_CHARACTERISTIC_CONFIGURATION
0x001c REPORT_REFERENCE
0x001d CHARACTERISTIC 0x2A4B
0x001e VALUE 0x2A4B
0x001f CHARACTERISTIC 0x2A4A
0x0020 VALUE 0x2A4A
0x0021 CHARACTERISTIC 0x2A4C
0x0022 VALUE 0x2A4C
```

---

## Task 1: Create `tests/host/test_hogp_profile_contract.ps1`

**Files:**
- Create: `tests/host/test_hogp_profile_contract.ps1`

**Interfaces:**
- Consumes: `rdx_hogp_profile.h`, `rdx_hogp_profile.c`, `rdx_hogp_keyboard.c`, `rdx_ble_server.c`
- Produces: Exit code `0` on pass, non-zero on failure; descriptive console output.

### Step 1.1: Define the contract assertions

The script must implement the following checks. Each check should output its name and `PASS`/`FAIL` status, then exit with code `0` only if all pass.

| Check ID | What to verify | Where to look |
|---|---|---|
| `HANDLE_SERVICE` | `HID_SERVICE_HANDLE == 0x0016` | `rdx_hogp_profile.h` |
| `HANDLE_PROTOCOL_MODE_CHARACTERISTIC` | `HID_PROTOCOL_MODE_CHARACTERISTIC_HANDLE == 0x0017` | `rdx_hogp_profile.h` |
| `HANDLE_PROTOCOL_MODE_VALUE` | `HID_PROTOCOL_MODE_VALUE_HANDLE == 0x0018` | `rdx_hogp_profile.h` |
| `HANDLE_INPUT_REPORT_CHARACTERISTIC` | `HID_INPUT_REPORT_CHARACTERISTIC_HANDLE == 0x0019` | `rdx_hogp_profile.h` |
| `HANDLE_INPUT_REPORT_VALUE` | `HID_INPUT_REPORT_VALUE_HANDLE == 0x001a` | `rdx_hogp_profile.h` |
| `HANDLE_INPUT_REPORT_CCC` | `HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE == 0x001b` | `rdx_hogp_profile.h` |
| `HANDLE_INPUT_REPORT_REFERENCE` | `HID_INPUT_REPORT_REFERENCE_HANDLE == 0x001c` | `rdx_hogp_profile.h` |
| `HANDLE_REPORT_MAP_CHARACTERISTIC` | `HID_REPORT_MAP_CHARACTERISTIC_HANDLE == 0x001d` | `rdx_hogp_profile.h` |
| `HANDLE_REPORT_MAP_VALUE` | `HID_REPORT_MAP_VALUE_HANDLE == 0x001e` | `rdx_hogp_profile.h` |
| `HANDLE_HID_INFORMATION_CHARACTERISTIC` | `HID_INFORMATION_CHARACTERISTIC_HANDLE == 0x001f` | `rdx_hogp_profile.h` |
| `HANDLE_HID_INFORMATION_VALUE` | `HID_INFORMATION_VALUE_HANDLE == 0x0020` | `rdx_hogp_profile.h` |
| `HANDLE_CONTROL_POINT_CHARACTERISTIC` | `HID_CONTROL_POINT_CHARACTERISTIC_HANDLE == 0x0021` | `rdx_hogp_profile.h` |
| `HANDLE_CONTROL_POINT_VALUE` | `HID_CONTROL_POINT_VALUE_HANDLE == 0x0022` | `rdx_hogp_profile.h` |
| `REPORT_MAP_LENGTH` | `RDX_HOGP_REPORT_MAP_LEN == 70` | `rdx_hogp_profile.h` |
| `REPORT_MAP_BYTES` | `rdx_hogp_report_map[]` byte sequence matches the 70-byte snapshot | `rdx_hogp_profile.c` |
| `INPUT_REPORT_LENGTH` | `rdx_hogp_key_send_usage` sends exactly 8 bytes | `rdx_hogp_keyboard.c` |
| `NO_REPORT_ID_PREFIX` | Input Report notify payload does not prepend a Report ID byte | `rdx_hogp_keyboard.c` |
| `PROFILE_ATTRIBUTE_ORDER` | HID Service attributes appear in the frozen order and byte values inside `rdx_profile_data[]` | `rdx_ble_server.c` |

### Step 1.2: Implement handle macro extraction

Parse `#define HID_..._HANDLE` lines from `rdx_hogp_profile.h` using a regex like:

```powershell
#define\s+(?<name>HID_\w+_HANDLE)\s+(?<value>0x[0-9A-Fa-f]+)
```

Build a hashtable and compare against the frozen snapshot. Fail with a message such as:

```text
FAIL: HID_INPUT_REPORT_VALUE_HANDLE expected 0x001a but found 0x001b
```

### Step 1.3: Implement Report Map length and byte check

Parse `RDX_HOGP_REPORT_MAP_LEN` from `rdx_hogp_profile.h` and assert it equals `70`.

Parse the `rdx_hogp_report_map[]` array from `rdx_hogp_profile.c`:
- Extract the content between `{` and `}`.
- Strip C comments (`//` and `/* ... */`).
- Extract all hex byte tokens (`0x[0-9A-Fa-f]{2}` or `[0-9A-Fa-f]{2}` with `0x` prefix).
- Compare the resulting 70-byte sequence against the frozen snapshot.

If the length or any byte differs, fail with:

```text
FAIL: Report Map byte at index 3 expected 0x06 but found 0x07
```

### Step 1.4: Implement Input Report payload checks

In `rdx_hogp_keyboard.c`, locate `rdx_hogp_key_send_usage()`.

The contract is: **the notify payload is exactly the 8-byte `report` array, with no extra byte prepended.** Verify this by checking the code structure, not by banning any specific byte value in `report[0]`:

- Assert that the local `report` array is declared as `u8 report[8]`.
- Assert that `app_ble_att_send_data` is called with `HID_INPUT_REPORT_VALUE_HANDLE, report, sizeof(report)`.
- Assert that the pointer argument is literally `report`, not `report + 1`, `&report[1]`, or any other offset.
- Assert that the length argument is literally `sizeof(report)`, not `sizeof(report) + 1`, `9`, or a variable that could exceed 8.

Do **not** assert that `report[0] != 0x01`; `0x01` is a legitimate Left-Ctrl modifier value and should not be treated as a Report ID in this context.

Acceptable regex-based checks:

```powershell
u8\s+report\[8\]
```

and

```powershell
app_ble_att_send_data\s*\([^,]+,\s*HID_INPUT_REPORT_VALUE_HANDLE\s*,\s*report\s*,\s*sizeof\(report\)
```

If the code is restructured so the send call is no longer in the same function (e.g., helper function), the test should follow the data flow to the final `app_ble_att_send_data` call and assert the same contract.

### Step 1.5: Implement profile data attribute order and byte check

In `rdx_ble_server.c`, locate the HID Service block inside `rdx_profile_data[]`.

Because the array is gated by `#if TCFG_RDX_HOGP_ENABLE`, the script should look for the block that starts with the `0x0016 PRIMARY_SERVICE 0x1812` comment and ends with the `0x0022 VALUE 0x2A4C` comment.

For each attribute entry, parse **both** the comment line and the byte line(s) that follow it. Do not rely on comments alone; the byte line is the actual firmware contract.

**Comment parsing:** extract handle, attribute type, and UUID. Normalize UUID strings by making the leading `0x` optional (`0x2A4E` and `2A4E` must be treated as equal).

**Byte line parsing:** Each attribute declaration in `rdx_profile_data[]` spans one or more lines of comma-separated hex bytes. The byte format follows the BTstack ATT database convention:

```text
[attribute_size_lo, attribute_size_hi, flags_lo, flags_hi, handle_lo, handle_hi, att_uuid..., value...]
```

Decode each attribute by type. The `att_uuid` is the ATT declaration UUID, which is **not** the same as the service/characteristic UUID for PRIMARY_SERVICE and CHARACTERISTIC entries.

| Attribute type | `att_uuid` | What to extract from `value` bytes |
|---|---|---|
| `PRIMARY_SERVICE` | `0x2800` | `service_uuid` (e.g., `0x1812`) |
| `CHARACTERISTIC` | `0x2803` | `properties` (1 byte), `value_handle` (2 bytes, little-endian), `characteristic_uuid` (e.g., `0x2A4E`) |
| `VALUE` | characteristic UUID itself (e.g., `0x2A4E`) | first byte(s) of the characteristic value, if statically defined |
| `CLIENT_CHARACTERISTIC_CONFIGURATION` | `0x2902` | initial CCC value (usually `0x00, 0x00`) |
| `REPORT_REFERENCE` | `0x2908` | `report_id` (1 byte), `report_type` (1 byte) |

For each attribute, build a contract record containing:
- `handle` from bytes at offset 4-5, little-endian
- `type` from the comment
- `att_uuid` from the byte line
- type-specific fields from the `value` bytes (service_uuid, characteristic_uuid, properties, value_handle, report_id, report_type)

Compare the ordered list of contract records against the frozen snapshot. The snapshot should store both the comment-level UUID and the byte-level decoded fields for each attribute. Example frozen entry:

```text
handle=0x0016, type=PRIMARY_SERVICE, att_uuid=0x2800, service_uuid=0x1812
handle=0x0017, type=CHARACTERISTIC, att_uuid=0x2803, properties=0x06, value_handle=0x0018, characteristic_uuid=0x2A4E
handle=0x0018, type=VALUE, att_uuid=0x2A4E, value=0x01
handle=0x0019, type=CHARACTERISTIC, att_uuid=0x2803, properties=0x1a, value_handle=0x001a, characteristic_uuid=0x2A4D
handle=0x001a, type=VALUE, att_uuid=0x2A4D
handle=0x001b, type=CLIENT_CHARACTERISTIC_CONFIGURATION, att_uuid=0x2902, value=0x0000
handle=0x001c, type=REPORT_REFERENCE, att_uuid=0x2908, report_id=0x01, report_type=0x01
...
```

Fail if any of the following regress:
- handle value mismatch (catches comment-correct but byte-wrong changes)
- attribute type reorder
- `att_uuid` mismatch (catches wrong declaration UUID)
- type-specific field mismatch, such as:
  - wrong `service_uuid` for PRIMARY_SERVICE
  - wrong `characteristic_uuid`, `properties`, or `value_handle` for CHARACTERISTIC
  - wrong `att_uuid` for VALUE (means wrong characteristic UUID)
  - wrong `report_id`/`report_type` for REPORT_REFERENCE
- missing or extra attributes

Example failure messages:

```text
FAIL: PROFILE_ATTRIBUTE_ORDER handle 0x0017 expected att_uuid 0x2803 but found 0x2800
FAIL: PROFILE_ATTRIBUTE_ORDER handle 0x0017 CHARACTERISTIC expected value_handle 0x0018 but found 0x0019
FAIL: PROFILE_ATTRIBUTE_ORDER handle 0x0016 PRIMARY_SERVICE expected service_uuid 0x1812 but found 0x1811
FAIL: PROFILE_ATTRIBUTE_ORDER attribute at index 2 expected handle 0x0018 type VALUE att_uuid 0x2A4E but found handle 0x0019
```

**UUID normalization note:** Source comments sometimes write `0x2A4E` and sometimes `2A4E`. The test must strip an optional `0x` prefix before comparison so existing code does not produce false positives.

### Step 1.6: Provide clear output and exit codes

Example successful output:

```text
HOGP Profile Contract Tests
===========================
PASS: HANDLE_SERVICE
PASS: HANDLE_PROTOCOL_MODE_CHARACTERISTIC
PASS: HANDLE_PROTOCOL_MODE_VALUE
PASS: HANDLE_INPUT_REPORT_CHARACTERISTIC
PASS: HANDLE_INPUT_REPORT_VALUE
PASS: HANDLE_INPUT_REPORT_CCC
PASS: HANDLE_INPUT_REPORT_REFERENCE
PASS: HANDLE_REPORT_MAP_CHARACTERISTIC
PASS: HANDLE_REPORT_MAP_VALUE
PASS: HANDLE_HID_INFORMATION_CHARACTERISTIC
PASS: HANDLE_HID_INFORMATION_VALUE
PASS: HANDLE_CONTROL_POINT_CHARACTERISTIC
PASS: HANDLE_CONTROL_POINT_VALUE
PASS: REPORT_MAP_LENGTH
PASS: REPORT_MAP_BYTES
PASS: INPUT_REPORT_LENGTH
PASS: NO_REPORT_ID_PREFIX
PASS: PROFILE_ATTRIBUTE_ORDER
---------------------------
All 18 HOGP profile contract checks passed.
```

Example failure output:

```text
FAIL: HANDLE_INPUT_REPORT_VALUE expected 0x001a but found 0x001c
...
---------------------------
1 of 18 HOGP profile contract checks failed.
```

---

## Task 2: Wire the test into the regression flow

**Files:**
- No source file changes.

**Interfaces:**
- Consumes: `tests/host/test_hogp_profile_contract.ps1`
- Produces: Updated developer workflow.

### Step 2.1: Document the new test in `CLAUDE.md`

Add a line under the Tests section in `D:\Felix\Work_felix\T2620\tide-bridge\CLAUDE.md`:

```markdown
- `tests/host/test_hogp_profile_contract.ps1` — host-side HOGP profile contract test; verifies frozen handle layout, Report Map bytes, Input Report payload format, and GATT attribute order.
```

### Step 2.2: Mention the test in the Phase 5 commit message

Recommended commit message:

```text
test(hogp): add host-side HOGP profile contract regression test

- Add tests/host/test_hogp_profile_contract.ps1
- Verify all 13 HID handle macros against frozen Phase 4 baseline
- Verify Report Map length (70 bytes) and byte sequence
- Verify Input Report payload is 8 bytes with no Report ID prefix
- Verify HID Service attribute order and byte-level handle/UUID values inside rdx_profile_data[]
- No firmware code changes; GATT/Report Map/payload remain identical
```

---

## Task 3: Build and regression check

**Files:**
- No new firmware files.

### Step 3.1: Run the new contract test

```powershell
.\tests\host\test_hogp_profile_contract.ps1
```

Expected: `All 18 HOGP profile contract checks passed.`

### Step 3.2: Run the existing host overlay test

```powershell
.\tests\host\test_t2620_config_overlay.ps1
```

Expected: `T2620 config overlay structure is valid.`

### Step 3.3: Build with HOGP enabled

```bash
cd SDK
export PATH="/c/JL/mc/bin:/c/JL/pi32/bin:$PATH"
make clean MKDIR='mkdir -p'
make MKDIR='mkdir -p'
```

Expected: clean build, no new warnings.

### Step 3.4: Build with HOGP disabled

Temporarily set `TCFG_RDX_HOGP_ENABLE 0` in `SDK/apps/earphone/include/t2620_project_config.h`, then:

```bash
cd SDK
make clean MKDIR='mkdir -p'
make MKDIR='mkdir -p'
```

Expected: clean build.

### Step 3.5: Restore HOGP enable

Restore `TCFG_RDX_HOGP_ENABLE 1` in `t2620_project_config.h`.

---

## Task 4: Deliberately inject failures to prove the test works

**Files:**
- No permanent changes.

### Step 4.1: Inject a handle change

Temporarily change `HID_INPUT_REPORT_VALUE_HANDLE` to `0x001b` in `rdx_hogp_profile.h`. Run the contract test and verify it fails with a clear message. Revert the change.

### Step 4.2: Inject a Report Map change

Temporarily change the first byte of `rdx_hogp_report_map[]` from `0x05` to `0x06`. Run the contract test and verify it fails. Revert the change.

### Step 4.3: Inject a payload length change

Temporarily change `u8 report[8]` to `u8 report[9]` in `rdx_hogp_key_send_usage()`. Run the contract test and verify it fails. Revert the change.

### Step 4.4: Inject an attribute reorder

Temporarily swap two **complete attribute entries** in `rdx_profile_data[]`. An entry includes both the comment line and all of the byte line(s) that follow it. For example, swap the entire `0x001e VALUE 0x2A4B` entry (comment + byte line) with the entire `0x0020 VALUE 0x2A4A` entry. Run the contract test and verify it fails because the parsed attribute order no longer matches the frozen snapshot. Revert the change.

### Step 4.5: Inject a byte-level handle change

Temporarily change the handle bytes in one attribute's byte line without updating the comment. For example, change the byte line for `0x0018 VALUE 0x2A4E` so the handle bytes encode `0x0019` while leaving the comment as `0x0018`. Run the contract test and verify it fails because the parsed byte-level handle does not match the frozen snapshot. Revert the change.

---

## Self-Review

**Spec coverage:**
- Add host-side HOGP profile contract test → Task 1.
- Check all 13 HID handle macros → Step 1.2.
- Check Report Map length and bytes → Step 1.3.
- Check Input Report payload length and no Report ID prefix → Step 1.4.
- Check profile data attribute order and byte-level handle/UUID values → Step 1.5.
- Wire into regression flow → Task 2.
- Build and regression check → Task 3.
- Prove the test catches regressions → Task 4.

**Behavior preservation:**
- No GATT handle changes.
- No Report Map changes.
- No Input Report payload changes.
- No firmware behavior changes.
- The only new artifact is the PowerShell test script.

**Recommended commit message:**

```text
test(hogp): add host-side HOGP profile contract regression test

- Add tests/host/test_hogp_profile_contract.ps1
- Verify all 13 HID handle macros against frozen Phase 4 baseline
- Verify Report Map length (70 bytes) and byte sequence
- Verify Input Report payload is 8 bytes with no Report ID prefix
- Verify HID Service attribute order and byte-level handle/UUID values inside rdx_profile_data[]
- No firmware code changes; GATT/Report Map/payload remain identical
```

---

## Next Phase

Phase 6 will pre-study a possible Profile v2 layout (Output Report inside HID Service, optional Report-ID-free single-keyboard Report Map, Device Information Service placement, etc.) without enabling it by default. Profile v2 must be gated by an explicit configuration macro and tested as a separate compatibility case.
