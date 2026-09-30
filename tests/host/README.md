# Host validation

Run from the repository root (also the VS Code `test: host software` task):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\host\run_host_tests.ps1
```

The entry point runs four source-contract scripts and nine C behavioral harnesses
as **13 independent processes**. A failed module does not suppress the others;
the exit code is 1 if any module fails. Use `-Suite contracts` or `-Suite behavior`
for focused runs; the default `all` remains the complete check. Each file can also
run directly. Individual PowerShell scripts accept `-Verbose` for assertion names.

## Coverage

| Source contract | Essential boundary |
| --- | --- |
| `test_t2620_product_contract.ps1` | Product configuration, power/charging, bounded shutdown, recording admission and SDK integration |
| `test_hogp_profile_contract.ps1` | HID protocol bytes, security, readiness and peer-scoped bonded CCC |
| `test_rdx_transport_contract.ps1` | Dual-wrapper ownership, sends/retries, advertising and persisted binding |
| `test_rdx_lifecycle_contract.ps1` | FIFO/worker reconnect barrier, RDX-only release, OTA cancellation and recording STOP |

| C behavioral harness | Production behavior exercised with mocked boundaries |
| --- | --- |
| `test_adv_policy.py` | Production offline advertising state machine: 120-second idle window, overlapping/pending/short business, paused/busy snapshots, stale timers, wraparound, deferred modes, queue/timer/radio/restore failures and tagged file-worker admission; RF and physical power remain on-device checks |
| `test_keymap_factory_reset.py` | Factory HID defaults survive reload; A/B write/readback/commit and apply failures preserve old keys; retry, revision overflow, idempotence and reset ACK/reboot ordering |
| `test_hogp_hold.py` | Scan/adapter/FIFO, hold/fast edges, offline clicks, KEY5 HID/recording gesture coexistence and disabled mapping, hot-apply release/rollback, ATT context, stale owners, RDX release, cancellation and failed Up recovery; `key5_record_checks.py` checks local hold/double recording ownership, playback exclusion, failed starts and release retries |
| `test_factory_usb.py` | CDC independent of DUT/key/SD health, MSC ownership/shutdown, stale sessions, partial I/O and bounded DMA; includes `test_factory_protocol.py` for CRLF framing, invalid-frame isolation, response/mailbox backpressure, timeout recovery and bounded identity fields |
| `test_finalpack.py` | Packaging commit order, VM/format/queue/timer failures, business unbinding and key DUT recovery |
| `test_dut_keys_speaker.py` | Parsing/ownership, key state reset, queue overflow/legacy cleanup, recording authorization, speaker ACK/stop ordering, volume rollback and PCM partial writes |
| `test_record_storage.py` | Async frame ownership/order, simulated 220ms/2s storage stalls, saturation, metadata/write failures, pause/stop drain and two-hour mono/stereo frame counts |
| `test_record_format.py` | Real cJSON metadata, DAT merge, identity conflicts, failed I/O and interrupted transaction replay; allocation failure preserves files; `file_delete_checks.py` executes APP deletion and the actual LLVM cache handoff for rejected requests, queue failures and cache/I/O failures; `recovery_regression_checks.py` executes the patched archive for empty busy results, cache load failures, flush retry, invalidated-cache reload/retry and sparse-ID playback navigation |
| `test_power_recovery.py` | Actual DIP controller, native power entry and idle completion: debounce, stalled app_core, fixed OFF deadline, cold charging, ordinary shutdown and accepted OFF→ON restart |

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

正式 CDC 协议终端工具见 [board README](../board/README.md)，其唯一源码为仓库根目录的
单文件 `CDC-test.bat`，由 `host_c_test_lib.load_cdc_tool()` 加载后测试。提供基础检查、受固件能力
门控的交互控制流程和完整身份 JSON 报告。`test_factory_station.py`
由 factory USB 组调用，覆盖旧固件阻断、超时不重发、LAST 覆盖、AUTH 完整显示/保存及控制顺序；
使用模拟串口，不打开硬件。完整录音/文件/包装流程尚未实现。

`python tests/board/factory_cdc_bytes.py --port COM6` requires `pyserial`, a DUT
and CDC TEST firmware. Use the CDC port, not the debug log port. This independent
tool checks startup bytes and four echoes; host validation never opens a port.
See [factory USB protocol](../../docs/8-0.厂测USB通信协议设计.md).

Power recovery executes the real DIP implementation with mocked pins and independent timers: debounce, stalled app_core, fixed deadline, OFF/ON latch and cold USB/battery behavior. Recording recovery covers damaged metadata, retained RAW tails, orphan metadata, duplicate identities, transactional deletion/retry and listing/deletion beyond 300 entries. App rendering and hardware timing/current require device validation.

BLE 连续下载、会议单声道及 RAW 回放特性已实板验收并收敛，对应的行为套件（`test_ble_download.py`、`test_meeting_mono.py`）及 `test_rdx_keymap_contract.ps1` 静态令牌检查已移除；下载策略结论见提交 366ce688，keymap 在线事务回归由 `test_keymap_factory_reset.py` 与实板验收覆盖。

CDC 主机检查已收敛到正式配置（TEST=0、LOG_LEVEL=1）：保留生命周期、传输/背压/旧会话隔离、驱动边界、协议分帧/身份及终端主流程；不再运行配置组合矩阵、历史 TEST=1 图样或日志统计内部细节。配置可编译性通过正常固件构建验证，未覆盖的宏组合需另行验证。
