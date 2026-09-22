# Host validation

Run from the repository root (also the VS Code `test: host software` task):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\host\run_host_tests.ps1
```

The entry point runs five source-contract scripts and eight C behavioral harnesses
as **13 independent processes**. A failed module does not suppress the others;
the exit code is 1 if any module fails. Use `-Suite contracts` or `-Suite behavior`
for focused runs; the default `all` remains the complete check. Each file can also
run directly. Individual PowerShell scripts accept `-Verbose` for assertion names.

## Coverage

| Source contract | Essential boundary |
| --- | --- |
| `test_t2620_product_contract.ps1` | Product configuration, power/charging, storage fences, recording admission and SDK integration |
| `test_hogp_profile_contract.ps1` | HID protocol bytes, security, readiness and peer-scoped bonded CCC |
| `test_rdx_transport_contract.ps1` | Dual-wrapper ownership, sends/retries, advertising and persisted binding |
| `test_rdx_lifecycle_contract.ps1` | FIFO/worker reconnect barrier, RDX-only release, OTA cancellation and recording STOP |
| `test_rdx_keymap_contract.ps1` | Five-key protocol, token checks across commit stages, A/B store wiring and response ownership |

| C behavioral harness | Production behavior exercised with mocked boundaries |
| --- | --- |
| `test_keymap_factory_reset.py` | Factory HID defaults survive reload; A/B write/readback/commit and apply failures preserve old keys; retry, revision overflow, idempotence and reset ACK/reboot ordering |
| `test_hogp_hold.py` | Scan/adapter/FIFO, hold/fast edges, offline clicks, KEY5 HID/recording gesture coexistence and disabled mapping, hot-apply release/rollback, ATT context, stale owners, RDX release, cancellation and failed Up recovery; `key5_record_checks.py` checks local hold/double recording ownership, playback exclusion, failed starts and release retries |
| `test_factory_usb.py` | SDK configuration matrix, storage admission/shutdown, CDC queues, stale sessions, partial I/O and bounded DMA |
| `test_finalpack.py` | Packaging commit order, VM/format/queue/timer failures, business unbinding and key DUT recovery |
| `test_dut_keys_speaker.py` | Parsing/ownership, key state reset, queue overflow/legacy cleanup, recording authorization, speaker ACK/stop ordering, volume rollback and PCM partial writes |
| `test_record_storage.py` | Async frame ownership/order, simulated 220ms/2s storage stalls, saturation, metadata/write failures, pause/stop drain and two-hour mono/stereo frame counts |
| `test_meeting_mono.py` | Mono/selector integration, allocation cleanup, pairing faults, channel metadata, RAW playback and player draining |
| `test_record_format.py` | Real cJSON metadata, DAT merge, identity conflicts, failed I/O and interrupted transaction replay |

## Keeping the suite small

Factory reset board regression: save a non-default HID map with the PC App,
send the phone App's `*APP#default#`, then reconnect after reboot and verify
KEY1–KEY5 are F13–F17. Power-cycle again and query the map to check persistence.
Repeat when the map is already default and with a held HID key; verify no stuck
key remains. The shared packaging reset must restore the same map without
rebooting. Storage failure must return failure and must not schedule a reboot.
Factory reset fences further App keymap commands until init/reboot; the dedicated
PC `RESET_KEYMAP` continues to support online editing without this fence.

Keep stable protocol/configuration boundaries and failure-path behavior. Prefer
observable outcomes over variable names, comments, exact call counts or copied
implementation sequences. Keep source-only integration guards where mocks do not
execute the caller; counts protecting two distinct recording variants remain.

The 2026-09-21 cleanup removes duplicate keymap hot-apply/DUT authorization checks
covered by C harnesses, duplicate reconnect-order checks, patch-instruction text
already protected by the pinned archive hash, and log/comment prerequisites.
USB class checks now inspect preprocessed SDK macros rather than duplicate header
spellings. LED mark colour/duration, default recording scene and boot prompt
layout are no longer daily source assertions; verify these product interactions
on device. No C behavior scenario was removed.

PowerShell assertions use `host_test_lib.ps1`; Python harnesses use
`host_c_test_lib.py`. Do not nest test entry points inside contract scripts or add
another framework. The shared C extractor skips declarations/comments/literals;
it is not a C preprocessor. Failed C assertions retain their generated source in
`cache/host-test-failures/` for diagnosis.

## Dependencies and limits

Requires Windows PowerShell 5.1, Python with `llvmlite`, and JL clang at
`C:/JL/pi32/bin/clang.exe`. Missing dependencies fail the affected modules.
Keep PowerShell scripts ASCII or UTF-8 with BOM for PowerShell 5.1 compatibility.
The shared C compiler invocation has a 60-second timeout; JIT execution has no
watchdog. Run suites serially because some harnesses use fixed cache paths.

Contracts inspect source; C harnesses extract production code and mock hardware,
OS, storage or DSP. Some use native host compilation, others retarget JL LLVM IR.
Neither validates target ABI, JL scheduling, binary library behavior or timing.
A/B keymap source checks do not constitute a power-loss recovery test.

Firmware builds and device regression remain required: HOGP receipt/latency/power,
IRQ/disconnect races, audio quality, SD power loss, USB enumeration and long runs.
See [HOGP plan](../../docs/2-6.HOGP长按持续输入实施方案.md),
[recording plan](../../docs/9-1.会议录音双声道改单声道实施方案.md) and
[DUT plan](../../docs/8-4.工厂APP按键与喇叭DUT测试实施方案.md).

## Optional board check

`python tests/board/factory_cdc_bytes.py --port COM6` requires `pyserial`, a DUT
and CDC TEST firmware. Use the CDC port, not the debug log port. This independent
tool checks startup bytes and four echoes; host validation never opens a port.
See [factory USB protocol](../../docs/8-0.厂测USB通信协议设计.md).
