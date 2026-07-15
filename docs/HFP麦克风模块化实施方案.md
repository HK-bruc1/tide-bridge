# HFP 经典蓝牙麦克风模块化实施方案

> 适用项目：VibeCoding Keyboard / T2620 / JL AC701N（BR28）
>
> 文档定位：复用 JL SDK 既有 Classic/HFP 能力，补齐 RDX 产品开关、状态观测、HOGP 共存和 KEY5 长按语音触发
>
> 实施原则：SDK 能力只复用、不复制；新增模块单一职责；每阶段可验收、可回归

## 1. 最终结论

第一版产品采用以下链路：

```text
同一台 T2620 设备
├─ BLE HOGP Keyboard：键盘输入与 KEY5 语音触发
├─ Classic HFP HF：麦克风上行，由 PC Audio Gateway 打开 SCO/eSCO
└─ RDX BLE Config：与 HOGP 复用同一 BLE Server，模式互斥
```

工程不需要重新实现 HFP 麦克风协议栈，也不需要新建一套 Classic 配对、回连、bond 或 SCO 状态机。JL SDK 已经提供：

- Classic Controller 和 Profile 生命周期；
- 配对、开机回连、超距回连、Inquiry/Page Scan；
- Link Key/bond 数据；
- HFP SDP、RFCOMM/AT 和 SLC；
- SCO/eSCO、mSBC/CVSD；
- `esco_player`、`esco_recoder`、AEC/CVP 和通话音频切换。

本项目真正需要新增的只有：

1. 让 T2620/RDX 产品不再在 BT 初始化后关闭 Classic Stack。
2. 将 Classic 开关与 SD/DAC 板级电源动作解耦。
3. 提供只读、可复用的 HFP 麦克风状态监视和诊断接口。
4. 冻结 T2620 Profile、名称和 HOGP/HFP 共存配置。
5. 复用现有 `LONG/HOLD/UP` 按键事件实现 KEY5 长按 PTT。
6. 由 PC Agent 根据 HID F24 down/up 控制录制、STT 和文本注入。

## 2. 代码事实与复用审计

### 2.1 已有能力及唯一所有者

| 能力 | 现有所有者 | 本项目策略 |
|---|---|---|
| Classic 初始化与应用生命周期 | `earphone.c` | 直接复用，不在 RDX 重建 |
| 开机扫描、配对、回连、连接超时 | `dual_conn.c`、`bt_event_func.c` | 直接复用，不新建配对状态机 |
| Link Key/bond | BT Stack/VM | 直接复用，不建第二份数据库 |
| Inquiry/Page Scan 联动 | `dual_conn.c::write_scan_conn_enable()` | 复用现有入口，不直接操作 LMP |
| HFP SDP 注册 | `bt_profile_config.c` | 由 `TCFG_BT_SUPPORT_HFP` 控制 |
| HFP SLC | JL BT Stack、`phone_call.c` 事件 | 只观察，不接管 |
| SCO/eSCO 生命周期 | `phone_call.c` | 唯一音频 owner |
| 麦克风编码和上行 | `esco_recoder` | 直接复用 |
| 下行播放 | `esco_player` | 直接复用，产品层决定是否静音 |
| mSBC/CVSD、AEC/CVP | JL 音频模块 | 直接复用 |
| BLE HOGP transport | `rdx_hogp_keyboard.c` | 保持唯一 HID transport owner |
| HOGP key action/report 转换 | `rdx_hogp_key_action.c` | 扩展 press/release，不重复构造 Report |
| 物理按键识别 | `key_driver.c`、key adapter | 复用 `LONG/HOLD/UP` |

虽然 `TCFG_BT_DUAL_CONN_ENABLE=0`，非 TWS 产品仍使用 `dual_conn.c` 管理单设备的扫描、记录设备回连和连接超时。“dual_conn”是 SDK 文件名，不表示本产品必须开启一拖二。

### 2.2 当前配置

| 配置 | 当前值 | 第一版目标 |
|---|---:|---:|
| `TCFG_BT_SUPPORT_HFP` | 1 | 1，overlay 冻结 |
| `TCFG_BT_MSBC_EN` | 1 | 1，保留 CVSD fallback |
| `TCFG_USER_BLE_ENABLE` | 1 | 1 |
| `TCFG_RDX_HOGP_ENABLE` | 1 | 1 |
| `TCFG_BLE_HIGH_PRIORITY_ENABLE` | 0 | 0/1 A/B 实测后冻结 |
| `TCFG_BT_SUPPORT_HID` | 1 | 0，禁止 Classic HID 重复枚举 |
| `TCFG_BT_SUPPORT_A2DP` | 1 | 基线阶段保持 1 |
| `TCFG_BT_SUPPORT_AVCTP` | 1 | 基线阶段保持 1 |
| `TCFG_USER_TWS_ENABLE` | 0 | 0 |
| `TCFG_LE_AUDIO_APP_CONFIG` | 0 | 0 |
| `RDX_CLASSIC_BT_PAGE_SCAN_ENABLE` | 0 | 删除，由 HFP 产品开关替代 |

### 2.3 当前阻断点

JL SDK 在 `BT_STATUS_INIT_OK` 后已经初始化 BLE、加载回连记录，并在非 TWS 路径打开 Classic 扫描。随后 RDX handler 又在约 1 秒后调用 `rdx_app_bt_shutdown()`：

```text
dual_conn_close()
USER_CTRL_POWER_OFF
sd_set_power(0)
dac_power_off()
```

因此当前问题不是“缺少 Classic 配对和回连实现”，而是 RDX 产品主动关闭了 SDK 已经启动的 Classic 能力，并把它与板级省电动作绑在了一起。

### 2.4 当前按键事实

IO key 默认参数为：

```text
scan_time = 10 ms
long_time = 75
hold_time = 90
```

对应行为：

- 按住约 750 ms：产生一次 `KEY_ACTION_LONG`；
- 继续按住：周期产生 `KEY_ACTION_HOLD`，adapter 还可能转换为 1/3/5 秒事件；
- 达到 long 后松开：产生一次 `KEY_ACTION_UP`；
- 未达到 long 松开：产生 `KEY_ACTION_CLICK`，不会产生 `UP`。

这套语义正好适用于“长按开始、松开结束”：短按没有启动 PTT，因此也不需要 release。无需新增物理 down/up observer。

## 3. 目标与非目标

### 3.1 目标

1. 最大化复用 JL SDK 的 Classic/HFP/音频/连接管理。
2. 新增模块只补状态观测、RDX 产品绑定和 KEY5 语音意图。
3. 支持同一台 PC 同时连接 BLE HOGP 与 Classic HFP。
4. HFP active 时保持 HOGP 输入，任一链路断开不主动关闭另一条链路。
5. KEY5 达到 long 阈值后发送 F24 down，松开时发送 release。
6. 所有产品覆盖位于 `t2620_project_config.h`，不修改工具生成配置。
7. 每个阶段有自动验收、设备验收、回归范围和退出条件。

### 3.2 非目标

- 不实现第二套 Classic 配对、扫描、回连或 bond 数据库。
- 不复制 `dual_conn.c`、`bt_event_func.c` 或 `phone_call.c`。
- 不在新模块中调用 `esco_player_open/close()` 或 `esco_recoder_open/close()`。
- 不直接调用 `lmp_hci_write_scan_enable()`。
- 不伪造 HFP SLC、SCO 或通话状态。
- 不把麦克风音频放到 HID、RDX GATT 或自定义 BLE characteristic。
- 不定义 Mic-only 私有 SDP。
- 不让 HFP 模块处理 KEY5；PTT 是独立产品模块。
- 不在第一版支持 PC HOGP、手机 RDX Config、Classic HFP 三方并发。
- 不在第一轮共存验证时同时精简 A2DP/AVCTP。

## 4. 推荐模块架构

### 4.1 架构图

```text
JL SDK（既有、唯一控制者）
├─ earphone.c / dual_conn.c / bt_event_func.c
│  └─ Classic 生命周期、扫描、配对、bond、回连
├─ BT Stack / bt_profile_config.c
│  └─ HFP SDP、ACL、SLC、SCO/eSCO
└─ phone_call.c / esco_player / esco_recoder
   └─ HFP 音频生命周期和麦克风数据

新增公共观测层
└─ bt_hfp_mic_status.c/.h
   └─ 从既有 BT_STATUS_* 派生只读快照和统计，不发送控制命令

RDX 产品层
├─ rdx_hfp_mic.c/.h
│  └─ 编译开关、事件转发、诊断、HOGP 共存信息
└─ rdx_voice_ptt.c/.h
   └─ KEY5 LONG/HOLD/UP -> HOGP key action press/release

既有 HOGP
├─ rdx_hogp_key_action.c/.h
│  └─ Action -> Keyboard Report、press/release 生命周期
└─ rdx_hogp_keyboard.c/.h
   └─ ATT notify transport
```

### 4.2 推荐文件

```text
SDK/apps/common/bt_common/
├─ bt_hfp_mic_status.h
└─ bt_hfp_mic_status.c

SDK/apps/common/third_party_profile/rdx_protocol/
├─ rdx_hfp_mic.h
├─ rdx_hfp_mic.c
├─ rdx_voice_ptt.h
└─ rdx_voice_ptt.c
```

不新增 `hfp_mic_core`、`hfp_mic_platform_ops` 或自建 pairing state machine。当前项目本身就是 JL SDK 固件，额外抽象一套平台控制接口只会形成双重所有权。

### 4.3 依赖规则

| 模块 | 允许依赖 | 禁止依赖 |
|---|---|---|
| `bt_hfp_mic_status` | JL 基础类型、BT event 常量 | RDX、HOGP、scan/reconnect、音频 open/close |
| `rdx_hfp_mic` | status public API、HOGP 只读状态 | LMP、dual_conn 内部状态、audio owner、KEY5 |
| `rdx_voice_ptt` | HOGP key action API、RDX 产品 key ID | HFP、Classic scan、SCO、音频模块 |
| `rdx_hogp_key_action` | HOGP report transport | HFP、物理 GPIO、PC Agent |

### 4.4 复用门禁

以下任一情况出现即视为架构回退：

- 新增第二套 pairing timeout、page list 或 link key 存储；
- 新模块直接控制 Inquiry/Page Scan；
- 新模块直接打开或关闭 SCO、player、recorder；
- fork 或复制 `phone_call.c`、`dual_conn.c` 的逻辑；
- HFP 状态变化切换 HOGP/Config mode；
- KEY5 代码进入 HFP 状态监视模块。

## 5. 只读 HFP 状态监视

### 5.1 定位

`bt_hfp_mic_status` 不是第二个状态机 owner。它只是把已经发生的 SDK 事件归一化为产品可查询的快照，用于日志、测试、LED/Agent 诊断和共存统计。

权威状态始终属于 JL BT Stack 和 `phone_call.c`。监视器不得根据快照发控制命令。

### 5.2 快照

```c
typedef enum {
    BT_HFP_MIC_LINK_IDLE = 0,
    BT_HFP_MIC_LINK_ACL,
    BT_HFP_MIC_LINK_SLC,
    BT_HFP_MIC_LINK_AUDIO,
} bt_hfp_mic_link_state_t;

typedef struct {
    bt_hfp_mic_link_state_t state;
    u8 acl_connected;
    u8 slc_connected;
    u8 audio_active;
    u8 addr[6];
    u16 codec;
    s32 last_reason;
} bt_hfp_mic_snapshot_t;
```

`state` 是事实位的派生展示值：

```text
audio_active  -> AUDIO
slc_connected -> SLC
acl_connected -> ACL
otherwise     -> IDLE
```

监视器不定义 `PAIRING`、`RECOVERING`、`ERROR` 等控制态，避免与 SDK 连接管理重叠。异常通过统计和日志表达。

### 5.3 事件映射

至少观察：

| SDK 事件 | 快照变化 |
|---|---|
| `BT_STATUS_FIRST/SECOND_CONNECTED` | ACL on，保存地址 |
| `BT_STATUS_FIRST/SECOND_DISCONNECT` | ACL/SLC/audio 全部清理 |
| `BT_STATUS_HFP_SERVICE_LEVEL_CONNECTION_OK` | ACL/SLC on，幂等 |
| `BT_STATUS_CONN_HFP_CH` | ACL/SLC on，幂等 |
| `BT_STATUS_DISCON_HFP_CH` | SLC/audio off |
| `BT_STATUS_SCO_STATUS_CHANGE` open | ACL/SLC/audio on，记录 codec |
| `BT_STATUS_SCO_STATUS_CHANGE` close | audio off |
| `BT_STATUS_SCO_DISCON` | audio off，幂等 |

要求：

- 不保存 `struct bt_event *`，只同步复制必要字段；
- 重复事件幂等；
- 首次事实位 `0 -> 1` 才增加对应 connect/open 计数；相同事件重复到达只增加 `duplicate_event_count`，不能重复增加业务计数；
- 上层事实可以补齐必需的下层事实：SLC 意味着 ACL 已建立，audio 意味着 ACL 与 SLC 均已建立；
- 未知事件计数后忽略；
- 断开事件将下层事实一并清理；
- 不因事件错序发送任何恢复命令；
- dump 同时输出 HOGP connected/ready，但 status 模块本身不 include HOGP。

### 5.4 API

```c
void bt_hfp_mic_status_init(void);
void bt_hfp_mic_status_reset(void);
void bt_hfp_mic_status_handle_event(const struct bt_event *event);
void bt_hfp_mic_status_get(bt_hfp_mic_snapshot_t *snapshot);
void bt_hfp_mic_status_stats_get(bt_hfp_mic_stats_t *stats);
void bt_hfp_mic_status_dump(void);
```

RDX binding 只负责从现有 `MSG_FROM_BT_STACK` handler 转发事件，并在需要时把 HOGP 状态追加到产品日志。

## 6. Classic 生命周期与产品开关

### 6.1 产品开关

```c
#ifndef TCFG_RDX_HFP_MIC_ENABLE
#define TCFG_RDX_HFP_MIC_ENABLE                  0
#endif

#ifndef TCFG_BT_HFP_MIC_STATUS_ENABLE
#define TCFG_BT_HFP_MIC_STATUS_ENABLE            TCFG_RDX_HFP_MIC_ENABLE
#endif
```

`TCFG_RDX_HFP_MIC_ENABLE` 控制 RDX 产品绑定和 Classic 保留策略；公共只读监视器默认跟随它。非 RDX 产品需要复用监视器时可以单独覆盖 `TCFG_BT_HFP_MIC_STATUS_ENABLE=1`。T2620 只需开启 RDX 产品开关，其他产品默认不增加代码尺寸。

### 6.2 启动行为

当 `TCFG_RDX_HFP_MIC_ENABLE=1`：

- 不调度 `rdx_app_bt_shutdown()`；
- 保留 `earphone.c` 和 `dual_conn.c` 的原生初始化、回连和扫描；
- 初始化只读 status/binding；
- 板级 SD/DAC 省电仍按独立 board power policy 执行。

当开关为 `0`：

- 保留非 HFP RDX 产品原有 Classic 关闭行为；
- 不编译或不调用新增 status/binding；
- 不改变现有 BLE/RDX 行为。

### 6.3 拆分旧 shutdown

旧函数必须拆成两个明确动作：

```text
rdx_classic_disable_for_product()
    -> dual_conn_close()
    -> USER_CTRL_POWER_OFF

rdx_board_idle_power_apply()
    -> SD/DAC 和其他板级省电
```

HFP 产品只跳过第一个动作，不能连带跳过板级省电。DUT 进入/退出路径也必须按产品开关恢复：HFP 产品退出 DUT 后回到 SDK 正常连接管理，而不是永久关闭 Classic。

### 6.4 配对和回连

第一版直接采用 SDK 现有参数：

```text
TCFG_BT_PAGE_TIMEOUT              = 8 s
TCFG_BT_POWERON_PAGE_TIME         = 60 s
TCFG_BT_TIMEOUT_PAGE_TIME         = 120 s
TCFG_DUAL_CONN_INQUIRY_SCAN_TIME  = 120 s
TCFG_DUAL_CONN_PAGE_SCAN_TIME     = 0
```

SDK 已负责：

- 从已记录设备列表发起开机回连；
- 回连超时后恢复扫描；
- 120 秒 Inquiry Scan；
- 连接后关闭 Inquiry、保留需要的 Page Scan；
- 连接超时和 Link Key missing 处理。

若实机行为不符合产品需求，优先调整上述 SDK 配置或调用 `dual_conn.h` 的公开入口。禁止在 HFP 模块内增加第二个 timer/page list/bond 状态机。

Phase 0 必须在 `TCFG_BT_DUAL_CONN_ENABLE=0` 的实际产品配置下验证 `write_scan_conn_enable()`、`dual_conn_state_handler()` 和 `dual_conn_close()`，不能仅凭文件名或编译通过推断单连接行为。

如果后续需要“用户主动重新进入 120 秒配对模式”，应在现有 `dual_conn` 所有权内增加或完善公开 pairing API，由它复用现有 scan flags 和 timer；不得把配对 timer 放进 `rdx_hfp_mic`。

## 7. KEY5 长按 Push-to-Talk

### 7.1 产品语义

```text
HOGP ready + KEY5 KEY_ACTION_LONG（约 750 ms）
    -> 发送 F24 Key Down
    -> ptt_active = 1（仅发送成功后）

KEY5 KEY_ACTION_HOLD
    -> 消费，不重复发送 F24

KEY5 KEY_ACTION_HOLD_1SEC/3SEC/5SEC 等
    -> 同样消费，不改变 PTT 状态

KEY5 KEY_ACTION_UP + ptt_active
    -> 从 held report 释放 F24，发送剩余组合状态（通常为全零）
    -> ptt_active = 0

KEY5 KEY_ACTION_CLICK
    -> 未启动 PTT，执行正常短按 keymap 或产品冻结的短按行为
```

这是长按触发，不是按下即录音。用户必须接受约 750 ms 的启动阈值；PC Agent 或 UI 应在真正开始采集后给出反馈。

### 7.2 复用 HOGP key action

扩展现有 executor：

```c
int rdx_hogp_key_action_press(
    const rdx_hogp_key_action_keyboard_t *action);
int rdx_hogp_key_action_release(void);
```

`rdx_voice_ptt` 使用一个标准 F24 action 调用上述 API，不直接构造 ATT payload，不调用 `app_ble_att_send_data()`。

要求：

- press 复用现有 action-to-report 转换和 HOGP ready 检查；
- press 成功后才设置 `ptt_active`；
- `KEY_ACTION_HOLD` 及 adapter 转换出的全部 HOLD 秒数事件都消费，禁止重复 down；
- UP 只在 active 时 release；
- disconnect、HOGP->Config、deinit、关机清理 active 并 best-effort release；
- release 失败记录错误并走 HOGP 既有 reset；
- KEY5 三击模式切换迁到 KEY1，与 HOGP 模块方案一致；
- 不新增底层物理 down/up observer。

### 7.3 与其他键并发

KEY5 持续按住时 F24 是持久按下状态。此时 KEY1～KEY4 仍应能够发送普通 click，不能被 click 的 20 ms release timer 顺带释放 F24。

最佳实践是在现有 `rdx_hogp_key_action` 内扩展状态合成：

```text
held_report       = 持久按下集合，例如 F24
transient_report  = 普通 click 的临时按键

发送 click：held + transient
click timer：恢复 held，而不是固定发送全零
KEY5 UP：从 held 删除 F24，再发送剩余状态
reset/deinit：清空 held + transient，发送全零
```

要求：

- 继续复用现有 8-byte Keyboard Report 和最多 6 个 usage；
- 合并 modifier 和 usage 时去重并检查容量；
- 持久状态、click timer 和 current report 只由 key action executor 管理；
- `rdx_voice_ptt` 只声明 F24 按下/释放意图，不维护 HID report 集合；
- 不新建第二个 HID sender 或 PTT 专用 ATT 通道。

若第一版产品明确禁止 PTT 期间使用其他四键，也必须写成冻结的产品限制并在路由层消费；不能让旧 timer 偶然释放 F24。当前推荐保留其他四键可用。

### 7.4 F24 边界

F24 是标准 Keyboard usage，不会天然被 Windows 屏蔽：

- Agent 在线且 hook 成功：消费 F24，控制录制；
- Agent 离线或 hook 失败：F24 可能进入当前应用，但不得引发设备端卡键；
- 设备端不能从 SCO active 推断 Agent 一定在保存音频；
- Agent 不在线不影响 KEY1～KEY4 和普通 HOGP 功能。

## 8. 名称与 Windows 枚举

### 8.1 名称策略

使用统一品牌前缀和端点后缀，不使用完全相同的名称：

```text
BLE HOGP：VibeCoding Keyboard
Classic HFP：VibeCoding Mic
BLE Config：VibeCoding Config
```

如果最终品牌为 Beanstalk，则三者统一替换前缀。禁止代码和文档混用 `Beanstalk`、`VibeKeyboard`、`VibeCoding`。

### 8.2 当前事实

- Classic 默认是 `Beanstalk RKB MIC`；
- HOGP `RDX_HOGP_NAME_SOURCE=0`，广播名来自 RDX Server local name；
- `RDX_HOGP_CUSTOM_NAME="VibeKeyboard"` 当前不生效；
- GAP Device Name `0x2A00` 当前也读取 RDX Server local name；
- BLE local name 可能已经保存在 VM；
- `bt_get_local_name()` 当前返回编译期 `BT_NAME`，Classic 动态改名必须实测 EIR/Windows 结果。

HOGP 广播名与 HOGP 模式下的 GAP `0x2A00` 必须一致。PC Agent 不得仅通过名称关联 Keyboard 和 Mic，名称只用于用户识别。

Classic 名称验收必须覆盖：修改 `BT_NAME` 后 EIR 和 Windows 显示是否更新、旧 bond 是否继续显示缓存名、是否必须删除设备并重新配对，以及 RDX 动态改名路径能否真正调用本地名/EIR 更新。若动态改名不可靠，第一版冻结为编译期产品名，不把 App 改名作为量产能力。

### 8.3 Profile 枚举

Phase 0 记录当前 Windows 枚举。T2620 正式配置必须关闭 Classic HID，避免第二个 Classic Keyboard。A2DP/AVCTP 在共存基线阶段保持开启，量产前再作为独立变更评审是否精简。

## 9. HOGP 与 HFP 共存

### 9.1 固定原则

- HFP active 不暂停 HOGP Input Report；
- HFP 状态不切换 HOGP/Config mode；
- HOGP 状态不控制 SCO/eSCO；
- 任一链路断开不主动关闭另一条链路；
- RDX Config 与 HOGP 继续保持单 BLE owner 和模式互斥；
- Classic HID 关闭；
- `phone_call.c` 继续唯一拥有 SCO 音频。

### 9.2 BLE high priority

`TCFG_BLE_HIGH_PRIORITY_ENABLE=1` 只是 A/B 测试候选，不是默认正确答案。必须对比 `0/1`：

- HOGP down/up notify 返回码、P50/P95/P99 时延；
- eSCO 上下行丢包、PLC、爆音、断续；
- 高频按键时 SCO 是否异常断开；
- HFP active 时 HOGP 断开后的回连；
- 近距离、临界距离和 2.4 GHz 干扰环境。

以键盘和音频双向数据决定最终值，不能只验证 HOGP。

### 9.3 HFP 下行

HFP 是全双工 Headset/Hands-Free，不是 Mic-only。当前工程配置了 DAC 和 `esco_dac_ch` 下行节点，量产硬件是否安装换能器必须结合原理图/BOM 和实机确认。

若产品不需要下行播放，在既有音频策略层静音或安全丢弃；不得修改 SDP、fork `phone_call.c` 或把该逻辑放进 status/binding。

## 10. 可观测性

### 10.1 统计

```c
typedef struct {
    u32 acl_connect_count;
    u32 acl_disconnect_count;
    u32 slc_connect_count;
    u32 slc_disconnect_count;
    u32 sco_open_count;
    u32 sco_close_count;
    u32 duplicate_event_count;
    u32 unknown_event_count;
    s32 last_disconnect_reason;
} bt_hfp_mic_stats_t;
```

### 10.2 日志

```text
[HFP_MIC] ACL on addr=xx:xx:xx:xx:xx:xx
[HFP_MIC] SLC on
[HFP_MIC] AUDIO on codec=mSBC hogp_conn=1 hogp_ready=1
[HFP_MIC] AUDIO off reason=0
```

按键和音频高频路径不逐包打印。状态监视只运行在 `app_core` 的现有消息路径，不新增任务，不在 callback 中写 Flash 或阻塞等待。

## 11. 分阶段实施与门禁

Phase 0～6 是累积顺序。每阶段必须满足代码评审、host 测试、正式工具链构建、设备验收和上一阶段回归；任一失败不得进入下一阶段。

### Phase 0：冻结基线与确认 SDK 复用

**目标**

证明 SDK 已有能力和当前阻断点，不改变产品行为。

**实施内容**

1. 记录当前宏、名称、地址、SDP Profile 和 HOGP 契约。
2. 记录当前 Classic 被 RDX 延迟关闭的启动日志。
3. 在实验配置中只阻止 RDX shutdown，验证 SDK 原生配对、回连、SLC 和 SCO。
4. 记录 Windows 的 HFP、Classic HID、A2DP 和 BLE HOGP 枚举。
5. 记录 SCO active 时 DAC、下行波形、底噪和功耗。
6. 记录 T2620 实际出现的 SLC 事件类型和顺序，确认 `BT_STATUS_HFP_SERVICE_LEVEL_CONNECTION_OK`、`BT_STATUS_CONN_HFP_CH` 是单独出现还是重复出现。
7. 在 `TCFG_BT_DUAL_CONN_ENABLE=0` 下实测 `write_scan_conn_enable()`、`dual_conn_state_handler()`、`dual_conn_close()` 的扫描和关闭行为。
8. HFP audio active 时连续发送 HOGP report，验证基础并发假设。
9. 使用现有 LONG 事件或等效交互样机进行约 750 ms 长按体验预演，由产品负责人冻结“长按触发”是否可接受。

**自动验收**

- 现有统一 host 测试全部通过；
- 新增 `test_hfp_mic_foundation.ps1`，冻结现有 HFP/mSBC/HOGP 和 SDK owner；
- 新增 `test_hfp_mic_reuse_contract.ps1`，禁止新增 pairing/bond/SCO owner。

**设备验收**

- 日志可观察 ACL -> SLC -> SCO open/close；
- 基线日志明确实际 SLC 事件来源；两个 SLC 事件都出现时只形成一次状态迁移和一次业务计数；
- SDK 原生 60 秒开机回连、120 秒超距回连和 120 秒 Inquiry 行为得到实测；
- `TCFG_BT_DUAL_CONN_ENABLE=0` 时现有 scan/state/close 入口行为符合单连接产品预期；
- HFP audio active 时 HOGP report 可持续发送，无立即掉线或明显阻塞；
- 约 750 ms 长按阈值取得产品确认；若不接受，停止后续 PTT 实施并单独评审即时 down/up 方案；
- 能明确区分“Profile 已编译”和“Classic 被 RDX 主动关闭”。

**回归范围**

HOGP、RDX Config、DUT、OTA、离线录音、本地播放、SD/DAC 电源。

**退出条件**

基线报告证明无需新建 Classic/HFP 控制状态机，所有未知事件有原始日志；长按触发语义、下行音频处理需求和 SDK 单连接行为已经冻结。

### Phase 1：保留 SDK Classic 生命周期并拆分 RDX 电源动作

**目标**

让 T2620 使用 SDK 已有 Classic/HFP 能力，不引入新的连接管理。

**实施内容**

1. 增加 `TCFG_RDX_HFP_MIC_ENABLE`，T2620 开启，其他产品默认关闭。
2. 拆分 Classic disable 与 board idle power。
3. HFP 产品启动时不再调度 RDX Classic shutdown。
4. DUT 退出按产品开关恢复 SDK 正常连接管理。
5. T2620 overlay 冻结 HFP、mSBC，并关闭 Classic HID。
6. 暂时保持 A2DP/AVCTP 与 BLE priority 当前值。

**自动验收**

- HFP 产品不调用延迟 Classic shutdown；
- 非 HFP 产品保留原行为；
- board power action 不受 HFP 开关跳过；
- 新代码不出现第二套 timer/page list/link key；
- 全量 host 测试和固件构建通过。

**设备验收**

- Windows 只枚举 BLE Keyboard，不枚举 Classic Keyboard；
- HFP 首次配对、开机回连、超距回连由 SDK 正常完成；
- DUT 进入/退出后 HFP 能恢复；
- SD/DAC 功耗行为与基线一致。

**回归范围**

Phase 0 全部项目，重点覆盖 DUT、软关机、OTA 和非 T2620 RDX 产品。

**退出条件**

Classic 生命周期只有 SDK owner，RDX 不再用一个 shutdown 同时控制协议栈和板级电源。

### Phase 2：只读状态监视与 RDX binding

**目标**

补齐模块化状态、日志和诊断，不改变连接或音频行为。

**实施内容**

1. 新增 `bt_hfp_mic_status.*`。
2. 新增薄 `rdx_hfp_mic.*`，从现有 RDX BT handler 转发事件。
3. 实现快照、统计、幂等和 dump。
4. HOGP 状态只在 binding 日志中组合。

**自动验收**

- `test_hfp_mic_module_contract.ps1` 检查依赖和禁止控制符号；
- `test_hfp_mic_status_contract.ps1` 检查 ACL/SLC/audio 派生规则；
- status contract 检查 SLC 补齐 ACL、audio 补齐 ACL/SLC，以及下层断开清理上层事实；
- status/binding 不出现 scan、bond、reconnect、`esco_*`、`bt_cmd_prepare`；
- 全量 host 测试和固件构建通过。

**设备验收**

- 快照与 SDK 日志一致；
- 重复 SLC/SCO close 幂等；
- 两个 SLC ready 事件连续到达时，`slc_connect_count` 只增加一次，`duplicate_event_count` 正确增加；
- 未知事件只计数，不改变事实位；
- 观察模块开关不影响连接、音频、功耗。

**回归范围**

HFP 单链路、HOGP 单链路、HFP+HOGP、睡眠唤醒、异常断电。

**退出条件**

状态监视连续 60 分钟无漂移，关闭模块时行为与 Phase 1 一致。

### Phase 3：名称与 HOGP/HFP 共存冻结

**目标**

形成可量产的双端点身份和射频配置。

**实施内容**

1. 落实 Keyboard/Mic/Config 名称与 VM 迁移策略。
2. 保证 HOGP 广播名与 GAP `0x2A00` 一致。
3. 验证 Classic 编译期名称、EIR、Windows 缓存和重新配对行为；动态改名不可靠时明确关闭该产品能力。
4. 对 BLE high priority 做 `0/1` A/B 测试并冻结结果。
5. 保持 HOGP/Config 单 BLE owner，不增加 BLE 连接数。

**自动验收**

- `test_hfp_hogp_coexistence_contract.ps1` 禁止双向控制；
- `test_hfp_mic_config.ps1` 冻结 overlay/Profile 配置；
- 名称测试冻结模式相关广播名和 GAP 名；
- 全量 host 测试和固件构建通过。

**设备验收**

- HOGP first、HFP first 两种配对顺序均通过；
- HFP active 时普通 HOGP 按键正常；
- 任一链路断开、回连不关闭另一条链路；
- Windows 显示 Keyboard 和 Mic 两个清晰端点。
- 记录 A2DP/AVCTP 导致的额外 Windows 音频端点，并标记为 Phase 6 精简前的预期行为而不是缺陷。

**回归范围**

HOGP Report Map/handle/payload、BLE Config、RDX App、HFP 音频、A2DP 基线。

**退出条件**

BLE priority 有双向数据支撑，名称和 Windows 枚举结果入库。

### Phase 4：KEY5 长按 PTT 设备端

**目标**

复用现有 `LONG/HOLD/UP` 和 HOGP executor，实现可靠 F24 生命周期。

**实施内容**

1. 扩展 HOGP key action press/release API。
2. 新增 `rdx_voice_ptt.*`。
3. KEY5 LONG 启动、全部 HOLD 家族事件消费、UP 释放、CLICK 保持短按策略。
4. KEY5 三击模式切换迁到 KEY1。
5. executor 合成持久 held report 与普通 click transient report，click timer 恢复 held。
6. disconnect/mode switch/deinit 清理 active。

**自动验收**

- `test_rdx_voice_ptt_contract.ps1` 覆盖 LONG/HOLD/UP、发送失败和异常清理；
- 覆盖 `HOLD_1SEC/3SEC/5SEC` 等转换事件，保证不重复发送 F24；
- 覆盖 F24 held 期间 KEY1～KEY4 click，timer 后必须恢复 F24 而不是发送全零；
- 覆盖 F24 held + KEY1 click + KEY2 click 的 transient 重叠、usage 去重、modifier 合并和 6-slot 容量边界；
- 覆盖 F24 held 期间 HOGP disconnect、进入 Config、press 失败和重复 LONG/HOLD；
- 迁移 HOGP 旧的 KEY5 三击和 no-press/release 契约，必须用新断言替换；
- Report Map、handle 和 8-byte payload 字节不变；
- PTT 模块不依赖 HFP/Classic/SCO；
- 全量 host 测试和固件构建通过。

**设备验收**

- 小于 long 阈值的短按不启动 PTT；
- 达到 long 后只产生一次 F24 down；
- HOLD 10 秒不重复 down；
- UP 立即 release；
- 按住 KEY5 时 KEY1～KEY4 仍能输入，且任一 click release 不会提前释放 F24；
- KEY1/KEY2 快速连续或重叠 click 后仍保持 F24，最终 KEY5 UP 后 report 收敛为全零；
- HFP idle/active 各完成 10,000 次长按周期，无卡键、重复键或丢 release；
- 按住时断开 HOGP，重连后无残留 F24。

**回归范围**

KEY1～KEY4、KEY5 短按、自定义 keymap、模式切换、离线按键、HFP 音频。

**退出条件**

设备端 PTT 不控制 HFP，只产生可靠 HID 意图；Agent 离线时无不可恢复副作用。

### Phase 5：PC Agent 与端到端语音

**目标**

完成长按、HFP 录音、STT 和文本注入闭环。

**实施内容**

1. Agent 绑定 Keyboard 与 Mic，不仅依赖名称。
2. 捕获并消费 F24 down/up。
3. 选择 HFP Mic，开始/停止录制。
4. 完成 STT 和当前焦点文本注入。
5. 决定音频端点预开或按 LONG 后打开，并记录时延/功耗。

**自动验收**

- Agent 测试覆盖重复 down、缺失 up、设备断开、进程退出；
- 固件与 Agent 的 F24 contract 一致；
- 固件全量 host 测试和构建继续通过。

**设备验收**

- LONG 后开始采集，UP 后停止并注入文本；
- 连续 100 次语音输入无残留录音或卡键；
- Agent 离线/hook 失败时 F24 可能进入应用，但普通键盘正常；
- HFP 失败不阻塞 KEY1～KEY4。

**回归范围**

Agent 未登录、默认音频设备变化、PC 睡眠唤醒、蓝牙重连、网络断开。

**退出条件**

启动时延、首字完整率和识别完成时延达到冻结的产品指标。

### Phase 6：长稳、精简与量产冻结

**目标**

证明复用边界稳定，并完成可选 Profile 精简。

**实施内容**

1. 非 T2620 配置验证 HFP 开关关闭时行为不变。
2. 独立评审 A2DP/AVCTP 是否关闭，不与其他变更混合。
3. 清理旧 Classic gate、散落 HFP 日志和失效函数。
4. 同步 HOGP 文档、技术栈、项目说明和测试名称。
5. 冻结 API、配置、Windows 矩阵和已知限制。

**自动验收**

- 全量 host 测试通过；
- T2620 与至少一个非 T2620 配置构建通过；
- 复用门禁确认没有第二套连接/音频 owner；
- 文档引用的文件、宏和测试全部存在。

**设备验收**

- HFP active 8 小时，HOGP 无非预期断开；
- 60 分钟高频按键无丢键、重复键、卡键；
- 睡眠唤醒、超距、PC 重启、异常断电均恢复；
- mSBC 与 CVSD fallback 覆盖目标 PC；
- Profile 精简前后 Windows 枚举和回连有对照数据。

**回归范围**

完整固件、HOGP、RDX Config、HFP、A2DP、音频、功耗、OTA、DUT、本地录音和播放。

**退出条件**

量产报告签署；无实验宏、重复 owner 或未归属策略。

## 12. Host 测试规划

统一入口：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\host\run_host_tests.ps1
```

按阶段新增：

```text
tests/host/test_hfp_mic_foundation.ps1
tests/host/test_hfp_mic_reuse_contract.ps1
tests/host/test_hfp_mic_module_contract.ps1
tests/host/test_hfp_mic_status_contract.ps1
tests/host/test_hfp_mic_config.ps1
tests/host/test_hfp_hogp_coexistence_contract.ps1
tests/host/test_rdx_voice_ptt_contract.ps1
```

必须冻结：

- `phone_call.c` 是唯一 SCO/eSCO 音频 owner；
- `dual_conn.c`/BT Stack 是唯一连接、扫描和 bond owner；
- status/binding 不含 scan、reconnect、bond、audio 控制；
- PTT 不含 HFP/Classic 依赖；
- KEY5 使用 LONG/HOLD/UP，不新增物理 down/up observer；
- key action executor 在 held + transient 并发时恢复 held，不由 click timer 固定 release all；
- HOGP Profile v1 字节级契约不变；
- 非 T2620 产品默认关闭新增能力。

PowerShell 只检查直接 include、直接函数调用、直接全局变量引用和配置契约；正式构建负责发现间接依赖、类型冲突和链接冲突。不要用复杂正则模拟 C 编译器，也不要因为某个禁止词出现在注释中就判定失败。

## 13. Code Review 门禁

### SDK 复用

- [ ] 未复制或 fork `dual_conn.c`、`bt_event_func.c`、`phone_call.c`。
- [ ] 未新增 pairing/reconnect/bond 状态机。
- [ ] 未新增 SCO/player/recorder owner。
- [ ] 新模块未直接操作 LMP 或 BT power。

### 模块边界

- [ ] status 只派生事实和统计。
- [ ] RDX binding 不处理 KEY5。
- [ ] PTT 不依赖 HFP。
- [ ] HOGP transport 不解释产品长按策略。
- [ ] Classic disable 与 board power 已解耦。

### KEY5

- [ ] LONG 成功后才置 active。
- [ ] HOLD 不重复发送。
- [ ] 全部 HOLD 秒数派生事件均不重复发送。
- [ ] UP 仅在 active 时 release。
- [ ] CLICK 不启动 PTT。
- [ ] KEY1～KEY4 click timer 不会释放 held F24。
- [ ] disconnect/mode/deinit 均清理 active。
- [ ] KEY5 不再承担模式切换三击。

### 回归

- [ ] 当前阶段测试已注册到统一 runner。
- [ ] 正式工具链构建通过。
- [ ] 设备验收有日志或报告。
- [ ] 上一阶段回归全部通过。
- [ ] 非目标产品行为已验证。

## 14. 最终量产形态

```text
SDK owner：
  earphone / dual_conn / bt_event_func = Classic 生命周期、配对、回连、bond
  phone_call / esco                  = HFP 音频

新增公共模块：
  bt_hfp_mic_status = 只读状态和统计

RDX 产品模块：
  rdx_hfp_mic       = 产品开关、事件转发、诊断
  rdx_voice_ptt      = KEY5 LONG/HOLD/UP 产品语义

BLE：
  rdx_hogp_key_action = press/release 和 Report 转换
  rdx_hogp_keyboard   = ATT notify transport

PC：
  Agent 监听 F24，使用 HFP Mic 录制并完成 STT/文本注入
```

该方案的模块化目标不是把 JL SDK 再包装一遍，而是明确唯一 owner，并在其上增加最小、可关闭、可诊断的产品能力。
