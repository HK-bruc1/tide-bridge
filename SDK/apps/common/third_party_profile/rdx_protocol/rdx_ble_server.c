/*=====================================================================================
 HEADER NAME: rdx_ble_server.c
 MODULE NAME: rdx ble server ctrl module.
 
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
 LastEditTime: 2024-10-16 13:50:10
 FilePath: \SDK\apps\common\third_party_profile\rdx_protocol\rdx_ble_server.c
 
 Self-documenting Code
=====================================================================================*/

#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".rdx_ble_server.data.bss")
#pragma data_seg(".rdx_ble_server.data")
#pragma const_seg(".rdx_ble_server.text.const")
#pragma code_seg(".rdx_ble_server.text")
#endif

/******************************************************************************
* Include files
******************************************************************************/
#include "sdk_config.h"
#include "app_msg.h"
#include "earphone.h"
#include "bt_tws.h"
#include "app_main.h"
#include "btstack/avctp_user.h"
#include "btstack/le/sm.h"
#include "btstack/le/le_user.h"
#include "btstack/btstack_event.h"
#include "multi_protocol_main.h"
#include "circular_buf.h"
#include "user_cfg.h"
#include "system/includes.h"
#include "app_config.h"

#include "rdx_ble_server.h"
#include "rdx_hogp_keyboard.h"
#include "rdx_hogp_profile.h"
#include "rdx_protocol.h"
#include "poweroff.h"
#include "rdx_record.h"
#include "clock.h"
#include "rdx_app.h"
#include "rdx_util.h"
#include "rdx_commonDef.h"
#include "rdx_app_config.h"
#include "rdx_uxfile.h"
#include "rdx_led_ctrl.h"

/*******************************************************************************
* Macro Define Section
*******************************************************************************/
#if (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN)

#define LOG_TAG                                     "[rdx_ble]"
#define LOG_ERROR_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_INFO_ENABLE
/* #define LOG_DUMP_ENABLE */
#define LOG_CLI_ENABLE
#include "debug.h"

//-----------------------------------------------------------------------------------------

#define MANUFAC_DATA_LENGTH                         (50)

#define RDX_FORCE_DISCONNECT_TIMEOUT                (20 * 1000)

// characteristics <--> handles
#define ATT_CHARACTERISTIC_2A00_01_VALUE_HANDLE 0x0003
#define ATT_CHARACTERISTIC_06068D1C_6B97_11EF_B864_0241AC120002_01_VALUE_HANDLE 0x0006
#define ATT_CHARACTERISTIC_06068D2C_6B97_11EF_B864_0242AC120002_01_VALUE_HANDLE 0x0008
#define ATT_CHARACTERISTIC_06068D2C_6B97_11EF_B864_0242AC120002_01_CLIENT_CONFIGURATION_HANDLE 0x0009
#define ATT_CHARACTERISTIC_06068D3C_6B97_11EF_B864_0243AC120002_01_VALUE_HANDLE 0x000b
#define ATT_CHARACTERISTIC_2A19_01_VALUE_HANDLE 0x000e
#define ATT_CHARACTERISTIC_2A19_01_CLIENT_CONFIGURATION_HANDLE 0x000f
#define ATT_CHARACTERISTIC_00239A7F_C616_89BB_3374_F15AF588A7B3_01_VALUE_HANDLE 0x0012
#define ATT_CHARACTERISTIC_00239A8F_C616_89BB_3374_F25AF588A7B3_01_VALUE_HANDLE 0x0014
#define ATT_CHARACTERISTIC_00239A8F_C616_89BB_3374_F25AF588A7B3_01_CLIENT_CONFIGURATION_HANDLE 0x0015

// Device Information Service handles (appended after HID Service)
#define DIS_SERVICE_HANDLE                                              0x0023
#define DIS_PNP_ID_CHARACTERISTIC_HANDLE                                0x0024
#define DIS_PNP_ID_VALUE_HANDLE                                         0x0025
#define DIS_MANUFACTURER_NAME_CHARACTERISTIC_HANDLE                     0x0026
#define DIS_MANUFACTURER_NAME_VALUE_HANDLE                              0x0027


//0 ~ 5 reserved.
#define ADV_MODE_BIT_MASK_AI_MODE                       (7)
#define ADV_MODE_BIT_MASK_BOUND                         (6)

#define RDX_BLE_ADV_INTERVAL_LOW                        (800)//(2400u)
#define RDX_BLE_ADV_INTERVAL_CHANGE_TIMEOUT             (120 * 1000)

#define RDX_SELF_MARK                                   "NV"
#define RDX_BLE_ADV_DEV_COLOR_POSITION                  (18)

/*******************************************************************************
* Structure and Enum Section
*******************************************************************************/

/******************************************************************************
* Global variable Section
******************************************************************************/


/*******************************************************************************
* Local variables Section
*******************************************************************************/
/// Global BLE server context instance
static rdx_ble_server_info_t g_rdx_ble_server_info = {
    .ble_work_state = 0,
    .ble_con_handle = 0,
    .ble_conn = FALSE,
    .rdx_ble_server_hdl = NULL,
    .ble_characteristic_value_len = 0,
    .ble_mtu_size = 0,
    .ccc_configured = FALSE,
    .stream_tx_ready = FALSE,
    .force_disconnect_timer = 0,
    .adv_interval_change_timer = 0,
    .adv_interval_min = 160,
    .ble_local_name = {0},
    .ble_mac_addr = {0},
};

static u16 g_syn_data_timer = 0;

const char *const rdx_phy_result[] = {
    "None",
    "1M",
    "2M",
    "Coded",
};

/*************************************************
                  BLE 相关内容
*************************************************/
const uint8_t rdx_profile_data[] = {
    //////////////////////////////////////////////////////
    //
    // 0x0001 PRIMARY_SERVICE  0x1800
    //
    //////////////////////////////////////////////////////
    0x0a, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x28, 0x00, 0x18,

     /* CHARACTERISTIC,  2A00, READ | DYNAMIC, */
    // 0x0002 CHARACTERISTIC 2A00 READ | DYNAMIC 
    0x0d, 0x00, 0x02, 0x00, 0x02, 0x00, 0x03, 0x28, 0x02, 0x03, 0x00, 0x00, 0x2a,
    // 0x0003 VALUE 2A00 READ | DYNAMIC  
    0x08, 0x00, 0x02, 0x01, 0x03, 0x00, 0x00, 0x2a,

    //////////////////////////////////////////////////////
    //
    // 0x0004 PRIMARY_SERVICE  06068D0C-6B97-11EF-B864-0240AC120002
    //
    //////////////////////////////////////////////////////
    0x18, 0x00, 0x02, 0x00, 0x04, 0x00, 0x00, 0x28, 0x02, 0x00, 0x12, 0xac, 0x40, 0x02, 0x64, 0xb8, 0xef, 0x11, 0x97, 0x6b, 0x0c, 0x8d, 0x06, 0x06,

     /* CHARACTERISTIC,  06068D1C-6B97-11EF-B864-0241AC120002, WRITE_WITHOUT_RESPONSE | DYNAMIC, */
    // 0x0005 CHARACTERISTIC 06068D1C-6B97-11EF-B864-0241AC120002 WRITE_WITHOUT_RESPONSE | DYNAMIC 
    0x1b, 0x00, 0x02, 0x00, 0x05, 0x00, 0x03, 0x28, 0x04, 0x06, 0x00, 0x02, 0x00, 0x12, 0xac, 0x41, 0x02, 0x64, 0xb8, 0xef, 0x11, 0x97, 0x6b, 0x1c, 0x8d, 0x06, 0x06,
    // 0x0006 VALUE 06068D1C-6B97-11EF-B864-0241AC120002 WRITE_WITHOUT_RESPONSE | DYNAMIC  
    0x16, 0x00, 0x04, 0x03, 0x06, 0x00, 0x02, 0x00, 0x12, 0xac, 0x41, 0x02, 0x64, 0xb8, 0xef, 0x11, 0x97, 0x6b, 0x1c, 0x8d, 0x06, 0x06,

     /* CHARACTERISTIC,  06068D2C-6B97-11EF-B864-0242AC120002, NOTIFY, */
    // 0x0007 CHARACTERISTIC 06068D2C-6B97-11EF-B864-0242AC120002 NOTIFY 
    0x1b, 0x00, 0x02, 0x00, 0x07, 0x00, 0x03, 0x28, 0x10, 0x08, 0x00, 0x02, 0x00, 0x12, 0xac, 0x42, 0x02, 0x64, 0xb8, 0xef, 0x11, 0x97, 0x6b, 0x2c, 0x8d, 0x06, 0x06,
    // 0x0008 VALUE 06068D2C-6B97-11EF-B864-0242AC120002 NOTIFY  
    0x16, 0x00, 0x10, 0x02, 0x08, 0x00, 0x02, 0x00, 0x12, 0xac, 0x42, 0x02, 0x64, 0xb8, 0xef, 0x11, 0x97, 0x6b, 0x2c, 0x8d, 0x06, 0x06,
    // 0x0009 CLIENT_CHARACTERISTIC_CONFIGURATION 
    0x0a, 0x00, 0x0a, 0x01, 0x09, 0x00, 0x02, 0x29, 0x00, 0x00,

     /* CHARACTERISTIC,  06068D3C-6B97-11EF-B864-0243AC120002, READ | DYNAMIC, */
    // 0x000a CHARACTERISTIC 06068D3C-6B97-11EF-B864-0243AC120002 READ | DYNAMIC 
    0x1b, 0x00, 0x02, 0x00, 0x0a, 0x00, 0x03, 0x28, 0x02, 0x0b, 0x00, 0x02, 0x00, 0x12, 0xac, 0x43, 0x02, 0x64, 0xb8, 0xef, 0x11, 0x97, 0x6b, 0x3c, 0x8d, 0x06, 0x06,
    // 0x000b VALUE 06068D3C-6B97-11EF-B864-0243AC120002 READ | DYNAMIC  
    0x16, 0x00, 0x02, 0x03, 0x0b, 0x00, 0x02, 0x00, 0x12, 0xac, 0x43, 0x02, 0x64, 0xb8, 0xef, 0x11, 0x97, 0x6b, 0x3c, 0x8d, 0x06, 0x06,

    //////////////////////////////////////////////////////
    //
    // 0x000c PRIMARY_SERVICE  0x180F
    //
    //////////////////////////////////////////////////////
    0x0a, 0x00, 0x02, 0x00, 0x0c, 0x00, 0x00, 0x28, 0x0f, 0x18,

     /* CHARACTERISTIC,  2A19, READ | NOTIFY | DYNAMIC, */
    // 0x000d CHARACTERISTIC 2A19 READ | NOTIFY | DYNAMIC 
    0x0d, 0x00, 0x02, 0x00, 0x0d, 0x00, 0x03, 0x28, 0x12, 0x0e, 0x00, 0x19, 0x2a,
    // 0x000e VALUE 2A19 READ | NOTIFY | DYNAMIC  
    0x08, 0x00, 0x12, 0x01, 0x0e, 0x00, 0x19, 0x2a,
    // 0x000f CLIENT_CHARACTERISTIC_CONFIGURATION 
    0x0a, 0x00, 0x0a, 0x01, 0x0f, 0x00, 0x02, 0x29, 0x00, 0x00,

    //////////////////////////////////////////////////////
    //
    // 0x0010 PRIMARY_SERVICE  00239A6F-C616-89BB-3374-F05AF588A7B3
    //
    //////////////////////////////////////////////////////
    0x18, 0x00, 0x02, 0x00, 0x10, 0x00, 0x00, 0x28, 0xb3, 0xa7, 0x88, 0xf5, 0x5a, 0xf0, 0x74, 0x33, 0xbb, 0x89, 0x16, 0xc6, 0x6f, 0x9a, 0x23, 0x00,

     /* CHARACTERISTIC,  00239A7F-C616-89BB-3374-F15AF588A7B3, WRITE_WITHOUT_RESPONSE | DYNAMIC, */
    // 0x0011 CHARACTERISTIC 00239A7F-C616-89BB-3374-F15AF588A7B3 WRITE_WITHOUT_RESPONSE | DYNAMIC 
    0x1b, 0x00, 0x02, 0x00, 0x11, 0x00, 0x03, 0x28, 0x04, 0x12, 0x00, 0xb3, 0xa7, 0x88, 0xf5, 0x5a, 0xf1, 0x74, 0x33, 0xbb, 0x89, 0x16, 0xc6, 0x7f, 0x9a, 0x23, 0x00,
    // 0x0012 VALUE 00239A7F-C616-89BB-3374-F15AF588A7B3 WRITE_WITHOUT_RESPONSE | DYNAMIC  
    0x16, 0x00, 0x04, 0x03, 0x12, 0x00, 0xb3, 0xa7, 0x88, 0xf5, 0x5a, 0xf1, 0x74, 0x33, 0xbb, 0x89, 0x16, 0xc6, 0x7f, 0x9a, 0x23, 0x00,

     /* CHARACTERISTIC,  00239A8F-C616-89BB-3374-F25AF588A7B3, NOTIFY, */
    // 0x0013 CHARACTERISTIC 00239A8F-C616-89BB-3374-F25AF588A7B3 NOTIFY 
    0x1b, 0x00, 0x02, 0x00, 0x13, 0x00, 0x03, 0x28, 0x10, 0x14, 0x00, 0xb3, 0xa7, 0x88, 0xf5, 0x5a, 0xf2, 0x74, 0x33, 0xbb, 0x89, 0x16, 0xc6, 0x8f, 0x9a, 0x23, 0x00,
    // 0x0014 VALUE 00239A8F-C616-89BB-3374-F25AF588A7B3 NOTIFY  
    0x16, 0x00, 0x10, 0x02, 0x14, 0x00, 0xb3, 0xa7, 0x88, 0xf5, 0x5a, 0xf2, 0x74, 0x33, 0xbb, 0x89, 0x16, 0xc6, 0x8f, 0x9a, 0x23, 0x00,
    // 0x0015 CLIENT_CHARACTERISTIC_CONFIGURATION
    0x0a, 0x00, 0x0a, 0x01, 0x15, 0x00, 0x02, 0x29, 0x00, 0x00,

    //////////////////////////////////////////////////////
    //
    // 0x0016 PRIMARY_SERVICE  0x1812 (HID)
    //
    //////////////////////////////////////////////////////
    0x0a, 0x00, 0x02, 0x00, 0x16, 0x00, 0x00, 0x28, 0x12, 0x18,

     /* CHARACTERISTIC,  2A4E, READ | WRITE_WITHOUT_RESPONSE, value=0x01 */
    // 0x0017 CHARACTERISTIC 2A4E READ | WRITE_WITHOUT_RESPONSE
    0x0d, 0x00, 0x02, 0x00, 0x17, 0x00, 0x03, 0x28, 0x06, 0x18, 0x00, 0x4e, 0x2a,
    // 0x0018 VALUE 2A4E READ | WRITE_WITHOUT_RESPONSE
    0x09, 0x00, 0x06, 0x00, 0x18, 0x00, 0x4e, 0x2a, 0x01,

     /* CHARACTERISTIC,  2A4D, READ | WRITE | NOTIFY | DYNAMIC */
    // 0x0019 CHARACTERISTIC 2A4D READ | WRITE | NOTIFY | DYNAMIC
    0x0d, 0x00, 0x02, 0x00, 0x19, 0x00, 0x03, 0x28, 0x1a, 0x1a, 0x00, 0x4d, 0x2a,
    // 0x001a VALUE 2A4D READ | WRITE | NOTIFY | DYNAMIC
    0x08, 0x00, 0x1a, 0x01, 0x1a, 0x00, 0x4d, 0x2a,
    // 0x001b CLIENT_CHARACTERISTIC_CONFIGURATION
    0x0a, 0x00, 0x0a, 0x01, 0x1b, 0x00, 0x02, 0x29, 0x00, 0x00,
    // 0x001c REPORT_REFERENCE, report_id=1, report_type=1 (Input)
    0x0a, 0x00, 0x02, 0x00, 0x1c, 0x00, 0x08, 0x29, 0x01, 0x01,

     /* CHARACTERISTIC,  2A4B, READ | DYNAMIC */
    // 0x001d CHARACTERISTIC 2A4B READ | DYNAMIC
    0x0d, 0x00, 0x02, 0x00, 0x1d, 0x00, 0x03, 0x28, 0x02, 0x1e, 0x00, 0x4b, 0x2a,
    // 0x001e VALUE 2A4B READ | DYNAMIC
    0x08, 0x00, 0x02, 0x01, 0x1e, 0x00, 0x4b, 0x2a,

     /* CHARACTERISTIC,  2A4A, READ | DYNAMIC */
    // 0x001f CHARACTERISTIC 2A4A READ | DYNAMIC
    0x0d, 0x00, 0x02, 0x00, 0x1f, 0x00, 0x03, 0x28, 0x02, 0x20, 0x00, 0x4a, 0x2a,
    // 0x0020 VALUE 2A4A READ | DYNAMIC
    0x08, 0x00, 0x02, 0x01, 0x20, 0x00, 0x4a, 0x2a,

     /* CHARACTERISTIC,  2A4C, WRITE_WITHOUT_RESPONSE | DYNAMIC */
    // 0x0021 CHARACTERISTIC 2A4C WRITE_WITHOUT_RESPONSE | DYNAMIC
    0x0d, 0x00, 0x02, 0x00, 0x21, 0x00, 0x03, 0x28, 0x04, 0x22, 0x00, 0x4c, 0x2a,
    // 0x0022 VALUE 2A4C WRITE_WITHOUT_RESPONSE | DYNAMIC
    0x08, 0x00, 0x04, 0x01, 0x22, 0x00, 0x4c, 0x2a,

    //////////////////////////////////////////////////////
    //
    // 0x0023 PRIMARY_SERVICE  0x180a (Device Information)
    //
    //////////////////////////////////////////////////////
    0x0a, 0x00, 0x02, 0x00, 0x23, 0x00, 0x00, 0x28, 0x0a, 0x18,

     /* CHARACTERISTIC,  2A50, READ, */
    // 0x0024 CHARACTERISTIC 2A50 READ
    0x0d, 0x00, 0x02, 0x00, 0x24, 0x00, 0x03, 0x28, 0x02, 0x25, 0x00, 0x50, 0x2a,
    // 0x0025 VALUE 2A50 READ (static PnP ID: USB-IF, VID=0x1234, PID=0x0001, Ver=0x0001)
    0x0f, 0x00, 0x02, 0x00, 0x25, 0x00, 0x50, 0x2a, 0x02, 0x34, 0x12, 0x01, 0x00, 0x01, 0x00,

     /* CHARACTERISTIC,  2A29, READ, */
    // 0x0026 CHARACTERISTIC 2A29 READ
    0x0d, 0x00, 0x02, 0x00, 0x26, 0x00, 0x03, 0x28, 0x02, 0x27, 0x00, 0x29, 0x2a,
    // 0x0027 VALUE 2A29 READ (static "JieLi")
    0x0d, 0x00, 0x02, 0x00, 0x27, 0x00, 0x29, 0x2a, 0x4a, 0x69, 0x65, 0x4c, 0x69,

    // 0x0028 CHARACTERISTIC 0x2A4D (Output Report): Read | Write | Write Without Response
    0x0d, 0x00, 0x02, 0x00, 0x28, 0x00, 0x03, 0x28, 0x0e, 0x29, 0x00, 0x4d, 0x2a,
    // 0x0029 VALUE 0x2A4D (Output Report): Read | Write | Write Without Response, 1 byte LED state
    0x09, 0x00, 0x0e, 0x00, 0x29, 0x00, 0x4d, 0x2a, 0x00,
    // 0x002a REPORT_REFERENCE (ID=1, Type=2=Output)
    0x0a, 0x00, 0x02, 0x00, 0x2a, 0x00, 0x08, 0x29, 0x01, 0x02,

    // END
    0x00, 0x00,
};

/******************************************************************************
* Function Declaration Section
******************************************************************************/ 
extern void rdx_record_start(void* priv);
extern void sys_set_auto_off_time(u32 auto_off_time);
extern u32 sys_get_auto_off_time(void);
extern bool rdx_app_get_poweroff_flag(void);
extern void rdx_protocol_record_trigger_indicate(RecordStatus* d, bool factor);
extern void rdx_ota_stop(void);
extern void rdx_app_clk_unlock(const char *task_name);
extern RecordStatus* rdx_record_get_status(void);
extern u8 rdx_app_get_record_mode(void);
extern int bt_modify_name(u8 *new_name);
extern void rdx_protocol_record_state_indicate(void);
extern u8 get_self_battery_level(void);
extern u8 get_ota_status();
extern void rdx_uxfile_datFileInfo_sendBuf_free(void);
extern void rdx_uxfile_recordFileData_sendBuf_free(void);
extern void rdx_protocol_ble_name_set_ack_indicate(u8 result, char* ble_name);
extern u8 rdx_protocol_get_ble_sent(void);
extern void rdx_protocol_set_ble_sent(u8 d);
extern void rdx_app_emmc_poweroff_check(void);
extern void rdx_app_emmc_poweroff_check_timer_stop(void);
extern bool rdx_app_get_power_ready_flag(void);
extern void rdx_app_set_power_ready_flag(void);
extern bool rdx_app_get_dut_status(void);
extern void rdx_app_emmc_poweron(u8 check_en);
extern void rdx_app_emmc_poweroff(void);
extern void rdx_record_mode_active_check(bool show);
extern u8 rdx_battery_get_percent(void);
extern void rdx_protocol_file_sync_busy_timer_stop(void);
extern void rdx_protocol_uploadFileInfo_clean(void);
extern void rdx_protocol_clear_send_confirm_flag(void);
extern void rdx_record_stream_interrupt(void);
extern void rdx_record_stream_resume_delayed(void);

int rdx_ble_server_adv_enable(u8 enable);
void rdx_ble_server_auto_shut_down_enable(u8 enable);
void rdx_ble_server_adv_data_changed(void);
void rdx_ble_server_syn_data_after_ble_write_ready(void* priv);
void rdx_ble_server_stop_force_disconnect_timer(void);
void rdx_ble_server_bulk_data_send_para_reset(void);
void rdx_ble_server_adv_interval_change_timer_stop(void);

/******************************************************************************
* Function Section
******************************************************************************/ 

#define ATT_LOCAL_MTU_SIZE          (517) 
//ATT缓存的buffer大小,  note: need >= 20,可修改
#define ATT_SEND_CBUF_SIZE          (4096) 

//共配置的RAM
#define ATT_RAM_BUFSIZE             (ATT_CTRL_BLOCK_SIZE + ATT_LOCAL_MTU_SIZE + ATT_SEND_CBUF_SIZE)//note:
static u8 att_ram_buffer[ATT_RAM_BUFSIZE] __attribute__((aligned(4)));

// Connection parameter update request settings
// Whether to enable parameter update request, 0--disable, 1--enable
static const uint8_t connection_update_enable = 1; ///0--disable, 1--enable
// Current request parameter table index
static uint8_t connection_update_cnt = 0; //

// Parameter table //total 9.
static const struct conn_update_param_t connection_param_table[] = {
    {6,  10, 4, 600},
    {10, 14, 4, 600},
    {14, 20, 4, 600},
    {8,  20, 10, 800},
    {12, 28, 10, 800},
    {16, 24, 10, 800},
    {20, 40, 19, 2000},
    {40, 60, 19, 2000},
    {60, 80, 19, 2000}, //8
}; 

#define CONN_PARAM_TABLE_CNT      (sizeof(connection_param_table)/sizeof(struct conn_update_param_t))

/**************************************************************************
 * function: rdx_ble_server_send_request_connect_parameter
 * description: 
 * param (u8) table_index
 * return (*)
 **************************************************************************/
void rdx_ble_server_send_request_connect_parameter(u8 table_index)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    struct conn_update_param_t *param = (void *)&connection_param_table[table_index];
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    log_info("====== update_request:-%d-%d-%d-%d-\r", param->interval_min, param->interval_max, param->latency, param->timeout);
    if(g_rdx_ble_server_info.ble_con_handle) {
        ble_op_conn_param_request(g_rdx_ble_server_info.ble_con_handle, param);
    }
}

/**************************************************************************
 * function: rdx_ble_server_check_connetion_updata_deal
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
static void rdx_ble_server_check_connetion_updata_deal(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if (connection_update_enable) {
        if (connection_update_cnt < CONN_PARAM_TABLE_CNT) {
            rdx_ble_server_send_request_connect_parameter(connection_update_cnt);
        }
    }
}

/**************************************************************************
 * function: rdx_ble_server_reset_local_name
 * description: 
 * param (char) *name
 * param (u8) len
 * return (*)
 **************************************************************************/
int rdx_ble_server_reset_local_name(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    DevBaseInfo* p = rdx_app_get_dev_base_info();
    u16 len = strlen(BLE_LOCAL_NAME);
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    if (len > BLE_LOCAL_NAME_MAX_LEN) {
        len = BLE_LOCAL_NAME_MAX_LEN;
    }
    //clear local name in vm.
    memset(g_rdx_ble_server_info.ble_local_name, 0, BLE_LOCAL_NAME_MAX_LEN);

    u8 buf[5] = {0};
    y_printf("%s --> AuthKey:%s \r", __func__, p->auth);
    if (strlen((char *)p->auth) >= 24) {
        memcpy(buf, p->auth + 20, 4);
    }
    snprintf(g_rdx_ble_server_info.ble_local_name, BLE_LOCAL_NAME_MAX_LEN, "%s %s", BLE_LOCAL_NAME, buf);

    int ret = syscfg_write(VM_RDX_BLE_NAME, g_rdx_ble_server_info.ble_local_name, BLE_LOCAL_NAME_MAX_LEN);
    if (ret <= 0) {
        log_info("%s --> write local name failed \r", __func__);
    } else {
        log_info("%s --> write local name success: %s \r", __func__, g_rdx_ble_server_info.ble_local_name);
    }

    return ret;
}

/**************************************************************************
 * function: rdx_ble_server_get_local_name
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
char* rdx_ble_server_get_local_name(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    DevBaseInfo* p = rdx_app_get_dev_base_info();
    char tmp[BLE_LOCAL_NAME_MAX_LEN + 1];
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    memset(tmp, 0, BLE_LOCAL_NAME_MAX_LEN + 1);
    int ret = syscfg_read(VM_RDX_BLE_NAME, tmp, BLE_LOCAL_NAME_MAX_LEN);
    if (ret <= 0) {
        log_info("===> %s --> local name set default! \r", __func__);
        int local_name_len = strlen(BLE_LOCAL_NAME);
        if (local_name_len > BLE_LOCAL_NAME_MAX_LEN) {
            local_name_len = BLE_LOCAL_NAME_MAX_LEN;
        }
        memset(g_rdx_ble_server_info.ble_local_name, 0, BLE_LOCAL_NAME_MAX_LEN);

        u8 buf[5] = {0};
        if (strlen((char *)p->auth) >= 24) {
            memcpy(buf, p->auth + 20, 4);
        }
        snprintf(g_rdx_ble_server_info.ble_local_name, BLE_LOCAL_NAME_MAX_LEN, "%s %s", BLE_LOCAL_NAME, buf);

        ret = syscfg_write(VM_RDX_BLE_NAME, g_rdx_ble_server_info.ble_local_name, local_name_len);
        if (ret <= 0) {
            log_info("%s --> write local name failed \r", __func__);
        } else {
            log_info("%s --> write local name success: %s \r", __func__, g_rdx_ble_server_info.ble_local_name);
        }
    }else{
        log_info("===> %s --> read local name success, current name: %s \r", __func__, tmp);
        memset(g_rdx_ble_server_info.ble_local_name, 0, BLE_LOCAL_NAME_MAX_LEN);
        strncpy(g_rdx_ble_server_info.ble_local_name, tmp, strlen(tmp));
    }
    return g_rdx_ble_server_info.ble_local_name;
}

/**************************************************************************
 * function: rdx_ble_server_set_local_name
 * description: 
 * param (char) *name
 * param (u8) len
 * return (*)
 **************************************************************************/
int rdx_ble_server_set_local_name(char *name, u8 len)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    if(name == NULL) {
        log_info("%s --> name is NULL\r", __func__);
        return -1;
    }
    if (len > BLE_LOCAL_NAME_MAX_LEN) {
        len = BLE_LOCAL_NAME_MAX_LEN;
    }
    y_printf("===> %s --> set local name: %s, len = %d \r", __func__, name, len);
    memset(g_rdx_ble_server_info.ble_local_name, 0, BLE_LOCAL_NAME_MAX_LEN);
    sprintf(g_rdx_ble_server_info.ble_local_name, "%s", name);

    g_printf("===> %s --> set local name: %s \r", __func__, g_rdx_ble_server_info.ble_local_name);
    int ret = syscfg_write(VM_RDX_BLE_NAME, g_rdx_ble_server_info.ble_local_name, BLE_LOCAL_NAME_MAX_LEN);
    if (ret <= 0) {
        log_info("%s --> write local name failed \r", __func__);
    } else {
        log_info("%s --> write local name success: %s \r", __func__, g_rdx_ble_server_info.ble_local_name);
    }
    return ret;
}

/**************************************************************************
 * function: rdx_ble_server_local_name_handle
 * description: 
 * param (char*) name
 * param (u16) len
 * return (*)
 **************************************************************************/
int rdx_ble_server_local_name_handle(char* name, u16 len)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    int ret = rdx_ble_server_set_local_name(name, len);
    if(ret <= 0){
        log_info("%s --> ble name set error! \r", __func__);
        rdx_protocol_ble_name_set_ack_indicate(1, name);
        return E_PROTOCOL_ECODE_FAIL;
    }

    //result: 0 -> ok, 1 -> error
    log_info("%s --> ble name set ok: %s \r", __func__, name);
    rdx_protocol_ble_name_set_ack_indicate(0, name);
    return E_PROTOCOL_ECODE_SUCCESS;
}

/**************************************************************************
 * function: rdx_ble_server_bt_name_set_handle
 * description: BT 经典蓝牙名称 set/query 
 **************************************************************************/
int rdx_ble_server_bt_name_set_handle(u8 has_value, const char* in_name, char* out_name, u16 out_cap)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    int ret = 0;
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    if(!out_name || out_cap == 0) return -1;
    memset(out_name, 0, out_cap);

    if(has_value){
        if(!in_name || in_name[0] == '\0') return -1;
        size_t copy = strlen(in_name);
        if(copy >= out_cap) copy = out_cap - 1;
        memcpy(out_name, in_name, copy);
        out_name[copy] = '\0';

        ret = bt_modify_name((u8*)out_name);
        if(ret == 0){
            log_info("%s --> bt_modify_name fail \r", __func__);
            return -1;
        }
    }else{
        const char* cur = bt_get_local_name();
        if(cur){
            size_t copy = strlen(cur);
            if(copy >= out_cap) copy = out_cap - 1;
            memcpy(out_name, cur, copy);
            out_name[copy] = '\0';
        }
    }
    return 0;
}

/**************************************************************************
 * function: rdx_ble_server_ble_name_set_handle
 * description: BLE 名称 set/query
 **************************************************************************/
int rdx_ble_server_ble_name_set_handle(u8 has_value, const char* in_name, char* out_name, u16 out_cap)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    int ret = 0;
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    if(!out_name || out_cap == 0) return -1;
    memset(out_name, 0, out_cap);

    if(has_value){
        if(!in_name || in_name[0] == '\0') return -1;
        size_t copy = strlen(in_name);
        if(copy >= out_cap) copy = out_cap - 1;
        memcpy(out_name, in_name, copy);
        out_name[copy] = '\0';

        ret = rdx_ble_server_set_local_name((char*)out_name, (u8)strlen(out_name));
        if(ret <= 0){
            log_info("%s --> rdx_ble_server_set_local_name fail \r", __func__);
            return -1;
        }
    }else{
        const char* cur = rdx_ble_server_get_local_name();
        if(cur){
            size_t copy = strlen(cur);
            if(copy >= out_cap) copy = out_cap - 1;
            memcpy(out_name, cur, copy);
            out_name[copy] = '\0';
        }
    }
    return 0;
}

/**************************************************************************
 * function: rdx_ble_server_get_conn_handle
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
u16 rdx_ble_server_get_conn_handle(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    return g_rdx_ble_server_info.ble_con_handle;
}

/**************************************************************************
 * function: rdx_ble_server_set_conn_handle
 * description: 
 * param (u16) con
 * return (*)
 **************************************************************************/
void rdx_ble_server_set_conn_handle(u16 con)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    g_rdx_ble_server_info.ble_con_handle = con;
}

/**************************************************************************
 * function: rdx_ble_server_set_g_rdx_ble_server_info.ble_work_state
 * description: 
 * param (ble_state_e) state
 * return (*)
 **************************************************************************/
static void rdx_ble_server_set_ble_work_state(ble_state_e state)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    if (state != g_rdx_ble_server_info.ble_work_state) {
        log_info("ble_work_st:%x->%x\r", g_rdx_ble_server_info.ble_work_state, state);
        g_rdx_ble_server_info.ble_work_state = state;
    }
}

/**************************************************************************
 * function: rdx_ble_server_get_g_rdx_ble_server_info.ble_work_state
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
ble_state_e rdx_ble_server_get_ble_work_state(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    return g_rdx_ble_server_info.ble_work_state;
}

/**************************************************************************
 * function: rdx_ble_server_disconnect
 * description: 
 * param (void) *priv
 * return (*)
 **************************************************************************/
static int rdx_ble_server_disconnect(void *priv)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    y_printf("====== %s --> con_handle = %d \n", __func__, g_rdx_ble_server_info.ble_con_handle);
    if (g_rdx_ble_server_info.ble_con_handle) {
        //stop recording if is running.
        RecordStatus* rp = rdx_record_get_status();
        if(rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME){
            //disconnect actively, stop recording.
            log_info(">>>rdx ble disconnect, stop recording\r");
            rp->run = RECORD_STATE_STOP;
            //send job.
            rdx_record_process();
        }

        if (BLE_ST_SEND_DISCONN != g_rdx_ble_server_info.ble_work_state) {
            log_info(">>>rdx ble send disconnect\r");
            g_rdx_ble_server_info.ble_work_state = BLE_ST_SEND_DISCONN;
            ble_op_disconnect(g_rdx_ble_server_info.ble_con_handle);
        } else {
            log_info(">>>rdx ble wait disconnect...\n");
        }
        return 0;
    } else {
        return -1;
    }
}

/**************************************************************************
 * function: rdx_ble_server_connection_update_complete_success
 * description: 
 * param (u8) *packet
 * return (*)
 **************************************************************************/
static void rdx_ble_server_connection_update_complete_success(u8 *packet)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    int con_handle, conn_interval, conn_latency, conn_timeout;
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    con_handle = hci_subevent_le_connection_update_complete_get_connection_handle(packet);
    conn_interval = hci_subevent_le_connection_update_complete_get_conn_interval(packet);
    conn_latency = hci_subevent_le_connection_update_complete_get_conn_latency(packet);
    conn_timeout = hci_subevent_le_connection_update_complete_get_supervision_timeout(packet);

    log_info("get conn_interval = %d\n", conn_interval);
    log_info("get conn_latency = %d\n", conn_latency);
    log_info("get conn_timeout = %d\n", conn_timeout);
}

/**************************************************************************
 * function: rdx_ble_server_force_disconnect_timer_cb
 * description: 
 * param (void) *priv
 * return (*)
 **************************************************************************/
void rdx_ble_server_force_disconnect_timer_cb(void *priv)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    y_printf("---- %s ----> return \n", __func__);
    return;
    
    y_printf("====== %s --> \n", __func__);
    //delte force disconnect timer.
    rdx_ble_server_stop_force_disconnect_timer();
    
    //DO disnconnect ble.
    rdx_ble_server_disconnect(NULL);
}

/**************************************************************************
 * function: rdx_ble_server_start_force_disconnect_timer
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_ble_server_start_force_disconnect_timer(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    y_printf("====== %s \n", __func__);
    if(g_rdx_ble_server_info.force_disconnect_timer == 0){
        g_rdx_ble_server_info.force_disconnect_timer = sys_timeout_add(NULL, rdx_ble_server_force_disconnect_timer_cb, RDX_FORCE_DISCONNECT_TIMEOUT);
    }
}

/**************************************************************************
 * function: rdx_ble_server_stop_force_disconnect_timer
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_ble_server_stop_force_disconnect_timer(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    y_printf("====== %s \n", __func__);
    //delte force disconnect timer.
    if(g_rdx_ble_server_info.force_disconnect_timer){
        sys_timeout_del(g_rdx_ble_server_info.force_disconnect_timer);
        g_rdx_ble_server_info.force_disconnect_timer = 0;
    }
}

/**************************************************************************
 * function: rdx_ble_server_disconnected_delay_handle
 * description: 
 * param (void*) priv
 * return (*)
 **************************************************************************/
void rdx_ble_server_disconnected_delay_handle(void* priv)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    RdxWifiInfo* k = rdx_app_get_wifi_info();
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if (k->onoff == TRANSFER_BY_WIFI_ON) {
        r_printf("[BLE] disconnected_delay_handle: WiFi transfer active, skip file cleanup\n");
        return;
    }

    rdx_protocol_uploadFileInfo_clean();
	rdx_uxfile_recordFileData_sendBuf_free();
	rdx_uxfile_datFileInfo_sendBuf_free();
    rdx_protocol_file_sync_busy_timer_stop();

    rdx_protocol_send_buffer_reinit();

    rdx_app_emmc_poweroff_check();
}

/**************************************************************************
 * FUNCTION
 *  rdx_ble_server_disconnected_handle
 * DESCRIPTION
 *  
 * PARAMETERS
 *  null
 * RETURNS
 *  null
**************************************************************************/
void rdx_ble_server_disconnected_handle(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    RdxWifiInfo* k = rdx_app_get_wifi_info();
    RecordStatus* rp = rdx_record_get_status();
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    //stop force disconnect timer.
    rdx_ble_server_stop_force_disconnect_timer();  

    if(g_syn_data_timer) {
        sys_timeout_del(g_syn_data_timer);
        g_syn_data_timer = 0;
    }

    //set connect flag.
    g_rdx_ble_server_info.ble_conn = FALSE;
    
    g_rdx_ble_server_info.ble_mtu_size = 0;
    g_rdx_ble_server_info.ccc_configured = FALSE;
    g_rdx_ble_server_info.stream_tx_ready = FALSE;

    //LED控制：BLE断开后闪烁（1s一次），但WiFi传输中不改变灯效
    if(k->onoff != TRANSFER_BY_WIFI_ON){
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_BLE_DISCONNECTED);
    }

    rdx_record_stream_interrupt();
    rdx_record_on_ble_conn_changed(false);

    //record stop.  //dons++ 20250326 离线录音时BLE断开后不停止录音
#if (RDX_AI_SEL_APP & APP_NINGQU_EN) || (RDX_AI_SEL_APP & APP_JMEASY_EN) || (RDX_AI_SEL_APP & APP_RAYCON_EN) || (RDX_AI_SEL_APP & APP_CDJY_EN) || (RDX_AI_SEL_APP & APP_BRANDWORKS_EN) || (RDX_AI_SEL_APP & APP_LYNSE_EN) || (RDX_AI_SEL_APP & APP_YYS_EN) || (RDX_AI_SEL_APP & APP_FINDAI_EN) || (RDX_AI_SEL_APP & APP_NEVIEW_EN) || (RDX_AI_SEL_APP & APP_SHENGLANG_EN) || (RDX_AI_SEL_APP & APP_BEANSTALK_EN) || (RDX_AI_SEL_APP & APP_ZENCHORD_EN) || (RDX_AI_SEL_APP & APP_DEEPMINER_EN)
    // r_printf("====== %s --> orig_mode: %d, mode: %d \n", __func__, rp->orig_mode, rp->mode);
    if(rp->orig_mode != RECORD_MODE_OFFLINE){
        rp->mode = RECORD_MODE_OFFLINE;
        rp->orig_mode = RECORD_MODE_OFFLINE;
    }
#else
    r_printf("====== %s --> orig_mode: %d, mode: %d \n", __func__, rp->orig_mode, rp->mode);
    if(rp->orig_mode != RECORD_MODE_OFFLINE){
        //if not offline mode, stop recording.
        if(rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME){
            rp->run = RECORD_STATE_STOP;
        #if !(RDX_AI_SEL_APP & APP_TURING_EN)
            rp->rerun = true;
        #endif

            //time to restart.
            // sys_timeout_add(NULL, rdx_record_start, 2000);
            rdx_record_process();
        }
    }
#endif
#if (TCFG_USER_TWS_ENABLE && TCFG_APP_BT_EN) 
    //tws sibling ble connect status. 
    rdx_app_tws_bind_info_sync();
#endif

    //adv restart.
    bool rdx_uxfile_sd_format_status_check(void);
    if(k->onoff == TRANSFER_BY_WIFI_ON){
        //wifi on, ble without adv.
        rdx_ble_server_adv_enable(0);
    }else{
        //power off, ble adv shut off.
        bool flag = rdx_app_get_dut_status();
        if(rdx_app_get_poweroff_flag() || flag == true || rdx_uxfile_sd_format_status_check()){
            //power off, shut down adv.
            rdx_ble_server_adv_enable(0);
        }else{
            //wifi off, ble with adv.
            rdx_ble_server_adv_data_changed();
            rdx_ble_server_auto_shut_down_enable(1);
        }
        if(!rdx_vm_is_unbouding()){
        #if (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_OLED) || (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_BOTH_OLED_EMMC)
            // OLED 功能已删除 // os_taskq_post_msg("oled_show_task", 1, OLED_SHOW_BLE_DISCONNECTED); 
        #endif
            r_printf("=== %s ---> do not show disconnect icon, unbounding now! \r", __FUNCTION__);
        }
    }

    //ota.
    if (get_ota_status()){
        rdx_ota_stop();
    }
    
    //file free if needed.
    sys_timeout_add(NULL, rdx_ble_server_disconnected_delay_handle, 500);
}

/**************************************************************************
 * FUNCTION
 *  rdx_ble_server_connected_handle
 * DESCRIPTION
 *  
 * PARAMETERS
 *  null
 * RETURNS
 *  null
**************************************************************************/
void rdx_ble_server_connected_handle(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/ 
    rdx_app_emmc_poweroff_check_timer_stop();
    rdx_app_emmc_poweron(1);

    if(0 == rdx_app_get_power_ready_flag()){
        rdx_app_set_power_ready_flag();
    }

    //stop adv change timer.
    rdx_ble_server_adv_interval_change_timer_stop();

    //init connect state.
    rdx_protocol_send_buffer_reinit();

    //set connect flag.
    g_rdx_ble_server_info.ble_conn = TRUE;

    //disbale shutdown timer.
    rdx_ble_server_auto_shut_down_enable(0);

    rdx_record_on_ble_conn_changed(true);

#if (TCFG_USER_TWS_ENABLE && TCFG_APP_BT_EN) 
    rdx_app_tws_bind_info_sync();
#endif

#if (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_OLED) || (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_BOTH_OLED_EMMC)
    // OLED 功能已删除 // os_taskq_post_msg("oled_show_task", 1, OLED_SHOW_BLE_CONNECTED); 
#endif

    //start force disconnect timer.
    rdx_ble_server_start_force_disconnect_timer();

    //LED控制：BLE连接后常亮1s后熄灭
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_BLE_CONNECTED);

#if (RDX_BJ_VERSION == BJ_BOARD_VERSION_02) || (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_EMMC) || (RDX_BJ_VERSION == BJ_BOARD_VERSION_03)
    RecordStatus* rp = rdx_record_get_status();
    if(rp->mode != RECORD_MODE_ONLINE){
        rp->mode = RECORD_MODE_ONLINE;
        if(rp->run == RECORD_STATE_STOP){
            rp->orig_mode = RECORD_MODE_ONLINE;
        }else{
            y_printf("offline recording now, do not change original record mode \r");
        }
    }
    rdx_record_stream_resume_delayed();
#endif
}

/**************************************************************************
 * function: 
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
u16 rdx_ble_server_get_mtu(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    return g_rdx_ble_server_info.ble_mtu_size;
}

/**************************************************************************
 * function: rdx_ble_server_cbk_packet_handler
 * description: 
 * param (void) *hdl
 * param (uint8_t) packet_type
 * param (uint16_t) channel
 * param (uint8_t) *packet
 * param (uint16_t) size
 * return (*)
 **************************************************************************/

static void set_connection_data_phy(u16 con_handle, u8 tx_phy, u8 rx_phy)
{
    if (0 == con_handle) {
        return;
    }

    u8 all_phys = 0;
    u16 phy_options = CONN_SET_PHY_OPTIONS_S8;

    ble_op_set_ext_phy(con_handle, all_phys, tx_phy, rx_phy, phy_options);
}

static void rdx_ble_server_sm_event_callback(void *hdl, uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    (void)hdl;
    (void)channel;

    rdx_hogp_on_sm_event(packet_type, packet, size);
}

static void rdx_ble_server_cbk_packet_handler(void *hdl, uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    u16 con_handle = 0;
    u32 tmp = 0;
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    // log_info("cbk packet_type:0x%x, packet[0]:0x%x, packet[2]:0x%x", packet_type, packet[0], packet[2]);
    switch (packet_type) {
    case HCI_EVENT_PACKET:
        switch (hci_event_packet_get_type(packet)) {
            case ATT_EVENT_CAN_SEND_NOW:
                {
                    BleBulkSendData* p_bulk_send_data = rdx_protocol_get_bulk_send_data();
                    rdx_protocol_set_ble_sent(0);
                    
                    rdx_protocol_clear_send_confirm_flag();
                    
                    if(p_bulk_send_data->bulk_flag == true){
                        p_bulk_send_data->bulk_flag = false;
                    }
                    
                    BLE_SendData* p_ble_send_data = rdx_protocol_get_ble_send_data();
                    os_sem_post(&p_ble_send_data->send_sem);
                }
                break;

            case HCI_EVENT_LE_META:
                switch (hci_event_le_meta_get_subevent_code(packet)) {
                    case HCI_SUBEVENT_LE_ENHANCED_CONNECTION_COMPLETE: 
                        {
                            r_printf("---------> HCI_SUBEVENT_LE_ENHANCED_CONNECTION_COMPLETE \n");
                            con_handle = little_endian_read_16(packet, 4);
                            log_info("HCI_SUBEVENT_LE_CONNECTION_COMPLETE: %0x", con_handle);

                            // HOGP mode connection tracking (forward to submodule)
                            if (hogp_mode_get()) {
                                rdx_hogp_on_connected(con_handle);
                            }
                            // set_connection_data_phy(con_handle, CONN_SET_2M_PHY, CONN_SET_2M_PHY);
                        }
                        break;

                    case HCI_SUBEVENT_LE_CONNECTION_COMPLETE:
                        con_handle = little_endian_read_16(packet, 4);
                        log_info("HCI_SUBEVENT_LE_CONNECTION_COMPLETE: %0x", con_handle);

                        //set connect handle.
                        rdx_ble_server_set_conn_handle(con_handle);

                        // HOGP mode connection tracking
                        if (hogp_mode_get()) {
                            rdx_hogp_on_connected(con_handle);
                            break;  // 阻止后续 RDX 连接初始化
                        }

                        //ble conn state.
                        rdx_ble_server_set_ble_work_state(BLE_ST_CONNECT);

                        rdx_ble_server_reset_send_fail_cnt();

                        rdx_ble_server_connection_update_complete_success(packet + 8);
                        put_buf(&packet[8], 6);
                        att_server_set_exchange_mtu(con_handle);

                        // set_connection_data_phy(con_handle, CONN_SET_2M_PHY, CONN_SET_2M_PHY);

                        // rdx_ble_server_send_request_connect_parameter(1);

                        //deal connect handle.
                        rdx_ble_server_connected_handle();
                        // int msg[2];
                        // msg[0] = (int)rdx_ble_server_connected_handle;
                        // msg[1] = 0;
                        // int ret = os_taskq_post_type("app_core", Q_CALLBACK, 2, msg);
                        // if(ret) {
                        //     log_info("%s record taskq post err \n", __func__);
                        // }
                        
                        break;

                    case HCI_SUBEVENT_LE_CONNECTION_UPDATE_COMPLETE:
                        if (con_handle != little_endian_read_16(packet, 4)) {
                            break;
                        }
                        rdx_ble_server_connection_update_complete_success(packet);
                        break;

                    case HCI_SUBEVENT_LE_DATA_LENGTH_CHANGE:
                        log_info("APP HCI_SUBEVENT_LE_DATA_LENGTH_CHANGE\n");
                        /* set_connection_data_phy(CONN_SET_CODED_PHY, CONN_SET_CODED_PHY); */
                        break;

                    case HCI_SUBEVENT_LE_PHY_UPDATE_COMPLETE:
                        log_info("APP HCI_SUBEVENT_LE_PHY_UPDATE %s\n", hci_event_le_meta_get_phy_update_complete_status(packet) ? "Fail" : "Succ");
                        log_info("Tx PHY: %s\n", rdx_phy_result[hci_event_le_meta_get_phy_update_complete_tx_phy(packet)]);
                        log_info("Rx PHY: %s\n", rdx_phy_result[hci_event_le_meta_get_phy_update_complete_rx_phy(packet)]);
                        break;

                    default:
                        break;
                }
                break;

            case HCI_EVENT_DISCONNECTION_COMPLETE:
                {
                    log_info("HCI_EVENT_DISCONNECTION_COMPLETE: %0x", packet[5]);
                    con_handle = 0;
                    rdx_ble_server_set_conn_handle(con_handle);
                    //set connect state.
                    rdx_ble_server_set_ble_work_state(BLE_ST_DISCONN);
                    // ble_op_att_send_init(con_handle, 0, 0, 0);

                    // HOGP mode disconnect tracking
                    if (hogp_mode_get()) {
                        rdx_hogp_on_disconnected(con_handle);
                        break;
                    }

                    rdx_ble_server_reset_send_fail_cnt();

                    rdx_record_stream_interrupt();

                    //deal disconnect handle.
                    rdx_ble_server_disconnected_handle();
                }
                break;

            case HCI_EVENT_ENCRYPTION_CHANGE:
                {
                    u16 enc_handle = hci_event_encryption_change_get_connection_handle(packet);
                    u8 enc_enabled = hci_event_encryption_change_get_encryption_enabled(packet);
                    u8 enc_status = hci_event_encryption_change_get_status(packet);
                    rdx_hogp_on_encryption_change(enc_handle, enc_enabled, enc_status);
                }
                break;

            case ATT_EVENT_MTU_EXCHANGE_COMPLETE:
                u16 mtu = att_event_mtu_exchange_complete_get_MTU(packet) - 3;
                log_info("===== ATT MTU = %u\r", mtu);
                ble_op_att_set_send_mtu(mtu);
                g_rdx_ble_server_info.ble_mtu_size = mtu;
                /* set_connection_data_length(251, 2120); */
                // rdx_ble_server_check_connetion_updata_deal();
                break;

            case L2CAP_EVENT_CONNECTION_PARAMETER_UPDATE_RESPONSE:
                {
                    tmp = little_endian_read_16(packet, 4);
                    log_info("-update_rsp: %02x\r", tmp);
                    if (tmp) {
                        connection_update_cnt++;
                        log_info("remoter reject!!!\n");
                        rdx_ble_server_check_connetion_updata_deal();
                    } else {
                        connection_update_cnt = CONN_PARAM_TABLE_CNT;
                    }
                }
                break;

            default:
                break;
        }
        break;
    }
    return;
}

/**************************************************************************
 * function: rdx_ble_server_get_ble_characteristic_value
 * description: 
 * param (u8) *len
 * return (*)
 **************************************************************************/
static u8 *rdx_ble_server_get_ble_characteristic_value(u8 *len)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    *len = g_rdx_ble_server_info.ble_characteristic_value_len;
    return g_rdx_ble_server_info.ble_characteristic_value;
}

/**************************************************************************
 * function: rdx_ble_server_gatts_value_set
 * description: 
 * param (u8) *p_data
 * param (u8) length
 * return (*)
 **************************************************************************/
static int rdx_ble_server_gatts_value_set(u8 *p_data, u8 length)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    log_info("rdx_ble_gatts_value_set, len:%d", length);
    put_buf(p_data, length);
    g_rdx_ble_server_info.ble_characteristic_value_len = length;
    memset(g_rdx_ble_server_info.ble_characteristic_value, 0, sizeof(g_rdx_ble_server_info.ble_characteristic_value));
    memcpy(g_rdx_ble_server_info.ble_characteristic_value, p_data, g_rdx_ble_server_info.ble_characteristic_value_len);
    return 0;
}

/**************************************************************************
 * function: rdx_ble_server_att_read_callback
 * description: 
 * param (void) *hdl
 * param (hci_con_handle_t) connection_handle
 * param (uint16_t) att_handle
 * param (uint16_t) offset
 * param (uint8_t) *buffer
 * param (uint16_t) buffer_size
 * return (*)
 **************************************************************************/
static uint16_t rdx_ble_server_att_read_callback(void *hdl, hci_con_handle_t connection_handle, uint16_t att_handle, uint16_t offset, uint8_t *buffer, uint16_t buffer_size)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    uint16_t att_value_len = 0;
    uint16_t handle = att_handle;
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    r_printf("<-------------read_callback, handle= 0x%04x,buffer= %08x \r", handle, (u32)buffer);

    switch (handle) {
        case ATT_CHARACTERISTIC_2A00_01_VALUE_HANDLE:
            const char *gap_name = rdx_ble_server_get_local_name();//bt_get_local_name();
            att_value_len = strlen(gap_name);
            if ((offset >= att_value_len) || (offset + buffer_size) > att_value_len) {
                break;
            }
            if (buffer) {
                memcpy(buffer, &gap_name[offset], buffer_size);
                att_value_len = buffer_size;
                log_info("\n------read gap_name: %s \r", gap_name);
            }

            // att_value_len = 24;
            // if (buffer) {
            //     uint8_t *p_data = rdx_get_ble_characteristic_value(&att_value_len);
            //     memcpy(buffer, (void *)p_data, att_value_len);
            // }

            break;

        case ATT_CHARACTERISTIC_06068D3C_6B97_11EF_B864_0243AC120002_01_VALUE_HANDLE:
            {
            #if (RDX_AI_TRANSLATE_SUPPORT == 1)
                //readchar data read.
                char* p = rdx_app_earphone_get_readchardata();
                y_printf("%s --> readchardata: %s \r", __func__, p);
                att_value_len = BLE_READCHAR_INFO_SIZE;
                if (buffer) {
                    strncpy(buffer, p, BLE_READCHAR_INFO_SIZE);
                } 
            #else
                // if (buffer) {
                //     buffer[0] = att_get_ccc_config(handle);
                //     buffer[1] = 0;
                // }
                // att_value_len = 2;

                
                //readchar data read.
                char* p = rdx_app_earphone_get_readchardata();
                att_value_len = BLE_READCHAR_INFO_SIZE;
                if (buffer) {
                    strncpy((char*)buffer, p, BLE_READCHAR_INFO_SIZE);
                    y_printf("%s --> readchardata: %s \r", __func__, buffer);
                } 
            #endif
            }
            break;

        case ATT_CHARACTERISTIC_2A19_01_VALUE_HANDLE:
            {
                //battery read.
            #if 0
                DeviceBatInfo* pb = rdx_protocol_update_dev_battery_level();
                char d[30];
                memset(d, 0, 30);
                sprintf(d, "<%d,%d,%d>", pb->tbat_percent_L, pb->tbat_percent_R, pb->tbat_percent_C);
                att_value_len = strlen(d);
                y_printf("%s --> %s, att_value_len = %d \r", __func__, d, att_value_len);
                if(buffer){
                    strncpy(buffer, d, att_value_len);
                }
            #else
                u8 master_bat = rdx_battery_get_percent();//get_self_battery_level() * 10 + 10;
                if (master_bat > 100) {
                    master_bat = 100;
                }  
                y_printf("%s --> master_bat: %d \r", __func__, master_bat);

                att_value_len = 1;
                if (buffer) {
                    buffer[0] = master_bat;
                }   
            #endif  
            }
            break;

        case HID_PROTOCOL_MODE_VALUE_HANDLE:
        case HID_REPORT_MAP_VALUE_HANDLE:
        case HID_INFORMATION_VALUE_HANDLE:
        case HID_INPUT_REPORT_VALUE_HANDLE:
        case HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE:
            att_value_len = rdx_hogp_att_read(connection_handle, handle, offset, buffer, buffer_size);
            if (att_value_len) {
                y_printf("[HOGP] read hdl=0x%04x offset=%d len=%d\r", handle, offset, att_value_len);
            }
            break;


        default:
            break;
    }

    log_info("\natt_value_len = %d \r", att_value_len);
    return att_value_len;
}

/**************************************************************************
 * function: rdx_ble_server_gatt_receive_data
 * description: 
 * param (u8*) p_data
 * param (u16) len
 * return (*)
 **************************************************************************/
void rdx_ble_server_gatt_receive_data(u8* p_data, u16 len)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    int res = 0;
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    // y_printf("%s --> recieve data len = %d \r", __func__, len);
    // printf("------------------ before --------------\r");
    // mem_stats();
    
    // rdx_protocol_recieve_data_handle(p_data, len);
    rdx_protocol_packet_recv(p_data, len);
}

/**************************************************************************
 * function: rdx_ble_server_syn_data_after_ble_write_ready
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
static void rdx_ble_server_stream_tx_ready_cb(void* priv)
{
    g_rdx_ble_server_info.stream_tx_ready = TRUE;
    y_printf("[BLE] Stream TX ready! (delayed after sync data)\r");
}

void rdx_ble_server_syn_data_after_ble_write_ready(void* priv)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    RecordStatus* rp = rdx_record_get_status();
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    g_syn_data_timer = 0;

    y_printf("====== %s --> rp->run: %d, rp->mode: %d, rp->orig_mode: %d \r", __func__, rp->run, rp->mode, rp->orig_mode);

    if(rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME){
        y_printf("====== %s --> sync record state to app \r", __func__);
        rdx_protocol_record_state_indicate();
    } else {
        r_printf("====== %s --> record not running, skip sync. rp->run: %d \r", __func__, rp->run);
    }

    //check record mode.
    rdx_record_mode_active_check(0);

    sys_timeout_add(NULL, rdx_ble_server_stream_tx_ready_cb, 500);
}

/**************************************************************************
 * function: rdx_ble_server_att_write_callback
 * description: 
 * param (void) *hdl
 * param (hci_con_handle_t) connection_handle
 * param (uint16_t) att_handle
 * param (uint16_t) transaction_mode
 * param (uint16_t) offset
 * param (uint8_t) *buffer
 * param (uint16_t) buffer_size
 * return (*)
 **************************************************************************/
static int rdx_ble_server_att_write_callback(void *hdl, hci_con_handle_t connection_handle, uint16_t att_handle, uint16_t transaction_mode, uint16_t offset, uint8_t *buffer, uint16_t buffer_size)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    int result = 0;
    u16 tmp16;
    u16 handle = att_handle;
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    // g_printf("<-------------write_callback, handle= 0x%04x,size = %d \r", handle, buffer_size);

    // In HOGP mode, route all HID Service writes to the HOGP handler so
    // that default branches in the RDX switch do not swallow them.
    if (hogp_mode_get() && handle >= HID_SERVICE_START_HANDLE && handle <= HID_SERVICE_END_HANDLE) {
        return rdx_hogp_att_write(connection_handle, handle, transaction_mode, offset, buffer, buffer_size);
    }

    switch (handle) {
        case ATT_CHARACTERISTIC_2A00_01_VALUE_HANDLE:
            break;

        case ATT_CHARACTERISTIC_06068D1C_6B97_11EF_B864_0241AC120002_01_VALUE_HANDLE:
            // log_info("rx(%d):\r", buffer_size);
            // put_buf(buffer, buffer_size);
            {
                char temp[30];
                memset(temp, 0, 30);
                if(buffer_size < 30){
                    strncpy(temp, (char*)buffer, buffer_size);
                }else{
                    strncpy(temp, (char*)buffer, 30);
                }
                g_printf("======> %s \r", temp);
            }

            //parse data.
            rdx_ble_server_gatt_receive_data(buffer, buffer_size);
            // os_taskq_post_msg(RDX_PROTOCOL_RECV_TASK_NAME, 2, buffer, buffer_size); 
            break;

        case ATT_CHARACTERISTIC_00239A7F_C616_89BB_3374_F15AF588A7B3_01_VALUE_HANDLE:
            {
                log_info("ota rx(%d):\r", buffer_size);
                rdx_protocol_ota_handle(buffer, buffer_size);
            }
            break;

        case ATT_CHARACTERISTIC_06068D2C_6B97_11EF_B864_0242AC120002_01_CLIENT_CONFIGURATION_HANDLE:
            log_info("\nwrite ccc:%04x, %02x\n", handle, buffer[0]);
            att_set_ccc_config(handle, buffer[0]);
            g_rdx_ble_server_info.ccc_configured = (buffer[0] == 0x01) ? TRUE : FALSE;

            if(g_syn_data_timer) {
                sys_timeout_del(g_syn_data_timer);
                g_syn_data_timer = 0;
            }
            g_syn_data_timer = sys_timeout_add(NULL, rdx_ble_server_syn_data_after_ble_write_ready, 1000);
            y_printf("====== syn_data_timer created: %d \r", g_syn_data_timer);
            break;

        case ATT_CHARACTERISTIC_00239A8F_C616_89BB_3374_F25AF588A7B3_01_CLIENT_CONFIGURATION_HANDLE:
            // rdx_ble_server_check_connetion_updata_deal();
            log_info("\n ota character write ccc:%04x, %02x\n", handle, buffer[0]);
            att_set_ccc_config(handle, buffer[0]);
            break;

        case HID_OUTPUT_REPORT_VALUE_HANDLE:  // Output Report (LED state)
            if (buffer_size >= 1) {
                y_printf("[HOGP] output report write, LED=0x%02x\r", buffer[0]);
            }
            return 0;

        default:
            break;
    }
    return 0;
}

/**************************************************************************
 * function: rdx_ble_server_fill_adv_data
 * description: 
 * param (u8) *adv_data
 * return (*)
 **************************************************************************/
static u8 rdx_ble_server_fill_adv_data(u8 *adv_data)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    u8 offset = 0;
    const char *name_p = rdx_ble_server_get_local_name();
    int name_len = strlen(name_p);
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    //make eir.
    offset += make_eir_packet_val(&adv_data[offset], offset, HCI_EIR_DATATYPE_FLAGS, 0x0A, 1);
    if(name_len > BLE_LOCAL_NAME_MAX_LEN){
        name_len = BLE_LOCAL_NAME_MAX_LEN;
    }
    offset += make_eir_packet_data(&adv_data[offset], offset, HCI_EIR_DATATYPE_COMPLETE_LOCAL_NAME, (void *)name_p, name_len);

    if (offset > ADV_RSP_PACKET_MAX) {
        r_printf("***rsp_data overflow!!!!!!\n");
        return 0;
    }
    return offset;
}

/**************************************************************************
 * function: rdx_ble_server_fill_rsp_data
 * description: 
 * param (u8) *rsp_data
 * return (*)
 **************************************************************************/
static u8 rdx_ble_server_fill_rsp_data(u8 *rsp_data)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    u8 offset = 0;
    u8 ble_mac[RDX_MAC_LEN];
    u8 tmp_ble_addr[RDX_MAC_LEN];
    u8 manu_data[MANUFAC_DATA_LENGTH] = {0};
    u8 hd_code[PRODUCT_CODE_SIZE] = PRODUCT_CODE;
    u8 fac[FACTORY_CODE_SIZE] = FACTORY_CODE;
    u8 len = 0;
    u8 bd = rdx_vm_get_bound_status();
#if (RDX_AI_TRANSLATE_SUPPORT == 1)
    AImodeInfo* pm = rdx_app_get_AI_mode_info();
#endif
    u32 ability = RDX_DEVICE_ABILITY;
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    memset(ble_mac, 0, RDX_MAC_LEN);
    // rdx_ble_server_get_ble_mac(ble_mac);
    le_controller_get_mac(ble_mac);
    r_printf("%s -->ble mac: %02X:%02X:%02X:%02X:%02X:%02X \r", __FUNCTION__, ble_mac[5], ble_mac[4], ble_mac[3], ble_mac[2], ble_mac[1], ble_mac[0]);
    //pack mannufacture data.
    memset(manu_data, 0, sizeof(manu_data));
    //hardware code.
    strncpy((char*)manu_data, (char*)hd_code, PRODUCT_CODE_SIZE);
    len += PRODUCT_CODE_SIZE;
    //ble mac.
    memset(tmp_ble_addr, 0, RDX_MAC_LEN);
    memcpy(tmp_ble_addr, ble_mac, RDX_MAC_LEN);
    rdx_util_reverse_byte(tmp_ble_addr, RDX_MAC_LEN);
    memcpy(manu_data + len, tmp_ble_addr, RDX_MAC_LEN);
    len += RDX_MAC_LEN;
    //fac code.
    memcpy(manu_data + len, fac, FACTORY_CODE_SIZE);
    len += FACTORY_CODE_SIZE;
#if (RDX_AI_TRANSLATE_SUPPORT == 1)
    //AI mode & bound/unbound.
    manu_data[len++] = 0x00; //pm->ai_mode << 7 & 0x80;
    // y_printf("****** ai_mode = %d, manu_data[len] = 0x%02X, manu_len = %d \r", pm->ai_mode, manu_data[len - 1], len);
#else
    if(bd == 0){
        manu_data[len++] &= bd << ADV_MODE_BIT_MASK_BOUND;
    }else{
        manu_data[len++] |= bd << ADV_MODE_BIT_MASK_BOUND;
    }
    y_printf("****** bd = %d, manu_data[len] = 0x%02X, manu_len = %d, ability = %08X \r", bd, manu_data[len], len + 1, ability);
#endif
    // Add ability to manu_data
    manu_data[len++] = GET_INT_BYTE1(ability);
    manu_data[len++] = GET_INT_BYTE2(ability);
    manu_data[len++] = GET_INT_BYTE3(ability);
    manu_data[len++] = GET_INT_BYTE4(ability);
    // protocol version.
#if (BLE_FILE_TRANSFER_PACK_SIZE == BLE_SEND_SINGLE_PACK_SIZE)
    manu_data[len++] = rdx_protocol_get_version();
#endif
    // Add protocol type.
    memcpy(manu_data + len, PRODUCT_TYPE, 2);
    len += 2;
    // Add self mark.
    memcpy(manu_data + len, RDX_SELF_MARK, 2);
    len += 2;
    
    offset += make_eir_packet_data(&rsp_data[offset], offset, HCI_EIR_DATATYPE_MANUFACTURER_SPECIFIC_DATA, (void *)manu_data, len);
    if (offset > ADV_RSP_PACKET_MAX) {
        r_printf("***rsp_data overflow!!!!!!\n");
        return -1;
    }   

    return offset;
}

void rdx_ble_server_adv_interval_change_timer_stop(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(g_rdx_ble_server_info.adv_interval_change_timer){
        sys_timeout_del(g_rdx_ble_server_info.adv_interval_change_timer);
        g_rdx_ble_server_info.adv_interval_change_timer = 0;
    }
}

/**************************************************************************
 * FUNCTION
 *  rdx_ble_server_adv_interval_change_timer_cb
 * DESCRIPTION
 *  
 * PARAMETERS
 *  null
 * RETURNS
 *  null
 * **************************************************************************/
void rdx_ble_server_adv_interval_change_timer_cb(void * priv)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    uint8_t adv_type = ADV_IND;
    uint8_t adv_channel = ADV_CHANNEL_ALL;
    u16 change_adv_interval_min = RDX_BLE_ADV_INTERVAL_LOW; //800*1.25= 1000ms
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    y_printf("=== %s --> 快速广播超时，切换到慢速广播 \r", __func__);
    rdx_ble_server_adv_interval_change_timer_stop();
    
    //check if adv is enabled.
    if (0 == app_ble_adv_state_get(g_rdx_ble_server_info.rdx_ble_server_hdl)) {
        y_printf("=== %s --> adv is not enabled, return \r", __func__);
        return;
    }
    //change adv interval to slow mode.
    app_ble_adv_enable(g_rdx_ble_server_info.rdx_ble_server_hdl, 0);
    app_ble_set_adv_param(g_rdx_ble_server_info.rdx_ble_server_hdl, change_adv_interval_min, adv_type, adv_channel);
    app_ble_adv_enable(g_rdx_ble_server_info.rdx_ble_server_hdl, 1);
    
    // 进入慢速广播后，关闭 LED 灯效（广播继续但 LED 熄灭），但WiFi传输 / 录音中不改变灯效
    RdxWifiInfo* wifi_info = rdx_app_get_wifi_info();
    RecordStatus* rp = rdx_record_get_status();
    if(wifi_info->onoff != TRANSFER_BY_WIFI_ON
       && !(rp && (rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME))){
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_OFF);
    }
}

/**************************************************************************
 * FUNCTION
 *  rdx_ble_server_adv_interval_change_timer_start
 * DESCRIPTION
 *  
 * PARAMETERS
 *  null
 * RETURNS
 *  null
 * **************************************************************************/
void rdx_ble_server_adv_interval_change_timer_start(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    y_printf("=== %s \r", __func__);
    if(g_rdx_ble_server_info.adv_interval_change_timer == 0){
        g_rdx_ble_server_info.adv_interval_change_timer = sys_timeout_add(NULL, rdx_ble_server_adv_interval_change_timer_cb, RDX_BLE_ADV_INTERVAL_CHANGE_TIMEOUT); //2 mins
    }
}

/**************************************************************************
 * FUNCTION
 *  rdx_ble_server_fast_adv_restart
 * DESCRIPTION
 *  重新进入快速广播模式并点亮 LED 灯效（按键唤醒时调用）
 * PARAMETERS
 *  null
 * RETURNS
 *  null
 * **************************************************************************/
void rdx_ble_server_fast_adv_restart(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    uint8_t adv_type = ADV_IND;
    uint8_t adv_channel = ADV_CHANNEL_ALL;
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    y_printf("=== %s --> 按键唤醒，重新进入快速广播 \r", __func__);
    
    // 如果已连接，不需要重启广播
    if (g_rdx_ble_server_info.ble_conn) {
        y_printf("=== %s --> BLE 已连接，无需重启广播 \r", __func__);
        return;
    }
    
    // 停止当前定时器
    rdx_ble_server_adv_interval_change_timer_stop();
    
    // 检查广播是否开启
    if (0 == app_ble_adv_state_get(g_rdx_ble_server_info.rdx_ble_server_hdl)) {
        // 广播未开启，直接开启快速广播
        rdx_ble_server_adv_enable(1);
        return;
    }
    
    // 切换回快速广播间隔
    app_ble_adv_enable(g_rdx_ble_server_info.rdx_ble_server_hdl, 0);
    app_ble_set_adv_param(g_rdx_ble_server_info.rdx_ble_server_hdl, g_rdx_ble_server_info.adv_interval_min, adv_type, adv_channel);
    app_ble_adv_enable(g_rdx_ble_server_info.rdx_ble_server_hdl, 1);
    
    // 重新启动 2 分钟定时器
    rdx_ble_server_adv_interval_change_timer_start();
    
    // 点亮 BLE 广播 LED 灯效，但WiFi传输中不改变灯效
    RdxWifiInfo* wifi_info = rdx_app_get_wifi_info();
    if(wifi_info->onoff != TRANSFER_BY_WIFI_ON){
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_BLE_ADV_START);
    }
}

/**************************************************************************
 * function: rdx_ble_server_ble_is_on_adv
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
int rdx_ble_server_ble_is_on_adv(void)
{ 
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    return app_ble_adv_state_get(g_rdx_ble_server_info.rdx_ble_server_hdl);
}

/**************************************************************************
 * FUNCTION
 *  rdx_ble_server_adv_enable
 * DESCRIPTION
 *  
 * PARAMETERS
 *  null
 * RETURNS
 *  null
**************************************************************************/
int rdx_ble_server_adv_enable(u8 enable)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    uint8_t adv_type = ADV_IND;
    uint8_t adv_channel = ADV_CHANNEL_ALL;
    uint8_t advData[ADV_RSP_PACKET_MAX] = {0};
    uint8_t rspData[ADV_RSP_PACKET_MAX] = {0};
    uint8_t len = 0;

    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    // y_printf("%s --> app_ble_adv_state_get(g_rdx_ble_server_info.rdx_ble_server_hdl) = %d \r", __func__, app_ble_adv_state_get(g_rdx_ble_server_info.rdx_ble_server_hdl));
    if (enable == app_ble_adv_state_get(g_rdx_ble_server_info.rdx_ble_server_hdl)) {
        return 0;
    }
    if (enable) {
        app_ble_set_adv_param(g_rdx_ble_server_info.rdx_ble_server_hdl, g_rdx_ble_server_info.adv_interval_min, adv_type, adv_channel);
        len = rdx_ble_server_fill_adv_data(advData);
        if (len) {
            put_buf(advData, len);
            app_ble_adv_data_set(g_rdx_ble_server_info.rdx_ble_server_hdl, advData, len);
        }
        len = rdx_ble_server_fill_rsp_data(rspData);
        if (len) {
            put_buf(rspData, len);
            app_ble_rsp_data_set(g_rdx_ble_server_info.rdx_ble_server_hdl, rspData, len);
        }
        //start adv interval change timer.
        rdx_ble_server_adv_interval_change_timer_start();
    }
    int ret = app_ble_adv_enable(g_rdx_ble_server_info.rdx_ble_server_hdl, enable);
    g_printf("===== rdx_adv_enable = %d ret = %d\r", enable, ret);
    
    //LED控制：BLE广播开启时，如果未连接则闪烁，但WiFi传输中不改变灯效
    RdxWifiInfo* wifi_info = rdx_app_get_wifi_info();
    if (enable && !g_rdx_ble_server_info.ble_conn && wifi_info->onoff != TRANSFER_BY_WIFI_ON) {
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_BLE_ADV_START);
    }
    
    return 0;
}

/**************************************************************************
 * FUNCTION
 *  rdx_ble_server_adv_data_changed
 * DESCRIPTION
 *  
 * PARAMETERS
 *  null
 * RETURNS
 *  null
**************************************************************************/
void rdx_ble_server_adv_data_changed(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    u8 bt_mac[RDX_MAC_LEN];
    u8 ble_mac[RDX_MAC_LEN];
#if (RDX_AI_TRANSLATE_SUPPORT == 1)
    AImodeInfo* p = rdx_app_get_AI_mode_info();
    int st = tws_api_get_tws_state() & TWS_STA_SIBLING_CONNECTED;
#endif
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    r_printf("%s \r", __func__);

    //adv off.
    rdx_ble_server_adv_enable(0);

    //get mac.
    memset(ble_mac, 0, RDX_MAC_LEN);
#if (RDX_AI_TRANSLATE_SUPPORT == 1)
    if(p->ai_mode == DEVICE_WORK_MODE_AI){
        u8 bt_mac[RDX_MAC_LEN];
        memset(bt_mac, 0, RDX_MAC_LEN);
        syscfg_read(CFG_BT_MAC_ADDR, bt_mac, RDX_MAC_LEN);
        bt_make_ble_address(ble_mac, (void *)bt_mac);
        bt_update_mac_addr(bt_mac);
        app_ble_set_mac_addr(rdx_ble_server_hdl, (void *)ble_mac);
        le_controller_set_mac(ble_mac);
    }else{
        if(st){
            y_printf("$$$$ tws  \r");
            memcpy(ble_mac, p->comAddr, RDX_MAC_LEN);
            bt_update_mac_addr(p->comAddr);
            app_ble_set_mac_addr(rdx_ble_server_hdl, (void *)ble_mac);
            le_controller_set_mac(ble_mac);
        }else{
            y_printf("$$$$ no tws \r");
            rdx_ble_server_get_ble_mac(ble_mac);
        }
    }
#else
    // rdx_ble_server_get_ble_mac(ble_mac);
    le_controller_get_mac(ble_mac);
#endif
    r_printf("%s -->ble mac: %02X:%02X:%02X:%02X:%02X:%02X \r", __FUNCTION__, ble_mac[5], ble_mac[4], ble_mac[3], ble_mac[2], ble_mac[1], ble_mac[0]);     
    // rdx_util_reverse_byte(ble_mac, RDX_MAC_LEN);

    //adv on.
    rdx_ble_server_adv_enable(1);
}

static u8 g_ble_send_fail_cnt = 0;
#define BLE_SEND_FAIL_THRESHOLD     5

u8 rdx_ble_server_get_send_fail_cnt(void)
{
    return g_ble_send_fail_cnt;
}

void rdx_ble_server_reset_send_fail_cnt(void)
{
    g_ble_send_fail_cnt = 0;
}

/**************************************************************************
 * FUNCTION
 *  rdx_ble_server_send
 * DESCRIPTION
 *  发送BLE数据，移除重试机制避免阻塞，增加失败计数用于检测连接异常
 * PARAMETERS
 *  data - 要发送的数据
 *  len - 数据长度
 * RETURNS
 *  成功返回0，失败返回错误码
**************************************************************************/
int rdx_ble_server_send(u8 *data, u32 len)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    int ret = 0;
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    //is connected?
    if(!g_rdx_ble_server_info.ble_con_handle){ 
        g_ble_send_fail_cnt++;
        return -1;
    }
    //is data none?
    if(!data || len == 0){
        log_info("%s --> send data error! no data! \r", __func__);
        return -1;
    }

    //ble send buffer is full?
    if(app_ble_att_vaild_len_get(g_rdx_ble_server_info.rdx_ble_server_hdl) < len){
        g_ble_send_fail_cnt++;
        return -1;
    }

    ret = app_ble_att_send_data(g_rdx_ble_server_info.rdx_ble_server_hdl, 
                               ATT_CHARACTERISTIC_06068D2C_6B97_11EF_B864_0242AC120002_01_VALUE_HANDLE, 
                               data, len, ATT_OP_AUTO_READ_CCC);
    if (ret) { 
        g_ble_send_fail_cnt++;
    } else {
        g_ble_send_fail_cnt = 0;
    }

    return ret;
}

/**************************************************************************
 * function: rdx_ble_server_ota_send
 * description: 
 * param (u8) *data
 * param (u32) len
 * return (*)
 **************************************************************************/
int rdx_ble_server_ota_send(u8 *data, u32 len)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    int ret = 0;
    int i;
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    // y_printf("---> rdx_ble_send len = %d \r", len);
    // put_buf(data, len);
    if(!data || len == 0){
        r_printf("%s --> buf is null \r", __FUNCTION__);
        return 0;
    }
    if(len < 30){
        g_printf("%s ==> %s \n", __func__, data);
    }

    ret = app_ble_att_send_data(g_rdx_ble_server_info.rdx_ble_server_hdl, ATT_CHARACTERISTIC_00239A8F_C616_89BB_3374_F25AF588A7B3_01_VALUE_HANDLE, data, len, ATT_OP_AUTO_READ_CCC);
    if (ret) {
        log_info("ota data send fail\n");
    }
    return ret;
}

/**************************************************************************
 * FUNCTION
 *  rdx_ble_server_app_disconnect
 * DESCRIPTION
 *  
 * PARAMETERS
 *  null
 * RETURNS
 *  null
**************************************************************************/
void rdx_ble_server_app_disconnect(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    if(g_rdx_ble_server_info.ble_conn == FALSE){
        return;
    }
    rdx_ble_server_disconnect(NULL);
}

/**************************************************************************
 * function: rdx_ble_server_auto_shut_down_enable
 * description: 
 * param (u8) enable
 * return (*)
 **************************************************************************/
void rdx_ble_server_auto_shut_down_enable(u8 enable)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    RecordStatus* rp = rdx_record_get_status();
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
#if TCFG_AUTO_SHUT_DOWN_TIME
    y_printf("rdx_ble_server_auto_shut_down_enable: %d\r", enable);
    if (enable) {
        if (bt_get_total_connect_dev() == 0 && g_rdx_ble_server_info.ble_conn == 0 && (rp->run == RECORD_STATE_STOP)) { 
            sys_auto_shut_down_enable();
        }
    } else {
        sys_auto_shut_down_disable();
    }
#endif
}

/**************************************************************************
 * function: rdx_ble_server_get_ble_mac
 * description: 
 * param (void) *addr
 * return (*)
 **************************************************************************/
int rdx_ble_server_get_ble_mac(void *addr)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    int ret = 0;
    u8 mac_buf_tmp[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    u8 mac_buf_tmp2[6] = {0, 0, 0, 0, 0, 0};
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    memset(g_rdx_ble_server_info.ble_mac_addr, 0, 6);
    ret = syscfg_read(VM_RDX_BLE_MAC, g_rdx_ble_server_info.ble_mac_addr, 6);
    if ((ret != 6) || !memcmp(g_rdx_ble_server_info.ble_mac_addr, mac_buf_tmp, 6) || !memcmp(g_rdx_ble_server_info.ble_mac_addr, mac_buf_tmp2, 6)) {
        le_controller_get_mac(g_rdx_ble_server_info.ble_mac_addr);
        syscfg_write(VM_RDX_BLE_MAC, g_rdx_ble_server_info.ble_mac_addr, 6);
    }
    memcpy(addr, g_rdx_ble_server_info.ble_mac_addr, 6);
    return 0;
}

/**************************************************************************
 * function: rdx_ble_server_get_info
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
rdx_ble_server_info_t * rdx_ble_server_get_info(void) 
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    return &g_rdx_ble_server_info;
}

/**************************************************************************
 * function: rdx_ble_server_init
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_ble_server_init(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    const uint8_t *edr_addr = bt_get_mac_addr();
    char temp[LOCAL_NAME_LEN];
    u8 len = 0;
    u8 bt_name_len = 0;
    u8 tmp_ble_addr[RDX_MAC_LEN];
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    log_info("====== %s\n", __func__);
    log_info("edr addr:");
    put_buf((uint8_t *)edr_addr, RDX_MAC_LEN);

    // BLE init
    if (g_rdx_ble_server_info.rdx_ble_server_hdl == NULL) {
        g_rdx_ble_server_info.rdx_ble_server_hdl = app_ble_hdl_alloc();
        if (g_rdx_ble_server_info.rdx_ble_server_hdl == NULL) {
            log_info("g_rdx_ble_server_info.rdx_ble_server_hdl alloc err !\n");
            return;
        }
        //get ble mac.
        memset(tmp_ble_addr, 0, RDX_MAC_LEN);
        int r = le_controller_get_mac(tmp_ble_addr);
        if(r != 0 && is_invalid_ble_mac(tmp_ble_addr)) {
            rdx_ble_server_get_ble_mac(tmp_ble_addr);
            r_printf("le_controller_get_mac fail, use bt addr to make ble mac!\r");
        }else{
            g_printf("le_controller_get_mac success, ble mac: %02X:%02X:%02X:%02X:%02X:%02X \r", tmp_ble_addr[5], tmp_ble_addr[4], tmp_ble_addr[3], tmp_ble_addr[2], tmp_ble_addr[1], tmp_ble_addr[0]);
        }
        // le_controller_set_mac(tmp_ble_addr);

        app_ble_set_mac_addr(g_rdx_ble_server_info.rdx_ble_server_hdl, (void *)tmp_ble_addr);
        app_ble_profile_set(g_rdx_ble_server_info.rdx_ble_server_hdl, rdx_profile_data);
        app_ble_att_read_callback_register(g_rdx_ble_server_info.rdx_ble_server_hdl, rdx_ble_server_att_read_callback);
        app_ble_att_write_callback_register(g_rdx_ble_server_info.rdx_ble_server_hdl, rdx_ble_server_att_write_callback);
        app_ble_att_server_packet_handler_register(g_rdx_ble_server_info.rdx_ble_server_hdl, rdx_ble_server_cbk_packet_handler);
        app_ble_hci_event_callback_register(g_rdx_ble_server_info.rdx_ble_server_hdl, rdx_ble_server_cbk_packet_handler);
        app_ble_l2cap_packet_handler_register(g_rdx_ble_server_info.rdx_ble_server_hdl, rdx_ble_server_cbk_packet_handler);
        app_ble_sm_event_callback_register(g_rdx_ble_server_info.rdx_ble_server_hdl, rdx_ble_server_sm_event_callback);

        //init HOGP submodule.
        rdx_hogp_init(g_rdx_ble_server_info.rdx_ble_server_hdl);

        //init sem.
        os_mutex_create(&g_rdx_ble_server_info.ble_send_queue_mutex);

        //enable ble broadcast.
        rdx_ble_server_adv_enable(1);
    }
}

/**************************************************************************
 * function: rdx_ble_server_exit
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_ble_server_exit(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    log_info("====== %s\n", __func__);

    // BLE exit
    if (app_ble_get_hdl_con_handle(g_rdx_ble_server_info.rdx_ble_server_hdl)) {
        app_ble_disconnect(g_rdx_ble_server_info.rdx_ble_server_hdl);
    }

    //do disconnect and stop adv.
    rdx_ble_server_app_disconnect();
    rdx_ble_server_adv_enable(0);
    
    app_ble_hdl_free(g_rdx_ble_server_info.rdx_ble_server_hdl);
    g_rdx_ble_server_info.rdx_ble_server_hdl = NULL;
}

u8 rdx_ble_server_is_stream_tx_ready(void)
{
    return g_rdx_ble_server_info.stream_tx_ready;
}

#endif
