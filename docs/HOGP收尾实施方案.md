# HOGP 收尾实施方案（当前代码事实版）

> 文档状态：已同步当前 `rdx_protocol` 实现与 host 契约测试结果；R3/R4/R5-A 软件收尾已关闭，R5-B/R5-C 等 BLE App 配置协议。
> 适用项目：VibeCoding Keyboard / T2620 / JL AC701N（BR28）。
> 当前判断：R3 配置收口、R4 Mode Controller 轻量独立化、R5-A Key Action 内部整理已按当前软件验收关闭；Profile v1 软件侧收尾基本完成。剩余需要固件 enabled/disabled 构建与硬件回归确认。R5-B/R5-C 产品化部分等待 BLE App 配置协议。

## 1. 收尾目标

HOGP 当前已经超过 MVP 状态。HID Profile、ATT read/write、广播、owner 授权、默认 HOGP、五键测试路径都已经落地，并通过 host 静态契约检查。

本轮收尾目标保持不变：

1. 不破坏当前 Profile v1 外部契约。
2. 不新增第二个 GATT Server 或第二个 `app_ble` handle。
3. 不改变 HID handle、Report Map、Input Report payload。
4. 不让 HOGP 传输层承担 RDX App 配置协议、Flash 保存、Keymap/Macro/Layer 解释。
5. 为后续 BLE App 按键设置、Key Action Executor 产品化、PC Agent、HFP/Voice 和 Profile v2 留出稳定边界。

## 2. 当前代码基线

### 2.1 模块职责

| 文件 | 当前职责 |
|---|---|
| `rdx_ble_server.c/.h` | 单 `app_ble` handle、总 ATT 表、RDX App BLE 通道、mode request facade、owner 授权入口、广播/断连/suppression/runtime sync 等 BLE 实际调度 |
| `rdx_ble_mode_controller.c/.h` | BLE mode controller 状态管理：requested/advertised mode、connection owner、switch pending、有效默认模式 |
| `rdx_hogp_keyboard.c/.h` | HOGP runtime 状态、HID ATT read/write、Protocol Mode、Control Point、CCC、加密状态、Input Report 当前值、Keyboard Report notify、HOGP 广播 payload |
| `rdx_hogp_profile.c/.h` | HID handle 宏、UUID、Report Map、HID Information、Report Reference 常量、ATT 表展开宏 |
| `rdx_hogp_config.h` | HOGP 编译期开关、默认 BLE mode、key-up delay、加密、广播名、Appearance、日志默认值；主动 include `app_config.h` 以消除 include 顺序风险 |
| `rdx_hogp_key_action.c/.h` | 当前五键测试 keymap、默认空 keymap、RAM active keymap、Keyboard Report 转换、click 后 release timer |
| `rdx_app.c` | IO NUM 物理键分发、KEY1 三击模式切换、CLICK 转发到 Key Action，保留旧 RDX key table |
| `tests/host/test_hogp_profile_contract.ps1` | Profile v1、模块边界、配置门控、mode controller、Key Action 测试路径的静态契约检查 |

### 2.2 冻结的 Profile v1 契约

以下内容本轮不得改变：

| 项目 | 当前值 |
|---|---|
| HID Service handle range | `0x0016-0x0022` |
| Input Report value handle | `0x001a` |
| Input Report CCC handle | `0x001b` |
| Report Map handle | `0x001e` |
| HID Information handle | `0x0020` |
| HID Control Point handle | `0x0022` |
| Output Report 兼容债务 | `0x0028-0x002a`，仍在 Device Information Service 后，受 `TCFG_RDX_HOGP_ENABLE` 门控 |
| Report Map | 70 字节，Profile v1 冻结 |
| Notify payload | 8 字节 `[modifier, reserved, key1..key6]`，不前置 Report ID |
| BLE 架构 | 复用 RDX 单 `app_ble` handle 和单份静态 ATT 数据库 |
| T2620 默认模式 | `RDX_BLE_DEFAULT_MODE_HOGP` |
| HOGP 名称来源 | 默认复用 RDX Server local name |

Consumer Control、规范化 Output Report 位置、Report ID 策略、Windows GATT cache 迁移、独立 BLE App 配置 Service 都属于 Profile v2 或专项设计范围。

### 2.3 当前验证结果

在 macOS 上直接执行子测试：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File ./tests/host/test_t2620_config_overlay.ps1
pwsh -NoProfile -ExecutionPolicy Bypass -File ./tests/host/test_hogp_profile_contract.ps1
```

结果：

- `test_t2620_config_overlay.ps1` 通过。
- `test_hogp_profile_contract.ps1` 通过，当前为 133 项 HOGP profile contract checks。

注意：`tests/host/run_host_tests.ps1` 当前硬性查找 `powershell.exe`。在 Windows PowerShell 环境中这是统一入口；在 macOS 上需要单独跑子脚本，或后续改 runner 同时支持 `pwsh`。

固件编译和硬件回归仍必须单独执行。host 脚本不能替代 Windows 配对、断连切换、CCC 订阅、真实按键 notify 和 on-device 压力测试。

## 3. 当前收尾状态

| 收尾项 | 状态 | 判断 |
|---|---|---|
| R1：Profile 字节表与宏统一 | 已完成 | HID block 和 Output Report block 已使用 `RDX_HOGP_ATT_*` 宏展开 |
| R2：HOGP 与 Server 生命周期解耦 | 按当前验收已完成 | HOGP 不再主动断连、不读取 Server 连接状态、不恢复 Config 广播。剩余 owner 查询依赖归入 R4 |
| R3：配置项收口 / include 顺序安全 | 已完成 | 配置入口集中到 `rdx_hogp_config.h`，release delay 配置化，默认 mode fallback 不再散落在 Server |
| R4：Mode Controller 独立化 | 已完成 | 状态已拆到 `rdx_ble_mode_controller.c/.h`；Server 继续负责 BLE side effects |
| R5：Key Action Executor 产品化 | 拆分处理 | R5-A 已完成最小内部整理；R5-B/R5-C 等 BLE App 配置协议 |

一句话结论：P0 的 Profile v1 软件侧核心收尾已经落地，R3/R4/R5-A 已按当前验收关闭。下一步重点是固件 enabled/disabled 构建和硬件回归。R5-B/R5-C 不应在 BLE App 配置协议未定时深度产品化。

## 4. 已关闭事项

### 4.1 R1 已关闭：Profile 字节表与宏统一

代码事实：

- `rdx_hogp_profile.h` 已定义 HID handle、UUID、Report Reference、ATT 属性展开宏。
- `rdx_ble_server.c` 中 `rdx_profile_data[]` 的 HID Service block（`0x0016-0x0022`）使用 `RDX_HOGP_ATT_PRIMARY_SERVICE_16`、`RDX_HOGP_ATT_CHARACTERISTIC_16`、`RDX_HOGP_ATT_VALUE_16`、`RDX_HOGP_ATT_VALUE_16_U8`、`RDX_HOGP_ATT_CCC`、`RDX_HOGP_ATT_REPORT_REFERENCE`。
- Output Report block（`0x0028-0x002a`）也使用同一套宏和 profile 常量。
- host 测试已经检查 HID block 必须使用 `RDX_HOGP_ATT_*` 宏，并验证 attribute 顺序、handle、properties、UUID、Report Reference 值。

后续要求：

- 不再手写 HID handle magic number。
- 如果调整 profile 宏或 `rdx_profile_data[]`，必须保持 host 快照完全通过。
- Profile v1 不改 handle、不改 Report Map、不改 notify payload。

### 4.2 R2 已关闭：HOGP 不再拥有 BLE Server 生命周期决策

代码事实：

- `rdx_hogp_keyboard.c` 不再直接调用 `rdx_ble_server_app_disconnect()`。
- `rdx_hogp_keyboard.c` 不再调用 `rdx_ble_server_adv_enable()` 或 `rdx_ble_server_get_local_name()`。
- `rdx_hogp_keyboard.c` 不再读取 `rdx_ble_server_get_info()->ble_conn` 或 `adv_interval_min`。
- `rdx_hogp_adv_start(u16 adv_interval_min, const char *local_name)` 由 Server 注入广播间隔和名称。
- 断连、pending mode apply、HOGP/Config 广播恢复、DUT/Poweroff/WiFi/SD format 抑制都在 Server mode controller 中决策。

R4 关闭后的边界：

- `rdx_hogp_keyboard.c` 不再 include `rdx_ble_server.h`。
- HOGP owner 查询来自 `rdx_ble_mode_controller.h`。
- Server facade 继续对外提供带 BLE 副作用的 mode request wrapper，避免把断连/广播策略下沉到 HOGP。

后续要求：

- HOGP 可以维护 runtime active 标志、构造 HID advertising payload、发送 Keyboard Report。
- HOGP 不得恢复 RDX Config 广播。
- HOGP 不得主动决定断连或模式切换。

### 4.3 Phase 6 C1-C5 静态契约已落地

| 契约 | 当前状态 |
|---|---|
| C1 Mode Controller 私有状态机 | 已拆到 `rdx_ble_mode_controller.c`，Server 只执行 BLE side effects |
| C1 owner 授权 | HID read/write、Output Report、RDX App 写、RDX notify/OTA send 均有 owner 检查 |
| C3 模块边界 | HOGP 公共 API 头不 include 配置头；Server 头不传递 HOGP 头 |
| C3 disabled stubs | `TCFG_RDX_HOGP_ENABLE=0` 分支覆盖 HOGP 公共函数 |
| C4 HOGP runtime | Protocol Mode、Control Point、加密、suspend、当前 report 同步已检查 |
| C5 默认 HOGP | T2620 overlay 设置 `RDX_BLE_DEFAULT_MODE_HOGP` |
| C5 Key Action 与模式切换 | KEY1 三击是正式 HOGP/Config 切换入口，不受测试键表开关控制；五键 CLICK 按 HID 连接状态在 executor 与离线 key table 间互斥分发；LONG/HOLD/UP 回旧 key table |

## 5. 已关闭：R3 配置项收口

优先级：P1，当前已按 host 静态契约关闭。

### 5.1 已解决问题

1. `rdx_hogp_config.h` 主动 include `app_config.h`，项目 overlay 在任意 include 顺序下都能先解析。
2. Key Action release delay 已收口为 `TCFG_RDX_HOGP_KEY_UP_DELAY_MS`，不再在 executor 内硬编码私有 `20 ms` 宏。
3. `RDX_BLE_DEFAULT_MODE` 默认值 fallback 已集中到 `rdx_hogp_config.h`，`rdx_ble_server.c` 不再兜底。

### 5.2 当前实现

- HOGP/Key Action 默认配置集中在配置头。
- 项目级覆盖仍只放在 `t2620_project_config.h` 或项目 overlay。
- HOGP 公共 API 头不 include 配置头。
- 任意 `.c` 文件包含 HOGP resolved config 时，不会因为 include 顺序导致 HOGP 被静默关闭。
- 修改 `TCFG_RDX_HOGP_KEY_UP_DELAY_MS` 后，click release timer 使用新值。
- 当前采用直接在 `rdx_hogp_config.h` include `app_config.h` 的实现，没有新增 resolved config 文件。

```c
/* rdx_hogp_config.h */
#include "app_config.h"

#ifndef RDX_BLE_DEFAULT_MODE_CONFIG
#define RDX_BLE_DEFAULT_MODE_CONFIG           0
#endif

#ifndef RDX_BLE_DEFAULT_MODE_HOGP
#define RDX_BLE_DEFAULT_MODE_HOGP             1
#endif

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

### 5.3 R3 验收结果

- `rdx_hogp_key_action.c` 不再定义硬编码 `RDX_HOGP_KEY_ACTION_RELEASE_DELAY_MS 20`。
- `sys_timeout_add(... release_timer_cb, ...)` 使用 `TCFG_RDX_HOGP_KEY_UP_DELAY_MS`。
- `RDX_BLE_DEFAULT_MODE` fallback 不再散落在 `rdx_ble_server.c`。
- `RDX_BLE_DEFAULT_MODE_CONFIG` / `RDX_BLE_DEFAULT_MODE_HOGP` 不再要求 include `rdx_ble_server.h` 才能使用。
- host 测试已覆盖 release delay 配置来源、include 顺序约束和默认 mode fallback 位置。
- `TCFG_RDX_HOGP_ENABLE=1` 和 `TCFG_RDX_HOGP_ENABLE=0` 固件构建仍需在 JL toolchain 环境单独确认。

## 6. 已关闭：R4 Mode Controller 轻量独立化

优先级：P1，当前已按轻量 Server-driven 模型关闭。

原方案把 R4 定义为触发式拆分：当 RDX App 按键设置、PC Agent 或 HFP 策略开始调用模式切换时再拆。当前代码已经提前完成轻量独立化，拆分仅移动状态与窄查询 API，不新增业务策略。

### 6.1 当前状态

Mode Controller 状态现在位于 `rdx_ble_mode_controller.c/.h`：

- `rdx_ble_mode_t`
- `rdx_ble_connection_owner_t`
- `s_ble_mode`
- `rdx_ble_mode_is_hogp_requested()`
- `rdx_ble_connection_owner_is_hogp()`
- `rdx_ble_mode_get_requested()` / `rdx_ble_mode_get_advertised()`
- `rdx_ble_mode_request_set()`
- `rdx_ble_connection_owner_get()` / `rdx_ble_connection_owner_set()`

`rdx_ble_server.c` 继续负责带副作用的行为：

- `rdx_ble_mode_request_hogp()`
- `rdx_ble_mode_request_toggle()`
- 广播开始、断连后恢复、pending apply、suppression 判断等 helper

公共头 `rdx_ble_server.h` 继续只暴露窄 wrapper，没有暴露 mode enum 和 owner enum。这一点需要保留。

### 6.2 当前 API

已新增文件：

```text
rdx_ble_mode_controller.h
rdx_ble_mode_controller.c
```

公共 API：

```c
typedef enum {
    RDX_BLE_MODE_CONFIG = 0,
    RDX_BLE_MODE_HOGP,
} rdx_ble_mode_t;

typedef enum {
    RDX_BLE_OWNER_NONE = 0,
    RDX_BLE_OWNER_CONFIG,
    RDX_BLE_OWNER_HOGP,
} rdx_ble_connection_owner_t;

void rdx_ble_mode_controller_init(void);
void rdx_ble_mode_controller_reset(void);

int  rdx_ble_mode_request_set(rdx_ble_mode_t mode);
u8   rdx_ble_mode_is_hogp_requested(void);

rdx_ble_mode_t rdx_ble_mode_get_requested(void);
rdx_ble_mode_t rdx_ble_mode_get_advertised(void);
void rdx_ble_mode_set_advertised(rdx_ble_mode_t mode);
u8   rdx_ble_mode_switch_pending(void);
void rdx_ble_mode_clear_pending(void);

rdx_ble_connection_owner_t rdx_ble_connection_owner_get(void);
void rdx_ble_connection_owner_set(rdx_ble_connection_owner_t owner);
u8   rdx_ble_connection_owner_is_hogp(void);
```

底层 BLE 操作仍由 Server 执行：

- 断连；
- 开关 advertising；
- 设置 HOGP advertising；
- 设置 Config advertising；
- 读取 `ble_conn` / `ble_con_handle`；
- 检查 DUT/Poweroff/WiFi/SD format suppression。

Mode Controller 只管理状态和决策，不直接访问 HOGP profile bytes，不解释 keymap，不处理 VM/HFP/PC Agent 业务。

对外兼容状态：

- 现有 `rdx_ble_mode_request_hogp(u8 enable)` 和 `rdx_ble_mode_request_toggle(void)` 行为不变。
- 这些带副作用的请求入口继续由 `rdx_ble_server.c` 作为 facade 提供，由 Server 调用 Mode Controller 状态 API 后自行决定是否断连、是否启动广播。
- 后续如果需要让 RDX App/PC Agent/HFP 直接依赖 Mode Controller，再把 facade 收敛到 `rdx_ble_mode_controller.h`。

### 6.3 实际拆分方式

为降低风险，R4 只搬现有行为，不新增策略。

当前采用 Server-driven 模型，不引入回调注册：

1. `rdx_ble_mode_controller.c` 保存 `requested_mode`、`advertised_mode`、`connection_owner`、`switch_pending`。
2. `rdx_ble_mode_controller.c` 提供 getter/setter/request state API。
3. `rdx_ble_server.c` 在 init、connect、disconnect、adv refresh、mode request wrapper 中主动查询 mode controller 状态，并继续执行断连、广播、suppression、HOGP runtime sync 等底层动作。

这样可以避免 Mode Controller 反向调用 Server，也避免新增 callback 注册生命周期。

保守边界：

- advertising/断连 helper 仍留在 Server。
- DUT/Poweroff/WiFi/SD format suppression 仍留在 Server。
- HOGP runtime sync 可以先留在 Server，后续有必要再抽象。
- HOGP 只 include `rdx_ble_mode_controller.h` 获取 owner 查询。

### 6.4 R4 验收结果

- `rdx_hogp_keyboard.c` 不再 include `rdx_ble_server.h`。
- HOGP owner 查询来自 `rdx_ble_mode_controller.h`。
- `rdx_ble_server.h` 不暴露 `rdx_ble_mode_t` / `rdx_ble_connection_owner_t` 私有状态字段。
- 模式切换行为保持：连接中 request 先断连，断连后 force apply，按当前 advertised identity 恢复广播。
- DUT/Poweroff/WiFi/SD format suppression 行为保持在 Server。
- 未引入 Mode Controller 到 Server 的 callback 注册机制。
- host `C1_*`、`C3_*`、`C5_*` 契约已更新并通过。

## 7. R5 Key Action Executor 拆分策略

R5 产品化深度受 BLE App 配置协议约束。在不知道 APP 下发 keymap 格式、VM 持久化契约、配置事务语义前，不应贸然实现宏、Layer、VM 或完整配置下发。

因此将 R5 拆成三段：

| 阶段 | 状态 | 范围 |
|---|---|---|
| R5-A | 已完成 | 不依赖 BLE App 协议的内部整理，不新增物理按下/抬起语义 |
| R5-B | 暂缓 | BLE App keymap 下发数据结构、RAM ABI、VM 持久化 |
| R5-C | 暂缓 | Macro、Layer、配置事务、回滚、Profile v2 关联能力 |

### 7.1 R5-A：已完成的内部整理

已落地：

1. 增加内部显式转换函数：

```c
static void rdx_hogp_key_action_to_keyboard_report(
    const rdx_hogp_key_action_keyboard_t *action,
    rdx_hogp_keyboard_report_t *report);
```

该函数只做：

- `report->modifiers = action->modifiers`;
- `report->reserved = 0`;
- copy `usages[6]`。

2. 继续复用原生按键事件框架，不新增 HOGP 私有 press/release 语义。

约束：

- `rdx_app.c` 暂时仍只把 `KEY_ACTION_CLICK` 接到 executor。
- `KEY_ACTION_LONG`、`KEY_ACTION_HOLD`、`KEY_ACTION_HOLDUP` 继续回旧 key table，直到产品按键语义确定。
- HOGP click path 内部发送一次 key-down report，再用短 delay 发送 key-up report。这个 delay 是 HID 报告释放补包，不代表用户可感知的物理抬起事件。
- 如果后续产品需要长按、保持、组合键、hold-tap 或 layer modifier，应优先从原生 `KEY_ACTION_*` 事件进入 executor，而不是另建一套 HOGP 物理按下/抬起框架。
- 不做多键并发状态机、hold-tap、layer modifier、rollover 策略。

3. 测试 keymap 与默认 keymap 加载骨架：

```c
#if RDX_HOGP_KEY_ACTION_TEST_ENABLE
    load_test_keymap();
#else
    load_default_keymap();
#endif
```

当前 `rdx_hogp_key_action_load_default_keymap()` 只是清空 active keymap，不做 VM 恢复。

注释要求：

- 当前默认 keymap 为空属于预期行为。
- 非测试模式下，CLICK 进入 executor 后应返回未消费，继续回退到旧 key table。
- 不要在 BLE App 配置协议未定前把空默认 keymap 解释成 VM 恢复失败。

4. release delay 已由 R3 收口到配置项：

```c
#ifndef TCFG_RDX_HOGP_KEY_UP_DELAY_MS
#define TCFG_RDX_HOGP_KEY_UP_DELAY_MS         20
#endif
```

### 7.2 R5-A 验收结果

- 测试键表与后续正式键表共用同一个 `click()` executor。
- `KEY1 TRIPLE_CLICK` 是正式模式切换入口，不依赖测试键表开关。
- `LONG/HOLD/UP` 不被测试 executor 消费。
- `rdx_hogp_key_action.c` 不访问 advertising、disconnect、VM、HFP、mode private state。
- 转换函数集中处理 `reserved = 0`。
- 不新增 HOGP 私有 press/release API；多种按键事件继续由原生按键框架提供。
- host 契约新增 `C5_R5A_REPORT_CONVERSION`、`C5_R5A_KEYMAP_LOADERS`、`C5_R5A_NO_PRIVATE_PRESS_RELEASE_API` 并通过。

### 7.3 R5-B：等待 BLE App 配置协议

暂缓内容：

- keymap 下发 RAM ABI；
- 结构体版本、长度、CRC；
- 字段对齐和大小端；
- 整表替换还是增量更新；
- VM 持久化格式；
- 配置失败回滚策略；
- 出厂默认 keymap 与用户 keymap 的切换策略。

### 7.4 R5-C：等待产品需求和 Profile v2

暂缓内容：

- Macro 执行引擎；
- Macro delay 表示方式；
- Layer 状态机；
- Layer 切换规则；
- Consumer Control；
- 高层 action 解释器；
- 配置事务、预览/应用/确认；
- PC Agent 和 HFP/Voice 共存策略。

## 8. 本轮明确不做

以下内容不进入当前 Profile v1 收尾：

1. 不改变 `config_le_gatt_server_num`。
2. 不新增第二个 `app_ble` handle。
3. 不调整 HID handle 布局。
4. 不修改 70 字节 Report Map。
5. 不给 Input Report payload 增加 Report ID 前缀。
6. 不把 Output Report 移回 HID Service。
7. 不加入 Consumer Control。
8. 不处理 Windows GATT cache 迁移策略。
9. 不新建独立 BLE App 配置 GATT Service。
10. 不在 HOGP 传输层实现 Flash 保存、Keymap 解释、宏解释、Layer 或 HFP 策略。
11. 不在 BLE App 配置协议未定前写 VM keymap 格式。
12. 不改变当前 LONG/HOLD/UP 回旧 key table 的行为。
13. 不为当前测试路径新增 HOGP 私有 press/release 按键框架。

## 9. 提交拆分状态

R3/R4 对应的提交范围已经明确，后续提交可按以下语义整理：

```text
config(hogp): centralize HOGP defaults and key-up delay
test(hogp): cover config include order and release delay source
refactor(ble): extract BLE mode controller state from server
test(hogp): update mode controller boundary checks
refactor(hogp): normalize key action report conversion
docs(hogp): update closing plan to current code state
```

当前判断：

- 第 1-2 个提交关闭 R3，当前代码已满足。
- 第 3-4 个提交关闭 R4，当前代码已满足。
- 第 5 个提交只做 R5-A 的非行为整理，当前代码已满足。
- R5-B/R5-C 不进入本轮。

## 10. 回归要求

### 10.1 Host 侧

Windows 统一入口：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\host\run_host_tests.ps1
```

macOS 当前可直接运行子脚本：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File ./tests/host/test_t2620_config_overlay.ps1
pwsh -NoProfile -ExecutionPolicy Bypass -File ./tests/host/test_hogp_profile_contract.ps1
```

新增或扩展检查：

- `rdx_hogp_config.h` 或 resolved config 不再依赖调用方 include 顺序。
- release delay 来自 `TCFG_RDX_HOGP_KEY_UP_DELAY_MS`。
- `RDX_BLE_DEFAULT_MODE` 默认值入口集中。
- R4 后 HOGP 不 include `rdx_ble_server.h` 获取 owner。
- Mode Controller 公共头不暴露不必要的 Server internals。
- R5-A 后 LONG/HOLD/UP 仍不被测试路径消费。
- R5-A 后没有新增 HOGP 私有 press/release API。
- R5-A 后 report conversion 和 default/test keymap loader 边界保持在 executor 内。

### 10.2 固件构建

```text
[ ] TCFG_RDX_HOGP_ENABLE=1 全量编译
[ ] TCFG_RDX_HOGP_ENABLE=0 全量编译
[ ] git diff --check
[ ] 确认 SDK/cpu/br28/tools/、output/ 等生成二进制不进入提交
```

### 10.3 硬件回归

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

## 11. 当前完成定义

### 11.1 Profile v1 收尾完成

Profile v1 收尾完成需要满足：

- R1 已完成：Profile 字节表与宏统一。
- R2 已完成：HOGP 不再拥有 Server 生命周期决策。
- R3 已完成：配置项收口，include 顺序安全，release delay 配置化。
- R4 已完成：Mode Controller 状态轻量独立化，HOGP 不再依赖 Server 私有状态。
- 当前 host tests 全部通过。
- HOGP enabled/disabled 构建通过。
- 硬件回归通过。

### 11.2 Key Action 产品化完成

Key Action 产品化不作为当前 Profile v1 收尾的阻塞项。

它需要等 BLE App 配置协议明确后再定义：

- APP 下发 keymap 格式；
- RAM ABI；
- VM 格式；
- 配置事务和回滚；
- Macro/Layer 表达方式；
- PC Agent / HFP / Profile v2 共存边界。

当前只保留五键测试路径、默认空 keymap 和最小 executor 边界，确保硬件可以验证 HOGP Profile v1 的输入链路。
