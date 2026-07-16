# VibeCoding Keyboard 产品技术栈方案

## 1. 文档目的

本文档用于定义 **VibeCoding Keyboard** 的产品技术栈、蓝牙协议选型、工作模式、连接策略、风险控制和后期扩展方向。

当前产品形态为：

- 设备端包含 5 个物理按键；
- 设备作为 HID mini keyboard 接入 PC；
- 用户可通过复用现有 RDX BLE App 通道，自定义 5 个按键的键值、组合键、宏或层功能；
- 设备包含麦克风，作为语音采集入口；
- 麦克风音频传递给 PC 后，由 PC 侧完成语音转文字，并将文本输入到当前聚焦输入处。

本文档重点解决以下技术问题：

- 键盘、App 配置、麦克风分别应采用什么协议；
- BLE HOGP、BLE App、Classic HFP 是否需要并发；
- 第一版量产方案如何降低实现风险；
- 后期客户要求热配置时如何扩展。

---

## 2. 产品功能概述

### 2.1 核心功能

| 功能模块 | 功能描述 |
|---|---|
| 5 键 mini keyboard | 通过 5 个物理按键模拟键盘输入 |
| 自定义键值 | 通过 BLE App 修改每个按键对应的键值、组合键、宏或层映射 |
| HID 输入 | 作为键盘类 HID 设备接入 PC |
| 麦克风输入 | 作为语音采集入口，将音频传递给 PC |
| 语音转文字 | 由 PC 侧软件或系统语音能力完成 STT，并输入到聚焦窗口 |

### 2.2 关键设计原则

1. **键盘输入稳定性优先**  
   键盘是产品主功能，HID 输入不能因 App 配置或音频链路受到明显影响。

2. **当前版本避免复杂并发**  
   第一版不建议同时支持 PC BLE HOGP、手机 BLE App、Classic HFP 三方并发。

3. **App 配置与键盘工作模式分离**  
   App 配置仅在设备未连接 PC 键盘时开放，降低 BLE 多 Central 连接风险。

4. **麦克风不使用 HID 模拟**  
   HID 适合键盘、鼠标、Consumer Control 等输入报告，不适合麦克风音频输入。麦克风应走蓝牙音频协议或 USB Audio 类协议。

5. **语音转文字由 PC 侧完成**  
   设备只负责采集和传输音频；语音识别、文本注入、聚焦窗口输入由 PC Agent 或系统语音能力完成。

6. **RDX BLE App 底座全盘复用**  
   BLE App/RDX 连接、鉴权、收发、OTA、基础状态和既有业务协议继续作为配置底座。VibeCoding Keyboard 只在该底座上新增按键设置、Keymap/Macro/Layer 配置命令，不另起一套 BLE App 协议栈。

### 2.3 五个物理按键的工作规则

五个物理按键根据电脑键盘连接状态自动切换用途：

| 产品状态 | 按键行为 |
|---|---|
| 已与电脑建立键盘连接 | 执行用户设置的自定义键值、组合键或其他已配置功能 |
| 未与电脑建立键盘连接 | 执行产品原有的离线按键功能 |

该规则适用于出厂默认配置和用户通过 App 修改后的配置。手机 App 是否已连接，不改变五个物理按键的工作方式。

电脑键盘连接建立后，五个按键只执行自定义功能，不得同时触发离线功能。某个按键未配置、被禁用或自定义功能暂时无法执行时，该按键可以无动作，但不能自动改为执行离线功能，避免产生用户未预期的操作。

电脑键盘断开后，五个按键从下一次操作开始恢复离线功能。设备处于键盘等待连接状态时，也按未连接电脑处理，保证产品离线状态下仍可正常使用。

---

## 3. 推荐总体技术架构

### 3.1 当前推荐架构

```text
VibeCoding Keyboard
├── BLE Peripheral
│   ├── HOGP / HID over GATT：五键键盘输入
│   └── RDX BLE App GATT：复用现有 App 通道，扩展按键设置
│
├── Classic Bluetooth
│   └── HFP Hands-Free Profile：麦克风音频输入
│
├── 本地存储
│   └── Keymap / Macro / Layer 配置保存到 Flash
│
└── PC 侧
    ├── 系统识别 BLE Keyboard
    ├── 系统识别 Bluetooth Microphone / Headset
    └── PC Agent 或系统语音输入完成 STT 和文本注入
```

### 3.2 推荐协议栈

| 模块 | 推荐技术 | 说明 |
|---|---|---|
| 键盘输入 | BLE HOGP | 低功耗，适合键盘类 HID 输入 |
| App 配置 | RDX BLE App GATT 复用 | 保留现有 RDX 连接、鉴权、收发和 OTA 能力，只扩展按键设置 |
| 麦克风音频 | Classic Bluetooth HFP | 当前 PC 兼容性相对更稳 |
| 后期音频增强 | LE Audio | 可作为后续高端版本预研 |
| 语音识别 | PC Agent / 系统 STT | 设备端不直接负责语音识别 |
| 文本注入 | PC Agent | 将识别文本输入到当前聚焦窗口 |

---

## 4. 蓝牙协议选型说明

### 4.1 键盘部分：BLE HOGP

键盘部分建议使用 **BLE HID over GATT Profile，简称 HOGP**。

原因：

- 适合键盘、鼠标、遥控器等 HID 设备；
- 功耗低，适合小型电池设备；
- 五个按键的数据量极小，BLE 带宽完全足够；
- 可直接被 Windows、macOS、Linux 等系统识别为键盘类输入设备；
- 与 RDX BLE App GATT 服务共存在同一 BLE GATT Server 中。

典型服务组成：

```text
BLE GATT Server
├── HID Service
├── Battery Service
├── Device Information Service
└── RDX BLE App Service（含 VibeCoding 按键设置扩展）
```

需要注意的是，虽然 HOGP 和 RDX BLE App GATT 可以共存在同一个 GATT Server 中，但如果它们分别连接到不同设备，例如 PC 连接 HOGP、手机连接 App，则会涉及 BLE 多 Central 并发能力，不建议第一版启用。

---

### 4.2 App 配置部分：复用 RDX BLE App

App 配置不重新设计一套独立 BLE 协议栈，而是复用现有 RDX BLE App 部分。继续沿用 RDX 的 GATT Service、连接管理、鉴权、收发队列、OTA、基础设备状态和 App 侧通信模型，在其业务命令层增加 VibeCoding Keyboard 的按键设置能力。

复用边界：

- BLE 连接、广播、鉴权、基础读写、OTA 和设备状态仍归 RDX App 底座；
- VibeCoding 只新增 Keymap/Macro/Layer/触发模式等配置命令；
- 配置命令写入 RAM 中的候选配置，校验通过后再保存到 Flash；
- HOGP 不解析 App 配置帧，不写 Flash，不直接响应 App 命令；
- Key Action Executor 从配置存储读取当前 Keymap，并生成标准 Keyboard Report 交给 HOGP。

新增按键设置内容包括：

- 单键键值；
- 组合键，例如 Ctrl + C、Ctrl + V、Alt + Tab；
- Consumer Control，例如音量、播放暂停；
- 宏命令；
- 多层 Layer；
- 按键触发模式，例如单击、双击、长按；
- 语音输入按键行为，例如按住说话、松开识别。

建议在 RDX 命令层增加的能力：

```text
RDX BLE App extension
├── Device Status：包含当前 HOGP/Config 模式和配置版本
├── Keymap Read：读取当前按键配置
├── Keymap Write：写入候选按键配置
├── Macro Write：写入宏配置
├── Layer Config：写入层配置
├── Save Config：CRC 校验后保存到 Flash
└── Exit Config Mode：保存后退出配置模式并恢复 HOGP
```

配置写入建议采用：

```text
App 写入临时 RAM 配置
↓
CRC 校验
↓
用户点击保存
↓
固件写入 Flash
↓
返回保存结果
↓
设备退出配置模式
```

---

### 4.3 麦克风部分：Classic Bluetooth HFP

麦克风部分不建议使用 HID 模拟。推荐使用 **Classic Bluetooth HFP**。

原因：

- PC 侧更容易识别为蓝牙麦克风或蓝牙耳机麦克风；
- 兼容当前主流 Windows PC 的蓝牙音频路径；
- 适合作为第一版无线语音输入方案；
- 比 LE Audio 在现有 PC 上有更成熟的兼容性基础。

需要注意：

- HFP 通常是 Headset / Hands-Free 形态，而不是单纯 Mic-only 形态；
- PC 可能同时识别出输入设备和输出设备；
- 即使产品没有扬声器，协议栈可能仍需处理下行音频；
- 音质受 HFP codec 限制，建议芯片支持 mSBC 宽带语音；
- 如果要追求更好语音质量，可在后期评估 LE Audio 或 USB Audio。

---

### 4.4 语音转文字部分：PC 侧实现

设备端麦克风只负责音频采集和音频传输。

完整的“语音输入到聚焦窗口”体验需要 PC 侧能力：

```text
设备麦克风采集音频
↓
蓝牙 HFP 传递给 PC
↓
PC Agent 获取音频输入
↓
调用本地或云端 STT 引擎
↓
获取文本结果
↓
通过系统输入接口写入当前聚焦窗口
```

如果没有 PC Agent，设备只能作为蓝牙麦克风使用，不能保证语音自动转换成文本并输入到当前光标位置。

PC Agent 可选能力：

- 选择 VibeCoding Mic 作为音频输入源；
- 按住指定按键开始录音；
- 松开按键结束录音；
- 调用本地 STT 或云端 STT；
- 将识别文本注入当前焦点窗口；
- 支持快捷键触发、状态提示、错误提示；
- 支持断网降级或本地模型识别。

---

## 5. 工作模式设计

### 5.1 模式一：正常工作模式

正常工作模式用于日常键盘输入和语音输入。

```text
正常工作模式
├── BLE HOGP：连接 PC，作为键盘
├── Classic HFP：连接 PC，作为麦克风
└── BLE App：不允许连接配置，或仅允许扫描状态
```

特点：

- 电脑键盘已连接时，五个按键执行用户自定义功能；
- 电脑键盘未连接或等待回连时，五个按键执行产品原有离线功能；
- 麦克风可用于语音采集；
- App 不参与实时配置；
- 避免 PC 与手机同时作为 BLE Central 连接设备。

---

### 5.2 模式二：App 配置模式

App 配置模式用于修改按键映射和设备参数。

```text
配置模式
├── BLE HOGP：未连接 PC，或主动断开 PC
├── Classic HFP：可保持断开，或根据产品策略暂停
└── BLE App：连接设备并写入配置
```

配置模式下设备不向电脑发送键盘输入，但五个物理按键仍保留产品原有的离线功能。进入或退出配置模式所使用的专用按键或组合键，按对应产品交互定义执行。

推荐进入条件：

```text
设备未连接 PC HOGP
或
用户三击 Key1 主动进入配置模式
```

推荐配置流程：

```text
用户三击 Key1
↓
设备进入配置模式
↓
停止 HOGP 广播 / 主动断开 HOGP
↓
启动 Config 广播
↓
手机 App 搜索并连接设备
↓
用户修改按键配置
↓
App 写入配置并保存
↓
设备退出配置模式
↓
恢复 HOGP 广播
↓
PC 自动回连键盘
```

---

### 5.3 模式三：后期高级热配置模式

后期如果客户要求“键盘不断开也能通过手机 App 配置”，可以扩展为高级模式。

```text
高级热配置模式
├── PC BLE Central：连接 HOGP Keyboard
├── 手机 BLE Central：连接 RDX BLE App GATT（按键设置扩展）
└── Classic HFP：根据芯片能力选择是否允许同时工作
```

该模式不是第一版量产主路径。

需要额外验证：

- 芯片是否支持 BLE Peripheral 多 Central 连接；
- PC HOGP 与手机 GATT 并发是否稳定；
- Classic HFP 音频工作时，BLE 多连接是否稳定；
- App 写 Flash 时是否影响按键响应；
- 配置更新过程中是否会产生异常 HID report；
- PC 和手机同时回连时的优先级策略；
- 多设备绑定和加密管理是否可靠；
- 量产状态下是否存在连接恢复异常。

---

## 6. BLE HOGP 与 BLE App 并发策略

### 6.1 当前版本策略

当前版本推荐策略：

```text
仅在设备未连接 PC HOGP 时，允许手机 App 配置。
```

该限制只用于决定手机 App 是否可以进入配置，不用于决定五个物理按键的用途。物理按键始终按照“连接电脑时执行自定义功能、未连接电脑时执行离线功能”的产品规则工作。

也就是说，第一版不支持：

```text
PC 连接 HOGP
+
手机同时连接 BLE App
```

### 6.2 设计原因

| 原因 | 说明 |
|---|---|
| 降低协议复杂度 | 避免 BLE 多 Central 连接 |
| 提升 HID 稳定性 | App GATT 访问不会影响键盘输入 |
| 简化配对绑定 | 避免 PC 和手机同时维护 BLE bond |
| 降低功耗 | 单 BLE 连接调度更简单 |
| 降低 HFP 共存压力 | 只需重点验证 HFP + HOGP |
| 便于量产验证 | 测试矩阵更小，问题更容易定位 |

### 6.3 用户交互建议

当设备已连接 PC 时，App 可提示：

```text
设备当前正在键盘模式下使用。
如需修改按键配置，请先断开 PC 连接，或三击 Key1 进入配置模式。
```

当设备进入配置模式时，App 可提示：

```text
配置期间不会向电脑发送键盘输入，设备按键仍可执行离线功能。
保存配置后，设备将自动恢复键盘模式。
```

---

## 7. Classic HFP 与 BLE HOGP 共存策略

### 7.1 是否存在协议冲突

BLE HOGP 与 Classic HFP 不属于同一类业务：

| 功能 | 协议 | 类型 |
|---|---|---|
| 键盘输入 | BLE HOGP | BLE GATT / HID |
| 麦克风音频 | Classic HFP | BR/EDR Audio |
| App 配置 | RDX BLE App GATT | BLE GATT |

二者在协议定义上不互斥。

但在芯片实现上，需要确认双模蓝牙芯片是否支持：

```text
Classic HFP active
+
SCO / eSCO 音频链路 active
+
BLE Peripheral HOGP connection maintained
+
BLE HID Report 正常发送
```

### 7.2 芯片选型必须确认的问题

芯片选型时，需要向原厂确认以下能力：

```text
1. 是否支持 Classic Bluetooth HFP HF role？
2. 是否支持 HFP SCO / eSCO 音频链路？
3. HFP 音频工作时，是否支持 BLE Peripheral 连接保持？
4. HFP 音频工作时，BLE HOGP Report 是否能稳定发送？
5. 是否提供 BLE HOGP Keyboard 示例？
6. 是否支持现有 RDX BLE App GATT 与 HOGP 共存？
7. 是否支持 BLE 广播模式在不同工作状态下切换？
8. 是否支持 HFP + BLE HID 的 Windows 量产案例？
9. 是否支持 mSBC 宽带语音？
10. 是否支持可靠的 Bond 管理和回连策略？
```

### 7.3 实机验证重点

需要重点验证：

- 录音过程中连续按键是否丢失；
- HFP 通话链路建立时，HOGP 是否掉线；
- HOGP 回连过程中，HFP 是否异常断开；
- PC 休眠/唤醒后，键盘和麦克风是否恢复正常；
- Windows 蓝牙设置中是否显示异常重复设备；
- HFP 音频启动时，按键延迟是否明显增加；
- App 配置模式切换后，PC 是否能自动回连键盘。

---

## 8. 广播、连接与配对策略

### 8.1 推荐广播名称

建议当前版本使用不同广播名称区分工作状态。

```text
键盘工作模式：
VibeCoding Keyboard

App 配置模式：
VibeCoding Config

音频设备名称：
VibeCoding Mic
或
VibeCoding Audio
```

如果后期希望用户感知为一个统一设备，可进一步评估同名双模设备策略。但第一版建议采用清晰区分的方式，便于调试、售后和用户理解。

### 8.2 推荐连接优先级

```text
优先级 1：HOGP 键盘连接稳定
优先级 2：HFP 麦克风连接稳定
优先级 3：App 配置连接
```

### 8.3 配置模式进入方式

推荐支持两种进入方式：

| 方式 | 说明 |
|---|---|
| 设备未连接 PC 时自动开放配置 | 最简单，风险最低 |
| 三击 Key1 切换配置模式 | 用户主动切换键盘广播与配置广播 |

按键操作：

```text
三击 Key1：在键盘模式与配置模式之间切换
```

---

## 9. Keymap 与宏配置设计

### 9.1 Keymap 数据结构建议

每个物理按键可配置为以下类型：

```text
Key Action Type
├── Normal Keyboard Key
├── Keyboard Combo
├── Consumer Control
├── Macro
├── Layer Switch
├── Voice Input Trigger
└── Disabled
```

### 9.2 示例配置结构

```json
{
  "version": 1,
  "profile_id": 0,
  "keys": [
    {
      "key_id": 1,
      "type": "keyboard_combo",
      "modifiers": ["CTRL"],
      "keycode": "C"
    },
    {
      "key_id": 2,
      "type": "keyboard_combo",
      "modifiers": ["CTRL"],
      "keycode": "V"
    },
    {
      "key_id": 3,
      "type": "voice_input_trigger",
      "mode": "push_to_talk"
    },
    {
      "key_id": 4,
      "type": "consumer_control",
      "usage": "VOLUME_UP"
    },
    {
      "key_id": 5,
      "type": "layer_switch",
      "target_layer": 1
    }
  ],
  "crc32": "0x12345678"
}
```

### 9.3 配置保存策略

建议采用双分区或备份机制：

```text
Config Slot A：当前有效配置
Config Slot B：新配置写入区
CRC 校验通过后切换 active slot
```

避免配置写入过程中断电导致设备无法正常工作。

---

## 10. 语音输入功能设计

### 10.1 推荐语音触发方式

建议将其中一个按键定义为语音触发键。

可支持：

| 模式 | 说明 |
|---|---|
| Push-to-Talk | 按住开始录音，松开结束识别 |
| Toggle | 单击开始录音，再次单击结束 |
| Hold + Cancel | 按住说话，滑动或组合键取消 |
| Short Command | 短语音命令，例如打开搜索、执行快捷操作 |

第一版推荐：

```text
Push-to-Talk：按住说话，松开识别
```

原因：

- 用户理解成本低；
- 可减少误触发；
- 便于 PC Agent 判断录音开始和结束；
- 适合五键设备场景。

### 10.2 PC Agent 角色

PC Agent 建议负责：

```text
1. 监听 HID 语音触发按键
2. 选择 VibeCoding Mic 作为录音源
3. 录制音频
4. 调用 STT 引擎
5. 获取识别文本
6. 将文本输入到当前聚焦窗口
7. 提供状态提示和错误提示
```

### 10.3 没有 PC Agent 时的能力边界

如果没有 PC Agent：

- 设备只能作为蓝牙键盘和蓝牙麦克风；
- 用户需要手动打开系统听写或第三方语音输入；
- 不能保证自动输入到当前聚焦窗口；
- 产品体验不完整。

因此，如果语音输入是核心卖点，建议将 PC Agent 纳入正式产品范围。

---

## 11. 当前版本推荐技术方案

### 11.1 推荐方案

```text
当前版本主方案：
BLE HOGP Keyboard
+
Classic Bluetooth HFP Microphone
+
RDX BLE App Key Settings，仅在未连接 PC 时开放
+
PC Agent，可选但强烈建议
```

### 11.2 当前版本不推荐内容

第一版不建议做：

```text
PC BLE HOGP 持续连接
+
手机 BLE App 同时连接
+
Classic HFP 同时录音
```

也不建议做：

```text
使用 HID 模拟麦克风
```

原因：

- HID 不适合音频输入；
- BLE 多 Central 并发增加芯片和协议栈风险；
- HFP 音频链路会占用较高实时调度资源；
- 三方并发会显著增加测试矩阵；
- 对第一版量产稳定性不利。

---

## 12. 后期优化方向

### 12.1 BLE 热配置

后期可支持：

```text
PC 保持 HOGP 连接
+
手机 App 同时连接 RDX BLE App GATT（按键设置扩展）
+
配置实时更新
```

前提条件：

- 芯片支持 BLE Peripheral 多 Central；
- SDK 支持 HOGP 与 RDX BLE App GATT 多连接并发；
- HFP 音频期间仍能保持 BLE 多连接；
- Flash 写入不会影响 HID report；
- 配置更新具有事务机制和回滚机制。
- HOGP 仍只接收 Key Action Executor 输出的 Keyboard Report，不承载 App 按键设置协议。

### 12.2 LE Audio

后期可评估 LE Audio，用于改善语音质量、功耗和延迟。

但由于 PC 端对 LE Audio 的支持依赖操作系统版本、蓝牙硬件、音频 codec 和驱动，第一版不建议将 LE Audio 作为主路径。

### 12.3 USB Dongle / USB Audio

如果产品对语音转文字稳定性要求很高，可以考虑后期增加 USB Dongle 版本。

优势：

- 音频输入稳定；
- 延迟更可控；
- 不依赖 PC 蓝牙栈；
- 配对流程更简单；
- 更容易统一键盘、麦克风和 PC Agent 的体验。

---

## 13. 风险列表

| 风险 | 等级 | 说明 | 对策 |
|---|---|---|---|
| HFP 与 HOGP 共存不稳定 | 高 | HFP 音频链路可能影响 BLE 调度 | 芯片选型阶段必须实测 |
| BLE App 与 HOGP 并发复杂 | 高 | 涉及 BLE 多 Central | 第一版禁止并发 |
| PC 侧语音转文字体验不完整 | 高 | 麦克风本身不负责 STT | 增加 PC Agent |
| HFP 音质不足 | 中 | Classic HFP 音质有限 | 支持 mSBC，后期评估 LE Audio |
| Windows 识别多个设备 | 中 | 键盘与音频可能分开显示 | 使用清晰设备名称和用户引导 |
| 配置写 Flash 导致异常 | 中 | 写入中断可能损坏配置 | 双 Slot + CRC + 回滚 |
| 配对和回连异常 | 中 | BLE 与 Classic 双模绑定复杂 | 明确配对流程和状态机 |
| 用户配置期间误解键盘离线 | 低 | 配置模式下键盘暂不可用 | LED 和 App 提示 |

---

## 14. 测试验证清单

### 14.1 BLE HOGP 测试

```text
[ ] Windows 首次配对
[ ] Windows 自动回连
[ ] 五键单键输入
[ ] 组合键输入
[ ] Consumer Control 输入
[ ] 长按、短按、双击识别
[ ] 连接电脑后五个按键只执行自定义功能
[ ] 自定义按键未配置或被禁用时不误触发离线功能
[ ] 断开电脑后下一次按键恢复离线功能
[ ] 等待电脑连接或自动回连期间离线功能正常
[ ] PC 休眠唤醒后回连
[ ] 低电量状态下输入稳定性
```

### 14.2 App 配置测试

```text
[ ] 未连接 PC 时 App 可连接
[ ] 已连接 PC 时 App 不允许配置
[ ] App 连接期间五个按键仍可执行离线功能
[ ] 三击 Key1 进入配置模式
[ ] 再次三击 Key1 恢复键盘模式
[ ] 配置写入成功
[ ] 配置写入失败回滚
[ ] 配置保存后自动恢复键盘模式
[ ] PC 自动回连
[ ] App 提示信息正确
```

### 14.3 HFP 麦克风测试

```text
[ ] Windows 识别蓝牙麦克风
[ ] 录音软件可正常采集声音
[ ] HFP 建链时间可接受
[ ] 录音过程中 HOGP 按键无丢失
[ ] HOGP 按键过程中 HFP 不断音
[ ] PC 休眠唤醒后音频设备恢复
[ ] 多品牌 PC 兼容性测试
```

### 14.4 PC Agent 测试

```text
[ ] 监听语音触发键
[ ] 正确选择麦克风输入源
[ ] Push-to-Talk 开始和结束录音
[ ] STT 返回文本
[ ] 文本输入到当前聚焦窗口
[ ] 无焦点窗口时提示用户
[ ] 网络异常时提示用户
[ ] 识别失败时不误输入
```

---

## 15. 推荐量产路径

### 15.1 EVT 阶段

目标：验证核心技术可行性。

```text
EVT 验证内容：
- BLE HOGP 键盘输入
- BLE App 配置模式
- Classic HFP 麦克风
- HFP + HOGP 共存
- PC Agent 基础语音输入
```

### 15.2 DVT 阶段

目标：验证稳定性和兼容性。

```text
DVT 验证内容：
- 多品牌 Windows PC 兼容性
- 多版本 Windows 兼容性
- 不同蓝牙适配器兼容性
- 长时间 HFP + HOGP 共存
- 配置模式切换压力测试
- 低电量与异常断电测试
```

### 15.3 PVT 阶段

目标：验证量产一致性。

```text
PVT 验证内容：
- 批量设备蓝牙地址和名称管理
- 出厂配置写入
- 默认 Keymap 验证
- App 配置一致性
- PC 连接说明和用户引导
- 售后恢复出厂设置流程
```

---

## 16. 最终推荐结论

VibeCoding Keyboard 当前版本建议采用以下技术栈：

```text
键盘输入：
BLE HOGP Keyboard

App 配置：
RDX BLE App GATT + Key Settings Extension
仅在设备未连接 PC HOGP 时开放

麦克风输入：
Classic Bluetooth HFP

语音转文字：
PC Agent 或系统 STT

后期扩展：
BLE 多 Central 热配置
LE Audio
USB Dongle / USB Audio
```

当前版本的核心策略是：

```text
正常使用时：
BLE HOGP + Classic HFP

配置时：
RDX BLE App Config only

不做：
PC HOGP + 手机 App GATT + HFP 三方并发
```

五个物理按键遵循统一的产品规则：连接电脑键盘时执行自定义功能，未连接电脑键盘时执行离线功能；手机 App 的连接状态不参与按键用途切换。

该方案可以在保证键盘输入稳定性的前提下，降低蓝牙并发复杂度，保留后期热配置和高质量音频升级空间，适合作为第一版量产技术方案。
