#ifndef T2620_PROJECT_CONFIG_H
#define T2620_PROJECT_CONFIG_H

/* -------------------------------------------------------------------------- */
/* 片内 Flash 布局                                                            */
/* -------------------------------------------------------------------------- */

/*
 * 新主控的片内 Flash 容量为 32 Mbit，即 4 MiB（0x400000 字节）。
 * sdk_config.h 由杰理配置工具生成，目前只能生成 16 Mbit（0x200000）的配置；app_config.h
 * 会在 sdk_config.h 之后包含本文件，因此在此覆盖容量可以同时作用于链接脚本和打包配置，
 * 并且不会在配置工具重新生成 sdk_config.h 时丢失。
 *
 * 工程已开启双备份，4 MiB 配置下打包工具给出的单个代码区分界线为 0x1FF000；
 * 当前 CODE0 为 0x102000，剩余 0xFD000，满足双备份固件的空间要求。
 */
#undef CONFIG_FLASH_SIZE
#define CONFIG_FLASH_SIZE                         0x400000

/*
 * 原 2 MiB 布局的 VM 起始地址为 0x1FC000，即位于 Flash 末尾前 0x4000 处。
 * Flash 扩大到 4 MiB 后保持相同的尾部布局，所以 VM 起始地址相应改为 0x3FC000。
 * 当前 TCFG_VM_SIZE 为 8 KiB，打包结果中 VM 占用 0x3FC000~0x3FE000，随后
 * 0x3FE000~0x3FF000 为 BTIF 保留区。
 */
#define CONFIG_VM_ADDR                            0x3FC000

/*
 * T2620 项目级配置覆盖。
 *
 * 本文件紧跟在杰理配置工具生成的 sdk_config.h 之后包含，只保留新增功能
 * 开关及其必要的联合配置。工具已经管理且取值正确的配置不要在这里重复定义。
 */

/* -------------------------------------------------------------------------- */
/* 硬件资源归属                                                               */
/* -------------------------------------------------------------------------- */

/* PB1 专用于拨码电源开关。 */
#ifndef TCFG_DIP_SWITCH_POWER_ENABLE
#define TCFG_DIP_SWITCH_POWER_ENABLE              1
#endif

#ifndef TCFG_DIP_SWITCH_POWER_IO
#define TCFG_DIP_SWITCH_POWER_IO                  IO_PORTB_01
#endif

/* -------------------------------------------------------------------------- */
/* PC 存储接管                                                                */
/* -------------------------------------------------------------------------- */

/* 可选的 SD0 导出功能。SD0/MSC 硬件配置由 JL Studio 管理，
 * 本产品配置层负责 PC 模式及互斥的 USB 类别。
 * 充电、固定存储介质注册和启动数据保护独立于导出功能。 */
#ifndef TCFG_T2620_PC_STORAGE_ENABLE
#define TCFG_T2620_PC_STORAGE_ENABLE               0
#endif

/* 挂载失败（包括缺少 VM 标记）不代表空盘。
 * 每次启动均保留用户数据，包括纯充电和 PC 模式启动。
 * 首次初始化或故障恢复必须通过显式格式化操作完成。 */
#define TCFG_T2620_STORAGE_PRESERVE_ON_BOOT         1

/* 充电与业务共存策略独立于 USB 枚举功能。 */
#ifndef TCFG_T2620_CHARGE_COEXIST_ENABLE
#define TCFG_T2620_CHARGE_COEXIST_ENABLE           1
#endif

/* 板载焊接 SD 常在线属于硬件属性，录音和 MSC 均依赖此配置。 */
#if TCFG_SD0_ENABLE
#undef TCFG_SD_ALWAY_ONLINE_ENABLE
#define TCFG_SD_ALWAY_ONLINE_ENABLE                1
#endif

#if TCFG_T2620_PC_STORAGE_ENABLE
#if !TCFG_SD0_ENABLE
#error "T2620 PC storage requires SD0"
#endif

#if !TCFG_USB_SLAVE_MSD_ENABLE
#error "T2620 PC storage requires USB MSC"
#endif

#undef TCFG_APP_PC_EN
#define TCFG_APP_PC_EN                             1

#undef TCFG_USB_SLAVE_HID_ENABLE
#define TCFG_USB_SLAVE_HID_ENABLE                  0

#undef TCFG_USB_SLAVE_AUDIO_SPK_ENABLE
#define TCFG_USB_SLAVE_AUDIO_SPK_ENABLE            0

#undef TCFG_USB_SLAVE_AUDIO_MIC_ENABLE
#define TCFG_USB_SLAVE_AUDIO_MIC_ENABLE            0
#else
/* 即使 JL Studio 后续启用通用 PC 模式，此处仍强制关闭，防止意外出盘。 */
#undef TCFG_APP_PC_EN
#define TCFG_APP_PC_EN                             0
#endif

#undef TCFG_PC_ENABLE
#define TCFG_PC_ENABLE                            TCFG_APP_PC_EN

/* -------------------------------------------------------------------------- */
/* USB CDC 产测：仅在台架测试固件中显式启用。 */
/* -------------------------------------------------------------------------- */
#ifndef TCFG_T2620_FACTORY_USB_CDC_ENABLE
#define TCFG_T2620_FACTORY_USB_CDC_ENABLE          0
#endif

/* 原始数据回显及主动发送 00..FF 测试数据；禁止与第三阶段协议同时启用。 */
#ifndef TCFG_T2620_FACTORY_USB_CDC_TEST_ENABLE
#define TCFG_T2620_FACTORY_USB_CDC_TEST_ENABLE     0
#endif
/* COM3 日志：0=关闭，1=每秒汇总接收/发送完成情况及生命周期日志，2=限长报文十六进制日志。 */
#ifndef TCFG_T2620_FACTORY_USB_CDC_LOG_LEVEL
#define TCFG_T2620_FACTORY_USB_CDC_LOG_LEVEL 1
#endif
#if TCFG_T2620_FACTORY_USB_CDC_LOG_LEVEL < 0 || TCFG_T2620_FACTORY_USB_CDC_LOG_LEVEL > 2
#error "Factory CDC log level must be 0, 1 or 2"
#endif

#if TCFG_T2620_FACTORY_USB_CDC_TEST_ENABLE && !TCFG_T2620_FACTORY_USB_CDC_ENABLE
#error "Factory CDC byte test requires factory CDC"
#endif

#if TCFG_T2620_FACTORY_USB_CDC_ENABLE
#undef TCFG_USB_CDC_BACKGROUND_RUN
#define TCFG_USB_CDC_BACKGROUND_RUN               1
/* CDC 与 MSC 分别枚举；使用 BR28 硬件端点 1/2。 */
#define CDC_DATA_EP_IN                            1
#define CDC_DATA_EP_OUT                           1
#define CDC_INTR_EP_IN                            2
#define CDC_INTR_EP_ENABLE                        1
#endif

/* -------------------------------------------------------------------------- */
/* RDX 与 HOGP 功能                                                           */
/* -------------------------------------------------------------------------- */

/*
 * T2620 外置功放使能：PE5 高有效。开机默认电平由板级配置保持为低，
 * 运行时只允许 RDX/T2620 外设电源模块跟随 DAC 模拟电源生命周期驱动。
 */
#ifndef TCFG_T2620_AMP_POWER_ENABLE
#define TCFG_T2620_AMP_POWER_ENABLE               1
#endif

#ifndef TCFG_T2620_AMP_ENABLE_IO
#define TCFG_T2620_AMP_ENABLE_IO                   IO_PORTE_05
#endif

/*
 * T2620 共享外设 VDD：PA4 高有效，同时给板载 SD NAND 与外置 RGB 供电。
 * DRY_RUN 和 UNMOUNT_ONLY 已完成核心链路板测，当前进入 POWER_CUT：
 * SD/RGB 安全停用后拉低 PA4，业务恢复时先拉高 PA4 再恢复外设。
 * UNMOUNT_ONLY 未执行的播放、USB MSC 及多轮循环项已记入设计文档，
 * 并移入 POWER_CUT 聚焦回归，不得将其记录为已通过。
 */
#define T2620_SHARED_VDD_MODE_DRY_RUN              0
#define T2620_SHARED_VDD_MODE_UNMOUNT_ONLY         1
#define T2620_SHARED_VDD_MODE_POWER_CUT            2

#ifndef TCFG_T2620_SHARED_VDD_ENABLE
#define TCFG_T2620_SHARED_VDD_ENABLE               1
#endif

#ifndef TCFG_T2620_SHARED_VDD_IO
#define TCFG_T2620_SHARED_VDD_IO                   IO_PORTA_04
#endif

#ifndef TCFG_T2620_SHARED_VDD_MODE
#define TCFG_T2620_SHARED_VDD_MODE                 T2620_SHARED_VDD_MODE_POWER_CUT
#endif

/* PA4 真正断电后的 LDO/SD NAND 上电稳定窗口；1 个 OS tick 约 10 ms。 */
#ifndef TCFG_T2620_SHARED_VDD_POWER_STABLE_TICKS
#define TCFG_T2620_SHARED_VDD_POWER_STABLE_TICKS  1
#endif

#if (TCFG_T2620_SHARED_VDD_MODE < T2620_SHARED_VDD_MODE_DRY_RUN) || \
    (TCFG_T2620_SHARED_VDD_MODE > T2620_SHARED_VDD_MODE_POWER_CUT)
#error "Invalid T2620 shared VDD mode"
#endif

/* 在 RDX 复合 GATT Profile 中启用 HOGP 键盘。 */
#ifndef TCFG_RDX_HOGP_ENABLE
#define TCFG_RDX_HOGP_ENABLE                      1
#endif

/* 联调时打印 RDX App 下行 ATT 命令；量产版本可在本覆盖层关闭。 */
#ifndef TCFG_RDX_APP_RX_TRACE_ENABLE
#define TCFG_RDX_APP_RX_TRACE_ENABLE              1
#endif

/* -------------------------------------------------------------------------- */
/* RDX 录音与本地播放                                                         */
/* -------------------------------------------------------------------------- */

/* 本地录音播放总开关；关闭时一并移除其解码依赖以释放 CODE0。 */
#ifndef TCFG_RDX_LOCAL_PLAYBACK_ENABLE
#define TCFG_RDX_LOCAL_PLAYBACK_ENABLE            1
#endif

/* 本地播放需要杰理双声道 Opus 解码器。 */
#ifndef TCFG_DEC_STENC_OPUS_ENABLE
#define TCFG_DEC_STENC_OPUS_ENABLE                TCFG_RDX_LOCAL_PLAYBACK_ENABLE
#endif

/*
 * 录音文件是无头、固定 80 字节的 CBR Opus 帧。杰理解码库使用该开关启用
 * raw/CBR 输入配置；虽然宏名包含 OGG，本地播放仍必须与总开关联动。
 */
#ifndef TCFG_DEC_OGG_OPUS_ENABLE
#define TCFG_DEC_OGG_OPUS_ENABLE                  TCFG_RDX_LOCAL_PLAYBACK_ENABLE
#endif

#if TCFG_RDX_LOCAL_PLAYBACK_ENABLE && \
    (!TCFG_DEC_STENC_OPUS_ENABLE || !TCFG_DEC_OGG_OPUS_ENABLE)
#error "RDX local playback requires stereo and raw/CBR Opus decoding"
#endif

#endif /* T2620_PROJECT_CONFIG_H */
