#ifndef __RDX_CONFIG_VALIDATE_H__
#define __RDX_CONFIG_VALIDATE_H__

/* Keep this file ASCII-only for the legacy Windows/JL build. */
#define _RDX_APP_MASK (APP_NEVIEW_EN | APP_XLSW_EN | APP_GNT_EN | APP_XYZL_EN | \
                       APP_NINGQU_EN | APP_NOTTA_EN | APP_TINGNAO_EN | APP_JMEASY_EN | \
                       APP_SHENGLANG_EN | APP_AITIR_EN | APP_YYS_EN | APP_LYNSE_EN | \
                       APP_TURING_EN | APP_RAYCON_EN | APP_CDJY_EN | APP_BRANDWORKS_EN | \
                       APP_FINDAI_EN | APP_WAN_EN | APP_BEANSTALK_EN | APP_ZENCHORD_EN | \
                       APP_DEEPMINER_EN | APP_TTEASY_EN | APP_AISPEECH_EN | APP_SHUGUO_EN | \
                       APP_VASCO_EN | APP_ABC_EN | APP_MLAMPWXB_EN)

_Static_assert((RDX_AI_SEL_APP & ~_RDX_APP_MASK) == 0,
               "RDX_AI_SEL_APP contains an unknown APP bit");
_Static_assert((RDX_AI_SEL_APP != 0) &&
               ((RDX_AI_SEL_APP & (RDX_AI_SEL_APP - 1)) == 0),
               "RDX_AI_SEL_APP must select exactly one APP");

#if (RDX_SEL_DEVICE != DEVICE_RDX_EP_A9) && \
    (RDX_SEL_DEVICE != DEVICE_RDX_BJ_T2403) && \
    (RDX_SEL_DEVICE != DEVICE_DACOM_EP_T2401) && \
    (RDX_SEL_DEVICE != DEVICE_DACOM_CC_T2401) && \
    (RDX_SEL_DEVICE != DEVICE_1MORE_EP_T2402) && \
    (RDX_SEL_DEVICE != DEVICE_1MORE_CC_T2402) && \
    (RDX_SEL_DEVICE != DEVICE_ZENCORD_EP_T2616) && \
    (RDX_SEL_DEVICE != DEVICE_ZENCORD_CC_T2616)
#error "RDX_SEL_DEVICE must be one of DEVICE_*"
#endif

#if (RDX_AI_SEL_APP == APP_ZENCHORD_EN)
#if (RDX_SEL_DEVICE != DEVICE_ZENCORD_EP_T2616) && \
    (RDX_SEL_DEVICE != DEVICE_ZENCORD_CC_T2616)
#error "APP_ZENCHORD_EN must pair with DEVICE_ZENCORD_*_T2616"
#endif
#elif (RDX_SEL_DEVICE != DEVICE_RDX_BJ_T2403)
#error "Record-card APP must pair with DEVICE_RDX_BJ_T2403"
#endif

#ifndef BT_NAME
#error "Product config must define BT_NAME"
#endif
#ifndef BLE_LOCAL_NAME
#error "Product config must define BLE_LOCAL_NAME"
#endif
#ifndef FACTORY_CODE
#error "Product config must define FACTORY_CODE"
#endif
#ifndef PRODUCT_CODE
#error "Product config must define PRODUCT_CODE"
#endif
#ifndef FIRMWARE_VERSION
#error "Product config must define FIRMWARE_VERSION"
#endif
#ifndef FIRMWARE_VERSION_HEX
#error "Product config must define FIRMWARE_VERSION_HEX"
#endif
#ifndef HARDWARE_VERSION
#error "Product config must define HARDWARE_VERSION"
#endif
#ifndef HARDWARE_VERSION_HEX
#error "Product config must define HARDWARE_VERSION_HEX"
#endif

#if (RDX_SEL_DEVICE != DEVICE_ZENCORD_EP_T2616)
#ifndef PRODUCT_TYPE
#error "Product config must define PRODUCT_TYPE"
#endif
#ifndef WIFI_AP_SSID
#error "Product config must define WIFI_AP_SSID"
#endif
#ifndef WIFI_AP_PASSWORD
#error "Product config must define WIFI_AP_PASSWORD"
#endif
#endif

#ifdef PRODUCT_TYPE
#define RDX_HAS_PRODUCT_TYPE                           (1)
#else
#define RDX_HAS_PRODUCT_TYPE                           (0)
#endif

#if defined(WIFI_AP_SSID) && defined(WIFI_AP_PASSWORD)
#define RDX_HAS_WIFI_AP_CONFIG                         (1)
#elif defined(WIFI_AP_SSID) || defined(WIFI_AP_PASSWORD)
#error "WIFI_AP_SSID and WIFI_AP_PASSWORD must be configured together"
#else
#define RDX_HAS_WIFI_AP_CONFIG                         (0)
#endif

_Static_assert((RDX_AI_TRANSLATE_SUPPORT == 0) || (RDX_AI_TRANSLATE_SUPPORT == 1),
               "RDX_AI_TRANSLATE_SUPPORT must be 0 or 1");
_Static_assert((RDX_RECORD_USE_LOCAL_PIPELINE == 0) || (RDX_RECORD_USE_LOCAL_PIPELINE == 1),
               "RDX_RECORD_USE_LOCAL_PIPELINE must be 0 or 1");
_Static_assert((RDX_RECORD_DISCONNECT_TO_OFFLINE == 0) || (RDX_RECORD_DISCONNECT_TO_OFFLINE == 1),
               "RDX_RECORD_DISCONNECT_TO_OFFLINE must be 0 or 1");
_Static_assert((RDX_RECORD_DISCONNECT_RERUN == 0) || (RDX_RECORD_DISCONNECT_RERUN == 1),
               "RDX_RECORD_DISCONNECT_RERUN must be 0 or 1");
_Static_assert((RDX_RECORD_SINK_AUTO_INIT == 0) || (RDX_RECORD_SINK_AUTO_INIT == 1),
               "RDX_RECORD_SINK_AUTO_INIT must be 0 or 1");
_Static_assert((RDX_IDLE_WAKE_ON_LONG_PRESS == 0) || (RDX_IDLE_WAKE_ON_LONG_PRESS == 1),
               "RDX_IDLE_WAKE_ON_LONG_PRESS must be 0 or 1");
_Static_assert((RDX_HAS_SK4558_CHARGER == 0) || (RDX_HAS_SK4558_CHARGER == 1),
               "RDX_HAS_SK4558_CHARGER must be 0 or 1");
_Static_assert((RDX_SUPPORT_KEY_DUT_ENTRY == 0) || (RDX_SUPPORT_KEY_DUT_ENTRY == 1),
               "RDX_SUPPORT_KEY_DUT_ENTRY must be 0 or 1");
_Static_assert((RDX_NEEDS_POWER_ACTIVITY_GUARD == 0) || (RDX_NEEDS_POWER_ACTIVITY_GUARD == 1),
               "RDX_NEEDS_POWER_ACTIVITY_GUARD must be 0 or 1");

#endif
