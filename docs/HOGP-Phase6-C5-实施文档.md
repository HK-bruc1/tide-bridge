# Phase 6 C5 实施文档：默认 HOGP 与专属 Key Action 测试骨架

> **文档状态：** 评审修订版，待源码按本版同步后重新审核
> **前置阶段：** Phase 6 C1-C4 已完成；C4 已提交 `a25a116`
> **阶段定位：** 完成 T2620 默认 HOGP 产品路径，并把临时按键注入升级为可被 BLE App/VM 配置替换的 HOGP 专属 Key Action Executor

## 1. 已确认的产品决策

1. T2620 上电默认进入 HOGP 模式并广播，`TCFG_RDX_HOGP_ENABLE=0` 时自动回退 RDX BLE App Config 模式。
2. C5 不迁移产品名称。HOGP 继续使用与 RDX BLE App 相同的 Server local name，即保持 `RDX_HOGP_NAME_SOURCE=0`；后续客户需要独立名称时，再切换到 custom name 模式。
3. BLE App 下发键值和持久化尚未完成，测试路径不能删除。用一个项目级宏 `RDX_HOGP_KEY_ACTION_TEST_ENABLE` 统一门控内置测试 Keymap 和 KEY1 三击模式切换。
4. 五个物理键的单击都进入测试 Keymap：

| 物理键 | 单击动作 | HID Report | 覆盖能力 |
|---|---|---|---|
| KEY1 / `KEY_IO_NUM0` | `Ctrl+V` | modifier `0x01`，usage `0x19` | 修饰键组合 |
| KEY2 / `KEY_IO_NUM1` | `A` | modifier `0x00`，usage `0x04` | 普通字符键 |
| KEY3 / `KEY_IO_NUM2` | `Enter` | modifier `0x00`，usage `0x28` | 非字符功能键 |
| KEY4 / `KEY_IO_NUM3` | `Ctrl+C` | modifier `0x01`，usage `0x06` | 修饰键组合 |
| KEY5 / `KEY_IO_NUM4` | `Backspace` | modifier `0x00`，usage `0x2a` | 编辑功能键 |

5. KEY1 三击 `KEY_ACTION_TRIPLE_CLICK` 在 HOGP 与 Config 之间切换。三击只请求模式，不直接控制广播、连接或 HOGP 私有状态。
6. KEY1 的 `LONG/HOLD/UP` 不由 Key Action Executor 消费，完整保留给后续 HFP MIC：长按开始采集、保持期间维持、抬起停止采集。
7. 当前只实现单击触发的 Keyboard Report。双击、长按、宏、Layer、Consumer Control、BLE App 命令和 Flash 持久化不属于 C5。

## 2. 范围

### 2.1 允许修改

- `SDK/apps/earphone/include/t2620_project_config.h`
- `SDK/apps/common/third_party_profile/rdx_protocol/rdx_app_config.h`
- `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_config.h`
- `SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_server.c`
- `SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_server.h`
- `SDK/apps/common/third_party_profile/rdx_protocol/rdx_app.c`
- `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_key_action.c`（新增）
- `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_key_action.h`（新增）
- `SDK/Makefile`
- `tests/host/test_hogp_profile_contract.ps1`
- `docs/HOGP收尾实施方案.md`
- 本实施文档

### 2.2 明确不修改

- 不修改 HID handle、ATT 属性顺序、Report Map、HID Information、Report Reference 或 8 字节 Input Report 契约。
- 不修改 `rdx_hogp_keyboard.c/.h` 的传输职责；HOGP 不解释物理键和按键动作，也不访问 VM。
- 不修改 `rdx_key.c/.h`；它们继续只负责物理键、场景和 `APP_MSG_*` 映射，不包含 HID usage、HOGP action 类型或测试宏。
- 不新增 RDX BLE App 命令，不分配新的 `VM_RDX_*` ID，不冻结持久化二进制布局。
- 不实现 HFP MIC 行为，只保证 KEY1 的 `LONG/HOLD/UP` 不被消费。
- 不修改 Classic HFP、TWS、BT 音乐、录音、低功耗、电源或完整 LED/UI 策略。
- 不将 HOGP 名称强制改为 `VibeCoding Keyboard`，不将 Config 名称强制改为 `VibeCoding Config`。
- 不把生成二进制纳入提交。

## 3. 当前 C4 后基线

当前实现仍有以下 C5 债务：

1. `rdx_ble_mode_controller_init()` 把 requested/advertised mode 初始化为 Config，`rdx_ble_server_init()` 末尾直接调用 `rdx_ble_server_adv_enable(1)`，所以上电仍先走 RDX Config 广播。
2. `rdx_app.c` 内存在临时 `rdx_app_hogp_debug_*` adapter，只支持 NUM1-4 的 A-D，并由 NUM0 单击/长按进入和退出 HOGP。
3. release timer 和固定 usage 表仍归 `rdx_app.c`，不能作为后续 BLE App Keymap 的稳定执行边界。
4. `RDX_BLE_DEBUG_MODE_SWITCH_KEY` 的名称和语义已经过时，不能表达“内置测试 Keymap + KEY1 三击切换”的统一门控。
5. HOGP 当前已经默认 `RDX_HOGP_NAME_SOURCE=0`，会调用 `rdx_ble_server_get_local_name()`；C5 应保持该行为，不制造名称和 Windows 配对迁移。

## 4. 目标架构

```text
JL physical key framework
        |
        | key_id + KEY_ACTION_*
        v
rdx_app.c key routing
        |
        | only KEY_ACTION_CLICK
        v
rdx_hogp_key_action.c
  - active keymap in RAM
  - built-in five-key test keymap
  - build full 8-byte keyboard report
  - own key-up timer
        |
        v
rdx_hogp_keyboard_report_send()

KEY1 + KEY_ACTION_TRIPLE_CLICK
        |
        v
rdx_ble_mode_request_hogp(!current_requested_mode)
        |
        v
existing C1 BLE mode controller
```

职责约束：

- `rdx_app.c` 识别物理 NUM 键并转发事件，不保存 Keymap，不构造 HID Report。
- `rdx_key.c/.h` 保持现有职责，只负责物理键、场景和 `APP_MSG_*` 映射，不依赖 HOGP 类型或配置。
- `rdx_hogp_key_action.c/.h` 是 HOGP Keyboard action 的唯一所有者：保存内置测试 Keymap 和私有 active keymap，构造完整 Keyboard Report，并管理 release timer；不控制广播、连接、HFP 或 VM。
- `rdx_ble_server.c` 保持模式状态机唯一所有者，并提供窄模式查询/切换 API。
- `rdx_hogp_keyboard.c` 继续只负责 HOGP 协议状态、ATT、广播 payload 和 Report 发送。

## 5. 配置与数据契约

### 5.1 单一测试门控

在 `rdx_hogp_config.h` 中删除旧 `RDX_BLE_DEBUG_MODE_SWITCH_KEY`。在现有 `rdx_app_config.h` 的公共配置区增加关闭态默认值：

```c
#ifndef RDX_HOGP_KEY_ACTION_TEST_ENABLE
#define RDX_HOGP_KEY_ACTION_TEST_ENABLE        0
#endif
```

在 `t2620_project_config.h` 中显式开启：

```c
#ifndef RDX_HOGP_KEY_ACTION_TEST_ENABLE
#define RDX_HOGP_KEY_ACTION_TEST_ENABLE        1
#endif
```

该宏只控制：

- 内置五键测试 Keymap 是否成为 active keymap；
- KEY1 三击模式切换入口是否启用。

该宏不得门控 `rdx_hogp_key_action.c/.h` 的公共类型、函数定义或 `CLICK -> Executor` 正式路由。测试宏关闭后，执行器仍可由未来 BLE App/VM 配置驱动；C5 尚无正式配置源时，事件返回未处理并回落到原 RDX 按键表。

`rdx_hogp_key_action.h` 不传递包含配置头。`rdx_app.c` 和 `rdx_hogp_key_action.c` 必须先包含 `app_config.h`，再显式包含 `rdx_app_config.h`，确保 T2620 override 先于 fallback 生效。测试门控 fallback 保留在现有 `rdx_app_config.h`，不新增小型配置文件，也不放回 `rdx_hogp_config.h`。

`TCFG_RDX_HOGP_ENABLE=0` 时，即使误把 `RDX_HOGP_KEY_ACTION_TEST_ENABLE` 设为 1，也不得编译或执行 HOGP Report 注入和模式切换。HOGP 专属 Executor API 仍必须可链接并保持无副作用。

### 5.2 默认模式配置

默认模式属于 BLE Server/产品配置，不属于 HOGP 协议配置。建议在 `rdx_ble_server.h` 只公开稳定值：

```c
#define RDX_BLE_DEFAULT_MODE_CONFIG             0
#define RDX_BLE_DEFAULT_MODE_HOGP               1

```

`RDX_BLE_DEFAULT_MODE` 的 fallback 放在 `rdx_ble_server.c` 中，并且必须位于 `app_config.h` 之后：

```c
#ifndef RDX_BLE_DEFAULT_MODE
#define RDX_BLE_DEFAULT_MODE                    RDX_BLE_DEFAULT_MODE_CONFIG
#endif
```

公共 Server 头不提供该配置 fallback，避免其他 translation unit 在未先包含 `app_config.h` 时锁定错误默认值。

T2620 覆盖：

```c
#define RDX_BLE_DEFAULT_MODE                    RDX_BLE_DEFAULT_MODE_HOGP
```

必须增加编译期合法值检查。`TCFG_RDX_HOGP_ENABLE=0` 时，无论项目默认值是什么，内部 effective default mode 都必须为 Config。

### 5.3 HOGP 测试映射与 Active Keymap 最小结构

C5 在 `rdx_hogp_key_action.h` 中定义 HOGP Keyboard action 和运行时 Keymap：

```c
#define RDX_HOGP_KEY_ACTION_PHYSICAL_KEY_COUNT  5

typedef struct {
    u8 modifiers;
    u8 usages[6];
} rdx_hogp_key_action_keyboard_t;

typedef struct {
    u8 version;
    u8 key_count;
    rdx_hogp_key_action_keyboard_t keys[RDX_HOGP_KEY_ACTION_PHYSICAL_KEY_COUNT];
} rdx_hogp_key_action_keymap_t;
```

`rdx_hogp_key_action.c` 在测试宏开启时保存五个 `static const` 默认 action，并在 `init()` 中复制为私有 active keymap。`rdx_key.c/.h` 不包含上述类型，不保存 HOGP usage，也不提供 HOGP getter。

约束：

- 物理 key ID 使用稳定的逻辑索引 `0-4`，不把 `KEY_IO_NUM0` 等 SDK 枚举写进配置结构。
- C5 action 只表达标准 Keyboard Report 的 modifier 和最多 6 个 usage。
- `reserved` 字节始终由执行器构造为 0，不存入 Keymap。
- `version` 只用于 RAM 结构校验，不等同于未来 Flash schema version。
- 内置测试表和 active keymap 都归 HOGP 专属 Executor；外部不得获得任何内部可写指针。
- `key_count` 表示有效 entry 数量。`click(key_id)` 必须同时检查物理上限和 `key_id < active_keymap.key_count`，不能执行未声明的尾部 entry。

为后续配置源预留窄接口：

```c
void rdx_hogp_key_action_init(void);
void rdx_hogp_key_action_reset(void);
void rdx_hogp_key_action_deinit(void);
int  rdx_hogp_key_action_keymap_apply(const rdx_hogp_key_action_keymap_t *keymap);
int  rdx_hogp_key_action_click(u8 key_id);
```

`keymap_apply()` 在 C5 可以只供 host 契约和后续模块使用，但实现必须执行完整副本及 version/key_count 校验，清零未声明的尾部 entry，不能保存调用方指针。`usages[6]` 已由固定结构限制容量，不需要无效的空循环“校验”。未来 BLE App 的 candidate/CRC/VM 事务应在独立配置存储层完成，验证成功后才调用该接口替换 active keymap。

`rdx_hogp_key_action_click()` 的返回值必须区分“没有映射”和“已消费但发送失败”：

```text
 0  已消费且 key-down 发送成功
 1  已消费，但当前不在可发送的 HOGP 状态或发送失败
-1  key ID 非法或没有 active mapping，调用方可以回落旧按键表
```

这样 HOGP 已请求但尚在广播、加密、订阅或模式切换中时，单击不会误触发旧 RDX 录音/业务动作。发送失败要记录日志，但不得启动 release timer。

## 6. 实施步骤

### Step 1：把默认模式变成配置值

修改 `rdx_ble_server.h/.c`：

1. 增加 `RDX_BLE_DEFAULT_MODE_CONFIG/HOGP` 和 `RDX_BLE_DEFAULT_MODE` 配置，增加非法值编译错误。
2. 新增私有 `rdx_ble_mode_effective_default()`，HOGP enabled 时按配置返回，disabled 时始终返回 Config。
3. `rdx_ble_mode_controller_init()` 使用 effective default 同时初始化 requested/advertised mode，owner 仍为 NONE，pending 仍为 0。
4. `s_ble_mode` 的静态初值保持安全的 Config/NONE，不依赖 include 顺序；运行时初始化后再进入产品默认模式。
5. `rdx_ble_server_init()` 完成 handle、callbacks、HOGP 和 mutex 初始化后，根据 `s_ble_mode.advertised_mode` 调用现有 `rdx_ble_mode_start_hogp_advertising()` 或 `rdx_ble_mode_start_config_advertising()`，替换直接 `rdx_ble_server_adv_enable(1)`。
6. 启动仍经过 `rdx_ble_mode_broadcast_suppressed()`；DUT、poweroff、WiFi transfer 或 SD format 状态下不得因为默认 HOGP 绕过 C1 广播抑制规则。

不得只把 `s_hogp_mode` 初值改成 1，也不得在 `rdx_app.c` 初始化后补调 `rdx_ble_mode_request_hogp(1)`。默认模式必须从模式控制器的初始状态开始一致。

### Step 2：提供窄模式查询与 toggle API

KEY1 三击需要根据模式控制器状态切换，但调用方不能读取 HOGP 私有 `s_hogp_mode`。在 `rdx_ble_server.h/.c` 增加：

```c
u8  rdx_ble_mode_is_hogp_requested(void);
void rdx_ble_mode_request_toggle(void);
```

要求：

- 查询 requested mode，而不是 connection owner 或 `rdx_hogp_mode_get()`。
- `request_toggle()` 内部仍调用 `rdx_ble_mode_request_hogp(!rdx_ble_mode_is_hogp_requested())`，有连接时沿用“先断开、断连完成后应用”的 C1 流程。
- HOGP disabled 时查询返回 0，toggle 是无副作用 stub/空操作。
- 上层仍不得直接调用 `rdx_hogp_mode_set()`、`rdx_hogp_adv_start/stop()` 或 `rdx_ble_server_adv_enable()`。

### Step 3：新增 HOGP 专属 Key Action Executor

新增 `rdx_hogp_key_action.c/.h`，并把 `.c` 加入 `SDK/Makefile`，位置紧邻 HOGP/Profile 源文件。`rdx_key.c/.h` 不做 C5 改动。

实现内容：

1. 保存私有 active keymap 和“是否已有有效 keymap”状态。
2. 在本文件保存受 `RDX_HOGP_KEY_ACTION_TEST_ENABLE && TCFG_RDX_HOGP_ENABLE` 门控的五键 `static const` 测试表，精确表达 `Ctrl+V`、`A`、`Enter`、`Ctrl+C`、`Backspace`。
3. `init()` 清零状态；测试宏开启时完整复制内置表为 active，宏关闭时等待未来 BLE App/VM 调用 `keymap_apply()`。
4. `keymap_apply()` 校验后完整复制到 active keymap，不保存外部指针，并清零 `key_count` 之外的 entry。
5. `click(key_id)` 同时检查 `0-4` 和 active `key_count`；无 active keymap、无对应 entry 或 key ID 非法时返回 `-1`，HOGP 未 ready 时返回“已消费但未发送”，均不启动 timer。
6. 根据 action 构造 `rdx_hogp_keyboard_report_t`：`reserved=0`，复制 modifier 和 6 个 usage，调用 `rdx_hogp_keyboard_report_send()`。
7. 仅当 key-down notify 返回 `APP_BLE_NO_ERROR` 时，覆盖旧 release timer 并启动新的 20 ms timer。
8. 必须检查 `sys_timeout_add()` 返回值；返回 0 时立即尝试 `rdx_hogp_keyboard_release_all()`、记录错误并返回“已消费但失败”，避免 Host 留下粘键。
9. timer 回调清零自身 ID，再调用 `rdx_hogp_keyboard_release_all()`。
10. `reset()` 取消 timer、尝试 release、清除瞬态状态，但保留 active keymap。
11. `deinit()` 取消 timer、尝试 release、清零 active keymap 和有效标志；不能留下回调访问已释放 BLE handle。

20 ms release delay 是执行策略，定义在 `rdx_hogp_key_action.c`，不放入 `rdx_hogp_keyboard.c` 的协议传输配置。

### Step 4：迁移临时 adapter 并按事件精确消费

删除 `rdx_app.c` 中以下临时代码：

- `RDX_APP_HOGP_DEBUG_KEY_UP_DELAY_MS`
- `s_rdx_app_hogp_release_timer`
- `s_rdx_app_hogp_debug_usages`
- `rdx_app_hogp_cancel_release_timer()`
- `rdx_app_hogp_release_timer_cb()`
- `rdx_app_hogp_debug_click_usage()`
- `rdx_app_hogp_debug_num_key()`

在 NUM0-4 分发中改为明确的事件路由：

```text
if HOGP_TEST_ENABLE && key_id == KEY1 && action == TRIPLE_CLICK:
    request mode toggle
    consume

if HOGP_ENABLE && action == CLICK:
    execute HOGP active keymap action
    consume when return >= 0, including Config/not-ready/send-failed

otherwise:
    fall through to existing rdx_key_get_io_num_table()
```

关键不变量：

- KEY1 `CLICK` 发送 `Ctrl+V`，KEY1 `TRIPLE_CLICK` 只切模式。
- KEY1 `LONG/HOLD/UP` 必须回落原按键路径，C5 不消费，给后续 HFP MIC 使用。
- KEY2-5 也只消费 `CLICK`；双击、三击、长按等未定义事件继续回落。
- active test keymap 存在时，Config 模式下普通单击应被消费但不得调用 HOGP send，不能回落触发旧 RDX 业务动作；KEY1 三击仍可切回 HOGP。
- `CLICK -> rdx_hogp_key_action_click()` 只受 HOGP 主开关门控，绝不能受测试宏门控。只有测试宏关闭且未来配置层尚未提供 active mapping 时，`click()` 才返回 `-1`，允许回落原 RDX 按键表。
- 三击由现有 JL `multi_clicks_translate()` 产生，C5 不增加自己的点击计数 timer。
- 现有框架会等待多击判定窗口后才产生单击，这是采用三击入口的预期行为；上机需评估单击体感延迟。

### Step 5：接入执行器生命周期

生命周期必须覆盖以下路径：

- RDX App/BLE Server 正常初始化：调用 `rdx_hogp_key_action_init()`。
- HOGP 断连：调用 `rdx_hogp_key_action_reset()`，取消未完成 release timer。
- 从 HOGP 切到 Config：在 HOGP runtime cleanup 前后调用 `reset()`，保证不会在新 Config 身份下发送旧 release。
- BLE Server exit / deinit：先调用 `rdx_hogp_key_action_deinit()`，再调用 `rdx_hogp_deinit()`，最后释放 `app_ble` handle。Executor 必须在 HOGP handle 和协议状态尚有效时取消 timer 并尝试 release。

调用必须可重复，不能因为断连 cleanup 和 mode cleanup 都触发而 double free timer。disabled build 中相同 API 必须可编译并保持无副作用。

### Step 6：保持名称来源不变

`RDX_HOGP_NAME_SOURCE` 保持默认值 0，不在 T2620 覆盖 custom name：

```text
HOGP advertising local name
    -> rdx_ble_server_get_local_name()
    -> VM_RDX_BLE_NAME or BLE_LOCAL_NAME fallback
```

C5 不修改 `RDX_HOGP_CUSTOM_NAME` 的 fallback 内容，因为 source=0 时不会使用。后续客户需要独立 HOGP 名称时，可以将 source 改为 1 并覆盖 custom name，不需要改 `rdx_hogp_fill_adv_data()`。

上机验证必须确认 HOGP 与 Config 两种广播身份显示相同 local name。这里“相同”指名称来源相同，不代表 Service UUID 和 Appearance 相同。

### Step 7：扩展 host 契约测试

继续扩展 `tests/host/test_hogp_profile_contract.ps1`，不新增独立 runner。至少覆盖：

1. T2620 定义 `RDX_BLE_DEFAULT_MODE=RDX_BLE_DEFAULT_MODE_HOGP` 和 `RDX_HOGP_KEY_ACTION_TEST_ENABLE=1`。
2. `rdx_app_config.h` fallback 的测试宏为 0，旧 `RDX_BLE_DEBUG_MODE_SWITCH_KEY` 从源码消失。
3. HOGP 名称来源仍为 Server local name；不得把 `RDX_HOGP_NAME_SOURCE` 改成 1，也不得要求 `VibeCoding Keyboard`。
4. enabled 默认启动调用 HOGP advertising helper，disabled effective default 回退 Config。
5. 初始化末尾不再无条件直接启动 RDX Config 广播。
6. KEY1 三击调用 `rdx_ble_mode_request_toggle()`；不得调用 HOGP mode/advertising 私有 API。
7. KEY1 `LONG/HOLD/UP` 不在执行器消费条件中；`CLICK -> Executor` 受 HOGP 主开关而非测试宏门控，active mapping 存在时 Config/not-ready 的单击仍被消费。
8. `rdx_hogp_key_action.c` 是五键默认 Report 的唯一字面量来源，精确为：`Ctrl+V`、`A`、`Enter`、`Ctrl+C`、`Backspace`。
9. `rdx_key.c/.h` 不出现 `RDX_HOGP_KEY_ACTION_TEST_ENABLE`、HID usage 表、HOGP action 类型或 HOGP getter；现有 `key_table_io_num*_normal[]` 保持不变。
10. `keymap_apply()` 校验 version/key_count、清零尾部 entry；`click()` 检查 `key_id < active_keymap.key_count`。
11. key-down 成功后才启动 release timer；新任务覆盖旧 timer；timer 分配失败立即 release；reset/deinit 取消 timer。
12. `rdx_app.c` 不再保存固定 usage 表、构造 `rdx_hogp_keyboard_report_t` 或拥有 HOGP release timer。
13. `rdx_hogp_key_action.c` 不调用广播、断连、VM、HFP 或 `rdx_hogp_mode_set()`。
14. Server init 在启动默认广播前检查统一 suppression；Server exit 的 key-action deinit 位于 HOGP deinit 和 handle free 之前。
15. Profile v1 原有 92 项契约继续全部通过。

正则应匹配函数体和明确符号，避免仅凭注释或过宽的 `hogp.*mode` 模式产生误判。

## 7. 验证

### 7.1 Host 测试

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\host\run_host_tests.ps1
```

预期：统一 runner 全部通过，原 92 项 C1-C4 HOGP 契约无回归，新增 C5 检查全部通过。

### 7.2 Enabled 固件构建

确认 `t2620_project_config.h`：

```c
#define TCFG_RDX_HOGP_ENABLE          1
#define RDX_BLE_DEFAULT_MODE          RDX_BLE_DEFAULT_MODE_HOGP
#define RDX_HOGP_KEY_ACTION_TEST_ENABLE 1
```

执行：

```text
cd SDK
make clean
make
```

### 7.3 Disabled 固件构建

临时把 `TCFG_RDX_HOGP_ENABLE` 改为 0，保持另外两个宏不变，执行完整 clean build。预期：

- 不包含 HID ATT 条目；
- 上电进入 RDX Config 广播；
- KEY1 三击无 HOGP 切换副作用；
- 五键回落现有 RDX key table；
- 所有 Key Action 公共 API 仍有可链接实现。

验证后必须恢复 `TCFG_RDX_HOGP_ENABLE=1`，重新检查工作区。

### 7.4 上机回归

```text
[ ] 冷启动不按任何键，直接出现 HOGP 广播并可由 PC 连接
[ ] HOGP 与 Config 广播显示当前 RDX BLE App local name
[ ] KEY1 单击执行 Ctrl+V，且不会切换模式
[ ] KEY2 单击输入 A
[ ] KEY3 单击执行 Enter
[ ] KEY4 单击执行 Ctrl+C
[ ] KEY5 单击执行 Backspace
[ ] 每个 key-down 后都有 release，无粘键
[ ] KEY1 三击：HOGP 已连接时先断开 PC，再进入 Config 广播
[ ] KEY1 再三击：Config 连接/广播完整退出，恢复 HOGP 广播和自动回连
[ ] KEY1 长按、保持、抬起不会发送 Ctrl+V，也不会切换模式
[ ] 评估启用三击识别后五个单击的体感延迟并记录
[ ] 连续模式往返 100 次，无残留连接、广播错误或旧 timer 回调
[ ] 按下后在 20 ms release 前触发断连/切模式/关机，无崩溃和旧 handle 发送
[ ] PC 休眠/唤醒、主动断连、加密失败和 Host suspend 行为保持 C4 基线
```

KEY1 HFP MIC 尚未实现，因此本阶段只验证 `LONG/HOLD/UP` 未被 HOGP/测试路径消费，不验证实际音频采集。

### 7.5 提交前检查

```text
[ ] git diff --check
[ ] git status --short
[ ] git diff --stat
[ ] 确认 t2620_project_config.h 中 HOGP 开关恢复为 1
[ ] 丢弃 db_update_data.bin 等生成二进制改动
[ ] 不提交 SDK/cpu/br28/tools/、output/ 或其他构建产物
```

## 8. 风险与注意事项

- **单击延迟**：现有 JL 多击框架本来就会等待 `click_delay_time` 窗口结束后才生成单击，C5 不新增第二套计时，也不私自缩短窗口；上机仍需记录实际体感。
- **KEY1 事件所有权**：C5 只占用 KEY1 的 CLICK 和测试宏下的 TRIPLE_CLICK。LONG/HOLD/UP 是后续 HFP 的硬边界，审查和 host 测试必须守护。
- **连续点击覆盖**：执行器当前只有一个 release timer，新 key-down 会覆盖旧 timer。对于 C5 的单击测试足够；timer 分配失败必须立即 release。宏序列、并发 chord 和队列策略留给后续完整执行器阶段。
- **切换时 release**：模式切换前应取消 timer并清理本地 Report。Host 侧若在 key-down 后立即断开，无法保证收到可见 release；断连会由 Host 清理键盘状态，这是可接受边界。
- **名称缓存**：虽然 C5 不迁移名称，Windows 仍可能缓存设备名；验收以同一 local-name source 和实际广播包为准，不把 UI 缓存误判为固件变更。
- **配置结构兼容**：`rdx_hogp_key_action.c` 的测试表和 C5 runtime keymap 都不是 Flash ABI。BLE App/VM 落地时必须另行设计 schema version、长度、CRC、candidate/active 事务和掉电恢复，不能直接把 C struct 原样写入 VM。
- **关闭态**：测试宏和默认 HOGP 都是项目配置，但 HOGP 主开关拥有最高优先级。disabled build 必须完整回退 Config，不能出现“请求 HOGP 但无 Profile”的死状态。

## 9. 审核清单

```text
[ ] 同意 C5 改为“默认 HOGP + Key Action 测试骨架”，不执行产品名称迁移
[ ] 同意只使用 RDX_HOGP_KEY_ACTION_TEST_ENABLE 一个宏门控测试 Keymap 与 KEY1 三击入口
[ ] 同意五键映射：Ctrl+V、A、Enter、Ctrl+C、Backspace
[ ] 同意 KEY1 三击切换 HOGP/Config
[ ] 同意 KEY1 LONG/HOLD/UP 明确保留给后续 HFP MIC
[ ] 同意 rdx_key.c/.h 完全保持原有 APP_MSG 映射职责，不承载 HOGP 类型或测试配置
[ ] 同意 C5 新增 rdx_hogp_key_action.c/.h 作为 HOGP 专属执行器和测试 Keymap 唯一来源
[ ] 同意 CLICK 正式路由不受测试宏门控，供未来 BLE App/VM active keymap 复用
[ ] 同意 timer 分配失败、部分 key_count、启动广播抑制和退出清理顺序按评审结论修复
[ ] 同意默认 HOGP 必须由现有 BLE mode controller 启动
[ ] 同意 HOGP disabled 时强制回退 Config
[ ] 同意 C5 不修改 Profile v1 外部契约
```

## 10. 建议提交信息

```text
feat(hogp): Phase 6 C5 默认进入 HOGP 并建立 Key Action 骨架

- Start T2620 in HOGP mode through the BLE mode controller while preserving
  the Config fallback when HOGP is compiled out.
- Add an HOGP-specific active-keymap executor for full keyboard reports and move
  release timer ownership out of rdx_app.c.
- Keep one project test gate for the five-key mixed map and KEY1 triple-click
  mode toggle, while reserving KEY1 long/hold/up for the future HFP MIC path.
- Keep HOGP advertising on the existing RDX Server local-name source and
  preserve the Profile v1 byte contract.

验证：make clean && make；TCFG_RDX_HOGP_ENABLE=0 构建；run_host_tests.ps1；
五键单击、KEY1 三击模式切换和 100 次往返上机回归通过。
```

## 11. 完成后状态

C5 完成后，系统具备以下稳定边界：

- T2620 默认从统一 BLE mode controller 启动 HOGP，关闭态可靠回退 Config。
- 五个物理键通过统一 Executor 生成完整 Keyboard Report，已覆盖普通键、功能键和 modifier 组合键。
- KEY1 的 CLICK、TRIPLE_CLICK 和未来 HFP LONG/HOLD/UP 拥有明确且互不混淆的事件所有权。
- 测试入口集中在一个项目级宏中，不再散落于 `rdx_app.c`。
- HOGP 与 Config 继续复用当前 RDX BLE local name，不引入产品名称和配对迁移风险。
- 后续 BLE App 只需实现 candidate 配置、校验、VM 事务并调用 `rdx_hogp_key_action_keymap_apply()`；无需修改 HOGP Profile v1 或按键发送链路。
