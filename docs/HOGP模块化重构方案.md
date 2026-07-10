# HOGP 模块化重构方案

> 目标：在不破坏当前已打通的 BLE HID 键盘行为前提下，将 HOGP 从 `rdx_ble_server.c` 中拆成可维护、可配置、可复用的模块。
>
> 第一阶段的核心原则是：**内部结构可以变化，PC 端看到的 GATT Profile、Report Map、广播身份和 Input Report 行为必须保持不变。**

> 产品边界更新：VibeCoding Keyboard 的 BLE App 部分全盘复用现有 RDX BLE App 底座，只在 RDX 业务命令层增加按键设置能力。HOGP 不承担 App 配置协议、鉴权、OTA、Flash 保存或 Keymap 解释，只提供稳定的 HID Profile 和 Keyboard Report 传输能力。

## 1. 背景与当前问题

当前 HOGP MVP 已经完成：

- Windows 能识别为 BLE HID 键盘；
- 能完成连接、配对/加密、HID 枚举；
- 按键能输出字母；
- HOGP 复用 RDX 的 `app_ble` handle 和同一份 GATT Server。

但当前实现仍然是 MVP 形态：

- HOGP 状态变量、HID attribute read/write、SM 事件、HCI 连接事件、广播切换、按键发送都集中在 `rdx_ble_server.c`；
- HID handle 布局、Report Map、HID Information、Report Reference 等硬编码在 RDX profile data 中；
- `rdx_app.c` 直接知道 HOGP 的模式和按键发送 API，产品按键语义与 HID 传输层耦合；
- 临时键位映射与 HOGP 传输配置混在一起，后续接入 RDX App 按键设置时容易误伤已稳定链路；
- HOGP 与 RDX BLE Server 的职责边界不清晰，后续排查问题时日志和状态不容易归因。

## 2. 总体目标

1. 保持当前已验证的 BLE HID 键盘功能稳定。
2. 将 HOGP 抽成 RDX BLE Server 下的子模块，而不是创建第二个 BLE Server。
3. 固化 HOGP 外部契约：handle、Report Map、Report payload、广播字段在第一阶段不变。
4. 将 HOGP 的协议状态、事件处理、HID read/write、Report 当前值和 Report 发送集中到独立文件。
5. 给后续产品化能力预留稳定边界：RDX App 负责按键设置，Key Action Executor 负责 Keymap/Macro/Layer，HOGP 只接收标准 Keyboard Report。

## 3. 不做的事

第一轮重构不做以下事情：

- 不改变 `config_le_gatt_server_num`；
- 不新增独立 `app_ble` handle；
- 不改变 RDX 原有服务 handle；
- 不改变 HOGP 当前 handle 布局；
- 不改变 Report Map 字节内容；
- 不改变当前能出字母的 Input Report payload 格式；
- 不改变 HOGP 与 RDX 广播互斥的模式切换语义；
- 不把 HOGP 逻辑迁移到 JL 官方独立 HOGP Server 框架。
- 不新建独立 BLE App 配置 GATT 服务；
- 不让 HOGP 解析 RDX App 配置帧、保存 Flash 或解释 Keymap/Macro/Layer。

这些变化可以放到后续 Profile v2 阶段单独评估。

## 4. 外部契约冻结

Phase 1 必须保持以下内容字节级或行为级不变。

### 4.1 GATT handle 布局

当前 HOGP 仍挂在 RDX profile data 后面：

```text
0x0001-0x0015: RDX / GAP / Battery / Notify 等既有服务
0x0016-0x0022: HID Service
0x0023-...   : 当前后续追加属性，第一阶段不主动重排
```

HID Service 的 MVP handle 不变：

| Handle | 内容 | 要求 |
|---|---|---|
| `0x0016` | HID Service `0x1812` | 不变 |
| `0x0018` | Protocol Mode `0x2A4E` | 不变 |
| `0x001a` | Input Report `0x2A4D` | 不变 |
| `0x001b` | Input Report CCCD `0x2902` | 不变 |
| `0x001c` | Report Reference `0x2908` | 不变 |
| `0x001e` | Report Map `0x2A4B` | 不变 |
| `0x0020` | HID Information `0x2A4A` | 不变 |
| `0x0022` | HID Control Point `0x2A4C` | 不变 |

### 4.2 Report Map 与 Report payload

第一阶段保持当前已验证的 Report Map。

Input Report 发送保持当前成功格式：

```text
8 bytes keyboard report:
[modifier, reserved, key1, key2, key3, key4, key5, key6]
```

示例：

```text
A down: 00 00 04 00 00 00 00 00
A up:   00 00 00 00 00 00 00 00
```

### 4.3 广播与名称

HOGP 模式广播保持：

- Flags: `0x06`
- 16-bit Service UUID: `0x1812`
- Appearance: `0x03C1`
- Local Name: 继续使用当前已验证的名称来源

RDX 模式和 HOGP 模式继续互斥。进入 HOGP 模式时停止 RDX 广播并设置 HID 广播；退出 HOGP 模式时恢复 RDX 广播。

## 5. 目标架构

HOGP 不拥有 BLE Server 生命周期，只作为 RDX BLE Server 的一个功能模块。

```text
rdx_ble_server.c
├─ app_ble handle 分配与注册
├─ RDX profile data 总表
├─ RDX 原有 read/write/event 处理
├─ RDX BLE App 连接、鉴权、收发、OTA 和业务命令
└─ HOGP 集成胶水
   ├─ 转发 HID handle read/write
   ├─ 转发 HCI/SM 事件
   ├─ 调用 HOGP 广播构造
   └─ 调用 HOGP Report 发送

RDX App / Key Action 层
├─ 复用 RDX BLE App 协议处理按键设置
├─ 管理 Keymap / Macro / Layer / 触发模式配置
├─ 将物理按键事件转换为 Keyboard Report
└─ 不直接操作 HID handle 或 ATT notify

rdx_hogp_keyboard.c
├─ HOGP 模式状态
├─ 连接/加密/CCC 状态
├─ HID attribute read/write
├─ SM Just Works 处理
├─ HOGP 广播 payload 构造
├─ Input Report 当前值
└─ Keyboard Report 发送

rdx_hogp_profile.h
├─ HID handle 宏
├─ HID Report Map
├─ HID Information
├─ Report Reference 常量
└─ handle range 判断

rdx_hogp_config.h
└─ HOGP 编译期开关、加密、广播名和传输策略默认值
```

## 6. 建议文件拆分

### 6.1 新增文件

```text
SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_config.h
SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_profile.h
SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_keyboard.h
SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_keyboard.c
```

### 6.2 保留文件职责

`rdx_ble_server.c` 保留：

- `app_ble_hdl_alloc()`；
- `app_ble_profile_set()`；
- RDX 原有 profile data 总数组；
- RDX 原有 ATT read/write；
- RDX 原有 HCI/L2CAP/SM 注册；
- RDX BLE App 原有连接、鉴权、收发、OTA 和业务命令入口；
- 调用 HOGP 模块的转发点。

`rdx_app.c` 保留：

- 生产按键事件入口；
- RDX App 按键设置命令的业务处理入口；
- 向 Key Action Executor 转发物理按键事件和配置变更；
- 不再直接构造 HID usage、HID report 或访问 HOGP 私有状态。

## 7. 模块 API 设计

建议 `rdx_hogp_keyboard.h` 暴露以下 API。

```c
void rdx_hogp_init(void *app_ble_hdl);
void rdx_hogp_deinit(void);

u8 rdx_hogp_mode_get(void);
void rdx_hogp_mode_set(u8 enable);

u8 rdx_hogp_is_handle(u16 att_handle);
u8 rdx_hogp_is_service_handle(u16 att_handle);

u16 rdx_hogp_att_read(hci_con_handle_t connection_handle,
                      u16 att_handle,
                      u16 offset,
                      u8 *buffer,
                      u16 buffer_size);

int rdx_hogp_att_write(hci_con_handle_t connection_handle,
                       u16 att_handle,
                       u16 transaction_mode,
                       u16 offset,
                       u8 *buffer,
                       u16 buffer_size);

void rdx_hogp_on_connected(u16 con_handle);
void rdx_hogp_on_disconnected(u16 con_handle);
void rdx_hogp_on_encryption_change(u16 con_handle, u8 enabled, u8 status);
void rdx_hogp_on_sm_event(u8 packet_type, u8 *packet, u16 size);

int rdx_hogp_fill_adv_data(u8 *adv_data, u8 max_len);
void rdx_hogp_adv_start(void);
void rdx_hogp_adv_stop(void);

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

迁移期可以保留以下兼容 wrapper，方便 Phase 1-3 降低改动面；最终应由 Key Action Executor 生成完整 `rdx_hogp_keyboard_report_t` 后调用 HOGP：

```c
int rdx_hogp_key_send_usage(u8 usage, u8 pressed);
int rdx_hogp_key_click_usage(u8 usage);
int rdx_hogp_key_click_index(u8 key_index);
int rdx_hogp_on_io_num_key(u8 num_idx, u8 action);
```

`rdx_ble_server.c` 不直接访问 HOGP 内部状态变量，只通过这些 API 交互。

稳定边界要求：

- RDX BLE App 解析配置帧并保存 Keymap；
- Key Action Executor 把物理按键、宏和 Layer 转换为 `rdx_hogp_keyboard_report_t`；
- HOGP 校验连接、加密、CCC、suspend 状态后发送 Report；
- HOGP 不知道“Key1/Key5/NUM1/A 键/宏/Layer”的产品语义。

## 8. 配置项设计

建议新增 `rdx_hogp_config.h`，但它只保存 HOGP 传输层配置，不保存产品按键映射。

```c
#ifndef RDX_HOGP_CONFIG_H
#define RDX_HOGP_CONFIG_H

#define TCFG_RDX_HOGP_ENABLE                  1
#define TCFG_RDX_HOGP_REQUIRE_ENCRYPTION      1
#define TCFG_RDX_HOGP_AUTO_REQUEST_PAIRING    1
#define TCFG_RDX_HOGP_KEY_UP_DELAY_MS         20
#define TCFG_RDX_HOGP_APPEARANCE              0x03C1
#define TCFG_RDX_HOGP_NAME_USE_RDX_LOCAL_NAME 1

#endif
```

按键映射不放入 HOGP。VibeCoding Keyboard 的 Keymap/Macro/Layer 由 RDX BLE App 按键设置命令写入配置存储，再由 Key Action Executor 解释：

```text
RDX BLE App command
    -> Config Storage / RAM candidate
    -> Key Action Executor
    -> rdx_hogp_keyboard_report_t
    -> HOGP notify
```

`TCFG_RDX_HOGP_KEY_UP_DELAY_MS` 属于迁移期兼容项。最终 release 节奏、宏步骤间隔和按键触发策略应下沉到 Key Action Executor。

## 9. 分阶段实施计划

## Phase 0：基线冻结

### 目标

在重构前固定当前可工作版本，记录 BLE 外部契约，建立对照日志。

### 工作内容

1. 保存当前能输出字母的串口日志。
2. 记录当前 GATT handle 表。
3. 记录当前 Report Map 字节长度和内容。
4. 记录当前广播包字段。
5. 记录按键输出路径：NUM1/2/3/4 到 A/B/C/D。

### 验收标准

- Windows 已配对设备不删除的情况下，当前固件仍能输出 A/B/C/D。
- 日志中能看到：

```text
[HOGP] HID advertising started
[HOGP] encryption_change ... enabled=1 status=0
[HOGP] read hdl=0x001e ...
[HOGP] key_send ... report=00 00 04 ...
[HOGP] key_send ret=0
```

- 有一份可对照的 GATT/Report Map 快照。

## Phase 1：纯搬迁，零行为变化

### 目标

把 HOGP 代码从 `rdx_ble_server.c` 中拆到 `rdx_hogp_keyboard.c/.h`，但不改变 PC 看到的任何行为。

### 工作内容

1. 新增 `rdx_hogp_keyboard.h/c`。
2. 迁移以下内容：
   - `hogp_mode`；
   - `hogp_connected`；
   - `hid_notify_enabled`；
   - `hogp_encrypted`；
   - `hid_con_handle`；
   - `hid_att_read()`；
   - `hid_att_write()`；
   - `hogp_key_send()`；
   - `hogp_key_click_send()`；
   - `hogp_adv_start()`；
   - `hogp_adv_stop()`；
   - SM Just Works 处理逻辑。
3. 在 `rdx_ble_server.c` 中保留转发：

```c
if (rdx_hogp_is_handle(handle)) {
    return rdx_hogp_att_read(...);
}
```

```c
if (rdx_hogp_is_handle(handle)) {
    return rdx_hogp_att_write(...);
}
```

4. HCI 事件中只调用：

```c
rdx_hogp_on_connected(con_handle);
rdx_hogp_on_disconnected(con_handle);
rdx_hogp_on_encryption_change(con_handle, enabled, status);
```

5. `rdx_app.c` 暂时仍可调用兼容 API：

```c
hogp_mode_get()
hogp_mode_set()
hogp_key_click_send()
```

这些 API 可在新模块里保留同名 wrapper，避免一次性改太多调用点。

### 验收标准

- `rdx_ble_server.c` 中不再直接定义 HOGP 状态变量。
- `rdx_ble_server.c` 中 HOGP 代码只剩转发和 profile data 总表。
- GATT handle 表与 Phase 0 完全一致。
- Report Map 与 Phase 0 字节级一致。
- Windows 不删除已配对设备，连接后仍能输出 A/B/C/D。
- HOGP 日志顺序与 Phase 0 等价。
- RDX App 原有 BLE 功能不回归：RDX 广播、连接、数据通道、断连恢复都正常。

## Phase 2：Profile 常量集中管理

### 目标

将 HID handle 宏、Report Map、HID Information 等从 `rdx_ble_server.c` 中集中到 `rdx_hogp_profile.h`，但 profile data 字节不改。

### 工作内容

1. 新增 `rdx_hogp_profile.h`。
2. 迁移：
   - HID handle 宏；
   - HID handle range 宏；
   - HID Report Map；
   - HID Information；
   - Report Reference 常量；
   - PnP ID / Manufacturer 常量，如果当前已经存在并需要 HOGP 管理。
3. 给每个 handle 增加注释，标明 UUID、属性、用途。
4. 增加静态检查宏：

```c
#define RDX_HOGP_HANDLE_START 0x0016
#define RDX_HOGP_HANDLE_END   0x0022
```

5. `rdx_ble_server.c` 的 profile data 仍保留总数组，但引用同一套 handle 宏，避免 handle 宏和 attribute 字节注释分叉。

### 验收标准

- HID handle 宏只在 `rdx_hogp_profile.h` 定义一次。
- `rdx_ble_server.c` 中没有重复的 HID handle magic number 定义。
- 编译通过。
- Report Map 长度与 Phase 0 一致。
- Windows 不删除已配对设备仍能输出 A/B/C/D。
- 日志中 read/write handle 与 Phase 0 一致。

## Phase 3：HOGP 传输配置化与 Keymap 外移

### 目标

把 HOGP 传输层可变项抽到 `rdx_hogp_config.h`，并明确按键映射不属于 HOGP。后续换键位、组合键、宏和 Layer 通过 RDX BLE App 按键设置与 Key Action Executor 完成，不修改 HOGP 核心逻辑。

### 工作内容

1. 新增 `rdx_hogp_config.h`。
2. 配置以下项：
   - `TCFG_RDX_HOGP_ENABLE`；
   - `TCFG_RDX_HOGP_REQUIRE_ENCRYPTION`；
   - `TCFG_RDX_HOGP_AUTO_REQUEST_PAIRING`；
   - `TCFG_RDX_HOGP_KEY_UP_DELAY_MS`；
   - `TCFG_RDX_HOGP_APPEARANCE`；
   - `TCFG_RDX_HOGP_NAME_USE_RDX_LOCAL_NAME`。
3. 将默认 A/B/C/D 临时映射标记为迁移期测试入口，不作为 HOGP 长期配置模型。
4. `rdx_app.c` 不再直接知道 NUM1=A、NUM2=B，而是向 Key Action Executor 转发标准化按键事件。
5. Key Action Executor 根据 RDX App 写入的 Keymap 生成 `rdx_hogp_keyboard_report_t`。
6. HOGP 模块只负责发送完整 Keyboard Report，不根据 keymap 决定 usage。
7. 所有 HOGP 配置宏提供默认值，避免未定义时报错。

### 验收标准

- 修改 `TCFG_RDX_HOGP_KEY_UP_DELAY_MS` 后，release 延迟随配置变化。
- 修改 RDX App Keymap 后，Key Action Executor 输出的 Report 变化，HOGP 发送路径不需要改动。
- `TCFG_RDX_HOGP_ENABLE=0` 时，HOGP 代码可被编译排除或行为关闭，RDX 原功能正常。
- 默认配置下，Windows 不删除已配对设备仍能输出 A/B/C/D。
- `rdx_app.c` 不再直接调用低层 `hogp_key_send()`。

## Phase 4：日志与诊断标准化

### 目标

统一 HOGP 日志格式，方便后续现场排查。

### 工作内容

1. 增加日志宏：

```c
#define RDX_HOGP_LOG(fmt, ...) y_printf("[HOGP] " fmt "\r", ##__VA_ARGS__)
```

2. 关键状态统一打印：
   - mode enter/exit；
   - adv start/stop；
   - connected/disconnected；
   - pairing request；
   - encryption change；
   - CCC read/write；
   - Report Map read；
   - Input Report send；
   - send ret。
3. 增加状态 dump：

```c
void rdx_hogp_dump_state(void);
```

示例输出：

```text
[HOGP] state mode=1 conn=1 con=0x0050 ccc=1 enc=1 hdl=0x12345678
```

### 验收标准

- 一次完整连接和按键日志能明确回答：
  - 是否进入 HOGP 模式；
  - 是否连接；
  - 是否加密；
  - 是否订阅 CCC；
  - 实际发送了什么 report；
  - 发送返回值是多少。
- 日志不再依赖乱码段中推断关键状态。
- 默认日志量不会明显影响按键发送时序。

## Phase 5：测试与回归脚本

### 目标

建立轻量级 host-side 检查，防止后续改动破坏 handle 或 Report Map。

### 工作内容

1. 新增 PowerShell 测试脚本：

```text
tests/host/test_hogp_profile_contract.ps1
```

2. 检查内容：
   - `HID_SERVICE_HANDLE == 0x0016`；
   - `HID_INPUT_REPORT_VALUE_HANDLE == 0x001a`；
   - `HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE == 0x001b`；
   - Report Map 长度等于当前冻结长度；
   - `rdx_profile_data[]` 中 HID attribute 顺序没有变化；
   - `hid_input_report` 默认长度为 8；
   - `hogp_key_send` 不再向 payload 写 Report ID。
3. 将脚本加入人工回归步骤。

### 验收标准

- `.\tests\host\test_hogp_profile_contract.ps1` 通过。
- 现有 `.\tests\host\test_t2620_config_overlay.ps1` 仍通过。
- 修改 handle 或 Report Map 时，测试能失败并给出清晰提示。

## Phase 6：Profile v2 预研，不默认启用

### 目标

在不影响当前稳定 Profile 的前提下，预研更规范的 HID Service 结构。

### 可选内容

1. 将 Output Report 放回 HID Service 内部。
2. 明确 Input Report / Output Report 的 Report Reference。
3. 评估是否保留 Report ID，或使用无 Report ID 的单键盘 Report Map。
4. 评估 Device Information Service 的位置与 PnP ID 权限。
5. 评估 Service Changed 或改 BLE 地址/名称版本以规避 Windows GATT cache。

### 验收标准

- Profile v2 必须由配置宏显式启用，例如：

```c
#define TCFG_RDX_HOGP_PROFILE_VERSION 2
```

- 默认固件仍使用当前稳定 Profile v1。
- 启用 Profile v2 时必须要求 Windows 删除旧配对设备后重新配对测试。
- Profile v2 需要单独产出测试日志和兼容性结论。

## 10. 每轮硬件回归清单

每个阶段完成后至少做以下回归：

1. 上电默认 RDX 模式正常广播。
2. 短按 NUM0 进入 HOGP 模式。
3. Windows 能看到 BLE HID 键盘。
4. Windows 能连接。
5. 日志出现加密成功。
6. 日志出现 Report Map read。
7. 短按 NUM1 输出 A。
8. 短按 NUM2 输出 B。
9. 短按 NUM3 输出 C。
10. 短按 NUM4 输出 D。
11. 长按 NUM0 退出 HOGP 模式。
12. RDX 广播恢复。
13. RDX App 原有 BLE 连接和数据通道仍正常。

## 11. 风险与规避

| 风险 | 影响 | 规避 |
|---|---|---|
| handle 改动导致 Windows GATT cache 错乱 | 能连但不出字或不订阅 CCC | Phase 1-5 禁止改 handle |
| Report Map 改动导致 HID 枚举变化 | Windows 重新识别或拒绝输入 | Report Map 字节级冻结 |
| HOGP 模块误拥有 BLE Server 生命周期 | 与 RDX handle 冲突 | HOGP 只接收 RDX 传入的 app_ble handle |
| Output Report 位置不规范 | CapsLock/NumLock 行为不稳定 | 放到 Profile v2 单独处理 |
| 日志不足 | 现场问题难定位 | Phase 4 标准化日志 |
| 配置宏误关功能 | 产物不可发现键盘 | 默认配置保持当前行为 |

## 12. 推荐提交拆分

建议按阶段提交，避免一个大提交同时移动代码、改行为、改配置。

```text
commit 1: docs: add HOGP modular refactor plan
commit 2: refactor: move HOGP state and report send into module
commit 3: refactor: move HOGP ATT read/write into module
commit 4: refactor: centralize HOGP profile constants
commit 5: config: add HOGP keyboard config header
commit 6: test: add HOGP profile contract host check
commit 7: log: standardize HOGP diagnostics
```

## 13. 最终完成定义

本次模块化重构完成时，应满足：

- HOGP 主要逻辑不再堆在 `rdx_ble_server.c`；
- `rdx_ble_server.c` 继续承载 RDX BLE Server/RDX App 底座，并只通过转发点接入 HOGP；
- `rdx_app.c` 继续承载 RDX App 业务，并把物理按键和 App 按键设置转交给 Key Action/配置层；
- HOGP 的传输配置、profile 常量、协议状态机、ATT 处理、Report 发送有清晰文件边界；
- 默认配置下，Windows 旧配对不删除也能继续输出字母；
- RDX 原有 BLE 功能不回归；
- 后续改键位走 RDX App 按键设置和 Key Action Executor，改 HOGP 传输策略、关 HOGP、试 Profile v2 都有明确入口。
