# AGENTS.md

This file provides guidance to Codex (Codex.ai/code) when working with code in this repository.

## Project overview

This is a **JieLi (JL) AC701N / BR28 TWS earphone firmware** project. The application is built on top of JL's SDK (`SDK/`) with product-specific configuration in `src/` and output images in `output/`.

The current branch adds an **RDX third-party BLE protocol stack** with a recent **HOGP keyboard extension** that reuses RDX's GATT server handle.

- Project descriptor: `project.jlproj` (JL Studio project file)
- Target chip: AC701N, SDK type "TWS耳机", BR28 CPU
- Main application: `SDK/apps/earphone/`
- Build system: `SDK/Makefile` with custom `clang`/`pi32v2-lto-wrapper` toolchain

## Build commands

All firmware builds run from `SDK/`. The checked-in VS Code tasks in
`SDK/.vscode/tasks.json` are the source of truth for build and test commands.

### Windows

Use the same wrapper as the VS Code `all` and `clean` tasks:

```powershell
cd SDK
.\.vscode\winmk.bat all
.\.vscode\winmk.bat clean
```

`winmk.bat` prepends `SDK/tools/utils` to `PATH` and invokes parallel `make`
with `%NUMBER_OF_PROCESSORS%`. Prefer this wrapper over a bare `make` command
on Windows so command-line builds match VS Code.

### Linux

The corresponding commands from `SDK/.vscode/tasks.json` are:

```bash
cd SDK
make all -j`nproc`
make clean -j`nproc`
```

Outputs are produced under `SDK/cpu/br28/tools/` and then copied by the post-build script:

- `SDK/cpu/br28/tools/sdk.elf` — linked ELF
- `SDK/cpu/br28/tools/app.bin`
- `SDK/cpu/br28/tools/jl_isd.bin` / `jl_isd.fw`
- `SDK/cpu/br28/tools/update.ufw`
- `SDK/cpu/br28/tools/download/earphone/db_update_data.bin`

These are the files that ultimately land in `output/`.

The VS Code default build task is `all`; the other firmware task is `clean`.

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

## Flash / download

The post-build script `SDK/cpu/br28/tools/download.bat` converts `sdk.elf` into flashable binaries and calls `SDK/cpu/br28/tools/download/earphone/download.bat`. In practice flashing is done through JL's `ISD_download.exe` or the JL Studio IDE, not from the command line in this repo.

## Tests

Run all host-side software validation tests through the unified entry point:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\host\run_host_tests.ps1
```

Run that command from the repository root. The VS Code test task
`test: host software` in `SDK/.vscode/tasks.json` calls the same script and is
the default test task.

The host test runner currently covers:

- `test_t2620_config_overlay.ps1` - verifies T2620-specific config overlays (`t2620_project_config.h`) on top of tool-generated `sdk_config.h`/`sdk_config.c`, and verifies that the DIP-switch GPIO (PB1) is excluded from `iokey_config.c`.
- `test_hogp_profile_contract.ps1` - freezes the HOGP external contract: HID handle macros, Report Map length and bytes, 8-byte Input Report payload without a Report ID prefix, and HID Service attribute order / byte-level values.
- `test_rdx_unified_adv_phase1.ps1` - freezes the production unified advertising layout and verifies that no development compatibility switch or standalone HOGP advertising configuration remains.
- `test_rdx_unified_session_phase2b.ps1` - freezes the owner-free single-link capability model, deferred advertising restart, RDX access policy, HID-ready boundary, and peer-scoped bonded CCC recovery.
- `test_hogp_keymap_architecture.ps1` / `test_hogp_keymap_behavior.ps1` - verify keymap transaction boundaries and keyboard action behavior.
- `test_rdx_local_playback_config.ps1` - verifies the RDX local playback compile-time boundary: master switch propagation, decoder/encoder separation, guarded application and key wiring, public Source_Dev0 APIs, and recording-side fix independence.
- `test_rdx_playback_navigation.ps1` - verifies local playback navigation, wrap/skip behavior, pause/resume state, seeking, and invalid-selection recovery.

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

The HOGP feature is implemented by extending the same RDX GATT server instead of creating a separate one:

- HID Service (`0x1812`) is appended to `rdx_profile_data[]` after the existing RDX services, using handles `0x0016–0x0022`; Device Information uses `0x0023–0x0027`, and the HID Output Report uses `0x0028–0x002a`
- `rdx_ble_server_att_read_callback()` dispatches HID reads (Protocol Mode, Report Map, HID Information, Input Report, Output Report)
- `rdx_ble_server_att_write_callback()` handles encrypted HID dynamic writes, including Protocol Mode, Input CCC, Control Point, and Output Report
- HID reports are sent via `app_ble_att_send_data()` on the RDX wrapper handle
- Physical-key routing lives in `rdx_app_earphone_key_remap()`; `rdx_hogp_key_action.c` builds reports and `rdx_hogp_keyboard.c` owns ATT transport

Key points:

- `config_le_gatt_server_num` stays `1`; `att_server_init()` is called once inside `btstack.a`
- RDX and HOGP use one connectable advertising entry: primary ADV keeps `Flags + local name`, while Scan Response keeps RDX Manufacturer Data first and appends HID UUID `0x1812`
- There is no CONFIG/HOGP advertising mode, connection owner, or unified-entry compatibility switch
- Any current BLE center may access RDX commands; online keyboard routing is independently gated by `Input CCC enabled && encrypted && !suspended`
- Bonded HID subscription intent is persisted per SM peer identity so a Windows reconnect can restore ready state without leaking CCC state to another peer

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
3. Build on Windows: `cd SDK` then `.\.vscode\winmk.bat all`
4. Run host software checks from the repository root: `powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\host\run_host_tests.ps1`
5. Flash via JL Studio / `ISD_download.exe` using files in `SDK/cpu/br28/tools/download/earphone/` or copied `output/`

## Important files to know

- `tests/host/run_host_tests.ps1` - unified host-side software test entry point
- `tests/host/test_hogp_profile_contract.ps1` - host-side HOGP profile contract validation
- `tests/host/test_rdx_local_playback_config.ps1` - host-side RDX local playback modularity validation
- `tests/host/test_rdx_playback_navigation.ps1` - host-side playback navigation validation

- `SDK/Makefile` — build system; source file list, defines, includes, libraries
- `SDK/.vscode/tasks.json` — source of truth for VS Code build/test commands
- `SDK/.vscode/winmk.bat` — Windows build wrapper used by the VS Code tasks
- `SDK/apps/earphone/app_main.c` — tasks, main init
- `SDK/apps/earphone/include/app_config.h` — includes sdk_config + t2620_project_config
- `SDK/apps/earphone/include/t2620_project_config.h` — project overrides (DIP switch, ADKEY/LP-touch disable)
- `SDK/apps/common/third_party_profile/multi_protocol_main.c` — BLE protocol registration hub
- `SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_server.c` — RDX GATT server and HOGP extension
- `SDK/apps/common/third_party_profile/rdx_protocol/rdx_app.c` — RDX app logic and key remapping
- `SDK/apps/earphone/board/iokey_config.c` — runtime IO key platform data builder
- `tests/host/test_t2620_config_overlay.ps1` — host-side config overlay validation
- `HOGP_MVP_实施方案.md` — detailed HOGP implementation notes
- `HOGP键盘移植最终评估.md` — HOGP architecture assessment and handle layout
