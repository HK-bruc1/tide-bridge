# HOGP 键盘最小可行性（MVP）实施方案（v2.0 已实施版）

> **状态：Phase 1–3 全部代码已实现，待硬件测试。**
> 本文档根据 `rdx_ble_server.c` / `rdx_ble_server.h` / `rdx_app.c` 的实际提交代码编写。

## 目标

1. 在 `t2620-firmware` 工程中**扩展现有 RDX GATT Server**，追加 HID Service（0x1812），使 PC 发现并识别为 HID 键盘；
2. 在 RDX 框架内扩展，复用 RDX 的 `app_ble` handle、广播和连接管理；
3. 复用生产工程已有的按键事件框架，实现"按一个键 → PC 记事本出字母"的最小验证。

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
0x0016–0x0022: 【HID Service (0x1812)】← 新增
```

### HID Service 详细 attribute 表

| Handle | 类型 | UUID | Properties | 说明 |
|--------|------|------|------------|------|
| 0x0016 | Service Declaration | 0x1812 | — | HID Service |
| 0x0017 | Characteristic | 0x2A4E | — | Protocol Mode 声明 |
| 0x0018 | Value | 0x2A4E | Read, Write w/o Resp | Protocol Mode 值 (默认 0x01 = Report Protocol) |
| 0x0019 | Characteristic | 0x2A4D | — | Input Report 声明 |
| 0x001a | Value | 0x2A4D | Read, Write, Notify, Dynamic | Input Report 值 (8 字节键盘 Report) |
| 0x001b | Descriptor | 0x2902 | Read, Write | CCC（Client Characteristic Configuration） |
| 0x001c | Descriptor | 0x2908 | Read | Report Reference (ID=1, Type=Input) |
| 0x001d | Characteristic | 0x2A4B | — | Report Map 声明 |
| 0x001e | Value | 0x2A4B | Read, Dynamic | Report Map（标准 8 字节键盘） |
| 0x001f | Characteristic | 0x2A4A | — | HID Information 声明 |
| 0x0020 | Value | 0x2A4A | Read, Dynamic | HID Information (bcd=1.11, country=0, flags=3) |
| 0x0021 | Characteristic | 0x2A4C | — | HID Control Point 声明 |
| 0x0022 | Value | 0x2A4C | Write w/o Resp, Dynamic | HID Control Point |

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
```

### Report Map（标准 8 字节 Boot Keyboard）

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
- `hid_att_read(att_handle, offset, buffer, buffer_size)` — 4 个 value handle 分发
- `hid_att_write(connection_handle, att_handle, buffer, buffer_size)` — CCC + Control Point 分发，使用 `multi_att_set_ccc_config(connection_handle, att_handle, cfg)` 写入带连接句柄的 CCC 表

### Read / Write callback 扩展

在 `rdx_ble_server_att_read_callback()` 的 switch 中：
- 新增 4 个 HID value handle case → 调用 `hid_att_read()` → break（落到统一 return）
- 新增 `HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE` case → `multi_att_get_ccc_config(connection_handle, handle)` → 返回 2 字节 CCC 值

在 `rdx_ble_server_att_write_callback()` 的 switch 中：
- 新增 HID Control Point + CCC case → 调用 `hid_att_write(connection_handle, ...)` → break（落到统一 `return 0`）

### GATT 注册

`app_ble_profile_set(hdl, rdx_profile_data)` 在 `rdx_ble_server_init()` 中注册，一次注册全部 RDX + HID attribute。Read / Write callback 通过 `app_ble_att_read_callback_register` / `app_ble_att_write_callback_register` 绑定到同一个 `app_ble` handle。

### PC 发现验证

1. 设备进入 HOGP 模式（短按 IO NUM0）；
2. PC 蓝牙设置里应看到名为 **VibeKeyboard** 的键盘图标设备；
3. 用 nRF Connect / LightBlue 扫描，广播包应包含 0x1812 和 Appearance 0x03C1。

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
| 非 HOGP | IO NUM0 短按 | 进入 HOGP 模式 | — | 切换广播为 HID |
| HOGP | IO NUM0 长按 | 退出 HOGP 模式 | — | 恢复 RDX 广播，断开连接 |
| HOGP | IO NUM1 短按 | 发送字母 A | 0x04 | down + 20ms 后自动 up |
| HOGP | IO NUM2 短按 | 发送字母 B | 0x05 | 同上 |
| HOGP | IO NUM3 短按 | 发送字母 C | 0x06 | 同上 |
| HOGP | IO NUM4 短按 | 发送字母 D | 0x07 | 同上 |

### 实际按键拦截代码（rdx_app.c 行 ~609–634）

```c
if (hogp_mode_get()) {
    if (num_idx == 0) {
        if (index == KEY_ACTION_LONG) {
            hogp_mode_set(0);
            y_printf("[HOGP] exit HOGP mode\r");
        }
    } else {
        if (index == KEY_ACTION_CLICK) {
            hogp_key_click_send(num_idx - 1);   // NUM1=A, NUM2=B, NUM3=C, NUM4=D
        }
    }
    *value = APP_MSG_NULL;
    return;
} else {
    if (num_idx == 0 && index == KEY_ACTION_CLICK) {
        hogp_mode_set(1);
        y_printf("[HOGP] enter HOGP mode\r");
        *value = APP_MSG_NULL;
        return;
    }
}
```

### HOGP 状态变量（rdx_ble_server.c）

```c
static volatile u8 hogp_mode = 0;          // 1: HOGP 键盘模式
static volatile u8 hogp_connected = 0;     // 1: PC 已连接且当前处于 HOGP 模式
static volatile u8 hid_notify_enabled = 0; // Input Report CCC 已使能
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

    y_printf("[HOGP] key_send idx=%d pressed=%d report[2]=0x%02x conn=%d notify=%d\r",
             key_index, pressed, report[2], hogp_connected, hid_notify_enabled);

    // 三层检查，逐层 log
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

    int ret = app_ble_att_send_data(g_rdx_ble_server_info.rdx_ble_server_hdl,
                                    HID_INPUT_REPORT_VALUE_HANDLE,
                                    report, sizeof(report),
                                    ATT_OP_AUTO_READ_CCC);
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
}

// HCI_EVENT_DISCONNECTION_COMPLETE:
if (hogp_mode) {
    hogp_connected = 0;
    hid_con_handle = 0;
    hid_notify_enabled = 0;
    y_printf("[HOGP] disconnect\r");
}
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
| Complete Local Name | "VibeKeyboard" | |

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
2. **短按 IO NUM0** → 进入 HOGP 模式
3. PC 蓝牙扫描 → 发现 "VibeKeyboard" 键盘 → 连接
4. 打开记事本
5. **短按 IO NUM1~4** → 记事本出现 A / B / C / D
6. **长按 IO NUM0** → 退出 HOGP 模式，恢复 RDX 广播

### 正常流程串口日志序列

```
[HOGP] enter HOGP mode
[HOGP] active ble conn, disconnect before mode switch   ← 如果切换时已连接
[HOGP] HID advertising started
       ← PC 连接 →
[HOGP] conn complete hdl=0x0001
[HOGP] read hdl=0x001e offset=0 len=45   ← PC 读 Report Map
[HOGP] read hdl=0x0020 offset=0 len=4    ← PC 读 HID Information
[HOGP] CCC write hdl=0x001b cfg=0x0001 notify=1   ← PC 使能 notify
       ← 短按 IO NUM1 →
[HOGP] key_send idx=0 pressed=1 report[2]=0x04 conn=1 notify=1
[HOGP] key_send ret=0
[HOGP] key_send idx=0 pressed=0 report[2]=0x00 conn=1 notify=1
[HOGP] key_send ret=0
```

### 日志诊断表

| 现象 | 关键 log | 诊断 |
|------|----------|------|
| 按键无反应（无 HOGP log） | 无 `[HOGP]` 前缀 | key->value 不在 IO_NUM0~4，或 key_event_deal 未走到 rdx_app_earphone_key_remap |
| enter HOGP mode 有，HID advertising started 无 | 缺少第二行 | `rdx_ble_server_hdl == NULL`，rdx_ble_server_init 未完成或调用顺序错 |
| PC 搜不到 VibeKeyboard | `HID advertising started` 已打印 | 用 nRF Connect 手机 App 扫描，确认广播包是否包含 0x1812；如无，检查 app_ble_adv_data_set 返回值 |
| PC 连上但无 CCC write log | 缺少 `CCC write` | PC 蓝牙驱动未完成 HID 枚举；删除 PC 上已配对设备重连；检查是否有 ATT 层错误 |
| CCC write cfg=0x0000 | `cfg=0x0000` | PC 未使能通知；删除配对重连；尝试换一台 PC |
| key_send skipped: not connected | `skipped: not connected` | HCI 连接事件未触发或 hogp_mode 在连接前被清；检查 conn complete log 是否存在 |
| key_send skipped: notify not enabled | `skipped: notify not enabled` | PC 连上了但未写 CCC；检查 CCC write log 是否出现 |
| key_send ret ≠ 0 | `ret=X` (非 0) | 查看 `app_ble_att_send_data` 文档：-1=参数错，-2=CCC 未使能，其他=栈内部错误 |
| 模式切换后按键发不出 | `active ble conn, disconnect before mode switch` | 正常：切换前有活跃连接，已断开。对端需重连 |
| PC 连接后按键无响应（无 key_send log） | 有 CCC write 但无 key_send | 检查 NUM1~4 的 KEY_ACTION_CLICK 是否被识别；确认 num_idx-1 映射正确 |

### 已知风险

**Output Report 缺失：** 当前 Input Report characteristic 的 properties 包含 Write（0x1a），但 `hid_att_write` 的 default case 吞掉了对该 handle 的写入。Windows 连接 HID 键盘后通常会写 Output Report 来设置 LED 状态（CapsLock/NumLock）。由于写操作返回 0（成功但忽略），大多数 Windows 版本可容忍。如果实测遇到"连接成功但按键无响应"，优先用 BLE 抓包确认 PC 是否在写 0x001a handle，再决定是否增加 Output Report characteristic。

---

## 6. 改动文件清单

| 文件 | 改动内容 |
|------|----------|
| `SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_server.c` | 主要改动文件：扩展 rdx_profile_data[]（追加 HID Service 0x0016–0x0022）、扩展 read/write callback、增加 HOGP 状态变量、HID read/write/CCC 辅助函数、hogp_mode_set/key_send/key_click_send API、hogp_fill_adv_data/hogp_adv_start/hogp_adv_stop 广播切换、HCI 事件中连接/断开跟踪 |
| `SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_server.h` | 新增 HOGP API 声明 |
| `SDK/apps/common/third_party_profile/rdx_protocol/rdx_app.c` | 在 `rdx_app_earphone_key_remap()` 的 IO NUM 键值分发处增加 HOGP 模式拦截 |

**未修改的文件：** Makefile、lib_btstack_config.c、multi_protocol_main.c、bt_key_msg_table.c（按键在 rdx_app.c 层拦截，未走 key table 方案）。

---

## 7. 预计时间

| 阶段 | 内容 | 状态 |
|------|------|------|
| Phase 1 | GATT 扩展 + callback | ✅ 已完成 |
| Phase 2 | 按键 → HID Report | ✅ 已完成 |
| Phase 3 | 广播切换 | ✅ 已完成 |
| 硬件测试 | 烧录验证 | ⏳ 待进行 |
