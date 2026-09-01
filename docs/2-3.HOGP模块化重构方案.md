# HOGP 模块化重构方案（代码事实版）

> 适用项目：VibeCoding Keyboard / T2620 / JL AC701N（BR28）
> 文档定位：本文件合并了原重构方案与收尾实施方案，是 HOGP Profile v1 的唯一重构与收尾依据。
> 当前结论：HOGP HID 模块、正式 APP keymap 下发路径、两阶段 A/B VM 持久化和 host 侧软件契约已经落地；产品确认由 KEY5 三击切换模式，仍需确认量产默认 keymap、接入真实连接级鉴权并完成真实掉电与硬件回归。当前 weak 鉴权钩子默认允许 Config owner，VM 绑定状态不作为当前 BLE 会话身份凭据。

## 1. 关键产品状态

### 1.1 正式 APP 配键路径已接入

**BLE APP 通过 RDX `custom/hogpkm` 命令下发五键 keymap 的正式路径已经接入。当前固件可以接收 APP 下发的 keymap、校验并应用到 HOGP Key Action executor，并通过 VM 持久化保存。硬件测试已确认 SET 后可生效，READ 可读回，并且重启/重新初始化后配置可恢复。**

代码事实如下：

- `rdx_app_custom_command_parse()` 将 `hogpkm` custom 命令交给 `rdx_hogp_keymap_config_handle_custom()`。
- `rdx_hogp_keymap_protocol.c` 负责 v1 wire frame 的 HEX 解码、版本/长度/CRC 校验、opcode 解析、HID usage 白名单校验和响应编码。
- `rdx_hogp_keymap_config.c` 负责 Config owner 鉴权、pending 串行化、revision/idempotency、SET/GET/CAPS/RESET 分发、executor apply、VM commit 和失败回包。
- `rdx_hogp_keymap_store.c` 负责 A/B VM 持久化记录和提交记录，降低掉电中断造成配置损坏的风险。
- `rdx_hogp_key_action.c` 仍然只负责 RAM active keymap、Keyboard Report 转换、click 发送和 release timer，不直接解析 BLE APP 帧或访问 VM。
- `RDX_HOGPKM_TRACE_ENABLE` 默认关闭；排查 APP 下发问题时可临时打开，输出解析、校验、apply、VM commit、回包和队列状态。

当前 v1 keymap 是完整五键表，每个物理键固定 7 字节：

```text
[modifier, usage1, usage2, usage3, usage4, usage5, usage6]
```

KEY1-KEY5 顺序仍按产品物理映射固定：

| 产品键 | key value | `num_idx` | GPIO | 当前正式 keymap entry |
|---|---|---:|---|---|
| KEY1 | `KEY_IO_NUM0` | 0 | PB2 | payload bytes 0-6 |
| KEY2 | `KEY_IO_NUM1` | 1 | PG7 | payload bytes 7-13 |
| KEY3 | `KEY_IO_NUM2` | 2 | PB4 | payload bytes 14-20 |
| KEY4 | `KEY_IO_NUM3` | 3 | PG8 | payload bytes 21-27 |
| KEY5 | `KEY_IO_NUM4` | 4 | PC2 | payload bytes 28-34 |

该映射同时由 `sdk_config.c` 的 GPIO 配置、`app_main.c` 的 KEY-GPIO 日志、`rdx_key.c` 的旧按键表注释、HOGP executor 的 key index 和正式 keymap payload 顺序交叉确认。

正式路径如下：

```text
RDX BLE APP 业务命令
    -> 数据校验与配置事务
    -> RAM active keymap / VM 持久化
    -> Key Action Executor
    -> 8-byte Keyboard Report
    -> HOGP notify
```

HOGP HID 传输层仍不解析 APP 配置帧、不保存 Flash，也不解释 Macro 或 Layer；正式配置能力被限定在 `rdx_hogp_keymap_*` 模块和 Key Action executor 边界内。

### 1.2 当前物理按键路由

`rdx_app_earphone_key_remap()` 的当前行为为：

- HOGP 已连接时，五个 IO NUM 键的 `KEY_ACTION_CLICK` 进入 `rdx_hogp_key_action_click()`，并由 executor 发送按下 Report、延时后发送全零释放 Report。
- HOGP 未连接时，CLICK 回退到原 RDX key table。
- `LONG`、`HOLD`、`HOLDUP` 等其他事件继续走原 RDX key table，不由 HOGP Key Action executor 消费。
- 模式切换入口不受 keymap 配置控制，它属于正式模式控制路径；五键 HID 动作映射由正式 APP keymap/VM 配置驱动，未配置时按默认 keymap 行为处理。

### 1.3 模式切换键为 KEY5

模式切换已经按产品确认统一：

- KEY5=`KEY_IO_NUM4`/PC2，对应 `num_idx == 4`。
- `rdx_app.c` 判断 `num_idx == 4 && index == KEY_ACTION_TRIPLE_CLICK`。
- KEY1=`KEY_IO_NUM0`/PB2，只保留普通 keymap 动作，不承担模式切换。

因此当前代码由 **KEY5 三击**触发模式切换。host 契约明确冻结 `num_idx == 4`，避免再次依据旧文档把产品行为误改为 KEY1。

当前处理：

1. `rdx_app.c` 的模式切换条件为 `num_idx == 4`。
2. 保持 KEY1-KEY5 的 keymap payload/executor 顺序不变。
3. host 契约明确断言三击条件绑定 `num_idx == 4`。
4. 统一 host tests 已通过；仍需在硬件上验证 KEY5 可切换、KEY1 不触发切换。

在硬件确认前，该路径标记为“软件已完成、硬件待确认”。

## 2. 收尾目标与边界

本轮收尾目标：

1. 保持已经验证的 BLE HID 键盘行为和 Profile v1 外部契约不变。
2. HOGP 继续复用 RDX 的单个 `app_ble` handle 和同一份静态 ATT 数据库。
3. 明确 Server、Mode Controller、HOGP runtime、Profile 常量和 Key Action 的职责边界。
4. 正式 APP keymap 配置复用 RDX BLE APP 业务通道，保持 HOGP HID 传输层与配置协议解耦。
5. 通过 host 契约、固件构建和硬件回归完成收尾。

当前不做：

- 不修改 `config_le_gatt_server_num`，不创建第二个 GATT Server 或第二个 `app_ble` handle。
- 不调整 HID handle、70 字节 Report Map 或 8 字节 Input Report payload。
- 不给 Input Report 增加 Report ID 前缀。
- 不把 Output Report 移回 HID Service；现有兼容布局留待 Profile v2 评估。
- 不新增 Consumer Control、Macro、Layer、hold-tap 或多键并发状态机。
- 不新建独立的 BLE APP 配置 GATT Service；配置能力应复用 RDX BLE APP 业务通道。
- 不在 HOGP HID transport/runtime 模块中实现 APP 配置协议、VM、OTA、鉴权或 HFP/Voice 策略；APP keymap 配置限定在 `rdx_hogp_keymap_*` 模块内。
- 不把 APP keymap wire DTO 直接固化为 executor C struct 或 Flash 裸结构；VM 格式必须继续带版本、长度和 CRC。

## 3. 当前架构

HOGP 是 RDX BLE Server 下的功能模块，不拥有 BLE Server 生命周期。

```text
rdx_ble_server.c
├─ 分配和注册唯一 app_ble handle
├─ 保存完整 rdx_profile_data[]
├─ 执行广播、断连、连接事件和 suppression 等 BLE 副作用
├─ 按 connection owner 隔离 RDX Config 与 HOGP ATT/发送路径
└─ 将 HID ATT、SM/HCI 事件和广播构造转发给 HOGP runtime

rdx_ble_mode_controller.c
└─ 保存 requested/advertised mode、connection owner、switch pending

rdx_hogp_profile.c/.h
└─ 保存 HID handle、UUID、Report Map、HID Information、Report Reference 和 ATT 展开宏

rdx_hogp_keyboard.c/.h
└─ 管理 Protocol Mode、Control Point、CCC、加密、suspend、当前 Report、广播 payload 和 notify

rdx_hogp_key_action.c/.h
└─ 管理 RAM active keymap、默认 keymap、Report 转换和 click release timer

rdx_hogp_keymap_protocol.c/.h
└─ 编解码 APP `hogpkm` v1 wire frame，校验版本、长度、CRC 和 HID usage

rdx_hogp_keymap_config.c/.h
└─ 管理 APP keymap 配置事务、revision/idempotency、executor apply、VM commit 和响应回包

rdx_hogp_keymap_store.c
└─ 管理 keymap A/B VM 持久化记录、提交记录和重启恢复

rdx_app.c
└─ 分发物理按键、触发正式模式切换，并在 HOGP 连接与离线 RDX key table 之间路由
```

### 3.1 模块职责表

| 文件 | 当前职责 | 明确不负责 |
|---|---|---|
| `rdx_ble_server.c/.h` | 唯一 BLE handle、ATT 总表、连接与广播副作用、owner 授权、mode request facade | Keymap/Macro/Layer 解释 |
| `rdx_ble_mode_controller.c/.h` | 模式、广播身份、连接 owner、pending 状态 | 广播/断连操作、业务策略 |
| `rdx_hogp_profile.c/.h` | Profile v1 常量和字节级契约 | runtime 与产品按键 |
| `rdx_hogp_keyboard.c/.h` | HID ATT、连接安全状态、广播数据、标准 Keyboard Report 发送 | 物理键号和 APP 配置协议 |
| `rdx_hogp_config.h` | HOGP 编译开关、默认模式、release delay、安全、名称与日志默认值 | 产品 keymap |
| `rdx_hogp_key_action.c/.h` | RAM active keymap、Report 转换和释放 timer | BLE 生命周期、VM、APP 帧解析 |
| `rdx_hogp_keymap_protocol.c/.h` | `hogpkm` v1 wire frame 编解码、CRC 和 usage 校验 | BLE 发送队列、VM、executor |
| `rdx_hogp_keymap_config.c/.h` | APP 配键事务、revision/idempotency、executor apply、响应回包 | HID ATT runtime、物理按键扫描 |
| `rdx_hogp_keymap_store.c` | A/B VM 持久化、commit record、重启恢复 | BLE/RDX transport、executor apply |
| `rdx_app.c` | 物理事件和产品模式路由 | HID handle、Report 字节和 release timer |

### 3.2 生命周期与授权

- `rdx_ble_mode_controller` 只保存状态，不直接调用 Server 或 HOGP 的 BLE 操作。
- `rdx_ble_server` 根据 mode controller 状态执行断连、广播切换和 pending apply。
- 连接建立时由实际 advertised identity 设置 `RDX_BLE_OWNER_CONFIG` 或 `RDX_BLE_OWNER_HOGP`。
- HOGP read/write、Output Report 和 Input Report notify 只允许 HOGP owner。
- RDX APP 写、RDX notify 和 OTA send 只允许 Config owner。
- HOGP runtime 不主动恢复 Config 广播，也不直接决定断连和模式切换。

## 4. Profile v1 冻结契约

以下内容已经由 `test_hogp_profile_contract.ps1` 冻结，本轮不得改变。

### 4.1 GATT handle

| Handle | Attribute |
|---|---|
| `0x0016` | HID Service `0x1812` |
| `0x0017` / `0x0018` | Protocol Mode declaration/value |
| `0x0019` / `0x001a` | Input Report declaration/value |
| `0x001b` | Input Report CCC |
| `0x001c` | Input Report Reference |
| `0x001d` / `0x001e` | Report Map declaration/value |
| `0x001f` / `0x0020` | HID Information declaration/value |
| `0x0021` / `0x0022` | HID Control Point declaration/value |
| `0x0023-0x0027` | Device Information Service |
| `0x0023-0x0025` | HID Service 内的 Output Report block，受 `TCFG_RDX_HOGP_ENABLE` 门控 |

### 4.2 Report 契约

- Report Map 长度固定为 70 字节，字节内容保持不变。
- Input Report 长度固定为 8 字节：`[modifier, reserved, key1, key2, key3, key4, key5, key6]`。
- ATT notify payload 不附加 Report ID。
- `rdx_hogp_keyboard_report_t` 明确包含 `modifiers`、`reserved` 和 `usages[6]`。
- `rdx_hogp_keyboard_release_all()` 发送 8 字节全零 Report。

### 4.3 广播与默认模式

- HOGP 广播包含 Flags `0x06`、完整 16-bit Service UUID `0x1812`、Appearance `0x03C1`。
- `RDX_HOGP_NAME_SOURCE=0`，当前名称复用 RDX Server local name；`VibeKeyboard` 只是 custom-name fallback，当前未启用。
- RDX Config 与 HOGP 广播互斥。
- T2620 设置 `TCFG_RDX_HOGP_ENABLE=1` 和 `RDX_BLE_DEFAULT_MODE=RDX_BLE_DEFAULT_MODE_HOGP`，因此当前产品默认进入 HOGP 身份。
- HOGP 总开关关闭时，`rdx_ble_mode_effective_default()` 强制回到 Config 模式。

## 5. 原重构方案落地状态

原《HOGP 模块化重构方案》的有效内容已归并为下表。

| 原阶段 | 当前状态 | 代码事实 |
|---|---|---|
| Phase 0：基线冻结 | 已完成 | host 测试冻结 handle、Report Map、attribute 顺序和 8 字节 payload |
| Phase 1：HOGP 纯搬迁 | 已完成 | runtime、ATT、SM/HCI 和 Report 发送已移入 `rdx_hogp_keyboard.c` |
| Phase 2：Profile 常量集中 | 已完成 | handle、UUID、Report Map、Information、Reference 和 ATT 宏位于 `rdx_hogp_profile.*` |
| Phase 3：传输配置化 | 已完成 | 编译开关、默认 mode、key-up delay、安全、名称和日志集中到 `rdx_hogp_config.h` |
| Phase 3：正式 Keymap 接入 | 已完成 | APP `hogpkm` v1 wire frame、完整五键 keymap、revision/idempotency、executor apply、A/B VM 持久化和 SET/GET/CAPS/RESET 回包已落地，并经硬件测试确认可下发、应用和持久化 |
| Phase 4：日志诊断 | 已完成 | 状态变化、错误、Report 发送及 `rdx_hogp_dump_state()` 已落地 |
| Phase 5：host 契约 | 已完成 | HOGP profile contract 已纳入统一 host runner |
| Mode Controller 独立化 | 已完成 | 状态位于 `rdx_ble_mode_controller.*`，BLE 副作用仍由 Server 执行 |
| Key Action 最小拆分 | 已完成 | RAM keymap、转换和 release timer 已移出 App/HOGP 传输层 |
| 模式切换物理键绑定 | 软件已完成 | 产品确认为 KEY5/IO_NUM4，条件和 host 契约均冻结 `num_idx == 4`；硬件待确认 |
| Profile v2 | 不进入本轮 | Output Report 规范化、Consumer Control、Report ID/GATT cache 另行设计 |

## 6. 正式 Key Action 接入实现

正式 APP 配键路径已按独立 DTO 和配置事务实现，没有把 executor RAM struct 直接暴露为无线协议或 Flash ABI。

v1 wire frame 的稳定事实：

1. APP 通过 RDX `custom` 命令 `hogpkm` 下发十六进制 frame。
2. Frame header 包含 `version`、`opcode`、`request_id`、`base_revision` 和 `payload_len`。
3. Frame 尾部携带 CRC32，覆盖 header 和 payload。
4. `SET_KEYMAP` payload 固定为 35 字节，即 5 个 `[modifier + usages[6]]` entry。
5. `GET_KEYMAP` 响应 payload 为 `status + 35-byte keymap`，因此 HEX value 长度为 100。
6. `GET_CAPS` 响应当前能力：5 个物理键、每键最多 6 个 usage。
7. `SET_KEYMAP` 和 `RESET_KEYMAP` 成功响应携带 `status + keymap_crc32`。

配置事务边界：

1. `rdx_hogp_keymap_protocol.c` 只负责编解码、长度/版本/CRC 校验和 HID usage 合法性校验。
2. `rdx_hogp_keymap_config.c` 只允许 Config owner 执行配置命令，并通过 pending 请求串行化处理。
3. SET 先校验 candidate，再计算 canonical keymap CRC，再检查 revision/idempotency。
4. commit 时先 PREPARE/读回校验 A/B VM 非活动槽，再 apply executor，最后写 commit record；COMMIT 失败会回滚旧 payload。
5. commit 成功后更新 RAM current keymap、revision、keymap CRC 和 active VM slot。
6. GET 读取当前 RAM keymap；init 时从 VM 恢复，VM 无有效记录时使用产品默认 keymap。

掉电与断连边界：

- commit record 首次读回失败时会再次读取完整 slot，并重新校验 data/commit 的 magic、schema、revision、payload 与 CRC；该兜底不能证明底层 `syscfg` 已完成物理落盘，真实掉电测试仍需覆盖写缓存和部分写入。
- A/B 槽 revision 相同但 payload 不同时视为存储冲突并进入默认安全状态，不得任意选择某个槽；如需改善现场诊断，应增加异常计数或上报，而不是猜测有效配置。
- 请求在 COMMIT 前检测到断连会停止事务或回滚 RAM；COMMIT 已成功后发生断连则保留新持久化结果但抑制旧连接 ACK，APP 重连后通过 GET_KEYMAP 对账。
- generation 可拦截未开始的 pending 请求、排队状态响应和事务关键边界；发送前仍需依赖 BLE/RDX 连接状态检查共同缩小断连竞态，硬件压力测试不能省略。

边界仍需保持：

- `rdx_hogp_key_action_keymap_t` 仍是 executor 内部 RAM 结构，不作为无线 ABI 或 Flash ABI。
- HOGP HID transport 只负责 HID ATT、Keyboard Report 和 notify，不解析 APP 配置帧。
- v1 不包含 Macro、Layer、Consumer Control 或 hold-tap；后续若需要必须新增协议版本或扩展字段。

### 6.1 RDX 静态库 custom 回包栈溢出记录

正式 `GET_KEYMAP` 响应暴露了一个 RDX 预编译静态库中的历史问题：`librdxApp.a` 内的 `rdx_protocol_custom_msg_indicate()` 会在栈上拼接完整 custom 上行包，但其内部局部缓冲区不足以容纳 HOGPKM 的 100 字符 HEX value。

HOGPKM `GET_KEYMAP` 上行包长度为：

```text
*DEV#custom#     12 bytes
hogpkm            6 bytes
两个 #            2 bytes
value           100 bytes
实际发送长度     120 bytes
NUL 结尾         +1 byte
总存储需求       121 bytes
```

旧 wrapper 的栈缓冲区不足，100 字符 value 加 NUL 会覆盖返回地址附近内容，串口日志表现为 GET_KEYMAP 回包后 `Chip Exception`、`instruction fetch hmem exception`，寄存器/栈中出现大量 ASCII `'0'`。

当前修复策略：

1. 保持 `CUSTOM_VALUE_MAX_LENGTH` 为 100，不修改预编译库 ABI。
2. HOGPKM 长回包不再调用 `rdx_protocol_custom_msg_indicate()`。
3. 在 `rdx_hogp_keymap_config.c` 中自行构造完整 `*DEV#custom#hogpkm#<value>#` 上行包。
4. 直接调用 `rdx_protocol_packet_send_priority(packet, offset)` 入队发送。
5. 已确认 RDX 队列入队时会 `malloc + memcpy`，因此静态拼包缓冲区不会产生异步悬空问题。
6. 已修正返回值语义：`rdx_protocol_packet_send_priority()` 成功返回正数长度，HOGPKM 封装层将其转换为 `0` 成功、负数失败。

该方案是有意限定影响面的绕行方案，只绕过 HOGPKM 这条已知 100 字符长回包路径，不改变其他 RDX custom 短消息路径。它解决了当前硬件卡死问题，但没有修复静态库内部 wrapper 的通用缺陷。

长期最佳实践：

- 获取 RDX 库源码后，修复 `rdx_protocol_custom_msg_indicate()` 本身。
- wrapper 内部应按实际 `cmd/value` 长度计算所需空间，使用足够大的缓冲区或动态分配。
- 拼包必须使用有界格式化/有界 memcpy，并明确返回值语义。
- 修复后重新生成 `librdxApp.a`，再评估是否移除 HOGPKM 的临时绕行路径。

## 7. 验证状态

### 7.1 已完成的 host 验证

Windows 统一入口：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\host\run_host_tests.ps1
```

当前 host 验证结果：

- T2620 config overlay：通过。
- HOGP profile contract：通过。
- HOGP keymap architecture：通过，覆盖长回包绕过旧 wrapper、返回值语义和 HOGPKM trace 默认关闭。
- RDX local playback configuration：通过。
- RDX playback navigation：通过。
- 统一 runner：6 个测试全部通过。

Host 契约主要覆盖：

- Profile handle、Report Map、attribute 顺序、Output Report 门控和 payload。
- disabled stubs、模块 include 边界和职责隔离。
- mode controller、connection owner、模式切换和广播恢复规则。
- HOGP runtime 的 CCC、加密、suspend、当前 Report 和 release 行为。
- T2620 默认 HOGP、正式 keymap 配置模块边界、CLICK 路由结构和 executor 生命周期。
- HOGPKM 长回包不走旧 `rdx_protocol_custom_msg_indicate()`，避免预编译库栈缓冲区溢出。

新增行为测试执行 A1/A2 SET/GET/CAPS 向量、CRC32、keymap usage 校验以及 PREPARE/APPLY/COMMIT 和处理中断连的语义模型；架构契约同时冻结两阶段事务顺序、访问策略、app_core 响应串行化和 KEY5 的 `num_idx == 4`。这些是 host 语义模型，不等同于真实 `syscfg` 掉电故障注入；量产关闭仍需验证写缓存、部分写入和 commit record 落盘行为。

### 7.2 固件构建状态

```text
[x] TCFG_RDX_HOGP_ENABLE=1 全量编译
[x] TCFG_RDX_HOGP_ENABLE=0 全量编译
[x] 确认新增模块均进入最终链接
[ ] 确认 tools/output 生成二进制不作为源码提交
```

### 7.3 硬件回归状态

```text
[ ] 上电默认出现 HOGP 广播，名称与当前 Server local name 一致
[ ] Windows 首次配对、Just Works、加密和 CCC 订阅正常
[ ] 五键默认/APP 配置映射在 HID 输入中正常生效
[ ] 每次 click 都有正确 release，无卡键
[x] 模式切换条件确认为 `num_idx == 4`
[ ] 产品 KEY5（IO_NUM4/PC2）三击进入 Config，先断开 HOGP 再切广播
[ ] 产品 KEY1（IO_NUM0/PB2）三击不触发模式切换
[ ] 再次三击恢复 HOGP 广播并可回连
[ ] HOGP owner 下 RDX APP 属性写/notify/OTA 被拒绝
[ ] Config owner 下 HID read/write/notify 被拒绝
[ ] 模式往返压力 100 次
[ ] 切换中关机或复位后无旧 timer、旧 handle 回调
[ ] suspend 或加密失败时不发送业务 Report
[ ] Config 模式下原 RDX BLE APP、OTA 和断连恢复无回归
[x] APP SET_KEYMAP 可下发五键 keymap 并应用到 HID 输入
[x] APP GET_KEYMAP 可读回完整 35-byte keymap
[x] keymap 可写入 VM 并在重新初始化后恢复
[x] 100 字符 GET_KEYMAP 回包不再触发 `Chip Exception`
```

硬件测试已确认 APP 正式配键主路径可用；完整产品回归仍需覆盖上表中未完成的模式切换、owner 隔离、压力和异常场景。

## 8. 完成定义

### 8.1 Profile v1 软件收尾

以下软件工作已经完成：

- HOGP runtime、Profile、Mode Controller、Key Action 的模块边界已落地。
- 单 GATT Server、单 `app_ble` handle 架构保持不变。
- Profile v1 外部契约已冻结并由 host 测试保护。
- RDX Config 与 HOGP connection owner 授权已经落地。
- T2620 默认 HOGP、五键 CLICK 路由和正式 APP keymap 配置路径已经接线。
- APP 下发 keymap 可以应用到 executor，并可通过 VM 持久化恢复。
- 统一 host tests 全部通过。

Profile v1 最终关闭还需要：

- HOGP enabled/disabled 两种固件构建通过。
- 模式切换条件确认为 KEY5/IO_NUM4，并增加对应 host 断言。
- 本文硬件回归项通过。

### 8.2 BLE APP 正式配键

BLE APP 正式配键已经接入当前 Profile v1 实现，属于 HOGP HID 模块完整实现的一部分。

该功能当前状态必须统一表述为：

> BLE APP 通过 RDX `custom/hogpkm` v1 协议下发完整五键 keymap；固件完成帧解析、CRC/usage 校验、revision/idempotency、executor apply、A/B VM 持久化、SET/GET/CAPS/RESET 回包。硬件测试已确认可以接收并应用下发键值，且可持久化恢复。当前仍需完成模式切换键索引修正、HOGP disabled 构建和完整硬件回归。
