# HOGP 收尾实施方案（代码现状版）

> 文档状态：基于当前代码重新评估后重写。
> 适用项目：VibeCoding Keyboard / T2620 / JL AC701N（BR28）。
> 输入文档：`docs/HOGP模块化重构方案.md`、当前 `rdx_protocol` 代码、`tests/host/test_hogp_profile_contract.ps1`。

## 1. 文档目的

当前 HOGP 已经不是 MVP 形态。Phase 1-5 的主要拆分已经完成，Phase 6 中连接归属、默认 HOGP、关闭态门控、Key Action 测试骨架等内容也已经大部分落地。

本文件不再重复描述已完成的迁移步骤，而是记录当前代码事实、重新评估模块化/可配置化/复用性质量，并定义最后需要关闭的收尾项。

收尾目标保持不变：

1. 不破坏当前 Profile v1 外部契约。
2. 不新增第二个 GATT Server 或第二个 `app_ble` handle。
3. 不改变 HID handle、Report Map、Input Report payload。
4. 不让 HOGP 承担 RDX App 配置协议、Flash 保存、Keymap/Macro/Layer 解释。
5. 为 RDX App 按键设置、Key Action Executor、PC Agent、HFP/Voice 和后续 Profile v2 留出稳定边界。

## 2. 当前代码基线

### 2.1 已实现的模块拆分

当前 HOGP 相关文件职责如下：

| 文件 | 当前职责 |
|---|---|
| `rdx_ble_server.c/.h` | 单 `app_ble` handle、总 ATT 表、RDX App BLE 通道、BLE 模式状态机、owner 授权、广播/断连实际调度 |
| `rdx_hogp_keyboard.c/.h` | HOGP runtime 状态、HID ATT read/write、Protocol Mode、Control Point、CCC、加密状态、Input Report 当前值、Keyboard Report notify、HOGP 广播 payload |
| `rdx_hogp_profile.c/.h` | HID handle 宏、UUID、Report Map、HID Information、Report Reference 常量 |
| `rdx_hogp_config.h` | HOGP 编译期开关、加密、广播名、Appearance、日志默认值 |
| `rdx_hogp_key_action.c/.h` | 当前五键测试 Keymap、RAM active keymap、Keyboard Report 构造、release timer |
| `rdx_app.c` | IO NUM 物理键分发、KEY1 三击模式切换、CLICK 转发到 Key Action，保留旧 RDX key table |
| `tests/host/test_hogp_profile_contract.ps1` | HOGP Profile v1、模块边界、配置门控、模式控制、Key Action 测试骨架的静态契约检查 |

### 2.2 已冻结的外部契约

Profile v1 仍保持以下契约：

| 项目 | 当前值 |
|---|---|
| HID Service handle range | `0x0016-0x0022` |
| Input Report value handle | `0x001a` |
| Input Report CCC handle | `0x001b` |
| Report Map handle | `0x001e` |
| HID Information handle | `0x0020` |
| HID Control Point handle | `0x0022` |
| Output Report 兼容债务 | `0x0028-0x002a`，当前仍在 Device Information Service 后，受 `TCFG_RDX_HOGP_ENABLE` 门控 |
| Report Map | 70 字节，Profile v1 冻结 |
| Notify payload | 8 字节 `[modifier, reserved, key1..key6]`，不前置 Report ID |
| BLE 架构 | 复用 RDX 单 `app_ble` handle 和单份静态 ATT 数据库 |
| T2620 默认模式 | `RDX_BLE_DEFAULT_MODE_HOGP` |
| HOGP 名称来源 | 默认复用 RDX Server local name |

禁止在本轮收尾中改变以上契约。Consumer Control、规范化 Output Report 位置、Report ID 策略和 Windows GATT cache 迁移均属于 Profile v2 范围。

### 2.3 当前验证入口

主机侧验证统一入口：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\host\run_host_tests.ps1
```

该入口当前覆盖：

- T2620 配置覆盖；
- HOGP handle、Report Map、Input Report payload；
- HID Service attribute 顺序和字节级值；
- Output Report 门控；
- BLE mode controller 静态边界；
- HOGP 公共头、配置头、disabled stub；
- Protocol Mode、Control Point、加密状态、当前 Report 同步；
- 默认 HOGP 模式和五键 Key Action 测试骨架。

固件编译和硬件回归仍必须单独执行，host 脚本不能替代 Windows 配对、断连切换、CCC 订阅和真实按键 notify 验证。

## 3. 质量重新评估

### 3.1 模块化质量：B+

已达成：

- HOGP 主要逻辑已经从 `rdx_ble_server.c` 拆出。
- HOGP 状态、ATT read/write、Report 发送集中在 `rdx_hogp_keyboard.c`。
- HID handle、Report Map、HID Information 集中到 `rdx_hogp_profile.h/.c`。
- Server 内部已有 `requested_mode`、`advertised_mode`、`connection_owner`、`switch_pending`。
- ATT read/write、Output Report、RDX App 写入、RDX notify/OTA 入口都有 owner 授权。
- `rdx_app.c` 不再构造 HID report，也不直接调用底层 notify。

主要缺口：

1. `rdx_profile_data[]` 中 HOGP attribute 字节仍是手写 literal，和 `rdx_hogp_profile.h` 的 handle/UUID 宏形成双重事实源。
2. `rdx_hogp_keyboard.c` 仍反向依赖 Server helper，例如断连、广播恢复、`adv_interval_min`、`ble_conn` 和 owner 查询。
3. Mode Controller 仍内嵌在 `rdx_ble_server.c`，当前可接受，但当 RDX App 配置、PC Agent、HFP 策略都需要请求模式时，应独立成模块。
4. `rdx_hogp_mode_get/set()` 仍让 HOGP 保存一份模式状态，和 Server mode controller 存在语义重叠。

### 3.2 可配置化质量：B

已达成：

- `TCFG_RDX_HOGP_ENABLE` 可关闭 HOGP，并提供 disabled stub。
- T2620 项目配置在 `t2620_project_config.h` 中开启 HOGP、设置默认 HOGP 模式、打开测试 Key Action。
- 加密、配对模式、Appearance、名称来源、custom name、日志级别都有默认宏。
- HID Service 和 Output Report 均受主开关门控。
- HOGP 公共 API 头不再包含配置头，降低了传递 include 污染。

主要缺口：

1. `rdx_hogp_config.h` 仍明确依赖 `app_config.h` 先被包含，否则 `TCFG_RDX_HOGP_ENABLE` fallback 可能提前锁成 `0`。
2. Key Action release delay 仍在 `rdx_hogp_key_action.c` 中写死为 `20 ms`，尚未按 Phase 3 目标收口到配置项。
3. `RDX_BLE_DEFAULT_MODE` 的默认值当前在 `rdx_ble_server.c` 兜底，项目覆盖在 `t2620_project_config.h`，配置入口不够集中。
4. 测试 Keymap 开关在 `rdx_app_config.h` 有保守默认，在 T2620 项目覆盖中打开，长期产品配置入口还需要和 RDX App/VM 配置设计对齐。

### 3.3 复用性质量：A-

已达成：

- HOGP 对外提供完整 8 字节 Keyboard Report API。
- `rdx_hogp_keyboard_report_t` 能表达普通键和 modifiers 组合键。
- `rdx_hogp_key_action_keymap_apply()` 提供 RAM active keymap 替换入口。
- 当前 Consumer Control 未混入 Keyboard Report。
- HOGP 传输层不解释物理键、宏、Layer、VM 或 HFP 业务。
- `rdx_hogp_key_action.c` 不控制广播、断连、VM 或 HFP。

主要缺口：

1. `rdx_hogp_key_action_click()` 目前只表达单次 click，并在固定 delay 后 release，尚不能表达 press/release、宏序列、自定义 delay 或 Layer 状态。
2. `rdx_hogp_key_action_keyboard_t` 与 `rdx_hogp_keyboard_report_t` 不完全同构，当前由 click path 手动补 `reserved = 0`，长期应保留明确转换函数。
3. HOGP 仍通过 `rdx_ble_server.h` 获取 owner 查询；Mode Controller 独立后应改为依赖模式控制器公共头。
4. 当前复用目标是“RDX BLE Server 子模块复用”，不是独立 HOGP library。跨项目复用仍需要带上 RDX app_ble/server glue。

## 4. 已关闭事项

以下事项在当前代码中已经达到收尾方案要求，后续只做回归保护：

1. HOGP 逻辑拆分到独立模块。
2. HID handle、Report Map、HID Information 集中管理。
3. 单 `app_ble` handle、单静态 ATT 数据库。
4. HOGP/RDX Config 通过 owner 授权隔离。
5. HOGP 关闭态不暴露 HID Service 和 Output Report。
6. Server exit 调用 HOGP 和 Key Action deinit。
7. 当前 Input Report read 返回最新发送值，release 后归零。
8. Protocol Mode 和 HID Control Point 有合法值处理。
9. HOGP 广播 payload 做容量检查。
10. T2620 默认进入 HOGP，保留五键测试 Keymap 和 KEY1 三击模式切换。
11. Host 契约测试覆盖当前 Profile v1 和 Phase 6 C1-C5 静态边界。

## 5. 剩余收尾项

### R1：关闭 Profile 字节表与宏的双重事实源

优先级：P0。

当前问题：

- `rdx_hogp_profile.h` 定义了 HID handle、UUID、Report Reference 常量。
- `rdx_ble_server.c` 的 `rdx_profile_data[]` 仍手写 HID attribute 字节。
- 当前依赖 host 测试解析注释和字节兜底，一旦人工修改字节表但忘记同步宏，编译期不会直接失败。

目标状态：

- HOGP HID block 的 handle、UUID、value handle、properties、Report Reference 字节由同一套宏展开。
- 至少为每个关键 handle 建立编译期绑定检查。
- Host 契约测试继续保留，作为外部 Profile v1 冻结测试，而不是唯一一致性保障。

建议做法：

1. 在 `rdx_hogp_profile.h` 或 Server 私有头中增加 little-endian 字节展开宏。
2. 将 `rdx_profile_data[]` 的 `0x0016-0x0022` HID block 改成宏展开。
3. Output Report `0x0028-0x002a` 也引用 `HID_OUTPUT_REPORT_VALUE_HANDLE` 和 Report Reference 常量。
4. 保持最终预处理后的字节与当前 host 快照完全一致。

验收标准：

- `rdx_profile_data[]` HOGP block 不再重复手写 HID handle magic number。
- `tests/host/test_hogp_profile_contract.ps1` 通过。
- Report Map、handle、attribute 顺序、notify payload 不变。
- Windows 不删除旧配对时仍能正常输入。

### R2：解耦 HOGP 与 Server 生命周期控制

优先级：P0。

当前问题：

`rdx_hogp_keyboard.c` 仍直接调用或读取 Server 细节：

- `rdx_ble_server_app_disconnect()`；
- `rdx_ble_server_adv_enable()`；
- `rdx_ble_server_get_info()->adv_interval_min`；
- `rdx_ble_server_get_info()->ble_conn`；
- `rdx_ble_connection_owner_is_hogp()`。

其中 owner 查询是合理依赖，但应最终来自 Mode Controller；断连、模式切换和广播恢复不应由 HOGP 决策。

目标状态：

- Mode Controller 决定何时断连、何时进入 HOGP、何时恢复 Config 广播。
- HOGP 只负责：
  - 构造 HID advertising payload；
  - 在 Server 明确要求时设置/停止 HID advertising；
  - 维护 HOGP protocol runtime；
  - 校验 ready 后发送 Keyboard Report。
- `rdx_hogp_mode_set()` 不再承担“发现连接、主动断连、切广播”的完整模式切换逻辑。

建议做法：

1. 将断连前置逻辑完全保留在 `rdx_ble_mode_request()`。
2. 将 `adv_interval_min` 作为参数传入 HOGP advertising start，或由 Server 调用 `rdx_hogp_fill_adv_data()` 后自行设置 adv param/data。
3. 将 `rdx_hogp_mode_get/set()` 降级为内部 runtime active 标志，或删除公开声明。
4. HOGP ready check 中的 owner 查询改为依赖未来 `rdx_ble_mode_controller.h`，当前阶段可先保留 Server wrapper。

验收标准：

- `rdx_hogp_keyboard.c` 不再直接引用 `rdx_ble_server_app_disconnect()`。
- `rdx_hogp_keyboard.c` 不再读取 `rdx_ble_server_get_info()->ble_conn`。
- HOGP 不负责恢复 RDX Config 广播。
- 模式切换、断连后重启广播、DUT/Poweroff/WiFi/SD format 抑制逻辑仍全部通过 host 检查和硬件回归。

### R3：配置项收口并消除 include 顺序风险

优先级：P1。

当前问题：

- `rdx_hogp_config.h` 依赖调用方先 include `app_config.h`。
- Key Action release delay 写死。
- `RDX_BLE_DEFAULT_MODE` 默认值在 Server 中兜底，配置入口分散。

目标状态：

- HOGP/Key Action 相关默认配置集中在配置头。
- 项目级覆盖仍只放在 `t2620_project_config.h`。
- 公共 API 头不包含配置头。
- 任意 `.c` 文件包含 `rdx_hogp_config.h` 时不会因为 include 顺序导致 HOGP 被静默关闭。

建议配置项：

```c
#ifndef TCFG_RDX_HOGP_ENABLE
#define TCFG_RDX_HOGP_ENABLE                  0
#endif

#ifndef TCFG_RDX_HOGP_KEY_UP_DELAY_MS
#define TCFG_RDX_HOGP_KEY_UP_DELAY_MS         20
#endif

#ifndef RDX_BLE_DEFAULT_MODE
#define RDX_BLE_DEFAULT_MODE                  RDX_BLE_DEFAULT_MODE_CONFIG
#endif
```

具体实现可选择以下一种：

1. 让 `rdx_hogp_config.h` 内部包含 `app_config.h`，并确认没有循环 include。
2. 新增私有 `rdx_hogp_config_resolved.h`，由它固定先包含 `app_config.h` 再包含默认配置。
3. 保持公共头无配置依赖，只要求所有 `.c` 统一包含 resolved config 头。

验收标准：

- `rdx_hogp_key_action.c` 不再硬编码 release delay。
- 修改 `TCFG_RDX_HOGP_KEY_UP_DELAY_MS` 后，release timer 使用新值。
- host 测试覆盖配置默认值和 include 顺序约束。
- HOGP enabled/disabled 两种配置均能编译。

### R4：Mode Controller 独立化

优先级：P1，触发条件为 RDX App 按键设置、PC Agent 或 HFP 策略开始调用模式切换。

当前状态：

Mode Controller 作为 `rdx_ble_server.c` 内部静态状态机存在，这是当前阶段可接受的实现。

拆分触发条件：

- RDX App 配置命令需要请求 HOGP/Config 切换；
- PC Agent 需要观察模式事件；
- HFP/Voice 策略需要根据模式进入/退出协调音频链路；
- HOGP 模块需要 owner 查询但不应 include 整个 Server 头。

目标状态：

新增：

```text
rdx_ble_mode_controller.h
rdx_ble_mode_controller.c
```

公共 API：

```c
int rdx_ble_mode_request(rdx_ble_mode_t mode);
void rdx_ble_mode_request_hogp(u8 enable);
void rdx_ble_mode_request_toggle(void);
rdx_ble_mode_t rdx_ble_mode_get_requested(void);
rdx_ble_mode_t rdx_ble_mode_get_advertised(void);
rdx_ble_connection_owner_t rdx_ble_connection_owner_get(void);
u8 rdx_ble_connection_owner_is_hogp(void);
```

Server 仍负责底层 BLE 操作，Mode Controller 只管理状态和决策，必要时通过回调或 Server glue 执行断连/广播。

验收标准：

- `rdx_ble_server.h` 不再暴露内部 mode field。
- HOGP 只 include Mode Controller 公共头获取 owner。
- RDX App/PC Agent/HFP 只通过 Mode Controller 请求或观察模式。
- 模式切换 100 次硬件压力测试通过。

### R5：Key Action Executor 产品化

优先级：P1。

当前状态：

`rdx_hogp_key_action.c` 是 C5 测试骨架，支持五键 click、RAM keymap apply、20 ms release timer。

缺口：

- 不支持 press/release 分离。
- 不支持宏序列和步骤 delay。
- 不支持 Layer 状态。
- 不支持 App 下发配置事务、VM 持久化和回滚。
- `rdx_hogp_key_action_keyboard_t` 与 `rdx_hogp_keyboard_report_t` 之间转换是局部手写逻辑。

目标状态：

- Key Action Executor 负责物理键事件到 Keyboard Report 序列的转换。
- HOGP 仍只发送完整 Keyboard Report。
- Release delay、宏 delay、Layer 切换策略不进入 HOGP 传输层。

建议 API 方向：

```c
int rdx_hogp_key_action_event(u8 key_id, u8 action);
int rdx_hogp_key_action_press(u8 key_id);
int rdx_hogp_key_action_release(u8 key_id);
int rdx_hogp_key_action_sequence_start(const void *sequence);
```

或者让更上层宏执行器直接生成：

```c
rdx_hogp_keyboard_report_t report;
rdx_hogp_keyboard_report_send(&report);
```

验收标准：

- CLICK 测试路径仍可用。
- KEY1 LONG/HOLD/UP 不被测试路径消费。
- 后续 BLE App keymap 改变时，只更新 Key Action active config，不修改 HOGP 传输层。
- 宏/Layer 实现不访问 HID handle、ATT notify、BLE 广播或连接状态。

## 6. 本轮明确不做

以下内容不进入当前收尾：

1. 不改变 `config_le_gatt_server_num`。
2. 不新增第二个 `app_ble` handle。
3. 不调整 HID handle 布局。
4. 不修改 70 字节 Report Map。
5. 不给 Input Report payload 增加 Report ID 前缀。
6. 不把 Output Report 移回 HID Service。
7. 不加入 Consumer Control。
8. 不处理 Windows GATT cache 迁移策略。
9. 不新建独立 BLE App 配置 GATT Service。
10. 不在 HOGP 中实现 Flash 保存、Keymap 解释、宏解释或 HFP 策略。

这些内容统一归入 Profile v2、RDX App 配置设计、Key Action 产品化或 HFP/PC Agent 专项。

## 7. 推荐提交拆分

建议按以下顺序提交，避免一次提交同时修改 Profile、生命周期、配置和产品行为：

```text
refactor(hogp): bind HID profile data to profile constants
refactor(hogp): move BLE lifecycle decisions out of HOGP keyboard
config(hogp): centralize release delay and default mode config
refactor(ble): extract BLE mode controller when external callers arrive
feat(hogp): extend key action executor for product keymaps
test(hogp): extend host checks for final boundary contracts
docs(hogp): record final HOGP closing state
```

R1 和 R2 优先级最高。R4 可以等 RDX App 配置、PC Agent 或 HFP 策略真正接入时再拆，但拆分前不得让更多模块直接依赖 `rdx_ble_server.c` 私有状态。

## 8. 回归要求

### 8.1 Host 侧

每次收尾提交后运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\host\run_host_tests.ps1
```

新增或扩展检查：

- HOGP HID block 使用 profile 宏或编译期绑定。
- `rdx_hogp_keyboard.c` 不直接调用 Server 断连和读取 `ble_conn`。
- release delay 来自配置宏。
- 公共 API 头无配置 include 依赖。
- Mode Controller 独立后，HOGP 不 include `rdx_ble_server.h` 获取 owner。
- Key Action 产品化后，HOGP 模块仍不出现物理键、宏、Layer、VM、HFP 私有语义。

### 8.2 固件构建

```text
[ ] TCFG_RDX_HOGP_ENABLE=1 全量编译
[ ] TCFG_RDX_HOGP_ENABLE=0 全量编译
[ ] git diff --check
[ ] 确认 SDK/cpu/br28/tools/、output/ 等生成二进制不进入提交
```

### 8.3 硬件回归

```text
[ ] 上电默认 HOGP 广播
[ ] Windows 首次配对、加密、CCC 订阅
[ ] 五键 Ctrl+V、A、Enter、Ctrl+C、Backspace 正常
[ ] release 正常，无卡键
[ ] KEY1 三击进入 Config，先断开 HOGP 再切广播
[ ] KEY1 三击退出 Config，恢复 HOGP 广播并可回连
[ ] HOGP owner 写 RDX App 配置属性被拒绝
[ ] Config owner 写 HID CCC 或发送 Input Report 被拒绝
[ ] 模式往返压力 100 次
[ ] 切换中关机/复位，无旧 timer 或旧 handle 回调
[ ] 加密失败或 Host suspend 后禁止发送业务 Report
[ ] RDX App 原有 BLE 通道、OTA、断连恢复不回归
```

## 9. 最终完成定义

HOGP 收尾完成必须同时满足：

- HOGP Profile v1 的 handle、Report Map、Report payload、广播身份保持不变。
- `rdx_profile_data[]` HOGP block 与 `rdx_hogp_profile.h` 不再是人工双源。
- HOGP 不再负责模式切换决策、主动断连或恢复 RDX Config 广播。
- HOGP 公共 API 不依赖配置 include 顺序。
- release delay 和默认模式等 tunable 有清晰默认值和项目覆盖入口。
- Mode Controller 在外部调用方增加前后有明确边界，不让 PC Agent/RDX App/HFP 直接访问 Server 私有状态。
- Key Action Executor 能承接 RDX App 下发的 active keymap，HOGP 仍只发送完整 Keyboard Report。
- Host 测试、HOGP enabled/disabled 构建、Windows 硬件回归全部通过。

完成后，下一阶段可以并行推进 RDX App 按键设置、VM 配置存储、宏/Layer、HFP 共存、PC Agent 和 Profile v2，而不再修改 HOGP Profile v1 的核心传输契约。
