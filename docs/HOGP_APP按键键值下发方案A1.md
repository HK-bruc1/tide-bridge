# HOGP APP 按键键值下发方案

> 适用项目：T2620 / JL AC701N（BR28）/ RDX BLE / HOGP Keyboard
> 协议入口：保持现有 `*APP#custom#...#` 不变
> 文档目标：定义 APP 配置五个物理按键时的正式下发格式、校验规则、应答语义和持久化边界
> 当前状态：v1 设计基线；第 1.2 节量产冻结项全部关闭后方可标记为正式协议

## 1. 结论

正式协议保留 RDX `custom` 入口，并在该入口下使用单一 HOGP Keymap 命名空间：

```text
*APP#custom#hogpkm#<HEX_FRAME>#
```

设备应答同样使用 `custom` 入口：

```text
*DEV#custom#hogpkm#<HEX_FRAME>#
```

核心规则如下：

1. `custom` 入口和 RDX 原有收发链路不变。
2. `hogpkm` 是 HOGP Keymap 的固定命令名。
3. `HEX_FRAME` 是 `custom` 的唯一 value，内部不再出现 `#`。
4. APP 下发标准 USB HID Keyboard Usage，不下发 ASCII、Windows VK、JavaScript keyCode 或 Android KeyCode。
5. 五个物理键组成一张完整 keymap，每次以整表形式原子更新。
6. v1 keymap 使用固定顺序的 `5 × [modifier + usages[6]]`，共 35 字节。
7. 协议包含版本、操作码、请求 ID、配置 revision、长度和 CRC32。
8. 设备只有在 RAM 与 VM 均提交成功后才返回成功应答。

### “上报”入口说明

本方案中的“上报”必须区分两条不同链路，不得混用：

| 上报对象 | 内容 | 统一入口 | 实际链路 |
|---|---|---|---|
| APP | 配键请求的执行结果、当前 keymap、能力信息 | HOGP keymap 上行适配器 | RDX `custom` 上行，外层格式为 `*DEV#custom#hogpkm#<HEX_FRAME>#` |
| PC / HOGP Host | 用户按键产生的标准 8-byte Keyboard Input Report | `rdx_hogp_key_action_click(key_id)` | HOGP Input Report characteristic ATT Notify |

设备向 APP 应答 `SET_KEYMAP`、`GET_KEYMAP`、`GET_CAPS`、`RESET_KEYMAP` 时，应先生成对应的 `0x81`～`0x84` 响应 Frame，将完整 Frame 编码为以 `\0` 结尾的 HEX 字符串，再由 HOGP keymap 上行适配器构造完整外层命令：

```c
*DEV#custom#hogpkm#<HEX_FRAME>#
```

当前 `librdxApp.a` 中的旧 `rdx_protocol_custom_msg_indicate()` 按 100-byte custom value 编译，无法安全容纳 100 字符 GET value 及结尾 `\0`。因此 `hogpkm` 上行使用 121-byte 静态完整命令缓冲，并按 120-byte 明确长度调用 `rdx_protocol_packet_send_priority()`；其他短 custom 命令继续使用旧接口。

物理按键需要向电脑发送键盘动作时，不经过 RDX `custom`，业务层调用：

```c
rdx_hogp_key_action_click(key_id);  /* key_id: 0..4，对应 KEY1..KEY5 */
```

该入口读取当前已生效 keymap，发送按下 Report，并在配置的延时后发送全零释放 Report。底层 `rdx_hogp_keyboard_report_send()` 是原始 Report 发送接口，不作为物理按键业务的首选统一入口。

v1 只定义设备对 APP 请求的响应，没有定义“设备主动向 APP 推送 keymap 变化”或“把每次物理按键同步上报 APP”的 opcode。若后续需要主动事件，必须新增独立 opcode、触发条件和去重/时序规则；不得复用 `SET_KEYMAP_RSP`，也不得把 HOGP Keyboard Report 塞入 `custom` 通道。

### 1.1 当前阻塞点：RDX custom 通道容量尚未确认

当前联调日志出现 `===== ATT MTU = 514`。该日志打印的并不是标准意义上的完整 ATT_MTU，而是固件从 MTU Exchange 事件取得协商值后减去 3 字节 ATT Write Header 的结果。对应代码为：

```c
u16 mtu = att_event_mtu_exchange_complete_get_MTU(packet) - 3;
log_info("===== ATT MTU = %u\r", mtu);
```

因此，本次连接可以反推出：

```text
协商 ATT_MTU                         = 517 bytes
ATT Write Request/Command Value 上限 = 517 - 1-byte opcode - 2-byte handle
                                    = 514 bytes
```

这里的 514 是由 ATT PDU 字段长度计算出的单次 Attribute Value 载荷预算。具体特征值实现、SDK、RDX packet 封装和业务缓冲区仍然可以给出更小的限制。精简后的五键 SET 外层命令为 118 字符，低于该预算，因此在本次连接的 BLE/ATT 层具备单包承载条件。

但这只能证明当前连接的 ATT 容量足够，不能证明整条 RDX `custom` 链路已经支持该长度。以下接口位于现有 RDX 协议链或预编译协议库中，其内部缓冲、切分和组包上限目前未从仓库源码确认：

- `rdx_protocol_packet_recv()` 的原始收包上限；
- `rdx_protocol_handle_custom_cmd()` 的完整命令及 `cmd`/`value` 上限；
- `rdx_protocol_custom_msg_indicate()` 的上行组包和发送上限；
- RDX packet 层是否会在进入 custom 解析前完成跨 GATT Write 的分片重组；
- 内部字符串缓冲区是否为结尾 `\0` 预留空间。

`rdx_app_custom_command_parse(char *cmd, char *value)` 确实在应用源码中实现，并由静态库直接回调；但 `char *` 只携带内存地址，不携带容量或字符串长度，也不表示数据可以无限增长。容量仍由静态库调用方提供的内存决定。调用方既可能把命令复制到固定长度的局部数组后传入指针，也可能在原始接收包上就地写入 `\0` 并传入指向包内的指针；两种实现对回调函数呈现的形参完全相同，仅凭 `char *` 声明无法判断实际容量。

因此，这里需要确认的不是 `rdx_app_custom_command_parse()` 是否有实现，而是静态库在调用它之前如何保存和切分完整命令，以及上行接口如何组包。静态库还必须保证 `cmd`/`value` 以 `\0` 结尾，并保证指针所指内存至少在回调返回前有效。

因此，**当前正式接入的阻塞点是 RDX custom 通道端到端的最大报文长度尚未确认，而不是 HOGP 或本次连接的 ATT MTU 不足。** 在完成协议库负责人确认或端到端长度实测前，本方案可以作为 wire protocol 设计基线，但不能把“五键整表已可稳定下发”标记为完成。

端到端最大可用命令长度由整条链路中最小的容量决定：

```text
max_command_len = min(
    当前连接的 ATT Value 单包预算或分片重组能力,
    RDX packet 接收/发送与重组容量,
    静态库 custom 解析/组包容量,
    应用层自有解析缓冲区容量
)
```

所以不能简单表述为“标准 ATT 支持 514，最终只由入口回调函数的缓冲区决定”。`rdx_app_custom_command_parse()` 自身只有指针形参，并没有声明或拥有一个固定大小的入口缓冲区；真正可能形成限制的是静态库在调用回调前使用的接收、切分或临时缓冲区。哪一层容量最小，哪一层才是最终瓶颈。

需要满足的最低容量为：

| 方向 | 最大基线示例 | 二进制 Frame | HEX value | 完整外层命令 | C 字符串缓冲区最低要求 |
|---|---|---:|---:|---:|---:|
| APP → DEV | 五键 `SET_KEYMAP` | 49 bytes | 98 chars | 118 chars | 完整命令至少 119 bytes；value 至少 99 bytes |
| DEV → APP | 五键 `GET_KEYMAP_RSP` | 50 bytes | 100 chars | 120 chars | 完整命令至少 121 bytes；value 至少 101 bytes |

应向 RDX 协议库负责人确认上述接口的明确上限，并通过 64、96、128、150、180 字节的 `custom` value 阶梯测试验证实际固件库版本不存在截断、越界或静默丢包。验收要求是每档下行完整到达应用回调、上行完整回显、长度与内容 CRC 均一致。若现有缓冲区不足，优先扩大 custom 收发缓冲区；若协议库不可修改，再评估 Base64 或带事务的应用层分片，不能退回无原子性的 `setkey1` 至 `setkey5` 逐条提交。

### 1.2 量产冻结前必须关闭的事项

以下事项属于 v1 量产冻结门槛：

1. 完成 RDX custom 上下行容量阶梯测试，至少覆盖本方案的 118-byte SET 和 120-byte GET 响应。
2. 由产品侧给出 T2620 正式出厂 keymap；当前 Ctrl+C/Ctrl+V/Ctrl+X/Backspace/Enter 只能作为协议测试向量。
3. 明确绑定/鉴权状态的查询接口；CONFIG/HOGP connection owner 已有现成接口，不属于未知项。
4. 完成 VM ABI、掉电原子提交和重启恢复测试。
5. 确认 RDX 外层包校验方式。v1 内层 Frame CRC32 仍强制保留，用于端到端 custom 内容校验和请求指纹；外层 CRC 不替代该字段。

## 2. 为什么不直接采用初步格式

初步联调格式为：

```text
*APP#custom#setkey1#S#112#0#
```

该格式可以用于快速验证收包入口，但不适合作为正式产品协议。

### 2.1 与现有 custom 回调模型不匹配

当前应用侧入口是：

```c
void rdx_app_custom_command_parse(char *cmd, char *value);
```

它天然对应：

```text
*APP#custom#<cmd>#<value>#
```

而 `setkey1#S#112#0` 实际包含多个独立 value。继续扩展这种格式会让字段数量、缺失字段、尾部分隔符和协议升级越来越难处理。

推荐格式始终只有两个业务参数：

```text
cmd   = hogpkm
value = HEX_FRAME
```

### 2.2 数字 112 的键值语义不唯一

十进制 `112` 在不同体系中含义完全不同：

| 编码体系 | `112` 的可能含义 |
|---|---|
| ASCII / Unicode code point | 字符 `p` |
| Windows VK / JavaScript legacy keyCode | `F1` |
| USB HID Keyboard Usage | `0x70`，即 `F21` |

HOGP 固件最终发送的是 HID Keyboard Report，因此正式协议必须直接使用 USB HID Keyboard/Keypad Usage Page（`0x07`）中的 Usage ID。

例如：

| 用户动作 | modifier | HID usage |
|---|---:|---:|
| `Ctrl+C` | `0x01` | `0x06` |
| `Ctrl+V` | `0x01` | `0x19` |
| `Shift+A` | `0x02` | `0x04` |
| `F1` | `0x00` | `0x3A` |
| Enter | `0x00` | `0x28` |
| Backspace | `0x00` | `0x2A` |

APP UI 可以显示字符、快捷键名称或平台键名，但在组包前必须转换成标准 HID modifier 和 usage。

### 2.3 `S` 的含义不稳定

`S` 可能被理解成 Single、Shift、String 或 Shortcut，不适合作为无线协议枚举。

- 如果 `S` 表示 Shift，应使用 modifier 的 `LShift` 位，即 `0x02`。
- 如果 `S` 表示 Single Click，当前 HOGP v1 只为物理键 CLICK 配置键盘动作，不需要在每个条目中重复携带。
- 双击、长按、Macro 或 Consumer Control 应由后续协议版本独立设计，不得复用含义模糊的单字符字段。

### 2.4 逐键更新无法保证整表一致性

如果 APP 依次下发 `setkey1` 至 `setkey5`，中途断连、掉电或某条校验失败时，设备可能留下部分新配置和部分旧配置。

本产品只有五个物理键，整张 keymap 的数据量很小。即使用户只修改一个键，APP 也应在本地修改整表副本，然后一次性提交完整五键表。

### 2.5 缺少可靠重试依据

实际联调日志中，同一条 `setkey1` 命令在超时后被 APP 重复发送，但没有可关联的 request ID、成功应答和幂等规则。正式协议必须使设备能够判断：

- 这是一个新配置请求；
- 这是同一请求的超时重试；
- 这是基于旧 revision 的过期写入；
- 这是内容与当前配置完全相同的幂等重放。

## 3. 外层命令格式

### 3.1 APP 下行

```text
*APP#custom#hogpkm#<HEX_FRAME>#
```

### 3.2 设备上行

```text
*DEV#custom#hogpkm#<HEX_FRAME>#
```

### 3.3 HEX 编码规则

1. 二进制 Frame 逐字节编码成两个十六进制字符。
2. 发送端推荐使用大写 `0-9A-F`，接收端应兼容大小写。
3. HEX 字符串内部不得包含空格、逗号、换行或 `#`。
4. HEX 字符数必须为偶数。
5. 接收端必须先完成完整外层 RDX 命令重组，再解析 HEX Frame；不得把单个 GATT 分片当成完整命令。

选择 HEX 而不是继续增加 `#` 字段的理由是：

- 完全符合现有 `cmd/value` 两参数入口；
- 不需要 JSON 解析器和动态内存；
- 定长字段便于边界检查和 host 测试；
- 日志仍可直接复制和离线解码；
- 不受二进制数据中 `0x00`、`#` 或字符串终止符影响。

## 4. 通用 Frame

所有多字节整数统一使用小端序。

| 偏移 | 字段 | 类型 | 长度 | 说明 |
|---:|---|---|---:|---|
| 0 | `version` | `u8` | 1 | 协议版本，首版固定为 `1` |
| 1 | `opcode` | `u8` | 1 | 请求或响应操作码 |
| 2 | `request_id` | `u16 LE` | 2 | APP 生成的请求流水号 |
| 4 | `revision` | `u32 LE` | 4 | 请求时为基准配置版本，响应时为设备当前版本 |
| 8 | `payload_len` | `u16 LE` | 2 | payload 的字节长度 |
| 10 | `payload` | `u8[]` | N | 操作对应的数据 |
| 10+N | `crc32` | `u32 LE` | 4 | Header 与 payload 的 CRC32 |

Frame 总长度：

```text
frame_len = 10 + payload_len + 4
```

解析时必须满足：

```text
decoded_hex_len == frame_len
```

### 4.1 CRC32 参数

统一采用 CRC-32/ISO-HDLC（常见名称：CRC-32、CRC-32/ADCCP、IEEE CRC32）：

```text
poly   = 0x04C11DB7
refin  = true
refout = true
init   = 0xFFFFFFFF
xorout = 0xFFFFFFFF
```

CRC 覆盖范围为 Frame 的 `version` 至 payload 最后一个字节，不包含 CRC 字段本身。CRC 数值在线路中按小端序写入。

Frame CRC32 是 v1 强制字段，用于发现 custom 内容损坏并作为请求指纹；即使 RDX 外层包也启用 CRC，该字段仍保留，使本协议不依赖预编译库是否向应用暴露外层校验结果。canonical keymap CRC 和 VM record CRC 采用同一算法，但覆盖范围不同。所有 CRC 都不代表身份认证，配置写入仍必须受 RDX Config owner 和产品鉴权状态约束。

## 5. Opcode

| Opcode | 名称 | 方向 | 说明 |
|---:|---|---|---|
| `0x01` | `SET_KEYMAP` | APP → DEV | 原子替换完整五键表 |
| `0x02` | `GET_KEYMAP` | APP → DEV | 读取当前生效配置 |
| `0x03` | `GET_CAPS` | APP → DEV | 查询协议和键盘能力 |
| `0x04` | `RESET_KEYMAP` | APP → DEV | 恢复产品默认配置 |
| `0x81` | `SET_KEYMAP_RSP` | DEV → APP | SET 应答 |
| `0x82` | `GET_KEYMAP_RSP` | DEV → APP | GET 应答及当前 keymap |
| `0x83` | `GET_CAPS_RSP` | DEV → APP | 能力应答 |
| `0x84` | `RESET_KEYMAP_RSP` | DEV → APP | RESET 应答 |

未知 opcode 必须返回 `UNSUPPORTED_OPCODE`，不得静默忽略。

v1 payload 长度矩阵固定为：

| Opcode | 成功 payload | 失败 payload |
|---|---:|---:|
| `SET_KEYMAP` | 35 bytes | 不适用 |
| `GET_KEYMAP` | 0 bytes | 不适用 |
| `GET_CAPS` | 0 bytes | 不适用 |
| `RESET_KEYMAP` | 0 bytes | 不适用 |
| `SET_KEYMAP_RSP` | 5 bytes | 1-byte status |
| `GET_KEYMAP_RSP` | 36 bytes | 1-byte status |
| `GET_CAPS_RSP` | 5 bytes | 1-byte status |
| `RESET_KEYMAP_RSP` | 5 bytes | 1-byte status |

## 6. Keymap payload v1

`SET_KEYMAP` 的 payload 是一张完整五键表：

```c
struct hogp_keymap_payload_v1 {
    struct {
        u8 modifiers;   /* HID modifier bitmap */
        u8 usages[6];   /* HID Keyboard/Keypad Usage IDs */
    } keys[5];          /* 固定顺序 KEY1..KEY5 */
};
```

以上仅表示 wire DTO 的字段顺序，不要求固件直接声明或透传这个 C struct。wire DTO、VM 记录和 executor 内部 RAM struct 必须保持独立，通过显式转换连接。

### 6.1 payload 长度

```text
5 × 7 = 35 bytes = 0x0023
```

对于 `version=1` 的 `SET_KEYMAP`：

- `payload_len` 必须等于 `35`；
- 每 7 字节表示一个物理键；
- 五个条目固定按 KEY1、KEY2、KEY3、KEY4、KEY5 顺序出现。

固定数量和顺序使 `key_count` 与 `key_id` 不再需要在线路中重复传输；它还能生成唯一的 canonical payload，方便 CRC、幂等判断、VM 比较和 host 测试。

### 6.2 物理键编号

| payload 字节范围 | 产品键 | 固定索引 | 当前物理索引 |
|---:|---|---:|---|
| 0–6 | KEY1 | 0 | `KEY_IO_NUM0` |
| 7–13 | KEY2 | 1 | `KEY_IO_NUM1` |
| 14–20 | KEY3 | 2 | `KEY_IO_NUM2` |
| 21–27 | KEY4 | 3 | `KEY_IO_NUM3` |
| 28–34 | KEY5 | 4 | `KEY_IO_NUM4` |

wire 协议使用稳定的固定位置，不直接传 GPIO 编号或端口名称。GPIO 调整不得改变 APP 协议中的产品键顺序。

### 6.3 禁用按键

一个条目的 7 字节全部为零表示 `disabled`：

```text
00 00 00 00 00 00 00
```

disabled 物理键在 HOGP 模式下必须被消费但不得发送 Keyboard Report。无需额外传输 `action_type`；Macro、Layer 和 Consumer Control 由未来协议版本定义，不得借用 v1 数据。

### 6.4 modifiers

`modifiers` 直接对应标准 Keyboard Report 第 0 字节：

| Bit | 掩码 | 含义 |
|---:|---:|---|
| 0 | `0x01` | Left Ctrl |
| 1 | `0x02` | Left Shift |
| 2 | `0x04` | Left Alt |
| 3 | `0x08` | Left GUI / Windows / Command |
| 4 | `0x10` | Right Ctrl |
| 5 | `0x20` | Right Shift |
| 6 | `0x40` | Right Alt |
| 7 | `0x80` | Right GUI |

组合修饰键使用按位或，例如 `Ctrl+Shift` 为 `0x03`。

### 6.5 usages

`usages[6]` 对应标准 8-byte Keyboard Report 的六个普通键槽，不包含 Report ID：

```text
[modifier, reserved, usage1, usage2, usage3, usage4, usage5, usage6]
```

规则：

1. 有效 Usage 从 `usages[0]` 开始连续排列，遇到首个 `0x00` 后，其余元素必须全部为零。
2. 有效 Usage 数量由连续非零项推导，范围为 `0..6`，不单独传输 `usage_count`。
3. 有效 Usage 不得重复。
4. HID ErrorRollOver/PostFail/ErrorUndefined（`0x01..0x03`）不得作为 APP 配置值。
5. Modifier Usage（`0xE0..0xE7`）必须编码到 `modifiers`，不得重复放入 `usages[]`。
6. 固件必须按产品支持的 USB HID Keyboard/Keypad Usage 白名单校验保留值。
7. `modifiers` 非零且六个 Usage 全零是合法的，用于发送单独 Ctrl、Shift 等 modifier。
8. `modifiers` 与六个 Usage 全零表示 disabled，不发送 Report。

## 7. 请求与响应语义

### 7.1 request_id

1. APP 为每个新请求分配 `u16 request_id`。
2. 同一连接中的新请求不得复用尚未完成的 request ID。
3. 超时重试必须原样重发相同 request ID、revision 和 payload。
4. 设备只缓存最近 1 条成功提交的 SET/RESET 请求，不得建立无界历史队列；校验失败、BUSY 和 STORAGE_ERROR 不进入缓存，允许 APP 修正或重试。
5. GET/CAPS 是只读操作，无需缓存，重试时直接从当前状态重新生成响应。
6. 收到相同 request ID 和相同 Frame CRC 时，设备重新生成并返回原结果，不重复写 VM。
7. 相同 request ID 但 Frame CRC 不同时，如果旧请求仍在处理则返回 `REQUEST_ID_CONFLICT`；旧请求已经结束时可按新请求处理，revision 仍负责阻止过期写入。
8. request ID 回绕后可以重新使用，但不得与仍在处理的请求冲突。
9. BLE 断开时清除该 RAM 缓存；重连后的重复 SET 通过 revision 与当前 keymap CRC 按幂等成功处理。

幂等缓存的逻辑字段固定为：

```text
u16 request_id;
u32 request_frame_crc32;
u8  opcode;
u32 revision;
u32 keymap_crc32;
u8  valid;
```

原始字段共 16 字节，考虑编译器对齐后应控制在 16～20 字节。不得缓存完整 HEX 命令或完整响应字符串。

### 7.2 revision

`revision` 是设备持久化 keymap 的单调递增版本。

- `revision=0` 保留给“尚无用户提交配置、当前使用产品出厂默认表”的状态。
- `SET_KEYMAP`：APP 填写其读取到的当前 revision。
- `GET_KEYMAP` / `GET_CAPS`：APP 填写 `0`。
- 成功 SET/RESET：设备提交后 revision 加 1，并在响应中返回新 revision。
- revision 不匹配且 payload 与当前配置不同：返回 `REVISION_CONFLICT`。
- revision 不匹配但 payload CRC 与当前配置完全相同：允许按幂等成功处理，返回当前 revision，不重复写 VM。

首次 SET 必须携带 `revision=0`，成功后持久化 `revision=1`。RESET 恢复产品默认内容，但 revision 必须继续递增，不能回退到 0。APP 不应通过特殊 revision 强制覆盖配置。

revision 禁止回绕。如果当前 revision 已为 `0xFFFFFFFF`，设备必须拒绝新的 SET/RESET，并进入需要维护工具处理的异常状态，不能静默回到 0。

### 7.3 响应 status

所有响应 payload 的第一个字节都是 `status`：

| Status | 名称 | 说明 |
|---:|---|---|
| `0x00` | `OK` | 操作成功 |
| `0x01` | `UNSUPPORTED_VERSION` | 不支持 Frame 版本 |
| `0x02` | `UNSUPPORTED_OPCODE` | 不支持操作码 |
| `0x03` | `MALFORMED_FRAME` | HEX、长度或字段结构错误 |
| `0x04` | `CRC_ERROR` | Frame CRC 不匹配 |
| `0x05` | `REVISION_CONFLICT` | APP 基准 revision 已过期 |
| `0x06` | `INVALID_KEYMAP` | 固定长度、条目布局或零填充规则错误 |
| `0x07` | `INVALID_HID_USAGE` | modifier/usage 配置非法 |
| `0x08` | `STORAGE_ERROR` | VM 写入、读回或提交失败 |
| `0x09` | `NOT_AUTHORIZED` | owner、绑定或鉴权状态不允许 |
| `0x0A` | `BUSY` | 当前正在处理互斥操作 |
| `0x0B` | `INTERNAL_ERROR` | 其他内部错误 |
| `0x0C` | `REQUEST_ID_CONFLICT` | 相同 request ID 正在处理不同内容 |

错误响应的 Frame Header `revision` 必须填写设备当前 revision，便于 APP 决定是否重新 GET。错误响应 payload 固定为 1 字节 status，不在 payload 中重复携带 revision。

如果 HEX 或 Frame 短到无法安全取得完整 Header 和 request ID，设备必须静默丢弃，由 APP 超时重试；不得构造一个无法关联的错误响应。能够安全取得 request ID 后发生的版本、长度、CRC 或业务校验错误，才返回对应 status。

### 7.4 SET_KEYMAP_RSP payload

成功响应：

```text
u8  status;         // 0x00
u32 keymap_crc32;   // canonical 35-byte keymap payload 的 CRC32，小端
```

失败响应 payload 必须且只能携带一个字节的 status；当前 revision 已由 Frame Header 提供。

### 7.5 GET_KEYMAP_RSP payload

成功响应：

```text
u8 status;          // 0x00
u8 keymap[35];      // 当前 canonical keymap payload
```

如果设备尚无有效生产配置，应返回明确的产品默认 keymap、`revision=0` 及其 keymap CRC；不得把固件测试 keymap 冒充为用户配置。

### 7.6 GET_CAPS_RSP payload

v1 响应格式固定为：

```text
u8 status;
u8 max_physical_keys;  // 5
u8 max_usages;         // 6
u8 action_mask;        // bit0=disabled, bit1=keyboard
u8 modifier_mask;      // 0xFF
```

`GET_CAPS_RSP` 成功 payload 长度必须为 5 字节。APP 首次连接或固件版本变化后必须读取能力，避免把未来协议能力写死在 UI 中。

### 7.7 RESET_KEYMAP 请求与响应

`RESET_KEYMAP` 请求 payload 必须为空，Frame Header revision 必须等于 APP 最近读取到的当前 revision。设备使用与 SET 相同的 PREPARE/APPLY/COMMIT 事务提交产品默认 35-byte payload，并令 revision 加 1。

成功 `RESET_KEYMAP_RSP` payload 与 SET 成功响应一致：

```text
u8  status;         // 0x00
u32 keymap_crc32;   // 产品默认 35-byte payload 的 CRC32，小端
```

RESET 不是“删除 VM 后回到 revision 0”。只有设备从未存在 committed 用户记录时才使用 revision 0；任何成功 RESET 都产生新的 committed revision。

## 8. 完整示例

本节所有 CRC 均按第 4.1 节参数计算，可用于 APP、固件和 host 测试的共同测试向量。

示例 keymap：

| Key | 动作 |
|---|---|
| KEY1 | Ctrl+C |
| KEY2 | Ctrl+V |
| KEY3 | Ctrl+X |
| KEY4 | Backspace |
| KEY5 | Enter |

### 8.1 五个 Entry

```text
KEY1 Ctrl+C:
01 06 00 00 00 00 00

KEY2 Ctrl+V:
01 19 00 00 00 00 00

KEY3 Ctrl+X:
01 1B 00 00 00 00 00

KEY4 Backspace:
00 2A 00 00 00 00 00

KEY5 Enter:
00 28 00 00 00 00 00
```

完整 35-byte canonical keymap payload：

```text
01 06 00 00 00 00 00
01 19 00 00 00 00 00
01 1B 00 00 00 00 00
00 2A 00 00 00 00 00
00 28 00 00 00 00 00
```

无空格 HEX：

```text
0106000000000001190000000000011B0000000000002A000000000000280000000000
```

该 payload 的 CRC32 数值为：

```text
0xC1142483
```

### 8.2 SET_KEYMAP 请求

条件：

```text
version     = 1
opcode      = 0x01
request_id  = 42 / 0x002A
revision    = 7
payload_len = 35 / 0x0023
```

二进制 Frame：

```text
01 01 2A 00 07 00 00 00 23 00
01 06 00 00 00 00 00
01 19 00 00 00 00 00
01 1B 00 00 00 00 00
00 2A 00 00 00 00 00
00 28 00 00 00 00 00
40 29 56 B8
```

Frame CRC32 数值为 `0xB8562940`，线路小端字节为 `40 29 56 B8`。

最终 APP 下发字符串：

```text
*APP#custom#hogpkm#01012A000700000023000106000000000001190000000000011B0000000000002A000000000000280000000000402956B8#
```

### 8.3 SET_KEYMAP 成功响应

设备成功提交后 revision 从 `7` 增加到 `8`。

响应 payload：

```text
00 83 24 14 C1
```

- `00`：status=`OK`
- `83 24 14 C1`：keymap CRC32 `0xC1142483` 的小端字节

最终设备响应：

```text
*DEV#custom#hogpkm#01812A0008000000050000832414C130E7EB13#
```

APP 必须同时核对：

1. opcode 为 `SET_KEYMAP_RSP`；
2. request ID 为 `42`；
3. status 为 `OK`；
4. revision 为 `8`；
5. 响应中的 keymap CRC 与 APP 提交内容一致；
6. 整个响应 Frame CRC 正确。

### 8.4 GET_KEYMAP 请求

```text
*APP#custom#hogpkm#01022B00000000000000CE55A286#
```

含义：

```text
opcode     = GET_KEYMAP
request_id = 43
revision   = 0
payload    = empty
```

### 8.5 GET_KEYMAP 成功响应

当前 revision 为 `8`，响应携带 status 和完整 keymap：

```text
*DEV#custom#hogpkm#01822B00080000002400000106000000000001190000000000011B0000000000002A0000000000002800000000003311346E#
```

响应 `payload_len=36`：

```text
1-byte status + 35-byte keymap
```

### 8.6 GET_CAPS 请求与响应

请求：

```text
*APP#custom#hogpkm#01032C0000000000000094481C9B#
```

设备当前 revision 为 8 时的成功响应：

```text
*DEV#custom#hogpkm#01832C0008000000050000050603FF141E7965#
```

成功 payload 为：

```text
00 05 06 03 FF
```

- `00`：OK
- `05`：最多 5 个物理键
- `06`：每键最多 6 个普通 Usage
- `03`：支持 disabled 与 keyboard
- `FF`：支持八个标准 modifier 位

## 9. APP 推荐流程

### 9.1 首次进入配键页面

```text
连接 RDX Config
    → GET_CAPS
    → GET_KEYMAP
    → 校验响应 CRC
    → 保存 revision 与完整 keymap 副本
    → 显示到 UI
```

### 9.2 用户修改一个键

```text
修改 APP 本地完整 keymap 副本
    → 生成 canonical 35-byte payload
    → 使用已读取 revision 发送 SET_KEYMAP
    → 等待匹配 request ID 的 SET_KEYMAP_RSP
    → 成功后更新本地 revision
```

不要因为用户只修改 KEY1 就只发送 KEY1。五键整表更新使设备可以在一次事务内完成校验、持久化和运行时切换。

### 9.3 超时与重试

```text
超时
    → 原样重发相同 request ID、revision、payload 和 CRC
    → 不生成新的 request ID
```

如果最终无法确认 SET 是否成功，APP 重新连接后执行 GET_KEYMAP，通过 revision 和 keymap CRC 对账。

### 9.4 模式切换

配键成功不应自动触发 Config → HOGP 切换或立即断开 BLE。推荐顺序：

```text
SET_KEYMAP
    → 收到并验证成功 ACK
    → 必要时 GET_KEYMAP 回读
    → APP 单独请求切换 HOGP 模式
```

这样可以避免设备在 ACK 发出前切换广播身份，导致 APP 把成功写入误判为超时。

## 10. 固件处理流程

推荐增加独立的正式配置层，例如：

```text
rdx_app_custom_command_parse()
    → 识别 cmd == "hogpkm"
    → rdx_hogp_keymap_config_handle_hex(value)
    → Hex 解码与 Frame 校验
    → 构造 candidate keymap
    → 完整业务校验
    → VM PREPARE
    → executor RAM 串行 apply
    → VM COMMIT
    → 发送响应
```

职责边界：

- RDX `custom` 层：只负责命令入口和上行发送。
- HOGP keymap config 层：负责 wire DTO、校验、revision、幂等、VM 和事务。
- HOGP key action executor：只负责 RAM active keymap 和 Keyboard Report 执行。
- HOGP keyboard transport：只负责 HID 状态、8-byte Report 和 notify。

`rdx_hogp_key_action_keymap_t` 是 executor 内部 RAM 结构，不得直接作为 HEX Frame 或 VM ABI。

### 10.1 回调指针的容量与生命周期

静态库调用：

```c
void rdx_app_custom_command_parse(char *cmd, char *value);
```

该签名不包含 `len`，因此应用层必须把 `cmd` 和 `value` 当作由调用方临时借出的、以 `\0` 结尾的只读字符串使用，不能根据指针类型推断缓冲区大小。

正式处理规则：

1. 仅在回调有效期间访问 `cmd` 和 `value`。
2. 使用协议定义的最大值做有界长度检查，例如 `strnlen(value, RDX_HOGP_KEYMAP_HEX_MAX_LEN + 1)`；v1 最大下行 HEX value 为 98 字符，超过上限立即拒绝。
3. 不得把静态库传入的原始 `cmd`/`value` 指针直接投递到 `app_core`、定时器或其他异步任务。
4. 如果需要异步处理，应在回调内完成 HEX 长度和字符检查，并把数据复制到模块自有缓冲区；更推荐完成 Frame 解码与校验后，仅投递自有的 candidate DTO。
5. 回调返回后不得继续读取、修改或释放静态库提供的内存。
6. 上行 `rdx_protocol_custom_msg_indicate(char *cmd, char *value)` 同样只接收指针，其实际发送容量仍取决于静态库内部组包缓冲区。

示意：

```c
#define RDX_HOGP_KEYMAP_HEX_MAX_LEN 98

size_t value_len = strnlen(value, RDX_HOGP_KEYMAP_HEX_MAX_LEN + 1);
if (value_len > RDX_HOGP_KEYMAP_HEX_MAX_LEN) {
    /* 返回 MALFORMED_FRAME，不再解析 */
    return;
}

/* 在回调内解码到模块自有 candidate，不能异步保存 value 指针。 */
```

以上有界检查用于保护应用解析逻辑，但不能替代对静态库调用方容量和 `\0` 契约的确认。如果静态库在回调前已经截断或越界，应用层无法通过指针形参补救。

### 10.2 原子提交

掉电安全、VM 与 RAM 一致以及 ACK 时序属于 v1 强制行为。参考实现使用两个 VM slot；如果 SDK 已提供经过验证的等价原子记录机制，可以替代 A/B 物理布局，但必须通过相同故障注入测试。

强制事务顺序：

```text
解析到 candidate
    → 完整校验 candidate
    → 写入 VM 非活动槽，commit marker 保持未提交
    → 读回并校验 magic/schema/length/revision/payload/CRC
    → 在串行上下文中保存旧 RAM 快照并 apply candidate
    → 原子写入新槽 commit marker
    → 返回成功 ACK
```

失败处理必须满足：

- VM PREPARE 或读回失败：不修改 RAM，旧 VM slot 保持有效。
- RAM apply 失败：新槽不得 COMMIT，旧 RAM 与旧 VM 保持有效。
- commit marker 写入失败：立即把 RAM 回滚到旧快照，旧 VM slot 保持有效。
- 所有失败路径 revision 均不增加，并返回对应错误码。
- commit marker 成功但 ACK 丢失：新 VM 与新 RAM 已生效；APP 重试时按 revision 和 keymap CRC 幂等返回成功。

掉电语义必须为：

- RAM apply 前掉电：启动时加载旧 committed slot。
- RAM apply 后、commit marker 前掉电：RAM 状态随复位消失，启动时仍加载旧 committed slot。
- commit marker 后掉电：启动时加载新 committed slot。

### 10.3 VM ABI v1

VM 不保存 HEX 字符串、完整 wire Frame 或 executor C struct。每个 slot 保存独立的 55-byte `HOGP_KEYMAP_VM_V1` 记录；所有多字节字段使用小端序。

| 偏移 | 字段 | 长度 | v1 值/说明 |
|---:|---|---:|---|
| 0 | `magic` | 4 | ASCII `HKM1`：`48 4B 4D 31` |
| 4 | `schema_version` | 1 | `0x01` |
| 5 | `reserved` | 1 | 必须为 `0x00` |
| 6 | `payload_len` | 2 | `0x0023`，即 35 |
| 8 | `revision` | 4 | committed 配置版本，必须大于 0 |
| 12 | `keymap_payload` | 35 | 第 6 节 canonical keymap |
| 47 | `record_crc32` | 4 | 覆盖偏移 0–46 |
| 51 | `commit_marker` | 4 | ASCII `CMIT`：`43 4D 49 54` |

slot 判定规则：

1. 只有 magic、schema、length、record CRC 和 commit marker 全部正确的 slot 才有效。
2. 两个 slot 都有效时，选择 revision 较大的记录。
3. 两个有效 slot revision 相同且 payload 相同，可以任选；revision 相同但 payload 不同视为 VM 冲突，不得任意选择，应进入默认安全状态并上报诊断。
4. 新记录先写偏移 0–50并读回校验；RAM apply 成功后最后写 commit marker。
5. 新槽提交成功后，不立即破坏旧槽；下一次更新才复用较旧 slot。
6. 如果底层 VM API 不允许单独原子写 commit marker，必须使用 SDK 提供的等价事务或独立原子 active metadata，不能假设一次普通 VM 写天然掉电安全。
7. `record_crc32` 与 wire Frame CRC32 作用域不同：前者保护持久化记录，后者保护本次 custom Frame。

### 10.4 并发与按键释放

BLE 收包任务与物理按键处理可能不在同一任务上下文。正式 apply 不应直接在任意协议回调中无保护地覆盖 active keymap。

v1 必须保证配置 apply、release timer 和物理按键执行串行化。推荐使用 `app_core` 或 executor 所属单一任务；如果使用锁或临界区替代，必须证明不会在 BLE 回调或 timer 上下文造成阻塞和死锁。

1. 将已校验 candidate 投递到 `app_core` 或 executor 所属串行上下文。
2. 切换 keymap 前取消旧 release timer，并尽力发送一次全零释放 Report。
3. 原子替换 active keymap。
4. 新的物理 CLICK 只读取完整的新 keymap。

### 10.5 owner 与鉴权

只有满足以下条件时才接受写配置：

- 当前连接 owner 为 RDX Config；
- 连接处于产品允许的绑定/鉴权状态；
- 当前不处于 OTA、关机或其他互斥事务；
- HOGP owner 连接不得通过 HID 属性修改生产 keymap。

CRC 只能防止传输或存储损坏，不能代替鉴权。

connection owner 已有明确实现：RDX Config 特征写入在进入 `rdx_protocol_packet_recv()` 前会检查 `rdx_ble_connection_owner_get() == RDX_BLE_OWNER_CONFIG`。custom handler 内必须再次检查 owner 作为纵深防御，但 owner 查询本身不是阻塞点。量产前真正需要补齐的是绑定/鉴权状态的可调用接口和失败返回路径。

## 11. MTU 与分包要求

示例 SET Frame 为 49 bytes，HEX 后为 98 字符，完整外层命令为 118 字符；GET_KEYMAP_RSP 完整外层命令为 120 字符。相比原 51-byte keymap 设计，精简后的命令更有机会直接通过常见的 128-byte 字符串缓冲区，但仍必须实测，不能由该估算推定静态库容量。

### 11.1 ATT_MTU 与 ATT Value 载荷来源

`ATT_EVENT_MTU_EXCHANGE_COMPLETE` 表示 ATT MTU Exchange 已完成。固件通过：

```c
att_event_mtu_exchange_complete_get_MTU(packet)
```

取得本次连接协商出的标准 ATT_MTU。当前代码随后立即减去 3：

```c
u16 mtu = att_event_mtu_exchange_complete_get_MTU(packet) - 3;
```

对于承载 RDX 数据的 ATT Write Request/Command，其 PDU 预算为：

```text
ATT opcode       1 byte
Attribute Handle 2 bytes
Attribute Value  ATT_MTU - 3 bytes
```

所以当前日志：

```text
===== ATT MTU = 514
```

实际表示：

```text
协商 ATT_MTU = 517
MTU 推导出的单次 ATT Value 预算 = 514
```

日志标签沿用了 `ATT MTU`，但变量实际已经是减去 Header 后的 payload 长度。该值随后传给 `ble_op_att_set_send_mtu()`，并保存到 `g_rdx_ble_server_info.ble_mtu_size`。

需要注意：

- 517 是本次连接的协商结果，不是所有手机和每次连接的固定值；
- 514 是 MTU 推导出的 ATT Value 字段预算，不保证上层一定能使用全部空间；
- 如果 RDX packet 在 ATT Value 内还有包头、校验或分片字段，业务命令的单包上限还要扣除这些开销；
- 特征值实现、SDK 回调和 RDX 静态库仍可能设置比 514 更小的限制。

### 11.2 与当前阻塞点的关系

当前 118-byte SET 小于本次连接的 514-byte ATT Value 预算，只能得出：

> BLE/ATT 这一层在本次连接中不是已知瓶颈。

它不能证明 118 字节一定能够完整到达 `rdx_app_custom_command_parse()`，也不能证明 120 字节 GET 响应一定能由 `rdx_protocol_custom_msg_indicate()` 完整发送。还必须确认 RDX packet 与静态库 custom 层没有更小的缓冲区或长度限制。

如果静态库采用原始收包内存就地切分，回调指针可能直接指向 118-byte 命令中的相应位置；如果静态库先复制到固定数组，数组大小就可能成为更小的瓶颈。两种情况下回调形参都是 `char *`，不能由函数签名判断。

### 11.3 正式接入前的容量确认

正式实现前必须同时确认以下三层限制：

| 层级 | 必须确认的内容 | 本方案最低要求 |
|---|---|---|
| BLE/ATT | 当前连接的有效 Write payload | SET 至少 118 bytes，或存在可靠分片 |
| RDX packet | 单包上限、接收缓存、分片重组 | 能向 custom 层交付完整 118-byte 命令 |
| RDX custom | 完整命令、cmd、value、上行组包缓存 | 下行 118 chars；上行 120 chars；另含 `\0` |

其中 BLE/ATT 层已在当前日志中观察到 514-byte 有效 payload；RDX packet 和 custom 层仍是待确认项。确认方式应同时包含：

1. 向协议库负责人获取接口和内部缓冲区的明确长度约束。
2. 在当前实际固件库上发送长度阶梯测试命令。
3. 在 `rdx_app_custom_command_parse()` 使用协议上限执行 `strnlen(value, max + 1)`，并记录实际长度和内容 CRC。
4. 使用 `rdx_protocol_custom_msg_indicate()` 回显同长度 value，验证上行链路。
5. 对 118-byte SET 和 120-byte GET 响应做最终端到端验证。

只有上述下行和上行均通过，才能关闭第 1.1 节的容量阻塞点。

正式实现仍须遵守：

1. APP 等待 MTU 协商完成后再发送。
2. 如果有效 ATT payload 小于完整命令长度，必须由现有 RDX packet 层做长度驱动的分片与重组。
3. `rdx_protocol_handle_custom_cmd()` 只能接收重组后的完整外层命令。
4. 不允许依赖字符串结尾猜测分片完成；必须使用 RDX 外层包长度或明确的重组状态。
5. 设置合理的最大 HEX Frame 长度，防止异常 APP 导致缓冲区溢出。

## 12. VM 启动行为

正式版本关闭 `RDX_HOGP_KEY_ACTION_TEST_ENABLE` 后，启动顺序应为：

```text
读取 VM 有效槽
    → 校验 magic/schema/length/revision/CRC
    → 转换为 executor RAM keymap
    → apply
```

如果 VM 不存在或损坏：

- 使用产品明确规定的默认 keymap，并令当前 revision 为 0；
- 不得加载测试 keymap；
- GET_KEYMAP 必须返回默认 keymap、`revision=0` 及其确定的 keymap CRC；
- 默认表只驻留于固件常量，不要求为了 revision 0 自动写 VM；
- 首次成功 SET 写入 revision 1；RESET 写入默认 payload，但 revision 在当前值基础上加 1。

### 12.1 T2620 出厂默认 keymap 决策

量产默认值属于产品输入，必须在冻结协议和关闭测试 keymap 前填写下表：

| 产品键 | 默认 modifiers | 默认 usages[6] | 状态 |
|---|---:|---|---|
| KEY1 | `TBD` | `TBD` | 待产品确认 |
| KEY2 | `TBD` | `TBD` | 待产品确认 |
| KEY3 | `TBD` | `TBD` | 待产品确认 |
| KEY4 | `TBD` | `TBD` | 待产品确认 |
| KEY5 | `TBD` | `TBD` | 待产品确认 |

本文第 8 节的 Ctrl+C/Ctrl+V/Ctrl+X/Backspace/Enter 仅是协议测试向量，不是量产默认表。以上五项仍为 `TBD` 时不得把正式配键功能标记为量产完成。

## 13. 测试要求

正式接入至少增加以下 host 和设备验证：

### 13.1 Frame 测试

- 本文 SET/GET/响应测试向量逐字节一致；
- 奇数长度 HEX、非法字符、截断 Frame 被拒绝；
- 错误 `payload_len` 被拒绝；
- 错误 CRC 被拒绝；
- 未知 version/opcode 返回明确错误。

### 13.2 Keymap 校验

- payload 长度不是 35；
- 五个固定位置与 KEY1–KEY5 映射错误；
- Usage 中间出现 0 后又出现非零值；
- 非零尾随 usage；
- 重复 usage；
- Modifier Usage 被错误放入 usages；
- 七字节全零条目被正确识别为 disabled 且不发送 Report；
- modifier-only 条目被正确识别并发送；
- Ctrl+C、Ctrl+V、Ctrl+X、Backspace、Enter 转换正确。

### 13.3 事务测试

- 五键整表成功后一次性生效；
- VM 写失败时 RAM 与 revision 不变；
- apply 失败时新槽不提交且不返回成功；
- commit marker 写失败时 RAM 回滚；
- PREPARE、apply、COMMIT 三个掉电窗口分别恢复预期 slot；
- 重启后 keymap、revision 和 CRC 保持一致；
- 相同 request ID 重试不重复写 VM；
- 幂等缓存仅保留最后一条 SET/RESET，断连后清除；
- 相同 request ID、不同 Frame CRC 的并发请求返回冲突；
- 旧 revision 写入返回冲突；
- 相同内容重复写按幂等成功处理；
- revision 0、首次 SET revision 1、RESET 递增语义正确；
- revision `0xFFFFFFFF` 时拒绝继续写入，不发生回绕；
- 双 slot 同 revision 不同 payload 被识别为冲突。

### 13.4 BLE 与模式测试

- Config owner 可以 GET/SET；
- HOGP owner 拒绝配置写入；
- 未绑定/未鉴权连接按产品策略拒绝写入；
- SET 成功 ACK 发出前不切换模式；
- ACK 丢失后 APP 重试可以获得相同结果；
- 切换 HOGP 后五个物理键发送新配置；
- 每次按键后均发送全零释放 Report，无卡键。

## 14. v1 明确不包含的能力

以下能力不进入本协议 v1：

- Macro 序列；
- Layer；
- Consumer Control；
- Mouse Report；
- hold-tap；
- 双击、长按等多触发器配置；
- APP 直接下发任意 8-byte 原始 Report；
- APP 直接下发或覆盖 HID Report Map。

需要上述能力时应新增 Frame version 或新的 action schema，不得改变 v1 字段的既有含义。

## 15. 最终约定摘要

```text
固定入口：      *APP#custom#
固定命令名：    hogpkm
唯一 value：    版本化 HEX Frame
键值标准：      USB HID Keyboard Usage + modifier bitmap
更新粒度：      完整五键 keymap
并发控制：      request_id + revision
完整性：        payload length + CRC32
可靠性：        ACK + 幂等重试 + GET 回读
持久化：        独立 VM ABI + 原子提交
payload：       5 × [modifier + usages[6]] = 35 bytes
运行时：        显式转换后调用 Key Action executor
```

该方案不改变现有 RDX `custom` 入口，同时解决初步格式中键值歧义、多参数解析、部分更新、超时重发、版本升级和掉电恢复问题，适合作为 T2620 HOGP 正式配键协议的 v1 基线。
