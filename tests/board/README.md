# CDC 终端产测工具

## CDC 已保存录音下载

烧录接入 LIST/GET 的固件后，正常 ON 并连接 CDC 数据口，双击 `CDC-test.bat` → `3` → `6`（单次）或 `7`（十次），输入 CDC COM 后自动获取设备录音列表，输入文件序号即可。无需手抄 SN/名称、DUT、BLE 登录、设时或先进行产测录音。列表支持 `r` 刷新、`q` 退出；空列表需先保存一份录音。独立 RAW 对照通过 `--file-reference` 提供。

命令行示例（替换 COM/SN/名称）：`python -X utf8 -x CDC-test.bat --port COM6 --mode file-download --file-sn 7 --file-name abc123.raw --bench-repeat 1`。对应设备命令为 `*APP#file#get#7#abc123.raw#` 加 CRLF，脚本负责后续二进制解析。

默认在 `output/cdc-file-reports/` 生成 RAW 和 JSON；可使用 `--file-reference 独立对照.raw` 验证去掉 BLE 封装后的同一文件内容。失败保留 `.part`，不覆盖已有文件。先验证单轮长度和 CRC，再使用至少 10 MB 的合法录音跑 `--bench-repeat 10`；小文件完整下载但不足性能验收大小时退出码为 2，不代表 CRC 错误。回传 JSON 和固件调试串口日志；实际录音速度尚待实机测量，内存源 700+ KB/s 不能替代此结果。

## CDC 吞吐瓶颈对照（开发实验）

菜单 3 的选项 1/2 保留边收边校验的单轮/十轮，3 保留取消；新增 4/5 为接收后完整校验的单轮/十轮。固定 10 MB、4 KB 块和 USB 环境，先跑 1→4→1，再跑 2/5。两种模式均要求完整 CRC/模式/END 校验，后置校验不支持取消。命令行可用 `python -X utf8 -x CDC-test.bat --port COM6 --mode file-bench --bench-receive-mode deferred --bench-repeat 10`。

JSON 中 KBps 始终包含完整校验；deferred 另报 receive_KBps、receive_elapsed_s、verification_s，接收阶段数字不是新的验收口径。固件默认关闭 TX 统计与抽样；诊断时同时设置 TCFG_T2620_CDC_TX_STATS_ENABLE=1 与 TCFG_T2620_CDC_TX_TIMING_ENABLE=1 并 clean + all。每约 64 次 IRQ 续交采样一次，在任务中每秒输出 CDC_TX_TIME；关注 samples、submit_us_sum / samples、submit_us_max、clock_invalid，并同时保存 CDC_TX。计时不覆盖中断响应延迟或总线包间隔。需关闭探针对照时，将 TCFG_T2620_CDC_TX_TIMING_ENABLE 设为 0 并 clean + all。完整范围及局限见 docs/10-4 第 11.10 节。

## 原有产测入口

统一入口是仓库根目录 **CDC-test.bat**（批处理引导 + 全部产测 Python 代码内嵌，`tests/host` 直接加载测试）。双击后选择 1 普通测试、2 包装测试或 3 CDC 文件传输测速，再输入串口号（例如 `6` 或 `COM6`）。菜单 1/2 先显示基础信息；普通测试核对后开始灯、喇叭、五键、录音及文件下载。菜单 3 为独立开发链路实验，不进入 DUT。需要 Python 3；首次运行自动安装当前模式所需依赖，只有完整普通测试需要 imageio-ffmpeg。

普通产测/包装只需分发 CDC-test.bat，报告在同目录 `output/factory-cdc-reports/`。开发测速还需保留 `tools/cdc_file_transfer.py` 相对路径，复用同一个二进制接收器，报告在 `output/cdc-file-reports/`。缺少该模块只会阻止菜单 3，不影响菜单 1/2。

当前工具的 DUT 控制请求使用 `*APP#custom#<命令>#<值>#` 并追加 CRLF，需配套支持此格式的固件；不会向旧固件自动降级或重发。新固件不再兼容旧 `DUT <命令> <值>` 工站请求，旧格式返回 `ERR CMD`。录音请求与状态 `LAST_CMD` 均统一为 `ft_rec_chat`，旧别名 `ft_rec` 返回 `ERR CMD`；按键布局只接受 `*APP#custom#ft_key_layout#`，显式参数返回 `ERR ARG`。固件与脚本必须配套升级；其余查询、响应及录音/文件传输扩展格式保持不变。

命令行方式（可选）：

```powershell
python -X utf8 -x CDC-test.bat --list-ports
python -X utf8 -x CDC-test.bat --port COM6
```

COM6 换成设备实际端口。板子拨码 ON，连接数据线，烧录 CDC=1、TEST=0 的正式协议固件；关闭占用该串口的其他工具。默认 full 模式先检查 PING、身份字段和 AUTH 已烧录，再进行交互测试；只读排查可显式使用 `--mode foundation`。可追加 `--expect-addr 123456789ABC --expect-fw 实际版本` 对照烧录记录和预期版本；未提供时只检查格式，不代表烧录记录一致。

交互控制流程：

```powershell
python -X utf8 -x CDC-test.bat --port COM6 --mode controls
```

执行顺序：检查空闲状态 → 操作员确认 → 进入 DUT → LED 启停及目视判定 → 喇叭启停及试听判定 → 五键捕获（默认 30 秒）→ 显式停止按键 → 退出 DUT → PING 验证通信保持。灯和喇叭输入 y 为通过、n 为失败，其他输入会重新提示。五键初始显示 `A: 0   B: 0   C: 0   D: 0   E: 0`，检测到按下后对应位置变为 1 并保持；终端原地刷新，仅状态变化时更新，五键齐全后自动继续。五键只验证五次按下均被捕获，不验证释放或电气指标。测试过程中不要让 BLE 工站或实体键提交其他产测动作。

**本次固件已支持 GET_STATUS、GET_KEY_STATUS 及进入／退出 DUT、LED、喇叭、五键控制。** 旧基础固件仍会在 GET_STATUS 返回 UNSUPPORTED 后以 BLOCKED 结束。请先烧录支持控制流程的固件。LED 复用 BLE 的实际效果：绿灯持续常亮，确认后脚本发送停止命令；灯和喇叭需要人工判定，五键累计由脚本轮询。控制主流程已通过实板验证，异常断线和共存场景仍待验收。

`--mode full`（菜单 1 的默认流程）在五键结束后开始录音：回车开始，对麦克风说话 5～10 秒，回车停止；设备最多录制 15 秒。脚本播放实时收到的录音，y/n 判定、r 重听；随后退出 DUT，独立下载同一次保存的文件，校验后再次播放并判定。两项结果分别保存，下载成功不能替代实时流通过。首次使用需烧录 AUDIO=1 固件；旧固件在能力查询时停止，不猜测发送录音命令。音频已实板验证自动限时停止、回车手动停止，以及本地播放暂停后的录音与下载；三轮实时流和下载原始文件逐字节一致。正常工厂测试无需预先播放或暂停，具体证据见 [8-4 第 9 节](../../docs/8-4.工厂CDC录音与文件下载测试实现说明.md)。

`--mode controls` 仍可单独回归灯、喇叭和五键，不要求音频解码依赖。普通测试不自动格式化、包装或关机。

每次生成 `output/factory-cdc-reports/*.json`，可用 `--report 路径.json` 指定新文件（不覆盖）。终端显示完整 PROTO、NAME、ADDR、FW、AUTH，供工厂核对；报告保留全部身份字段（包括完整 AUTH），以及时间、已确认步骤、最后请求、失败原因和范围，设备拒绝请求时保存 ERR 错误响应，其他原始串口响应不保存。`scope` 与 `outcome` 表示本次所选流程及其结果；`overall_factory_pass` 当前始终为 false，不代表本轮失败，也不将普通或包装单份报告视为全工站总判定。

退出码：0=所选范围通过；1=测试失败；2=固件能力/准备条件未满足；3=结果未知。PowerShell 通过 `$LASTEXITCODE` 查看。

通信/动作超时、编号被覆盖、Ctrl+C、EOF 都停止流程，不自动重发、重连或提交清理动作；设备可能仍在测试。检查设备状态，必要时拨码 OFF 安全关机后恢复连接。关闭/重开 COM 不能证明新会话已建立，不能据相同 ID 认领旧结果。人工判定失败时，在通信和动作结果确定的正常路径上停止当前测试并退出 DUT。

只读排查使用 `--mode foundation`（原独立脚本 `factory_cdc_protocol.py` 已并入单文件工具后移除）。`factory_cdc_bytes.py` 仅用于 TEST=1 的历史字节层测试，不用于正式协议固件。

当前默认构建已开启 CDC=1、TEST=0、LOG_LEVEL=1；直接使用 SDK 的 VS Code all 任务即可，无需额外宏覆盖。支持基础信息、普通控制、CDC 录音及文件下载、包装结束；独立格式化仍未开放。请使用包含对应功能的固件；普通主流程及独立包装均已有实板通过记录。

## CDC 文件传输链路测速（内存源基准）

当前项目在 t2620_project_config.h 中统一开启 TCFG_T2620_CDC_FILE_ENABLE=1，直接使用 SDK 的 VS Code clean → all，无需临时编译参数。烧录后保持正常 ON，连接 USB 数据线并关闭占用 CDC 口的其他工具。该开关控制通用文件传输基础设施，不是临时 MVP 开关；当前仅支持内存源，真实录音 GET 尚未实现，量产开放策略另行验收。切换配置必须 clean + all。当前构建方式见 docs/10-4.CDC通用文件传输业务方案.md 第 11.2 节。

双击 CDC-test.bat → 菜单 3 → 子菜单 1（单轮 10 MB）→ 输入 CDC 端口。首次先跑单轮，再运行子菜单 2（十轮）和 3（取消）。无需先录音，也不进行格式化、包装或 DUT 动作；应避免 BLE/实体键同时启动产测。

默认 4096 字节块，检查模式数据、逐帧 CRC 和整流 CRC，目标为 600,000 有效字节/秒。终端显示每轮速度及结论，JSON 自动保存到 output/cdc-file-reports/，已有报告不会覆盖。取消测试在收到约 1 MiB 后发送 CANCEL；失败不自动重发或猜测恢复。

命令行可使用 --mode file-bench、--bench-repeat 10、--bench-block 1024/2048/4096/8192，或 --mode file-cancel。通过独立工具 tools/cdc_file_transfer.py 还可选择落盘和填写 USB 环境信息。该菜单退出码为 0=实验通过、1=未达速率/运行失败、2=准备条件不足。

注意：当前测的是内存源链路，不是真实历史录音下载；即使测速通过，也不代表通用文件业务验收通过。请回传 JSON 及对应控制日志再评估下一阶段。

当前复测基线保留第二次调试的空闲唤醒、低水位边沿补充及诊断，以及第三次 IN IRQ 有界续包；第二次无明显提速不表示它在 IRQ 方案中可直接删除。[CDC_TX] 的 irq_fast 为中断成功续交包数，bytes/packets 只计硬件完成，dispatch 为任务处理次数。快速路径计数增加不等于吞吐达标；保留边界与测试对应关系见 10-4 第 11.9 节。

第四次合包单轮实测 724.53 KB/s，相比第三次单轮 724.04 KB/s 无明显收益，已撤回合包与 stream_join/stream_short 计数；保留 IN IRQ 续发和异常帧头诊断。清理版本仍需烧录后复测：保持 10,000,000 字节、4096 字节块及相同环境，进行十轮、取消及故障回归，以第三次十轮均值 718.65 KB/s 为参考，不将历史结果当作清理版本验收。若再次出现 invalid binary header，保留 JSON 原始头/期望偏移及同次固件日志。详见 10-4 第 11.8 节。

原始约 450 KB/s 仅作历史参考，当前复测重点是清理版本是否保留第三次约 700+ KB/s 的成果，不能仅以超过 600 KB/s 宣称无退化。请提供十轮均值/最低值、完整校验结果、取消结果、对应 [CDC_TX] 日志与 JSON，并补记固件构建身份。日志 bytes 包含协议开销，drained 包含正常结束排空，不应分别当作有效文件吞吐或必然断粮；停读、拔插、DIP OFF 和 BLE/HOGP 共存需另行回归。

## CDC 真实文件跨业务冲突回归

烧录后先保持 BLE 应用连接，使用菜单 3 → 6 下载真实录音。在确认已经接收数据后，分别触发 BLE 列表/下载、APP 录音、实体录音键及播放恢复；期望新业务失败、原 CDC 下载继续且 CRC 正确。关注 `[STORAGE_BUSY] owner=CDC_FILE request=...`，旧协议沿用失败/停止/状态通知，不保证应用显示字面值 BUSY。

反向验证先录音或 BLE 下载，再发起 CDC LIST/GET，应返回现有忙错误；原业务停止并收尾后 CDC 应恢复。取消、停读、拔插、重开串口、OFF 另测，不完整下载不算成功。无干预时再测十轮速度，不能把主动冲突时的速度作为纯吞吐基线。

删除、格式化、包装和恢复默认设置仅在已备份测试设备上单独验证：若命令错过下载窗口，可能正常执行并修改/清空数据。记录固件日志、CDC JSON、BLE 结果及操作顺序；完整步骤和本轮软件边界见 `docs/10-4.CDC通用文件传输业务方案.md` 第 9.7 节。

## 包装测试

选择菜单 2，或运行 `python -X utf8 -x CDC-test.bat --port COM6 --mode packaging`。核对完整身份后输入 y 确认：将清空存储中的文件、恢复默认参数、解绑并禁用实体按键进入产测。脚本进入 DUT，提交一次 `*APP#custom#ft_finalpack_end#1#`，轮询真实完成结果，再人工确认红灯常亮。完成后保持 FINALPACK 状态，提示拨码 OFF；不发送退出 DUT 或自动关机命令。OFF 动作不由脚本验证。

包装单独生成 scope=packaging 报告，不重跑灯、喇叭、五键，也不据此宣称普通测试或完整产测通过。旧固件返回 UNSUPPORTED 时停止，不执行包装，也不自动退出 DUT；可拨码 OFF 后烧录新版。通信中断不重发包装命令。包装提交及红灯已通过实板报告验证；拨码 OFF 与重启持久化需另行确认。

音频证据位于对应报告旁的 `<报告名>-audio/`，包含独立的 stream.raw/.opus/.wav 和 download.raw/.opus/.wav；失败时保留已收到的数据。控制与包装报告不产生音频文件。

## 2026-10-08 规范命令回归状态

`COM3_2026-10-08_15-10-13.log` 已确认 BLE/CDC 普通控制及录音请求对齐：按键布局无参数、录音使用 ft_rec_chat；两路录音落盘 error=0，CDC 文件读取请求走到关闭。尚未核对本轮 JSON、LAST_CMD 回复、音频文件校验及试听结果，不能据此宣布全项 PASS；包装未覆盖。两路均出现 `timer_no_response: app_core`，仍需单独排查。详细证据见 [8-3 第 14 节](../../docs/8-3.工厂CDC普通测试项DUT接入说明.md) 与 [8-4 第 10 节](../../docs/8-4.工厂CDC录音与文件下载测试实现说明.md)。
