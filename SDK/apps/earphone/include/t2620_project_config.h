#ifndef T2620_PROJECT_CONFIG_H
#define T2620_PROJECT_CONFIG_H

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

/* 在 RDX 复合 GATT Profile 中启用 HOGP 键盘。 */
#ifndef TCFG_RDX_HOGP_ENABLE
#define TCFG_RDX_HOGP_ENABLE                      1
#endif

/* -------------------------------------------------------------------------- */
/* RDX 录音与本地播放                                                         */
/* -------------------------------------------------------------------------- */

/* 量产固件关闭本地播放，以避免 CODE0 空间超限。 */
#ifndef TCFG_RDX_LOCAL_PLAYBACK_ENABLE
#define TCFG_RDX_LOCAL_PLAYBACK_ENABLE            0
#endif

/* 录音使用杰理双声道 Opus 包：16 kHz、双声道、20 ms、80 字节。 */
#ifndef TCFG_STENC_OPUS_ENABLE
#define TCFG_STENC_OPUS_ENABLE                    1
#endif

/* 仅在启用本地播放时加入对应的双声道 Opus 解码器。 */
#ifndef TCFG_DEC_STENC_OPUS_ENABLE
#define TCFG_DEC_STENC_OPUS_ENABLE                TCFG_RDX_LOCAL_PLAYBACK_ENABLE
#endif

#endif /* T2620_PROJECT_CONFIG_H */
