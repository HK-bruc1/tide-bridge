/*=====================================================================================
 HEADER NAME: rdx_ble_server.h
 MODULE NAME: rdx ble server application headfile.
 
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
 LastEditTime: 2024-10-16 14:29:38
 FilePath: \SDK\apps\common\third_party_profile\rdx_protocol\rdx_ble_server.h
 
 Self-documenting Code
=====================================================================================*/

#ifndef _RDX_BLE_SERVER_H_
#define _RDX_BLE_SERVER_H_

/******************************************************************************
* Include files
******************************************************************************/ 
#include "system/includes.h"
#include "ble_user.h"

/******************************************************************************
* Macro Define Section
******************************************************************************/ 
#define BLE_LOCAL_NAME_MAX_LEN                          (24)
#define BT_LOCAL_NAME_MAX_LEN                           (20)

// Ability bit definitions (big endian)
// First byte: [7:Local Storage][6:WiFi AP][5:Conference Recording][4:Call Recording][3:Conference Noise Reduction][2:Classic Bluetooth Support][1:RTC][0:Multimedia Recording]
#define ABILITY_LOCAL_STORAGE           (1 << 7)    // 0: Local storage not supported
#define ABILITY_WIFI_AP                 (1 << 6)    // 0: WiFi AP not supported
#define ABILITY_CONFERENCE_RECORDING    (1 << 5)    // 0: Conference recording not supported
#define ABILITY_CALL_RECORDING          (1 << 4)    // 0: Call recording not supported
#define ABILITY_CONFERENCE_NOISE_REDUCTION (1 << 3) // 0: Conference noise reduction not supported
#define ABILITY_CLASSIC_BT_SUPPORT      (1 << 2)    // 1: Classic Bluetooth supported
#define ABILITY_RTC                     (1 << 1)    // 1: RTC supported
#define ABILITY_MULTIMEDIA_RECORDING    (1 << 0)    // 0: Multimedia recording not supported

// Second byte (bit15..bit8 of u32 ability when shifted by 8):
//   [7:Left/Right 1V1][6:WiFi AP V2][5:CHILD_PARENT][4:NETWORK]
//   [3:FlashNote (V24)][2:Record Mark (V24)][1:Record Pause/Resume][0:Reserved]
#define ABILITY_LEFT_RIGHT_1V1          (1 << 15)    // 1: Left/Right 1V1 supported (AI mode)
#define ABILITY_WIFI_AP_V2              (1 << 14)    // 0: WiFi AP V2 not supported
#define ABILITY_CHILD_PARENT            (1 << 13)    // 0: Child/Parent not supported
#define ABILITY_NETWORK                 (1 << 12)    // V23: 联网（NETWORK）
#define ABILITY_FLASHNOTE               (1 << 11)    // V24: 闪记（*APP/DEV#flashnote#）
#define ABILITY_RECORD_MARK             (1 << 10)    // V24: 录音标记（*APP/DEV#recmark#）
#define ABILITY_RECORD_PAUSE_RESUME     (1 << 9)     // 录音暂停/恢复（*APP#record#2#/3#）
#define ABILITY_RESERVED_BITS_BYTE2     (0x01)       // Reserved bit [0] of second byte

// Third and fourth bytes: All reserved
#define ABILITY_RESERVED_BYTE3          (0x00)
#define ABILITY_RESERVED_BYTE4          (0x00)

#define RDX_DEVICE_ABILITY  (ABILITY_LOCAL_STORAGE       | \
                             ABILITY_WIFI_AP             | \
                             ABILITY_CONFERENCE_RECORDING | \
                             ABILITY_RTC                 | \
                             ABILITY_WIFI_AP_V2          | \
                             ABILITY_RECORD_MARK         | \
                             ABILITY_RECORD_PAUSE_RESUME)

#define IS_SUPPORT_ABILITY(ability_bit) (((RDX_DEVICE_ABILITY) & (ability_bit)) ? 1 : 0)

#define CHILD_BLE_STATE_DISCONNECTED     0
#define CHILD_BLE_STATE_CONNECTED        1

/******************************************************************************
* Structure and Enum Section
******************************************************************************/ 
/// BLE server context structure
typedef struct {
    // BLE connection state variables
    ble_state_e ble_work_state;              ///< Current BLE working state
    u16 ble_con_handle;                      ///< BLE connection handle
    u8 ble_conn;                             ///< BLE connection status flag
    
    // BLE server handles and characteristic values
    void *rdx_ble_server_hdl;                ///< BLE server handle
    u8 ble_characteristic_value[24];     ///< BLE characteristic value buffer
    u8 ble_characteristic_value_len;     ///< Length of BLE characteristic value
    
    // BLE communication parameters
    u16 ble_mtu_size;                        ///< BLE MTU size
    u8 ccc_configured;                       ///< CCC (Client Characteristic Configuration) configured flag
    u8 stream_tx_ready;                      ///< Stream TX ready flag (set after CCC + sync data complete)
    
    // Timer handles
    u16 force_disconnect_timer;          ///< Force disconnection timer
    u16 adv_interval_change_timer;           ///< Advertising interval change timer
    
    // Advertising parameters
    u16 adv_interval_min;                ///< Minimum advertising interval
    
    // Synchronization mechanisms
    OS_MUTEX ble_send_queue_mutex;           ///< Mutex for BLE send queue
    
    // BLE device information
    u8 ble_mac_addr[6];                  ///< BLE MAC address
    char ble_local_name[BLE_LOCAL_NAME_MAX_LEN]; ///< BLE local name
} rdx_ble_server_info_t;

/******************************************************************************
* Function Section
******************************************************************************/ 
int rdx_ble_server_adv_enable(u8 enable);
void rdx_ble_server_app_disconnect(void);
void rdx_ble_server_auto_shut_down_enable(u8 enable);
int rdx_ble_server_ota_send(u8 *data, u32 len);
void rdx_ble_server_stop_force_disconnect_timer(void);
void rdx_ble_server_fast_adv_restart(void);  // 按键唤醒时重新进入快速广播并点亮 LED

u8 rdx_ble_server_get_send_fail_cnt(void);
void rdx_ble_server_reset_send_fail_cnt(void);

rdx_ble_server_info_t * rdx_ble_server_get_info(void);

/* Phase 6 C1: BLE mode controller narrow public wrappers */
void rdx_ble_mode_request_hogp(u8 enable);
u8   rdx_ble_connection_owner_is_hogp(void);
u8   rdx_ble_mode_is_hogp_requested(void);
void rdx_ble_mode_request_toggle(void);

int rdx_ble_server_bt_name_set_handle(u8 has_value, const char* in_name, char* out_name, u16 out_cap);
int rdx_ble_server_ble_name_set_handle(u8 has_value, const char* in_name, char* out_name, u16 out_cap);

#endif
