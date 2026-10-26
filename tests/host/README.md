# Host validation

Run from the repository root (also the VS Code `test: host software` task):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\host\run_host_tests.ps1
```

The entry point runs four source-contract scripts and eighteen behavioral suites
as **22 independent processes**. A failed module does not suppress the others;
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
| `test_keymap_factory_reset.py` | F13/F14/F17/F18/F19 defaults, saved-map preservation, real A/B persistence, reset/reboot and write/readback/apply failures; configured maximum playback volume and busy/write failures, including playback-disabled builds |
| `test_record_tone.py` | Real `rectone` GET/SET parser and VM/default/reset behavior, same-value durability, busy rejection, queue/IO/send failures and original-token request/ACK isolation; recorder start/stop behavior belongs to the existing session harness |
| `test_hogp_hold.py` | Real scan/adapter, cycle snapshots, independent HID/local queues, plain-F19 stable-Down startup on any mapped key, cancellation, hot-map cleanup and retained KEY5 DUT intent; `key_record_dispatch_checks.py` owns usage routing, F18/F19 admission, stop-before-DUT and post-release re-entry; `record_key_session_checks.py` owns real recorder routes, meeting persistence versus inspiration streaming, late replies, worker STOP/READY fences and shared recording-tone snapshots for APP/F18/F19, online/offline and CHAT/CALL, plus factory/DUT isolation; `led_key_feedback_checks.py` owns 2 s indications, recording-kind restore and protected priorities |
| `test_factory_usb.py` | CDC independent of DUT/key/SD health, MSC ownership/shutdown, stale sessions, partial I/O and bounded DMA; includes `test_factory_protocol.py` for CRLF framing, invalid-frame isolation, response/mailbox backpressure, timeout recovery, bounded identity fields, session-scoped timer lifecycle and failed-notification recovery |
| `test_finalpack.py` | Packaging commit order, VM/format/queue/timer failures, business unbinding, key DUT recovery, completion reminder loop/volume/shutdown and packaged tone presence |
| `test_dut_keys_speaker.py` | Parsing/ownership, key state reset, queue overflow/legacy cleanup, recording authorization, speaker ACK/stop ordering, volume rollback and PCM partial writes; local pink-noise toggle/isolation, low-volume/fade/stalled-stop cleanup, counter wrap and generated-PCM octave spectrum |
| `test_record_storage.py` | Async frame ownership/order, simulated 220ms/2s storage stalls, saturation, metadata/write failures, pause/stop drain and two-hour mono/stereo frame counts; control-path `rdx_record_storage_admit` (-1 busy/fault, -2 reserve, balanced I/O lease), both `run_init` variants returning the space code, and `sink_dev1` failed-init START returning 0 with frames dropped, one fault/full latch and one paired exit |
| `test_record_format.py` | 使用真实 cJSON 验证元数据、DAT 合并、身份冲突、IO 失败与中断事务重放；覆盖 SN 迁移、VM 失败、空会话取消、短录音 RAW/MTA 清理及显式格式化复位；`file_delete_checks.py` 执行 APP 删除与实际 LLVM 缓存交接；`recovery_regression_checks.py` 覆盖实际归档库的收尾与准入、重载与重试、SN 补丁接入及混合编号按 DAT 顺序回听 |
| `test_power_recovery.py` | Actual DIP controller, native power entry and idle completion: debounce, stalled app_core, fixed OFF deadline, cold charging, ordinary shutdown and accepted OFF→ON restart; legacy storage power ABI delegates to the PA4 manager |
| `test_connected_power.py` | 实际 PA4 管理器：单/双 ACL 的 120 秒窗口、忙时停表、解析占用不重计、真实活动重计、旧回调与计数回绕、三种停用档位、关断边界竞争、USB/充满/关机门禁、失败保电与恢复、未连接慢广播边界 |
| `test_connected_params.py` | 实际 RDX 快档协调器：长时间空闲不降档、下载结束及旧参数索引均不恢复慢档、实际快档范围、延迟/拒绝时有界重试和停表、逻辑会话切换不刷新物理 ACL 预算、迟到慢参数恢复、HID 同链保护与异链隔离、RDX 退出后的 HID 快档收尾与跨 peer 接管、重连/旧回调隔离、消息/定时器失败恢复及计数回绕；协商、交互、射频与电流仍须板测 |
| `test_ble_entry.py` | 实际第二入口协调器、广播/连接适配与退出函数：120 秒到期、持续业务不续期、真实业务/物理键重开且纯 HID 不恢复 PA4、双 ACL/零 ACL、关闭两阶段确认与在途建链、软件缓存提前清零、超时/错误保守锁定、开放前取消、退出/重初始化及旧任务隔离、队列/定时器失败与回绕；固定已审阅 SDK 库哈希，不替代实际 HCI/RF 时序、睡眠与电流板测 |
| `test_cache_flush.py` | 实际缓存协调器：干净停表、固定 5 秒期限、重复修改不续期、同步清除、临时忙和故障、队列/定时器失败恢复、生命周期隔离及回绕；实际库的 dirty 通知/准入/有界互斥由 `test_record_format.py` 中的 `cache_flush_library_checks.py` 执行 |
| `test_protocol_recv.py` | 实际补丁库接收任务/生产者：无限等待、等待前/期间及消费中入队、自通知连续排空、空唤醒不提频、真实数据分派前提频、分配/入队/出队/通知失败、计数饱和、满队列旧包清理及零连接句柄下的 FIFO 屏障传输；`clock_manager_checks.py` 执行实际观察器调度，覆盖 `app_core` 归属、7 轮后停表/业务重启、刷新期限、已排队旧超时、回绕与分配失败 |
| `test_amp_output.py` | PE5 = DAC 模拟就绪 && 数字输出运行：对照 `media.a` 校验 `_audio_dac_status_hook` INIT/START/STOP ABI、最后通道 stop、淡出与互斥顺序；冷/热启动、录音占用、模拟关闭作废输出、未知状态、临界区及 `[AMP]` 诊断；复用消息缓冲并污染返回值/信号量标志，验证完整 `Q_CALLBACK` 消息与失败重投。MIC、功放时序与电流仍需板测 |

`test_lowpower_lrc.py` 验证独立 LRC 正常业务配置：真实工具配置与项目覆盖仅允许
产品开关及原生睡眠时钟类型不同，非法值、关闭原生低功耗或混入非零实验开关必须失败。
实际电源参数初始化和 `board_power_init()` 在模拟原生 API 下执行，覆盖 LRC/BT_OSC、
充电开关、初始化先于单次快照、按键唤醒不变，以及原生选择不同于默认值时只观察不强制。
默认保持 LRC=1，从启动配置所有状态的睡眠策略，不随快慢广播切钟；保留按键、USB、
UART 和动态调频，组合探针及其套件仅在实验分支。

2026-10-19 用户确认锁定正常业务版未连接慢广播 **0.6–1.6 mA 波动**，证据为
`COM3_2026-10-19_20-11-56.log` 与仪表反馈；日志支持低功耗后建链、一次 APP 录音
停止保存及断连后再次慢广播/PA4 OFF。电流不是主机测试推算，也不是积分平均值；
完整 USB/首键恢复、HID/双 ACL、RF/计时/温压长稳仍需资格验证。后续降耗项按用户决定
**暂不执行，仅保留方向**，不补做 A/B 包。详见
[6-0 第 5 节](../../docs/6-0.T2620外设低功耗与共享VDD电源管理设计.md)。

The CDC suite, test_cdc_file.py, runs the actual C memory/recording-source
producer, common parser and transport queue, plus the Python receiver. It covers
golden CRC/frame vectors, 1/2/4/8 KB blocks, partial-frame cancellation, stale
sessions, TX-full control reception, coalesced wakeups and bounded output.

第二入口测试固定当前 `btstack.a` / `btctrler.a` 的 SHA-256，防止 SDK 公共广播句柄
编号、命令缓冲生命周期及完成事件 ABI 变化后继续误用模拟结果；升级库必须重新审阅，
不能只替换哈希。关闭使用 `0x2039` 停广播、`0x203c` 移除确认，再用第二次移除完成
隔离 SDK 清理命令。退出在 `app_core` 有界排空；未确认的故障保留保护且禁止重开直到重启，
不能把退出/重新初始化或迟到完成当成恢复证据。此检查不等于执行真实 controller。

2026-10-19 第二入口单 RDX 核心路径已板测：两次关窗后的睡眠、列表唤醒重开和窗口内
不续期，用户实测入口开放/关窗待机约 4/2 mA，确认锁定阶段收益。第二手机物理接入后
因 RDX runtime 已占用而被拒绝接管，随后退回单 ACL 并重开窗口，也已覆盖一次。
此记录不代表持续录音/下载、HID/双 ACL、关闭故障、退出或长稳板测通过；完整边界见
`docs/6-2.T2620BLE连接后低功耗设计.md` 第 1.1、4.4 节。

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

F19 stable-Down device regression (2026-10-24): configure any KEY1-KEY5 to
plain F19 and check startup without waiting for LONG, release during APP/tone
startup, normal stop, hot remap and RDX loss. Recheck F13/F14 click/seek,
F18 toggle/mark and ordinary HID hold/release. If physical DUT entry is enabled,
KEY5 eight-click must finish its owned F19 cleanup before entering; disabled
entry stays disabled. Preceding short F19 requests/recordings are accepted.
Use `[F19_DOWN]` (stable `down_ms`, app_core `wait_ms`) and `[F19_CANCEL]`
alongside existing recorder logs. READY/submit is not APP receipt; physical
press-to-UI latency and audio quality still need real device/APP evidence.

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

连接态第二阶段同时扩充现有套件：录音格式准入验证先占用再恢复、失败释放、解析/缓存空检查不发布活动；
实际补丁库验证脏缓存、持久读句柄、删除队列及 IO 错误阻止停用，干净缓存不进入保存业务，
查询恢复失败返回独立空对象，GETLIST 带占用投递及队列失败响应，UXFILE bit29 标记不破坏内部消息号。
这些测试不能证明真实 SD 挂载时序、文件耐久性、首键延迟或功耗；板测要求见
[连接待机设计](../../docs/6-2.T2620BLE连接后低功耗设计.md)。

`test_cache_flush.py` 执行实际缓存调度器：干净态无定时器、脏缓存 5 秒固定期限、重复修改不续期、
同步保存取消待办、临时忙重试、故障停表、队列/定时器失败经现有桥接恢复、退出/重初始化、
旧回调隔离与时间回绕。`test_record_format.py` 内的 `cache_flush_library_checks.py` 提取实际
链接库，检查全部 14 处 dirty 写入通知、旧周期链删除及生命周期接线，并执行实际准入/保存包装。
清洁状态不访问存储，获取锁最多等待 1 个 OS tick，超时不保存；真实 IO 失败保留脏位及错误，不修改 JL USB 检测。
主机替身不证明真实调度器并发、断电耐久性或 mA 收益，须独立板测。

RGB 调度回归在 `led_key_feedback_checks.py` 中执行实际硬件恢复入口及灯效引擎，
模拟创建任务、目标任务、系统消息和真实毫秒期限：协议任务恢复后 OFF 不建表，
动态灯效固定在 `app_core`，常亮停表，短提示仅在到期处理，低电提醒独立保留；
覆盖多轮恢复、旧回调、请求乱序、时间/请求序号回绕、关机与分配失败。
队列和备用单次超时同时失败时，已有 DIP 优先级 0 桥接恢复灯效待办，
不会另建常驻轮询，也不会丢掉停止灯效的请求。绑定通知只在持久化成功后发送。
这些用例不等于执行了 JL 二进制调度器；首次 GETLIST 和实际灯效仍需板测。

接收任务测试直接提取当前链接库的 LLVM 函数，不另写接收循环模型；主机只在模拟的
无限阻塞点终止该轮执行，OS、队列内存分配及命令解析仍是替身。逻辑退出/断连的完整
所有权状态机由生命周期契约约束，接收测试只证明同一生产者的 FIFO 屏障可唤醒并排空，
不等于完成真实协议解析或 JL 调度器并发测试。调频测试验证任务归属和观察期限，
不模拟底层频率计算、RF 或电流；板测必须覆盖观察器停表后首次业务，以及业务后的再次降频。

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

CDC / online-F19 coverage stays in the existing suites. Usage dispatch owns
plain-F19 routing/readiness; `record_key_session_checks.py` owns both startup
orders, LIST/GET during pending/tone/active/pause, no-storage cleanup, ordinary
recording exclusion, stale owners and post-acquire conflicts. One shared fixture
covers both process variants and the worker guard: CDC deadline/error/cancel,
STOP/READY cleanup, stale work, queue retry, timer exhaustion and release races.
`cdc_source_checks.py` owns reader health/deadlines; archive snapshot/cache
behavior remains in record-format/recovery tests, without a second LLVM model.
Contracts retain caller wiring rather than repeating these behavioral checks.
USB/BLE timing, APP audio, integrity and throughput require the
[board checklist](../board/README.md); host success is not device acceptance.

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
KEY1–KEY5 are F13/F14/F17/F18/F19. Power-cycle again and query the map to check persistence.
Repeat when the map is already default and with a held HID key; verify no stuck
key remains. The shared packaging reset must restore the same map without
rebooting. Storage failure must return failure and must not schedule a reboot.
Factory reset fences further App keymap commands until init/reboot; the dedicated
PC `RESET_KEYMAP` continues to support online editing without this fence.

Keep stable protocol/configuration boundaries and failure-path behavior. Prefer
observable outcomes over variable names, comments, exact call counts or copied
implementation sequences. Keep source-only integration guards where mocks do not
execute the caller; counts protecting two distinct recording variants remain.

The 2026-10-22 key-binding cleanup gives each boundary one behavioral owner:
STOP tests share the recorder fixture instead of a second set of mocks, and
source contracts retain only unexecuted caller wiring. Duplicate KEY5/input and
LED deadline cases are consolidated. Unchanged playback-engine and boot-volume
scenarios are not reintroduced; usage dispatch, volume reset and LED behavior
remain covered. No new top-level suite is added.

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
