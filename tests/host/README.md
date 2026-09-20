# Host contracts

Run from the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\host\run_host_tests.ps1
```

VS Code `test: host software` uses the same entry point. The runner retains five
explicit groups; each can also run directly. Add `-Verbose` for individual
successful assertions. Failures identify the check, remaining groups still run,
and the final exit code is 0 on success or 1 on failure.

## Coverage

| Group | Coverage |
| --- | --- |
| `test_t2620_product_contract.ps1` | Configuration ownership, power, charging, storage, recording and optional USB; also runs the six C behavioral harnesses below |
| `test_hogp_profile_contract.ps1` | GATT/report bytes, encryption, readiness and peer-scoped bonded CCC |
| `test_rdx_transport_contract.ps1` | Two-wrapper topology, owner-scoped routing, binding and advertising |
| `test_rdx_lifecycle_contract.ps1` | Reconnect barriers, worker cleanup, RDX-only release, offline STOP replies and OTA cancellation |
| `test_rdx_keymap_contract.ps1` | Owner-bound transactions, verified A/B storage and release-before-apply |

| Behavioral harness | Production code exercised with mocks |
| --- | --- |
| `test_factory_usb.py` | USB admission/shutdown, final CDC configuration, queues, session invalidation, bounded DMA I/O and logging |
| `test_finalpack.py` | Packaging commit order, business unbinding, VM write/readback failures, format/queue/timer failures and key DUT recovery |
| `test_dut_keys_speaker.py` | Factory layout/press identity, strict parsing, token/CCC guards, stale events, real scan/adapter multi-click and hold reset, queue overflow through legacy LED/motor/WiFi/record cleanup with format ownership retained, recording stop barrier, speaker completion ACK ordering, maximum volume/mute restoration, sine phase through partial writes and 16/24-bit mono/stereo output |
| `test_record_storage.py` | Recording aggregation, byte preservation across blocks, tail flush and write failures |
| `test_meeting_mono.py` | Fixed/dynamic mono integration, PCM guards, allocation cleanup, selector ownership/failures, pairing timeout, stale fault isolation and channel reporting before worker startup |
| `test_record_format.py` | Format metadata and real cJSON, DAT merge, healthy boot without writes, legacy fields/marks, identity conflicts, failed I/O and interrupted transaction replay |

PowerShell source assertions share `host_test_lib.ps1`. Python harnesses share
`host_c_test_lib.py` for source extraction and JL C/LLVM execution; test modules
do not import each other. Keep checks in the relevant group or harness rather
than adding another daily entry point.

## Dependencies and limits

Requires Windows PowerShell 5.1, Python with `llvmlite`
(`python -m pip install llvmlite`), and JL clang at
`C:/JL/pi32/bin/clang.exe`. Missing dependencies fail rather than skip checks.
The product group invokes Python in UTF-8 mode.

Most contracts inspect source. Behavioral harnesses execute production C against
mock hardware, storage or DSP. The format harness uses the host compilation target
for libc allocation and pointer-sized cJSON structures; the DUT harness also uses
the host target and Windows CRT text formatting. Other harnesses use
JL IR retargeted for host execution. Neither proves target ABI or real-time behavior.

Firmware builds and device acceptance remain necessary for real Opus/selector
behavior, APP decoding, SD flush/power loss, audio quality, USB enumeration,
interrupt concurrency, disconnect races and long runs. Recording evidence and
remaining acceptance items live in
[the mono implementation plan](../../docs/9-1.会议录音双声道改单声道实施方案.md).

Factory acceptance remains pending for physical key positions, small-MTU APP
text reassembly, real DAC start/stop timing, speaker loudness and continuous tone,
RDX/HID coexistence and DIP OFF cleanup. The DUT harness mocks SDK audio and BLE;
it does not prove sound output or IRQ/task scheduling. See
[the DUT implementation plan](../../docs/8-4.工厂APP按键与喇叭DUT测试实施方案.md).

The DUT harness also executes the recording authorization and audio-start guard:
unbound CHAT/CALL admission, unchanged ordinary binding gates, stale owner and
recording generations, revoke/stop cleanup, startup failure, and speaker STOP
completion before unbound recording. Hardware acceptance must still verify
recording audio, saved-file retrieval and disconnect/exit during the start tone.

## Optional board check

`tests/board/factory_cdc_bytes.py` is a separate physical CDC echo tool, requiring
`pyserial`, a connected DUT and CDC TEST firmware:

```powershell
python tests/board/factory_cdc_bytes.py --port COM6
```

It runs four short echo checks. Use the DUT CDC port, not the debug log port.
The host suite never opens a serial port. Board setup and acceptance records:
[factory USB protocol](../../docs/8-0.厂测USB通信协议设计.md).
