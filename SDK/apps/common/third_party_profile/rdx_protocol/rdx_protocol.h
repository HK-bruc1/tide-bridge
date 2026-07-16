/*=====================================================================================
 HEADER NAME:  rdx_protocol.h
 MODULE NAME:  rdx protocol application headfile.
 
 PRE-INCLUDE FILES DESCRIPTION: 	
 
 GENERAL DESCRIPTION: 	
 	This File will gather functions that special handle msg(UserEvent,Timer) from 
 	Intergration.These functions don't be changed by project changed.
 =======================================================================================	
 Revision History: 
 ---------------------------------------
 Author: sheng.dong
 Date: 2024-09-16 09:17:34
 LastEditors: sheng.dong
 LastEditTime: 2024-10-16 14:18:25
 FilePath: \SDK\apps\common\third_party_profile\rdx_protocol\rdx_protocol.h
 
 Self-documenting Code
=====================================================================================*/

#ifndef __RDX_PROTOCOL_H__
#define __RDX_PROTOCOL_H__

/******************************************************************************
* Include files
******************************************************************************/ 
#include "system/includes.h"
#include <stdbool.h>
#include "rdx_app_config.h"
#include "rdx_queue.h"
#include "rdx_vm.h"

/******************************************************************************
* Macro Define Section
******************************************************************************/ 
//support protocol.
//------------------ upload command: Device ---> App ------------------ 
#define CMD_UP_HEARTBEAT                    	"*DEV#active#"
#define CMD_UP_RECORD_STATE						"*DEV#record#"
#define CMD_UP_CALLING                      	"*DEV#mode#1#"
#define CMD_UP_NO_CALL                      	"*DEV#mode#0#"
#define CMD_UP_BATTERY_LEVEL                	"*DEV#battery#"
#define CMD_UP_FIRMWARE_VERSION             	"*DEV#version#"
#define CMD_UP_RECORD_TRIGGER					"*DEV#devrec#"
#define CMD_UP_RECORD_STREAM					"*DEV#stream#"

//V24: 闪记 / 录音标记
#define CMD_UP_FLASHNOTE						"*DEV#flashnote#"
#define CMD_UP_RECORD_MARK						"*DEV#recmark#"

#define CMD_UP_CALL_STATE						"*DEV#call#"
#define CMD_UP_RECORD_MODE						"*DEV#switch#"
//ota data request.
#define CMD_UP_OTA_DATA_REQ						"*DEV#otareq#"
#define CMD_UP_UPGRADE							"*DEV#upgrade#"
#define CMD_UP_EDR_STATE						"*DEV#edr#"

#define CMD_UP_ALG_KEY							"*DEV#algkey#"
#define CMD_UP_SDK_KEY							"*DEV#sdkkey#"
#define CMD_UP_RECORD_DATFILEINFO				"*DEV#listreq#"
#define CMD_UP_RECORD_FILEINFO					"*DEV#flist#"
#define CMD_UP_RECORD_FILEDATA					"*DEV#opusfile#"
#define CMD_UP_WIFI_CTRL						"*DEV#wifi#"
#define CMD_UP_SD_MEM							"*DEV#mem#"
#define CMD_UP_SD_FORMAT_CTRL					"*DEV#format#"
#define CMD_UP_SD_FORMAT_RESULT					"*DEV#fmtret#"
#define CMD_UP_BOUND_SET						"*DEV#bound#"
#define CMD_UP_AUTH_SN							"*DEV#authsn#"

#define CMD_UP_FILE_DELETE						"*DEV#delfile#"
#define CMD_UP_BT_NAME_SET						"*DEV#btname#"
#define CMD_UP_BLE_NAME_SET						"*DEV#blename#"
#define CMD_UP_OFFTIME_SET						"*DEV#offtime#"
#define CMD_UP_RTC_SET							"*DEV#rtc#"

#define CMD_UP_SYS_SET_DEFAULT					"*DEV#default#"

#define CMD_UP_GET_FILE_LIST					"*DEV#getlist#"

#define CMD_UP_BT_NAME_CHECK					"*DEV#cbtname#"
#define CMD_UP_BLE_NAME_CHECK					"*DEV#cblename#"
#define CMD_UP_OFFTIME_CHECK					"*DEV#cofftime#"

#define CMD_UP_MIC_GAIN_SET						"*DEV#micgain#"
#define CMD_UP_MIC_GAIN_CHECK					"*DEV#cmicgain#"

#define CMD_UP_APP_OS_TYPE						"*DEV#ostype#"

#define CMD_UP_UNBOUND							"*DEV#unbound#"

#define CMD_UP_FILE_CONTINOUS_TRANSMIT			"*DEV#filect#"
#define CMD_UP_FILE_CMD							"*DEV#filecmd#"

#define CMD_UP_DEVICE_PAIR						"*DEV#devpair#"
#define CMD_UP_DEVICE_UNPAIR					"*DEV#devunpair#"

#define CMD_UP_DEVICE_EPBT						"*DEV#epBT#"

#define CMD_UP_CUSTOM							"*DEV#custom#"

#define CMD_UP_APP_KEY							"*DEV#appkey#"

#define CMD_UP_CHILD_MSG						"*DEV#child#"

#define CMD_UP_CHILD_BLE_CONNECTED				"*DEV#connstate#"

//--------------------------------------------------------------
//used for child.
#define CMD_UP_TWS_INFO							"*DEV#twsinfo#"
//--------------------------------------------------------------
#define CMD_UP_OTA_STATE						"*DEV#otastate#"
#define CMD_UP_OTA_CTRL							"*DEV#otactrl#"

#define CMD_UP_INCHARGE							"*DEV#incharge#"

//length.
#define CMD_HEAD_LENGTH_STREAM					(12)

//------------------ download command: App ---> Device ------------------ 
//heartbeat.
#define CMD_DL_RECORDING_HEARTBEAT				"*APP#active#"   //心跳包，耳机回复
//record.
#define CMD_DL_RECORD_STOP                  	"*APP#record#0#"   //结束录音
#define CMD_DL_RECORD_START                 	"*APP#record#1#"   //开始录音
#define CMD_DL_RECORD_PAUSE                 	"*APP#record#2#"   //暂停录音
#define CMD_DL_RECORD_RESUME                	"*APP#record#3#"   //恢复录音

#define CMD_DL_RECORD                       	"*APP#record#"      //开始录音
#define CMD_DL_RECORD_START_SINGLE          	"*APP#record#1#0#"   //开始录音
#define CMD_DL_RECORD_START_DUAL            	"*APP#record#1#1#"   //开始录音

//V24: 闪记 / 录音标记
#define CMD_DL_FLASHNOTE						"*APP#flashnote#"    //闪记开始/结束（command:0=关闭,1=开启）
#define CMD_DL_RECORD_MARK						"*APP#recmark#"      //录音打标（无参数）

//call state check.
#define CMD_DL_CALL_STATE                   	"*APP#call#"   //查询通话状态、响铃、接通、或未通话
//battery check.
#define CMD_DL_BATTERY_LEVEL                	"*APP#battery#"   //查询电量
//version.
#define CMD_DL_FIRMWARE_VERSION             	"*APP#version#"   //查询版本号
//音频流下发.
#define CMD_DL_AUDIO_STREAM                 	"*APP#stream#"   //音频流下发头
#define CMD_DL_AI_MODE                      	"*APP#AI#1#"
#define CMD_DL_TWS_MODE                     	"*APP#AI#0#"
#define CMD_DL_RECORD_MODE						"*APP#switch#"
//ota
#define CMD_DL_UPGRADE							"*APP#upgrade#"
#define CMD_DL_OTA_DATA							"*APP#ota#"
//get phone system.
#define CMD_DL_SYS_TYPE							"*APP#system#"
#define CMD_DL_CHECK_RECORD_MODE				"*APP#mode#"
#define CMD_DL_BT_INFO_CHECK					"*APP#edr#"
#define CMD_DL_ALG_KEY							"*APP#algkey#"
#define CMD_DL_SDK_KEY							"*APP#sdkkey#"
//record file.
#define CMD_DL_RECORD_DATFILEINFO				"*APP#listreq#"
#define CMD_DL_RECORD_FILEINFO					"*APP#flist#"
#define CMD_DL_RECORD_FILEDATA					"*APP#opusfile#"
#define CMD_DL_WIFI_CTRL						"*APP#wifi#"
#define CMD_DL_SD_MEM							"*APP#mem#"
#define CMD_DL_SD_FORMAT						"*APP#format#"
#define CMD_DL_BOUND_SET						"*APP#bound#"
#define CMD_DL_AUTH_SN							"*APP#authsn#"

#define CMD_DL_FILE_DELETE						"*APP#delfile#"
#define CMD_DL_BT_NAME_SET						"*APP#btname#"
#define CMD_DL_BLE_NAME_SET						"*APP#blename#"
#define CMD_DL_OFFTIME_SET						"*APP#offtime#"
#define CMD_DL_RTC_SET							"*APP#rtc#"

#define CMD_DL_SYS_SET_DEFAULT					"*APP#default#"
#define CMD_DL_GET_FILE_LIST					"*APP#getlist#"

#define CMD_DL_BT_NAME_CHECK					"*APP#cbtname#"
#define CMD_DL_BLE_NAME_CHECK					"*APP#cblename#"
#define CMD_DL_OFFTIME_CHECK					"*APP#cofftime#"

#define CMD_DL_MIC_GAIN_SET						"*APP#micgain#"
#define CMD_DL_MIC_GAIN_CHECK					"*APP#cmicgain#"

#define CMD_DL_APP_OS_TYPE						"*APP#ostype#"

#define APP_OS_TYPE_ANDROID						(0)
#define APP_OS_TYPE_IOS							(1)
#define APP_OS_TYPE_HARMONY						(2)

#define CMD_DL_UNBOUND							"*APP#unbound#"

#define CMD_DL_FILE_CONTINOUS_TRANSMIT			"*APP#filect#"
#define CMD_DL_FILE_CMD							"*APP#filecmd#"

#define CMD_DL_DEVICE_PAIR						"*APP#devpair#"
#define CMD_DL_DEVICE_UNPAIR					"*APP#devunpair#"

#define CMD_DL_APP_KEY							"*APP#appkey#"

#define CMD_DL_DEVICE_EPBT						"*APP#epBT#"

#define CMD_DL_CHILD_MSG						"*APP#child#"

#define CMD_DL_CUSTOM							"*APP#custom#"

// #define CMD_UP_TWS_INFO							"*APP#twsinfo#"

#define CMD_DL_OTA_STATE						"*APP#otastate#"
#define CMD_DL_OTA_CTRL							"*APP#otactrl#"

#define CMD_DL_INCHARGE							"*APP#incharge#"


//---------------------------------------------------------------------------
#define RDX_PROTOCOL_SEND_TASK_NAME				"protocol_send_task"   
#define RDX_PROTOCOL_RECV_TASK_NAME				"protocol_recv_task"   

//Transmit Verification way.
#define RDX_PACKAGE_VERIFY_CHECKSUM				(0x01)
#define RDX_PACKAGE_VERIFY_CRC32				(0x10)

#define RDX_RECORD_CHANNAL_SINGLE				(0)
#define RDX_RECORD_CHANNAL_DUAL					(1)

#define COMMAND_RECORD_STOP                     (0)
#define COMMAND_RECORD_START                    (1)
#define COMMAND_RECORD_PAUSE                    (2)
#define COMMAND_RECORD_RESUME                   (3)

#define COMMAND_RECORD_SCENE_CHAT               (0)
#define COMMAND_RECORD_SCENE_CALL               (1)
#define COMMAND_RECORD_SCENE_MULTIMEDIA         (2)  /* V24 协议表 R203 多媒体 */
#define COMMAND_RECORD_SCENE_FLASHNOTE          (3)  /* V24 协议表 R203 闪记 */

#define COMMAND_RECORD_SAMPLERATE_8K            (0)
#define COMMAND_RECORD_SAMPLERATE_16K           (1)
#define COMMAND_RECORD_SAMPLERATE_32K           (2)


#define BLE_SEND_SINGLE_PACK_SIZE				(4080 * 6)

// [优化] WiFi 包大小支持动态调整（文件数超过 1000 时自动降速）
#define WIFI_SEND_SINGLE_PACK_SIZE_DEFAULT		(8000 * 5)    // 默认大小 20000
#define WIFI_SEND_SINGLE_PACK_SIZE_SMALL		(8000 * 2)    // 文件数多时降速 16000
#define WIFI_SEND_SINGLE_PACK_SIZE				WIFI_SEND_SINGLE_PACK_SIZE_DEFAULT  // 兼容旧代码

#define WIFI_SEND_SINGLE_PACK_MULTI_SIZE		(7200 * 2)

#define BLE_DAT_SINGLE_PACK_SIZE				(460)
#define WIFI_DAT_SINGLE_PACK_SIZE				(7200)

#define BLE_FILE_TRANSFER_PACK_SIZE				BLE_SEND_SINGLE_PACK_SIZE


#define BLE_TRANSFER_GENERAL_CHANNEL			(0)
#define BLE_TRANSFER_OTA_CHANNEL				(1)

#define AUDIO_PACK_MONO_COUNT                   (1)
#define AUDIO_PACK_STERO_COUNT                  (1)
#define AUDIO_PACK_BUF_SIZE                     (512)

#define RDX_APP_FILE_CMD_STOP                   (0)
#define RDX_APP_FILE_CMD_READY                  (1)

#define CUSTOM_CMD_MAX_LENGTH					(30)
#define CUSTOM_VALUE_MAX_LENGTH					(100)

#define CUSTOM_CMD_KEYPRESS_ONCE				"keypress_once"
#define CUSTOM_CMD_KEYPRESS_TWICE				"keypress_twice"
#define CUSTOM_CMD_KEYPRESS_LONG				"keypress_long"

/******************************************************************************
* Structure and Enum Section
******************************************************************************/ 
typedef enum {
    PROTOCOL_EVENT_CMD_NONE,
    PROTOCOL_EVENT_CHILD_CMD_FORWARD,
    PROTOCOL_EVENT_CHILD_CMD_OTA,
    PROTOCOL_EVENT_CMD_OTA_CTRL,                  /* OTA_Ctrl*                     */
    PROTOCOL_EVENT_CMD_OTA_STATE_QUERY,           /* NULL                          */
    PROTOCOL_EVENT_CMD_RECORD,                    /* Record_info*                  */
    PROTOCOL_EVENT_CMD_RTC,                       /* ProtocolRtcParams*            */
    PROTOCOL_EVENT_CMD_NET_CTRL,
    PROTOCOL_EVENT_CMD_NET_STATE_QUERY,
    PROTOCOL_EVENT_CMD_NET_INFO_QUERY,
    PROTOCOL_EVENT_CMD_SERVER_ENV_SWITCH,
    PROTOCOL_EVENT_CMD_SERVER_INTERVAL_SET,
    PROTOCOL_EVENT_CMD_SERVER_INFO_QUERY,
    PROTOCOL_EVENT_CMD_SERVER_ENV_SET,
    PROTOCOL_EVENT_CMD_CUSTOM,                    /* CustomChannel*                */
    PROTOCOL_EVENT_CMD_EC800_FOTA_CTRL,
    PROTOCOL_EVENT_CMD_FLASHNOTE,                 /* u8*  flashnote cmd value      */
    PROTOCOL_EVENT_CMD_RECMARK,                   /* u8*  recmark source value     */
    PROTOCOL_EVENT_CMD_BLE_FILE_TRANSFER_TIMEOUT, /* NULL                          */
    PROTOCOL_EVENT_CMD_BATTERY_QUERY,             /* NULL                          */
    PROTOCOL_EVENT_CMD_INCHARGE_QUERY,            /* NULL                          */
    PROTOCOL_EVENT_CMD_VERSION_QUERY,             /* NULL                          */
    PROTOCOL_EVENT_CMD_AUDIO_STREAM,              /* ProtocolAudioStreamParams*    */
    PROTOCOL_EVENT_CMD_RECORD_MODE_QUERY,         /* NULL                          */
    PROTOCOL_EVENT_CMD_SD_MEM_QUERY,              /* NULL                          */
    PROTOCOL_EVENT_CMD_SD_FORMAT,                 /* NULL                          */
    PROTOCOL_EVENT_CMD_AUTH_SN,                   /* NULL                          */
    PROTOCOL_EVENT_CMD_BT_NAME_SET,               /* ProtocolNameParams*           */
    PROTOCOL_EVENT_CMD_BT_NAME_QUERY,             /* NULL                          */
    PROTOCOL_EVENT_CMD_BLE_NAME_SET,              /* ProtocolNameParams*           */
    PROTOCOL_EVENT_CMD_BLE_NAME_QUERY,            /* NULL                          */
    PROTOCOL_EVENT_CMD_OFFTIME_SET,               /* ProtocolOfftimeParams*        */
    PROTOCOL_EVENT_CMD_OFFTIME_QUERY,             /* NULL                          */
    PROTOCOL_EVENT_CMD_MIC_GAIN_SET,              /* ProtocolMicGainSetParams*     */
    PROTOCOL_EVENT_CMD_MIC_GAIN_QUERY,            /* ProtocolMicGainQueryParams*   */
    PROTOCOL_EVENT_CMD_BOUND,                     /* ProtocolBoundParams*          */
    PROTOCOL_EVENT_CMD_UNBOUND,                   /* ProtocolUnboundParams*        */
    PROTOCOL_EVENT_CMD_FILE_DELETE,               /* ProtocolFileDeleteParams*     */
    PROTOCOL_EVENT_CMD_OS_TYPE,                   /* ProtocolOsTypeParams*         */
    PROTOCOL_EVENT_CMD_SYS_RESET,                 /* NULL                          */
#if RDX_PRODUCT_IS_CHARGE_CASE
    PROTOCOL_EVENT_CMD_DEVICE_PAIR,               /* ProtocolDevicePairParams*     */
    PROTOCOL_EVENT_CMD_DEVICE_UNPAIR,             /* NULL                          */
#endif
    PROTOCOL_EVENT_CMD_TYPE_MAX
}ProtocolEvents;

/* ----------------------------------------------------------------------------
 * 事件参数结构体: 协议层 parse 完填充, app 层直接读字段执行业务
 * ---------------------------------------------------------------------------- */
typedef struct {
    u32 timestamp;        /* unix timestamp 秒 (0 = 无效) */
} ProtocolRtcParams;

typedef struct {
    u8  cmd;              /* 1 = bind, 0 = unbind                          */
} ProtocolBoundParams;

typedef struct {
    int user_para;        /* APP 给的解绑用户 ID                           */
    int format_en;        /* 是否同步格式化存储 (0/1)                      */
} ProtocolUnboundParams;

typedef struct {
    int  file_sn;
    char file_name[64];
} ProtocolFileDeleteParams;

typedef struct {
    char name[64];        /* 设置名称 (空=查询当前)                        */
    u8   has_value;       /* 1 = 设置, 0 = 查询                            */
} ProtocolNameParams;     /* BT_NAME_SET / BLE_NAME_SET 共用               */

typedef struct {
    u32 offtime;          /* 自动关机时长 (秒)                             */
    u8  has_value;        /* 1 = 设置, 0 = 查询当前                        */
} ProtocolOfftimeParams;

typedef struct {
    int mode;             /* 0=chat, 1=call                                */
    int mic1_gain;
    int mic2_gain;
} ProtocolMicGainSetParams;

typedef struct {
    int mode;             /* 查询的 mic gain 模式 (0/1)                    */
} ProtocolMicGainQueryParams;

typedef struct {
    int os_type;          /* APP_OS_TYPE_ANDROID/IOS/HARMONY               */
} ProtocolOsTypeParams;

#if RDX_PRODUCT_IS_CHARGE_CASE
typedef struct {
    char auth_code[RDX_BLE_DEVICE_AUTH_KEY_SIZE + 1];
    char ep_mac[RDX_BLE_MAC_STRING_SIZE + 1];
    char case_mac[RDX_BLE_MAC_STRING_SIZE + 1];
    char label_sn[RDX_LABEL_SN_SIZE + 1];
} ProtocolDevicePairParams;
#endif /* RDX_PRODUCT_IS_CHARGE_CASE */

typedef struct {
    int total;            /* 帧总数, 0 表示关闭 player                     */
    int per;              /* 每帧大小                                      */
    u8 *stream;           /* 流数据指针 (位于 cmd buffer 内部)             */
    u16 stream_len;       /* 流数据长度                                    */
} ProtocolAudioStreamParams;

typedef enum{
	E_PROTOCOL_ECODE_SUCCESS = 0,
	E_PROTOCOL_ECODE_BADTOKEN,
	E_PROTOCOL_ECODE_INVALID,
	E_PROTOCOL_ECODE_FAIL,
	E_PROTOCOL_ECODE_NOMEMORY,
	E_PROTOCOL_ECODE_FULL,
	E_PROTOCOL_ECODE_CLOSED,
	E_PROTOCOL_ECODE_BAD_LOGIC,
	E_PROTOCOL_ECODE_EXIST,
	E_PROTOCOL_ECODE_TIMEOUT,
	E_PROTOCOL_ECODE_NEED_RECONNECT,
	E_PROTOCOL_ECODE_HOLDING_TIMEOUT,
	E_PROTOCOL_ECODE_CONNECTING_TIMEOUT,
	E_PROTOCOL_ECODE_BUSY,
	E_PROTOCOL_ECODE_IS_DOWNLOAD,
	E_PROTOCOL_ECODE_NOT_SUPPORT,
	E_PROTOCOL_ECODE_CONNECT_STATUS,
	E_PROTOCOL_ECODE_BLOCK,
	E_PROTOCOL_ECODE_TRANSATION_ISFULL,
	E_PROTOCOL_ECODE_MAX
}Protocol_ErrorCode;

typedef enum{
    E_BLE_TRANSMIT_SCENE_NORMAL = 0,
    E_BLE_TRANSMIT_SCENE_ONLINE_RECORD,
    E_BLE_TRANSMIT_SCENE_FILE_SYNC,
    E_BLE_TRANSMIT_SCENE_OTA,
}BLE_TransmitScene;

typedef enum{
	E_FILE_TRANSFER_STOP,
	E_FILE_TRANSFER_SINGLE_PACK,
	E_FILE_TRANSFER_LOOP,
	E_FILE_TRANSFER_FILE_SYNC,
}FileTransferMode;

typedef struct{
    OS_SEM send_sem; 
	cbuffer_t cbuf_hd;
	u8* send_buf;
	u32 total;
	u32 sent;
	u32 fail_cnt;
	u32 sent_cnt;
	u32 send_len;
	u8* pack_data;
	bool busy;
    bool bulk_flag;
}BleBulkSendData;

typedef struct{
    Queue sendQueue;
    OS_SEM send_sem;
    bool send_pending;
    bool bulk_sending;
    DataPacket sendDataPack;
    u8 send_data_retry_cnt;
}BLE_SendData;

typedef struct{
	u8 tbat_charge_L;
	u8 tbat_charge_R;
	u8 tbat_charge_C;
	u8 tbat_percent_L;
	u8 tbat_percent_R;
	u8 tbat_percent_C;
}DeviceBatInfo;

typedef struct{
    u8 *buff;
    u32 len;
}WIFI_recv_buff;

typedef struct{
    char cmd[CUSTOM_CMD_MAX_LENGTH];
    char value[CUSTOM_VALUE_MAX_LENGTH];
}CustomChannel;

typedef struct {
	int	app_select;
	int device_select;
    char *fw_version;
    char *hw_version;
    void (*rdx_protocol_cb)(ProtocolEvents event, void* data, u32 len);
}RdxProtocolCallbacks;

/******************************************************************************
* Function Section
******************************************************************************/ 
int rdx_protocol_task_create(RdxProtocolCallbacks *cb);
void rdx_protocol_send_buffer_reinit(void);
void rdx_protocol_ota_handle(u8* p_data, u16 len);
void rdx_protocol_set_ble_sent(u8 d);
int rdx_protocol_packet_send(void *buf, u16 len);
int rdx_protocol_packet_send_priority(void *buf, u16 len);
void rdx_protocol_call_state_indicate(u8 inter);
DeviceBatInfo* rdx_protocol_update_dev_battery_level(void);
void rdx_protocol_conn_state_set_and_indicate(uint8_t state);
void rdx_protocol_record_file_info_indicate(void);
void rdx_protocol_file_cmd_handle(u8 type);
int rdx_protocol_packet_recv(void *buf, u16 len);
u8 rdx_protocol_get_version(void);
u8 rdx_protocol_package_verify_method(void);
bool rdx_protocol_is_preallocated_buf(void* ptr);

void rdx_protocol_custom_msg_indicate(char* cmd, char* value);
int rdx_protocol_handle_custom_cmd(u8* d, u16 len);

void rdx_protocol_incharge_indicate(void);
u32 rdx_protocol_get_ble_dat_pack_size(void);
char* rdx_protocol_get_firmware_version(void);
char* rdx_protocol_get_hardware_version(void);

typedef struct {
    /* ---- 主动查询/上报类 ---- */
    void (*battery_indicate)(u8 left, u8 right, u8 chargebox);
    void (*incharge_indicate)(u8 charge_state, u8 left, u8 right, u8 chargebox);
    void (*version_indicate)(char* hv, char* sv);
    void (*record_mode_indicate)(u8 scene, u8 run);
    void (*auth_sn_indicate)(void);
    void (*bt_name_check_ack_indicate)(u8 result, const char* name);
    void (*ble_name_check_ack_indicate)(u8 result, const char* name);
    void (*offtime_check_ack_indicate)(u8 result, u32 sec);
    void (*mic_gain_check_ack_indicate)(u8 result, int mode, int gain1, int gain2);
    void (*sd_mem_indicate)(u32 left, u32 total);
    void (*os_type_ack_indicate)(void);
    /* 主动上报 (蓝牙状态 / 媒体 / 音量), 非协议下行命令应答 */
    void (*volume_indicate)(u8 volume);
    void (*conn_state_indicate)(u8 state);
    void (*play_status_indicate)(u8 status);

    /* ---- 下行命令回执类 (result: 0=ok, 非 0=fail) ---- */
    void (*bound_result_ack_indicate)(u8 result);
    void (*unbound_ack_indicate)(u8 result, u8 is_bound);
    void (*file_delete_ack_indicate)(u8 result, int file_sn, char* file_name);
    void (*bt_name_set_ack_indicate)(u8 result, char* bt_name);
    void (*ble_name_set_ack_indicate)(u8 result, char* ble_name);
    void (*offtime_set_ack_indicate)(u8 result, u16 timeout);
    void (*rtc_set_ack_indicate)(u8 result, u32 timestamp);
    void (*mic_gain_set_ack_indicate)(u8 result, int mode, int gain1, int gain2);
    void (*sd_format_ack_indicate)(u8 result);
    void (*sys_set_default_ack_indicate)(u8 result);
#if RDX_PRODUCT_IS_CHARGE_CASE
    void (*device_pair_ack_indicate)(u8 result);
    void (*device_unpair_ack_indicate)(u8 result);
#endif

    /* ---- 流数据 sink (非 ack, 把流写入设备 player) ---- */
    int  (*audio_stream_play)(const ProtocolAudioStreamParams* p);
} RdxProtocolIndicateOps;

const RdxProtocolIndicateOps* rdx_protocol_get_indicate_ops(void);


#include "rdx_ble_server.h"

#if ((RDX_DEVICE_ABILITY) & ABILITY_FLASHNOTE)
#define TDX_HAS_FLASHNOTE_ABILITY      1
#else
#define TDX_HAS_FLASHNOTE_ABILITY      0
#endif

#if ((RDX_DEVICE_ABILITY) & ABILITY_RECORD_MARK)
#define TDX_HAS_RECMARK_ABILITY        1
#else
#define TDX_HAS_RECMARK_ABILITY        0
#endif

/* 录音暂停/恢复 (*APP#record#2#/3#) 独立广播能力位 (协议表 Byte2 bit1).
 * 与 PROTOCOL_VERSION 解耦后, 仅看本位是否点亮即视为支持. 老固件 (未点亮本位)
 * APP 端会主动屏蔽该控件; 本固件已实现完整 PAUSE/RESUME 状态机, 板级默认点亮. */
#if ((RDX_DEVICE_ABILITY) & ABILITY_RECORD_PAUSE_RESUME)
#define TDX_HAS_PAUSE_RESUME_ABILITY   1
#else
#define TDX_HAS_PAUSE_RESUME_ABILITY   0
#endif

static inline bool rdx_protocol_is_flashnote_supported(void)
{
    return TDX_HAS_FLASHNOTE_ABILITY ? true : false;
}

static inline bool rdx_protocol_is_recmark_supported(void)
{
    return TDX_HAS_RECMARK_ABILITY ? true : false;
}

static inline bool rdx_protocol_is_pause_resume_supported(void)
{
    return TDX_HAS_PAUSE_RESUME_ABILITY ? true : false;
}

#define RDX_RECMARK_SOURCE_KEY  (0u)   /* 设备按键触发 */
#define RDX_RECMARK_SOURCE_APP  (1u)   /* APP 下行触发 */

#define RDX_FLASHNOTE_CMD_STOP  (0u)
#define RDX_FLASHNOTE_CMD_START (1u)

#define RDX_FLASHNOTE_STATE_END             (0u)
#define RDX_FLASHNOTE_STATE_RUNNING         (1u)
#define RDX_FLASHNOTE_STATE_FAIL_BUSY       (2u)
#define RDX_FLASHNOTE_STATE_FAIL_NOSPACE    (3u)

void rdx_protocol_flashnote_indicate(u8 state, u32 sn, const char* filename);

#define RDX_RECMARK_RESULT_OK         (0u)
#define RDX_RECMARK_RESULT_BAD_STATE  (1u)   /* 录音暂停 / 未在录音 */
#define RDX_RECMARK_RESULT_BUSY       (2u)   /* 设备忙 / 6s 去重命中 */
#define RDX_RECMARK_RESULT_FULL       (3u)   /* 标记数达上限 */
#define RDX_RECMARK_INDEX_MAX         (20u)
void rdx_protocol_record_mark_indicate(u8 result, u32 sn, const char* filename,
                                       u8 index, u32 offset_ms, u8 source);

/* V24: opus_format 字段位编码（u32，详见协议表末"opus 字段位图说明"）
 *   bit31~26 (6 bit) : sample_rate_kHz   直接存采样率 kHz
 *   bit25~23 (3 bit) : channels          直接存通道数
 *   bit22~16 (7 bit) : frame_ms_x2       帧长(ms) × 2，兼容 2.5ms 半毫秒
 *   bit15~6  (10 bit): bitrate_kbps      编码码率(kbps)
 *   bit5~0   (6 bit) : Reserved          固定 0
 *                                        （协议表 sheet 预留 DTX/CBR/codec_id 扩展位，
 *                                          SDK 当前不暴露这三项选择，整段维持 0）
 *   传输形式：u32 编码 → 转十进制字符串
 *     示例：16 kHz / mono / 20 ms / 16 kbps
 *       PACK = (16<<26)|(1<<23)|(40<<16)|(16<<6) = 0x40A80400 = 1084752896
 *     JSON   : "opus":"1084752896"
 *     协议串 : ...#1084752896#
 */
#define RDX_OPUS_FMT_PACK(sr_kHz, ch, frame_ms_x2, br_kbps) \
    (((u32)((sr_kHz)      & 0x3Fu)  << 26) | \
     ((u32)((ch)          & 0x07u)  << 23) | \
     ((u32)((frame_ms_x2) & 0x7Fu)  << 16) | \
     ((u32)((br_kbps)     & 0x3FFu) << 6))

/* V24: 内部 RECORD_FORMATE_OPUS_* → opus_format u32 编码 */
u32 rdx_protocol_calc_opus_format(u8 record_formate);


#endif

