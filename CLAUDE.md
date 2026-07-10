# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project overview

This is a **JieLi (JL) AC701N / BR28 TWS earphone firmware** project. The application is built on top of JL's SDK (`SDK/`) with product-specific configuration in `src/` and output images in `output/`.

The current branch adds an **RDX third-party BLE protocol stack** with a recent **HOGP keyboard extension** that reuses RDX's GATT server handle.

- Project descriptor: `project.jlproj` (JL Studio project file)
- Target chip: AC701N, SDK type "TWS耳机", BR28 CPU
- Main application: `SDK/apps/earphone/`
- Build system: `SDK/Makefile` with custom `clang`/`pi32v2-lto-wrapper` toolchain

## Build commands

All firmware builds run from `SDK/`.

```bash
cd SDK
make
```

Outputs are produced under `SDK/cpu/br28/tools/` and then copied by the post-build script:

- `SDK/cpu/br28/tools/sdk.elf` — linked ELF
- `SDK/cpu/br28/tools/app.bin`
- `SDK/cpu/br28/tools/jl_isd.bin` / `jl_isd.fw`
- `SDK/cpu/br28/tools/update.ufw`
- `SDK/cpu/br28/tools/download/earphone/db_update_data.bin`

These are the files that ultimately land in `output/`.

Other useful targets:

```bash
cd SDK
make clean                 # remove objs/ and sdk.elf
make VERBOSE=1             # verbose compile log
```

The Makefile pre-build step generates several derived files from C source using the preprocessor (`-D__LD__ -E -P`):

- `apps/earphone/sdk_used_list.used`
- `apps/earphone/movable/section.txt`
- `cpu/br28/sdk.ld`
- `cpu/br28/tools/download.bat`
- `cpu/br28/tools/isd_config.ini`

Do not edit generated files directly; edit their `.c` sources instead.

### Toolchain (Windows)

The Makefile expects the JL toolchain at:

```
C:/JL/pi32/bin/clang.exe
C:/JL/pi32/bin/pi32v2-lto-wrapper.exe
```

On Linux, install the toolchain under `/opt/jieli` and run `ulimit -n 8096` before linking (see Makefile comments).

### VS Code tasks

`SDK/.vscode/tasks.json` exposes three tasks:
- `all` — build the firmware (Windows uses `.vscode/winmk.bat all`)
- `clean` — remove `objs/` and `sdk.elf`
- `test: host software` — run `.\tests\host\run_host_tests.ps1`

## Flash / download

The post-build script `SDK/cpu/br28/tools/download.bat` converts `sdk.elf` into flashable binaries and calls `SDK/cpu/br28/tools/download/earphone/download.bat`. In practice flashing is done through JL's `ISD_download.exe` or the JL Studio IDE, not from the command line in this repo.

## Tests

Run all host-side software validation tests through the unified entry point:

```powershell
.\tests\host\run_host_tests.ps1
```

The VS Code test task `test: host software` in `SDK/.vscode/tasks.json` calls the same script. To run a single test directly:

```powershell
.\tests\host\test_t2620_config_overlay.ps1
.\tests\host\test_hogp_profile_contract.ps1
```

`test_t2620_config_overlay.ps1` verifies that T2620-specific config overlays (`t2620_project_config.h`) are applied correctly on top of tool-generated `sdk_config.h`/`sdk_config.c`, and that the DIP-switch GPIO (PB1) is excluded from `iokey_config.c`.

`test_hogp_profile_contract.ps1` verifies that the frozen HOGP external contract has not regressed: HID handle macros in `rdx_hogp_profile.h`, Report Map length and byte sequence in `rdx_hogp_profile.c`, the 8-byte Input Report payload without a Report ID prefix in `rdx_hogp_keyboard.c`, and HID Service attribute order and byte-level values in `rdx_ble_server.c`. It reads the C source/header files and does not build or flash firmware.

There is no unit-test framework for the firmware itself; correctness is verified by build success, the PowerShell checks, and on-device testing.

## High-level architecture

### SDK layout

- `SDK/apps/earphone/` — product application entry point, modes (BT/idle/linein/PC/power-on), UI, audio scene switching
- `SDK/apps/common/` — shared subsystems: device drivers (key, charge, USB, LED), config tools, third-party BLE protocols
- `SDK/audio/` — audio framework, codecs, effects, CVP/AEC/ANC
- `SDK/cpu/br28/` — chip-specific drivers, linker scripts, prebuilt libraries (`SDK/cpu/br28/liba/`)
- `SDK/interface/` — SDK API headers
- `src/` — JSON configuration produced by JL Studio (power, board, keys, BT, audio, upgrade, tones, audio flows)

### Tasking model

`apps/earphone/app_main.c` defines `task_info_table[]`, which declares the OS tasks: `app_core`, `btctrler`, `btstack`, `jlstream`, `a2dp_dec`, `aec`, `file_dec`, `update`, `dac`, etc. Most application code runs on `app_core`; Bluetooth controller/stack run on their own tasks.

### Third-party protocol registration

`SDK/apps/common/third_party_profile/multi_protocol_main.c` is the central registration point for BLE/SPP protocols (RCSP, DMA, GFPS, Swift Pair, RDX, etc.). It defines the shared ATT RAM buffer and calls `app_ble_init()` internally from `btstack.a`, which creates a single shared ATT database.

Individual protocols allocate a wrapper handle with `app_ble_hdl_alloc()` and register their profile data, read/write callbacks, and packet handlers against that handle.

### RDX BLE server

The RDX stack lives in `SDK/apps/common/third_party_profile/rdx_protocol/` and is linked as `librdxApp.a` plus several source files listed in the Makefile:

- `rdx_ble_server.c` / `rdx_ble_server.h` — GATT server, advertising, connection state, and now the HOGP keyboard extension
- `rdx_app.c` / `rdx_app.h` — RDX application logic, TWS sync, key remapping
- `rdx_key.c`, `rdx_dip_switch.c`, `rdx_led_ctrl.c`, `rdx_battery.c`, etc. — per-feature modules

`rdx_ble_server_init()` registers `rdx_profile_data[]` and the read/write callbacks. All RDX attributes share one handle range.

### HOGP keyboard extension

The HOGP feature is implemented by extending the same RDX GATT server instead of creating a separate one. All attributes live in `rdx_profile_data[]` and share the single `app_ble` wrapper handle allocated by RDX.

#### GATT handle layout

```text
0x0001–0x0003: GAP Service (0x1800): Device Name
0x0004–0x000b: RDX custom 128-bit UUID service
0x000c–0x000f: Battery Service (0x180F)
0x0010–0x0015: RDX notify service
0x0016–0x0022: HID Service (0x1812)
0x0023–0x0027: Device Information Service (0x180A)
0x0028–0x002a: Output Report (0x2A4D) + Report Reference
```

HID Service details:

| Handle | Type | UUID | Purpose |
|--------|------|------|---------|
| 0x0016 | Service Declaration | 0x1812 | HID Service |
| 0x0018 | Value | 0x2A4E | Protocol Mode (Report Protocol = 0x01) |
| 0x001a | Value | 0x2A4D | Input Report (8-byte keyboard report) |
| 0x001b | Descriptor | 0x2902 | Input Report CCC |
| 0x001c | Descriptor | 0x2908 | Report Reference (ID=1, Type=Input) |
| 0x001e | Value | 0x2A4B | Report Map |
| 0x0020 | Value | 0x2A4A | HID Information |
| 0x0022 | Value | 0x2A4C | HID Control Point |

#### Data flow

1. User short-presses IO NUM0 → `rdx_app_earphone_key_remap()` calls `hogp_mode_set(1)`
2. `hogp_mode_set()` stops RDX advertising, clears stale scan response, and starts HID advertising with UUID `0x1812` and Appearance `0x03C1`
3. PC discovers the device and connects
4. On `HCI_SUBEVENT_LE_CONNECTION_COMPLETE`, the firmware records the handle and calls `sm_api_request_pairing()`
5. `rdx_ble_server_sm_event_callback()` handles `SM_EVENT_JUST_WORKS_REQUEST` and confirms pairing
6. On `HCI_EVENT_ENCRYPTION_CHANGE`, the `hogp_encrypted` flag is set
7. PC reads Report Map, HID Information, and writes `0x0001` to the Input Report CCC (`hid_notify_enabled = 1`)
8. Short-pressing IO NUM1~4 triggers `hogp_key_click_send()` → `hogp_key_send(pressed=1)` → 20 ms later `hogp_key_send(pressed=0)`
9. `app_ble_att_send_data()` sends the 8-byte Input Report via ATT notify

#### Key implementation points

- `config_le_gatt_server_num` stays `1`; `att_server_init()` is called once inside `btstack.a`
- HOGP and RDX advertising are mutually exclusive; the firmware switches advertising data when toggling HOGP mode
- PC-visible name in HOGP mode is taken from `rdx_ble_server_get_local_name()` so it matches the GAP Device Name
- Input Reports are **8-byte payloads** (`modifier` + `reserved` + 6 key slots). The key code goes at `report[2]`.
- **Do not prefix a Report ID byte in the ATT payload.** The Report ID (`0x01`) is declared inside the Report Map (`0x85, 0x01`) and associated with the characteristic via the Report Reference descriptor. This is the single biggest HOGP pitfall: USB HID prefixes reports with Report ID when multiple reports share an endpoint; BLE HID gives each report its own characteristic, so the payload is just the report body.
- Reports are only sent after `HCI_EVENT_ENCRYPTION_CHANGE` sets the `hogp_encrypted` flag
- Output Report (`0x0029`) receives Windows LED state writes (Caps/Num/Scroll Lock). T2620 has no physical LED, so MVP only logs the value.

#### Common HOGP issues

| Symptom | Likely cause | Where to look |
|---------|--------------|---------------|
| PC disconnects right after connection (reason 0x0D) | Advertising name ≠ GAP Device Name | `hogp_fill_adv_data()` must use `rdx_ble_server_get_local_name()` |
| No SM/paring logs, then disconnect | `config_le_sm_support_enable = 0` | `SDK/apps/earphone/log_config/lib_btstack_config.c` |
| Pairing request not confirmed | Missing SM event callback / `sm_just_works_confirm()` | `rdx_ble_server_sm_event_callback()` registration in `rdx_ble_server_init()` |
| `key_send ret=0` but no letters | Input Report payload includes Report ID byte (wrong format) | `hogp_key_send()` must send 8 bytes with keycode at `report[2]`; see HOGP_MVP_实施方案.md 坑5 |
| `key_send skipped: notify not enabled` | PC never wrote CCC | Check `HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE` write path; open `rdx_ble_server_att_write_callback()` debug print |
| After disconnect, PC sees RDX broadcast instead of HOGP | HOGP disconnect handler falls through to RDX disconnect logic | Add `break` in the HOGP branch of `HCI_EVENT_DISCONNECTION_COMPLETE` |

For the full step-by-step troubleshooting record, see `HOGP_MVP_实施方案.md` (especially the “踩坑记录” section).

### T2620 project config overlay

`sdk_config.h` and `sdk_config.c` under `SDK/apps/earphone/board/br28/` are owned by the JL visual configuration tool. Project-specific overrides that must survive tool regeneration go in:

```
SDK/apps/earphone/include/t2620_project_config.h
```

This overlay is included by `SDK/apps/earphone/include/app_config.h` immediately after `sdk_config.h`.

Current overlay rules:

- `TCFG_DIP_SWITCH_POWER_ENABLE` and `TCFG_DIP_SWITCH_POWER_IO` (PB1) are defined here
- Because PB1 is reserved for the DIP power switch, `TCFG_ADKEY_ENABLE` and `TCFG_LP_TOUCH_KEY_ENABLE` are forced to `0`
- Do **not** add `TCFG_DIP_SWITCH_POWER*` macros to `sdk_config.h`, `sdk_config.c`, or `iokey_config.c`

### GPIO / key configuration

Physical IO keys are configured in the tool-generated JSON (`src/按键配置.json`) and compiled into `g_iokey_info[]` via the board package. `SDK/apps/earphone/board/iokey_config.c` builds the `iokey_platform_data` from that table at runtime. PB1 must not appear in the IO key table because it is dedicated to the DIP power switch.

### Audio pipeline

Audio routing is configured visually in `src/音频流程/` as `.x6flow` files and compiled to `stream.bin`. Scenarios include system mode, BT music, BT call, USB audio, LE Audio, factory test, and translation earphone modes. The runtime audio framework loads these node graphs from `stream.bin`.

## Common development workflow

1. Edit product configs in JL Studio; it regenerates `src/*.json` and `SDK/apps/earphone/board/br28/sdk_config.h/c`
2. Add project-specific overrides only in `SDK/apps/earphone/include/t2620_project_config.h`
3. Build: `cd SDK && make`
4. Run host-side checks: `.\tests\host\run_host_tests.ps1`
5. Flash via JL Studio / `ISD_download.exe` using files in `SDK/cpu/br28/tools/download/earphone/` or copied `output/`

## Important files to know

- `SDK/Makefile` — build system; source file list, defines, includes, libraries
- `SDK/apps/earphone/app_main.c` — tasks, main init
- `SDK/apps/earphone/include/app_config.h` — includes sdk_config + t2620_project_config
- `SDK/apps/earphone/include/t2620_project_config.h` — project overrides (DIP switch, ADKEY/LP-touch disable)
- `SDK/apps/common/third_party_profile/multi_protocol_main.c` — BLE protocol registration hub
- `SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_server.c` — RDX GATT server and HOGP extension
- `SDK/apps/common/third_party_profile/rdx_protocol/rdx_app.c` — RDX app logic and key remapping
- `SDK/apps/earphone/board/iokey_config.c` — runtime IO key platform data builder
- `tests/host/run_host_tests.ps1` — unified entry point for host-side validation
- `tests/host/test_t2620_config_overlay.ps1` — host-side config overlay validation
- `tests/host/test_hogp_profile_contract.ps1` — host-side HOGP contract regression test
- `HOGP_MVP_实施方案.md` — detailed HOGP implementation notes
- `HOGP键盘移植最终评估.md` — HOGP architecture assessment and handle layout
