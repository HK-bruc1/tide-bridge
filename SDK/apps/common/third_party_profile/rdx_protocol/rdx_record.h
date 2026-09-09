/*=====================================================================================
 HEADER NAME:  rdx_record.h
 MODULE NAME:  rdx record application headfile.
 
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
 LastEditTime: 2024-10-16 14:27:12
 FilePath: \SDK\apps\common\third_party_profile\rdx_protocol\rdx_record.h
 
 Self-documenting Code
=====================================================================================*/

#ifndef _RDX_RECORD_H_
#define _RDX_RECORD_H_

#include "rdx_ble_session.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
* Include files
******************************************************************************/ 
#include "os/os_type.h"
#include "key_driver.h"
#include "app_msg.h"

/******************************************************************************
* Macro Define Section
******************************************************************************/ 
#define RECORD_STATE_START                              (0u)
#define RECORD_STATE_PAUSE                              (1u)
#define RECORD_STATE_RESUME                             (2u)
#define RECORD_STATE_STOP                               (3u)

#define RECORD_FORMATE_OPUS_32K_MONO                    (0u)
#define RECORD_FORMATE_OPUS_16K_MONO                    (1u)
#define RECORD_FORMATE_OPUS_16K_STERO                   (2u)
#define RECORD_FORMATE_OPUS_8K_MONO                     (3u)

// 与协议定义保持一致 (COMMAND_RECORD_SCENE_CHAT=0, COMMAND_RECORD_SCENE_CALL=1)
#define RECORD_SCENE_CHAT                               (0u)  // 会议模式
#define RECORD_SCENE_CALL                               (1u)  // 通话模式

#define CALL_STATE_OFF                                  (0)
#define CALL_STATE_ON                                   (1)
#define CALL_STATE_READY                                (2)
#define CALL_STATE_ACCEPT                               (3)

#define RECORD_MODE_OFFLINE                             (0u)
#define RECORD_MODE_ONLINE                              (1u)

#define RECORD_MIC_DB_VALUE_MIN                         (0)
#define RECORD_MIC_DB_VALUE_MAX                         (19)

#define RECORD_FILE_NAME_LENGTH                         (10)

/******************************************************************************
* Structure and Enum Section
******************************************************************************/
typedef enum {
    REC_PROCESS_STATE_READY,
    REC_PROCESS_STATE_BUSY
} RecProcessState;

typedef void (*RecUIFunction)(void);

typedef struct {
    u8 run;
    u8 formate;
    u8 scene;
    u8 orig_scene;
    bool is_switch;
    u8 switch_orig_scene;
    u8 noshow;
    u8 process_state;
    u8 mode;
    u8 orig_mode;
    bool key_trigger;
    RecUIFunction ui_notify;
    u32 begin_time;
    bool rerun;
    bool stream_discont;
    u32 pause_start_ms;
    u32 paused_accumulated_ms;
    u16 pause_timeout_timer;
} RecordStatus;

typedef struct {
    u8 cmd;
    u8 formate;
    u8 type; 
} Record_info;

typedef struct {
    u8 id;
    u8 type;
    u16 len;
    u8 data[20];
} Record_indicate_data;

typedef struct { 
    bool chat_mic_flag;
    bool call_mic_flag;
    u8 chat_mic0_gain;
    u8 chat_mic1_gain;
    u8 call_mic0_gain; //骨麦
    u8 call_mic1_gain;
} MicGainPara;


#define TWO_BYTE_TO_DATA(x)         ((x[0] << 8) + x[1])
#define U16_TO_LITTLEENDIAN(x)      (((x & 0xff) << 8) + (x & 0xff00))
#define FOUR_BYTE_TO_DATA(x)        ((x[0] << 24) + (x[1] << 16) + (x[2] << 8) + x[3])
#define RDX_LEGAL_CHAR(c)           ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))

/**************************************************************************
 * V24: 录音标记 source 编码
 *   - 与 rdx_protocol.h::RDX_RECMARK_SOURCE_KEY/APP 同值，互通无需转换
 **************************************************************************/
#define RDX_MARK_SOURCE_KEY     (0u)   /* 设备按键触发 */
#define RDX_MARK_SOURCE_APP     (1u)   /* APP 下发触发 */

/******************************************************************************
* Function Section
******************************************************************************/ 
void rdx_record_process(void);
void rdx_record_cmd_handle(Record_info *r_info);
/* App-originated commands carry the RDX link token captured by the protocol
 * callback.  A stale token must never be allowed to mutate recording state. */
void rdx_record_cmd_handle_from_rdx(Record_info *r_info,
                                    const rdx_ble_async_token_t *token);
/* Capture and validate the fixed token of the current App-originated online
 * recording session.  Callers that queue a record indication must retain this
 * value instead of looking up the current RDX owner when the callback runs. */
u8 rdx_record_online_session_token_capture(rdx_ble_async_token_t *token);
u8 rdx_record_online_session_token_is_current(
    const rdx_ble_async_token_t *token);
void rdx_record_stream_only_start_arm(const rdx_ble_async_token_t *token);
void rdx_record_stream_only_start_cancel(void);
u8 rdx_record_stream_only_session_is_active(void);
int rdx_record_task_create(void);
int rdx_record_task_free(void);
RecordStatus* rdx_record_get_status(void);
int rdx_record_run_exit(void);
int rdx_record_run_init(void);
int rdx_record_run_data_handle(u8* d, u32 len);

int rdx_record_add_mark(u8 source);
u32 rdx_record_get_active_offset_ms(void);
void rdx_record_clear_marks(void);

u8 rdx_record_get_marks(u32 *out, u8 max);


#define RDX_RECORD_MIC_MODE_CHAT     (0)
#define RDX_RECORD_MIC_MODE_CALL     (1)

/* 查询指定 mode 下 mic1/mic2 的增益.
 *   @param mode   RDX_RECORD_MIC_MODE_CHAT / RDX_RECORD_MIC_MODE_CALL
 *   @param gain1  [out] 返回 mic0 增益 (chat_mic0 / call_mic0, 骨麦)
 *   @param gain2  [out] 返回 mic1 增益 (chat_mic1 / call_mic1)
 *   @return 0=成功, 非 0=失败 (mode 非法 / VM 不可读) */
int rdx_record_mic_gain_query(int mode, int* gain1, int* gain2);

/* 设置指定 mode 下 mic1/mic2 的增益.
 *   - 越界通道 (RECORD_MIC_DB_VALUE_MIN ~ MAX 之外) 自动跳过, 不计入失败.
 *   - 仅两通道都成功更新时, 才设置 chat_mic_flag / call_mic_flag.
 *   - 写 VM 失败时 gain1 / gain2 会被回填为 VM 当前实际值, 便于 ack 回包.
 *   @param mode   RDX_RECORD_MIC_MODE_CHAT / RDX_RECORD_MIC_MODE_CALL
 *   @param gain1  [in/out] 请求值; 失败时被覆盖为 VM 实际值
 *   @param gain2  [in/out] 请求值; 失败时被覆盖为 VM 实际值
 *   @return 0=成功, 非 0=失败 */
int rdx_record_mic_gain_set(int mode, int* gain1, int* gain2);

/* V24: PAUSE + BLE 断开兜底超时 */
#ifndef RDX_RECORD_PAUSE_TIMEOUT_MS
#define RDX_RECORD_PAUSE_TIMEOUT_MS     (30u * 60u * 1000u)  /* 30 min */
#endif

void rdx_record_pause_timeout_start(void);
void rdx_record_pause_timeout_stop(void);

void rdx_record_on_ble_conn_changed(u8 connected);

/* app_core requests; record worker closes streams and acknowledges the ticket. */
int rdx_record_usb_quiesce_request(u32 ticket);
int rdx_record_usb_quiesce_poll(u32 ticket);


#ifdef __cplusplus
}
#endif

#endif
