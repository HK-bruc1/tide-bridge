# RDX 本地录音播放器完整功能实施方案

> 文档状态：Phase 1 已完成
>
> 最后更新：2026-07-14
>
> 适用范围：T2620、RDX 本地录音、UXFILE/DAT 文件索引、Source_Dev0/JLStream 播放链路

## 1. 目标与结论

当前工程已经打通以下基础链路：

```text
UXFILE/DAT 按 SN 定位录音
    -> 打开板载 SD NAND FAT 分区中的 raw 文件
    -> 定时数据泵
    -> Source_Dev0
    -> Stereo Opus Decoder
    -> DAC
```

下一阶段应先把它收敛为一个完整、可预测、可维护的本地录音播放器。针对当前产品，用户侧只需要支持：

- 上一曲、下一曲；
- 快退、快进；
- 自然播放结束后停止，等待用户下一次操作；
- 首尾回绕；
- SN 不连续、文件删除、坏文件和 SD 异常处理；
- 与录音、文件同步、文件传输、格式化互斥；
- 可查询当前 SN、播放状态、当前位置和总时长。

本方案的核心结论是：

> 将录音集合建模为“按 SN 升序排列的逻辑双向环”，但不在播放器中实现动态双向链表。UXFILE/DAT 是文件集合的唯一真相源，播放器只保存当前游标和播放状态。

优先在 UXFILE 层提供“相邻有效 SN”查询接口。若现阶段无法修改 UXFILE 实现，则在 `rdx_playback.c` 内使用连续的 `u32 SN` 快照作为兼容索引。不要为每个文件分配链表节点。

## 2. 当前基线

### 2.1 已确认的音频格式

本项目的 `storage/sd0` 是 JL SDK 对板载 SD NAND 的逻辑设备命名，不是可插拔 SD 卡槽。播放器不需要支持用户热插拔，存储异常模型应限定为：

- 上电初始化或 FAT 挂载失败；
- 运行中块设备 I/O 超时或读错误；
- FAT、DAT 或 pending.dat 数据损坏；
- 主动格式化及格式化后的重新挂载；
- 异常掉电、看门狗复位后的文件和索引恢复；
- NAND 寿命、坏块或底层控制器异常暴露出的持续 I/O 错误。

后文中的“存储不可用”均指上述故障，不包含物理拔卡场景。

```text
文件格式       : headerless stereo Opus raw
编码输入       : 16000 Hz, 2 ch
Opus 帧时长    : 20 ms
Opus 帧大小    : 80 bytes
码率           : 32000 bit/s
文件数据率     : 4000 bytes/s
解码输出       : 48000 Hz PCM
```

因此文件位置与播放时间可以进行确定性换算：

```text
frame_index = byte_offset / 80
position_ms = frame_index * 20
byte_offset = position_ms / 20 * 80

1 秒  = 50 帧  = 4000 字节
5 秒  = 250 帧 = 20000 字节
10 秒 = 500 帧 = 40000 字节
```

所有 Seek 目标都必须向 80 字节帧边界对齐。

### 2.2 当前已有能力

`rdx_playback.c` 已经具备：

- 从 DAT 缓存取得文件数量和最大 SN；
- 按 SN 查询文件信息；
- 打开并播放单个录音；
- 非阻塞定时数据泵；
- Source_Dev0 背压处理；
- EOF 后等待 Source_Dev0 排空；
- 上一条、下一条；
- 跳过空文件名和无法打开的文件；
- 播放与录音、同步、传输之间的启动互斥。

### 2.3 当前主要缺口

1. Phase 1 已完成上下曲、空头入口、EOF 停止和 key/app 门控收敛。
2. Phase 2 已接入单文件内 `ff/fr` 相对 Seek，设备回归后再标记完成。
3. `ftell()` 表示文件读取头，不表示用户正在听到的位置，因为 Source_Dev0 中存在预读数据。
4. Phase 3 的音量调节应复用系统 `APP_MSG_VOL_UP/DOWN` 和 TWS 既有音量同步路径，不在 RDX 播放器内自建音量状态。
5. 文件列表刷新只观察 `count/max_sn`，无法完整表达同数量替换、删除当前文件等变化。
6. 只有启动前互斥检查，冲突业务在播放过程中开始时缺少统一的抢占策略。

## 3. 设计原则

### 3.1 UXFILE 是唯一真相源

文件是否存在、SN 是多少、文件名是什么，应由 UXFILE/DAT 决定。播放器不得维护另一份可独立修改的文件数据库。

播放器缓存的列表只能是可丢弃、可重建的导航快照，不拥有文件元数据。

### 3.2 逻辑环不等于链表

双向链表在本场景没有明显收益：

- 每个节点需要额外的前后指针；
- 频繁 `malloc/free` 会带来碎片和失败路径；
- 文件本身已有稳定的单调 SN 排序键；
- 上一曲、下一曲只需要相邻查询，不需要任意节点插入操作；
- DAT 更新后仍然必须重新校验链表。

适合本项目的表示方式按优先级排序：

1. UXFILE 直接提供前后相邻 SN 查询，播放器只保存 `current_sn`；
2. 播放器保存按 SN 升序排列的连续 `u32` 数组和 `cursor`；
3. 无索引时按 SN 分时扫描，作为内存不足或索引构建失败的降级路径。

### 3.3 所有控制命令在 app_core 串行执行

按键、协议命令、EOF、列表失效和业务抢占最终都转换为播放器命令，并在 `app_core` 执行。音频线程只负责取帧，不直接修改播放列表、文件句柄或播放器状态。

这样可以避免以下并发问题：

- 数据泵正在 `fread()` 时另一个任务关闭文件；
- Seek 与 EOF 同时触发两次切换；
- 按键连击导致重复创建 JLStream；
- 文件删除与当前播放切换互相覆盖状态。

### 3.4 先准备候选，再提交游标

切歌不能先修改 `current_sn`，再尝试打开文件。正确顺序是：

```text
找到候选 SN
    -> 取得文件信息
    -> 验证文件名
    -> 尝试打开文件
    -> 创建播放流成功
    -> 提交 current_sn/cursor
```

候选失败时继续寻找下一个候选。只有开始播放成功后，候选才成为当前曲目。

## 4. 导航模型

### 4.1 环形顺序

假设有效录音 SN 为：

```text
[3, 8, 11, 20]
```

按 SN 升序定义时间顺序，最大 SN 是最新录音。进入环后，产品按键语义定义为：

```text
下一曲：20 -> 11 -> 8 -> 3 -> 20   // 走向更旧录音，SN 递减
上一曲：3 -> 8 -> 11 -> 20 -> 3     // 走向更新录音，SN 递增
```

规则如下：

- `next` 查找严格小于当前 SN 的最大有效 SN；不存在则回绕到最大有效 SN；
- `prev` 查找严格大于当前 SN 的最小有效 SN；不存在则回绕到最小有效 SN；
- 开机默认是空头状态，不指向任何实际录音；
- 单文件列表中，`prev` 和 `next` 都指回自身；
- 发现坏文件时，本次导航继续沿同一方向搜索；
- 搜索一整圈仍没有可播放文件时停止，不能无限循环。

### 4.2 开机游标

开机后不应立即打开文件，也不应自动出声。播放器处于空头状态，在真正启动成功前不写入稳定游标：

```text
selected_sn       = 0
empty_head        = true
state             = STOPPED/UNREADY
position          = 0
```

第一次收到 `next/prev` 命令时，空头只负责选择入口端点：例如 `[123,456,789]` 中，`next` 播放最大 SN `789`，`prev` 播放最小 SN `123`。启动成功后才同时提交 `selected_sn/current_sn`，之后关机前都在同一个环形区域内切换。

不要在播放器核心内部把 `next()` 同时定义为“首次播放最新文件”和“播放下一文件”。命令语义应保持单一。

### 4.3 自然结束与手动回绕分离

手动上一曲、下一曲始终允许首尾回绕。自然播放结束不自动切换下一条，必须停止并保留当前 `selected_sn`，等待用户再次按 `prev/next/ff/fr`。

## 5. 文件导航索引

### 5.1 首选方案：UXFILE 提供相邻查询

建议为 UXFILE 增加只读接口：

```c
typedef enum {
    UXFILE_NAV_PREV = -1,
    UXFILE_NAV_NEXT = 1,
} uxfile_nav_dir_t;

bool rdx_uxfile_get_latest_sn(u32 *sn);
bool rdx_uxfile_get_adjacent_sn(u32 current_sn,
                                uxfile_nav_dir_t direction,
                                bool wrap,
                                u32 *result_sn);
u32 rdx_uxfile_get_list_generation(void);
```

要求：

- 查询只读取已完成同步的 DAT 缓存；
- 返回的 SN 必须对应有效 DAT 项；
- 不在接口内部打开录音文件；
- 每次新增、删除、同步替换、格式化或缓存重建时增加 `generation`；
- 接口不能把 UXFILE 内部指针长期交给播放器保存。

播放器只保存 SN，真正播放前仍通过 `rdx_uxfile_get_file_data_by_sn()` 重新取得短生命周期文件信息。

### 5.2 兼容方案：播放器构建 SN 快照

如果 UXFILE 当前由库提供、短期无法增加接口，则在播放器内部构建：

```c
typedef struct {
    u32 *items;          /* 严格升序，只保存 SN */
    u16 count;
    u16 cursor;
    u32 generation;
    bool valid;
} pb_playlist_t;
```

构建规则：

1. 读取 `count` 和 `max_sn`；
2. 分批扫描 SN，不在一个 `app_core` 回调中扫描巨大区间；
3. 只复制 SN，不复制 `uxfile_data_t` 指针；
4. 结果按升序保存；
5. 列表播放期间发现文件打不开，可将该 SN 标记为本次会话不可用；
6. 录音完成、删除、同步完成、格式化和存储恢复重挂载时显式失效；
7. 分配失败时降级到按 SN 分时扫描，不因索引失败导致系统崩溃。

连续数组的内存成本为：

```text
100 个文件  : 400 bytes
1000 个文件 : 4 KB
5000 个文件 : 20 KB
```

需要结合产品最大文件数决定是否允许完整快照。如果文件数可能长期达到数千以上，应优先推动 UXFILE 相邻查询，避免重复缓存。

### 5.3 列表变化策略

以下事件必须使导航快照失效：

- 新录音成功写入 DAT；
- 文件删除成功；
- 开机同步完成；
- pending.dat 恢复完成；
- 格式化、挂载故障恢复后的 `sd0` 重新挂载；
- 格式化完成；
- DAT 缓存被释放、重载或替换。

列表失效不要求立即停止正在播放的、仍然有效的文件。下一次导航前重建即可。但以下情况必须立即处理：

- 当前文件被删除：先停止播放，再执行删除；
- `sd0` 发生持续 I/O 错误或被设备管理器置为不可用：停止播放并进入 `UNREADY`；
- 格式化开始：立即停止并使列表失效。

## 6. 播放器状态机

### 6.1 状态定义

建议替换当前简单数值状态：

```c
typedef enum {
    PB_STATE_UNREADY = 0,
    PB_STATE_STOPPED,
    PB_STATE_STARTING,
    PB_STATE_PLAYING,
    PB_STATE_SWITCHING,
    PB_STATE_DRAINING,
} pb_state_t;
```

状态含义：

| 状态 | 含义 |
|---|---|
| `UNREADY` | DAT 同步未完成、SD 不可用或无录音 |
| `STOPPED` | 已选择曲目但未播放，位置为 0 |
| `STARTING` | 正在打开文件和创建播放流 |
| `PLAYING` | 正常送帧和播放 |
| `SWITCHING` | 正在切换到另一 SN |
| `DRAINING` | 文件已读完，等待 Source_Dev0 排空 |

### 6.2 主要状态转换

```text
UNREADY --列表就绪--> STOPPED
STOPPED --prev/next-> STARTING -> PLAYING
PLAYING --prev/next-> SWITCHING -> PLAYING
PLAYING --文件读完--> DRAINING -> STOPPED
任意状态 --stop----> STOPPED
任意活动状态 --SD异常/格式化--> UNREADY
```

### 6.3 命令与状态接受矩阵

稳定状态下的命令行为如下。`幂等` 表示返回成功但不重复执行资源操作。

| 命令 | `UNREADY` | `STOPPED` | `PLAYING` | `DRAINING` |
|---|---|---|---|---|
| `prev/next` | 尝试刷新列表，未就绪则拒绝 | 以 `selected_sn` 为锚点切换；空头时 `next` 进入最大 SN、`prev` 进入最小 SN | 切换并继续播放 | 等待排空或先 `stop` |
| `ff/fr` | 拒绝或仅记录未实现 | Phase 1 拒绝或仅记录未实现 | Phase 1 仅接线；Phase 2 实现 Seek | 拒绝 |
| `stop` | 幂等 | 幂等 | 停止并保留 `selected_sn` | 取消排空并停止 |
| `invalidate` | 保持 `UNREADY` | 标记列表失效 | 内容变化时延迟重建；存储失效时停止 | 存储失效时停止 |

`STARTING` 和 `SWITCHING` 是 `app_core` 中的同步过渡状态。普通播放器消息只能排队，不能在同一事务执行到一半时重入。实现要求：

- 连续 `prev/next` 可以依次执行，但每条命令最多遍历一圈；
- Phase 2 的连续 Seek 必须合并，不能累积大量过期 Seek；
- 关机、格式化等强制停止若需要中断同步文件操作，必须由底层 I/O 提供取消能力，不能假设状态枚举本身可以抢占正在执行的 `fopen/fseek`；
- 状态矩阵用于公共入口校验，内部事务不得绕过状态提交规则。

### 6.4 命令接口

建议公共接口使用“命令语义”，不要暴露内部文件操作：

```c
void rdx_playback_init(void);
int  rdx_playback_prev(void);
int  rdx_playback_next(void);
void rdx_playback_fr(void);
void rdx_playback_ff(void);
void rdx_playback_stop(void);
void rdx_playback_invalidate_playlist(u32 reason);
void rdx_playback_get_info(pb_public_info_t *info);
```

返回值应区分：

```text
OK
NO_FILE
NOT_READY
BUSY_RECORDING
BUSY_TRANSFER
IO_ERROR
DECODER_ERROR
INVALID_STATE
```

不能只打印日志后静默返回，否则按键、BLE 协议和后续 UI 无法获得一致结果。

错误状态按作用域处理，避免把普通坏文件升级成永久故障：

| 错误类型 | 状态处理 |
|---|---|
| 单个文件不存在、为空或损坏 | 本次导航跳过，不进入持久错误状态 |
| 列表为空或同步未完成 | `UNREADY` |
| `sd0` 挂载失效或持续 I/O 错误 | `UNREADY`，等待存储恢复事件 |
| JLStream 临时创建失败 | `STOPPED + last_error`，允许用户重试 |
| 播放器内部状态或资源一致性破坏 | 停止播放并记录 `last_error`，只允许 `stop/invalidate/reinit` 恢复 |

隐藏循环通过“单次命令最多遍历一圈”和明确的重试上限避免，而不是通过禁止用户重试避免。

## 7. 生命周期拆分

当前整体清理函数同时关闭定时器、播放流和文件，不适合 Phase 2 的 Seek。建议拆成三层：

```c
static void pb_stop_pump(void);
static void pb_close_stream(void);   /* 清 Source_Dev0/JLStream，不关闭文件 */
static void pb_close_track(void);    /* 关闭文件并清当前曲目资源 */
```

职责如下：

### 7.1 停止数据泵

- 删除 pump timer；
- 阻止新的 `fread()` 和 Source 写入；
- 清空 pending chunk 状态；
- 不关闭文件和播放流。

### 7.2 关闭播放流

- 调用 `dev_flow_player_close()`；
- 由 Source_Dev0 的 stop/close 生命周期释放并清空压缩数据缓冲；
- 重置 Source 消费计数；
- 保留文件句柄、SN 和逻辑播放位置。

### 7.3 关闭当前曲目

- 先停止数据泵和播放流；
- 关闭文件句柄；
- 清空 pending、EOF 和 drain 状态；
- 根据调用原因决定是否保留 `selected_sn`。

`stop`、`switch` 以及 Phase 2 的 `seek` 不应继续共享一个无法区分意图的全量 cleanup。

## 8. 上一曲、下一曲事务

建议将两个方向合并为一个内部实现：

```c
static int pb_switch_track(pb_direction_t direction,
                           pb_switch_reason_t reason);
```

执行顺序：

```text
1. 检查命令和业务互斥
2. 标记 SWITCHING，停止当前数据泵
3. 从当前 SN 的相邻节点开始寻找候选；空头时 `next` 从最新端进入，`prev` 从最旧端进入
4. 尝试打开候选文件
5. 关闭旧播放流和旧文件
6. 为候选创建播放流并预填数据
7. 成功后提交 cursor/current_sn，进入 PLAYING
8. 失败则继续沿同方向寻找，最多一整圈
9. 全部失败则关闭旧资源，进入 STOPPED/UNREADY 并记录错误码
```

为了减少切歌空窗，可以在不关闭旧文件的前提下先 `fopen()` 候选文件。但 JLStream 只有一个共享播放器实例，创建新播放流前仍必须关闭旧流。

坏文件处理原则：

- 空文件名、`flen <= 0`、`fopen` 失败：跳过；
- 播放流创建失败：记录错误并停止本次切换，避免反复创建流耗尽资源；
- 解码器启动后立即报错：将当前 SN 标记为会话内坏文件，再尝试下一条；
- 一次命令最多访问列表中每个 SN 一次。

坏文件标记不是 Phase 1 的必要状态。第一版依靠单次导航的一圈上限防止重复访问；若设备测试证明反复打开坏文件造成明显阻塞，再增加固定 8 或 16 项的坏 SN LRU。不得按 `u16` 最大文件数预分配字节数组。若已有实际文件数快照，可选用按实际 `count` 分配的 bitset，65535 项 bitset 约为 8 KB，而不是 64 KB。

## 9. 快进、快退与位置模型

### 9.1 不使用 ftell 作为播放位置

数据泵会提前读取文件并写入 2 KB Source_Dev0 缓冲，因此：

```text
ftell = 已读取到的位置
用户听到的位置 < ftell
```

2 KB 对应约 512 ms Opus 数据，再加上当前 pending chunk 和下游 PCM 缓冲，误差不可忽略。

建议在 Source_Dev0 增加消费计数接口：

```c
u32 source_dev0_get_consumed_bytes(void);
void source_dev0_reset_consumed_bytes(void);
```

计数在 Source 成功把完整压缩帧交给 JLStream frame 后增加，在 Source 打开、关闭和 Seek reset 时清零。播放器位置为：

```text
position_frame = seek_base_frame + consumed_bytes / 80
position_ms    = position_frame * 20
```

这是“已经交给解码器”的位置，仍可能比 DAC 实际出声略领先，但远比 `ftell()` 稳定，而且足以支持 5 秒级 Seek。

### 9.2 Seek 事务

`ff/fr` 只允许在当前正在播放的文件内 Seek，不进入环形导航，不跨文件。统一使用绝对目标执行 Seek：

```text
1. 根据当前位置计算 target_ms
2. clamp 到 [0, duration_ms]
3. 换算并对齐到 80 字节帧边界
4. 进入 Seek 事务，停止数据泵
5. 关闭播放流，清空 Source_Dev0 旧数据
6. fseek 当前文件到目标 offset
7. 重置 pending/EOF/drain/消费计数
8. 更新 seek_base_frame
9. 重新创建播放流、预填并恢复 PLAYING
```

不能在旧播放流仍活动时直接 `fseek()`，否则旧缓冲音频会与新位置音频拼接。

`stop`、`switch`、`seek` 的资源动作必须区分：

- `stop`：关闭 timer、播放流和文件句柄，但保留 `selected_sn`；
- `switch`：候选文件打开成功后关闭旧播放流和旧文件，再切到新文件；
- `seek`：关闭 timer 和播放流，保留当前文件句柄，对同一文件 `fseek()` 后重建播放流。

### 9.3 边界行为

- 快退小于 0：停在 0 ms；
- 快进达到文件末尾：停在末尾并结束当前播放，等待用户下一次操作；
- 快进到末尾与自然 EOF 约束一致：不自动 next，不改变环形游标方向；
- 快进快退不允许跨文件，跨文件只能由 `prev/next` 表达；
- 停止状态 Seek：默认拒绝，或先选择曲目后保持 STOPPED；
- 文件长度不是 80 的整数倍：忽略不足一帧的尾部；
- 小于一帧的文件视为不可播放文件。

### 9.4 连续快进快退

第一版建议每次长按事件执行一次 `+/-5 秒`，先保证事务正确。连续按住的增强版应避免每个 HOLD 事件都重建 JLStream：

- LONG 首次立即跳 5 秒；
- HOLD 只累计目标位置；
- 默认以 200 ms 作为连续 Seek 节流起点，设备实测后再调成 200/300/500 ms 或“仅抬起时提交”；
- UP 时立即应用最后目标；
- 新 Seek 到来时取消尚未执行的旧 Seek；
- Seek 命令必须在 `rdx_playback.c` 内合并目标位置，不能在 `rdx_app.c` 中维护播放位置，也不能形成消息队列积压。

## 10. 音量调节参考

音量调节不应放入 `rdx_playback.c`。本项目应复用系统/TWS 已有音量路径：

- `APP_MSG_VOL_UP/DOWN` 是系统标准音量消息；
- `app_common_key_msg_handler()` 中普通场景调用 `app_audio_volume_up/down(1)` 并发送 `APP_MSG_VOL_CHANGED`；
- BT/TWS 场景中 `bt_app_msg_handler()` 调用 `bt_volume_up/down(1)` 后执行 `bt_tws_sync_volume()`；
- RDX key 层只需要把物理动作映射到 `APP_MSG_VOL_UP/DOWN`，不要直接操作 DAC、volume node 或 RDX 播放器状态。

## 11. 业务互斥与抢占

播放器必须同时处理“能否启动”和“播放中是否被抢占”。推荐优先级：

```text
关机/存储不可用/格式化
    > 录音
    > 文件同步或文件传输
    > 本地播放
```

策略表：

| 事件 | 播放器动作 |
|---|---|
| 开始录音 | 同步停止播放并释放文件，再启动录音 |
| 开始文件传输 | 停止播放；传输结束后不自动恢复 |
| 开始 DAT 同步 | 停止或拒绝新播放，使列表失效 |
| 删除当前文件 | 先停止播放，再删除并刷新列表 |
| 删除其他文件 | 当前播放继续，导航列表失效 |
| `sd0` 持续 I/O 错误或挂载失效 | 停止数据泵，关闭播放资源，进入 UNREADY；存储恢复后重建列表 |
| 格式化 | 立即停止，清空选中项和列表 |
| 关机 | 取消 timer，关闭流和文件，不触发自动下一曲 |
| 来电/系统高优先级音频 | Phase 1 采用停止，后续如需共存策略单独评估 |

抢占动作必须通过播放器公共接口执行，其他模块不能直接关闭 `pb_file`、删除 pump timer 或调用 `dev_flow_player_close()`。

## 12. 按键和产品语义

建议第一版映射：

| 动作 | 命令 |
|---|---|
| 上一曲键短按 | 上一条有效录音 |
| 下一曲键短按 | 下一条有效录音 |
| 上一曲键长按 | 快退 5 秒 |
| 下一曲键长按 | 快进 5 秒 |
| 音量加键短按/长按 | 系统音量加 |
| 音量减键短按/长按 | 系统音量减 |
| 长按保持 | 连续累计 Seek，增强阶段启用 |
| 长按抬起 | 提交最后一次累计 Seek |

当前硬件键位中 `NUM0 CLICK` 已映射为下一曲、`NUM0 LONG` 为快进、`NUM1 CLICK` 为上一曲、`NUM1 LONG` 为快退、`NUM2 CLICK/LONG` 为音量加。`NUM3` 当前仍用于录音切换，Phase 3 若要补齐音量减，应优先评估是否将 `NUM3 CLICK/LONG` 调整为 `APP_MSG_VOL_DOWN`，或另行确定物理动作。播放器核心不依赖具体键值。

本产品以录音回听为主，建议 `prev` 每次都切换到上一 SN，不采用音乐播放器常见的“播放超过 3 秒则先回到本曲开头”规则。无屏设备上，固定切换语义更容易形成肌肉记忆；回到本曲开头可通过持续快退完成。

## 13. 推荐内部数据结构

```c
typedef struct {
    u32 selected_sn;
    u32 current_sn;
    u32 pending_sn;

    u32 file_size;
    u32 duration_frames;
    u32 seek_base_frame;

    u16 total_count;
    u16 cursor;

    pb_state_t state;
    pb_intent_t intent;

    FILE *file;
    u16 pump_timer;

    u16 pending_len;
    u16 pending_off;
    bool eof;
    bool playlist_dirty;
    int last_error;
} rdx_playback_t;
```

约束：

- `selected_sn` 表示已经确认有效的稳定选择，停止时仍保留；
- `current_sn` 表示当前绑定到活动播放流的曲目，没有活动流时为 `0`；
- `pending_sn` 只表示 `STARTING/SWITCHING` 正在尝试的候选，事务结束后必须清零；
- 候选 `fopen`、JLStream 或 pump 启动失败时，不得把失败 SN 写入 `selected_sn/current_sn`；
- 切换失败但旧流仍在播放时，`selected_sn/current_sn` 均保持旧值；旧流已经关闭后启动失败时，`current_sn=0`，`selected_sn` 保留上次稳定选择；
- `duration_frames = flen(file) / 80`；
- `seek_base_frame` 表示最近一次打开或 Seek 的帧基准；
- 不在结构体中长期保存 `uxfile_data_t *`；
- 所有帧计数和文件偏移统一用无符号 32 位；
- 计算毫秒和字节乘法时先提升到 64 位，避免长录音溢出。

公共状态建议使用快照返回：

```c
typedef struct {
    pb_state_t state;
    u32 sn;
    u32 position_ms;
    u32 duration_ms;
    u16 track_index;
    u16 track_count;
    int last_error;
} pb_public_info_t;
```

## 14. 日志与可观测性

保留低频状态日志，禁止在每帧或每个 pump tick 打印。

建议事件日志：

```text
[PB] list ready: count=12 first=3 last=20 gen=7
[PB] start: sn=20 size=34240 duration=8560ms
[PB] switch: reason=user_next 20->3
[PB] seek: sn=3 12400ms->17400ms offset=69600
[PB] skip: sn=8 reason=open_failed
[PB] eof: sn=3 read=... written=... consumed=...
[PB] preempt: reason=record_start
[PB] error: state=starting sn=11 err=-...
```

建议累计统计：

- 成功播放次数；
- 切歌次数；
- Seek 次数；
- 跳过坏文件次数；
- Source 写入短写次数；
- pump 调度失败次数；
- drain watchdog 次数；
- 播放被录音/同步/传输抢占次数。

## 15. 代码改造边界

### 15.1 `rdx_playback.c/.h`

- 引入完整状态机和命令接口；
- 抽出导航器和切歌事务；
- 拆分 pump、stream、track 生命周期；
- Phase 2 实现绝对/相对 Seek，Phase 3 复用系统音量路径；
- EOF 停止并保留当前选择，用户切歌和错误跳过仍走统一导航逻辑；
- 提供公开状态快照；
- 保留 `TCFG_RDX_LOCAL_PLAYBACK_ENABLE` 模块边界。

### 15.2 `rdx_uxfile.h` 及其实现

- 首选增加 latest/adjacent/generation 只读接口；
- 在新增、删除、同步、恢复和格式化后更新 generation；
- 文件删除前通知播放器处理当前文件。

若实现位于预编译库且当前不可修改，则先采用播放器 SN 快照兼容方案，并在文档和代码中明确这是过渡层。

### 15.3 `source_dev0.h/source_dev0_file.c`

- 增加压缩数据消费字节计数查询；
- Source 打开时清零，成功读取完整帧时增加；
- 查询和更新需满足目标 CPU 的并发安全要求；
- 不将播放器专用状态放入 Source 节点。

### 15.4 `rdx_app.c`

- 将 `APP_MSG_REC_FR/FF` 接到播放器接口；
- 所有播放器命令统一在 `app_core` 调用；
- 根据返回值决定按键反馈或协议回执。

### 15.5 `rdx_key.c`

- 只负责物理动作到消息的映射；
- 不直接操作文件或播放器内部状态；
- 连续 Seek 阶段再启用 HOLD/UP 映射；
- 不做本地播放开关宏替换，功能门控由 `rdx_app.c` 统一负责。

## 16. 分阶段实施

### Phase 1：上一曲、下一曲切换

目标：上下曲、空头入口和 EOF 停止成为稳定、独立、可验证的播放器能力。

> 实施状态（2026-07-14）：代码、host 检查、固件构建和设备上下曲回归已完成。

- [x] 建立无动态列表分配的逻辑环导航器；
- [x] 修正单文件自循环；
- [x] 抽出统一切歌事务；
- [x] 明确开机空头入口：首次 `next` 进入最新录音，首次 `prev` 进入最旧录音；
- [x] 引入 `selected_sn/current_sn/pending_sn` 提交语义；
- [x] 统一坏文件跳过和一圈搜索上限；
- [x] 增加列表显式失效入口并接入文件删除；
- [x] 自然播放结束后停止并保留 `selected_sn`；
- [x] key 表直接发送 `APP_MSG_REC_*`，播放器开关门控集中在 `rdx_app.c`；
- [x] 补充 host 导航行为与源码契约测试；
- [x] 完成板载 SD NAND 设备回归：上一曲、下一曲符合空头入口和环形区域语义；
- [x] 提交前在完整 JL 构建环境生成固件并做一次冒烟烧录。

### Phase 2：位置、快进和快退

目标：实现准确到 Opus 帧的 `+/-5 秒` Seek。

> 实施状态（2026-07-14）：代码已接入，待固件构建和设备回归。

- [x] Source_Dev0 增加 consumed bytes 查询和 reset；
- [x] 将播放流关闭和文件关闭拆开；
- [x] 实现当前文件内 `+/-5 秒` 相对 Seek；
- [x] 完成 `APP_MSG_REC_FR/FF` 的真实 Seek 行为；
- [x] 完成起点、终点和短文件边界处理：快退到 0，快进到尾停止且不跨文件；
- [ ] 完成固件构建和设备回归，测量单次 Seek 重建耗时。

### Phase 3：音量加、音量减

目标：复用 TWS/系统既有音量路径，完成本地录音回听场景下的音量加减。

- 优先使用系统消息 `APP_MSG_VOL_UP` / `APP_MSG_VOL_DOWN`；
- 参考 `app_common_key_msg_handler()`：非 BT 特殊场景最终调用 `app_audio_volume_up/down(1)`，并发送 `APP_MSG_VOL_CHANGED`；
- 参考 BT/TWS 路径：BT 模式中 `APP_MSG_VOL_UP/DOWN` 会调用 `bt_volume_up/down(1)` 并执行 `bt_tws_sync_volume()`；
- 不在 `rdx_playback.c` 内维护音量变量，不直接操作 DAC 节点；
- 当前 `NUM2 CLICK/LONG` 已是 `APP_MSG_VOL_UP`，Phase 3 需明确音量减物理动作，候选为 `NUM3 CLICK/LONG -> APP_MSG_VOL_DOWN`。

### Phase 4：全面评估与最佳实践核查

目标：在上下曲、快进快退和音量加减都完成后，统一评估是否符合最佳实践、是否存在过度设计。

- 核查 UXFILE/DAT 是否仍是唯一真相源；
- 核查播放器是否仍无动态播放列表和无链表节点；
- 核查所有播放器命令是否仍由 `app_core` 串行执行；
- 核查 key 层是否只做消息映射，不操作播放器状态；
- 核查 Phase 2/3 新增代码是否引入重复状态、跨任务裸操作或过度抽象；
- 完成 host 测试、固件构建和设备回归后再进入提交评审。

## 17. 测试方案

### 17.1 Host 侧契约测试

在现有 `tests/host/run_host_tests.ps1` 下扩充本地播放检查，至少冻结：

- 功能关闭时不会引入播放依赖；
- `prev/next/fr/ff` 应用消息均有 guarded 调用；
- Source consumed API 在头文件声明并由实现提供；
- 80 字节帧和 20 ms 换算常量一致；
- Seek offset 始终进行帧对齐；
- `rdx_key.c` 只做物理按键到 `APP_MSG_REC_*` 的直接映射；
- 录音端修复不依赖本地播放开关。

### 17.2 纯逻辑导航用例

| 文件集合 | 当前 SN | 命令 | 预期 |
|---|---:|---|---:|
| 空 | 0 | next | NO_FILE |
| 空 | 0 | prev | NO_FILE |
| `[20]` | 20 | next | 20 |
| `[20]` | 20 | prev | 20 |
| `[123,456,789]` | 空头 | next | 789 |
| `[123,456,789]` | 空头 | prev | 123 |
| `[3,8,11,20]` | 3 | next | 20 |
| `[3,8,11,20]` | 20 | prev | 3 |
| `[3,8,11,20]` | 8 | next | 3 |
| `[3,8,11,20]` | 8 | prev | 11 |
| `[3,8,11,20]` 且 3 损坏 | 8 | next | 20 |

导航逻辑最好抽成不依赖文件系统和 JLStream 的小函数，以便 host 测试真实执行，而不是只做正则检查。

### 17.3 Seek 用例

- 8.56 秒文件从 0 快进 5 秒，目标为 20000 字节；
- 8.56 秒文件从 5 秒再快进 5 秒，按末尾策略处理；
- 从 2 秒快退 5 秒，目标为 0；
- 非 80 字节整数长度只播放完整帧；
- 连续 20 次快进/快退无内存增长、无残留 timer、无双播放器实例；
- Seek 后不得先播放旧 Source 缓冲中的音频。

### 17.4 设备测试矩阵

- 0、1、2、10、100 个文件；
- 连续 SN、大量空洞 SN、删除最大 SN、删除最小 SN；
- 最新文件损坏、中间文件损坏、全部文件损坏；
- 播放时开始录音、同步、BLE/WiFi 文件传输；
- 播放时删除当前文件和非当前文件；
- 播放、Seek、切歌过程中注入 `fread/fseek` 错误或 `sd0` 离线事件；
- 上电挂载失败、格式化后重挂载、DAT 损坏和异常掉电恢复；
- 最短一帧文件、长达数小时文件；
- 单曲 EOF、最后一曲 EOF 均停止并等待用户操作；
- 快速连按和长按按键；
- 关机时仍处于 PLAYING、SWITCHING、DRAINING。

每轮设备测试至少确认：

```text
无死机
无文件句柄泄漏
无 timer 泄漏
无重复 JLStream 实例
无明显旧音频残留
播放位置误差小于一个 Seek 步长
EOF 尾部不截断
互斥业务可正常启动
```

## 18. 验收标准

### P0

- 空列表、单文件、多文件和 SN 空洞下上下曲行为正确；
- 开机默认空头，不提交 `selected_sn`，首次 `next` 进入最新录音、首次 `prev` 进入最旧录音；
- 切歌失败不会提交错误游标；
- 坏文件不会导致死循环；
- 自然播放结束后停止并等待用户操作；
- key 表直发 `APP_MSG_REC_*`，功能门控在 `rdx_app.c`；
- host 测试通过，固件构建和设备冒烟通过。

### P1

- 快进快退严格按 80 字节帧对齐；
- Seek 后不播放旧 Source 缓冲；
- 位置查询误差稳定在 500 ms 以内；
- 连续快进快退不会造成命令堆积。

### P2

- 音量加减复用系统/TWS 既有路径；
- 音量变化能触发既有 `APP_MSG_VOL_CHANGED` 或等价状态同步；
- 不在 RDX 播放器内引入独立音量状态。

### P3

- 对四个阶段完成后的代码做最佳实践和过度设计复核；
- host 测试、固件构建和设备回归全部通过；
- 评估是否需要继续保留或删除临时 stub、TODO 和未使用接口。

## 19. 最终推荐

针对当前 T2620/RDX 场景，最佳实践不是在 `rdx_playback.c` 中实现一个拥有文件节点的双向循环链表，而是：

```text
UXFILE/DAT 维护文件集合与顺序
        +
逻辑双向环导航器维护相邻关系
        +
播放器状态机维护当前 SN、位置和生命周期
        +
Source_Dev0 消费计数提供可靠位置基准
        +
app_core 串行执行所有控制事务
```

这一结构既保留了“环形楼梯”的直观产品模型，又避免重复文件数据库、链表碎片、失效指针和 Seek 缓冲污染。实施顺序应先完成上下曲切换，再做快进快退，然后复用系统音量路径完成音量加减，最后做最佳实践和过度设计复核。
