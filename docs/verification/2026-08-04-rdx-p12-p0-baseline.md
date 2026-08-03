# RDX P12.0 文件传输 ABI 与行为预审基线

> 采集日期：2026-08-04
>
> 源码基点：`607e96795d17861fe054940de04ac19ef9c67a0d`
>
> 范围：当前工作树可复核的源码、Host 检查和通用 archive 符号证据
> 状态：**P12.0 部分完成；TX-complete 写路径迁移仍阻塞**

## 1. 边界与结论

本基线不修改文件传输可执行逻辑，不修改、替换、解包或重打包 `librdxApp.a`。本切片只：

- 归档当前 archive 指纹和可读取的符号关系；
- 记录 TX-complete、SPI 锁上下文、caller 和 NULL 行为的源码观察；
- 在兼容头中补齐既有 getter 的原签名声明；
- 用 Host public contract 保护 getter 和 send-finish 源码声明；
- 明确当前 macOS 环境不能证明的 JL ABI、ELF/map 和目标板项目。

因此，本证据允许继续进行无行为的 public query 设计和死引用清理，但**不允许进入 P12.3 TX-complete 写状态机迁移**。

## 2. `librdxApp.a` 指纹与符号

Archive：

```text
SDK/apps/common/third_party_profile/rdx_protocol/librdxApp.a
size: 534214 bytes
sha256: c540d70540dc4d61e15d1ca13579cd2342d4ea972ff0a74a1afccc04b1ef4aca
```

链接位置和产物：

- archive 链接：`SDK/Makefile:1273`
- linker script：`SDK/Makefile:1275`
- map：`SDK/Makefile:1276`

macOS `nm -A` 可复核的关键关系：

```text
rdx_protocol.c.o  T rdx_protocol_get_uploadfileInfo
rdx_protocol.c.o  t rdx_protocol_send_task
rdx_protocol.c.o  U rdx_uxfile_recordFileData_send_finish
rdx_uxfile.c.o    T rdx_uxfile_recordFileData_send_finish
xxpUart.c.o       U rdx_protocol_get_uploadfileInfo
```

这证明 getter 和 local send task 位于 `rdx_protocol.c.o`，send-finish 位于 `rdx_uxfile.c.o`，且 `xxpUart.c.o` 依赖 getter。它不证明 getter 返回对象的存储位置、地址稳定性或异步生命周期。

BSD `ar -t` 对该 JL archive 输出了特殊成员名和不完整列表，不作为权威成员清单。完整成员、defined/undefined symbols 必须在生产环境使用 JL `lto-ar`/`llvm-ar`/`llvm-nm` 重新归档。

## 3. `ReqFileInfo` 源码契约

结构定义位于 `SDK/apps/common/third_party_profile/rdx_protocol/rdx_uxfile.h:88-105`。当前 Host public contract 保护字段名称和顺序；本切片另保护：

```c
ReqFileInfo *rdx_protocol_get_uploadfileInfo(void);
void rdx_uxfile_recordFileData_send_finish(ReqFileInfo *rf_info);
```

源码检查不能证明以下目标 ABI：

- `sizeof` 和 alignment；
- 每个字段的 `offsetof`；
- `int`、指针、`u8`、`u32` 宽度；
- JL 编译参数下的最终布局。

这些项目保持 **BLOCKED**，必须用已知良好的 JL 生产工具链生成并冻结编译期断言结果。

## 4. TX-complete 源码观察基线

实现：`SDK/apps/common/third_party_profile/rdx_protocol/service/rdx_wifi_service.c:44-95`。

| 分支 | 当前顺序 |
|---|---|
| owner NULL | getter 返回 NULL 后立即返回 |
| 公共前置 | `file_send_busy == true` 时先写 `false` |
| `send_stop` | protocol busy timer stop → record send buffer free → prepared data clean → 可选 WiFi timer stop → start → `interrupt=false` → return |
| `interrupt` | 先 `interrupt=false`；`loop=true` 时单次 post 同一 owner；无 delay、结果检查或 retry |
| normal loop | 严格比较 `total_pack > pack_num` → increment 或回零 → 两支均 `ack=0` → delay 2 → first post → 失败时 delay 1 → second post；忽略第二次结果 |
| stuck retry | owner 存在且 `loop && !send_stop && !interrupt` → `ack=1` → 单次 post；无 delay/retry |

三次 post 都直接使用 getter 返回的同一个局部 owner 指针。源码只能证明发送侧传入相同变量，不能证明 receiver 消费时对象仍存活或地址恒定。

## 5. SPI 同步与锁上下文

| 路径 | 基线 |
|---|---|
| normal write-complete | `rdx_spi.c:754` 加锁；`816-818` 同步 callback；`820` 解锁。cleanup、delay 和 task post 均发生在锁内 |
| exception | `rdx_spi.c:859-863` 清状态并解锁；`865-868` 同步 TX-done callback |
| stuck retry | `rdx_spi.c:903-910` 锁内清状态并解锁；`912` 同步 retry command |

P12 不得改变上述 callback 同步属性或相对 mutex unlock 的位置。

## 6. Caller 与 NULL 行为

### 6.1 直接 caller

| Caller | 位置 | NULL 行为 |
|---|---|---|
| WiFi TX-done/query/retry | `service/rdx_wifi_service.c:44-94,156-174` | TX-done/retry no-op；Boolean query 映射为 false |
| quadruple-click | `rdx_app.c:1251-1253` | 无 guard，直接解引用 |
| DUT entry | `rdx_app.c:1559-1569` | getter 早于 OTA/record gate；通过 gate 后无 guard 解引用 |
| LED | `rdx_led_ctrl.c:127-135` | NULL-safe，视为 inactive |
| record local pipeline | `rdx_record.c:2166-2173` | 无 guard，直接解引用；STOP 在初始化前 |
| record alternate pipeline | `rdx_record.c:2381-2388` | 无 guard，直接解引用；STOP 在初始化前 |
| OTA dead local | `rdx_ota.c:831-859` | getter 被调用，结果完全未使用 |
| VM dead extern | `rdx_vm.c:100-123` | 只有声明，无调用 |

当前不存在统一且行为等价的 Boolean NULL policy。本轮 P12.1 已批准并采用三态 query：历史 NULL-safe caller 将 unavailable 保持为 not-busy；原本无保护的 app/record caller 对 unavailable 安全失败并阻止动作。

### 6.2 间接 WiFi facade caller

- SPI exception 和 stuck：`rdx_spi.c:854-870,895-913`
- SPI low-power idle gate：`rdx_spi.c:1152-1170`
- device reset/unbind gates：`service/rdx_device_service.c:252-300,331-349,369-385`
- storage format gate：`service/rdx_storage_service.c:14-29`

其中 SPI low-power idle gate 是 P12 主文档 caller 表的遗漏项，后续 cutover 必须纳入。

## 7. Cleanup 生命周期

两条现有 compat 序列与 P12 文档一致：

- record disconnect：`compat/rdx_file_transfer_cleanup_compat.c:8-15`
- BLE delayed cleanup：`compat/rdx_file_transfer_cleanup_compat.c:17-25`

额外生命周期边缘也必须在 P12.2/P12.4 复核：

- record PAUSE 单独 clean：`rdx_record.c:1370-1374`
- record STOP 单独 clean：`rdx_record.c:1393-1397`
- BLE connect send-buffer reinit：`service/rdx_record_service.c:107-110`

不得把这些动作机械合并进两条 compat cleanup profile。

## 8. 构建基线

当前 Makefile：

- 默认 product：`zenchord_cc`，配置选择位于 `SDK/Makefile:57-74`
- RDX include paths：`SDK/Makefile:292-296`
- RDX 显式源码列表：`SDK/Makefile:512-554`
- archive/map：`SDK/Makefile:1273-1276`

当前工作树没有可归档的 current clean build 产物：

```text
SDK/cpu/br28/tools/sdk.elf              absent
SDK/cpu/br28/tools/sdk.map              absent
SDK/cpu/br28/tools/sdk.elf.objs.txt     absent
SDK/apps/earphone/sdk_used_list.used    absent
SDK/cpu/br28/sdk.ld                     absent
```

macOS 当前没有仓库所需 JL pi32v2 工具链，故默认 `zenchord_cc` clean ELF、map/size、EP/shadow build 均为 **未执行**，不能用 Host 验证代替。

生产环境应执行不触发 `all` post-build 的命令，并在前后复核 archive SHA256：

```bat
SDK\tools\utils\make.exe -C SDK clean
SDK\tools\utils\make.exe -C SDK pre_build cpu/br28/tools/sdk.elf -j %NUMBER_OF_PROCESSORS%
```

## 9. 验证结果

当前 macOS Host：

```text
pwsh -NoProfile -ExecutionPolicy Bypass -File tools/verify_rdx.ps1
PASS（仅代表下列源码级 Host contract；不代表 P12.0 生产验收通过）
```

覆盖：

- architecture constraints；
- public compatibility contract；
- `ReqFileInfo` 字段顺序；
- getter 和 send-finish 声明；
- 既有 record/storage public contract。

`git diff --check` 通过。`librdxApp.a` 未修改。

## 10. P12.0 未完成项与停止条件

以下均为 **BLOCKED / 未取得生产证据**：

- [ ] JL 工具链权威 archive 成员与完整 symbols；
- [ ] getter 返回同一稳定 owner 地址及其生命周期；
- [ ] `rdx_protocol_send_task` 对 argc、queue framing 和 owner 消息槽的解析；
- [ ] `ReqFileInfo` 的 target `sizeof/alignment/offsetof` 和基础类型宽度；
- [ ] 默认 `zenchord_cc` clean ELF、map、size；
- [ ] 可用的 EP/shadow clean build；
- [ ] 目标板基础冒烟和 WiFi 文件传输基线；
- [x] query NULL policy 已批准：三态 query；历史 NULL-safe caller 保持 not-busy，原无保护 caller 安全失败；

在前三项 ABI/owner/message 证据完成前，不得进入 TX-complete 写路径迁移。P12.0 当前只达到“Host/source 观察基线已记录”，不能标记为生产验收完成。

## 11. P12.1 静态实施结果

截至 2026-08-04，当前工作树已完成 P12.1 普通业务 query 收口：

- 新增无 legacy 类型的 unavailable/idle/busy 三态 public query；
- app、LED、record、device service、storage service 已切换到语义 query；
- WiFi file-busy API 保留为迁移期 wrapper，TX-complete/send-stop/retry 仍留待 P12.3；
- record unavailable 在 task post 和初始化副作用前安全失败，START/RESUME 回退到 STOP/PAUSE；
- OTA dead local 和 VM dead extern 已删除；
- Host architecture/public contract、Makefile 显式源文件和 sink run-init 返回传播已同步更新。

Mac 静态验证通过，archive hash 不变。Windows/JL clean build、两个 record pipeline 配置编译、map/layout 和目标板回归待生产环境完成；因此本提交可作为 P12.1 静态实现 checkpoint，但不能替代生产验收。
