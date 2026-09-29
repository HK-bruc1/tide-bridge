# CDC 终端产测工具

最简单的方式：双击仓库根目录 **CDC-test.bat**，先选择 1 普通测试或 2 包装测试，再输入串口号（例如 `6` 或 `COM6`）。两个流程都先显示完整基础信息。普通测试核对后回车开始灯、喇叭和五键测试。结束后窗口保留，报告自动保存。需要已安装 Python 3；首次运行会自动联网安装 pyserial。

从仓库根目录执行，Python 3 + pyserial：

```powershell
python -m pip install -r tests/board/requirements.txt
python -X utf8 tests/board/factory_cdc_flow.py --list-ports
python -X utf8 tests/board/factory_cdc_flow.py --port COM6
```

COM6 换成设备实际端口。板子拨码 ON，连接数据线，烧录 CDC=1、TEST=0 的正式协议固件；关闭占用该串口的其他工具。默认 controls 模式先检查 PING、身份字段和 AUTH 已烧录，再进行交互测试；只读排查可显式使用 `--mode foundation`。可追加 `--expect-addr 123456789ABC --expect-fw 实际版本` 对照烧录记录和预期版本；未提供时只检查格式，不代表烧录记录一致。

交互控制流程：

```powershell
python -X utf8 tests/board/factory_cdc_flow.py --port COM6 --mode controls
```

执行顺序：检查空闲状态 → 操作员确认 → 进入 DUT → LED 启停及目视判定 → 喇叭启停及试听判定 → 五键捕获（默认 30 秒）→ 显式停止按键 → 退出 DUT → PING 验证通信保持。灯和喇叭输入 y 为通过、n 为失败，其他输入会重新提示。五键初始显示 `A: 0   B: 0   C: 0   D: 0   E: 0`，检测到按下后对应位置变为 1 并保持；终端原地刷新，仅状态变化时更新，五键齐全后自动继续。五键只验证五次按下均被捕获，不验证释放或电气指标。测试过程中不要让 BLE 工站或实体键提交其他产测动作。

**本次固件已支持 GET_STATUS、GET_KEY_STATUS 及进入／退出 DUT、LED、喇叭、五键控制。** 旧基础固件仍会在 GET_STATUS 返回 UNSUPPORTED 后以 BLOCKED 结束。请先烧录支持控制流程的固件。LED 复用 BLE 的实际效果：绿灯持续常亮，确认后脚本发送停止命令；灯和喇叭需要人工判定，五键累计由脚本轮询。控制主流程已通过实板验证，异常断线和共存场景仍待验收。

`--mode full` 在控制流程后仍报告 BLOCKED：录音结果、取文件、校验/音质判定尚未接入。普通测试不发送录音、格式化、整包或关机命令，不会将部分通过当作完整产测通过。

每次生成 `output/factory-cdc-reports/*.json`，可用 `--report 路径.json` 指定新文件（不覆盖）。终端显示完整 PROTO、NAME、ADDR、FW、AUTH，供工厂核对；报告保留全部身份字段（包括完整 AUTH），以及时间、已确认步骤、最后请求、失败原因和范围，不保存原始串口响应。`overall_factory_pass` 当前始终为 false。

退出码：0=所选范围通过；1=测试失败；2=固件能力/准备条件未满足；3=结果未知。PowerShell 通过 `$LASTEXITCODE` 查看。

通信/动作超时、编号被覆盖、Ctrl+C、EOF 都停止流程，不自动重发、重连或提交清理动作；设备可能仍在测试。检查设备状态，必要时拨码 OFF 安全关机后恢复连接。关闭/重开 COM 不能证明新会话已建立，不能据相同 ID 认领旧结果。人工判定失败时，在通信和动作结果确定的正常路径上停止当前测试并退出 DUT。

旧工具 `factory_cdc_protocol.py` 保留为最小只读检查；`factory_cdc_bytes.py` 仅用于 TEST=1 的历史字节层测试，不用于正式协议固件。

当前默认构建已开启 CDC=1、TEST=0、LOG_LEVEL=1；直接使用 SDK 的 VS Code all 任务即可，无需额外宏覆盖。支持基础信息、普通控制流程及包装结束；录音取回和独立格式化仍未开放。包装测试需要重新编译烧录本版固件。

## 包装测试

选择菜单 2，或运行 `python -X utf8 tests/board/factory_cdc_flow.py --port COM6 --mode packaging`。核对完整身份后输入 y 确认：将清空存储中的文件、恢复默认参数、解绑并禁用实体按键进入产测。脚本进入 DUT，提交一次 `DUT ft_finalpack_end 1`，轮询真实完成结果，再人工确认红灯常亮。完成后保持 FINALPACK 状态，提示拨码 OFF；不发送退出 DUT 或自动关机命令。OFF 动作不由脚本验证。

包装单独生成 scope=packaging 报告，不重跑灯、喇叭、五键，也不据此宣称普通测试或完整产测通过。旧固件返回 UNSUPPORTED 时停止，不执行包装，也不自动退出 DUT；可拨码 OFF 后烧录新版。通信中断不重发包装命令。此流程尚待实板验收。
