#ifndef T2620_PROJECT_CONFIG_H
#define T2620_PROJECT_CONFIG_H

/*
 * T2620 project-level overrides.
 *
 * Keep these definitions outside sdk_config.h/c because those files are owned
 * by the JL visual configuration tool.
 */

/* T2620 currently uses the T2616 feature set with a customer-neutral RDX app. */
#ifndef RDX_AI_SEL_APP
#define RDX_AI_SEL_APP                           APP_CUSTOM_TEST_EN
#endif

#ifndef RDX_SEL_DEVICE
#define RDX_SEL_DEVICE                           DEVICE_BEANSTALK_RKB_T2620
#endif

#ifndef TCFG_DIP_SWITCH_POWER_ENABLE
#define TCFG_DIP_SWITCH_POWER_ENABLE              1
#endif

#ifndef TCFG_DIP_SWITCH_POWER_IO
#define TCFG_DIP_SWITCH_POWER_IO                  IO_PORTB_01
#endif

/* USB PC mode exposes SD0 as the product's only USB class.
 * USB insertion while the DIP switch is OFF remains charge-only. */
#undef TCFG_CHARGE_POWERON_ENABLE
#define TCFG_CHARGE_POWERON_ENABLE                 0

#undef TCFG_APP_PC_EN
#define TCFG_APP_PC_EN                             1

#undef TCFG_USB_SLAVE_MSD_ENABLE
#define TCFG_USB_SLAVE_MSD_ENABLE                  1

/* SD0 is soldered SD NAND, not removable media. Keep detect events stable
 * when the FAT owner changes between the device and USB MSC. */
#undef TCFG_SD_ALWAY_ONLINE_ENABLE
#define TCFG_SD_ALWAY_ONLINE_ENABLE                1

#undef TCFG_USB_SLAVE_HID_ENABLE
#define TCFG_USB_SLAVE_HID_ENABLE                  0

#undef TCFG_USB_SLAVE_AUDIO_SPK_ENABLE
#define TCFG_USB_SLAVE_AUDIO_SPK_ENABLE            0

#undef TCFG_USB_SLAVE_AUDIO_MIC_ENABLE
#define TCFG_USB_SLAVE_AUDIO_MIC_ENABLE            0

#undef TCFG_USB_SLAVE_CDC_ENABLE
#define TCFG_USB_SLAVE_CDC_ENABLE                  0

#undef TCFG_USB_CUSTOM_HID_ENABLE
#define TCFG_USB_CUSTOM_HID_ENABLE                 0

#undef TCFG_USB_SLAVE_MTP_ENABLE
#define TCFG_USB_SLAVE_MTP_ENABLE                  0

#undef TCFG_USB_SLAVE_MIDI_ENABLE
#define TCFG_USB_SLAVE_MIDI_ENABLE                 0

#undef TCFG_USB_SLAVE_PRINTER_ENABLE
#define TCFG_USB_SLAVE_PRINTER_ENABLE              0

/* Never format user storage merely because a mount attempt failed. */
#undef TCFG_SD0_AUTO_FORMAT_ON_MOUNT_FAIL_ENABLE
#define TCFG_SD0_AUTO_FORMAT_ON_MOUNT_FAIL_ENABLE  0

#if TCFG_SD0_AUTO_FORMAT_ON_MOUNT_FAIL_ENABLE
#error "T2620 production firmware must not auto-format SD0 on mount failure"
#endif

#if TCFG_DIP_SWITCH_POWER_ENABLE
/*
 * The current generated ADKEY and LP_TOUCH defaults use PB1. PB1 is reserved
 * for the DIP power switch on T2620, so keep those modules disabled here even
 * if the visual tool regenerates sdk_config.h.
 */
#undef TCFG_ADKEY_ENABLE
#define TCFG_ADKEY_ENABLE                         0

#undef TCFG_LP_TOUCH_KEY_ENABLE
#define TCFG_LP_TOUCH_KEY_ENABLE                  0
#endif

#ifndef TCFG_RDX_HOGP_ENABLE
#define TCFG_RDX_HOGP_ENABLE                      1
#endif

#ifndef TCFG_RDX_LOCAL_PLAYBACK_ENABLE
#define TCFG_RDX_LOCAL_PLAYBACK_ENABLE            1
#endif

/* RDX recordings are headerless, fixed-size standard Opus packets. */
#ifndef TCFG_DEC_OGG_OPUS_ENABLE
#define TCFG_DEC_OGG_OPUS_ENABLE                  1
#endif

/* RDX local recordings use JL stereo Opus packets: 16 kHz, 2 ch, 20 ms, 80 B. */
#ifndef TCFG_STENC_OPUS_ENABLE
#define TCFG_STENC_OPUS_ENABLE                    1
#endif

#ifndef TCFG_DEC_STENC_OPUS_ENABLE
#define TCFG_DEC_STENC_OPUS_ENABLE                TCFG_RDX_LOCAL_PLAYBACK_ENABLE
#endif

#ifndef RDX_HOGP_KEY_ACTION_TEST_ENABLE
#define RDX_HOGP_KEY_ACTION_TEST_ENABLE                0
#endif

#endif /* T2620_PROJECT_CONFIG_H */
