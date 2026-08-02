# RDX P11 Ownership Validation

> 状态：P11.6 Mac 静态阶段通过并已获提交授权；等待 Windows/JL 生产验证。
> 日期：2026-08-02（Asia/Shanghai）

## 1. 验证范围

本次只执行 Mac 可证明的源码边界、固定 ABI 与 Host 契约，不执行或替代 Windows/JL 配置矩阵、生产 clean build、link/map/size、目标板回归和真实 caller trace。

版本身份：

- P10 source baseline：`128eb8d77f945ee6184fa10953e03f5e13c62186`
- 固定 P8 ABI baseline：`5d0284f17006bd333de992ed22d5a1c7484c37a0`
- P11.5 source candidate：`9dc7429f31f09a445c032c25b8ea6bb48a6594b9`
- P11.6 validation tree：在上述 candidate 上迁移 eMMC 活跃 timer 路径的 owner query，并同步修改门禁、Host 测试、allowlist 与文档；本归档随该批次提交
- 启动时工作区：tracked 文件无修改；存在未跟踪目录 `codex-micro-4-core2/`，本次保留且未读取、未修改

## 2. 静态库冻结证据

- 文件：`SDK/apps/common/third_party_profile/rdx_protocol/librdxApp.a`
- P8 baseline Git blob：`c4864848d32c71af5c655f1302f3c503ea69e1df`
- P11 candidate Git blob：`c4864848d32c71af5c655f1302f3c503ea69e1df`
- SHA256：`c540d70540dc4d61e15d1ca13579cd2342d4ea972ff0a74a1afccc04b1ef4aca`
- ABI baseline 未更新；`rdx_protocol.h`、legacy layout、旧定义可见性、undefined dependency 与稳定 status 身份继续由固定 P8 ABI 测试保护

## 3. Final 门禁收口

P11.6 将两个 wrapper 的默认 ownership 从 `Baseline` 切换为 `Final`，保留显式 `Baseline`/`Progress` 参数供历史检查使用。

Final exact allowlist 相对候选提交新增 12 条精确记录，覆盖：

- eMMC 活跃 timer 路径新增的四个 storage owner 窄查询；
- `rdx_record.c` owner 数据面 uxfile 声明；
- 转交 P12 的 idle/poweroff `rdx_uxfile_task_free()` 生命周期调用。

APP/VM 的两条无用途 legacy 声明（对应三个 legacy symbol 命中）已删除，不再通过 allowlist 保留。eMMC public auto-poweroff check 入口仍只有立即 `return`；复审确认 `rdx_device_service_emmc_poweron(check_en)` 可直接进入 timer-start，因此该旁路按活跃路径验证。timer-start/callback 已改用 record/storage owner 窄查询，保留原 query/gate/timer/retry/poweroff 顺序，未启用 public 入口或新增硬件行为。callback 诊断日志由裸 `rp->run` 改为本次单次语义观察得到的 `offline_active`，不参与控制决策；Windows/JL 回归需确认没有外部日志解析依赖。ownership 扫描同时屏蔽 C 注释与字符串，避免把日志文本误报为状态写入。

## 4. 验证结果

执行：

```powershell
pwsh -NoProfile -File tools/validate_rdx_p11_static.ps1
```

结果：通过。默认参数已实际走 `OwnershipMode=Final`，通过项包括：

- source boundaries、storage contract、record recovery contract；
- 固定 static-library ABI；
- P11 Final ownership 与 exact allowlist；
- trace source scaffold；
- protocol adapter、storage domain、storage service、legacy format compatibility、file-transfer cleanup Host contracts。

readiness 复核：

```powershell
pwsh -NoProfile -File tools/validate_rdx_p11_readiness.ps1
```

源码、ABI 与 Host 部分全部通过；最终返回阻塞，且只剩以下 3 类生产证据：

1. Windows/JL archive linkage、map cross-reference 与 JL layout evidence。
2. P10 final CC firmware/ELF/map/build log、持久化与板测归档。
3. P11 caller-context 与可执行 P10/P11 golden trace evidence。

该阻塞是预期结果，不属于 Mac 静态失败。

## 5. 生产环境待办

在 Windows/JL 环境按以下顺序继续：配置矩阵 -> EP clean build -> shadow clean build -> 最终 CC clean build -> firmware/ELF/map/hash/Flash/RAM 归档 -> linkage/layout evidence -> caller trace -> CC 录音状态所有权专项 -> P10 持久化抽样 -> 核心链路冒烟。

最终 CC build 后不得再运行会覆盖最终产物的 EP 或 shadow build。上述证据全部完成且 readiness 通过前，不关闭 P11，也不进入 P12。
