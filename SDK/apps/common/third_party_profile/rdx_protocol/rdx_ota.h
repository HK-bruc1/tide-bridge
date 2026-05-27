/*=====================================================================================
 HEADER NAME: rdx_ota.h
 MODULE NAME: rdx ota module headfile.
 
 PRE-INCLUDE FILES DESCRIPTION: 	
 
 GENERAL DESCRIPTION: 	
 	This File will gather functions that special handle msg(UserEvent,Timer) from 
 	Intergration.These functions don't be changed by project changed.
 =======================================================================================	
 Revision History: 
 ---------------------------------------
 Author: sheng.dong
 Date: 2024-10-16 22:52:41
 LastEditors: sheng.dong
 LastEditTime: 2024-10-16 22:52:42
 FilePath: \SDK\apps\common\third_party_profile\rdx_protocol\rdx_ota.h
 
 Self-documenting Code
=====================================================================================*/

#ifndef __RDX_OTA__
#define __RDX_OTA__

/******************************************************************************
* Include files
******************************************************************************/ 
#include "typedef.h"
// #include "asm/cpu.h"

/******************************************************************************
* Macro Define Section
******************************************************************************/ 
#define OTA_MAX_DATA_LEN					128

#define READ_BIG_U32(a)						((*((u8*)(a)) <<24) + (*((u8*)(a)+1)<<16) + (*((u8*)(a)+2)<<8) + *((u8*)(a)+3))

/******************************************************************************
* Structure and Enum Section
******************************************************************************/ 
typedef enum {
    RDX_BLE_SUCCESS  = 0x00,
    RDX_BLE_ERR_INTERNAL,
    RDX_BLE_ERR_NOT_FOUND,
    RDX_BLE_ERR_NO_EVENT,
    RDX_BLE_ERR_NO_MEM,
    RDX_BLE_ERR_INVALID_ADDR, // Invalid pointer supplied
    RDX_BLE_ERR_INVALID_PARAM, // Invalid parameter(s) supplied.
    RDX_BLE_ERR_INVALID_STATE, // Invalid state to perform operation.
    RDX_BLE_ERR_INVALID_LENGTH,
    RDX_BLE_ERR_DATA_SIZE,
    RDX_BLE_ERR_TIMEOUT,
    RDX_BLE_ERR_BUSY,
    RDX_BLE_ERR_COMMON,
    RDX_BLE_ERR_RESOURCES,
    RDX_BLE_ERR_UNKNOWN, // other ble sdk errors
} rdx_ble_status_t;

typedef enum {
    RDX_BLE_OTA_REQ,
    RDX_BLE_OTA_FILE_INFO,
    RDX_BLE_OTA_FILE_OFFSET_REQ,
    RDX_BLE_OTA_DATA,
    RDX_BLE_OTA_END,
    RDX_BLE_OTA_UNKONWN,
} rdx_ble_ota_data_type_t;

enum {
    RDX_OTA_STATE_NORMAL = 0,
    RDX_OTA_STATE_PID_NO_MATCH,
    RDX_OTA_STATE_VER_LOW,
    RDX_OTA_STATE_FILE_SIZE_TOO_LARGE,
};

enum {
    RDX_OTA_DATA_SUCC = 0,
    RDX_OTA_DATA_PKT_NUM_ERR,
    RDX_OTA_DATA_LEN_ERR,
    RDX_OTA_DATA_CRC_FAILED,
    RDX_OTA_DATA_OTHER_ERR,
};

typedef struct {
    rdx_ble_ota_data_type_t type;
    u16 data_len;
    u8 *p_data;
} rdx_ble_ota_response_t;


typedef struct rdx_ota_req_response {
    u8  flag;
    u8  ota_version;
    u8  reserve;
    u32 frame_version;
    u16 max_pkt_len;
} rdx_ota_req_response_t;

typedef struct rdx_ota_file_info_response {
    u8  reserve;
    u8  state;
    u32 store_file_len;         //用于断点续传，目前不支持
    u32 store_crc;
    u8  md5[16];                //目前不使用
} rdx_ota_file_info_response_t;

typedef struct rdx_ota_file_offset_response {
    u8  reserve;
    u32 offset;
} rdx_ota_file_offset_response_t;

typedef struct rdx_ota_data_response {
    u8  reserve;
    u8  state;
} rdx_ota_data_response_t;

typedef struct rdx_ota_end_response {
    u8  reserve;
    u8  state;
} rdx_ota_end_response_t;


typedef enum{
	OTA_UPGRADE_BEGIN,
	OTA_DATA_DL
}OTA_DataType;

/******************************************************************************
* Local Variables Section
******************************************************************************/ 



/******************************************************************************
* Global Variables Section
******************************************************************************/ 


/******************************************************************************
* Function Section
******************************************************************************/ 
void rdx_ota_proc(u16 type, u8 *recv_data, u32 recv_len);



#endif