#ifndef __RDX_PRODUCT_NOTTA_H__
#define __RDX_PRODUCT_NOTTA_H__

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

#define RDX_RECORD_USE_LOCAL_PIPELINE                  (0)
#define RDX_RECORD_DISCONNECT_TO_OFFLINE                (0)
#define RDX_RECORD_DISCONNECT_RERUN                     (1)
#define RDX_RECORD_SINK_AUTO_INIT                       (0)
#define RDX_IDLE_WAKE_ON_LONG_PRESS                     (0)

#endif
