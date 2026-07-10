# HOGP 键盘移植最终评估报告（v3.0 最终版）

## 1. 最终结论

| 项目 | 结论 |
|------|------|
| 厂商说法 | **属实**：`fw-AC63_BT_SDK` 中确实存在 HOGP 实现和键盘 Demo |
| 当前生产工程 | **`t2620-firmware` 基于 AC701N / BR28 TWS 耳机 SDK**，原生没有 HOGP 键盘源码 |
| BLE 架构 | `app_ble_init()`（在 `btstack.a` 内部调用 `att_server_init()`）→ `app_ble_*` 包装层 → RDX 等协议注册自己的 handle |
| 现有 BLE 业务 | **RDX 框架已注册完整 GATT Server**：`SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_server.c` |
| 历史痕迹 | `objs/` 下残留 `rdx_protocol/hid/*.o`、`.d`，说明此前有人尝试在 RDX 框架内集成 HID，但源码已删除 |
| HOGP 移植可行性 | **技术可行**，但**必须侵入 RDX 框架**扩展 profile data 和 callback |
| 推荐做法 | **在 RDX 的 `rdx_profile_data[]` 末尾追加 HID Service（0x1812），并扩展 `rdx_ble_server_att_read_callback` / `rdx_ble_server_att_write_callback`** |
| 预计工期 | **约 4–5 周**（不含 HFP 并发验证）；含 HFP 并发验证约 **6–7 周** |

---

## 2. 最终架构确认

### 2.1 实际 BLE 架构

```text
multi_protocol_profile_init()
    └── app_ble_init()              // btstack.a 内部，调用一次 att_server_init()
        └── 创建底层 GATT Server 数据库

rdx_ble_server_init()
    └── app_ble_hdl_alloc()         // 分配 wrapper 层 handle
    └── app_ble_profile_set(hdl, rdx_profile_data[])
    └── app_ble_att_read_callback_register(hdl, rdx_ble_server_att_read_callback)
    └── app_ble_att_write_callback_register(hdl, rdx_ble_server_att_write_callback)
    └── app_ble_att_server_packet_handler_register(hdl, rdx_ble_server_cbk_packet_handler)
    └── app_ble_adv_enable(hdl, 1)  // 开 RDX 广播
```

### 2.2 `rdx_profile_data[]` 当前布局

| Handle 范围 | 内容 |
|-------------|------|
| `0x0001–0x0003` | GAP Service（0x1800）：Device Name（0x2A00） |
| `0x0004–0x000b` | 自定义 128-bit UUID Service（RDX 协议通道：Write + Notify + Read） |
| `0x000c–0x000f` | Battery Service（0x180F） |
| `0x0010–0x0015` | 另一个自定义 128-bit UUID Service（Notify 通道） |
| `0x0016–0x0024` | **建议追加：HID Service（0x1812）** |

### 2.3 为什么不能用“独立 app_ble handle”方案

虽然 `app_ble_hdl_alloc()` 支持分配多个 wrapper 层 handle，但底层数据库由 `app_ble_init()` 内部一次性通过 `att_server_init()` 注册。所有 handle 共享同一份 ATT 数据库，`app_ble_profile_set()` 是把各 handle 的 attribute 注册到这份共享数据库中，并通过 `app_ble_att_handle_enable()` 按 handle 范围启用/禁用。

因此：

- 新增 HID Service 不能绕过 RDX 的 profile data；
- 独立 handle 方案最终仍需要协调 handle 范围，本质与扩展 RDX profile 相同；
- 直接扩展 RDX profile 能复用 RDX 的初始化、广播、连接管理，是最小阻力路径。

---

## 3. 修正后的最小实现路径

| 步骤 | 正确做法 |
|------|----------|
| **GATT Profile** | 在 `rdx_profile_data[]` 末尾（`0x0015` 之后）追加 HID Service attributes，handle 从 `0x0016` 起 |
| **Read Callback** | 在 `rdx_ble_server_att_read_callback()` 的 switch 中增加 HID handle case：Protocol Mode（0x2A4E）、Report Map（0x2A4B）、HID Information（0x2A4A）、Input Report（0x2A4D） |
| **Write Callback** | 在 `rdx_ble_server_att_write_callback()` 中增加 CCC 配置 case 和 Control Point case |
| **发送 Report** | 使用 `app_ble_att_send_data(hdl, HID_INPUT_REPORT_VALUE_HANDLE, report, 8, ATT_OP_AUTO_READ_CCC)` |
| **连接/断开** | 在 `rdx_ble_server_cbk_packet_handler()` 中捕获 `ATT_EVENT_CONNECTED` / `ATT_EVENT_DISCONNECTED`，记录 `hid_con_handle` |
| **按键事件** | 在 RDX 按键映射表（或 `bt_key_msg_table.c`）中增加 HOGP 模式分支，调用 HID Report 发送 |
| **广播切换** | 使用 `app_ble_adv_enable(hdl, 0/1)` 控制；HOGP 模式广播里放 `0x1812` + Appearance `0x03C1`，RDX 模式恢复原有广播数据 |
| **`config_le_gatt_server_num`** | 保持 `1`，无需修改 |

---

## 4. 与先前评估的差异

| 版本 | 主要错误/偏差 | 最终修正 |
|------|---------------|----------|
| v1.0–v1.2 | 认为需要移植 `le_gatt_common.c` / `le_gatt_server.c` | 实际使用 `app_ble_*` 包装层，无需移植 |
| v1.3.x | 认为业务代码直接调用 `att_server_init()` | `att_server_init()` 在 `btstack.a` 内部由 `app_ble_init()` 调用 |
| v1.3.2 | 认为 `bt_ble.c` 没有 GATT Server，可以“新建”独立 HID Server | 忽略了 RDX 已注册的 GATT Server；直接新建会破坏 RDX |
| v2.0 | 建议新增独立 `app_ble` handle，不修改 RDX | 独立 handle 最终仍需共享底层数据库；最小阻力路径是扩展 RDX profile |
| 所有版本 | 工期低估 | 由于必须侵入 RDX 框架并重建 HID callback，工期应为 **4–5 周** |

---

## 5. 最终风险评估

| 风险 | 等级 | 说明 |
|------|------|------|
| **RDX 框架侵入** | 🟡 中 | 必须修改 `rdx_profile_data[]` 和两个 callback。好在这只是 switch/case 扩展，且历史 `.d` 文件证明 RDX 框架下做过 HID 集成 |
| **Handle 冲突** | 🟢 低 | HID Service 从 `0x0016` 开始，与现有 `0x0001–0x0015` 不冲突 |
| **广播 UUID 冲突** | 🟡 中 | HOGP 模式广播 `0x1812` 后，手机 App 可能扫不到设备；必须实现 HOGP ↔ RDX 广播切换 |
| **连接数限制** | 🟡 中 | `config_le_hci_connection_num = 1`。若键盘模式仍需手机 App 同时连接，需改为 `2`；互斥模型下保持 `1` |
| **HID 源码丢失** | 🟢 低 | 源码已删除，但 AC63 `ble_hogp_profile.h` 提供完整 HID attribute 定义，可重建 |
| **PC 兼容性** | 🟡 中 | Windows 对 HID Report Map、Appearance、配对流程敏感 |
| **HFP + HOGP 并发** | 🟡 中 | 若键盘模式仍需 HFP 通话，需验证调度 |
| **低功耗策略** | 🟡 中 | 键盘模式可能需调整休眠策略 |
| **BLE 无线电冲突** | 🟢 低 | 互斥模型下不存在 |

---

## 6. 最终工作量与工期

### 6.1 工作量拆分

| 任务 | 预估 | 说明 |
|------|------|------|
| 理解 `app_ble_*` 框架与 RDX 初始化流程 | 2–3 天 | 比直接 `att_server_init()` 多一层抽象 |
| 重建 HID profile data | 3–4 天 | 基于 AC63 `ble_hogp_profile.h` 提取，合并到 `rdx_profile_data[]` |
| 扩展 RDX read/write callback | 2–3 天 | 增加 HID handle 的 switch case |
| HID Report 发送 + 按键接入 | 3–5 天 | 使用 `app_ble_att_send_data()` + 在 RDX 按键链路加 HOGP 分支 |
| 广播模式切换 | 2–3 天 | HOGP ↔ RDX App 广播切换 |
| 验证与调通 | 3–5 天 | PC 发现、配对、发送按键测试 |

### 6.2 总体预估

| 场景 | 预估工期 | 风险 |
|------|----------|------|
| **AC701N + RDX 扩展 HID（5 键，不含 HFP 并发验证）** | **约 4–5 周** | 中 |
| **AC701N + RDX 扩展 HID（5 键，含 HFP 并发验证）** | **约 6–7 周** | 中高 |
| **AC701N + Classic HID（EDR）** | **约 4–6 周** | 中 |
| **换用 AC63 系列 + `apps/hid`** | **4–6 周** | 中低 |

> **注**：含 HFP 并发验证时，RX 扩展 HID 的工期会超过 Classic HID，因为 Classic HID 不占用 GATT Server 资源。若 HFP 并发不是必须，RX 扩展 HID 仍是最贴合生产工程的方案。

---

## 7. 最终推荐方案

### 7.1 首选：在 RDX 框架内扩展 HID Service

既然生产工程已深度绑定 RDX，且 RDX 已占用唯一的底层 GATT Server：

1. **扩展 `rdx_profile_data[]`**：在 `0x0015` 后追加 HID Service attributes，handle 从 `0x0016` 开始；
2. **扩展 RDX callback**：在 `rdx_ble_server_att_read_callback()` 和 `rdx_ble_server_att_write_callback()` 中增加 HID case；
3. **复用 RDX 发送 API**：用 `app_ble_att_send_data(hdl, HID_INPUT_REPORT_VALUE_HANDLE, report, 8, ATT_OP_AUTO_READ_CCC)` 发 Report；
4. **复用 RDX 事件回调**：在 `rdx_ble_server_cbk_packet_handler()` 中记录 HID 连接状态；
5. **广播互斥切换**：HOGP 模式改广播 UUID 为 `0x1812`，RDX 模式恢复原有 UUID。

### 7.2 次选：AC701N + Classic HID（EDR HID）

如果 RDX 框架侵入遇到不可接受的阻力，或 HFP 并发验证是硬需求：

- `btstack.a` 已内置 Classic HID 符号；
- 需补齐 8 个 `hid_*` 回调；
- 走 BR/EDR 链路，不占用 GATT Server；
- 功耗高于 BLE HOGP。

### 7.3 备选：换平台到 AC63

如果后续硬件可换，AC63 + `apps/hid` 仍是最成熟、零侵入 RDX 的路径。

---

## 8. 需要向厂商确认的问题（最终版）

1. `app_ble_init()` 内部是否只调用一次 `att_server_init()`？多个 `app_ble` handle 是否共享同一份底层 ATT 数据库？
2. 在 `rdx_profile_data[]` 末尾追加 HID Service 是否会影响 RDX 业务认证/兼容性测试？
3. `config_le_hci_connection_num = 1` 是否足够？若 HOGP 连接 PC 时仍需手机 App 连接，是否需要改为 `2`？
4. HFP 通话期间，BLE HOGP Report 发送是否能保证时序？
5. 8 个 Classic HID 回调是否有参考实现？
6. 同一份固件里，HOGP 模式与 RDX 模式切换时，PC/手机已配对回连行为是否有推荐做法？
7. 之前删除的 `rdx_protocol/hid/` 源码是否还有备份或参考版本？

---

## 9. 附录：关键文件路径

### AC63 SDK（源）

```text
apps/hid/modules/bt/ble_hogp.c
apps/hid/modules/bt/ble_hogp_profile.h
apps/hid/examples/keyboard/app_keyboard.c
apps/hid/examples/standard_keyboard/app_standard_keyboard.c
apps/hid/include/app_config.h
apps/common/include/standard_hid.h
```

### t2620-firmware / AC701N SDK（目标）

```text
t2620-firmware/project.jlproj
t2620-firmware/SDK/Makefile
SDK/apps/common/third_party_profile/multi_protocol_main.c
SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_server.c
SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_server.h
SDK/apps/common/third_party_profile/rdx_protocol/rdx_app.c
SDK/apps/earphone/log_config/lib_btstack_config.c
SDK/interface/btstack/third_party/common/app_ble_spp_api.h
SDK/interface/btstack/le/att.h
SDK/cpu/br28/liba/btstack.a
SDK/cpu/br28/objs/apps/common/third_party_profile/rdx_protocol/hid/*.o  // 历史编译产物
SDK/cpu/br28/objs/apps/common/third_party_profile/rdx_protocol/hid/*.d  // 历史依赖记录
```

---

## 10. 修订记录

| 日期 | 版本 | 修订内容 |
|------|------|----------|
| 2026-07-08 | v1.0 | 初版评估 |
| 2026-07-09 | v1.1–v1.3.2 | 逐步修正：Classic HID 符号、5 键简化、`att_server_init()` 直接调用等 |
| 2026-07-09 | v2.0 | 发现 RDX 已使用 `app_ble_*` 包装层；建议新增独立 app_ble handle |
| 2026-07-09 | **v3.0（最终版）** | 确认底层数据库共享，独立 handle 方案不可行；最终推荐在 RDX profile 内扩展 HID Service；更新风险、工期与文件路径 |

---

*报告生成时间：2026-07-09*  
*基于代码库：fw-AC63_BT_SDK / t2620-firmware*
