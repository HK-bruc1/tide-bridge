#ifndef __RDX_CONFIG_COMMON_H__
#define __RDX_CONFIG_COMMON_H__

#define FIRMWARE_NAME                                  "rdxos_ble_ai_recorder"

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

#define RDX_SUPPORT_MOTOR								(1)

#define RDX_MULTI_FUNC_INTERFACE						RDX_SUPPORT_EMMC //RDX_SUPPORT_BOTH_OLED_EMMC

#define RDX_SUPPORT_ALGORITHM							(1)

//RTC实现路径 软件模拟/硬件RTC

#ifndef RDX_RTC_PATH_SEL
#define RDX_RTC_PATH_SEL								RDX_RTC_PATH_HARDWARE
#endif

#if (RDX_RTC_PATH_SEL != RDX_RTC_PATH_SOFTWARE) && (RDX_RTC_PATH_SEL != RDX_RTC_PATH_HARDWARE)
#error "RDX_RTC_PATH_SEL must be RDX_RTC_PATH_SOFTWARE or RDX_RTC_PATH_HARDWARE"
#endif


#if (RDX_SEL_DEVICE == DEVICE_RDX_BJ_T2403) || (RDX_SEL_DEVICE == DEVICE_ZENCORD_CC_T2616)
#define RDX_BJ_VERSION									BJ_BOARD_VERSION_03
#endif


#define RDX_DEFAULT_SHUT_DOWN_TIME						(36000u) //minutes

//Transmit Verification way.

#define WIFI_COMMUNICATION_TYPE							TRANSFER_BY_TCP



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
// 默认: 动态密码 + MAC 末 3 字节; 个别产品在 config/product/<name>.h
// 中通过 #undef + 重定义切换到 auth 末 4 位.
//----------------------------------------------------------------------------
#define WIFI_AP_SSID_PSW_DYN_GENERATE					(1)

#define WIFI_AP_SSID_SUFFIX_MODE						WIFI_AP_SSID_SUFFIX_MAC_TAIL3

#if (RDX_SEL_DEVICE == DEVICE_RDX_BJ_T2403)
#define RDX_HAS_SK4558_CHARGER                          (1)
#else
#define RDX_HAS_SK4558_CHARGER                          (0)
#endif

#if (RDX_SEL_DEVICE == DEVICE_RDX_BJ_T2403) || \
    (RDX_SEL_DEVICE == DEVICE_DACOM_CC_T2401) || \
    (RDX_SEL_DEVICE == DEVICE_1MORE_CC_T2402) || \
    (RDX_SEL_DEVICE == DEVICE_ZENCORD_CC_T2616)
#define RDX_SUPPORT_KEY_DUT_ENTRY                       (1)
#define RDX_NEEDS_POWER_ACTIVITY_GUARD                  (1)
#else
#define RDX_SUPPORT_KEY_DUT_ENTRY                       (0)
#define RDX_NEEDS_POWER_ACTIVITY_GUARD                  (0)
#endif

#endif
