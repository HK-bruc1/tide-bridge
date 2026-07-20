#ifndef __RDX_CONFIG_IDS_H__
#define __RDX_CONFIG_IDS_H__

//App list. (max 32)
#define	APP_NEVIEW_EN									(1 << 0)
#define APP_XLSW_EN										(1 << 1)
#define APP_GNT_EN										(1 << 2)
#define APP_XYZL_EN										(1 << 3)
#define APP_NINGQU_EN									(1 << 4)
#define APP_NOTTA_EN									(1 << 5)
#define APP_TINGNAO_EN									(1 << 6)  // 听脑
#define APP_JMEASY_EN									(1 << 7)
#define APP_SHENGLANG_EN								(1 << 8)
#define APP_AITIR_EN									(1 << 9)  // 云译
#define APP_YYS_EN										(1 << 10) // 语亦思
#define APP_LYNSE_EN									(1 << 11) // 灵识
#define APP_TURING_EN									(1 << 12) // 图瓴
#define APP_RAYCON_EN									(1 << 13)
#define APP_CDJY_EN										(1 << 14) // 传德教育
#define APP_BRANDWORKS_EN								(1 << 15) // Brandworks
#define APP_FINDAI_EN									(1 << 16) // 发现力量
#define APP_WAN_EN										(1 << 17) // LAOWAN
#define APP_BEANSTALK_EN								(1 << 18) // Beanstalk
#define APP_ZENCHORD_EN									(1 << 19) // Zenchord
#define APP_DEEPMINER_EN								(1 << 20) // 明略科技deepminer
#define APP_TTEASY_EN									(1 << 21) // 凝趣3
#define APP_AISPEECH_EN									(1 << 22) // 思必驰
#define APP_SHUGUO_EN									(1 << 23) // 数果
#define APP_VASCO_EN									(1 << 24) // vasco
#define APP_ABC_EN										(1 << 25) // ABC
#define APP_MLAMPWXB_EN									(1 << 26) // 明略科技2 mlampwxb


//device list
#define DEVICE_RDX_EP_A9								(0x1000)  //RDX product series.
#define DEVICE_RDX_BJ_T2403								(0x1010)

#define DEVICE_DACOM_EP_T2401							(0x2000)  //DACOM product series.
#define DEVICE_DACOM_CC_T2401							(0x2010)

#define DEVICE_1MORE_EP_T2402							(0x2100)  //1MORE product series.
#define DEVICE_1MORE_CC_T2402							(0x2110)


#define DEVICE_ZENCORD_EP_T2616							(0x3000) //Zenchord Earphone.
#define DEVICE_ZENCORD_CC_T2616							(0x3010) //Zenchord Case.

#define RDX_SUPPORT_OLED                                (0x01)
#define RDX_SUPPORT_EMMC                                (0x10)
#define RDX_SUPPORT_BOTH_OLED_EMMC                      (0x11)

#define RDX_RTC_PATH_SOFTWARE                           (0)
#define RDX_RTC_PATH_HARDWARE                           (1)

#define BJ_BOARD_VERSION_00                             (0)
#define BJ_BOARD_VERSION_01                             (1)
#define BJ_BOARD_VERSION_02                             (2)
#define BJ_BOARD_VERSION_03                             (3)

#define RDX_PACKAGE_VERIFY_CHECKSUM                     (0x01)
#define RDX_PACKAGE_VERIFY_CRC32                        (0x10)

#define TRANSFER_BY_TCP                                 (0)
#define TRANSFER_BY_UDP                                 (1)

#define WIFI_CTRL_BUS_UART                              (0)
#define WIFI_CTRL_BUS_SPI                               (1)

#endif
