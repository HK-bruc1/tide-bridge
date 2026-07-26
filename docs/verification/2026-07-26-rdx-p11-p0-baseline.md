# RDX P11.0 ABI 与 Ownership 静态基线

> 日期：2026-07-26
> 结论：P11.0 静态文本基线初版在 Mac 审核通过，但 P11.0 阶段验收未完成，暂不授权 P11.1；未修改生产业务行为。Windows/JL clean build、最终 CC map、firmware/hash、目标板证据和可执行 caller trace 不能由本文的 Mac PASS 替代。

## 1. 基线与范围

| 项目 | 冻结值/状态 |
|---|---|
| P10 状态 | 用户于 2026-07-26 明确确认已验收 |
| P10 源码基线 | `128eb8d77f945ee6184fa10953e03f5e13c62186` |
| 固定 P8 ABI 基线 | `5d0284f17006bd333de992ed22d5a1c7484c37a0` |
| `librdxApp.a` SHA256 | `c540d70540dc4d61e15d1ca13579cd2342d4ea972ff0a74a1afccc04b1ef4aca` |
| 当前主机 | macOS 26.5.2 / Darwin arm64 |
| 静态工具 | PowerShell 7.6.3、Git 2.55.0、Apple LLVM `nm` 21.0.0 |
| 本阶段改动 | ABI/ownership 门禁、统一静态入口、Host trace schema、基线文档 |
| 本阶段禁止项 | 业务状态迁移、静态库替换/重打包、本机编译、用 Mac 结果代替 JL 证据 |

P10 的生产验收结论作为 P11 盘点授权使用。本轮没有重新运行或伪造 P10 的 Windows/JL 产物。仓库内未找到能够证明属于 P10 最终验收的 firmware、ELF、map、构建日志、持久化记录、目标板记录或遗留问题分类；缺口已逐项写入 `tests/host/rdx_p10_evidence.psd1`，并由 `test_rdx_p10_evidence.ps1` 以失败门禁暴露。P11 Flash/RAM 基线只能从 P10 最终 CC map 获取，当前 Mac 不生成替代值。

### 1.1 P10 evidence manifest

| 证据 | 当前链接状态 |
|---|---|
| P10 source commit | 已解析：`128eb8d77f945ee6184fa10953e03f5e13c62186` |
| frozen `librdxApp.a` | 已解析并校验 SHA256 |
| final CC firmware / ELF / map | **未链接** |
| clean-build log | **未链接** |
| persistence validation archive | **未链接** |
| target-board regression record | **未链接** |
| residual-issue classification | **未链接** |

以上状态不是“本轮未重跑”的同义词：即使 P10 已在外部验收，P11.0 仍需把原始证据的仓库内路径或受控外部归档引用填入 manifest，才能形成可追溯启动基线。

P11.0 自身的 Windows/JL archive linkage/map cross-reference 与实际 `sizeof/offsetof` 报告单独由 `tests/host/rdx_p11_linkage_evidence.psd1` 管理；当前两个路径均为空，不能由 Mac `nm` 和文本布局比对替代。

## 2. ABI 冻结结果

`tests/host/test_rdx_static_library_abi.ps1` 当前覆盖并已通过：

- archive Git blob 与固定 SHA256；
- `rdx_protocol.h` 全文件与 P8 基线；
- `RecordStatus`、`MicGainPara`、`rdx_tws_sync_record_t`、`ReqFileInfo` 文本布局；
- `record_status` 声明/初始化和 `rdx_record_get_status()` 返回同一稳定对象；
- `rdx_record_process()`、record mark/recovery、VM、BLE、ADC MIC 等旧签名；
- `rdx_app_get_record_mode()`、`rdx_app_get_wifi_info()`、`rdx_record_mode_active_check()`、三个 `rdx_app_emmc_*` wrapper；
- `rdx_record_service.h` 中 P8 已存在的 21 个公共函数声明及当前定义签名，特别是 `u8 rdx_record_service_is_active(void)`；
- archive undefined symbols 与关键 member -> symbol 依赖。

Mac 没有使用 JL ABI 编译器计算 `sizeof/offsetof`；本阶段以固定 archive、协议头全量比对和结构文本比对保护，实际 JL 大小/对齐仍须由 Windows/JL 生产证据确认。

### 2.1 archive member 证据

| archive member | P11 相关 undefined symbols |
|---|---|
| `rdx_protocol.c.o` | `rdx_app_get_record_mode`、`rdx_app_get_wifi_info`、`rdx_record_get_status`、`rdx_record_mode_active_check`、`rdx_record_process` |
| `rdx_uxfile.c.o` | `rdx_app_emmc_poweroff_check_timer_stop`、`rdx_app_emmc_poweron`、`rdx_app_get_wifi_info`、`rdx_record_clear_marks`、`rdx_record_err_reboot_flag_write_into_vm`、`rdx_record_get_marks`、`rdx_record_get_status` |
| `xxpUart.c.o` | `rdx_app_emmc_poweroff_check`、`rdx_app_emmc_poweroff_check_timer_stop`、`rdx_app_get_wifi_info`、`rdx_record_mode_active_check` |

`rdx_protocol.c.o` 与 `rdx_uxfile.c.o` 对 `rdx_record_get_status()` 返回对象的访问性质在 Mac 上均标为 **unknown**。当前工具无法对 archive 对象做可靠的 JL 指令级 read/write 分类；不得据此宣称静态库只读，也不得移动、替换或缩小 `record_status`。

## 3. Ownership 文本基线

`tests/host/test_rdx_p11_ownership.ps1 -Mode Baseline -BaselineRef 128eb8d` 的冻结总数如下。它是防漂移文本门禁；注释、声明、payload 字段等由后续精确 allowlist/上下文表解释，不能把总数直接称为活跃调用数。

| 指标 | P10 | P11.0 working |
|---|---:|---:|
| `RecordStatus *` | 72 | 72 |
| `rdx_record_get_status()` | 61 | 61 |
| `rdx_record_process()` | 14 | 14 |
| legacy record/payload 字段写模式 | 77 | 77 |
| `rdx_uxfile_*` | 53 | 53 |

P11.0 未减少或增加上述调用，目的只是锁住 P11.1 迁移前的 accepted-P10 起点。

## 4. Writer-context 表

“上下文”一列区分源码可证明事实和必须由 JL trace/map 复核的事实。未知项不允许在迁移时自动选择 now/post 或自行加锁。

| 字段/对象 | 当前 writer | 源码可见执行方式 | P11 控制结论 |
|---|---|---|---|
| `run/scene/formate`、pause/time、`process_state` | `rdx_record.c` command/process/run init/exit/timer 路径 | legacy owner；既有 direct、record task、timer 混合，具体 JL task 需生产 trace | 保持唯一物理对象；P11.1 不搬状态，不加宽锁；热路径留 owner |
| `is_switch` | `rdx_app_switch_keep_timer_cb()` | timer callback | 后续通过 domain command 清除；必须保持 callback 时序 |
| `run` | `APP_MSG_RECORD_OFF`、`rdx_app_enter_idle()` | 前者在 APP message handler 更新后 post `app_core`；后者当前 direct process | 分别固定为 post 与 now，禁止合并 |
| `key_trigger` | `APP_MSG_RECORD_SWITCH` | APP message handler | 使用语义 command，保留马达/ready flag 顺序 |
| `run` | `rdx_app_charge_prepare()` | charge 编排入口中 direct process；真实 JL caller task 待 trace | `stop_now(CHARGE_PREPARE)`，后续 WiFi/RTC 顺序不变 |
| `run` | `rdx_dut_rec_stop()` | DUT 命令链中 direct process | `stop_now(DUT)`；不改变 DUT 状态复位/显示顺序 |
| `run` | `rdx_device_service_soft_poweroff()` | poweroff 编排中 direct process | `stop_now(POWEROFF)`；必须先收尾再断 BLE/下电 |
| record owner 字段与 trigger payload slot | `rdx_record_service_upload_timer_cb()`、`device_record_handle()`、`switch()`、mode/BLE helpers | upload timer、service caller、BLE event；既有 direct/post/protocol post 混合 | domain 状态与 protocol payload 必须物理拆 adapter；payload 不是第二真相源 |
| `rerun/mode/orig_mode/run` | BLE disconnect/stop helpers | BLE event bus callback调用；publisher 的真实 JL context 待 trace | 保持现有 direct/post选择；未确认前不引入 mutex |
| `RecordStatus` unknown access | `librdxApp.a::rdx_protocol.c.o`、`rdx_uxfile.c.o` | archive 内部，read/write 未能静态确认 | 按可能写处理；稳定地址、完整布局和旧 getter永久保留 |

Snapshot 策略被冻结为：优先新增窄 query；确有多字段一致性需求时才在 owner 内一次性复制。当前证据不足以授权 mutex 或全局 critical section；若 Windows/JL trace 证明跨上下文撕裂风险，只允许通过 OSAL 增加最短、成对且不包围日志/文件/投递的 critical section。

## 5. Command-context 表

| caller/path | P10 方式 | 冻结目标 | 必须保持的顺序/语义 |
|---|---|---|---|
| `APP_MSG_RECORD_OFF` | 写 STOP，post `app_core` process | `stop_post(APP_MESSAGE)` | APP message 入口保留；只投递一次 |
| `rdx_app_charge_prepare()` | 写 STOP，当前上下文 direct process | `stop_now(CHARGE_PREPARE)` | stop/process -> WiFi off -> RTC store/restore timer |
| `rdx_dut_rec_stop()` | 写 STOP，direct process | `stop_now(DUT)` | process -> DUT func clear -> display |
| `rdx_app_enter_idle()` | 写 STOP，direct process | `stop_now(IDLE)` | process -> file stop -> WiFi off -> idle cleanup |
| `rdx_device_service_soft_poweroff()` | 写 STOP，direct process | `stop_now(POWEROFF)` | record收尾 -> WiFi/BLE -> delay/task/free/poweroff |
| BLE disconnect event | conn state更新后 direct process | BLE-disconnect 专用 command，保持 now | stream interrupt -> mode/rerun处理 -> process -> immediate cleanup |
| upload fallback connected | 写 STOP，protocol payload post | `prepare_stop(UPLOAD_FALLBACK)` + protocol trigger | 不额外 local process；factor `0` |
| upload fallback disconnected | 写 STOP，post local process | `stop_post(UPLOAD_FALLBACK)` | 不创建 protocol pool |
| device toggle, connected + offline active | 写 owner STOP，post local process，再 post protocol payload | `device_toggle(scene)` | 无 source 参数；local post 在 protocol post 前 |
| device toggle, connected 其他分支 | 构造 DEVICE payload并 protocol post | `device_toggle(scene)` | DEVICE slot 先清零；pool失败/投递失败行为保持 |
| device toggle, disconnected | 更新 owner START/STOP并 post local process | `device_toggle(scene)` | 不使用 protocol pool；只投递一次 |
| switch, disconnected active | 写 `noshow/STOP`，direct process，再写 switch字段/构造 payload | `switch_scene(orig_scene)` | direct 不改 post；随后 trigger post 和 keep-timer post |
| BLE `stop_from_ble()` | 写 STOP，direct process | BLE stop 专用 now | 保持 `rdx_err_t` wrapper 与同步返回 |
| `rdx_record.c` 内部 command/start/stop | owner 内 direct process | legacy owner private command | 不作为外围普通 API；ABI 入口继续导出 |

上表的 `_now/_post` 是迁移目标而非本阶段实现。真实 task 名称无法由 Mac 源码完全证明的 caller，在迁移前必须补 Windows/JL trace；command 不允许运行时猜 task 后自动分流。

## 6. Trigger、cleanup 与 format 冻结

### 6.1 trigger factor 与 payload

| kind | post factor | payload/pool 基线 |
|---|---:|---|
| UPLOAD fallback | `NULL` / 0 | connected 使用 pool，只覆盖现有字段；disconnected 不使用 pool |
| DEVICE toggle | `NULL` / 0 | connected 分配 slot 并 `memset` 清零；disconnected 直接更新 owner |
| SWITCH | `NULL` / 0 | 分配 slot，不新增统一清零；trigger post 后仍 post keep timer start |

三处 `rdx_os_task_post_callback2(..., rp_slot, NULL)` 已由门禁要求精确为 3 次。`device_toggle(scene)` 不新增 trigger/source 参数。

### 6.2 BLE cleanup profiles

| profile | 触发/skip | 冻结调用顺序 |
|---|---|---|
| immediate record-disconnect | BLE disconnect record event；不释放 DAT list | `uploadFileInfo_clean` -> record data buffer free -> file-sync timer stop -> send buffer reinit |
| delayed/full cleanup | delayed BLE cleanup；WiFi active 时保持 skip | `uploadFileInfo_clean` -> record data buffer free -> **DAT list free** -> file-sync timer stop -> send buffer reinit |

`rdx_storage_service_cleanup_ble_buffers()` 当前只代表 delayed/full compatibility profile，不能替换 immediate profile。

### 6.3 format-context 表

| 场景 | 请求与 callback | 冻结 ACK/reset/重入语义 |
|---|---|---|
| APP protocol format | command dispatcher -> busy gate -> `sd_format_ack_indicate` -> `rdx_uxfile_sd_format(rdx_storage_service_format_cb)` | busy ACK=1并返回；成功 ACK=0 **先于** format request；callback成功只发布 done event，不补第二 ACK |
| DUT format | `rdx_uxfile_device_sd_format(rdx_dut_format_cb)` | 先置 `DUT_FUNC_FORMAT`；callback清状态并恢复显示；callback task 由 uxfile/JL 决定，Mac unknown |
| DUT final-pack | reset defaults/写 DUT flag -> device format callback | callback只在 pending 时继续，随后按 500ms timer断 BLE、再500ms关机 |
| unbound | 置 unbounding -> `rdx_uxfile_sd_format(rdx_device_service_unbound_cb)` | 成功清配对/配置并 reset；失败 indicate fail并清 unbounding |
| choose-to-unbound + format | 预处理后先发成功 ACK，再请求 format | callback成功清绑定再 ACK/reset；失败 ACK=1；不得统一成 APP clean callback |

格式 callback 的真实 JL 执行 task 与重入能力不能由 Mac 静态分析证明；P11.5 迁移前必须用生产环境证据确认。P11.0 不引入通用 `cb + ctx`、队列或新重入策略。

## 7. 禁用路径与 Host trace schema

`rdx_device_service_emmc_poweroff_check()` 当前函数体只有立即 `return`；门禁要求继续如此。其后的 timer/activity 代码属于不可达历史实现，本阶段不迁移、不接线、不建立 storage activity/system-busy bitmap，也不宣称完成功能验证。

`tests/host/rdx_p11_trace_schema.h`、`rdx_p11_trace_spy.*` 和 `test_rdx_p11_golden_trace.c` 已形成 Host trace scaffold：比较 operation、execution context、关键参数、result 与边界 owner state，并包含错误 context/参数/result、重复 ACK/process、pool 失败后错误 follow-up 等负向用例。本轮已使用显式本机 C 编译器按 C11、`-Wall -Wextra -Werror` 编译并运行该 Host self-contract，8 个子用例全部 PASS。该 scaffold 未接入生产 caller，因此它仍不是可执行 P10 golden trace 证据。

所需 caller 场景、真实 execution context 和 P10 baseline trace 路径由 `tests/host/rdx_p11_trace_evidence.psd1` 管理；当前均显式为 `unknown`/空路径，`test_rdx_p11_trace_evidence.ps1` 必须失败。只有 caller spy 接线、Host/诊断运行和证据链接完成后，才能把本节状态改为可执行 golden trace PASS。

## 8. 静态验证结果

执行命令：

```powershell
pwsh -NoProfile -File tools/validate_rdx_p11_static.ps1
```

结果：PASS，包含：

1. RDX boundary checks；
2. P10 storage contract；
3. record recovery contract；
4. 固定 P8 ABI、archive fingerprint/undefined/member dependency；
5. P11 ownership Baseline counts、eMMC禁用、静态 ACK顺序、两个 cleanup profile和 factor；
6. service public-header legacy 类型扫描和“文件 + 函数 + 符号 + 用途”精确 allowlist；
7. Host trace scaffold 的静态结构契约。

明确未执行：C/C++ 编译、JL configuration matrix、EP/shadow/CC clean build、link/map、firmware/hash、Flash/RAM、Host native mock、目标板录音/断连/format/关机回归。

阶段 readiness 命令：

```powershell
pwsh -NoProfile -File tools/validate_rdx_p11_readiness.ps1
```

当前结果为预期的 **BLOCKED / exit 1**，同时报告三道未闭环门禁：Windows/JL ABI linkage/layout、P10 evidence linkage、caller-context/executable golden trace。只有补齐 manifest 后该命令整体 PASS，才允许重新评估 P11.1。

## 9. P11.0 阶段判定

- P11.0 静态文本基线初版：**PASS**。
- P11.0 阶段验收：**未完成，暂不授权 P11.1**。
- RDX 生产业务行为：**零迁移**。
- `librdxApp.a` ABI：**blob/hash/header/signature 静态证据未漂移**。
- Windows/JL ABI linkage/layout：**BLOCKED，map cross-reference 与实际 `sizeof/offsetof` 报告未关联**。
- archive getter read/write：**unknown，按可能写保护**。
- 精确 allowlist/public-header 扫描：**全量生产 `.c` 调用发现、逐函数/符号计数及 stale-entry 反查静态 PASS**。
- Host trace scaffold：**显式本机 C11 编译与 8 个 self-contract 子用例 PASS；未接 caller、未产生 P10 trace**。
- readiness evidence 校验：**要求普通非空文件、SHA256、固定 JSON schema 及 build/commit/toolchain/scenario identity；当前缺证据时预期 BLOCKED**。
- P10 生产证据链接：**BLOCKED，7 个必需路径未关联**。
- caller context/golden trace：**BLOCKED，11 个场景的 context 与 baseline trace 未关联**。
- 进入后续调用点迁移前：必须通过 `tools/validate_rdx_p11_readiness.ps1`；不得由实现者猜测或绕过 manifest。
