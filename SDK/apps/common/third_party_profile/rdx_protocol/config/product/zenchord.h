#ifndef __RDX_PRODUCT_ZENCHORD_H__
#define __RDX_PRODUCT_ZENCHORD_H__

#define RDX_AI_TRANSLATE_SUPPORT                         (0)

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

#define RDX_RECORD_USE_LOCAL_PIPELINE                  (1)
#define RDX_RECORD_DISCONNECT_TO_OFFLINE                (1)
#define RDX_RECORD_DISCONNECT_RERUN                     (1)
#define RDX_RECORD_SINK_AUTO_INIT                       (1)
#define RDX_IDLE_WAKE_ON_LONG_PRESS                     (0)

#endif
