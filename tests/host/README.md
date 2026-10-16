# Host validation

Run from the repository root (also the VS Code `test: host software` task):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\host\run_host_tests.ps1
```

The entry point runs four source-contract scripts and ten behavioral suites
as **14 independent processes**. A failed module does not suppress the others;
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
| `test_hogp_hold.py` | Scan/adapter/FIFO, hold/fast edges, offline clicks, KEY5 HID/recording gesture coexistence and disabled mapping, hot-apply release/rollback, ATT context, stale owners, RDX release, cancellation and failed Up recovery; `key5_record_checks.py` checks local hold/double recording ownership, playback exclusion, failed starts and release retries; KEY1/F13、KEY2/F14 单击反馈分流及 `led_key_feedback_checks.py` 中真实灯效引擎的颜色、超时、优先级、绑定后静默广播与实时恢复 |
| `test_factory_usb.py` | CDC independent of DUT/key/SD health, MSC ownership/shutdown, stale sessions, partial I/O and bounded DMA; includes `test_factory_protocol.py` for CRLF framing, invalid-frame isolation, response/mailbox backpressure, timeout recovery, bounded identity fields, session-scoped timer lifecycle and failed-notification recovery |
| `test_finalpack.py` | Packaging commit order, VM/format/queue/timer failures, business unbinding, key DUT recovery, completion reminder loop/volume/shutdown and packaged tone presence |
| `test_dut_keys_speaker.py` | Parsing/ownership, key state reset, queue overflow/legacy cleanup, recording authorization, speaker ACK/stop ordering, volume rollback and PCM partial writes; local pink-noise toggle/isolation, low-volume/fade/stalled-stop cleanup, counter wrap and generated-PCM octave spectrum |
| `test_record_storage.py` | Async frame ownership/order, simulated 220ms/2s storage stalls, saturation, metadata/write failures, pause/stop drain and two-hour mono/stereo frame counts |
| `test_record_format.py` | Real cJSON metadata, DAT merge, identity conflicts, failed I/O and interrupted transaction replay; allocation failure preserves files; `file_delete_checks.py` executes APP deletion and the actual LLVM cache handoff for rejected requests, queue failures and cache/I/O failures; `recovery_regression_checks.py` executes the patched archive for empty busy results, cache load failures, flush retry, invalidated-cache reload/retry and sparse-ID playback navigation |
| `test_power_recovery.py` | Actual DIP controller, native power entry and idle completion: debounce, stalled app_core, fixed OFF deadline, cold charging, ordinary shutdown and accepted OFF→ON restart; legacy storage power ABI delegates to the PA4 manager |

The CDC suite, test_cdc_file.py, runs the actual C memory/recording-source
producer, common parser and transport queue, plus the Python receiver. It covers
golden CRC/frame vectors, 1/2/4/8 KB blocks, partial-frame cancellation, stale
sessions, TX-full control reception, coalesced wakeups and bounded output.

The HOGP harness also checks event-only offline first-key/multi-click/KEY5
gestures, idle with no 10 ms timer, HID-ready start/stop, and queue/timer failure
recovery through the priority-0 product bridge (no idle system polling). CDC checks cover DTR-scoped protocol
timers, transport state notifications and detached audio/file cleanup. Neither
module needs a persistent 10 ms service without its valid session. The former
100 ms product system timer is removed; the priority-0 bridge posts only actual
work and does not force wakeups. USB transport lifecycle remains separate.
These scheduling checks do not prove on-device current or latency; see the
[disconnected power policy](../../docs/6-0.T2620外设低功耗与共享VDD电源管理设计.md)
and [CDC lifecycle](../../docs/8-2.工厂CDC通信基础与实施记录.md).

DUT 调度检查覆盖未连接、BLE 已连接、USB 在线和 DUT 模式内无待办停表；
执行实际初始化、物理键/命令入口、事件合并及处理中再次通知、消息与定时器失败恢复、
固定截止时间、旧超时回调、计数回绕、录音停止投递重试上限及迟到保存结果。
按键等待和喇叭稳定播放不保留通用服务周期；PCM 供数只在启动/结束通知，录音只在首帧通知，
包装提示音只在间隔阶段安排单次超时。生产者通知接线另做源检查；硬件线程时序、
USB/音频实际延迟和连接前后电流仍须板测，不由这些模拟用例证明。

Power contracts also reject the obsolete PA0-as-VDD alias and direct PA0 GPIO
use in RDX, preserving ADC0/ADC3's JL MIC_LDO selection. The storage ABI harness
checks wake/idle delegation and failed-wake retry without a second timer or power
state. These checks cannot prove PA0/MIC rail voltages, audio quality or current;
board tests must distinguish PA0 MIC power, PA4 SD/RGB power and PE5 amplifier enable.

The CDC suite's cdc_source_checks.py harness executes the recording worker with two read
buffers, delayed completion, read/close failures, cancellation, quarantine,
deadlines and bounded shutdown. Record-format tests cover the public in-flight
gate; archive regression tests cover queued format admission and locked DAT
cache handoff. LIST checks cover the private DAT reader, unaligned JSON payload,
empty/invalid/duplicate lists, unsupported entries, refresh/quit selection and
LIST-to-GET on one serial connection. Host simulations do not establish on-device concurrency timing.
Large-file regressions stream 143,340,000 synthetic bytes through the Python
receiver to disk with CRC/SHA-256 checks and simulated elapsed time above 120
seconds. The C worker streams the same length with fixed buffers; producer and
format tests cover offsets/lengths above 16 MiB and 2 GiB through the aligned
u32 boundary, idle timeout and timer wrap, large-offset cancellation, legacy/new
CAPS negotiation, and preservation of the short factory-recording limit.
These simulations do not certify physical large-file storage or USB transfer.
The historical 143 MB single-run board result does not replace large-file repeat,
independent-source comparison or regression of the new session-scoped scheduling.
The `cdc_conflict_checks.py` harness executes admission predicates and entry
reservations extracted from the patched protocol archive, deferred BLE list
admission and production OTA dispatch. Additional cases cover post-acquire CDC
conflicts, index activity independent of the CDC lease, and reset rejection.
These are targeted gate tests, not a simulation of the entire BLE parser or JL
scheduler. The patched archive now contains protocol and UXFILE modifications.
It does not establish physical USB timing, storage throughput or 600 KB/s.
See [CDC file transfer](../../docs/10-4.CDC通用文件传输业务方案.md),
sections 6.3–6.4 for board validation and section 7 for historical performance.

## Keeping the suite small

Full-volume regression: `test_record_storage.py` verifies the mono/stereo
admission budget, normal low-space STOP, exact accepted-tail drain, resampling
with in-flight frames, reuse after deleting files, explicit-format fault reset,
and truthful APP format ACKs. `test_record_format.py` checks the 1 MiB minimum
reserve, two-index-copy growth budget, KiB conversion/overflow/unknown queries,
and exclusive format admission from failed storage. The actual archive tests
exercise erase failure, cache-reload/VM-marker failure, dirty-cache suppression,
retry and exactly-once completion callbacks. Hardware acceptance must still
cover near-full long recording, STOP/reboot/list/download, explicit formatting
of the reported failed volume, and immediate recording after format. No host
test erases a physical device.

The CDC cleanup keeps one suite entry, `test_cdc_file.py`, and two focused
helpers: `cdc_source_checks.py` owns worker lifetime/failure behavior;
`cdc_conflict_checks.py` owns patched protocol admission and OTA cleanup.
Storage leases, verified RAW/DAT readers and UXFILE patches remain in the
existing record-format/recovery suite rather than being duplicated here.
Receiver CLI cases share one command-response serial fixture. Recording
fragmentation uses representative split/coalesced cases rather than a full
block-size/chunk-size Cartesian product; the shared CDF1 tests own wire-format
boundaries. Real-file producer behavior runs with BENCH disabled; the enabled
build owns memory vectors and block sizes. Both builds remain covered.
Unused fault-injection scaffolding and parser call-count assertions are removed;
archive behavior and legacy failure-response guards remain. Host checks still
cannot certify USB throughput or scheduler races; those require board tests.

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

Local DUT burn-in: KEY5 double-click starts/stops approximate pink noise;
eight-click exits DUT. The existing factory `FT_SPEAKER` tone is unchanged.
Playback uses half the configured music-volume steps (rounded up), not half
acoustic power, plus an independent half-scale PCM peak limit. The signal is
generated continuously at 48 kHz without SD files, with 10 ms start/stop ramps
and a roughly 30 Hz DC/subsonic high-pass. Normal stop drains the current PCM
block before a final fade block; a stalled DAC abandons the fade after ten
zero-write retries so shutdown can proceed.

The generated-PCM FFT checks a broad pink PSD slope over 125 Hz-8 kHz, not a
calibrated full-band acoustic response. On-device acceptance must include
measured output level/temperature, a long soak, repeated double-click on/off,
eight-click exit, OFF during playback, BLE connection/disconnection, CDC/BLE
speaker and key-test exclusion, and restoration of the previous volume/mute.
Verify the selected level against the actual speaker rating before a long soak.
Local burn-in occupies the shared SPEAKER resource; factory speaker commands
cannot replace or stop it, but DUT exit/power-off retain their cleanup authority.
KEY5 remains local in DUT even with BLE connected; ordinary recording/HID
behavior and factory key capture stay fenced.

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
[DUT plan](../../docs/8-1.工厂APP按键与喇叭DUT测试实施方案.md).

## Optional board check

正式 CDC 协议终端工具见 [board README](../board/README.md)，其唯一源码为仓库根目录的
单文件 `CDC-test.bat`，由 `host_c_test_lib.load_cdc_tool()` 加载后测试。提供基础检查、受固件能力
门控的交互控制流程和完整身份 JSON 报告。`test_factory_station.py`
由 factory USB 组调用，覆盖旧固件阻断、超时不重发、LAST 覆盖、AUTH 完整显示/保存及控制顺序；
使用模拟串口，不打开硬件。工具已接入录音、文件和独立包装流程；本段的主机模拟覆盖不等于完整实板验收，业务证据及剩余回归见 board README 和关联 CDC 文档。

`python tests/board/factory_cdc_bytes.py --port COM6` requires `pyserial`, a DUT
and CDC TEST firmware. Use the CDC port, not the debug log port. This independent
tool checks startup bytes and four echoes; host validation never opens a port.
See [factory USB protocol](../../docs/8-2.工厂CDC通信基础与实施记录.md).

Power recovery executes the real DIP implementation in 12 scenarios with MSC both disabled and enabled: stable idle without system timers or empty posts, timed debounce, stalled app_core, fixed OFF deadline, cold charging/ON entry, bounded mode retries, failed posts/timer allocation, stale callbacks, re-notification, recovery flags and clock wrap. Native shutdown and legacy PA4 delegation are checked separately. USB policy tests cover three-attempt exhaustion, event-independent deadlines, 300 ms reenumeration across wrap and unplug cancellation. Recording recovery covers damaged metadata, retained RAW tails, orphan metadata, duplicate identities, transactional deletion/retry and listing/deletion beyond 300 entries. App rendering and hardware timing/current require device validation.

BLE 连续下载、会议单声道及 RAW 回放特性已实板验收并收敛，对应的行为套件（`test_ble_download.py`、`test_meeting_mono.py`）及 `test_rdx_keymap_contract.ps1` 静态令牌检查已移除；下载策略结论见提交 366ce688，keymap 在线事务回归由 `test_keymap_factory_reset.py` 与实板验收覆盖。

CDC 主机检查已收敛到正式配置（TEST=0、LOG_LEVEL=1）：保留生命周期、传输/背压/旧会话隔离、驱动边界、协议分帧/身份及终端主流程；不再运行配置组合矩阵、历史 TEST=1 图样或日志统计内部细节。配置可编译性通过正常固件构建验证，未覆盖的宏组合需另行验证。

CDC 通用文件传输发送调度专项测试另覆盖空闲入队唤醒、在途入队不重复投递、半深度低水位边沿通知/重新武装、尾包排空补货、满 TX 时接收取消、投递失败定时补偿及会话重置清理。使用真实传输 C 源码与模拟端点，不能据此认定硬件吞吐提升或真实中断时序通过。

第二轮专项覆盖文件流 IN IRQ 单包续传、忙端点保护、字节顺序/完成计数、文本 ZLP 边界、任务内补货与 RX、复位/关闭/关机撤销、超时及丢通知补偿；另执行真实 cdc_tx_wakeup 和驱动提交函数，验证中断屏蔽与重新使能。实板需核对 irq_fast、吞吐及取消/拔插/OFF，不能用模拟测试替代。

第四次合包无明显吞吐收益，已撤回合包及专属测试。保留 IN IRQ 续发、逐队列项短包边界、字节顺序、完成统计、拒绝提交不消费及 stream/ZLP 回归。DATA、END、ERROR 均使用流发送接口。保留坏帧头有界证据测试，不据此宣称历史 invalid binary header 已修复。

瓶颈诊断回归新增 deferred 接收后完整校验：多块/分片、完整性、帧 CRC、模式、END 文件 CRC、错误帧/ID/长度、截断及取消互斥；工站路由覆盖菜单 4/5 与参数透传。真实传输 C harness 运行统计关闭、仅统计、统计与计时三种配置，并补测 LOG_LEVEL=0 的全关及统计/计时组合；关闭统计仍执行相同传输安全断言，开启时另验证计数及耗时累加，不验证硬件计时精度、总线间隔或实板吞吐。
