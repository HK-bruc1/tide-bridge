# HOGP 模块化重构方案（代码事实版）

> 适用项目：VibeCoding Keyboard / T2620 / JL AC701N（BR28）
> 文档定位：本文件合并了原重构方案与收尾实施方案，是 HOGP Profile v1 的唯一重构与收尾依据。
> 当前结论：模块化重构及 host 侧软件契约已经落地；模式切换键索引存在一处已确认的不一致，固件双配置构建和硬件回归仍需完成。

## 1. 关键产品状态

### 1.1 当前按键输入是测试路径

**BLE APP 的键值下发形式尚未确定，因此正式按键配置路径没有接入。当前固件使用内置五键测试 keymap 验证 HOGP 输入链路，不能视为已经支持 BLE APP 配键。**

代码事实如下：

- T2620 在 `t2620_project_config.h` 中设置 `RDX_HOGP_KEY_ACTION_TEST_ENABLE=1`。
- `rdx_hogp_key_action_init()` 因此调用 `rdx_hogp_key_action_load_test_keymap()`，把固件内置测试表载入 RAM active keymap。
- 当前测试 keymap 按 `num_idx=0..4` 顺序加载，映射关系如下。
- 关闭测试开关后，`rdx_hogp_key_action_load_default_keymap()` 只会清空 active keymap，不会从 BLE APP 或 VM 恢复正式配置。
- `rdx_hogp_key_action_keymap_apply()` 目前只是 RAM 内部接口；仓库中没有 BLE APP 命令解析或其他生产调用方。
- 当前未定义 APP 下发帧格式、版本/长度/校验、大小端与对齐、整表或增量更新、VM 持久化 ABI、事务和失败回滚。

| 产品键 | key value | `num_idx` | GPIO | 测试动作 |
|---|---|---:|---|---|
| KEY1 | `KEY_IO_NUM0` | 0 | PB2 | `Ctrl+C` |
| KEY2 | `KEY_IO_NUM1` | 1 | PG7 | `Ctrl+V` |
| KEY3 | `KEY_IO_NUM2` | 2 | PB4 | `Ctrl+X` |
| KEY4 | `KEY_IO_NUM3` | 3 | PG8 | `Backspace` |
| KEY5 | `KEY_IO_NUM4` | 4 | PC2 | `Enter` |

该映射同时由 `sdk_config.c` 的 GPIO 配置、`app_main.c` 的 KEY-GPIO 日志、`rdx_key.c` 的旧按键表注释和 HOGP 测试 keymap 交叉确认。

因此，正式路径必须等 BLE APP 键值协议确定后再接入：

```text
RDX BLE APP 业务命令
    -> 数据校验与配置事务
    -> RAM active keymap / VM 持久化
    -> Key Action Executor
    -> 8-byte Keyboard Report
    -> HOGP notify
```

HOGP 传输层不解析 APP 配置帧，不保存 Flash，也不解释 Keymap、Macro 或 Layer。

### 1.2 当前物理按键路由

`rdx_app_earphone_key_remap()` 的当前行为为：

- HOGP 已连接时，五个 IO NUM 键的 `KEY_ACTION_CLICK` 进入 `rdx_hogp_key_action_click()`，并由 executor 发送按下 Report、延时后发送全零释放 Report。
- HOGP 未连接时，CLICK 回退到原 RDX key table。
- `LONG`、`HOLD`、`HOLDUP` 等其他事件继续走原 RDX key table，不由当前 HOGP 测试 executor 消费。
- 模式切换入口不受测试 keymap 开关控制，它属于正式模式控制路径；只有五键 HID 动作映射仍是测试路径。

### 1.3 已确认的不一致：模式切换键索引

当前模式切换的设计意图、产品映射和实际条件不一致：

- `rdx_app.c` 注释写明 `KEY1 (IO_NUM0) TRIPLE_CLICK` 切换 HOGP/Config。
- 产品映射确认 KEY1=`KEY_IO_NUM0`/PB2，KEY5=`KEY_IO_NUM4`/PC2。
- 实际代码却判断 `num_idx == 4 && index == KEY_ACTION_TRIPLE_CLICK`。

因此当前代码实际由 **KEY5 三击**触发模式切换，不是注释、项目配置注释和测试名称所称的 KEY1 三击。这不是“产品 KEY1 对应 IO_NUM4”的别名关系，而是一处需要收尾修正的键索引不一致。

建议按当前一致的产品映射处理：

1. 将 `rdx_app.c` 的模式切换条件改为 `num_idx == 0`。
2. 保持测试 keymap 的 KEY1-KEY5 顺序不变。
3. 扩展 host 契约，明确断言三击条件绑定 `num_idx == 0`，避免只检查存在 `KEY_ACTION_TRIPLE_CLICK` 和 `rdx_ble_mode_request_toggle()`。
4. 修正后重跑统一 host tests，并在硬件上验证 KEY1 可切换、KEY5 不再切换。

在代码修正和硬件确认前，文档不再把该路径标记为“KEY1 三击已完成”。

## 2. 收尾目标与边界

本轮收尾目标：

1. 保持已经验证的 BLE HID 键盘行为和 Profile v1 外部契约不变。
2. HOGP 继续复用 RDX 的单个 `app_ble` handle 和同一份静态 ATT 数据库。
3. 明确 Server、Mode Controller、HOGP runtime、Profile 常量和 Key Action 的职责边界。
4. 为后续 BLE APP 配键留出接口，但不在协议未定时虚构正式 ABI。
5. 通过 host 契约、固件构建和硬件回归完成收尾。

当前不做：

- 不修改 `config_le_gatt_server_num`，不创建第二个 GATT Server 或第二个 `app_ble` handle。
- 不调整 HID handle、70 字节 Report Map 或 8 字节 Input Report payload。
- 不给 Input Report 增加 Report ID 前缀。
- 不把 Output Report 移回 HID Service；现有兼容布局留待 Profile v2 评估。
- 不新增 Consumer Control、Macro、Layer、hold-tap 或多键并发状态机。
- 不新建独立的 BLE APP 配置 GATT Service；配置能力应复用 RDX BLE APP 业务通道。
- 不在 HOGP 模块中实现 APP 协议、VM、OTA、鉴权或 HFP/Voice 策略。
- 不在 BLE APP 键值下发协议未定前定义生产 keymap/VM 格式。

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
└─ 管理 RAM active keymap、当前内置测试 keymap、Report 转换和 click release timer

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
| `rdx_hogp_key_action.c/.h` | RAM keymap、测试映射、Report 转换和释放 timer | BLE 生命周期、VM、APP 帧解析 |
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
| `0x0028-0x002a` | 兼容 Output Report block，受 `TCFG_RDX_HOGP_ENABLE` 门控 |

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
| Phase 3：正式 Keymap 接入 | 未开始 | BLE APP 键值格式、VM ABI 和配置事务均未确定；当前仅测试 keymap |
| Phase 4：日志诊断 | 已完成 | 状态变化、错误、Report 发送及 `rdx_hogp_dump_state()` 已落地 |
| Phase 5：host 契约 | 已完成 | HOGP profile contract 已纳入统一 host runner |
| Mode Controller 独立化 | 已完成 | 状态位于 `rdx_ble_mode_controller.*`，BLE 副作用仍由 Server 执行 |
| Key Action 最小拆分 | 已完成 | RAM keymap、转换和 release timer 已移出 App/HOGP 传输层 |
| 模式切换物理键绑定 | 待修正 | 设计与产品映射为 KEY1/IO_NUM0，实际条件为 `num_idx == 4`，当前触发键是 KEY5/IO_NUM4 |
| Profile v2 | 不进入本轮 | Output Report 规范化、Consumer Control、Report ID/GATT cache 另行设计 |

## 6. 正式 Key Action 接入条件

BLE APP 协议确定前，`rdx_hogp_key_action_keymap_t` 只能视为 executor 内部 RAM 结构，不能直接固化为无线协议或 Flash ABI。

正式接入前至少需要明确：

1. APP 命令 ID、帧版本、长度、校验和错误码。
2. 物理键编号与产品 KEY1-KEY5 的稳定映射。
3. HID modifier/usages 的编码形式，以及非法 usage 的校验规则。
4. 整表替换或增量更新语义。
5. RAM candidate、apply、confirm、rollback 的事务边界。
6. VM 数据版本、大小端、对齐、CRC、升级兼容和恢复默认策略。
7. 配置成功后的 APP 回包和读取回显。
8. Macro、Layer、Consumer Control 是否进入首版；未确认前不得塞入 v1 keymap。

推荐接入顺序：

1. 在 RDX APP 业务命令层解析并校验配置，不修改 HOGP transport。
2. 定义独立的 wire DTO，显式转换为 executor RAM keymap，不直接透传 C struct。
3. 先对 candidate 做完整校验，再一次性调用 `rdx_hogp_key_action_keymap_apply()`。
4. RAM 路径稳定后再定义带版本和 CRC 的 VM 格式。
5. 增加 host 测试，覆盖错误长度、错误版本、越界 usage、原子替换和重启恢复。
6. 关闭 `RDX_HOGP_KEY_ACTION_TEST_ENABLE`，确认正式配置为空、有效和损坏三种状态的行为。

## 7. 验证状态

### 7.1 已完成的 host 验证

Windows 统一入口：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\host\run_host_tests.ps1
```

本次文档合并时的结果：

- T2620 config overlay：通过。
- HOGP profile contract：137 项检查全部通过。
- RDX local playback configuration：24 项检查全部通过。
- 统一 runner：3 个测试全部通过。

Host 契约主要覆盖：

- Profile handle、Report Map、attribute 顺序、Output Report 门控和 payload。
- disabled stubs、模块 include 边界和职责隔离。
- mode controller、connection owner、模式切换和广播恢复规则。
- HOGP runtime 的 CCC、加密、suspend、当前 Report 和 release 行为。
- T2620 默认 HOGP、内置测试 keymap、CLICK 路由结构和 executor 生命周期。

已知测试缺口：当前 `C5_KEY1_TRIPLE_CLICK_TOGGLE` 只检查 `rdx_app.c` 中存在 `KEY_ACTION_TRIPLE_CLICK` 和 `rdx_ble_mode_request_toggle()`，没有检查三击分支绑定的是 `num_idx == 0`。因此 137 项全部通过不能证明 KEY1/KEY5 的物理索引正确。

### 7.2 尚未完成的固件构建

```text
[ ] TCFG_RDX_HOGP_ENABLE=1 全量编译
[ ] TCFG_RDX_HOGP_ENABLE=0 全量编译
[ ] 确认新增模块均进入最终链接
[ ] 确认 tools/output 生成二进制不作为源码提交
```

### 7.3 尚未完成的硬件回归

```text
[ ] 上电默认出现 HOGP 广播，名称与当前 Server local name 一致
[ ] Windows 首次配对、Just Works、加密和 CCC 订阅正常
[ ] 五键测试映射 Ctrl+C/Ctrl+V/Ctrl+X/Backspace/Enter 正常
[ ] 每次 click 都有正确 release，无卡键
[ ] 修正模式切换条件为 `num_idx == 0`
[ ] 产品 KEY1（IO_NUM0/PB2）三击进入 Config，先断开 HOGP 再切广播
[ ] 产品 KEY5（IO_NUM4/PC2）三击不触发模式切换
[ ] 再次三击恢复 HOGP 广播并可回连
[ ] HOGP owner 下 RDX APP 属性写/notify/OTA 被拒绝
[ ] Config owner 下 HID read/write/notify 被拒绝
[ ] 模式往返压力 100 次
[ ] 切换中关机或复位后无旧 timer、旧 handle 回调
[ ] suspend 或加密失败时不发送业务 Report
[ ] Config 模式下原 RDX BLE APP、OTA 和断连恢复无回归
```

硬件测试当前验证的是内置测试 keymap 链路，不代表 BLE APP 正式配键验收。

## 8. 完成定义

### 8.1 Profile v1 软件收尾

以下软件工作已经完成：

- HOGP runtime、Profile、Mode Controller、Key Action 的模块边界已落地。
- 单 GATT Server、单 `app_ble` handle 架构保持不变。
- Profile v1 外部契约已冻结并由 host 测试保护。
- RDX Config 与 HOGP connection owner 授权已经落地。
- T2620 默认 HOGP 和内置五键 CLICK 测试路径已经接线。
- 统一 host tests 全部通过。

Profile v1 最终关闭还需要：

- HOGP enabled/disabled 两种固件构建通过。
- 模式切换条件修正为 KEY1/IO_NUM0，并增加对应 host 断言。
- 本文硬件回归项通过。

### 8.2 BLE APP 正式配键

BLE APP 正式配键不作为当前 Profile v1 传输收尾的阻塞项，但它是产品按键配置功能完成的必要条件。

该功能当前状态必须统一表述为：

> BLE APP 键值下发形式尚未确定，正式配置路径尚未接入；当前通过固件内置五键测试 keymap 验证 HOGP Profile v1 输入链路。`rdx_hogp_key_action_keymap_apply()` 仅是内部 RAM 接口，不代表 APP 下发、VM 持久化或产品配置已经完成。
