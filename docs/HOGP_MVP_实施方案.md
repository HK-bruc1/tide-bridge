# HOGP 键盘最小可行性（MVP）实施方案（v2.0 已实施版）

> **状态：Phase 1–3 全部代码已实现，硬件测试已通过。**
> 本文档根据 `rdx_ble_server.c` / `rdx_ble_server.h` / `rdx_app.c` 的实际提交代码编写。

## 目标

1. 在 `t2620-firmware` 工程中**扩展现有 RDX GATT Server**，追加 HID Service（0x1812），使 PC 发现并识别为 HID 键盘；
2. 在 RDX 框架内扩展，复用 RDX 的 `app_ble` handle、广播和连接管理；
3. 复用生产工程已有的按键事件框架，实现"按一个键 → PC 记事本出字母"的最小验证。


## 当前状态（2026-07-09 更新）

**PC 连接已打通！** 经过以下修复，设备已能作为 BLE HID 键盘被 Windows 识别并成功连接：

| 问题 | 根因 | 修复 |
|------|------|------|
| PC 扫描到设备但连接即断开 | 广播名 (VibeKeyboard) 与 GAP Device Name (Beanstalk RKB 0002) 不一致 | hogp_fill_adv_data() 改用 
dx_ble_server_get_local_name() 统一名称 |
| 名称统一后仍连接断开 (0x0D) | config_le_sm_support_enable = 0，SM 配对模块未编译进固件 | lib_btstack_config.c 中改为 1 |
| PC 发起配对后设备无响应 | 设备未主动发起 Security Request | HOGP 连接完成时调用 sm_api_request_pairing(con_handle) |
| Just Works 配对确认缺失 | 缺少 SM 事件回调处理 SM_EVENT_JUST_WORKS_REQUEST | 注册回调并在 HOGP 模式下调用 sm_just_works_confirm() |

**已解决：** 按键发送 ret=0 但 PC 输入框无字母输出的问题已定位并修复，详见“坑5”。当前 HOGP MVP 已全部打通：PC 可发现、配对、连接，短按 IO NUM1~4 能在记事本输出 A/B/C/D。

---

## 踩坑记录

### 坑1：广播名与 GAP Device Name 不一致

**现象：** PC 扫描到 VibeKeyboard 键盘图标，连接后读取 GAP Device Name 得到 Beanstalk RKB 0002，Windows 判定设备身份不匹配，主动断开（HCI_EVENT_DISCONNECTION_COMPLETE: 0x0D）。

**根因：** hogp_fill_adv_data() 中广播名写死 "VibeKeyboard"，而 GAP Device Name 读回调始终返回 
dx_ble_server_get_local_name()（即 Beanstalk RKB 0002）。PC 连上来问"你是谁"，设备回答的名字跟广播里不一样。

**修复：** hogp_fill_adv_data() 改用 
dx_ble_server_get_local_name() 填充广播名，与 GAP Device Name 一致。PC 通过广播中的 HID Service UUID (0x1812) + Appearance (0x03C1) 识别设备为键盘，名称只作显示用途。

**日志证据（修复前）：**
`
------read gap_name: Beanstalk RKB 0002      ← GAP 返回旧名字
HCI_EVENT_DISCONNECTION_COMPLETE: 13          ← 0x0D = PC 主动断开
`

### 坑2：config_le_sm_support_enable = 0，SM 模块未编译

**现象：** sm_api_request_pairing() 调用后 PC 端 BLE 工具显示"开始配对"，但随即断开。设备端无任何 SM/配对日志。

**根因：** lib_btstack_config.c 中所有编译分支都将 config_le_sm_support_enable 设为 （"dons++" 注释表明被人为从 1 改为 ）。SM 模块代码未编译，sm_set_io_capabilities、sm_just_works_confirm 等均为空壳。

**修复：** lib_btstack_config.c 中 config_le_sm_support_enable 改为 1。对 RDX 连接无影响——pp_ble_sm_init 中 security_en=0 不变，RDX 仍不主动加密。

**PC 端工具日志证据：**
`
[INFO] 服务信息获取成功
[INFO] 开始配对
[WARN] 设备连接断开              ← SM 协议栈不在，配对超时
`

### 坑3：设备未主动发起配对 + Just Works 确认缺失

**现象：** config_le_sm_support_enable = 1 后 PC 仍断开。SM 模块已存在，但配对流程有两步缺失。

**根因：**
- multi_protocol_main.c:372 中 pp_ble_sm_init(..., 0) 最后一个参数 security_en=0 → sm_set_request_security(0)，设备不主动发 Security Request
- SDK 所有 BLE 示例（RCSP、FMY、DMA 等）均在 SM 事件回调中显式处理 SM_EVENT_JUST_WORKS_REQUEST 并调用 sm_just_works_confirm()，但 RDX 项目未注册 SM 事件回调

**修复：**
1. HOGP 连接完成时显式调用 sm_api_request_pairing(con_handle)，仅影响 HOGP 连接
2. 新增 
dx_ble_server_sm_event_callback，在 HOGP 模式下调用 sm_just_works_confirm()
3. 在 
dx_ble_server_init() 中注册：pp_ble_sm_event_callback_register()

**新增 SM 事件回调：**
`c
static void rdx_ble_server_sm_event_callback(void *hdl, uint8_t packet_type,
    uint16_t channel, uint8_t *packet, uint16_t size)
{
    switch (packet_type) {
    case HCI_EVENT_PACKET:
        switch (hci_event_packet_get_type(packet)) {
        case SM_EVENT_JUST_WORKS_REQUEST:
            if (hogp_mode) {
                y_printf("[HOGP] Just Works pairing request, confirm\r");
                sm_just_works_confirm(sm_event_just_works_request_get_handle(packet));
            }
            break;
        }
        break;
    }
}
`

### 坑4：断连后 RDX 广播覆盖 HOGP 广播

**现象：** 进入 HOGP 模式后，若之前有活跃 RDX 连接，hogp_mode_set(1) 触发异步断开，断连 handler 执行 RDX 的 dv_data_changed 恢复 RDX 广播，HOGP 广播被覆盖。

**根因：** HCI_EVENT_DISCONNECTION_COMPLETE 中 HOGP 断连跟踪后无 
eturn/reak，代码落入 RDX 断连逻辑。

**修复：** HOGP 断连跟踪块内加 reak;，阻止执行 RDX 的 
dx_ble_server_disconnected_handle()。

### 坑5：Input Report payload 里多塞了一个 Report ID 字节

**现象：** PC 能发现、配对、连接，CCC 也订阅了（CCC write cfg=0x0001），`app_ble_att_send_data()` 返回 0，但记事本就是不出现字母。

**排查过程：**
1. 日志确认 `report=00 00 04 00 00 00 00 00` 已经发出（keycode 0x04 = A 在 index 2），ret=0；
2. 怀疑是 CCC 问题，但 CCC read 最终显示已订阅；删除 PC 配对重连问题依旧；
3. 对照 AC63 参考工程 `fw-AC63_BT_SDK/apps/hid/examples/standard_keyboard/app_standard_keyboard.c`，发现其发送的是 8 字节 report，keycode 在 index 2，没有 Report ID 前缀；
4. 再对照 `fw-AC63_BT_SDK/apps/hid/modules/bt/ble_hogp.c` 的 `ble_hid_data_send()`，它直接发 `data`，不在 payload 前补 Report ID；
5. 回头看当前代码：`hogp_key_send()` 里 `u8 report[9]`，先写 `report[0] = 0x01`（Report ID），再把 keycode 放到 `report[3]`。发给 Windows 的 9 字节被解析成：byte0=modifier、byte1=reserved、byte2=keycode[0]，导致 keycode 被错位丢弃。

**根因：** 混淆了 USB HID 和 BLE HID 的 Report ID 用法。
- USB HID：多个 report 共享一个 endpoint，数据包前需要 Report ID 字节。
- BLE HID：每个 report 有独立的 characteristic handle，`Report Reference` descriptor（0x2908）已经把 Report ID 和 Type 告诉主机，ATT payload 只发 report 本体。

**修复：**
1. `hid_input_report[9]` → `hid_input_report[8]`；
2. `hogp_key_send()` 里 `u8 report[8]`，删除 `report[0] = 0x01`；
3. keycode 从 `report[3]` 改到 `report[2]`；
4. `app_ble_att_send_data()` 发送 `sizeof(report)` = 8 字节；
5. `hid_att_read()` 中 `HID_INPUT_REPORT_VALUE_HANDLE` 返回 8 字节。

**验证日志（COM3_2026-07-09_16-45-05.log）：**
```text
[HOGP] key_send idx=0 pressed=1 report=00 00 04 00 00 00 00 00 conn=1 notify=1 encrypted=1
[HOGP] key_send ret=0
```
PC 记事本成功输出字母 A/B/C/D。

**教训：** 不要凭 USB HID 惯性在 BLE HID 的 ATT payload 里加 Report ID。Report ID 只写在 Report Map（0x85, 0x01）和 Report Reference descriptor 里即可。

---

## 1. 整体思路

AC701N 的 BLE 业务通过 `app_ble_*` 包装层管理 GATT Server。关键约束：

- `app_ble_init()` 在 `btstack.a` 内部调用一次 `att_server_init()`，创建底层共享 ATT 数据库；
- `config_le_gatt_server_num = 1`，不能创建独立 Server 实例；
- RDX 框架已注册 profile data（handle `0x0001–0x0015`），**HOGP 必须在同一份 profile data 内扩展**；
- 广播层决定设备对外暴露成"HID 键盘"还是"RDX App 设备"——广播和 GATT Profile 是独立的，切换广播不改变数据库；
- 按键事件拦截在 `rdx_app.c` 的 `rdx_app_earphone_key_remap()` 中完成。

---

## 2. Phase 1：在 RDX profile data 中追加 HID Service

**实现文件：** `SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_server.c`

### 实际 handle 布局

```text
0x0001–0x0003: GAP Service (0x1800): Device Name
0x0004–0x000b: 自定义 128-bit UUID Service (RDX 协议通道)
0x000c–0x000f: Battery Service (0x180F)
0x0010–0x0015: 另一个自定义 128-bit UUID Service (Notify 通道)
0x0016–0x0025: HID Service (0x1812，含 Input/Output Report)
0x0026–0x002a: Device Information Service (0x180A)
```

### HID Service 详细 attribute 表

| Handle | 类型 | UUID | Properties | 说明 |
|--------|------|------|------------|------|
| 0x0016 | Service Declaration | 0x1812 | — | HID Service |
| 0x0017 | Characteristic | 0x2A4E | — | Protocol Mode 声明 |
| 0x0018 | Value | 0x2A4E | Read, Write w/o Resp | Protocol Mode 值 (默认 0x01 = Report Protocol) |
| 0x0019 | Characteristic | 0x2A4D | — | Input Report 声明 |
| 0x001a | Value | 0x2A4D | Read, Notify, Dynamic | Input Report 值 (8 字节键盘 Report) |
| 0x001b | Descriptor | 0x2902 | Read, Write | CCC（Client Characteristic Configuration） |
| 0x001c | Descriptor | 0x2908 | Read | Report Reference (ID=1, Type=Input) |
| 0x001d | Characteristic | 0x2A4B | — | Report Map 声明 |
| 0x001e | Value | 0x2A4B | Read, Dynamic | Report Map（标准 8 字节键盘，含 Output LED 描述） |
| 0x001f | Characteristic | 0x2A4A | — | HID Information 声明 |
| 0x0020 | Value | 0x2A4A | Read, Dynamic | HID Information (bcd=1.11, country=0, flags=3) |
| 0x0021 | Characteristic | 0x2A4C | — | HID Control Point 声明 |
| 0x0022 | Value | 0x2A4C | Write w/o Resp, Dynamic | HID Control Point |
| 0x0023 | Characteristic | 0x2A4D | — | Output Report 声明 |
| 0x0024 | Value | 0x2A4D | Read, Write, Write w/o Resp | Output Report 值 (1 字节 LED 状态) |
| 0x0025 | Descriptor | 0x2908 | Read | Report Reference (ID=1, Type=Output) |
| 0x0026 | Service Declaration | 0x180A | — | Device Information Service |
| 0x0027 | Characteristic | 0x2A50 | — | PnP ID 声明 |
| 0x0028 | Value | 0x2A50 | Read | PnP ID (USB-IF, VID=0x1234, PID=0x0001, Ver=0x0001) |
| 0x0029 | Characteristic | 0x2A29 | — | Manufacturer Name 声明 |
| 0x002a | Value | 0x2A29 | Read | Manufacturer Name ("JieLi") |

### Handle 宏定义（实际代码）

```c
#define HID_SERVICE_HANDLE                          0x0016
#define HID_PROTOCOL_MODE_CHARACTERISTIC_HANDLE     0x0017
#define HID_PROTOCOL_MODE_VALUE_HANDLE              0x0018
#define HID_INPUT_REPORT_CHARACTERISTIC_HANDLE      0x0019
#define HID_INPUT_REPORT_VALUE_HANDLE               0x001a
#define HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE 0x001b
#define HID_INPUT_REPORT_REFERENCE_HANDLE            0x001c
#define HID_REPORT_MAP_CHARACTERISTIC_HANDLE        0x001d
#define HID_REPORT_MAP_VALUE_HANDLE                 0x001e
#define HID_INFORMATION_CHARACTERISTIC_HANDLE       0x001f
#define HID_INFORMATION_VALUE_HANDLE                0x0020
#define HID_CONTROL_POINT_CHARACTERISTIC_HANDLE     0x0021
#define HID_CONTROL_POINT_VALUE_HANDLE              0x0022

// Output Report (inside HID Service)
#define HID_OUTPUT_REPORT_CHARACTERISTIC_HANDLE     0x0023
#define HID_OUTPUT_REPORT_VALUE_HANDLE              0x0024
#define HID_OUTPUT_REPORT_REFERENCE_HANDLE          0x0025

// Device Information Service (0x180A)
#define DIS_SERVICE_HANDLE                          0x0026
#define DIS_PNP_ID_CHARACTERISTIC_HANDLE            0x0027
#define DIS_PNP_ID_VALUE_HANDLE                     0x0028
#define DIS_MANUFACTURER_NAME_CHARACTERISTIC_HANDLE 0x0029
#define DIS_MANUFACTURER_NAME_VALUE_HANDLE          0x002a
```

### Report Map（标准 8 字节 Keyboard Report）

使用 AC63 `ble_hogp_profile.h` 的键盘部分，声明了 8 个 modifier bits + 6 个 key slots 的标准键盘 Report。

### 辅助数据

```c
static const u8 hid_information[] = {0x11, 0x01, 0x00, 0x03}; // bcdHID=1.11
static u8 hid_protocol_mode = 1;  // Report Protocol
static u8 hid_input_report[8] = {0};
static volatile u8 hid_notify_enabled = 0;
```

### HID read / write 分发函数

- `hid_read_helper(data, len, offset, buffer, buffer_size)` — 手动 memcpy 长数据
- `hid_att_read(att_handle, offset, buffer, buffer_size)` — HID value handle + DIS value handle 分发
- `hid_att_write(connection_handle, att_handle, buffer, buffer_size)` — CCC + Control Point + Output Report 分发，使用 `multi_att_set_ccc_config(connection_handle, att_handle, cfg)` 写入带连接句柄的 CCC 表

### Read / Write callback 扩展

在 `rdx_ble_server_att_read_callback()` 的 switch 中：
- 新增 HID value handle case + DIS value handle case → 调用 `hid_att_read()` → break（落到统一 return）
- 新增 `HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE` case → `multi_att_get_ccc_config(connection_handle, handle)` → 返回 2 字节 CCC 值

在 `rdx_ble_server_att_write_callback()` 的 switch 中：
- 新增 HID Control Point + CCC + Output Report case → 调用 `hid_att_write(connection_handle, ...)` → break（落到统一 `return 0`）

### GATT 注册

`app_ble_profile_set(hdl, rdx_profile_data)` 在 `rdx_ble_server_init()` 中注册，一次注册全部 RDX + HID attribute。Read / Write callback 通过 `app_ble_att_read_callback_register` / `app_ble_att_write_callback_register` 绑定到同一个 `app_ble` handle。

### PC 发现验证

1. 三击 KEY5（IO NUM4），设备切换到 HOGP 模式；
2. PC 蓝牙设置里应看到键盘图标设备；
3. 用 nRF Connect / LightBlue 扫描，广播包应包含 0x1812 和 Appearance 0x03C1；
4. **广播名与 GAP Device Name 必须一致**（当前统一为 RDX 本地名，例如 "Beanstalk RKB 0002"），否则 PC 可能在连接后因身份不一致而断开。

---

## 3. Phase 2：复用按键事件框架发 HID Report

**实现文件：** `rdx_ble_server.c`（HOGP API）+ `rdx_ble_server.h`（API 声明）+ `rdx_app.c`（按键拦截）

### 按键 hook 点

在 `rdx_app.c` 的 `rdx_app_earphone_key_remap()` 中，IO NUM 键值分发处加入 HOGP 拦截。按键路径：

```text
硬件按键 → sys_event → key_event_deal() → rdx_app_earphone_key_remap()
       → KEY_IO_NUM0~4 分发
         ├── hogp_mode == 1 → HOGP 处理 → APP_MSG_NULL（不进入 RDX 业务）
         └── hogp_mode == 0 → rdx_key_get_io_num_table() → RDX 原有逻辑
```

### 按键映射表

| 状态 | 按键 | 动作 | HID Usage | 说明 |
|------|------|------|-----------|------|
| 任意模式 | IO NUM4 三击 | 切换 HOGP/Config 模式 | — | 由正式 mode controller 执行断连与广播切换 |
| HOGP | IO NUM1 短按 | 发送字母 A | 0x04 | down + 20ms 后自动 up |
| HOGP | IO NUM2 短按 | 发送字母 B | 0x05 | 同上 |
| HOGP | IO NUM3 短按 | 发送字母 C | 0x06 | 同上 |
| HOGP | IO NUM4 短按 | 发送字母 D | 0x07 | 同上 |

### 当前正式按键拦截代码

```c
/* 早期 MVP 的 IO NUM0 短按/长按切换已废弃。产品正式契约为
 * KEY5/IO_NUM4 三击，并统一通过 mode controller 切换。 */
if (num_idx == 4 && index == KEY_ACTION_TRIPLE_CLICK) {
    rdx_ble_mode_request_toggle();
    *value = APP_MSG_NULL;
    return;
}
```

### HOGP 状态变量（rdx_ble_server.c）

```c
static volatile u8 hogp_mode = 0;          // 1: HOGP 键盘模式
static volatile u8 hogp_connected = 0;     // 1: PC 已连接且当前处于 HOGP 模式
static volatile u8 hid_notify_enabled = 0; // Input Report CCC 已使能
static volatile u8 hogp_encrypted = 0;     // 1: HOGP 连接已加密
static u16 hid_con_handle = 0;             // 当前 HID 连接句柄
```

### HID Report 发送（hogp_key_send）

```c
static const u8 key_to_hid_usage[5] = {0x04, 0x05, 0x06, 0x07, 0x08}; // A B C D E

void hogp_key_send(u8 key_index, u8 pressed)
{
    u8 report[8] = {0};

    if (key_index >= 5) {
        y_printf("[HOGP] err: key_index %d out of range\r", key_index);
        return;
    }

    if (pressed) {
        report[2] = key_to_hid_usage[key_index];
    }

    y_printf("[HOGP] key_send idx=%d pressed=%d report=%02x %02x %02x %02x %02x %02x %02x %02x conn=%d notify=%d encrypted=%d\r",
             key_index, pressed,
             report[0], report[1], report[2], report[3],
             report[4], report[5], report[6], report[7],
             hogp_connected, hid_notify_enabled, hogp_encrypted);

    // 四层检查，逐层 log
    if (!hogp_connected) {
        y_printf("[HOGP] key_send skipped: not connected\r");
        return;
    }
    if (g_rdx_ble_server_info.rdx_ble_server_hdl == NULL) {
        y_printf("[HOGP] key_send skipped: server hdl NULL\r");
        return;
    }
    if (!hid_notify_enabled) {
        y_printf("[HOGP] key_send skipped: notify not enabled\r");
        return;
    }
    if (!hogp_encrypted) {
        y_printf("[HOGP] key_send skipped: not encrypted\r");
        return;
    }

    int ret = app_ble_att_send_data(g_rdx_ble_server_info.rdx_ble_server_hdl,
                                    HID_INPUT_REPORT_VALUE_HANDLE,
                                    report, sizeof(report),
                                    ATT_OP_NOTIFY);
    y_printf("[HOGP] key_send ret=%d\r", ret);
}
```

### 短按自动释放（hogp_key_click_send）

```c
void hogp_key_click_send(u8 key_index)
{
    if (key_index >= 5) {
        return;
    }
    hogp_key_send(key_index, 1);                          // key down
    sys_timeout_add((void *)(u32)key_index,
                    hogp_key_up_timeout, 20);             // 20ms 后 key up
}
```

### 连接 / 断开跟踪

在 `rdx_ble_server_cbk_packet_handler()` 的 HCI 事件处理中：

```c
// HCI_SUBEVENT_LE_CONNECTION_COMPLETE / ENHANCED:
if (hogp_mode) {
    hogp_connected = 1;
    hid_con_handle = con_handle;
    y_printf("[HOGP] conn complete hdl=0x%04x\r", con_handle);
    sm_api_request_pairing(con_handle);   // 触发 Just Works 配对
}

// HCI_EVENT_DISCONNECTION_COMPLETE:
if (hogp_mode) {
    hogp_connected = 0;
    hid_con_handle = 0;
    hid_notify_enabled = 0;
    hogp_encrypted = 0;
    y_printf("[HOGP] disconnect\r");
    break;   // 防止执行下面的 RDX 断连逻辑（恢复 RDX 广播会覆盖 HOGP 状态）
}

// HCI_EVENT_ENCRYPTION_CHANGE:
if (hogp_mode) {
    u16 enc_handle = hci_event_encryption_change_get_connection_handle(packet);
    u8 enc_enabled = hci_event_encryption_change_get_encryption_enabled(packet);
    u8 enc_status = hci_event_encryption_change_get_status(packet);
    if (enc_handle == hid_con_handle && enc_enabled && enc_status == 0) {
        hogp_encrypted = 1;
        y_printf("[HOGP] link encrypted\r");
    }
}
```

### SM / 配对事件回调

RDX 原来没有注册 SM 事件回调，导致 PC 发起 Just Works 配对后设备 never confirm。修复：注册 `rdx_ble_server_sm_event_callback`，在 `SM_EVENT_JUST_WORKS_REQUEST` 时调用 `sm_just_works_confirm()`。

```c
static void rdx_ble_server_sm_event_callback(void *hdl, uint8_t packet_type,
                                             uint16_t channel, uint8_t *packet, uint16_t size)
{
    switch (packet_type) {
    case HCI_EVENT_PACKET:
        switch (hci_event_packet_get_type(packet)) {
        case SM_EVENT_JUST_WORKS_REQUEST:
            if (hogp_mode) {
                y_printf("[HOGP] Just Works pairing request, confirm\r");
                sm_just_works_confirm(sm_event_just_works_request_get_handle(packet));
            }
            break;
        }
        break;
    }
}
```

并在 `rdx_ble_server_init()` 中：

```c
app_ble_sm_event_callback_register(g_rdx_ble_server_info.rdx_ble_server_hdl,
                                   rdx_ble_server_sm_event_callback);
```

### CCC 读写（使用 multi_att API）

```c
// Write: PC subscribe CCC
case HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE:
    if (buffer_size >= 2) {
        u16 cfg = buffer[0] | (buffer[1] << 8);
        hid_notify_enabled = (cfg & 0x01);
        multi_att_set_ccc_config(connection_handle, att_handle, cfg);
        y_printf("[HOGP] CCC write hdl=0x%04x cfg=0x%04x notify=%d\r",
                 att_handle, cfg, hid_notify_enabled);
    }
    return 0;

// Read: PC 查询 CCC 状态
case HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE:
    att_value_len = 2;
    if (buffer) {
        buffer[0] = multi_att_get_ccc_config(connection_handle, handle) & 0xFF;
        buffer[1] = 0;
        y_printf("[HOGP] CCC read hdl=0x%04x cfg=0x%02x%02x\r",
                 handle, buffer[0], buffer[1]);
    }
    break;
```

> **关键：** 使用 `multi_att_set_ccc_config(connection_handle, att_handle, cfg)` 而非不带句柄的版本，确保多连接或连接切换场景下 CCC 与正确的主机关联。

### API 声明（rdx_ble_server.h）

```c
void hogp_mode_set(u8 enable);
u8 hogp_mode_get(void);
void hogp_key_send(u8 key_index, u8 pressed);
void hogp_key_click_send(u8 key_index);
```

---

## 4. Phase 3：HOGP 模式广播切换

**实现文件：** `rdx_ble_server.c`

### 核心函数：hogp_mode_set()

```c
void hogp_mode_set(u8 enable)
{
    u8 new_mode = enable ? 1 : 0;

    if (new_mode == hogp_mode) {
        return;  // 幂等
    }

    /* 如果有活跃连接，先断开，确保对端在新广播身份下重连 */
    if (g_rdx_ble_server_info.ble_conn) {
        y_printf("[HOGP] active ble conn, disconnect before mode switch\r");
        rdx_ble_server_app_disconnect();
    }

    if (new_mode) {
        hogp_mode = 1;
        hogp_adv_start();
    } else {
        hogp_adv_stop();
        hogp_mode = 0;
        hogp_connected = 0;
        hid_con_handle = 0;
        hid_notify_enabled = 0;
    }
}
```

### 进入 HOGP 广播：hogp_adv_start()

1. 停止 RDX 广播 `app_ble_adv_enable(hdl, 0)`
2. **清空旧 scan response** `app_ble_rsp_data_set(hdl, NULL, 0)`（防止 active scan 时残留 RDX 数据）
3. 填充 HID 广播数据 → `app_ble_adv_data_set(hdl, advData, len)`
4. 开启 HID 广播 `app_ble_adv_enable(hdl, 1)`

### HID 广播数据内容

| 字段 | 值 | 说明 |
|------|-----|------|
| Flags | 0x06 | LE General Discoverable, BR/EDR Not Supported |
| 16-bit Service UUIDs | 0x1812 | Human Interface Device |
| Appearance | 0x03C1 | HID Keyboard |
| Complete Local Name | `rdx_ble_server_get_local_name()` | 与 GAP Device Name 保持一致（原写死 "VibeKeyboard" 会导致 PC 断连） |

### 退出 HOGP 广播：hogp_adv_stop()

1. 停止 HID 广播 `app_ble_adv_enable(hdl, 0)`
2. 调用 `rdx_ble_server_adv_enable(1)` 恢复 RDX 广播（含 adv data + scan response + LED 控制）

### 广播与 GATT 的关系

> **广播和 GATT Profile 是独立的。** `rdx_profile_data[]` 永远同时包含 RDX Service 和 HID Service，广播切换只改变广播包里的 UUID 和 Appearance，不改变 ATT 数据库。PC 连上后做 Service Discovery 会看到两个 Service，但驱动层只会接管各自认识的 UUID，业务上互不干扰。

---

## 5. 验收标准与日志诊断

### 最小打通标准

**PC 记事本里出现字母（A / B / C / D），就算 MVP 打通。**

### 测试步骤

1. 设备上电，RDX 模式正常运行
2. **三击 KEY5（IO NUM4）** → 切换到 HOGP 模式
3. PC 蓝牙扫描 → 发现键盘图标设备（名称与 GAP Device Name 一致，例如 "Beanstalk RKB 0002"）→ 连接
4. 打开记事本
5. **短按 IO NUM1~4** → 记事本出现 A / B / C / D
6. **再次三击 KEY5（IO NUM4）** → 退出 HOGP 模式，恢复 RDX 广播

### 正常流程串口日志序列

```
[HOGP] enter HOGP mode
[HOGP] active ble conn, disconnect before mode switch   ← 如果切换时已连接
[HOGP] HID advertising started
       ← PC 连接 →
[HOGP] conn complete hdl=0x0050
[HOGP] Just Works pairing request, confirm               ← SM 回调确认配对
       ← 加密完成 →
[HOGP] encryption_change hdl=0x0050 enabled=1 status=0
[HOGP] link encrypted
[HOGP] read hdl=0x001e offset=0 len=70   ← PC 读 Report Map
[HOGP] read hdl=0x0020 offset=0 len=4    ← PC 读 HID Information
[HOGP] CCC write hdl=0x001b cfg=0x0001 notify=1   ← PC 使能 notify
       ← 短按 IO NUM1 →
[HOGP] key_send idx=0 pressed=1 report=00 00 04 00 00 00 00 00 conn=1 notify=1 encrypted=1
[HOGP] key_send ret=0
[HOGP] key_send idx=0 pressed=0 report=00 00 00 00 00 00 00 00 conn=1 notify=1 encrypted=1
[HOGP] key_send ret=0
```

### 日志诊断表

| 现象 | 关键 log | 诊断 |
|------|----------|------|
| 按键无反应（无 HOGP log） | 无 `[HOGP]` 前缀 | key->value 不在 IO_NUM0~4，或 key_event_deal 未走到 rdx_app_earphone_key_remap |
| enter HOGP mode 有，HID advertising started 无 | 缺少第二行 | `rdx_ble_server_hdl == NULL`，rdx_ble_server_init 未完成或调用顺序错 |
| PC 搜不到键盘设备 | `HID advertising started` 已打印 | 用 nRF Connect 手机 App 扫描，确认广播包是否包含 0x1812 / Appearance 0x03C1；如无，检查 app_ble_adv_data_set 返回值 |
| **PC 连上后约 30s 断开，断连原因 0x0D，GAP Name ≠ 广播名** | `------read gap_name: Beanstalk RKB 0002` 与广播名不一致 | 广播名和 GAP Device Name 必须一致；检查 `hogp_fill_adv_data()` 是否用了 `rdx_ble_server_get_local_name()` |
| **PC 连上但无 CCC write log，随后断开** | 有 `[HOGP] conn complete`，无 `[HOGP] Just Works pairing request, confirm` | 未注册 SM 事件回调或未在 `SM_EVENT_JUST_WORKS_REQUEST` 调用 `sm_just_works_confirm()` |
| PC 连上但无 CCC write log | 缺少 `CCC write` | PC 蓝牙驱动未完成 HID 枚举；删除 PC 上已配对设备重连；检查是否有 ATT 层错误 |
| CCC write cfg=0x0000 | `cfg=0x0000` | PC 未使能通知；删除配对重连；尝试换一台 PC |
| key_send skipped: not connected | `skipped: not connected` | HCI 连接事件未触发或 hogp_mode 在连接前被清；检查 conn complete log 是否存在 |
| key_send skipped: notify not enabled | `skipped: notify not enabled` | PC 连上了但未写 CCC；检查 CCC write log 是否出现 |
| key_send ret ≠ 0 | `ret=X` (非 0) | 查看 `app_ble_att_send_data` 文档：-1=参数错，-2=CCC 未使能，其他=栈内部错误 |
| 模式切换后按键发不出 | `active ble conn, disconnect before mode switch` | 正常：切换前有活跃连接，已断开。对端需重连 |
| **HOGP 断连后 PC 搜不到 HOGP 广播，变回 RDX 广播** | 断开 log 后紧跟 RDX 广播恢复 log | `HCI_EVENT_DISCONNECTION_COMPLETE` 的 HOGP 分支缺少 `break;`，执行流落入 RDX 断连逻辑 |
| PC 连接后按键无响应（无 key_send log） | 有 CCC write 但无 key_send | 检查 NUM1~4 的 KEY_ACTION_CLICK 是否被识别；确认 num_idx-1 映射正确 |
| **PC 连接、CCC 已订阅、ret=0，但记事本无字母** | `report=00 00 04 00 00 00 00 00` 但无输出 | Input Report payload 里多塞了 Report ID。参考坑5：BLE HID 的 ATT payload 只发 report 本体，Report ID 由 Report Reference descriptor 标识 |
| PC 连接后 Output Report 写不进来 | 无 `output report write` log | Output Report handle 路由是否正确；检查 characteristic properties 是否包含 Write |

### 已知风险

**Output Report 已实现：** 当前在 HID Service 内的 handle `0x0023–0x0025` 提供 Output Report characteristic（Report ID=1, Type=Output），Windows 写 LED 状态时由 `rdx_hogp_att_write()` 更新一字节 LED 状态。T2620 没有物理 LED 指示灯，后续如需驱动 LED，可在该 Output Report case 中增加产品回调。

---

## 6. 改动文件清单

| 文件 | 改动内容 |
|------|----------|
| `SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_server.c` | 扩展 rdx_profile_data[]（HID Service 0x0016–0x0025，Device Information Service 0x0026–0x002a），并把 HID handle 统一路由到 HOGP 子模块 |
| `SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_server.h` | 新增 HOGP API 声明 |
| `SDK/apps/common/third_party_profile/rdx_protocol/rdx_app.c` | 在 `rdx_app_earphone_key_remap()` 的 IO NUM 键值分发处增加 HOGP 模式拦截 |
| `SDK/apps/earphone/log_config/lib_btstack_config.c` | `config_le_sm_support_enable` 改为 1，使 SM 配对模块编译进固件 |
| `SDK/apps/common/third_party_profile/multi_protocol_main.c` | 调整 SM 初始化参数，配合 HOGP 配对流程 |

**未修改的文件：** Makefile、bt_key_msg_table.c（按键在 rdx_app.c 层拦截，未走 key table 方案）。

---

## 7. 预计时间

| 阶段 | 内容 | 状态 |
|------|------|------|
| Phase 1 | GATT 扩展 + callback | ✅ 已完成 |
| Phase 2 | 按键 → HID Report | ✅ 已完成 |
| Phase 3 | 广播切换 | ✅ 已完成 |
| 硬件测试 | 烧录验证 | ✅ 已通过 |
