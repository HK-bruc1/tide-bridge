# HOGP Phase 6 量产化重构方案

> 文档状态：待 RDX 按键设置扩展设计与分阶段实施
> 适用项目：VibeCoding Keyboard / T2620 / JL AC701N（BR28）
> 输入文档：`docs/HOGP模块化重构方案.md`、`docs/vibecoding_keyboard_tech_stack方案.md`

## 1. 文档目的

Phase 1-5 已完成 HOGP 逻辑拆分、Profile 常量集中、编译期配置、诊断日志和主机契约测试。本方案不是简单收尾，而是 HOGP 从 MVP 进入可维护量产架构的 Phase 6 重构。它解决连接归属、模式切换、关闭态、生命周期和模块边界问题，并为以下模块提供稳定接口：

- RDX BLE App 按键设置扩展；
- Keymap、组合键、宏和 Layer 执行器；
- Classic HFP 麦克风与语音触发；
- PC Agent；
- 后续 Consumer Control 和 HOGP Profile v2。

Phase 6 的 C1-C4 不以增加产品功能为目标。除 C5 明确列出的产品身份迁移外，现有 HID handle、Report Map 和 8 字节 Input Report 契约必须保持不变。

> **分支与提交约定**：Phase 6 在当前 `HOGP` 分支执行。每个 Phase 独立提交，提交前必须经过人工评估，禁止把生成二进制（`SDK/cpu/br28/tools/`、`output/` 等）纳入提交。

### 1.1 实施前置条件

进入源码实施前必须先确认两条实施边界：

1. 产品目标已明确为 VibeCoding Keyboard，默认上电 HOGP 键盘是目标形态。JL TWS earphone 只是厂商 SDK 默认工程基座，实施时需要判断哪些 earphone 路径继续复用、哪些只保留为 SDK 依赖。
2. RDX BLE App 按键设置扩展是否已有最小命令设计。如果尚未冻结，本文中的 `RDX_BLE_OWNER_CONFIG` 只作为模式和授权边界，不新增独立配置 GATT 服务。

这两个边界会影响 C5 的提交范围、测试矩阵和对 `app_main.c`、电源管理、TWS 配对、BT 音乐/通话路径的处理策略。

## 2. 产品约束与架构决策

根据产品技术栈方案，第一版采用单 BLE Central 策略：

```text
正常工作模式：BLE HOGP Keyboard + Classic HFP（允许共存）
App 配置模式：RDX BLE App Config，HOGP 断开且停止广播
禁止路径：PC HOGP + 手机 RDX BLE App 两个 BLE Central 并发
```

据此冻结以下决策：

1. 继续复用 RDX 的单个 `app_ble` handle 和单份 ATT 数据库，不新增第二个 GATT Server。
2. BLE Server 负责物理连接、广播和模式切换；HOGP 只负责 HID 协议状态、ATT 和 Report。
3. 正常量产启动模式应为 HOGP；配置模式必须通过模式控制器显式进入。
4. 物理键、单击/双击/长按、宏和 Layer 不属于 HOGP 模块。
5. HOGP 对上层提供“键盘 Report 传输能力”，不直接解释产品按键含义。
6. Consumer Control 需要新 Report Map/Characteristic，放入 Profile v2，不混入本次 Profile v1 稳定化重构。

### 2.1 对现有耳机主路径的影响

当前仓库基于 JL TWS earphone firmware，`app_main.c`、任务表、电源管理、TWS 配对、充电盒逻辑、BT 音乐和 HFP 都来自厂商默认产品形态。VibeCoding Keyboard 是利用该芯片和 SDK 能力重新定义的目标产品，Phase 6 不能在 HOGP 重构中顺手删除这些路径：

- C1-C4 只调整 BLE/HOGP 模块内部边界，不改变 SDK 基座的主应用启动模型。
- C5 才涉及默认上电 HOGP、广播名称和临时测试入口删除，是产品主路径对齐阶段，应单独提交和验收。
- Classic HFP 是产品技术栈的一部分，但启停策略由产品模式协调层决定，不由 HOGP 模块直接控制。
- TWS 左右耳、充电盒、电量上报、BT 音乐和低功耗策略需要逐项判断是复用、裁剪还是保留为空路径；本文只要求 HOGP 不再依赖这些私有状态。

### 2.2 RDX BLE App 按键设置扩展

VibeCoding Keyboard 不新建独立配置 GATT 服务，而是全盘复用现有 RDX BLE App 底座。Phase 6 先冻结以下最小契约，具体命令 ID、payload、CRC 和保存流程在 RDX App 按键设置设计中确定：

- 仍复用单份静态 ATT 数据库和单个 `app_ble` handle，不新增第二个 GATT Server。
- RDX BLE App 原有连接、鉴权、收发、OTA、基础状态和业务命令继续复用。
- 按键设置只新增 RDX 业务命令，不新增独立 handle；如果后续必须追加 handle，必须扩展 host 契约测试冻结 handle、UUID、属性顺序和字节级值。
- `RDX_BLE_OWNER_CONFIG` 表示当前连接归属 RDX App 配置模式；在按键设置命令未落地前，C1/C4 的 owner 授权只校验路由点和禁止跨 owner 访问。
- 测试可用现有 RDX 配置/业务写路由代表 Config owner，验证 HOGP owner 不进入这些写处理，RDX App owner 不启用 HID CCC 或发送 Input Report。
- 若 RDX App 按键设置需要写 Flash、保存 Keymap 或退出配置模式，只能通过 RDX App/Storage/Mode API 转发，不得直接调用 HOGP notify 或修改 HOGP 状态。

## 3. 当前基线与必须修复的问题

当前可工作的 Profile v1 契约为：

- HID Service：`0x0016-0x0022`；
- Report Map：70 字节；
- Input Report：8 字节，不前置 Report ID；
- HOGP 和 RDX 广播互斥；
- `TCFG_RDX_HOGP_ENABLE=1` 为 T2620 当前产品配置。

Phase 6 前存在以下阻断项：

| 编号 | 问题 | 影响 | 级别 |
|---|---|---|---|
| C-01 | `hogp_mode` 同时表示期望模式和连接归属 | 异步断连后可能走错 RDX/HOGP 清理分支 | P0 |
| C-02 | HOGP 连接未同步 RDX `ble_conn`，退出模式依赖该值 | 可能未断开物理连接便恢复 RDX 广播 | P0 |
| C-03 | `0x0028-0x002a` Output Report 未受主开关保护 | HOGP 关闭态仍暴露 HID 属性 | P0 |
| C-04 | Server 退出时未调用 `rdx_hogp_deinit()`，release timer 未取消 | 可能在 handle 释放后触发旧回调 | P0 |
| C-05 | HOGP 公共头经 `rdx_ble_server.h` 传递包含 | API 扩散且配置依赖 include 顺序 | P1 |
| C-06 | Profile handle 宏和 ATT 字节表仍分别维护 | 存在双重事实源，只能依靠测试发现分叉 | P1 |
| C-07 | Input Report 当前值未随发送更新；加密失败不清零 | ATT read 和安全状态可能过期 | P1 |
| C-08 | Protocol Mode 可写但写入未处理 | GATT 声明与行为不一致 | P1 |

## 4. 目标架构

```text
物理按键 / App 配置 / Flash
          |
          v
  Key Action Executor（后续模块）
  - keymap / combo / macro / layer
  - voice trigger
          |
          | keyboard report
          v
  rdx_hogp_keyboard
  - HID ATT read/write
  - Report 当前值与发送
  - CCC / encryption / suspend
          |
          v
  rdx_ble_mode_controller
  - requested_mode
  - advertised_mode
  - connection_owner
  - switch_pending
          |
          v
  rdx_ble_server / app_ble
  - handle、ATT 总表、HCI/SM 事件
  - 物理连接、断连和广播
```

`rdx_ble_mode_controller` 可以先作为 `rdx_ble_server.c` 内部状态机实现；当 Config 模式和更多调用方接入后再独立成 `.c/.h`。本轮不为了文件拆分引入无实际收益的抽象。

### 4.1 状态机不变量

- `requested_mode` 表示用户期望进入的模式。
- `advertised_mode` 表示当前广播身份。
- `connection_owner` 在 `rdx_ble_server_cbk_packet_handler()` 收到 `HCI_SUBEVENT_LE_CONNECTION_COMPLETE` 或 `HCI_SUBEVENT_LE_ENHANCED_CONNECTION_COMPLETE` 时根据 `advertised_mode` 锁定，直到 `HCI_EVENT_DISCONNECTION_COMPLETE` 清理完成前不得改变。
- 连接存在时收到模式切换请求，只记录目标模式并请求断连，不立即切换广播身份。
- 断连事件先分发给原 `connection_owner` 完成清理，再清空连接，最后应用 `requested_mode` 并开始新广播。
- 单份静态 ATT 数据库会同时包含 HOGP 和 RDX App 属性；模式切换不等于隐藏 Service。所有业务写入、CCC 和 notify 必须根据 `connection_owner` 授权。
- 任意时刻不得在物理连接仍存在时启动另一模式的广播。
- Server 停止后必须满足：无连接、无广播、无 HOGP timer、HOGP handle 为 `NULL`。

推荐状态字段：

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
```

### 4.2 ATT 访问边界

Profile v1 不在运行时替换 ATT 数据库，避免 Windows GATT cache 和 handle 变化。连接建立后执行以下约束：

- `RDX_BLE_OWNER_HOGP`：允许 HID read、CCC、Control Point 和 Input Report notify；拒绝 RDX App 配置写入与业务 notify。
- `RDX_BLE_OWNER_CONFIG`：允许 RDX App 读写与 notify；拒绝 HID CCC、Control Point 和 Input Report notify。
- `RDX_BLE_OWNER_NONE`：拒绝所有需要连接上下文的动态写入和 notify。
- Service discovery 可以看到同一 ATT 表中的全部服务，这不代表业务能力在当前模式可用。

Service discovery 可见全部服务是 Profile v1 为避免 Windows GATT cache 失效而接受的已知债务。Host 可能缓存非当前 owner 的服务，因此实现必须保证未授权写入和 notify 在运行时被拒绝，而不是依赖“扫描不到服务”来保证隔离。

RDX App 按键设置扩展必须调用统一 owner 检查，不得自行通过广播名称或 `hogp_mode` 推断连接身份。

## 5. 模块边界与稳定接口

### 5.1 BLE 模式控制器

建议对业务层只暴露：

```c
int rdx_ble_mode_request(rdx_ble_mode_t mode);
rdx_ble_mode_t rdx_ble_mode_get_requested(void);
rdx_ble_mode_t rdx_ble_mode_get_advertised(void);
rdx_ble_connection_owner_t rdx_ble_connection_owner_get(void);
u8 rdx_ble_connection_owner_is(rdx_ble_connection_owner_t owner);
```

App 配置、按键入口和后续 UI 只能请求模式，不得直接调用 `app_ble_adv_enable()`、`app_ble_disconnect()` 或修改 HOGP 内部状态。

模式控制器还应向应用协调层发布 `switch_started`、`mode_entered`、`connected`、`disconnected` 和 `switch_failed` 事件。LED、HFP 策略和 RDX App UI 只观察这些事件，不反向修改状态机字段。

### 5.2 HOGP Report 接口

用完整键盘 Report 替代“物理键序号 -> 单 usage”的模块接口：

```c
typedef struct {
    u8 modifiers;
    u8 reserved;
    u8 usages[6];
} rdx_hogp_keyboard_report_t;

int rdx_hogp_keyboard_report_send(
    const rdx_hogp_keyboard_report_t *report);
int rdx_hogp_keyboard_release_all(void);
u8 rdx_hogp_keyboard_is_ready(void);
```

这样组合键可以直接表达为 `modifiers + usages[]`，宏执行器可以按序发送多个 Report。`rdx_hogp_on_io_num_key()`、`rdx_hogp_key_click_index()` 和旧 `hogp_*` wrapper 应在调用方迁移后删除。

该结构仍对应当前 8 字节 Boot Keyboard Input Report，`usages[6]` 是 6KRO 键数组，不要求修改现有 70 字节 Report Map。

`rdx_hogp_keyboard_is_ready()` 只有在 owner 为 HOGP、物理连接存在、链路满足加密策略、CCC 已启用且 Host 未 suspend 时才返回 1。Key Action Executor 必须根据发送返回值处理失败，不得假定按键已送达。

### 5.3 后续模块责任

| 模块 | 输入 | 输出 | 不允许承担的责任 |
|---|---|---|---|
| Key Input Adapter | `key_id + key_action` | 标准化产品按键事件 | 不构造 ATT/HID 数据 |
| Key Action Executor | 按键事件 + 当前 Keymap/Layer | Keyboard Report、语音动作 | 不控制 BLE 广播和连接 |
| RDX App Key Settings | App 配置帧 | RAM 配置、校验和保存请求 | 不直接发送 HID Report |
| Config Storage | 版本化配置 | 原子加载/保存/回滚 | 不解释 BLE 状态 |
| HOGP | Keyboard Report | ATT notify | 不解释物理键、宏、Layer |
| HFP/Voice | 语音开始/结束动作 | 音频链路状态 | 不修改 HOGP 连接状态 |

Consumer Control 使用独立 Report 类型，不得塞入当前 8 字节 Keyboard Report。Profile v2 开始前必须先冻结 Report ID、Characteristic 和 Windows 缓存迁移策略。

## 6. 配置收口

### 6.1 配置所有权

项目级覆盖继续放在：

```text
SDK/apps/earphone/include/t2620_project_config.h
```

模块默认值继续放在 `rdx_hogp_config.h`，但公共 API 头不再包含配置头，也不再根据开关隐藏声明。关闭态由统一 stub 或链接门控提供，消除“必须先 include `app_config.h`”的顺序依赖。

### 6.2 量产配置建议

```c
#define TCFG_RDX_HOGP_ENABLE                  1
#define RDX_HOGP_ENCRYPTION_REQUIRED          1
#define RDX_HOGP_NAME_SOURCE                  1
#define RDX_HOGP_CUSTOM_NAME                  "VibeCoding Keyboard"
#define RDX_BLE_DEFAULT_MODE                  RDX_BLE_MODE_HOGP
```

配置广播名称建议使用 `VibeCoding Config`。名称从当前 `VibeKeyboard` 迁移属于一次产品身份变更，应作为独立 commit/Phase 执行。Windows 已配对设备可能缓存旧名称和 Service UUID，验收时必须删除旧配对、重新配对并验证自动回连；未完成该验证前不得把 C5 合入量产分支。

`RDX_HOGP_KEY_UP_DELAY_MS` 和固定 A-E keymap 后续应迁移到 Key Action Executor；它们是输入策略，不是 HOGP 传输配置。

## 7. 分阶段实施

推荐落地顺序为 C2 -> C1 -> C3/C4 -> C5 -> C6。C2 风险最低，先消除关闭态和 timer/handle 悬挂；C1 依赖清晰的 deinit/断连清理；C5 涉及产品身份和默认模式，必须在 C1-C4 稳定且 Config 进入/退出路径明确后执行。

### Phase C0：前置确认与开发约定

在进入 C1 源码改动前，必须先完成以下确认并落地方针：

1. **产品目标确认**：默认上电 HOGP 键盘是目标形态，C1-C4 不改 `app_main.c` 启动模型。
2. **RDX App 按键设置扩展确认**：当前不复用独立 Config GATT Service，`RDX_BLE_OWNER_CONFIG` 仅作为授权边界；若后续必须新增 handle，需同步扩展 host 契约测试。
3. **调试入口保留**：C1-C2 阶段保留一个编译期调试入口（如 `RDX_BLE_DEBUG_MODE_SWITCH_KEY` 宏控制的 NUM0 长按），用于本地验证模式切换；C5 产品化时移除。
4. **ATT error code 确认**：C4 实施前需确认 JL BTstack 实际支持的 ATT error code（查 `btstack/le/att.h`），文档中的 `ATT_ERROR_INVALID_HANDLE_VALUE` 为占位，找到实际可用码后更新实现与文档。
5. **增强连接事件确认**：确认 `rdx_ble_server_cbk_packet_handler()` 当前是否已分发 `HCI_SUBEVENT_LE_ENHANCED_CONNECTION_COMPLETE`；如未分发，C1 只处理普通连接完成事件。
6. **HFP 共存预研**：HFP 音频活动时 HOGP 按键丢失/超时涉及任务优先级与 audio 抢占，单独列项跟踪，不在 Phase 6 一次性解决。

### Phase C1：连接与模式状态机收口

目标：解决 C-01、C-02，建立其他 BLE 模式可复用的切换入口。

实施项：

1. 增加 `requested_mode`、`advertised_mode`、`connection_owner`、`switch_pending`。
2. 在 `HCI_SUBEVENT_LE_CONNECTION_COMPLETE` 和 `HCI_SUBEVENT_LE_ENHANCED_CONNECTION_COMPLETE` 锁定 owner，并统一维护物理 connection handle/connected 状态。
3. 模式切换改为“请求断连 -> 等待事件 -> 原 owner 清理 -> 启动目标广播”。
4. HCI 断连事件使用原 owner 分发，不再使用 `hogp_mode_get()` 判断。
5. ATT callback 和 notify 入口增加 owner 授权，模式不匹配时返回明确错误。
6. HOGP 和 RDX App 配置广播统一由 Server 启停。
7. 所有切换步骤增加结构化日志，包含 requested、advertised、owner、pending 和 con_handle。
8. **调试入口**：保留 `RDX_BLE_DEBUG_MODE_SWITCH_KEY` 宏控制的 NUM0 长按/单击用于本地验证；该宏默认开启，C5 关闭。

验收：RDX App 已连接进入 HOGP、HOGP 已连接进入 RDX App 配置模式、广播态直接切换三条路径均无残留连接状态，连续切换 100 次可恢复。

### Phase C2：生命周期与关闭态收口

目标：解决 C-03、C-04。

实施项：

1. 将 Output Report `0x0028-0x002a` 纳入 `TCFG_RDX_HOGP_ENABLE` 门控。
2. Server 释放 `app_ble` handle 前调用 `rdx_hogp_deinit()`。
3. 保存 key release timer ID；本阶段先只支持单个 release timer，新任务覆盖旧任务。deinit/断连/模式退出时取消 timer 并清零当前 Report，队列化策略留给后续 Key Action Executor。
4. 回调执行前校验模块 generation 或有效 handle，禁止使用已释放资源。
5. HOGP 关闭态确认无 HID Service、Report、Report Reference 和 HOGP 广播字段。

> **C2 与 C1 的衔接说明**：C2 基于当前 `hogp_mode` 字段实现临时清理逻辑；C1 完成后会迁移到 `connection_owner` 状态机。C2 commit message 应注明“基于现有模式字段的临时清理，C1 会迁移到 owner 状态机”，避免后续重构被误解为重复工作。

验收：启用/禁用双编译通过；禁用态 ATT 表不含任何 HOGP 属性；退出期间无超时回调访问旧 handle。

### Phase C3：模块边界和 API 收口

目标：解决 C-05、固定键位耦合，并给 Keymap/宏模块提供稳定出口。

实施项：

1. 从 `rdx_ble_server.h` 移除 `rdx_hogp_keyboard.h` 传递包含。
2. 每个调用方显式包含自己使用的头文件。
3. 删除无调用者的 `hogp_mode_*`、`hogp_key_*` 兼容 wrapper。
4. `rdx_app.c` 只向模式控制器或 Key Action Adapter 转发按键事件。
5. HOGP 删除 `KEY_IO_NUM*`、`KEY_ACTION_*` 和固定 5 键知识。
6. 引入完整 8 字节 Keyboard Report API，并保持线上 payload 不变。

验收：在 HOGP 模块内搜索不到物理键和产品动作常量；模拟 Ctrl+C 可通过统一 Report API 表达；现有 A-D 行为不回归。

### Phase C4：协议状态与 Profile 数据收口

目标：解决 C-06、C-07、C-08，同时保持 Profile v1 外部布局。

实施项：

1. 发送成功时同步当前 Input Report，release 后归零；ATT read 返回真实当前值。
2. encryption change 对当前连接采用完整赋值，禁用或失败时清零 encrypted。
3. 实现 Protocol Mode 的合法 0/1 写入；非法长度和值返回明确 ATT 错误，优先使用 `ATT_ERROR_INVALID_HANDLE_VALUE`，同时保持旧值不变。实施前需确认 JL BTstack 实际支持的 error code，必要时替换为可用值。
4. 处理 HID Control Point suspend/exit suspend；合法 0/1 写入更新 suspend 状态，非法长度和值返回明确 ATT 错误，并在 suspend 时停止业务 Report。
5. `rdx_hogp_fill_adv_data()` 对每个字段执行容量检查，禁止无符号下溢和越界。
6. handle、value_handle、UUID 和 Report Reference 从同一 Profile 定义生成或展开；契约测试校验预处理后的最终 ATT 字节。
7. 当前位于 Device Information Service 下的 Output Report 只记录为 Profile v1 兼容债务，本阶段不移动 handle。

验收：handle、Report Map、通知 payload 与 Phase 5 快照一致；Protocol Mode、suspend、加密降级和 Input Report read 行为可从日志验证。

### Phase C5：产品身份与默认模式对齐

目标：从测试入口切换为 VibeCoding Keyboard 产品主路径。本阶段默认上电 HOGP 的方向已确认，但应在 C1-C4 稳定后作为独立产品身份迁移提交，不阻塞架构修复先行合入。

实施项：

1. T2620 默认进入 HOGP 广播，名称使用 `VibeCoding Keyboard`。
2. 配置模式只通过产品定义的组合键或未来 RDX App/Mode API 进入；临时 NUM0 测试策略删除。
3. 配置模式名称冻结为 `VibeCoding Config`。
4. HFP 不由 HOGP 启停；产品模式协调层根据最终策略决定 Config 模式是否暂停 HFP。
5. 明确 LED/UI 状态事件，但本阶段只提供模式通知，不实现完整交互。

验收：上电可直接被 PC 发现为 `VibeCoding Keyboard`；进入配置模式后 PC HOGP 已断开；退出配置模式后恢复 HOGP 广播并自动回连；Windows 删除旧 `VibeKeyboard` 配对后重新配对成功，旧配对未清理时的异常行为已记录为迁移风险。

### Phase C6：验证、文档与交接

1. 更新 `docs/HOGP模块化重构方案.md` 为已实施状态和实际架构。
2. 更新 `AGENTS.md`、`CLAUDE.md` 中已过时的 HOGP 文件职责和测试入口。
3. 扩展统一 host runner，不创建新的独立人工入口。
4. 输出 GATT 快照、模式切换日志、Windows 配对/回连记录和关闭态构建结果。
5. 冻结供 RDX App 按键设置、Key Action、HFP 和 PC Agent 使用的 API/事件契约。

## 8. 测试方案

### 8.1 主机侧自动检查

现有统一入口保持不变：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\host\run_host_tests.ps1
```

测试入口分工固定如下：

- `test_hogp_profile_contract.ps1` 继续守护 Profile v1：13 个 HID handle、70 字节 Report Map、8 字节 Input Report payload、HID Service 属性顺序和字节级值。
- 新增静态检查优先扩展现有 HOGP 契约测试；只有覆盖非 HOGP 契约的大类时才新增脚本。
- `run_host_tests.ps1` 是唯一人工入口，VS Code task 和命令行都调用它。

新增检查建议：

- HOGP 禁用态预处理后的 ATT 表不含 `0x1812`、`0x2A4D`、`0x2908` HOGP 条目；
- Server 释放 handle 前调用 HOGP deinit；
- HOGP 公共头无传递包含和配置 include 顺序依赖；
- 旧 wrapper、物理键常量和 `hogp_mode_get()` 路由全部消失；
- 模式状态字段和 owner 路由点存在，禁止使用 requested mode 判断断连 owner。
- HOGP owner 不能进入 RDX App 配置写处理，RDX App owner 不能启用 HID CCC 或发送 Input Report。

### 8.2 固件构建

```text
[ ] TCFG_RDX_HOGP_ENABLE=1 全量编译
[ ] TCFG_RDX_HOGP_ENABLE=0 全量编译
[ ] 两种配置均执行 git diff --check
[ ] 检查生成二进制不进入源码提交
```

### 8.3 硬件回归

```text
[ ] 上电默认 HOGP 广播
[ ] Windows 首次配对、加密、CCC 订阅
[ ] A-D 基线输入与 release 正常
[ ] Ctrl+C 等组合键 Report 正常
[ ] PC 休眠/唤醒后自动回连
[ ] HOGP 已连接时切换 RDX App 配置模式，确认先断连后换广播
[ ] RDX App 已连接时切回 HOGP，确认原连接完整清理
[ ] HOGP 连接写 RDX App 配置属性被拒绝，RDX App 连接写 HID CCC 被拒绝
[ ] 模式往返压力 100 次
[ ] 切换中关机/复位，无旧 timer 和旧 handle 回调
[ ] 加密失败或关闭后禁止发送 Report
[ ] Host suspend/exit suspend 行为正常
[ ] HFP 音频活动时 HOGP 按键无明显丢失或超时（如失败，单独立项跟踪任务优先级/抢占问题，不在 Phase 6 硬解）
```

## 9. 提交拆分建议

```text
fix(hogp): close BLE mode transition and connection ownership
fix(hogp): close lifecycle and disabled-profile gaps
refactor(hogp): narrow module boundaries and report API
fix(hogp): synchronize protocol state and profile definitions
feat(hogp): align default mode and product identity
test(hogp): extend phase 6 regression coverage
docs(hogp): record phase 6 architecture and downstream contracts
```

每个提交只包含对应源码、测试或文档。`SDK/cpu/br28/tools/`、`output/` 和其他编译生成二进制不得纳入提交。

## 10. 完成定义

只有同时满足以下条件，HOGP Phase 6 才可标记为完成：

- 模式请求、广播身份和连接归属彼此独立，异步断连不会错分事件；
- HOGP 关闭态不暴露任何 HID 属性，不保留运行时资源；
- HOGP 不依赖物理键、宏、Layer、配置存储或 HFP 业务；
- 上层可以通过完整 Keyboard Report API 实现单键和组合键；
- Profile v1 外部契约测试、启用/禁用构建和硬件模式切换全部通过；
- 默认工作模式、广播名称和配置模式符合 VibeCoding Keyboard 产品文档；
- RDX App 按键设置、Key Action Executor、HFP 和 PC Agent 的开发不需要访问 HOGP 私有状态；
- Profile v2 的范围、配对迁移和 Windows GATT cache 风险已明确隔离。

完成后，下一阶段可并行开展 RDX App 按键设置、Keymap/宏/Layer、Flash 双 Slot、HFP 共存和 PC Agent，不再修改 HOGP Profile v1 的核心契约。
