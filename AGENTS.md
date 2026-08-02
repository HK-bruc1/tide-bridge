# AGENTS.md

This file provides guidance to Codex (Codex.ai/code) when working with code in this repository.

## Project overview

This is a **JieLi (JL) AC701N / BR28 TWS earphone firmware** project. The application is built on top of JL's SDK (`SDK/`) with product-specific configuration in `src/` and output images in `output/`.

The current branch adds an **RDX third-party BLE protocol stack** with a **HOGP keyboard extension**. Two `app_ble` wrappers expose the same composite RDX + HOGP GATT profile and carry up to two Peripheral ACLs.

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

The host test runner keeps a six-contract core suite covering:

- `test_t2620_product_contract.ps1` - T2620 configuration, USB/storage ownership, and power-off cleanup.
- `test_hogp_profile_contract.ps1` - HOGP external bytes, layout, security boundary, handle-scoped SM identity, persona-aware bonded CCC, and queued-write rejection.
- `test_rdx_transport_contract.ps1` - fixed two-wrapper topology, composable capabilities, owner-scoped routing, and unified advertising.
- `test_rdx_lifecycle_contract.ps1` - immutable-runtime reconnect state machine, FIFO barrier, worker-idle rearm, same-peer restriction, and fail-closed behavior.
- `test_rdx_keymap_contract.ps1` - token-bound keymap transaction, verified A/B storage, hot-apply release ordering, and owner-directed response.
- `test_codex_micro_contract.ps1` - Codex identity, Report ID 6 layout, framing, RPC allowlist, owner generation, and lifecycle wiring.

The former phase-specific, playback, and split configuration scripts were
historical evidence or inactive product checks and are no longer part of the
daily host framework.

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
- HID reports are sent via `app_ble_att_send_data()` on the current HID owner wrapper
- Physical-key routing lives in `rdx_app_earphone_key_remap()`; `rdx_hogp_key_action.c` builds reports and `rdx_hogp_keyboard.c` owns ATT transport

Key points:

- `config_le_hci_connection_num` is fixed at `2` for the RDX product and `config_le_gatt_server_num` stays `1`; `att_server_init()` is called once inside `btstack.a`
- Two wrappers expose the same address/profile, but only one idle wrapper advertises at a time. Primary ADV keeps `Flags + local name`, while Scan Response keeps RDX Manufacturer Data first and appends HID UUID `0x1812`.
- There is no CONFIG/HOGP advertising mode, connection owner, or unified-entry compatibility switch
- One RDX owner and one HID owner are allowed globally. They may occupy separate links or the same link as a composite `RDX_HID` owner.
- RDX access claims the current link; online keyboard routing is independently gated by `Input CCC enabled && encrypted && !suspended`
- Bonded HID subscription intent is persisted per SM peer identity so a Windows reconnect can restore ready state without leaking CCC state to another peer

### T2620 project config overlay

`sdk_config.h` and `sdk_config.c` under `SDK/apps/earphone/board/br28/` are owned by the JL visual configuration tool. Project-specific overrides that must survive tool regeneration go in:

```
SDK/apps/earphone/include/t2620_project_config.h
```

This overlay is included by `SDK/apps/earphone/include/app_config.h` immediately after `sdk_config.h`.

Current overlay rules:

- `TCFG_DIP_SWITCH_POWER_ENABLE` and `TCFG_DIP_SWITCH_POWER_IO` (PB1) are defined here
- `TCFG_T2620_PC_STORAGE_ENABLE` owns the T2620 PC-storage configuration group: it requires tool-configured SD0 + USB MSC, enables PC mode, and disables the mutually exclusive USB HID/UAC classes
- Because PB1 is reserved for the DIP power switch, keep `TCFG_ADKEY_ENABLE` and `TCFG_LP_TOUCH_KEY_ENABLE` disabled in the JL visual configuration tool; do not repeat them in the project overlay
- PC storage forces the soldered SD NAND always-online policy in `t2620_project_config.h`; mount-failure formatting remains in `board_ac701n_demo_cfg.h`, where the VM marker distinguishes first-time initialization from filesystem recovery and both paths format
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
- `tests/host/test_t2620_product_contract.ps1` - product configuration and storage safety contract
- `tests/host/test_hogp_profile_contract.ps1` - host-side HOGP profile contract validation
- `tests/host/test_rdx_transport_contract.ps1` - dual-link transport and advertising contract
- `tests/host/test_rdx_lifecycle_contract.ps1` - reconnect lifecycle contract
- `tests/host/test_rdx_keymap_contract.ps1` - online keymap transaction contract

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
- `tests/host/test_t2620_product_contract.ps1` — product configuration and storage safety contract
- `HOGP_MVP_实施方案.md` — detailed HOGP implementation notes
- `HOGP键盘移植最终评估.md` — HOGP architecture assessment and handle layout
