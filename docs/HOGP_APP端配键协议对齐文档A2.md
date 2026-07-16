# HOGP APP 端配键协议对齐文档

> 面向对象：APP 产品、客户端开发、固件开发、测试人员
> 设备范围：T2620 / JL AC701N（BR28）/ RDX BLE / HOGP Keyboard
> 对齐版本：v1 Draft
> 固定入口：`*APP#custom#...#`
> 详细固件设计：[HOGP_APP按键键值下发方案.md](./HOGP_APP按键键值下发方案.md)

## 1. 文档目的

本文只定义 APP 与设备之间的配键协议，不描述设备内部 VM、任务、定时器或 Flash 实现。

APP 侧需要完成：

1. 在 RDX Config 连接下查询设备能力和当前五键配置。
2. 将用户选择的按键转换成标准 USB HID Keyboard Usage。
3. 每次提交完整五键 keymap，不逐键提交。
4. 按本文定义生成 HEX Frame，并通过现有 `custom` 入口下发。
5. 校验设备响应的 version、opcode、request ID、revision、长度和 CRC。
6. 正确处理超时重试、revision 冲突、错误码和断线重连。

设备侧需要保证：

1. 完整校验 Frame 和五键 keymap。
2. 五键配置全部成功或全部不生效。
3. 成功持久化并切换运行配置后才返回成功响应。
4. 支持 GET 回读、RESET、幂等重试和 revision 冲突保护。

## 2. 协议结论

APP 下行格式固定为：

```text
*APP#custom#hogpkm#<HEX_FRAME>#
```

设备上行格式固定为：

```text
*DEV#custom#hogpkm#<HEX_FRAME>#
```

其中：

```text
cmd   = hogpkm
value = HEX_FRAME
```

`HEX_FRAME` 内部不得再出现 `#`。

五个物理键的配置一次性完整下发：

```text
KEY1 + KEY2 + KEY3 + KEY4 + KEY5
```

每个键固定 7 字节：

```text
[modifier, usage1, usage2, usage3, usage4, usage5, usage6]
```

完整 keymap 固定为：

```text
5 × 7 = 35 bytes
```

## 3. 连接和调用前提

APP 只能在设备处于 RDX Config 身份并建立 Config owner 连接时下发配键命令。

APP 必须遵守：

1. 完成现有 RDX 连接、绑定和鉴权流程。
2. 等待 RDX custom 写通道进入可写状态。
3. 完成通知订阅，确保能够收到 `*DEV#custom#hogpkm#...#` 响应。
4. HOGP owner 连接下不得发送配键命令。
5. 配键成功后不得根据断连推测成功，必须以成功响应或 GET 回读结果为准。
6. 配键成功不会自动切换 HOGP 模式；模式切换使用现有独立流程，并在成功响应后执行。

推荐交互顺序：

```text
连接 RDX Config
    → GET_CAPS
    → GET_KEYMAP
    → 用户编辑本地完整五键副本
    → SET_KEYMAP
    → 校验成功响应
    → 可选 GET_KEYMAP 回读
    → 使用独立命令切换 HOGP 模式
```

## 4. HEX Frame 通用格式

所有多字节整数使用小端序。

| 偏移 | 字段 | 类型 | 长度 | APP 要求 |
|---:|---|---|---:|---|
| 0 | `version` | `u8` | 1 | v1 固定为 `0x01` |
| 1 | `opcode` | `u8` | 1 | 见第 5 节 |
| 2 | `request_id` | `u16 LE` | 2 | APP 生成的请求流水号 |
| 4 | `revision` | `u32 LE` | 4 | SET/RESET 使用当前配置版本 |
| 8 | `payload_len` | `u16 LE` | 2 | payload 字节数 |
| 10 | `payload` | `u8[]` | N | 操作对应的数据 |
| 10+N | `crc32` | `u32 LE` | 4 | Header 与 payload 的 CRC32 |

二进制 Frame 总长度：

```text
frame_len = 10 + payload_len + 4
```

APP 解析响应时必须满足：

```text
HEX 字符数为偶数
decoded_len >= 14
decoded_len == 10 + payload_len + 4
Frame CRC32 正确
```

### 4.1 HEX 编码

1. 一个二进制字节编码成两个十六进制字符。
2. APP 发送时使用大写 `0-9A-F`。
3. HEX 内不得包含空格、逗号、换行、`0x` 或 `#`。
4. Frame 先按小端序生成二进制，再整体转成 HEX；不能把整数的显示字符串直接拼接。

示例：

```text
request_id = 42 = 0x002A
线路字节   = 2A 00
HEX         = 2A00
```

### 4.2 CRC32

采用 CRC-32/ISO-HDLC：

```text
poly   = 0x04C11DB7
refin  = true
refout = true
init   = 0xFFFFFFFF
xorout = 0xFFFFFFFF
```

等价的 reflected polynomial 为 `0xEDB88320`。

CRC 覆盖：

```text
version 至 payload 最后一个字节
```

CRC 不包含 CRC 字段本身，计算结果按小端序写入 Frame。

## 5. Opcode 和 payload 长度

| Opcode | 名称 | 方向 | 成功 payload | 失败 payload |
|---:|---|---|---:|---:|
| `0x01` | `SET_KEYMAP` | APP → DEV | 35 bytes | 不适用 |
| `0x02` | `GET_KEYMAP` | APP → DEV | 0 bytes | 不适用 |
| `0x03` | `GET_CAPS` | APP → DEV | 0 bytes | 不适用 |
| `0x04` | `RESET_KEYMAP` | APP → DEV | 0 bytes | 不适用 |
| `0x81` | `SET_KEYMAP_RSP` | DEV → APP | 5 bytes | 1 byte |
| `0x82` | `GET_KEYMAP_RSP` | DEV → APP | 36 bytes | 1 byte |
| `0x83` | `GET_CAPS_RSP` | DEV → APP | 5 bytes | 1 byte |
| `0x84` | `RESET_KEYMAP_RSP` | DEV → APP | 5 bytes | 1 byte |

响应 opcode 与请求对应：

```text
response_opcode = request_opcode | 0x80
```

APP 不得发送本文未定义的 opcode。

## 6. 五键 Keymap 数据

### 6.1 固定布局

`SET_KEYMAP` payload 固定为 35 字节：

| payload 字节范围 | 产品键 | 内容 |
|---:|---|---|
| 0–6 | KEY1 | `modifier + usages[6]` |
| 7–13 | KEY2 | `modifier + usages[6]` |
| 14–20 | KEY3 | `modifier + usages[6]` |
| 21–27 | KEY4 | `modifier + usages[6]` |
| 28–34 | KEY5 | `modifier + usages[6]` |

APP 不传 GPIO、`KEY_IO_NUMx`、key count、key ID、action type 或 usage count。

APP 必须始终发送五个条目。即使用户只修改 KEY1，也必须将修改后的完整 35-byte keymap 重新提交。

### 6.2 Modifier

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

组合 modifier 使用按位或：

```text
Ctrl+Shift = 0x01 | 0x02 = 0x03
```

### 6.3 Usage

APP 必须使用 USB HID Keyboard/Keypad Usage Page `0x07` 中的 Usage ID。

不得发送：

- ASCII/Unicode code point；
- Windows Virtual-Key；
- JavaScript legacy keyCode；
- Android KeyEvent code；
- iOS 平台键值。

例如十进制 `112` 在 Windows VK 中表示 F1，但作为 HID Usage `0x70` 表示 F21，不能直接透传。

常用 HID Usage：

| 按键 | HID Usage |
|---|---:|
| A | `0x04` |
| C | `0x06` |
| S | `0x16` |
| V | `0x19` |
| X | `0x1B` |
| Enter | `0x28` |
| Escape | `0x29` |
| Backspace | `0x2A` |
| Tab | `0x2B` |
| Space | `0x2C` |
| F1 | `0x3A` |
| Delete | `0x4C` |

完整按键表由 APP 侧统一维护，固件侧按同一 HID Usage 白名单校验。

### 6.4 Usage 排列规则

1. 每键最多 6 个普通 Usage。
2. 有效 Usage 必须从 `usages[0]` 开始连续排列。
3. 遇到第一个 `0x00` 后，后续 Usage 必须全部为零。
4. 有效 Usage 不得重复。
5. `0x01..0x03` 是 HID 错误码，不得作为配置值。
6. `0xE0..0xE7` 是 modifier，必须放入 modifier 位图，不得放入 usages。
7. 超过 6 个普通键的组合必须由 APP UI 拒绝。

### 6.5 单键、组合键和禁用示例

| 用户配置 | 7-byte Entry |
|---|---|
| A | `00 04 00 00 00 00 00` |
| Enter | `00 28 00 00 00 00 00` |
| Ctrl+C | `01 06 00 00 00 00 00` |
| Ctrl+Shift+S | `03 16 00 00 00 00 00` |
| Ctrl+Alt+Delete | `05 4C 00 00 00 00 00` |
| A+B 同时按下 | `00 04 05 00 00 00 00` |
| 单独 Left Ctrl | `01 00 00 00 00 00 00` |
| Disabled | `00 00 00 00 00 00 00` |

该协议表达的是“一次同时按下并整体释放”的静态组合键，不支持有时间顺序的按键序列。

## 7. revision 规则

revision 是设备当前持久化 keymap 的版本。

| 场景 | APP 填写的 revision |
|---|---:|
| GET_CAPS | 0 |
| GET_KEYMAP | 0 |
| 首次 SET | GET 返回的 0 |
| 后续 SET | 最近一次 GET/SET 成功得到的 revision |
| RESET | 最近一次 GET/SET 成功得到的 revision |

规则：

1. `revision=0` 表示设备尚无用户提交配置，当前使用产品出厂默认表。
2. 首次 SET 成功后 revision 变为 1。
3. 每次成功 SET 或 RESET，revision 加 1。
4. RESET 恢复默认内容，但 revision 不回到 0。
5. APP 不得自行递增 revision；只能使用设备响应返回的值。
6. APP 不得使用特殊 revision 强制覆盖设备配置。
7. 收到 `REVISION_CONFLICT` 后必须重新 GET，不得继续重试旧 SET。

APP 不得硬编码出厂默认 keymap。页面打开时始终以 GET_KEYMAP 返回值为准。

## 8. request ID、超时和重试

### 8.1 request ID

1. 每个新请求分配一个 `u16 request_id`。
2. 同时只允许一个 HOGP keymap 请求处于 pending 状态。
3. pending 请求完成前不得复用 request ID。
4. 超时重试必须重发完全相同的 Frame，包括 request ID、revision、payload 和 CRC。
5. 新业务请求使用新的 request ID。

推荐使用单调递增的 `u16` 计数器，回绕时跳过当前 pending ID。

### 8.2 超时策略

v1 联调基线：

```text
响应超时：5000 ms
最大重试：2 次
总发送次数：最多 3 次
```

超时重试必须使用原 Frame。不能只重新生成“语义相同”的新 Frame，因为新 request ID 或不同 CRC 会失去幂等关联。

连续三次无响应时：

```text
断开或重建 RDX Config 连接
    → GET_KEYMAP
    → 比较 revision 和 keymap CRC
    → 判断之前的 SET 是否已经成功
```

### 8.3 显式错误后的处理

| 值 | Status | APP 处理 |
|---:|---|---|
| `0x00` | `OK` | 接受响应并更新 revision |
| `0x01` | `UNSUPPORTED_VERSION` | 停止配键并提示固件不兼容 |
| `0x02` | `UNSUPPORTED_OPCODE` | 视为 APP/固件协议版本不一致 |
| `0x03` | `MALFORMED_FRAME` | 记录原始 Frame，修复 APP 组包逻辑 |
| `0x04` | `CRC_ERROR` | 使用完全相同 Frame 重试 |
| `0x05` | `REVISION_CONFLICT` | GET_KEYMAP 后重新应用用户修改 |
| `0x06` | `INVALID_KEYMAP` | 修复长度、固定位置或零填充 |
| `0x07` | `INVALID_HID_USAGE` | 阻止非法键值并修复映射表 |
| `0x08` | `STORAGE_ERROR` | 可重试一次，仍失败则提示设备异常 |
| `0x09` | `NOT_AUTHORIZED` | 停止重试，重新完成 Config 连接/鉴权 |
| `0x0A` | `BUSY` | 延时 500 ms 后使用相同 Frame 重试 |
| `0x0B` | `INTERNAL_ERROR` | 停止写入并记录日志 |
| `0x0C` | `REQUEST_ID_CONFLICT` | 等待旧请求结束，再使用新 request ID 发起新请求 |

Frame 短到无法取得 request ID 时，设备可能静默丢弃；APP 按超时策略处理。

## 9. 响应校验要求

APP 收到任何 `hogpkm` 响应时必须依次校验：

1. 外层命令是 `*DEV#custom#hogpkm#...#`。
2. HEX 合法且长度为偶数。
3. Frame version 为 `1`。
4. opcode 是 pending 请求对应的响应 opcode。
5. request ID 与 pending 请求一致。
6. `decoded_len == 10 + payload_len + 4`。
7. Frame CRC32 正确。
8. payload 长度符合第 5 节矩阵。
9. status 为已定义值。
10. 成功 SET/RESET 返回的 keymap CRC 与 APP 本地 35-byte payload CRC 一致。

任何一步失败都不得把配置标记为成功。

## 10. 各命令要求

### 10.1 GET_CAPS

APP 在首次连接或检测到固件版本变化时必须发送 GET_CAPS。

请求：

```text
opcode      = 0x03
revision    = 0
payload_len = 0
```

成功响应 payload：

```text
u8 status;             // 0x00
u8 max_physical_keys;  // 5
u8 max_usages;         // 6
u8 action_mask;        // bit0=disabled, bit1=keyboard
u8 modifier_mask;      // 0xFF
```

APP 必须检查：

```text
max_physical_keys >= 5
max_usages >= 6
(action_mask & 0x03) == 0x03
modifier_mask == 0xFF
```

### 10.2 GET_KEYMAP

请求：

```text
opcode      = 0x02
revision    = 0
payload_len = 0
```

成功响应 payload：

```text
u8 status;       // 0x00
u8 keymap[35];   // KEY1..KEY5
```

APP 保存：

- Frame Header 中的 revision；
- 完整 35-byte keymap；
- keymap CRC32；
- 对应固件版本。

### 10.3 SET_KEYMAP

请求：

```text
opcode      = 0x01
revision    = APP 最近读取到的 revision
payload_len = 35
payload     = 完整 KEY1..KEY5
```

成功响应 payload：

```text
u8  status;         // 0x00
u32 keymap_crc32;   // APP 提交的 35-byte payload CRC，小端
```

APP 只有在以下全部成立时才能显示“保存成功”：

- status 为 OK；
- request ID 匹配；
- 正常成功或同一提交的幂等重试中，响应 revision 应等于请求 revision + 1；如果返回更高 revision，APP 必须 GET_KEYMAP 对账后再决定是否显示成功；
- keymap CRC 与本地 payload 一致；
- Frame CRC 正确。

### 10.4 RESET_KEYMAP

请求：

```text
opcode      = 0x04
revision    = APP 最近读取到的 revision
payload_len = 0
```

成功响应 payload：

```text
u8  status;         // 0x00
u32 keymap_crc32;   // 产品默认 keymap CRC，小端
```

RESET 成功后 APP 必须执行 GET_KEYMAP，使用设备返回的默认表刷新 UI。APP 不得自行恢复硬编码默认值。

## 11. 完整联调测试向量

以下向量是双方共同的 v1 基准。

测试 keymap：

| Key | 动作 | Entry |
|---|---|---|
| KEY1 | Ctrl+C | `01 06 00 00 00 00 00` |
| KEY2 | Ctrl+V | `01 19 00 00 00 00 00` |
| KEY3 | Ctrl+X | `01 1B 00 00 00 00 00` |
| KEY4 | Backspace | `00 2A 00 00 00 00 00` |
| KEY5 | Enter | `00 28 00 00 00 00 00` |

35-byte payload HEX：

```text
0106000000000001190000000000011B0000000000002A000000000000280000000000
```

payload CRC32：

```text
0xC1142483
```

### 11.1 SET_KEYMAP

条件：

```text
request_id = 42
revision   = 7
```

APP 下发：

```text
*APP#custom#hogpkm#01012A000700000023000106000000000001190000000000011B0000000000002A000000000000280000000000402956B8#
```

完整命令长度：118 字符，不含 C 字符串结尾 `\0`。

设备成功响应，revision 变为 8：

```text
*DEV#custom#hogpkm#01812A0008000000050000832414C130E7EB13#
```

### 11.2 GET_KEYMAP

APP 下发：

```text
*APP#custom#hogpkm#01022B00000000000000CE55A286#
```

设备响应：

```text
*DEV#custom#hogpkm#01822B00080000002400000106000000000001190000000000011B0000000000002A0000000000002800000000003311346E#
```

完整响应长度：120 字符，不含 C 字符串结尾 `\0`。

### 11.3 GET_CAPS

APP 下发：

```text
*APP#custom#hogpkm#01032C0000000000000094481C9B#
```

设备响应：

```text
*DEV#custom#hogpkm#01832C0008000000050000050603FF141E7965#
```

## 12. MTU 与发送要求

当前设备日志显示：

```text
协商 ATT_MTU = 517
ATT Write Value 预算 = 517 - 3 = 514 bytes
```

本协议最大基线命令：

```text
APP SET       = 118 chars
DEV GET RSP   = 120 chars
```

在当前连接上可单次承载，但 APP 仍必须：

1. 等待 MTU 协商和 RDX 写通道 ready。
2. 使用现有 RDX packet API 发送完整 custom 命令。
3. 不得自行把字符串随意拆成多个 GATT Write，除非现有 RDX API 明确提供重组能力。
4. 联调时验证 64、96、128、150、180 字节 custom value 的上下行完整性。
5. 至少确认 118-byte SET 和 120-byte GET 响应端到端通过。

注意：514 只是本次连接的 ATT Value 预算，RDX 静态库内部缓冲区可能更小。APP 与固件必须以端到端实测结果为准。

## 13. APP 本地数据模型建议

APP 内部可以使用可读模型，但发包前必须转换为固定 35-byte payload。

示例：

```text
KeyBinding {
    enabled: boolean
    modifiers: uint8
    usages: uint8[0..6]
}

Keymap {
    revision: uint32
    keys: KeyBinding[5]
}
```

编码规则：

```text
enabled=false
    → 00 00 00 00 00 00 00

enabled=true
    → modifiers + 最多 6 个 usages + 尾部补零
```

APP 不得直接把语言结构体、JSON、平台 KeyCode 或数据库对象序列化后发送。

## 14. v1 不支持的功能

- 有时间顺序的 Macro；
- Layer；
- Consumer Control，如音量、多媒体播放键；
- Mouse Report；
- 双击、长按等触发器配置；
- hold-tap；
- APP 自定义 HID Report Map；
- 超过 6 个普通键同时按下；
- APP 直接发送任意 8-byte 原始 Keyboard Report。

APP UI 不得暴露上述能力，避免生成设备无法执行的配置。

## 15. 双方联调验收清单

### 15.1 APP 必须通过

- CRC32 算法与测试向量一致；
- 小端字段编码正确；
- 单键、modifier-only、多键组合和 disabled 编码正确；
- APP 只发送完整 35-byte keymap；
- SET/GET/CAPS 测试向量逐字节一致；
- 响应 opcode、request ID、revision、length、status 和 CRC 均校验；
- 超时使用相同 Frame 重试；
- revision conflict 后重新 GET；
- RESET 后 GET 回读，不使用硬编码默认表；
- Config/HOGP 模式切换不依赖 SET 的断连现象判断。

### 15.2 固件必须通过

- 118-byte SET 完整进入 custom handler；
- 120-byte GET 响应完整到达 APP；
- 错误长度、CRC、Usage 和 revision 被拒绝；
- 相同请求重试不重复写配置；
- 五键配置全部成功或全部不生效；
- 重启后 GET 返回相同 revision 和 keymap；
- Config owner 可配键，HOGP owner 和未授权连接被拒绝；
- 每次 HOGP 按键都有全零释放，无卡键。

### 15.3 当前待双方确认

1. T2620 量产出厂五键默认值；测试向量不等于产品默认值。
2. 现有 RDX custom 上下行最大长度实测结果。
3. APP 当前绑定/鉴权完成状态与固件查询接口的对应关系。
4. 5000 ms 超时、最多 2 次重试是否与 APP 公共 RDX 请求框架一致。

以上四项关闭后，双方冻结 v1 Frame、HID 映射表和交互状态机。
