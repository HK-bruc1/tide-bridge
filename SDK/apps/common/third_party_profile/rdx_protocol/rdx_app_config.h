/*=====================================================================================
 HEADER NAME: rdx_app_config.h
 MODULE NAME: rdx application config module headfile.
 
 PRE-INCLUDE FILES DESCRIPTION: 	
 
 GENERAL DESCRIPTION: 	
 	This File will gather functions that special handle msg(UserEvent,Timer) from 
 	Intergration.These functions don't be changed by project changed.
 =======================================================================================	
 Revision History: 
 ---------------------------------------
 Author: sheng.dong
 Date: 2024-09-16 17:38:51
 LastEditors: sheng.dong
 LastEditTime: 2024-10-16 14:30:47
 FilePath: \SDK\apps\common\third_party_profile\rdx_protocol\rdx_app_config.h
 
 Self-documenting Code
=====================================================================================*/

#ifndef __RDX_APP_CONFIG_H__
#define __RDX_APP_CONFIG_H__

/******************************************************************************
* Include files
******************************************************************************/ 
#include "rdx_common.h"

/******************************************************************************
* Macro Define Section
******************************************************************************/ 
//FW_INFO
#define FIRMWARE_NAME									"rdxos_ble_ai_recorder"

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


/*注意App和硬件的适配*/
//choose Application.
#define RDX_AI_SEL_APP									APP_ZENCHORD_EN//APP_BRANDWORKS_EN//APP_NEVIEW_EN //APP_TURING_EN //APP_NOTTA_EN
//choose hardware.
#define RDX_SEL_DEVICE									DEVICE_ZENCORD_CC_T2616 //DEVICE_ZENCORD_CC_T2616

//----------------------------------------------------------------------------
// 产品形态分类宏 (由 RDX_SEL_DEVICE 派生, 与 PRODUCT_CODE 对应)
//   - "601" : 耳机本体 (EP)              — DEVICE_*_EP_*
//   - "602" : 录音卡片 (Record Card)     — DEVICE_RDX_BJ_T2403 等
//   - "603" : 耳机仓 (Charge Case, CC)   — DEVICE_*_CC_*
//   - "604" : 录音 PIN
//
// RDX_PRODUCT_IS_CHARGE_CASE = 1 时启用仓配对 (*APP#devpair / devunpair),
// EarphoneInfo 持久化, BLE readchar 含 ep_mac/case_mac/wifi_mac 全量信息;
// 其它产品 (录音卡片/PIN) 维持原有"仅写 AuthKey"的认证码上报行为.
//----------------------------------------------------------------------------
#if (RDX_SEL_DEVICE == DEVICE_ZENCORD_CC_T2616) || \
    (RDX_SEL_DEVICE == DEVICE_DACOM_CC_T2401) || \
    (RDX_SEL_DEVICE == DEVICE_1MORE_CC_T2402)
#define RDX_PRODUCT_IS_CHARGE_CASE						(1)
#else
#define RDX_PRODUCT_IS_CHARGE_CASE						(0)
#endif

#define RDX_PRODUCT_IS_RECORD_CARD						(!RDX_PRODUCT_IS_CHARGE_CASE)

#define RDX_SUPPORT_MOTOR								(0) // 当前硬件无马达

#define RDX_WIFI_ENABLE									(0) // 当前硬件无 WiFi 模块

#define RDX_SUPPORT_OLED								(0x01)
#define RDX_SUPPORT_EMMC								(0x10)
#define RDX_SUPPORT_BOTH_OLED_EMMC						(0x11)
#define RDX_MULTI_FUNC_INTERFACE						RDX_SUPPORT_EMMC //RDX_SUPPORT_BOTH_OLED_EMMC

#define RDX_SUPPORT_ALGORITHM							(1)

//RTC实现路径 软件模拟/硬件RTC
#define RDX_RTC_PATH_SOFTWARE							(0)
#define RDX_RTC_PATH_HARDWARE							(1)

#ifndef RDX_RTC_PATH_SEL
#define RDX_RTC_PATH_SEL								RDX_RTC_PATH_HARDWARE
#endif

/* Phase 6 C5: unified gate for the built-in HOGP test keymap and KEY1
 * triple-click mode toggle. Defaults to 0; T2620 enables it in
 * t2620_project_config.h. */
#ifndef RDX_HOGP_KEY_ACTION_TEST_ENABLE
#define RDX_HOGP_KEY_ACTION_TEST_ENABLE             0
#endif

#if (RDX_RTC_PATH_SEL != RDX_RTC_PATH_SOFTWARE) && (RDX_RTC_PATH_SEL != RDX_RTC_PATH_HARDWARE)
#error "RDX_RTC_PATH_SEL must be RDX_RTC_PATH_SOFTWARE or RDX_RTC_PATH_HARDWARE"
#endif

#define BJ_BOARD_VERSION_00								(0)
#define BJ_BOARD_VERSION_01								(1) //新版本
#define BJ_BOARD_VERSION_02								(2) //WIFI + EMMC + OLED
#define BJ_BOARD_VERSION_03								(3)

#if (RDX_SEL_DEVICE == DEVICE_RDX_BJ_T2403) || (RDX_SEL_DEVICE == DEVICE_ZENCORD_CC_T2616)
#define RDX_BJ_VERSION									BJ_BOARD_VERSION_03
#endif


#define RDX_DEFAULT_SHUT_DOWN_TIME						(36000u) //minutes 

//Transmit Verification way.
#define RDX_PACKAGE_VERIFY_CHECKSUM						(0x01)
#define RDX_PACKAGE_VERIFY_CRC32						(0x10)

#define TRANSFER_BY_TCP									(0)
#define TRANSFER_BY_UDP									(1)
#define WIFI_COMMUNICATION_TYPE							TRANSFER_BY_TCP


#define WIFI_CTRL_BUS_UART								(0)
#define WIFI_CTRL_BUS_SPI								(1)

#define WIFI_CTRL_BUS_SELECT							WIFI_CTRL_BUS_SPI

//----------------------------------------------------------------------------
// WiFi AP 配置 (port/app 层选择, 通过 rdx_app.c 里的 RdxWifiCfg 静态实例
// 注入到 xxpUart 库, 见 xxp_uart_register_wifi_cfg). lib 不再直接读这些宏.
//
//  WIFI_AP_SSID_PSW_DYN_GENERATE  : 1 = SSID 加 suffix + 密码 SHA256(auth+MAC)
//                                   0 = SSID/密码直接使用 WIFI_AP_SSID/PASSWORD
//
//  WIFI_AP_SSID_SUFFIX_MODE       : 仅 PSW_DYN_GENERATE=1 时生效. 取值是
//                                   xxpUart.h::WifiApSsidSuffixMode 的枚举名
//                                   (token 替换, 由编译器解析为枚举常量):
//      WIFI_AP_SSID_SUFFIX_NONE       不加后缀
//      WIFI_AP_SSID_SUFFIX_MAC_TAIL3  ap_ssid_<MAC末3字节hex>  例 "Octic_C4F3D5"
//      WIFI_AP_SSID_SUFFIX_AUTH_TAIL4 ap_ssid_<auth SN末4位>   例 "Octic_M3MU"
//
// 默认: 动态密码 + MAC 末 3 字节; 个别产品需要切到 auth 末 4 位时, 可在该
// 产品自己的 #if (RDX_AI_SEL_APP & APP_xxx_EN) 段内 #undef + 重定义.
//----------------------------------------------------------------------------
#define WIFI_AP_SSID_PSW_DYN_GENERATE					(1)

#define WIFI_AP_SSID_SUFFIX_MODE						WIFI_AP_SSID_SUFFIX_MAC_TAIL3

//=======================================================================================
#if (RDX_AI_SEL_APP & APP_NEVIEW_EN)
//-------------------- device model --------------------
#if (RDX_SEL_DEVICE == DEVICE_RDX_BJ_T2403)

// #define PRODUCT_TYPE							"A2"

// //AI translate.
// #define RDX_AI_TRANSLATE_SUPPORT				(0)
// //BLE advertise messages.
// #define BT_NAME                             	"AI Note"//"NEVW_AIEP"//"NvAI"
// #define BLE_LOCAL_NAME                      	"AI Note"//"GLOBOTOK"//"GLBTK"
// //firmware & hardware version.
// #define FACTORY_CODE                            "NvEasy"//"NEVIEW"//"NvEasy"
// #define FACTORY_CODE_SIZE                       strlen(FACTORY_CODE)
// #define PRODUCT_CODE                            "602"
// #define PRODUCT_CODE_SIZE                       strlen(PRODUCT_CODE)
// //wifi information.
// #define WIFI_AP_SSID                            "AINote"
// #define WIFI_AP_PASSWORD                        "88888888"

// #define FIRMWARE_VERSION								"1.3.9"
// #define FIRMWARE_VERSION_HEX							0x00010309
// #define HARDWARE_VERSION								"0.3.0"
// #define HARDWARE_VERSION_HEX							0x00000300


#define PRODUCT_TYPE							"A0"

//AI translate.
#define RDX_AI_TRANSLATE_SUPPORT				(0)
//BLE advertise messages.
#define BT_NAME                             	"AI Pin"//"NEVW_AIEP"//"NvAI"
#define BLE_LOCAL_NAME                      	"AI Pin"//"GLOBOTOK"//"GLBTK"
//firmware & hardware version.
#define FACTORY_CODE                            "NvEasy"//"NEVIEW"//"NvEasy"
#define FACTORY_CODE_SIZE                       strlen(FACTORY_CODE)
#define PRODUCT_CODE                            "602"
#define PRODUCT_CODE_SIZE                       strlen(PRODUCT_CODE)
//wifi information.
#define WIFI_AP_SSID                            "AI Pin"
#define WIFI_AP_PASSWORD                        "88888888"

#define FIRMWARE_VERSION								"1.0.0"
#define FIRMWARE_VERSION_HEX							0x00010000
#define HARDWARE_VERSION								"0.0.1"
#define HARDWARE_VERSION_HEX							0x00000001

#endif

//=========================================================================================
#elif (RDX_AI_SEL_APP & APP_NINGQU_EN)

//-------------------- device model --------------------
#if (RDX_SEL_DEVICE == DEVICE_RDX_BJ_T2403)

#define PRODUCT_TYPE							"M9"

//AI translate.
#define RDX_AI_TRANSLATE_SUPPORT				(0)
//BLE advertise messages.
#define BT_NAME                             	"AI Note"
#define BLE_LOCAL_NAME                      	"AI Note"
//firmware & hardware version.
#define FACTORY_CODE                            "NqEasy"
#define FACTORY_CODE_SIZE                       strlen(FACTORY_CODE)
#define PRODUCT_CODE                            "602"
#define PRODUCT_CODE_SIZE                       strlen(PRODUCT_CODE)
//wifi information.
#define WIFI_AP_SSID                            "AINote"
#define WIFI_AP_PASSWORD                        "88888888"

#define FIRMWARE_VERSION								"1.4.1"
#define FIRMWARE_VERSION_HEX							0x00010401
#define HARDWARE_VERSION								"0.3.0"
#define HARDWARE_VERSION_HEX							0x00000300

#endif

//=========================================================================================
#elif (RDX_AI_SEL_APP & APP_NOTTA_EN)

//-------------------- device model --------------------
#if (RDX_SEL_DEVICE == DEVICE_RDX_BJ_T2403)

#define PRODUCT_TYPE							"M2"

//AI translate.
#define RDX_AI_TRANSLATE_SUPPORT				(0)
//BLE advertise messages.
#define BT_NAME                             	"Notta Memo"
#define BLE_LOCAL_NAME                      	"Notta Memo"
//firmware & hardware version.
#define FACTORY_CODE                            "NottaH"
#define FACTORY_CODE_SIZE                       strlen(FACTORY_CODE)
#define PRODUCT_CODE                            "602"
#define PRODUCT_CODE_SIZE                       strlen(PRODUCT_CODE)
//wifi information.
#define WIFI_AP_SSID                            "NottaMemo"
#define WIFI_AP_PASSWORD                        "88888888"

#define FIRMWARE_VERSION								"1.3.9"
#define FIRMWARE_VERSION_HEX							0x00010309
#define HARDWARE_VERSION								"0.3.0"
#define HARDWARE_VERSION_HEX							0x00000300

#endif

//=========================================================================================
#elif (RDX_AI_SEL_APP & APP_TINGNAO_EN)

//-------------------- device model --------------------
#if (RDX_SEL_DEVICE == DEVICE_RDX_BJ_T2403)

#define PRODUCT_TYPE							"T1"

//AI translate.
#define RDX_AI_TRANSLATE_SUPPORT				(0)
//BLE advertise messages.
#define BT_NAME                             	"TinCardT1"
#define BLE_LOCAL_NAME                      	"TinCardT1"
//firmware & hardware version.
#define FACTORY_CODE                            "TnCard"
#define FACTORY_CODE_SIZE                       strlen(FACTORY_CODE)
#define PRODUCT_CODE                            "602"
#define PRODUCT_CODE_SIZE                       strlen(PRODUCT_CODE)
//wifi information.
#define WIFI_AP_SSID                            "TinCardT1"
#define WIFI_AP_PASSWORD                        "88888888"

#define FIRMWARE_VERSION								"1.4.2"
#define FIRMWARE_VERSION_HEX							0x00010402
#define HARDWARE_VERSION								"0.3.0"
#define HARDWARE_VERSION_HEX							0x00000300

#endif

//=========================================================================================
#elif (RDX_AI_SEL_APP & APP_JMEASY_EN)

//-------------------- device model --------------------
#if (RDX_SEL_DEVICE == DEVICE_RDX_BJ_T2403)

#define PRODUCT_TYPE							"M9"

//AI translate.
#define RDX_AI_TRANSLATE_SUPPORT				(0)
//BLE advertise messages.
#define BT_NAME                             	"AI Note"
#define BLE_LOCAL_NAME                      	"AI Note"
//firmware & hardware version.
#define FACTORY_CODE                            "jmeasy"
#define FACTORY_CODE_SIZE                       strlen(FACTORY_CODE)
#define PRODUCT_CODE                            "602"
#define PRODUCT_CODE_SIZE                       strlen(PRODUCT_CODE)
//wifi information.
#define WIFI_AP_SSID                            "AINote"
#define WIFI_AP_PASSWORD                        "88888888"

#define FIRMWARE_VERSION								"1.4.1"
#define FIRMWARE_VERSION_HEX							0x00010401
#define HARDWARE_VERSION								"0.3.0"
#define HARDWARE_VERSION_HEX							0x00000300


#endif

//=========================================================================================
#elif (RDX_AI_SEL_APP & APP_SHENGLANG_EN)

//-------------------- device model --------------------
#if (RDX_SEL_DEVICE == DEVICE_RDX_BJ_T2403)

#define PRODUCT_TYPE							"P1"

//AI translate.
#define RDX_AI_TRANSLATE_SUPPORT				(0)
//BLE advertise messages.
#define BT_NAME                             	"OLA Memo"
#define BLE_LOCAL_NAME                      	"OLA Memo"
//firmware & hardware version.
#define FACTORY_CODE                            "OLA_AI"
#define FACTORY_CODE_SIZE                       strlen(FACTORY_CODE)
#define PRODUCT_CODE                            "602"
#define PRODUCT_CODE_SIZE                       strlen(PRODUCT_CODE)
//wifi information.
#define WIFI_AP_SSID                            "OLAMemo"
#define WIFI_AP_PASSWORD                        "88888888"

#define FIRMWARE_VERSION								"1.0.0"
#define FIRMWARE_VERSION_HEX							0x00010000
#define HARDWARE_VERSION								"0.3.0"
#define HARDWARE_VERSION_HEX							0x00000300

#endif

//=========================================================================================
#elif (RDX_AI_SEL_APP & APP_AITIR_EN)

//-------------------- device model --------------------
#if (RDX_SEL_DEVICE == DEVICE_RDX_BJ_T2403)

#define PRODUCT_TYPE							"A0"

//AI translate.
#define RDX_AI_TRANSLATE_SUPPORT				(0)
//BLE advertise messages.
#define BT_NAME                             	"AITIR Note"
#define BLE_LOCAL_NAME                      	"AITIR Note"
//firmware & hardware version.
#define FACTORY_CODE                            "NvEasy"
#define FACTORY_CODE_SIZE                       strlen(FACTORY_CODE)
#define PRODUCT_CODE                            "602"
#define PRODUCT_CODE_SIZE                       strlen(PRODUCT_CODE)
//wifi information.
#define WIFI_AP_SSID                            "AITIRNote"
#define WIFI_AP_PASSWORD                        "88888888"

#define FIRMWARE_VERSION								"1.4.3"
#define FIRMWARE_VERSION_HEX							0x00010403
#define HARDWARE_VERSION								"0.3.0"
#define HARDWARE_VERSION_HEX							0x00000300

#endif

//=========================================================================================
#elif (RDX_AI_SEL_APP & APP_YYS_EN)

//-------------------- device model --------------------
#if (RDX_SEL_DEVICE == DEVICE_RDX_BJ_T2403)

#define PRODUCT_TYPE							"A1"

//AI translate.
#define RDX_AI_TRANSLATE_SUPPORT				(0)
//BLE advertise messages.
#define BT_NAME                             	"My Pal"
#define BLE_LOCAL_NAME                      	"My Pal"
//firmware & hardware version.
#define FACTORY_CODE                            "YYSYYS"
#define FACTORY_CODE_SIZE                       strlen(FACTORY_CODE)
#define PRODUCT_CODE                            "602"
#define PRODUCT_CODE_SIZE                       strlen(PRODUCT_CODE)
//wifi information.
#define WIFI_AP_SSID                            "My Pal"
#define WIFI_AP_PASSWORD                        "88888888"

#define FIRMWARE_VERSION								"1.4.4"
#define FIRMWARE_VERSION_HEX							0x00010404
#define HARDWARE_VERSION								"0.3.0"
#define HARDWARE_VERSION_HEX							0x00000300

#endif

//=========================================================================================
#elif (RDX_AI_SEL_APP & APP_LYNSE_EN)

//-------------------- device model --------------------
#if (RDX_SEL_DEVICE == DEVICE_RDX_BJ_T2403)

#define PRODUCT_TYPE							"L1"

//AI translate.
#define RDX_AI_TRANSLATE_SUPPORT				(0)
//BLE advertise messages.
#define BT_NAME                             	"Lynse AI"
#define BLE_LOCAL_NAME                      	"Lynse AI"
//firmware & hardware version.
#define FACTORY_CODE                            "ilynse"
#define FACTORY_CODE_SIZE                       strlen(FACTORY_CODE)
#define PRODUCT_CODE                            "602"
#define PRODUCT_CODE_SIZE                       strlen(PRODUCT_CODE)
//wifi information.
#define WIFI_AP_SSID                            "Lynse AI"
#define WIFI_AP_PASSWORD                        "88888888"

#define FIRMWARE_VERSION								"1.4.1"
#define FIRMWARE_VERSION_HEX							0x00010401
#define HARDWARE_VERSION								"0.3.0"
#define HARDWARE_VERSION_HEX							0x00000300

#endif

//=========================================================================================
#elif (RDX_AI_SEL_APP & APP_TURING_EN)

//-------------------- device model --------------------
#if (RDX_SEL_DEVICE == DEVICE_RDX_BJ_T2403)

#define PRODUCT_TYPE							"A1"

//AI translate.
#define RDX_AI_TRANSLATE_SUPPORT				(0)
//BLE advertise messages.
#define BT_NAME                             	"AI Note"
#define BLE_LOCAL_NAME                      	"AI Note"
//firmware & hardware version.
#define FACTORY_CODE                            "Turing"
#define FACTORY_CODE_SIZE                       strlen(FACTORY_CODE)
#define PRODUCT_CODE                            "602"
#define PRODUCT_CODE_SIZE                       strlen(PRODUCT_CODE)
//wifi information.
#define WIFI_AP_SSID                            "AINote"
#define WIFI_AP_PASSWORD                        "88888888"

#define FIRMWARE_VERSION								"1.3.9"
#define FIRMWARE_VERSION_HEX							0x00010309
#define HARDWARE_VERSION								"0.3.0"
#define HARDWARE_VERSION_HEX							0x00000300

#endif

//=========================================================================================
#elif (RDX_AI_SEL_APP & APP_RAYCON_EN)

//-------------------- device model --------------------
#if (RDX_SEL_DEVICE == DEVICE_RDX_BJ_T2403)

#define PRODUCT_TYPE							"A0"

//AI translate.
#define RDX_AI_TRANSLATE_SUPPORT				(0)
//BLE advertise messages.
#define BT_NAME                             	"Raycon AI"
#define BLE_LOCAL_NAME                      	"Raycon AI"
//firmware & hardware version.
#define FACTORY_CODE                            "Raycon"
#define FACTORY_CODE_SIZE                       strlen(FACTORY_CODE)
#define PRODUCT_CODE                            "602"
#define PRODUCT_CODE_SIZE                       strlen(PRODUCT_CODE)
//wifi information.
#define WIFI_AP_SSID                            "Raycon AI"
#define WIFI_AP_PASSWORD                        "88888888"

#define FIRMWARE_VERSION								"1.4.2"
#define FIRMWARE_VERSION_HEX							0x00010402
#define HARDWARE_VERSION								"0.3.0"
#define HARDWARE_VERSION_HEX							0x00000300

#endif

//=========================================================================================
#elif (RDX_AI_SEL_APP & APP_CDJY_EN)

//-------------------- device model --------------------
#if (RDX_SEL_DEVICE == DEVICE_RDX_BJ_T2403)

#define PRODUCT_TYPE							"A0"

//AI translate.
#define RDX_AI_TRANSLATE_SUPPORT				(0)
//BLE advertise messages.
#define BT_NAME                             	"CDJY AI"
#define BLE_LOCAL_NAME                      	"CDJY AI"
//firmware & hardware version.
#define FACTORY_CODE                            "cdjykj"
#define FACTORY_CODE_SIZE                       strlen(FACTORY_CODE)
#define PRODUCT_CODE                            "602"
#define PRODUCT_CODE_SIZE                       strlen(PRODUCT_CODE)
//wifi information.
#define WIFI_AP_SSID                            "CDJY AI"
#define WIFI_AP_PASSWORD                        "88888888"

#define FIRMWARE_VERSION								"1.4.1"
#define FIRMWARE_VERSION_HEX							0x00010401
#define HARDWARE_VERSION								"0.3.0"
#define HARDWARE_VERSION_HEX							0x00000300

#endif
//=========================================================================================

#elif (RDX_AI_SEL_APP & APP_BRANDWORKS_EN)

//-------------------- device model --------------------
#if (RDX_SEL_DEVICE == DEVICE_RDX_BJ_T2403)

#define PRODUCT_TYPE							"A0"

//AI translate.
#define RDX_AI_TRANSLATE_SUPPORT				(0)
//BLE advertise messages.
#define BT_NAME                             	"Brand AI"
#define BLE_LOCAL_NAME                      	"Brand AI"
//firmware & hardware version.
#define FACTORY_CODE                            "Brdwks"
#define FACTORY_CODE_SIZE                       strlen(FACTORY_CODE)
#define PRODUCT_CODE                            "602"
#define PRODUCT_CODE_SIZE                       strlen(PRODUCT_CODE)
//wifi information.
#define WIFI_AP_SSID                            "Brand AI"
#define WIFI_AP_PASSWORD                        "88888888"

#define FIRMWARE_VERSION								"1.0.1"
#define FIRMWARE_VERSION_HEX							0x00010001
#define HARDWARE_VERSION								"0.3.0"
#define HARDWARE_VERSION_HEX							0x00000300

#endif
//=========================================================================================

#elif (RDX_AI_SEL_APP & APP_FINDAI_EN)

//-------------------- device model --------------------
#if (RDX_SEL_DEVICE == DEVICE_RDX_BJ_T2403)

#define PRODUCT_TYPE							"A0"

//AI translate.
#define RDX_AI_TRANSLATE_SUPPORT				(0)
//BLE advertise messages.
#define BT_NAME                             	"FINDAI"
#define BLE_LOCAL_NAME                      	"FINDAI"
//firmware & hardware version.
#define FACTORY_CODE                            "FINDAI"
#define FACTORY_CODE_SIZE                       strlen(FACTORY_CODE)
#define PRODUCT_CODE                            "602"
#define PRODUCT_CODE_SIZE                       strlen(PRODUCT_CODE)
//wifi information.
#define WIFI_AP_SSID                            "FINDAI"
#define WIFI_AP_PASSWORD                        "88888888"

#define FIRMWARE_VERSION								"1.4.1"
#define FIRMWARE_VERSION_HEX							0x00010401
#define HARDWARE_VERSION								"0.3.0"
#define HARDWARE_VERSION_HEX							0x00000300

#endif
//=========================================================================================
#elif (RDX_AI_SEL_APP & APP_BEANSTALK_EN)

//-------------------- device model --------------------
#if (RDX_SEL_DEVICE == DEVICE_RDX_BJ_T2403)

#define PRODUCT_TYPE							"A0"

//AI translate.
#define RDX_AI_TRANSLATE_SUPPORT				(0)
//BLE advertise messages.
#define BT_NAME                             	"Beanstalk AI"
#define BLE_LOCAL_NAME                      	"Beanstalk AI"
//firmware & hardware version.
#define FACTORY_CODE                            "BEANTK"
#define FACTORY_CODE_SIZE                       strlen(FACTORY_CODE)
#define PRODUCT_CODE                            "602"
#define PRODUCT_CODE_SIZE                       strlen(PRODUCT_CODE)
//wifi information.
#define WIFI_AP_SSID                            "Beanstalk AI"
#define WIFI_AP_PASSWORD                        "88888888"

#define FIRMWARE_VERSION								"1.0.0"
#define FIRMWARE_VERSION_HEX							0x00010000
#define HARDWARE_VERSION								"0.1.0"
#define HARDWARE_VERSION_HEX							0x00000100

#undef  WIFI_AP_SSID_SUFFIX_MODE
#define WIFI_AP_SSID_SUFFIX_MODE                WIFI_AP_SSID_SUFFIX_MAC_TAIL3

#endif

//=========================================================================================
#elif (RDX_AI_SEL_APP & APP_ZENCHORD_EN)

//-------------------- device model --------------------
#if (RDX_SEL_DEVICE == DEVICE_ZENCORD_EP_T2616)

//RDX --> EP
#define BT_NAME                             	"Zenchord Pods"//"NEVW_AIEP"//"NvAI"
#define BLE_LOCAL_NAME                          "Zenchord Pods"//"GLOBOTOK"//"GLBTK"
//BLE advertise messages.
#define FACTORY_CODE                            "ZENCHD"//"NEVIEW"//"NvEasy"
#define FACTORY_CODE_SIZE                       strlen(FACTORY_CODE)
#define PRODUCT_CODE                            "601"
#define PRODUCT_CODE_SIZE                       strlen(PRODUCT_CODE)
//firmware & hardware version.
#define FIRMWARE_VERSION						"1.0.1"  //sdk 3.0.0以上从v1.0.0开始
#define FIRMWARE_VERSION_HEX					0x00010001
#define HARDWARE_VERSION						"0.0.1"
#define HARDWARE_VERSION_HEX					0x00000001

#elif (RDX_SEL_DEVICE == DEVICE_ZENCORD_CC_T2616)

#define PRODUCT_TYPE							"E1"

// #define __EC800X_4G_MDU__
// #define __CHX_5529_CTRL__    // 暂时关闭CHX5529，改用PT0807直接灯控

//BLE advertise messages.
#define BT_NAME                             	"Zenchord Case"
#define BLE_LOCAL_NAME                      	"Zenchord Case"
//firmware & hardware version.
#define FACTORY_CODE                            "ZENCHD"
#define FACTORY_CODE_SIZE                       strlen(FACTORY_CODE)
#define PRODUCT_CODE                            "603"
#define PRODUCT_CODE_SIZE                       strlen(PRODUCT_CODE)
//wifi information.
#define WIFI_AP_SSID                            "Zenchord Case"
#define WIFI_AP_PASSWORD                        "88888888" 

#define FIRMWARE_VERSION						"1.0.0"
#define FIRMWARE_VERSION_HEX					0x00010000
#define HARDWARE_VERSION						"0.0.1"
#define HARDWARE_VERSION_HEX					0x00000001

#else

#endif

//=========================================================================================
#elif (RDX_AI_SEL_APP & APP_DEEPMINER_EN)

//-------------------- device model --------------------
#if (RDX_SEL_DEVICE == DEVICE_RDX_BJ_T2403)

#define PRODUCT_TYPE							"P1"

//AI translate.
#define RDX_AI_TRANSLATE_SUPPORT				(0)
//BLE advertise messages.
#define BT_NAME                             	"Octic"
#define BLE_LOCAL_NAME                      	"Octic"
//firmware & hardware version.
#define FACTORY_CODE                            "DEEPMN"
#define FACTORY_CODE_SIZE                       strlen(FACTORY_CODE)
#define PRODUCT_CODE                            "604"
#define PRODUCT_CODE_SIZE                       strlen(PRODUCT_CODE)
//wifi information.
#define WIFI_AP_SSID                            "Octic"
#define WIFI_AP_PASSWORD                        "88888888"

#define FIRMWARE_VERSION								"1.0.0"
#define FIRMWARE_VERSION_HEX							0x00010000
#define HARDWARE_VERSION								"0.0.1"
#define HARDWARE_VERSION_HEX							0x00000001

#undef  WIFI_AP_SSID_SUFFIX_MODE
#define WIFI_AP_SSID_SUFFIX_MODE                WIFI_AP_SSID_SUFFIX_AUTH_TAIL4

#endif
//=========================================================================================

#else

#endif

/******************************************************************************
* Structure and Enum Section
******************************************************************************/ 


/******************************************************************************
* Global Variables Section
******************************************************************************/ 




#endif
