# Phase 6 C3 实施文档：模块边界和 Report API 收口

> **目标：** 在 C1 连接归属状态机稳定后，收窄 HOGP 与 RDX App / BLE Server 的模块边界，用完整 8 字节 Keyboard Report API 替代“物理键序号/单 usage”入口，并移除 `rdx_ble_server.h` 对 HOGP 头文件的传递包含。
> **适用分支：** `HOGP`
> **前置阶段：** Phase 6 C2、C1 已完成；C1 已通过编译、host 检查和核心功能验证
> **冻结契约：** Profile v1（HID Service `0x0016-0x0022`、70 字节 Report Map、8 字节 Input Report、无 Report ID 前缀）保持不变

---

## 1. 范围

本阶段修改范围：

- `SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_server.h`
- `SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_server.c`
- `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_keyboard.h`
- `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_keyboard.c`
- `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_config.h`
- `SDK/apps/common/third_party_profile/rdx_protocol/rdx_app.c`
- `tests/host/test_hogp_profile_contract.ps1`

不修改：

- `rdx_profile_data[]` 的 HID handle、属性顺序和字节值。
- `rdx_hogp_report_map[]`。
- `TCFG_RDX_HOGP_ENABLE` 默认值。
- 上电默认模式、广播名称、NUM0 调试入口是否存在的产品策略。NUM0 调试入口仍保留到 C5，但从 HOGP 模块迁到调用方。
- Protocol Mode 写入、HID Control Point suspend、加密降级清理、Input Report read 同步。这些属于 C4。

---

## 2. 当前 C1 后基线

当前源码仍有以下 C3 债务：

1. `rdx_ble_server.h` 末尾包含 `rdx_hogp_keyboard.h`，导致任何包含 BLE Server 头的调用方都被动获得 HOGP API 和 HOGP 配置依赖。
2. `rdx_hogp_keyboard.h` 直接包含 `rdx_hogp_config.h`，并用 `TCFG_RDX_HOGP_ENABLE` 门控部分声明，仍存在 include 顺序依赖。
3. `rdx_hogp_keyboard.c` 内部保留固定 5 键表 `key_to_hid_usage[5]`，使用 `RDX_HOGP_KEYMAP_A-E`。
4. `rdx_hogp_on_io_num_key()` 让 HOGP 模块直接理解 `KEY_ACTION_CLICK`、`KEY_ACTION_LONG` 和 NUM 键语义。
5. 旧兼容 wrapper `hogp_mode_get/set()`、`hogp_key_send()`、`hogp_key_click_send()` 仍存在；`rdx_ble_server.c` 也仍调用 `hogp_mode_get/set()`。
6. host 契约测试仍锁定 `rdx_hogp_key_send_usage()` 内部的 `u8 report[8]`，不适配新的完整 Report API。

C3 的目标是让 HOGP 模块只提供“键盘 Report 传输能力”，不再承载产品物理键和固定 keymap 知识。

---

## 3. 设计原则

1. **API 显式包含**：谁调用 HOGP API，谁显式包含 `rdx_hogp_keyboard.h`；`rdx_ble_server.h` 不再替别人传递包含。
2. **公共头不依赖配置头**：`rdx_hogp_keyboard.h` 不包含 `rdx_hogp_config.h`，声明不再受 `TCFG_RDX_HOGP_ENABLE` 门控；关闭态由 `.c` 内 stub 提供。
3. **完整 Report 是稳定出口**：上层传入 8 字节 Boot Keyboard Report 结构，HOGP 只校验链路状态并 notify。
4. **物理键语义留在调用方**：NUM0 调试模式切换、NUM1-4 临时 A-D 输入留在 `rdx_app.c` 的临时 adapter 中，C5 或 Key Action Executor 再删除/替换。
5. **C3 不抢 C4 行为**：C3 可以改变发送 API，但不改变 Protocol Mode、suspend、加密失败、Input Report read 同步等协议状态语义。

---

## 4. C3 后目标接口

`rdx_hogp_keyboard.h` 中保留生命周期、ATT、连接事件、广播和诊断接口，键盘输入部分收口为：

```c
#define RDX_HOGP_KEYBOARD_REPORT_LEN  8

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

约束：

- `sizeof(rdx_hogp_keyboard_report_t)` 必须为 8。
- `modifiers` 是 Boot Keyboard modifier bitmap。
- `reserved` 固定保留字节，上层应写 0。
- `usages[6]` 是 6KRO usage 数组。
- `rdx_hogp_keyboard_report_send()` 只发送完整 8 字节 payload，不添加 Report ID。
- `rdx_hogp_keyboard_release_all()` 发送全 0 Report。
- `rdx_hogp_keyboard_is_ready()` 在 C3 只判断 HOGP owner、物理连接、wrapper handle、CCC notify 和加密要求；C4 再增加 suspend 约束。

---

## 5. 实施步骤

### Step 1：移除 BLE Server 头的传递包含

文件：`rdx_ble_server.h`

删除末尾：

```c
/* HOGP (HID over GATT Profile) extension API — implemented in rdx_hogp_keyboard.c */
#include "rdx_hogp_keyboard.h"
```

保留 C1 的窄 wrapper：

```c
void rdx_ble_mode_request_hogp(u8 enable);
u8   rdx_ble_connection_owner_is_hogp(void);
```

要求：

- `rdx_ble_server_info_t` 仍不包含 mode/owner 字段。
- `rdx_ble_server.h` 不出现 `rdx_hogp_keyboard.h`。
- 因此 `rdx_app.c`、`rdx_ble_server.c` 等真实调用方必须显式 include `rdx_hogp_keyboard.h`。

### Step 2：收窄 `rdx_hogp_keyboard.h`

文件：`rdx_hogp_keyboard.h`

删除：

```c
#include "rdx_hogp_config.h"
```

删除键输入旧声明：

```c
int  rdx_hogp_key_send_usage(u8 usage, u8 pressed);
int  rdx_hogp_key_click_usage(u8 usage);
int  rdx_hogp_key_click_index(u8 key_index);
int  rdx_hogp_on_io_num_key(u8 num_idx, u8 action);
```

删除旧兼容 wrapper 声明：

```c
void hogp_mode_set(u8 enable);
u8   hogp_mode_get(void);
void hogp_key_send(u8 key_index, u8 pressed);
void hogp_key_click_send(u8 key_index);
```

新增完整 Report API：

```c
#define RDX_HOGP_KEYBOARD_REPORT_LEN  8

typedef struct {
    u8 modifiers;
    u8 reserved;
    u8 usages[6];
} rdx_hogp_keyboard_report_t;

int rdx_hogp_keyboard_report_send(
    const rdx_hogp_keyboard_report_t *report);
int rdx_hogp_keyboard_release_all(void);
u8  rdx_hogp_keyboard_is_ready(void);
```

建议在头文件中保留 `system/includes.h`，因为该 SDK 中 `u8/u16` 类型来源不完全一致；不要在本阶段做无关 include 极限精简。

### Step 3：BLE Server 改用正式 HOGP 名称

文件：`rdx_ble_server.c`

当前 C1 helper 仍调用旧 wrapper：

```c
hogp_mode_set(1);
if (hogp_mode_get()) {
    ...
}
```

改为正式函数：

```c
rdx_hogp_mode_set(1);
if (rdx_hogp_mode_get()) {
    ...
}
```

涉及位置：

- `rdx_ble_mode_start_hogp_advertising()`
- `rdx_ble_mode_restart_hogp_advertising()`
- `rdx_ble_mode_sync_hogp_runtime()`

要求：

- `rdx_ble_server.c` 顶部继续显式 include `rdx_hogp_keyboard.h`。
- C1 的 owner 状态机逻辑不改。
- C1 host 检查中对 `hogp_mode_get/set` 的正则要同步更新为 `rdx_hogp_mode_get/set`。

### Step 4：HOGP 发送实现改为完整 Report

文件：`rdx_hogp_keyboard.c`

确认 include 顺序：

```c
#include "sdk_config.h"
#include "app_config.h"
#include "rdx_hogp_keyboard.h"
#include "rdx_hogp_profile.h"
#include "rdx_hogp_config.h"
```

`rdx_hogp_config.h` 必须由 `rdx_hogp_keyboard.c` 自己显式包含，且位于 `app_config.h` 之后，保证 `t2620_project_config.h` 中的 `TCFG_RDX_HOGP_ENABLE` 已经生效。移除公共头的传递包含后，不得依赖 `rdx_hogp_keyboard.h` 间接带入配置宏。

保留 `s_hid_input_report` 作为 ATT read 缓冲，但类型改为完整 Report：

```c
static rdx_hogp_keyboard_report_t s_hid_input_report = {0};
```

ATT read 继续返回 8 字节：

```c
case HID_INPUT_REPORT_VALUE_HANDLE:
    return hid_read_helper((const u8 *)&s_hid_input_report,
                           RDX_HOGP_KEYBOARD_REPORT_LEN,
                           offset, buffer, buffer_size);
```

新增 ready helper：

```c
u8 rdx_hogp_keyboard_is_ready(void)
{
    if (!s_hogp_connected) {
        return 0;
    }
    if (s_hogp_app_ble_hdl == NULL) {
        return 0;
    }
    if (!s_hid_notify_enabled) {
        return 0;
    }
#if RDX_HOGP_ENCRYPTION_REQUIRED
    if (!s_hogp_encrypted) {
        return 0;
    }
#endif
    if (!rdx_ble_connection_owner_is_hogp()) {
        return 0;
    }
    return 1;
}
```

新增发送 API：

```c
int rdx_hogp_keyboard_report_send(
    const rdx_hogp_keyboard_report_t *report)
{
    u8 payload[RDX_HOGP_KEYBOARD_REPORT_LEN];

    if (report == NULL) {
        return -1;
    }

    if (!rdx_hogp_keyboard_is_ready()) {
        RDX_HOGP_ERROR("report_send skipped: not ready");
        rdx_hogp_dump_state();
        return -1;
    }

    memcpy(payload, report, sizeof(payload));

    RDX_HOGP_LOG("report_send %02x %02x %02x %02x %02x %02x %02x %02x",
                 payload[0], payload[1], payload[2], payload[3],
                 payload[4], payload[5], payload[6], payload[7]);

    int ret = app_ble_att_send_data(s_hogp_app_ble_hdl,
                                    HID_INPUT_REPORT_VALUE_HANDLE,
                                    payload, sizeof(payload),
                                    ATT_OP_NOTIFY);
    if (ret != APP_BLE_NO_ERROR) {
        RDX_HOGP_ERROR("report_send failed ret=%d", ret);
        rdx_hogp_dump_state();
    }

    return ret;
}
```

新增 release API：

```c
int rdx_hogp_keyboard_release_all(void)
{
    rdx_hogp_keyboard_report_t report = {0};
    return rdx_hogp_keyboard_report_send(&report);
}
```

C3 注意点：

- 本阶段不要求发送成功后同步 `s_hid_input_report`；保持当前 ATT read 旧行为，C4 再修复。
- `payload` 必须是 8 字节，notify 仍使用 `sizeof(payload)`。
- 不新增 Report ID。

### Step 5：删除 HOGP 内部固定 keymap 和旧入口

文件：`rdx_hogp_keyboard.c`

删除：

```c
static const u8 key_to_hid_usage[5] = { ... };
int rdx_hogp_key_send_usage(...);
int rdx_hogp_key_click_usage(...);
int rdx_hogp_key_click_index(...);
int rdx_hogp_on_io_num_key(...);
void hogp_mode_set(...);
u8   hogp_mode_get(...);
void hogp_key_send(...);
void hogp_key_click_send(...);
```

关闭态 stub 中同步删除旧函数 stub，并增加新 API stub：

```c
int rdx_hogp_keyboard_report_send(
    const rdx_hogp_keyboard_report_t *report)
{
    (void)report;
    return -1;
}

int rdx_hogp_keyboard_release_all(void)
{
    return -1;
}

u8 rdx_hogp_keyboard_is_ready(void)
{
    return 0;
}
```

关闭态分支必须覆盖 `rdx_hogp_keyboard.h` 的全部公共声明，而不仅是 C3 新增 API。实施时逐项确认以下函数在 `#else /* !(TCFG_RDX_HOGP_ENABLE && ...) */` 分支均有 stub：

- `rdx_hogp_init()`、`rdx_hogp_deinit()`、`rdx_hogp_runtime_cleanup()`
- `rdx_hogp_mode_get()`、`rdx_hogp_mode_set()`
- `rdx_hogp_is_handle()`
- `rdx_hogp_att_read()`、`rdx_hogp_att_write()`
- `rdx_hogp_on_connected()`、`rdx_hogp_on_disconnected()`、`rdx_hogp_on_encryption_change()`、`rdx_hogp_on_sm_event()`
- `rdx_hogp_fill_adv_data()`、`rdx_hogp_adv_start()`、`rdx_hogp_adv_stop()`
- `rdx_hogp_dump_state()`
- `rdx_hogp_keyboard_report_send()`、`rdx_hogp_keyboard_release_all()`、`rdx_hogp_keyboard_is_ready()`

如果实施时发现 `hogp_key_up_timeout()` 仍调用旧 `rdx_hogp_key_send_usage()`，不要保留旧函数；应改为调用 `rdx_hogp_keyboard_release_all()`，或按 Step 6 把临时 click timer 挪到 `rdx_app.c`。

### Step 6：把 NUM 调试入口迁到 `rdx_app.c`

文件：`rdx_app.c`

显式 include：

```c
#include "rdx_hogp_config.h"
#include "rdx_hogp_keyboard.h"
```

在 `rdx_app_earphone_key_remap()` 附近增加一个本文件内临时 adapter。该 adapter 只用于 C3-C4 调试，C5 产品化时删除或替换为 Key Action Executor。

```c
#if TCFG_RDX_HOGP_ENABLE

#define RDX_APP_HOGP_DEBUG_KEY_UP_DELAY_MS  20

static u16 s_rdx_app_hogp_release_timer = 0;

static const u8 s_rdx_app_hogp_debug_usages[] = {
    0x04,  /* A */
    0x05,  /* B */
    0x06,  /* C */
    0x07,  /* D */
};

static void rdx_app_hogp_cancel_release_timer(void)
{
    if (s_rdx_app_hogp_release_timer) {
        sys_timeout_del(s_rdx_app_hogp_release_timer);
        s_rdx_app_hogp_release_timer = 0;
    }
}

static void rdx_app_hogp_release_timer_cb(void *priv)
{
    (void)priv;
    s_rdx_app_hogp_release_timer = 0;
    rdx_hogp_keyboard_release_all();
}

static int rdx_app_hogp_debug_click_usage(u8 usage)
{
    rdx_hogp_keyboard_report_t report = {0};
    int ret;

    report.usages[0] = usage;
    ret = rdx_hogp_keyboard_report_send(&report);
    if (ret == APP_BLE_NO_ERROR) {
        rdx_app_hogp_cancel_release_timer();
        s_rdx_app_hogp_release_timer =
            sys_timeout_add(NULL,
                            rdx_app_hogp_release_timer_cb,
                            RDX_APP_HOGP_DEBUG_KEY_UP_DELAY_MS);
    }

    return ret;
}

static int rdx_app_hogp_debug_num_key(u8 num_idx, u8 action)
{
#if RDX_BLE_DEBUG_MODE_SWITCH_KEY
    if (num_idx == 0 && action == KEY_ACTION_CLICK) {
        rdx_ble_mode_request_hogp(1);
        return 0;
    }

    if (num_idx == 0 && action == KEY_ACTION_LONG) {
        rdx_app_hogp_cancel_release_timer();
        rdx_ble_mode_request_hogp(0);
        return 0;
    }
#endif

    if (action != KEY_ACTION_CLICK) {
        return -1;
    }

    if (num_idx == 0 || num_idx > ARRAY_SIZE(s_rdx_app_hogp_debug_usages)) {
        return -1;
    }

    return rdx_app_hogp_debug_click_usage(
        s_rdx_app_hogp_debug_usages[num_idx - 1]);
}

#endif /* TCFG_RDX_HOGP_ENABLE */
```

然后替换原调用：

```c
#if TCFG_RDX_HOGP_ENABLE
        if (rdx_app_hogp_debug_num_key(num_idx, index) == 0) {
            *value = APP_MSG_NULL;
            return;
        }
#endif
```

要求：

- `rdx_app.c` 可以继续理解 `KEY_IO_NUM*` 和 `KEY_ACTION_*`，因为它是产品输入分发层。
- HOGP 模块不再出现 `KEY_IO_NUM`、`KEY_ACTION_`、固定 A-D/E keymap。
- NUM0 调试入口行为不回归：短按请求 HOGP，长按请求 CONFIG。
- NUM1-4 的 A-D 临时输入行为不回归。

### Step 7：清理 HOGP 配置项

文件：`rdx_hogp_config.h`

删除或迁出固定 keymap 配置：

```c
RDX_HOGP_KEYMAP_A
RDX_HOGP_KEYMAP_B
RDX_HOGP_KEYMAP_C
RDX_HOGP_KEYMAP_D
RDX_HOGP_KEYMAP_E
```

建议保留：

- `TCFG_RDX_HOGP_ENABLE`
- `RDX_HOGP_ENCRYPTION_REQUIRED`
- `RDX_HOGP_PAIRING_MODE`
- `RDX_HOGP_APPEARANCE`
- `RDX_HOGP_NAME_SOURCE`
- `RDX_HOGP_CUSTOM_NAME`
- `RDX_BLE_DEBUG_MODE_SWITCH_KEY`
- HOGP 日志宏

如果 Step 6 已把 key-up delay 迁到 `rdx_app.c`，则删除 `RDX_HOGP_KEY_UP_DELAY_MS`，避免 HOGP 配置头继续承载输入策略。若为了降低风险决定暂时保留 HOGP 内部 release timer，则该宏可以保留到 Key Action Executor 阶段；但必须在文档和 commit message 中说明这是临时债务。

推荐 C3 一次性迁出 key-up delay，因为本阶段已经引入了 `rdx_app.c` 临时 adapter。

### Step 8：更新 host 契约测试

文件：`tests/host/test_hogp_profile_contract.ps1`

更新既有检查：

1. “Input Report payload” 检查不再查 `rdx_hogp_key_send_usage()`，改查 `rdx_hogp_keyboard_report_send()`。
2. C1 检查中涉及旧 wrapper 的正则改为正式函数：
   - `hogp_mode_get()` -> `rdx_hogp_mode_get()`
   - `hogp_mode_set()` -> `rdx_hogp_mode_set()`
3. 广播 API 黑名单/白名单同步加入 `rdx_hogp_mode_get/set`，避免误判。
4. 同时增加负向检查：`rdx_ble_server.c` 不得再出现旧 `hogp_mode_get()` / `hogp_mode_set()` 调用。正则必须区分 `rdx_hogp_mode_get/set`，避免因为子串匹配把正式函数误判为旧 wrapper。

新增 C3 静态检查：

- `rdx_ble_server.h` 不包含 `rdx_hogp_keyboard.h`。
- `rdx_hogp_keyboard.h` 不包含 `rdx_hogp_config.h`。
- `rdx_hogp_keyboard.c` 显式包含 `rdx_hogp_config.h`，且该 include 位于 `app_config.h` 之后。
- `rdx_hogp_keyboard.h` 中存在 `rdx_hogp_keyboard_report_t`，字段为 `modifiers/reserved/usages[6]`。
- `RDX_HOGP_KEYBOARD_REPORT_LEN` 为 8。
- `rdx_hogp_keyboard_report_send()` 的 notify 使用 8 字节 payload，且没有 Report ID 前缀。
- `rdx_hogp_keyboard_release_all()` 存在并发送全 0 report。
- `rdx_hogp_keyboard_is_ready()` 检查 owner、connected、handle、CCC 和加密开关。
- 关闭态分支为 `rdx_hogp_keyboard.h` 中全部公共声明提供 stub，不只覆盖新增 Report API。
- `rdx_hogp_keyboard.c` 不再出现：
  - `KEY_IO_NUM`
  - `KEY_ACTION_`
  - `key_to_hid_usage`
  - `RDX_HOGP_KEYMAP_`
  - `rdx_hogp_on_io_num_key`
  - `rdx_hogp_key_click_index`
  - `hogp_key_send`
  - `hogp_key_click_send`
- `rdx_ble_server.c` 不再调用 `hogp_mode_get/set`，只调用 `rdx_hogp_mode_get/set`。
- `rdx_app.c` 显式包含 `rdx_hogp_keyboard.h`，且 NUM 键分支调用 `rdx_app_hogp_debug_num_key()` 或等效 adapter，而不是 `rdx_hogp_on_io_num_key()`。

---

## 6. 验证

### 6.1 Host 测试

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File tests/host/run_host_tests.ps1
```

如果 runner 找不到 Windows PowerShell，可直接运行：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File tests/host/test_t2620_config_overlay.ps1
pwsh -NoProfile -ExecutionPolicy Bypass -File tests/host/test_hogp_profile_contract.ps1
```

预期：

- 原 Profile v1 契约检查继续通过。
- C1 owner 状态机检查继续通过。
- 新增 C3 边界检查通过。

### 6.2 固件构建

```bash
cd SDK
make clean
make
```

预期：`TCFG_RDX_HOGP_ENABLE=1` 编译通过。

### 6.3 关闭态构建

临时把 `SDK/apps/earphone/include/t2620_project_config.h` 中：

```c
#define TCFG_RDX_HOGP_ENABLE 1
```

改为：

```c
#define TCFG_RDX_HOGP_ENABLE 0
```

然后执行：

```bash
cd SDK
make clean
make
```

验证后恢复为 1。

预期：

- 关闭态编译通过。
- `rdx_hogp_keyboard.h` 的公共声明不依赖 `TCFG_RDX_HOGP_ENABLE`。
- `.c` 中 stub 提供关闭态链接符号。

### 6.4 上机验证

1. 上电默认仍为 C1 行为，不切换到 C5 产品默认 HOGP。
2. NUM0 短按进入 HOGP 广播，PC 可连接。
3. NUM1-4 短按分别发送 A-D，按下和释放均正常。
4. NUM0 长按退出 HOGP，PC 断连并恢复 CONFIG/RDX 广播。
5. HOGP 连接状态下，模拟完整 Report：
   - 单键 A：`modifiers=0`，`usages[0]=0x04`
   - Ctrl+C：`modifiers=0x01`，`usages[0]=0x06`
   两者均可通过 `rdx_hogp_keyboard_report_send()` 表达。
6. 模式切换中触发 release timer，不出现旧 handle 访问或卡键。

---

## 7. 风险与注意事项

- **局部 timer 迁移风险**：如果按 Step 6 把临时 release timer 放到 `rdx_app.c`，它是 C3-C4 临时 adapter，不是最终 Key Action Executor。C5 或后续 Key Action 阶段必须删除或替换。
- **C4 边界**：C3 不保证 ATT read 返回最近一次发送的 Report；`s_hid_input_report` 同步放到 C4。
- **组合键能力**：C3 只提供表达能力，是否把某个物理键映射成 Ctrl+C 仍由后续 Key Action Executor 决定。
- **旧测试正则**：host 测试必须同步更新，否则会因为找不到 `rdx_hogp_key_send_usage()` 或旧 `hogp_mode_get()` 误报失败。
- **include 顺序**：`rdx_hogp_config.h` 不再经 HOGP 公共头传递；需要配置宏的 `.c` 必须在包含 `app_config.h` 后显式包含它。
- **C1 状态机不动**：本阶段不得把 C1 的私有 mode controller 挪进公共头，也不得新增公开 owner enum。
- **Profile v1 不动**：不要借 C3 移动 Output Report 到 HID Service 内部；该债务留给 Profile v2 设计。

---

## 8. 审核清单

实施前人工审核确认：

- [ ] 同意 C3 把 NUM 调试入口迁到 `rdx_app.c`，HOGP 模块只保留 Report 传输能力。
- [ ] 同意删除 `RDX_HOGP_KEYMAP_A-E`，临时 A-D 测试映射改为 `rdx_app.c` 文件内常量。
- [ ] 同意 `RDX_HOGP_KEY_UP_DELAY_MS` 迁出 HOGP 配置头；如不同意，需明确该宏保留到 Key Action Executor 阶段。
- [ ] 同意 C3 不修 Input Report read 同步，避免和 C4 混在一个提交。
- [ ] 同意 host 测试从旧 `rdx_hogp_key_send_usage()` 迁移到新 `rdx_hogp_keyboard_report_send()`。

---

## 9. 提交信息

```text
refactor(hogp): narrow module boundary and add report API (Phase 6 C3)

- Remove the transitive rdx_hogp_keyboard.h include from rdx_ble_server.h;
  each caller now includes the HOGP header explicitly.
- Make rdx_hogp_keyboard.h independent from rdx_hogp_config.h and expose
  unconditional declarations backed by enabled/disabled stubs.
- Add rdx_hogp_keyboard_report_t and the 8-byte keyboard report APIs:
  report_send, release_all and is_ready.
- Keep Profile v1 unchanged: HID handles, Report Map and notify payload length
  remain frozen.
- Replace legacy hogp_mode_get/set calls in rdx_ble_server.c with
  rdx_hogp_mode_get/set.
- Move the temporary NUM-key HOGP debug mapping out of HOGP and into rdx_app.c,
  so HOGP no longer depends on KEY_IO_NUM, KEY_ACTION or fixed A-D/E keymaps.
- Remove old hogp_key_* compatibility wrappers and physical-key HOGP entry
  points.
- Update host contract tests for the new Report API and C3 boundary checks.

C3 intentionally does not change Protocol Mode handling, suspend handling,
encryption downgrade behavior or Input Report read synchronization; those are
left for Phase 6 C4.
```

---

## 10. 完成后状态

C3 完成后：

- BLE Server 公共头不再扩散 HOGP API。
- HOGP 公共头不再依赖配置 include 顺序。
- HOGP 模块内部不再知道产品物理键、NUM 键动作或固定 A-D/E keymap。
- 上层可以用同一个 API 发送单键、组合键和后续宏执行器生成的完整 8 字节 Keyboard Report。
- C4 可以在这个基础上处理协议状态：当前 Report 同步、Protocol Mode 写入、Control Point suspend、加密降级和 adv data 容量检查。
