/*=====================================================================================
 HEADER NAME: rdx_app.h
 MODULE NAME: rdx application headfile.
 
 PRE-INCLUDE FILES DESCRIPTION: 	
 
 GENERAL DESCRIPTION: 	
 	This File will gather functions that special handle msg(UserEvent,Timer) from 
 	Intergration.These functions don't be changed by project changed.
 =======================================================================================	
 Revision History: 
 ---------------------------------------
 Author: sheng.dong
 Date: 2024-09-24 20:34:31
 LastEditors: sheng.dong
 LastEditTime: 2024-10-16 14:16:59
 FilePath: \SDK\apps\common\third_party_profile\rdx_protocol\rdx_app.h
 
 Self-documenting Code
=====================================================================================*/

#ifndef __RDX_APP_H__
#define __RDX_APP_H__

/******************************************************************************
* Include files
******************************************************************************/ 
#include "os/os_type.h"
#include "key_driver.h"
#include "app_msg.h"
#include "rdx_record.h"
#include "rdx_app_config.h"
#include "rdx_vm.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
* Macro Define Section
******************************************************************************/ 
#define VDD_POWER_PORT_IO                           IO_PORTA_00
#define VDD_POWER_PORT                              PORTA
#define VDD_POWER_PIN                               PORT_PIN_0

#define LED_PT0807_DATA_PORT_IO                     IO_PORTC_01

#define RECORD_STATE_START                          (0u)
#define RECORD_STATE_PAUSE                          (1u)
#define RECORD_STATE_RESUME                         (2u)
#define RECORD_STATE_STOP                           (3u)

#define RECORD_FORMATE_OPUS_32K_MONO                (0u)
#define RECORD_FORMATE_OPUS_16K_MONO                (1u)
#define RECORD_FORMATE_OPUS_16K_STERO               (2u)
#define RECORD_FORMATE_OPUS_8K_MONO                 (3u)

// #define RECORD_SCENE_CALL                           (0u)
// #define RECORD_SCENE_CHAT                           (1u)
// #define RECORD_SCENE_INIT                           (0xFF)

#define CALL_STATE_OFF                              (0)
#define CALL_STATE_ON                               (1)
#define CALL_STATE_READY                            (2)
#define CALL_STATE_ACCEPT                           (3)
#define CALL_STATE_CONNECTING                       (4)


#define TWO_BYTE_TO_DATA(x)                         ((x[0] << 8) + x[1])
#define U16_TO_LITTLEENDIAN(x)                      (((x & 0xff) << 8) + (x & 0xff00))
#define FOUR_BYTE_TO_DATA(x)                        ((x[0] << 24) + (x[1] << 16) + (x[2] << 8) + x[3])
#define RDX_LEGAL_CHAR(c)                           ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))


#define DEVICE_WORK_MODE_TWS                        (0)
#define DEVICE_WORK_MODE_AI                         (1)

#define TRANSFER_BY_WIFI_OFF                        (0)
#define TRANSFER_BY_WIFI_ON                         (1)

/* This command is injected locally through the immutable protocol receive
 * FIFO. It is never part of the external RDX command surface. */
#define RDX_LIFECYCLE_CUSTOM_CMD                    "__rdxlc"

/******************************************************************************
* Structure and Enum Section
******************************************************************************/

#include "user_cfg.h"
#define EQ_CNT 10

/******************************************************************************
* Structure and Enum Section
******************************************************************************/ 
enum {
    RDX_CONN_STATE_DISCONNECT = 0,
    RDX_CONN_STATE_CONNECTING,
    RDX_CONN_STATE_CONNECTED,
};

enum {
    RDX_SEND_DATA_TYPE_DT_RAW = 0,
    RDX_SEND_DATA_TYPE_DT_BOOL,
    RDX_SEND_DATA_TYPE_DT_VALUE,
    RDX_SEND_DATA_TYPE_DT_STRING,
    RDX_SEND_DATA_TYPE_DT_ENUM,
    RDX_SEND_DATA_TYPE_DT_BITMAP,
};

typedef struct{
    u8 onoff;
    u8 conn_state;
}RdxWifiInfo;


void rdx_app_online_key_down(u8 key_value);

#if (TCFG_USER_TWS_ENABLE && TCFG_APP_BT_EN) 

struct RDX_SYNC_INFO {
    char eq_info[EQ_CNT + 1];
    u8 rdx_eq_flag;
    char bt_name[LOCAL_NAME_LEN];
    u8 rdx_bt_name_flag;
    u8 anc_mode;
    u8 volume;
    u8 volume_flag;
    u8 key_r1;
    u8 key_r2;
    u8 key_r3;
    u8 key_l1;
    u8 key_l2;
    u8 key_l3;
    u8 key_change_flag;
    u8 find_device;
    u8 device_conn_flag;
    u8 device_disconn_flag;
    u8 phone_conn_flag;
    u8 phone_disconn_flag;
    u8 key_reset;
};

enum {
    APP_TWS_RDX_SYNC_EQ  = 0,
    APP_TWS_RDX_SYNC_ANC = 1,
    APP_TWS_RDX_SYNC_VOLUME = 2,
    APP_TWS_RDX_SYNC_KEY_R1 = 3,
    APP_TWS_RDX_SYNC_KEY_R2 = 4,
    APP_TWS_RDX_SYNC_KEY_R3 = 5,
    APP_TWS_RDX_SYNC_KEY_L1 = 6,
    APP_TWS_RDX_SYNC_KEY_L2 = 7,
    APP_TWS_RDX_SYNC_KEY_L3 = 8,
    APP_TWS_RDX_SYNC_FIND_DEVICE = 9,
    APP_TWS_RDX_SYNC_DEVICE_CONN_FLAG = 10,
    APP_TWS_RDX_SYNC_DEVICE_DISCONN_FLAG = 11,
    APP_TWS_RDX_SYNC_PHONE_CONN_FLAG = 12,
    APP_TWS_RDX_SYNC_PHONE_DISCONN_FLAG = 13,
    APP_TWS_RDX_SYNC_BT_NAME = 14,
    APP_TWS_RDX_SYNC_KEY_RESET = 15,
};

enum {
    RDX_MUSIC_PP = 100,
    RDX_MUSIC_NEXT,
    RDX_MUSIC_PREV,
};
enum {
    voice_down,
    voice_up,
    next_music,
    prev_music,
    music_play,
    ambient_sound,
    voice_assistant,
};

typedef struct {
    u8 eq_onoff;
    u8 eq_mode;
    char eq_data[EQ_CNT];
} __eq_info;

typedef struct {
    int trn_set;
    int noise_set;
    u8 noise_mode;
    u8 noise_scenes;
    u8 transparency_scenes;
} __noise_info;

typedef struct {
    u8 left1;
    u8 left2;
    u8 left3;
    u8 right1;
    u8 right2;
    u8 right3;
} __key_info;

typedef struct {
    u8 case_battery;
    u8 left_battery;
    u8 right_battery;
} __battery_info;

typedef struct {
    u8 cmd;
    u8 formate;
    u8 type; 
} __record_info;

typedef struct {
    u8 led_state;
    u8 rdx_conn_state;
    __eq_info eq_info;
    __noise_info noise_info;
    __key_info key_info;
    __battery_info battery_info;
    __record_info record_info;
} __rdx_info;

typedef struct {
    u8 id;
    u8 type;
    u16 len;
    u8 data;
} __battery_indicate_data;

typedef struct {
    u8 id;
    u8 type;
    u16 len;
    u8 data;
} __rdx_conn_state_data;

typedef struct {
    u8 id;
    u8 type;
    u16 len;
    u8 data[32];
} __rdx_bt_name_data;

typedef struct {
    u8 id;
    u8 type;
    u16 len;
    u8 data;
} __key_indicate_data;

typedef struct {
    u8 id;
    u8 type;
    u16 len;
    u8 data;
} __valume_indicate_data;

typedef struct {
    u8 id;
    u8 type;
    u16 len;
    u8 data[20];
} __record_indicate_data;

// typedef struct {
//     u8 login_key[LOGIN_KEY_LEN];
//     u8 device_virtual_id[DEVICE_VIRTUAL_ID_LEN];
//     u8 bound_flag;
// } rdx_tws_sync_info_t;


#define LOGIN_KEY_LEN                           6
#define ECC_SECRET_KEY_LEN                      32
#define DEVICE_VIRTUAL_ID_LEN                   22
#define SECRET_KEY_LEN                          16
#define PAIR_RANDOM_LEN                         6
#define BEACON_KEY_LEN                          16
#define LOGIN_KEY_V2_LEN                        16
#define RDX_BLE_PRODUCT_ID_MAX_LEN              16

typedef struct {
    u32  crc;
    u32  settings_version;
    // tuya_ble_product_id_type_t pid_type;
    u8   pid_len;
    u8   common_pid[RDX_BLE_PRODUCT_ID_MAX_LEN];
    u8   login_key[LOGIN_KEY_LEN];
    u8   ecc_secret_key[ECC_SECRET_KEY_LEN];
    u8   device_virtual_id[DEVICE_VIRTUAL_ID_LEN];
    u8   user_rand[PAIR_RANDOM_LEN];
    u8   bound_flag;
    u8   factory_test_flag;
    u8   server_cert_pub_key[64];
    u8   beacon_key[BEACON_KEY_LEN];
    u8   login_key_v2[LOGIN_KEY_V2_LEN];
    u8   secret_key[SECRET_KEY_LEN];
    u8   protocol_v2_enable;
    u8   res[14];
} rdx_ble_sys_settings_t;

typedef struct {
    u8 ble_conn;
    rdx_ble_sys_settings_t rdx_sys_setting;
    u8 neib_mac[6]
} rdx_tws_sync_info_t;

typedef struct {
    RecordStatus record_status;
} rdx_tws_sync_record_t;


typedef struct {
    u8 id;
    u8 type;
    u16 len;
    u8 data;
} __play_status_indicate_data;

typedef struct {
    u8 id;
    u8 type;
    u16 len;
    u8 data;
} __eq_onoff_indicate_data;

typedef struct {
    u8 id;
    u8 type;
    u16 len;
    u8 version;
    u8 eq_num;
    u8 eq_mode;
    u8 eq_data[10];
} __eq_indicate_data;

typedef struct {
    u8 id;
    u8 type;
    u16 len;
    u8 data;
} __call_status_indicate_data;

typedef struct{
    u8 ai_mode;
    u8 comAddr[6];
}AImodeInfo;

#endif

/******************************************************************************
* Function Section
******************************************************************************/ 

#if (TCFG_USER_TWS_ENABLE && TCFG_APP_BT_EN) 
void rdx_sync_flag_update_before_send(struct RDX_SYNC_INFO *rdx_sync_info);
void rdx_app_find_device_deal(void);
void rdx_app_set_music_volume(int volume);
void rdx_app_eq_data_deal(char *eq_info_data);
u8 rdx_app_key_event_swith(u8 event);
void rdx_app_change_bt_name(char *name, u8 name_len);
void rdx_app_eq_data_setting(char *eq_setting, char eq_mode);
void rdx_app_find_device(u8 data);
void rdx_app_ctrl_music_state(u8 state);
void rdx_app_eq_info_deal(__rdx_info rdx_info, char *data);
void rdx_app_eq_info_reset(__rdx_info rdx_info, u8 *data);
void rdx_app_bt_name_deal(u8 *data, u16 data_len);
void rdx_app_tone_deal(char *tone_files);
void rdx_app_earphone_key_init();
void rdx_app_earphone_key_remap(int *value, int *msg);
void rdx_app_volume_indicate(s8 volume);
void rdx_app_tone_post(const char *tone_files);
void rdx_app_eq_info_post(char *eq_info);
void rdx_app_eq_mode_post(char eq_mode);
void rdx_app_vm_info_modify(u8 mode);
void rdx_app_vm_info_post(char *info, u8 mode, u8 len);
void rdx_app_sync_key_info(struct RDX_SYNC_INFO *rdx_sync_info);
void rdx_app_reset_key_info();
void rdx_app_update_vm_key_info(u8 key_value_record[][6]);
void rdx_app_sync_info_send(void *rdx_info, u8 data_type);


#define RDX_KEY_SYNC_VM                             0
#define RDX_BT_NAME_SYNC_VM                         1
#define RDX_EQ_INFO_SYNC_VM                         2


#if RDX_PRODUCT_IS_CHARGE_CASE
#define BLE_READCHAR_INFO_SIZE                      (95)
#else
#define BLE_READCHAR_INFO_SIZE                      (39)
#endif


extern void rdx_app_set_device_work_mode(u8 d);

extern AImodeInfo* rdx_app_get_AI_mode_info(void);
extern char* rdx_app_earphone_get_readchardata(void);
extern void rdx_app_tws_AI_mode_sync(u8 mode);
extern void rdx_app_AI_mode_enter(void);
extern void rdx_app_AI_mode_exit(void);

#else

#if RDX_PRODUCT_IS_CHARGE_CASE
#define BLE_READCHAR_INFO_SIZE                      (95)
#else
#define BLE_READCHAR_INFO_SIZE                      (24)
#endif

extern char* rdx_app_earphone_get_readchardata(void);

#endif

extern RdxWifiInfo* rdx_app_get_wifi_info(void);

extern void rdx_led_hardware_init(void);
extern int rdx_led_hardware_resume(void);
extern int rdx_led_hardware_deinit(void);
extern void rdx_app_all_init(void);
extern void rdx_app_all_exit(void);
u8 rdx_pc_storage_is_busy(void);
u8 rdx_app_business_started(void);
u8 rdx_app_rdx_rebind_is_idle(void);


#ifdef __cplusplus
}
#endif

#endif
