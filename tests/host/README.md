# Host contracts

Run from the repository root (dependencies listed below):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\host\run_host_tests.ps1
```

The VS Code `test: host software` task uses the same entry point. Output shows
one result per group and a summary. Add `-Verbose` to see individual successful
assertions. Failures always include the contract name and reason; remaining
groups still run. Exit code is 0 on success and 1 on failure.

| Script | Coverage |
| --- | --- |
| `test_t2620_product_contract.ps1` | Configuration ownership, power sequencing, recording safety, charging, storage preservation and optional USB MSC ownership |
| `test_hogp_profile_contract.ps1` | GATT handles/report bytes, encryption, ready state and peer-scoped bonded CCC |
| `test_rdx_transport_contract.ps1` | Two-wrapper topology, owner-scoped routing, binding and advertising |
| `test_rdx_lifecycle_contract.ps1` | Reconnect barriers, worker cleanup, RDX-only release, offline recording STOP replies and OTA cancellation |
| `test_rdx_keymap_contract.ps1` | Owner-bound transactions, verified A/B storage and release-before-apply |

Each group can also run directly. Shared helpers live in `host_test_lib.ps1`.
Add checks to the relevant group; keep the runner's explicit five-group list.

The five core groups primarily check source contracts. The optional Python/
LLVM storage scripts were removed to keep one validation path; their runtime
fault injection is no longer covered here. Firmware builds and device checks
remain necessary for storage I/O failures, disconnect/worker races, PC/phone
handoff and OTA finalization. Historical validation records remain in `docs/`.

Most checks are static PowerShell assertions. The product contract also runs
`test_factory_usb.py`: it compiles the actual factory USB coordinator with hardware
stubs, executes its LLVM IR, and checks the real SDK's final CDC configurations.
The same harness checks CDC queues, session invalidation, bounded DMA I/O and
summary logging. The optional board tool runs only four short echo checks:
`python tests/board/factory_cdc_bytes.py --port COM6`.
Board instructions and acceptance records live in
`docs/8-0.厂测USB通信协议设计.md`; no serial port is opened by the host suite.
This requires Python with `llvmlite` (`python -m pip install llvmlite`) and the JL
compiler at `C:/JL/pi32/bin/clang.exe`, in addition to Windows PowerShell 5.1.
Missing dependencies fail the suite rather than silently skipping these checks.
The tests do not replace firmware builds or real-device qualification, including
USB enumeration, DMA/interrupt concurrency and physical endpoint backpressure.

The product contract also runs `test_record_storage.py` using the same JL clang
and llvmlite dependencies. It executes production recording aggregation with a
mock writer, checking byte preservation across blocks, tail flush and failures.
It does not validate the RDX binary writer or physical storage.

`test_meeting_mono.py` also runs within the product contract. It executes the
stage 2A encoder-node functions with a mock codec and stream framework, checking
fixed MIC selection, input guards, allocation cleanup, frame lengths and stale
fault isolation. It does not execute the real Opus library or prove App decoding.

The mono harness also executes recording startup to verify the reported channel mode
is set before the worker starts; mono CHAT reports 0, stereo and CALL report 1.

动态单声道（2B）：`test_meeting_mono.py` 同时覆盖配置 3 的算法集成、单例归属、初始化/处理失败清理及源配对超时锁存。算法与编码器使用 mock；真实库已参与固件链接，声学择优与同步性仍需实机验收。配置与步骤见 `docs/9-1.会议录音双声道改单声道实施方案.md` 第 9.6 节。


全 CHAT 保存/恢复：产品契约运行 `test_record_format.py`，执行真实 `rdx_record_format.c` 与 cJSON，mock JL 文件系统，检查格式持久化、索引合并、旧文件/marks 保留、身份校验及提交中断重放。该测试使用 JL clang 的主机目标生成 IR，避免把含动态分配的 32 位 cJSON 结构直接映射到 64 位主机。真实 SD flush/掉电耐久性、APP 下载解码和本地播放仍需实机验收，步骤见方案第 9.7 节。
