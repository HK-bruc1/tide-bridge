# Phase 6 C4 实施文档：协议状态与 Profile 数据收口

> **目标：** 在 C3 模块边界收窄后，补齐 HOGP 协议状态一致性：当前 Input Report、Protocol Mode、HID Control Point suspend、加密状态、广播包容量检查，以及 Profile 定义与 ATT 字节表的一致性。
> **适用分支：** `HOGP`
> **前置阶段：** Phase 6 C2、C1、C3 已完成；C3 已提交 `1bcc088`
> **冻结契约：** Profile v1 外部布局保持不变：HID Service `0x0016-0x0022`、Report Map 70 字节、Input Report notify payload 8 字节且无 Report ID 前缀。

---

## 1. 范围

本阶段允许修改：

- `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_keyboard.c`
- `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_keyboard.h`（仅在确有必要暴露诊断或常量时）
- `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_profile.h`
- `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_profile.c`
- `SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_server.c`
- `tests/host/test_hogp_profile_contract.ps1`

不修改：

- HID handle 数值、属性顺序、Report Map 字节和 Input Report 8 字节 payload 格式。
- Output Report `0x0028-0x002a` 的位置。本阶段只记录它是 Profile v1 兼容债务，不移动进 HID Service。
- C1 的 BLE mode controller 字段和 owner 路由策略。
- C3 的 Report API 名称和语义。
- C5 产品身份内容：默认 HOGP、广播名称、NUM0 调试入口删除、产品组合键。

---

## 2. 当前 C3 后基线

当前代码存在以下 C4 债务：

1. `rdx_hogp_keyboard_report_send()` 使用局部 `payload[8]` 发送 notify，但发送成功后没有同步 `s_hid_input_report`，ATT read 看到的当前值可能是旧值。
2. `rdx_hogp_keyboard_release_all()` 发送全 0 Report，但只有 notify 成功时才应把当前值归零；失败时不能伪造主机已收到 release。
3. `rdx_hogp_on_encryption_change()` 只在成功加密时置 1，禁用、失败或降级事件不会清零 `s_hogp_encrypted`。
4. `rdx_hogp_att_write()` 对 `HID_PROTOCOL_MODE_VALUE_HANDLE` 没有处理，虽然 Profile 声明 Protocol Mode 可写。
5. `HID_CONTROL_POINT_VALUE_HANDLE` 当前只打印日志，没有维护 suspend/exit suspend 状态。
6. `rdx_hogp_keyboard_is_ready()` 还没有检查 Host suspend 状态。
7. `rdx_hogp_fill_adv_data()` 使用 `max_len - offset - 2` 这类无符号计算，存在 underflow 和越界风险。
8. `rdx_hogp_profile.h` 中的 handle 宏与 `rdx_ble_server.c` 中的 ATT 字节表仍可能分叉；host 测试冻结了结果，但数据源仍不够集中。

---

## 3. ATT 错误码决策

`SDK/interface/btstack/le/att.h` 只声明了 write callback 可以返回 ATT error，但本仓库没有导出 `ATT_ERROR_INVALID_HANDLE_VALUE`、`ATT_ERROR_VALUE_NOT_ALLOWED` 等宏。C4 不得直接使用未定义宏。

在 `rdx_hogp_keyboard.c` 内部定义本模块使用的 ATT 错误码，使用蓝牙 ATT 规范数值，并用本地命名避免污染公共头：

```c
#define RDX_HOGP_ATT_ERR_INVALID_OFFSET                 0x07
#define RDX_HOGP_ATT_ERR_INVALID_ATTRIBUTE_VALUE_LEN    0x0d
#define RDX_HOGP_ATT_ERR_VALUE_NOT_ALLOWED              0x13
```

使用约定：

- `offset != 0` 且当前特征不支持偏移写入：返回 `RDX_HOGP_ATT_ERR_INVALID_OFFSET`。
- 单字节特征写入长度不是 1：返回 `RDX_HOGP_ATT_ERR_INVALID_ATTRIBUTE_VALUE_LEN`。
- 写入值不在合法集合内：返回 `RDX_HOGP_ATT_ERR_VALUE_NOT_ALLOWED`。
- 保留旧状态不变，打印明确日志。

如果实施时发现 JL BTstack 对这些非 0 返回值有特殊限制，以实际构建和上机结果为准，但必须在实现注释和提交说明中记录使用的最终错误码。

---

## 4. 目标状态

C4 完成后，HOGP 模块内部应满足以下不变量：

- `s_hid_input_report` 表示最近一次成功发送到 ATT notify 的 8 字节 Keyboard Report。
- `rdx_hogp_keyboard_release_all()` 成功后 `s_hid_input_report` 为全 0。
- `rdx_hogp_att_read(... HID_INPUT_REPORT_VALUE_HANDLE ...)` 返回真实当前值。
- `s_hogp_encrypted` 对当前连接完整赋值：成功加密为 1，失败、禁用、非当前连接或断连清理后为 0。
- `s_hogp_suspended` 表示 Host HID Control Point 的 suspend 状态；suspend 时 `rdx_hogp_keyboard_is_ready()` 返回 0。
- Protocol Mode 合法值只有 `0` Boot Protocol 和 `1` Report Protocol；默认仍为 `1`。
- 广播包构造逐字段检查容量，任何情况下不发生无符号下溢、越界写或无效长度返回。
- Profile v1 的最终 ATT 字节仍与 Phase 5/C3 host 快照一致。

---

## 5. 实施步骤

### Step 1：新增协议状态字段和常量

文件：`rdx_hogp_keyboard.c`

在局部变量区补充：

```c
static volatile u8 s_hogp_suspended = 0;
```

保留现有：

```c
static u8 hid_protocol_mode = 1;  // Report Protocol
```

建议改名为：

```c
static u8 s_hid_protocol_mode = RDX_HOGP_PROTOCOL_MODE_REPORT;
```

并在文件局部定义：

```c
#define RDX_HOGP_PROTOCOL_MODE_BOOT      0
#define RDX_HOGP_PROTOCOL_MODE_REPORT    1

#define RDX_HOGP_CONTROL_POINT_SUSPEND       0
#define RDX_HOGP_CONTROL_POINT_EXIT_SUSPEND  1
```

如果改名会导致 diff 扩大，可以保留 `hid_protocol_mode` 名称，但必须把合法值常量化。

状态复位点：

- `rdx_hogp_init()`：`protocol_mode = RDX_HOGP_PROTOCOL_MODE_REPORT`、`s_hogp_suspended = 0`、当前 Report 清零。
- `hogp_runtime_cleanup()` / `hogp_module_cleanup()`：当前 Report 清零，连接、CCC、加密、suspend 清零。
- `rdx_hogp_on_connected()`：新连接默认 `protocol_mode = RDX_HOGP_PROTOCOL_MODE_REPORT`，`s_hogp_suspended = 0`。
- `rdx_hogp_on_disconnected()`：当前 Report、CCC、加密、suspend 清零。

### Step 2：封装当前 Report 操作

文件：`rdx_hogp_keyboard.c`

新增内部 helper：

```c
static void rdx_hogp_current_report_clear(void)
{
    memset((void *)&s_hid_input_report, 0, sizeof(s_hid_input_report));
}

static void rdx_hogp_current_report_set(const u8 *payload, u8 len)
{
    if (payload == NULL || len != RDX_HOGP_KEYBOARD_REPORT_LEN) {
        return;
    }
    memcpy((void *)&s_hid_input_report, payload, sizeof(s_hid_input_report));
}
```

替换现有直接 `memset((void *)&s_hid_input_report, 0, ...)` 调用，统一改用 `rdx_hogp_current_report_clear()`。

约束：

- 发送失败时不更新当前 Report。
- 清理路径可以直接清零当前 Report，因为连接或模式已经失效。

### Step 3：发送成功后同步 Input Report 当前值

文件：`rdx_hogp_keyboard.c`

在 `rdx_hogp_keyboard_report_send()` 中，`app_ble_att_send_data()` 返回成功后同步当前值：

```c
int ret = app_ble_att_send_data(...);
if (ret == APP_BLE_NO_ERROR) {
    rdx_hogp_current_report_set(payload, sizeof(payload));
} else {
    RDX_HOGP_ERROR("report_send failed ret=%d", ret);
    rdx_hogp_dump_state();
}
```

`rdx_hogp_keyboard_release_all()` 不需要特殊分支；它发送全 0 Report，成功后自然通过 `report_send()` 把当前值归零。

`rdx_hogp_keyboard_report_send()` 入口仍先执行 ready 检查。Step 5 加入 suspend 检查后，suspend 状态下不会进入 `app_ble_att_send_data()`，因此也不会意外更新当前 Report。

ATT read 继续：

```c
case HID_INPUT_REPORT_VALUE_HANDLE:
    return hid_read_helper((const u8 *)&s_hid_input_report,
                           RDX_HOGP_KEYBOARD_REPORT_LEN,
                           offset, buffer, buffer_size);
```

验收点：

- host 测试能确认 `report_send()` 成功分支写回 `s_hid_input_report`。
- 上机可用 nRF Connect 或等效工具读取 Input Report，确认 key down 后为非 0，release 后为全 0。

### Step 4：补齐 Protocol Mode 写入

文件：`rdx_hogp_keyboard.c`

在 `rdx_hogp_att_write()` 的 switch 中新增 `HID_PROTOCOL_MODE_VALUE_HANDLE`：

```c
case HID_PROTOCOL_MODE_VALUE_HANDLE:
    if (offset != 0) {
        return RDX_HOGP_ATT_ERR_INVALID_OFFSET;
    }
    if (buffer_size != 1) {
        return RDX_HOGP_ATT_ERR_INVALID_ATTRIBUTE_VALUE_LEN;
    }
    if (buffer[0] != RDX_HOGP_PROTOCOL_MODE_BOOT &&
        buffer[0] != RDX_HOGP_PROTOCOL_MODE_REPORT) {
        RDX_HOGP_ERROR("protocol mode rejected val=0x%02x", buffer[0]);
        return RDX_HOGP_ATT_ERR_VALUE_NOT_ALLOWED;
    }
    s_hid_protocol_mode = buffer[0];
    RDX_HOGP_LOG("protocol mode=%d", s_hid_protocol_mode);
    return 0;
```

如果保留旧变量名，则读回调同步改为：

```c
return hid_read_helper(&hid_protocol_mode, 1, offset, buffer, buffer_size);
```

或：

```c
return hid_read_helper(&s_hid_protocol_mode, 1, offset, buffer, buffer_size);
```

要求：

- 非法长度和值必须返回非 0 ATT 错误。
- 非法写入不改变旧值。
- 不改变 `rdx_profile_data[]` 中 Protocol Mode 的属性和值字节。

### Step 5：补齐 HID Control Point suspend/exit suspend

文件：`rdx_hogp_keyboard.c`

替换当前 `HID_CONTROL_POINT_VALUE_HANDLE` 只打印日志的逻辑：

```c
case HID_CONTROL_POINT_VALUE_HANDLE:
    if (offset != 0) {
        return RDX_HOGP_ATT_ERR_INVALID_OFFSET;
    }
    if (buffer_size != 1) {
        return RDX_HOGP_ATT_ERR_INVALID_ATTRIBUTE_VALUE_LEN;
    }
    if (buffer[0] == RDX_HOGP_CONTROL_POINT_SUSPEND) {
        s_hogp_suspended = 1;
        rdx_hogp_current_report_clear();
        RDX_HOGP_LOG("control point: suspend");
        rdx_hogp_dump_state();
        return 0;
    }
    if (buffer[0] == RDX_HOGP_CONTROL_POINT_EXIT_SUSPEND) {
        s_hogp_suspended = 0;
        RDX_HOGP_LOG("control point: exit suspend");
        rdx_hogp_dump_state();
        return 0;
    }
    RDX_HOGP_ERROR("control point rejected val=0x%02x", buffer[0]);
    return RDX_HOGP_ATT_ERR_VALUE_NOT_ALLOWED;
```

同步更新 `rdx_hogp_keyboard_is_ready()`：

```c
if (s_hogp_suspended) {
    return 0;
}
```

C4 不要求在收到 suspend 后主动发送 release notify。这里选择只清本地当前值并停止后续业务 Report，避免在 Host 已进入 suspend 后继续发 notify。该选择不保证清除 Host 侧已经订阅到的非 0 Input Report 状态；如果产品要求 host-visible key-up，应由后续 Key Action Executor 在进入 suspend 前统一发送 release。

### Step 6：完整赋值加密状态

文件：`rdx_hogp_keyboard.c`

替换 `rdx_hogp_on_encryption_change()` 中只置 1 的逻辑：

```c
void rdx_hogp_on_encryption_change(u16 con_handle, u8 enabled, u8 status)
{
    RDX_HOGP_LOG("encryption_change hdl=0x%04x enabled=%d status=%d",
                 con_handle, enabled, status);

    if (!s_hogp_mode || con_handle != s_hid_con_handle) {
        RDX_HOGP_ERROR("encryption_change ignored: stale handle");
        rdx_hogp_dump_state();
        return;
    }

    s_hogp_encrypted = (enabled && status == 0) ? 1 : 0;
    if (!s_hogp_encrypted) {
        rdx_hogp_current_report_clear();
        RDX_HOGP_ERROR("link encryption disabled or failed");
    }

    rdx_hogp_dump_state();
}
```

要求：

- 当前连接失败/禁用时清零 encrypted。
- 非当前连接事件不得把 encrypted 置 1。
- 加密失败后 `rdx_hogp_keyboard_is_ready()` 必须返回 0。

### Step 7：广播数据容量检查

文件：`rdx_hogp_keyboard.c`

为 `rdx_hogp_fill_adv_data()` 增加安全 append helper，避免每一段手写容量计算：

```c
static u8 rdx_hogp_adv_append_data(u8 *adv_data,
                                   u8 max_len,
                                   u8 *offset,
                                   u8 eir_type,
                                   const void *data,
                                   u8 data_len)
{
    if (adv_data == NULL || offset == NULL || data == NULL) {
        return 0;
    }

    if ((u16)(*offset) + 2 + data_len > max_len) {
        RDX_HOGP_ERROR("adv overflow type=0x%02x off=%d len=%d max=%d",
                       eir_type, *offset, data_len, max_len);
        return 0;
    }

    *offset += make_eir_packet_data(&adv_data[*offset], *offset,
                                    eir_type, (void *)data, data_len);
    return 1;
}
```

对单值字段可以用小 buffer 或单独 helper：

```c
static u8 rdx_hogp_adv_append_val(...);
```

`rdx_hogp_fill_adv_data()` 要求：

- `adv_data == NULL` 或 `max_len == 0` 时返回 0。
- Flags、HID Service UUID、Appearance 是关键字段，任一写不下则返回 0，不启动错误广播。
- Local Name 可以截断，但必须先确认至少还有 2 字节 EIR header 空间；没有空间则跳过名称并打印日志。上机回归时需要用超长名称验证一次无越界。
- 禁止出现 `max_len - offset - 2` 这种无符号下溢写法。

### Step 8：Profile 定义集中化

文件：`rdx_hogp_profile.h`、`rdx_ble_server.c`、`tests/host/test_hogp_profile_contract.ps1`

C4 不改变最终字节，但要减少 handle/UUID/report reference 的双事实源。推荐做法：

1. 在 `rdx_hogp_profile.h` 增加 UUID 和默认值常量：

```c
#define RDX_HOGP_UUID_HID_SERVICE              0x1812
#define RDX_HOGP_UUID_PROTOCOL_MODE            0x2A4E
#define RDX_HOGP_UUID_REPORT                   0x2A4D
#define RDX_HOGP_UUID_REPORT_MAP               0x2A4B
#define RDX_HOGP_UUID_HID_INFORMATION          0x2A4A
#define RDX_HOGP_UUID_HID_CONTROL_POINT        0x2A4C
#define RDX_HOGP_UUID_REPORT_REFERENCE         0x2908
#define RDX_HOGP_UUID_CLIENT_CONFIGURATION     0x2902

#define RDX_HOGP_PROTOCOL_MODE_DEFAULT         0x01
```

2. 在 `rdx_ble_server.c` 的 HID Service 字节表中使用上述常量或行宏展开，减少手写 UUID/report reference 数值。

   可以先定义只在 `.c` 内使用的字节宏：

```c
#define U16_LE_BYTES(v)  ((u8)((v) & 0xff)), ((u8)(((v) >> 8) & 0xff))
```

   然后将 HID Service 内的 UUID、handle、value_handle、Report Reference 值从宏展开。保持最终字节不变。

3. host 测试从 `rdx_hogp_profile.h` 解析这些常量，构造期望值，不再在测试脚本里复制一份 UUID/report reference 字面量。

4. 回退策略：外部字节冻结优先于本阶段的宏展开重构。如果宏展开导致现有 Profile 字节解析器需要大量改动，或者最终字节与 Phase 5/C3 快照出现任何波动，C4 先只把常量收口到 `rdx_hogp_profile.h`，`rdx_ble_server.c` 的 ATT 字节表可以暂时保留现有字面量，并由 host 字节快照继续冻结外部布局。等 C4 协议状态稳定后，再用一次纯重构提交把字面量替换为宏展开。

5. 测试可以检查源文件是否开始使用 `RDX_HOGP_UUID_*` / `HID_*_HANDLE` 宏，并继续用字节快照守护最终布局；如果采用回退策略，测试应记录“常量已定义但 server 字节表暂未完全宏展开”的状态，不阻塞 C4 协议状态修复。

接受标准：

- `rdx_profile_data[]` 的 HID Service 最终字节仍通过现有 `PROFILE_ATTRIBUTE_ORDER`。
- `RDX_HOGP_INPUT_REPORT_ID/TYPE` 和 `RDX_HOGP_OUTPUT_REPORT_ID/TYPE` 是 Report Reference 的唯一来源。
- `HID_OUTPUT_REPORT_VALUE_HANDLE` 仍只记录 `0x0029` value handle，不扩大 HID Service handle range。

### Step 9：扩展 host 契约测试

文件：`tests/host/test_hogp_profile_contract.ps1`

新增 C4 检查建议：

- `C4_CURRENT_REPORT_SYNC`：`rdx_hogp_keyboard_report_send()` 成功分支写回 `s_hid_input_report`。
- `C4_RELEASE_ZERO_REPORT`：`rdx_hogp_keyboard_release_all()` 仍构造全 0 report，并通过统一 send API 发送。
- `C4_READY_CHECKS_SUSPEND`：`rdx_hogp_keyboard_is_ready()` 检查 `s_hogp_suspended`。
- `C4_PROTOCOL_MODE_WRITE`：`rdx_hogp_att_write()` 处理 `HID_PROTOCOL_MODE_VALUE_HANDLE`，校验长度和值，合法值 0/1。
- `C4_CONTROL_POINT_SUSPEND`：`HID_CONTROL_POINT_VALUE_HANDLE` 写入 0/1 更新 `s_hogp_suspended`。
- `C4_ENCRYPTION_ASSIGNMENT`：`rdx_hogp_on_encryption_change()` 对当前连接完整赋值，失败/禁用清零。
- `C4_ADV_CAPACITY_CHECK`：`rdx_hogp_fill_adv_data()` 使用 append helper，禁止 `max_len - offset - 2`。
- `C4_NO_UNDEFINED_ATT_ERROR`：源码不出现 `ATT_ERROR_INVALID_HANDLE_VALUE` 等未定义宏。
- `C4_PROFILE_CONSTANTS`：`rdx_hogp_profile.h` 定义 HID UUID、Report Reference 和 Protocol Mode 默认值常量，server/test 使用这些常量。

保留已有检查：

- 13 个 HID handle 值。
- Report Map 70 字节快照。
- Input Report notify payload 8 字节且无 Report ID 前缀。
- HID Service 属性顺序和字节级值。
- C1/C3 边界检查。

---

## 6. 验证

### 6.1 Host 测试

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\host\run_host_tests.ps1
```

预期：

- 原有 T2620 overlay 测试通过。
- HOGP profile contract 全部通过。
- C4 新增静态检查全部通过。

### 6.2 固件构建

开启态：

```bash
cd SDK
make clean
make
```

关闭态：

临时把 `SDK/apps/earphone/include/t2620_project_config.h` 中 `TCFG_RDX_HOGP_ENABLE` 改为 0：

```bash
cd SDK
make clean
make
```

验证后恢复为 1，并确认：

```powershell
git diff -- SDK/apps/earphone/include/t2620_project_config.h
git status --short
```

生成二进制不得进入提交。

### 6.3 上机验证

基础路径：

```text
[ ] NUM0 进入 HOGP，PC 可连接
[ ] NUM1-4 A-D 输入正常
[ ] release 后无卡键
[ ] PC 主动断连后 HOGP 广播恢复
[ ] HOGP/CONFIG 模式切换不回归
```

协议状态：

```text
[ ] 通过 nRF Connect 或等效工具读 HID Input Report：key down 后当前值非 0，release 后为全 0
[ ] 写 Protocol Mode = 0 成功，读回为 0
[ ] 写 Protocol Mode = 1 成功，读回为 1
[ ] 写 Protocol Mode = 2，或用非法长度 0/2 写入，均被拒绝，旧值不变
[ ] 写 HID Control Point = 0 后进入 suspend，业务 Report 停止发送
[ ] 写 HID Control Point = 1 后退出 suspend，业务 Report 恢复
[ ] 加密失败、加密关闭或断连后禁止发送 Report
[ ] 广播包仍包含 HID Service UUID、Appearance Keyboard 和名称字段；名称过长时无越界
```

如果 Windows 工具无法直接写 Protocol Mode / Control Point，使用手机 BLE 调试 App 或临时测试脚本验证。

### 6.4 提交前检查

```powershell
git diff --check
git status --short
```

要求：

- 无空白错误。
- 无 `SDK/cpu/br28/tools/`、`output/` 等生成二进制改动。
- `TCFG_RDX_HOGP_ENABLE` 已恢复为 1。

---

## 7. 风险与注意事项

- **ATT error code 风险**：本 SDK 没有公开 `ATT_ERROR_*` 宏；C4 使用本地常量。若构建或上机发现非 0 返回没有按预期传播，需要记录实际行为，但不能静默吞掉非法写入。
- **suspend 行为边界**：C4 只停止业务 Report，不实现系统级低功耗策略。Host suspend 后是否降功耗由后续产品模式协调层处理。suspend 不保证清除 Host 侧已经订阅到的非 0 Input Report 状态；如需 host-visible key-up，由后续 Key Action Executor 在进入 suspend 前统一发送 release。
- **Input Report read 同步**：只在 notify 成功后更新当前值；发送失败时保持旧值，避免伪造主机已收到的状态。
- **Protocol Mode 支持范围**：合法接受 Boot/Report 两种值，但当前 Report Map 和 notify payload 仍是现有 8 字节 Boot Keyboard 格式，不新增 Report ID。
- **Profile 数据收口**：本阶段不移动 Output Report，不改变 HID Service range。若行宏改造造成 host parser 大改或最终字节波动，应优先保持外部字节冻结，把 server 字节表宏展开留给后续纯重构提交。
- **广播容量检查**：HID Service UUID 和 Appearance 是关键字段，写不下应失败返回 0；名称可以截断或省略。
- **C5 不提前**：不要在 C4 改默认模式、广播名称或删除 NUM0 调试入口。

---

## 8. 审核清单

实施前人工审核确认：

- [ ] 同意 C4 使用本地 ATT error 常量，不直接依赖仓库不存在的 `ATT_ERROR_INVALID_HANDLE_VALUE`。
- [ ] 同意 suspend 时只清本地当前 Report 并阻止后续业务 Report，不主动补发 release notify。
- [ ] 同意 Profile v1 不移动 Output Report，仍把它记录为 Profile v1 兼容债务。
- [ ] 同意 Profile 常量收口以最终字节不变为第一优先级，host 契约测试必须继续冻结外部布局。
- [ ] 同意 C4 不处理产品身份、默认 HOGP、广播名称迁移和测试入口删除。

---

## 9. 提交信息

```text
fix(hogp): Phase 6 C4 同步协议状态与 Profile 定义

- 同步 HOGP Input Report 当前值：notify 成功后更新 ATT read 缓冲，
  release 成功后归零，发送失败不伪造状态。
- 完整维护加密状态，当前连接加密失败/禁用后清零 encrypted 并阻止
  后续 Report 发送。
- 实现 Protocol Mode 0/1 写入和 HID Control Point suspend/exit suspend，
  非法长度或非法值返回本地 ATT error。
- rdx_hogp_keyboard_is_ready() 增加 suspend 检查。
- rdx_hogp_fill_adv_data() 改为逐字段容量检查，避免无符号下溢和越界。
- 将 HID UUID、Report Reference 和默认 Protocol Mode 常量收口到
  rdx_hogp_profile.h，并扩展 host 契约测试守护最终 ATT 字节。

不改变 Profile v1 外部契约：HID Service 0x0016-0x0022、70-byte Report
Map、8-byte Input Report payload 保持不变；Output Report 0x0028-0x002a
仍作为 Profile v1 兼容债务保留原位置。

验证：make clean && make；TCFG_RDX_HOGP_ENABLE=0 构建；run_host_tests.ps1；
git diff --check；上机协议状态回归。
```

---

## 10. 完成后状态

C4 完成后：

- C1-C4 的架构修复闭环：连接归属、关闭态、模块边界、协议状态都已收口。
- HOGP Profile v1 的外部字节契约仍冻结。
- HOGP 对上层提供稳定的完整 Keyboard Report 发送能力，并正确反映 ready/suspend/encryption 状态。
- C5 可以独立开展产品身份迁移：默认 HOGP、`VibeCoding Keyboard` 名称、配置模式入口和 NUM0 调试入口删除。
