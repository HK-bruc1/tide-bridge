# HOGP Keyboard Modularization — Phase-by-Phase Todo

## Phase 1: Code Relocation Without Behavior Change

- [x] Create `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_keyboard.h` with public API.
- [x] Create `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_keyboard.c` and move HOGP state, ATT handlers, SM/connection events, advertising, key-send logic into it.
- [x] Update `SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_server.c` to thin forwarding layer; remove all HOGP state and internal functions.
- [x] Update `SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_server.h` to include `rdx_hogp_keyboard.h`.
- [x] Add `rdx_hogp_keyboard.c` to `SDK/Makefile` source list.
- [x] Build clean (`export PATH=".../SDK/tools/utils:$PATH" && cd SDK && make clean && make -j1`).
- [x] Run host test `tests/host/test_t2620_config_overlay.ps1`.
- [ ] Hardware regression pass (RDX/HOGP switch and A/B/C/D output).
- [ ] User approval before Phase 2.

## Phase 2: Profile Constants Centralization

- [x] Create `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_profile.h` with HID handle macros, Report Map, HID Information, Report Reference.
- [x] Replace magic handle numbers in `rdx_hogp_keyboard.c` and `rdx_ble_server.c` with macros from `rdx_hogp_profile.h`.
- [x] Remove temporary duplicated handle macros from `rdx_hogp_keyboard.c` (also clean up comment encoding).
- [x] Move `HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE` CCC read handling into `rdx_hogp_att_read()` to complete module boundary.
- [x] Build clean and verify byte-identical profile data.
- [x] User review and approval before Phase 3.

## Phase 3: Configuration

- [ ] Create `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_config.h` with `TCFG_RDX_HOGP_ENABLE`, encryption, pairing, delay, appearance, name-source, and default keymap macros.
- [ ] Make `rdx_hogp_keyboard.c` behavior conditional on `TCFG_RDX_HOGP_ENABLE` (stub functions when disabled).
- [ ] Conditionally include HID Service attributes in `rdx_profile_data[]` when `TCFG_RDX_HOGP_ENABLE`.
- [ ] Update `rdx_app.c` to call `rdx_hogp_on_io_num_key()` instead of direct `hogp_*` calls.
- [ ] Build clean for both `TCFG_RDX_HOGP_ENABLE=1` and `=0`.
- [ ] User review and approval before Phase 4.

## Phase 4: Logging and Diagnostics

- [ ] Introduce `RDX_HOGP_LOG` macro and standardize log points.
- [ ] Add `rdx_hogp_dump_state()` for live state inspection.
- [ ] Verify default log volume does not affect key-send timing.
- [ ] User review and approval before Phase 5.

## Phase 5: Host-Side Contract Tests

- [ ] Create `tests/host/test_hogp_profile_contract.ps1`.
- [ ] Verify handle macros, Report Map length, Input Report length, no Report ID prefix.
- [ ] Ensure existing `test_t2620_config_overlay.ps1` still passes.
- [ ] User review and final approval.

## Review Section

| Phase | Status | Notes |
|-------|--------|-------|
| Phase 1 | awaiting-hardware-regression | Code review passed (non-blocking: CCC read boundary + handle macro cleanup deferred to Phase 2). Build and host test passed. |
| Phase 2 | planning | Plan revised per feedback (Report Map length=70 with compile-time size check, extern+single-definition arrays, CCC read null-check, server-side CCC duplicate log removed, 0x0029 documented, Phase 1 baseline handling): `docs/superpowers/plans/2026-07-09-rdx-hogp-keyboard-phase2.md`. Awaiting final approval to implement. |
| Phase 2 | complete | 已实施并通过审核。HID 句柄、Report Map (70B)、HID Information 集中到 rdx_hogp_profile.h/.c，CCC 读封装到 rdx_hogp_att_read()，删除重复宏和死代码分支。GATT 表字节级一致。 |
| Phase 3 | pending | |
| Phase 4 | pending | |
| Phase 5 | pending | |
