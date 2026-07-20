#ifndef __RDX_PRODUCT_DEEPMINER_H__
#define __RDX_PRODUCT_DEEPMINER_H__

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

#define RDX_RECORD_USE_LOCAL_PIPELINE                  (1)
#define RDX_RECORD_DISCONNECT_TO_OFFLINE                (1)
#define RDX_RECORD_DISCONNECT_RERUN                     (1)
#define RDX_RECORD_SINK_AUTO_INIT                       (0)
#define RDX_IDLE_WAKE_ON_LONG_PRESS                     (0)

#endif
