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

/*
 * 开启后进入仅导出 SD0 的 USB MSC PC 模式。SD0 和 MSC 由杰理配置工具配置，
 * 此处校验依赖，并联动 PC 模式、SD0 常在线及互斥的 USB Class。开机充电是
 * 独立产品策略，不属于 PC 存储功能的依赖。
 */
#ifndef TCFG_T2620_PC_STORAGE_ENABLE
#define TCFG_T2620_PC_STORAGE_ENABLE               1
#endif

#if TCFG_T2620_PC_STORAGE_ENABLE
#if !TCFG_SD0_ENABLE
#error "T2620 PC storage requires SD0"
#endif

#if !TCFG_USB_SLAVE_MSD_ENABLE
#error "T2620 PC storage requires USB MSC"
#endif

/*
 * PC 接管前要求板载 SD NAND 已注册；常在线可在启动时直接加入 SD0，并停止
 * 插拔检测，避免 USB MSC 接管 FAT 期间产生伪插拔事件。该行为属于本项目的
 * PC 存储约束，因此覆盖原生取值。
 */
#undef TCFG_SD_ALWAY_ONLINE_ENABLE
#define TCFG_SD_ALWAY_ONLINE_ENABLE                1

#undef TCFG_APP_PC_EN
#define TCFG_APP_PC_EN                             1

#undef TCFG_USB_SLAVE_HID_ENABLE
#define TCFG_USB_SLAVE_HID_ENABLE                  0

#undef TCFG_USB_SLAVE_AUDIO_SPK_ENABLE
#define TCFG_USB_SLAVE_AUDIO_SPK_ENABLE            0

#undef TCFG_USB_SLAVE_AUDIO_MIC_ENABLE
#define TCFG_USB_SLAVE_AUDIO_MIC_ENABLE            0
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
