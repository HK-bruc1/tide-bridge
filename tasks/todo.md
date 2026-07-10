# HOGP Keyboard — Phase 6 量产化重构

## Historical Phases (Complete)

- [x] Phase 1: Code Relocation Without Behavior Change
- [x] Phase 2: Profile Constants Centralization
- [x] Phase 3: Configuration
- [x] Phase 4: Logging and Diagnostics
- [x] Phase 5: Host-Side Contract Tests

## Phase 6: Closeout Refactor to Production Architecture

Document: `docs/HOGP收尾实施方案.md`  
Branch: `HOGP`  
Constraint: Each Phase (C1-C6) committed separately; no commit until user evaluation.

### Phase C0: Preconditions

- [x] Product target confirmed: default boot into HOGP keyboard.
- [x] RDX App key-settings extension scoped as RDX BLE App command extension, not new GATT service.
- [ ] Confirm JL BTstack available ATT error codes before C4.
- [ ] Confirm `HCI_SUBEVENT_LE_ENHANCED_CONNECTION_COMPLETE` dispatch before C1.
- [ ] Create HFP coexistence research task (out of Phase 6 scope).

### Phase C2: Lifecycle and Disabled-Profile Gaps

- [ ] Gate Output Report `0x0028-0x002a` behind `TCFG_RDX_HOGP_ENABLE`.
- [ ] Call `rdx_hogp_deinit()` before Server releases `app_ble` handle.
- [ ] Track single release timer ID; cancel on deinit/disconnect/mode exit; zero current report.
- [ ] Validate handle/generation before callbacks touch released resources.
- [ ] Verify disabled build has no HID attributes in preprocessed ATT table.
- [ ] Build clean for `TCFG_RDX_HOGP_ENABLE=1` and `=0`.
- [ ] Run host tests `tests/host/run_host_tests.ps1`.
- [ ] User evaluation before commit.

### Phase C1: Connection Ownership State Machine

- [ ] Add `requested_mode`, `advertised_mode`, `connection_owner`, `switch_pending`.
- [ ] Lock owner on connection complete events.
- [ ] Switch mode via disconnect -> wait -> owner cleanup -> start target advertising.
- [ ] Route disconnect by original owner, not `hogp_mode_get()`.
- [ ] Add owner authorization to ATT callbacks and notify entry.
- [ ] Unified Server control of HOGP/RDX advertising.
- [ ] Structured logging at every switch step.
- [ ] Keep `RDX_BLE_DEBUG_MODE_SWITCH_KEY` for local verification.

### Phase C3: Module Boundaries and Report API

- [ ] Remove `rdx_hogp_keyboard.h` transitive include from `rdx_ble_server.h`.
- [ ] Delete legacy `hogp_mode_*` / `hogp_key_*` wrappers with no callers.
- [ ] Introduce `rdx_hogp_keyboard_report_t` and full-report API.
- [ ] Remove physical key / fixed keymap knowledge from HOGP.

### Phase C4: Protocol State and Profile Data

- [ ] Sync current Input Report on send; zero on release.
- [ ] Robust encryption change handling.
- [ ] Protocol Mode and Control Point write handling with verified error codes.
- [ ] Capacity checks in `rdx_hogp_fill_adv_data()`.
- [ ] Single source of truth for handles/UUIDs/Report Reference.

### Phase C5: Product Identity and Default Mode

- [ ] Default boot into HOGP with name `VibeCoding Keyboard`.
- [ ] Config mode name `VibeCoding Config`.
- [ ] Remove temporary NUM0 test entry.
- [ ] Windows re-pairing validation.

### Phase C6: Verification, Docs, Handoff

- [ ] Update architecture docs.
- [ ] Extend host contract tests for disabled-profile and API boundaries.
- [ ] Output GATT snapshot, mode-switch logs, pairing records.

## Review Section

| Phase | Status | Notes |
|-------|--------|-------|
| Phase 1-5 | complete | Host contract test and CLAUDE.md updated. |
| Phase 6 C0 | in_progress | Document refined; preconditions partially confirmed. |
| Phase 6 C2 | pending | First implementation phase. |
