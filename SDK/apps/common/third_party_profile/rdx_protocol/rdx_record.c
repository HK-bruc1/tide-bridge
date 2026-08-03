/*=====================================================================================
 HEADER NAME: rdx_record.c
 MODULE NAME: rdx record application module.
 
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
 LastEditTime: 2024-10-16 11:09:48
 FilePath: \SDK\apps\common\third_party_profile\rdx_protocol\rdx_record.c
 
 Self-documenting Code
=====================================================================================*/

/******************************************************************************
* Include files
******************************************************************************/ 
#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".rdx_record.data.bss")
#pragma data_seg(".rdx_record.data")
#pragma const_seg(".rdx_record.text.const")
#pragma code_seg(".rdx_record.text")
#endif

#include "rdx_record.h"
#include "effects/eq_config.h"
#include "audio_config.h"
#include "app_tone.h"
#include "vol_sync.h"
#include "key_driver.h"
#include "app_msg.h"
#include "bt_tws.h"
#include "btstack/avctp_user.h"
#include "clock_manager/clock_manager.h"
#include "asm/anc.h"
#include "audio_base.h"
#include "decoder_node.h"
#include "st_opus_enc/opus_stenc_api.h"

#include "rdx_app_config.h"
#include "rdx_record.h"
#include "rdx_protocol.h"
#include "rdx_led_ctrl.h"
#include "rdx_default_hooks.h"
#include "rdx_ble_server.h"
#include "jiffies.h"
#include "rdx_jl_osal.h"
#include "rdx_jl_storage.h"
#include "rdx_file_transfer_service.h"

#if defined(__UUX_FILE__)
#include "rdx_uxfile.h"
#endif

#include "os/os_api.h"
#include "app_tone.h"

#if (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN)

static bool rdx_record_file_transfer_ready(void)
{
    rdx_file_transfer_state_t state;

    return rdx_file_transfer_get_state(&state) == RDX_OK &&
           state != RDX_FILE_TRANSFER_STATE_UNAVAILABLE;
}

static bool rdx_record_file_transfer_admit(RecordStatus *rp)
{
    if (rp->run != RECORD_STATE_START && rp->run != RECORD_STATE_RESUME) {
        return true;
    }
    if (rdx_record_file_transfer_ready()) {
        return true;
    }

    r_printf("====== %s --> file transfer state unavailable \r", __func__);
    rp->run = rp->run == RECORD_STATE_RESUME
            ? RECORD_STATE_PAUSE
            : RECORD_STATE_STOP;
    return false;
}

/******************************************************************************
* Macro Define Section
******************************************************************************/ 
#define LOG_TAG                                         "[rdx_record]"
#define LOG_ERROR_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_INFO_ENABLE
/* #define LOG_DUMP_ENABLE */
#define LOG_CLI_ENABLE
#include "debug.h"

//----------------------------------------------------------------------------------------

#define RECORD_TASK_NAME                                "rdx_record_task"   

#define RECORD_MOTOR_VIB_DURATION                       (300)

#define RECORD_MOTOR_TWICE_ON                           (200)
#define RECORD_MOTOR_TWICE_OFF                          (100)

#define HEARTBEAT_TIMEOUT_COUNT                         (5)

// #define RECORD_HEARTBEAT_SUPPORT

#define RDX_RECORD_STATE_BUSY_TIMEOUT                   (2000) //2s

#define AUDIO_SEND_BUF_SIZE                             (100 * 80)
#define RDX_RECORD_LIMIT_TIME                           ((5*60*60 - 2)* 1000) // 5 hours - 2s

#define RECORD_MIC_0                                    (0)
#define RECORD_MIC_2                                    (2)
#define RECORD_MIC_3                                    (3)

#define RECORD_CHAT_GAIN_DEFAULT_MIC0                   (17)
#define RECORD_CHAT_GAIN_DEFAULT_MIC3                   (17)

#define RECORD_CALL_GAIN_DEFAULT_MIC2                   (17)
#define RECORD_CALL_GAIN_DEFAULT_MIC3                   (13)

/*******************************************************************************
* Structure and Enum Section
*******************************************************************************/


/******************************************************************************
* Global variable Section
******************************************************************************/


/*******************************************************************************
* Local variables Section
*******************************************************************************/
static RecordStatus record_status = {
    .run = RECORD_STATE_STOP,           // 初始化为停止状态，避免开机时被误判为录音中
    .formate = RECORD_FORMATE_OPUS_16K_STERO,
    .scene = RECORD_SCENE_CHAT,
    .orig_scene = RECORD_SCENE_CHAT,
    .process_state = REC_PROCESS_STATE_READY,
};
static u16 record_alive_timer = 0;
static BOOL record_keep = FALSE;
static u8 heartbeat_timer_cnt = 0;
static u8 stream_filter_cnt = 0;

static u16 record_set_process_state_timer = 0; //record process state set timer
static Record_info g_pending_busy_record_info;
static bool g_pending_busy_record_valid = false;
static bool g_pending_busy_replay_posted = false;
static u16 g_pending_busy_replay_timer = 0;

static u8 au_buf[AUDIO_SEND_BUF_SIZE];
static u32 au_len = 0;
static u16 rdx_record_max_timer = 0;

static MicGainPara mic_gain;

static u8 motor_twice_step = 0;
static bool is_record_task_created = false;
static u16 stream_resume_timer = 0;

#define RECORD_CMD_DELAY_MS             (2000)
#define RECORD_CMD_MAX_RETRY            (5)
static u16 g_record_cmd_delay_timer = 0;
static u8 g_record_cmd_retry_cnt = 0;
static Record_info g_pending_record_info;

/**************************************************************************
 * V24: 录音标记缓冲（运行时持有，记录每条标记相对于录音起点的 offset_ms）
 *   - 大小与协议表 RDX_RECMARK_INDEX_MAX (20) 对齐
 *   - 6 秒去重窗口与"写盘 chunk 周期"解耦，避免按键抖动 / APP+按键并发
 *   - PAUSE / STOP 状态不允许打标，由 rdx_record_add_mark 守护
 **************************************************************************/
#define RDX_RECORD_MARK_MAX             (RDX_RECMARK_INDEX_MAX)
#define RDX_RECORD_MARK_DEDUP_WINDOW_MS (6000u)
static u32 s_cur_marks[RDX_RECORD_MARK_MAX];
static u8  s_cur_mark_count = 0;

/******************************************************************************
* Function Declaration Section
******************************************************************************/ 
extern void anc_mode_switch(u8 mode, u8 tone_play);
extern void rdx_tx_speed_cal_timer_stop(void);
extern void rdx_tx_speed_cal_timer_start(void);
extern int translation_ear_recoder_open_all(u8 ch_mode);
extern void translation_ear_recoder_close_all(void);
extern void rdx_ble_server_auto_shut_down_enable(u8 enable);
extern bool rdx_app_get_dut_status(void);
extern void motor_run_by_time(u16 ms);
extern void rdx_record_mode_active_check(bool show);
extern void rdx_protocol_record_state_indicate(void);
extern int get_buffer_vaild_len(void);
extern int rdx_protocol_audio_data_indicate(uint8_t* d, uint16_t len);
extern void rdx_ble_server_auto_shut_down_enable(u8 enable);
extern int rdx_uxfile_dat_1_gen(u8 scene);
extern void rdx_app_do_emmc_reset(void);
extern void os_system_info_output(void);
extern void rdx_app_emmc_poweroff_check(void);
extern void rdx_app_emmc_poweroff_check_timer_stop(void);
extern void rdx_app_emmc_poweron(u8 check_en);

extern int rdx_audio_adc_file_get_gain_checked(u8 mic_index, u8 *gain);
extern int rdx_audio_adc_file_set_gain_checked(u8 mic_index, u8 gain);
extern void force_set_sd_online(char *sdx);
extern int dev_manager_mount(char *logo);
extern void rdx_app_set_record_mode(u8 d);
extern int rdx_uxfile_dat_1_save_gen(void);
extern void rdx_uxfile_operate_file_init(void);
extern uxfile_data_t* rdx_uxfile_get_operateFile_info(void);
extern void rdx_protocol_record_mark_indicate(u8 result, u32 sn, const char* filename,
                                              u8 index, u32 offset_ms, u8 source);
#if (RDX_SUPPORT_MOTOR == 1)
void rdx_record_motor_run(void);
#endif

RecordStatus* rdx_record_get_status(void);
void rdx_record_ui_notify(void);
void rdx_record_set_process_state_ready(void);
void rdx_record_stop(void);
u8 rdx_record_get_filter_cnt(void);
void rdx_record_set_filter_cnt(u8 cnt);
static void rdx_record_process_state_set_timer_cb(void* priv);
static void rdx_record_pending_busy_replay_schedule(void);
static void rdx_record_pending_busy_replay_retry_cb(void* priv);

/******************************************************************************
* Function Section
******************************************************************************/ 
u8 rdx_record_get_filter_cnt(void)
{
    return stream_filter_cnt;
}

void rdx_record_set_filter_cnt(u8 cnt)
{
    stream_filter_cnt = cnt;
}


/**************************************************************************
 * functions: rdx_record_set_alive
 * description: 
 * param (bool) state
 * return (*)
 **************************************************************************/
void rdx_record_set_alive(bool state)
{        
#ifdef RECORD_HEARTBEAT_SUPPORT
    record_keep = state;
#endif
}

/**************************************************************************
 * function: 
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_record_keep_alive_check_stop(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    g_printf("%s \r", __FUNCTION__);
    if(record_alive_timer){
        rdx_os_timer_periodic_del(record_alive_timer);
        record_alive_timer = 0;
    }
    //clear heartbeat_timer_cnt.
    heartbeat_timer_cnt = 0;
    
    if(record_status.run == RECORD_STATE_START || record_status.run == RECORD_STATE_RESUME){
        //timerout to stop recording.
        record_status.run = RECORD_STATE_STOP;
        //send job.
        if (rdx_os_task_post_callback0("app_core", rdx_record_process) != RDX_OK) {
            printf("%s record taskq post err \n", __func__);
        } 
    #if (TCFG_USER_TWS_ENABLE && TCFG_APP_BT_EN) 
        //play record tone.
        tws_play_tone_file(get_tone_files()->record_off, 400);   
    #endif
    }
}

/**************************************************************************
 * function: rdx_record_keep_alive_check_timer_cb
 * description: 
 * param (void*) priv
 * return (*)
 **************************************************************************/
void rdx_record_keep_alive_check_timer_cb(void* priv)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    log_info("%s --> record_keep = %d, cnt = %d, record_alive_timer = %d \r", __FUNCTION__, record_keep, heartbeat_timer_cnt, record_alive_timer);
    //check busy flag.
    if(record_keep == FALSE){
        if(heartbeat_timer_cnt > HEARTBEAT_TIMEOUT_COUNT){
            rdx_record_keep_alive_check_stop();
            record_keep = FALSE;
            heartbeat_timer_cnt = 0;
        }else{
            heartbeat_timer_cnt++;
        }
    }else{
        // g_printf("%s --> record_alive_timer rerun... \r", __FUNCTION__);
        record_keep = FALSE;
        heartbeat_timer_cnt = 0;
        if(record_alive_timer){
            rdx_os_timer_re_run(record_alive_timer);
        }
    }
}

/**************************************************************************
 * function: rdx_record_keep_alive_check_start
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_record_keep_alive_check_start(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    // g_printf("%s --> record_alive_timer = %d \r", __FUNCTION__);
    record_keep = FALSE;
    heartbeat_timer_cnt = 0;
    if(record_alive_timer == 0){
        record_alive_timer = rdx_os_timer_periodic_add(rdx_record_keep_alive_check_timer_cb, (void *)0, 3000);
    }
}

/**************************************************************************
 * function: rdx_record_process_state_timer_stop
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_record_process_state_timer_stop(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    log_info("%s --> record_set_process_state_timer = %d \r", __FUNCTION__, record_set_process_state_timer);
    //stop record process state del timer.
    if(record_set_process_state_timer){
        rdx_os_timer_del(record_set_process_state_timer);
        record_set_process_state_timer = 0;
    }
}

static void rdx_record_pending_busy_replay_cb(void)
{
    Record_info pending;
    bool valid;

    CPU_CRITICAL_ENTER();
    valid = g_pending_busy_record_valid;
    if(valid){
        memcpy(&pending, &g_pending_busy_record_info, sizeof(pending));
        g_pending_busy_record_valid = false;
    }
    g_pending_busy_replay_posted = false;
    CPU_CRITICAL_EXIT();

    if(valid){
        y_printf("[REC_BUSY] replay pending cmd=%c format=%c type=%c\r",
                 pending.cmd, pending.formate, pending.type);
        rdx_record_cmd_handle(&pending);
    }
}

static void rdx_record_pending_busy_replay_retry_cb(void* priv)
{
    (void)priv;
    g_pending_busy_replay_timer = 0;

    CPU_CRITICAL_ENTER();
    g_pending_busy_replay_posted = false;
    CPU_CRITICAL_EXIT();

    rdx_record_pending_busy_replay_schedule();
}

static void rdx_record_pending_busy_replay_schedule(void)
{
    bool should_post = false;

    CPU_CRITICAL_ENTER();
    if(g_pending_busy_record_valid && !g_pending_busy_replay_posted){
        g_pending_busy_replay_posted = true;
        should_post = true;
    }
    CPU_CRITICAL_EXIT();

    if(!should_post){
        return;
    }

    if(rdx_os_task_post_callback0("app_core", rdx_record_pending_busy_replay_cb) != RDX_OK){
        log_info("[REC_BUSY] replay post failed, retry by timeout\r");
        if(g_pending_busy_replay_timer == 0){
            g_pending_busy_replay_timer = rdx_os_timer_add(
                rdx_record_pending_busy_replay_retry_cb, NULL, 20);
        }
        if(g_pending_busy_replay_timer == 0){
            CPU_CRITICAL_ENTER();
            g_pending_busy_replay_posted = false;
            CPU_CRITICAL_EXIT();
            log_info("[REC_BUSY] replay deferred; pending cmd retained\r");
        }
    }
}

/**************************************************************************
 * function: rdx_record_process_state_set_timer_cb
 * description: 
 * param (void*) priv
 * return (*)
 **************************************************************************/
static void rdx_record_process_state_set_timer_cb(void* priv)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    log_info("%s --> record_set_process_state_timer = %d \r", __FUNCTION__, record_set_process_state_timer);

    record_set_process_state_timer = 0;
    rdx_record_set_process_state_ready();
}

/**************************************************************************
 * function: rdx_record_process_is_busy_check
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
bool rdx_record_process_is_busy_check(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(record_status.process_state == REC_PROCESS_STATE_BUSY){
        //start timer to change process state.
        if(record_set_process_state_timer == 0){
            record_set_process_state_timer = rdx_os_timer_add(rdx_record_process_state_set_timer_cb, (void *)0, RDX_RECORD_STATE_BUSY_TIMEOUT);
            log_info("%s --> record_set_process_state_timer = %d \r", __FUNCTION__, record_set_process_state_timer);
        }else{
            //if timer is running, do not set again.
            log_info("%s --> record_set_process_state_timer is running, do not set again \r", __FUNCTION__);
        }
        return TRUE;
    }else{
        log_info("%s --> record_status.process_state = %d \r", __FUNCTION__, record_status.process_state);
        return FALSE;
    }
}

/**************************************************************************
 * function: rdx_record_set_process_state_busy
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_record_set_process_state_busy(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    log_info("%s --> record_status.process_state = %d \r", __FUNCTION__, record_status.process_state);
    if(record_status.process_state != REC_PROCESS_STATE_BUSY){
        record_status.process_state = REC_PROCESS_STATE_BUSY;
    }
}

/**************************************************************************
 * function: rdx_record_set_process_state_ready
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_record_set_process_state_ready(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    // log_info("%s --> in record_status.process_state = %d \r", __FUNCTION__, record_status.process_state);
    if(record_status.process_state != REC_PROCESS_STATE_READY){
        record_status.process_state = REC_PROCESS_STATE_READY;
        log_info("%s --> record_status.process_state = %d \r", __FUNCTION__, record_status.process_state);
    }
    if(record_set_process_state_timer){
        rdx_record_process_state_timer_stop();
    }
    rdx_record_pending_busy_replay_schedule();
}

/**************************************************************************
 * function: rdx_record_set_default
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_record_set_default(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(record_set_process_state_timer){
        rdx_os_timer_del(record_set_process_state_timer);
        record_set_process_state_timer = 0;
    }
    if(g_pending_busy_replay_timer){
        rdx_os_timer_del(g_pending_busy_replay_timer);
        g_pending_busy_replay_timer = 0;
    }
    if(g_record_cmd_delay_timer){
        rdx_os_timer_del(g_record_cmd_delay_timer);
        g_record_cmd_delay_timer = 0;
    }

    memset(&record_status, 0, sizeof(RecordStatus));
    record_status.run = RECORD_STATE_STOP;
    record_status.formate = RECORD_FORMATE_OPUS_16K_STERO;
    record_status.scene = RECORD_SCENE_CHAT; //for chat.
    record_status.orig_scene = RECORD_SCENE_CHAT;
    record_status.process_state = REC_PROCESS_STATE_READY;
    record_status.begin_time = 0;
    record_status.ui_notify = rdx_record_ui_notify;
    record_status.stream_discont = false;
    /* V24: 暂停统计 / 录音标记缓冲在每次复位时一并清零，避免跨会话残留 */
    record_status.pause_start_ms = 0;
    record_status.paused_accumulated_ms = 0;
    s_cur_mark_count = 0;
    memset(s_cur_marks, 0, sizeof(s_cur_marks));
    memset(&g_pending_busy_record_info, 0, sizeof(g_pending_busy_record_info));
    memset(&g_pending_record_info, 0, sizeof(g_pending_record_info));
    g_pending_busy_record_valid = false;
    g_pending_busy_replay_posted = false;
    g_record_cmd_retry_cnt = 0;
}

/**************************************************************************
 * V24: 录音标记 / 录音活跃偏移
 *   - rdx_record_get_active_offset_ms: 当前录音时长 (ms), 扣除 PAUSE 累计
 *   - rdx_record_clear_marks: 清空标记 + pause 累计
 **************************************************************************/
u32 rdx_record_get_active_offset_ms(void)
{
    u32 now;
    u32 paused;

    if(record_status.begin_time == 0){
        return 0;
    }
    now = (u32)jiffies_msec();
    paused = record_status.paused_accumulated_ms;
    if(record_status.run == RECORD_STATE_PAUSE && record_status.pause_start_ms > 0){
        paused += (u32)(now - record_status.pause_start_ms);
    }
    if(now <= record_status.begin_time + paused){
        return 0;
    }
    return (u32)(now - record_status.begin_time - paused);
}

void rdx_record_clear_marks(void)
{
    s_cur_mark_count = 0;
    memset(s_cur_marks, 0, sizeof(s_cur_marks));
    record_status.pause_start_ms = 0;
    record_status.paused_accumulated_ms = 0;
}

/**************************************************************************
 * function: rdx_record_get_marks
 * description: 导出当前会话累计的 mark offset_ms 到外部数组, 供 dat_save
 *              落地到 dat_entry_t.marks 用.
 * param out  - 输出缓冲区 (调用方分配, 至少 max 个 u32)
 * param max  - 缓冲区可容纳的最大数量
 * return     - 实际拷贝出的 mark 数 (<= max, 也 <= 当前 s_cur_mark_count)
 *
 * NOTE:
 *   - 不修改全局 s_cur_marks / s_cur_mark_count, 仅"读出"
 *   - 调用方典型流程: dat_save -> get_marks -> clear_marks
 *   - 不是 ISR 安全 (s_cur_marks 写在 add_mark / clear_marks 路径)
 **************************************************************************/
u8 rdx_record_get_marks(u32 *out, u8 max)
{
    u8 n;
    if(!out || max == 0) return 0;
    n = (s_cur_mark_count < max) ? s_cur_mark_count : max;
    if(n > 0){
        memcpy(out, s_cur_marks, n * sizeof(u32));
    }
    return n;
}

int rdx_record_add_mark(u8 source)
{
#if defined(__UUX_FILE__)
    uxfile_data_t* fi = rdx_uxfile_get_operateFile_info();
    u32 sn = fi ? fi->sn : 0;
    const char* fname = (fi && fi->filename[0]) ? fi->filename : "";
#else
    u32 sn = 0;
    const char* fname = "";
#endif
    u32 offset_ms;

    /* 仅 START / RESUME 接受标记；PAUSE/STOP 返回 result=1（BAD_STATE） */
    if(record_status.run != RECORD_STATE_START && record_status.run != RECORD_STATE_RESUME){
        r_printf("[RECMARK] reject: not in START/RESUME (run=%d) \r", record_status.run);
        rdx_protocol_record_mark_indicate(RDX_RECMARK_RESULT_BAD_STATE,
                                          0, "", 0, 0, source);
        return RDX_RECMARK_RESULT_BAD_STATE;
    }

    offset_ms = rdx_record_get_active_offset_ms();

    /* 标记数已达上限 → 拒绝 */
    if(s_cur_mark_count >= RDX_RECORD_MARK_MAX){
        r_printf("[RECMARK] full: count=%u \r", s_cur_mark_count);
        rdx_protocol_record_mark_indicate(RDX_RECMARK_RESULT_FULL,
                                          0, "", 0, 0, source);
        return RDX_RECMARK_RESULT_FULL;
    }

    /* 6 秒去重窗口：避免按键抖动 / APP+按键并发触发同一标记 */
    if(s_cur_mark_count > 0){
        u32 last = s_cur_marks[s_cur_mark_count - 1];
        if(offset_ms >= last && (offset_ms - last) < RDX_RECORD_MARK_DEDUP_WINDOW_MS){
            r_printf("[RECMARK] dedup: cur=%lums last=%lums (win=%ums) \r",
                     (unsigned long)offset_ms, (unsigned long)last,
                     RDX_RECORD_MARK_DEDUP_WINDOW_MS);
            rdx_protocol_record_mark_indicate(RDX_RECMARK_RESULT_BUSY,
                                              0, "", 0, 0, source);
            return RDX_RECMARK_RESULT_BUSY;
        }
    }

    s_cur_marks[s_cur_mark_count++] = offset_ms;

    y_printf("[RECMARK] added: sn=%lu, name=%s, idx=%u, off=%lums, src=%u \r",
             (unsigned long)sn, fname,
             s_cur_mark_count, (unsigned long)offset_ms, source);
    rdx_protocol_record_mark_indicate(RDX_RECMARK_RESULT_OK, sn, fname,
                                      s_cur_mark_count, offset_ms, source);

#if (RDX_SUPPORT_MOTOR == 1)
    /* 打标成功 → 马达单次震动反馈 */
    rdx_record_motor_run();
#endif

    return RDX_RECMARK_RESULT_OK;
}

/**************************************************************************
 * function: rdx_record_init
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_record_init(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    // y_printf("====== %s \r", __FUNCTION__);
    rdx_record_set_default();

    motor_twice_step = 0;
}

/**************************************************************************
 * function: rdx_record_get_status
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
RecordStatus* rdx_record_get_status(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    return &record_status;
}

#if (RDX_SUPPORT_MOTOR == 1)

/**************************************************************************
 * function: rdx_record_motor_run
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_record_motor_run(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    motor_run_by_time(RECORD_MOTOR_VIB_DURATION);
    y_printf("===> %s ...\r", __FUNCTION__);
}

/**************************************************************************
 * function: motor_twice_callback
 * description: 电机两次震动的回调函数
 * param (void*) param
 * return (*)
 **************************************************************************/
static void motor_twice_callback(void* param)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    switch(motor_twice_step) {
        case 0:
            motor_off();
            motor_twice_step = 1;
            rdx_os_timer_add(motor_twice_callback, (void *)0, RECORD_MOTOR_TWICE_OFF);
            break;
            
        case 1:
            motor_on();
            motor_twice_step = 2;
            rdx_os_timer_add(motor_twice_callback, (void *)0, RECORD_MOTOR_TWICE_ON);
            break;
            
        case 2:
            motor_off();
            motor_twice_step = 0; // 重置步骤计数器
            break;
    }
}

/**************************************************************************
 * function: rdx_record_motor_twice
 * description: 使用定时器实现电机两次震动
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_record_motor_twice(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    y_printf("%s ...\r", __FUNCTION__);
    
    // 重置步骤并开启电机
    motor_twice_step = 0;
    motor_on();
    
    // 启动第一步定时器（关闭电机）
    rdx_os_timer_add(motor_twice_callback, (void *)0, RECORD_MOTOR_TWICE_ON);
}

#endif

static void rdx_record_cmd_delay_cb(void *priv)
{
    extern u8 rdx_ble_server_is_stream_tx_ready(void);

    g_record_cmd_delay_timer = 0;

    if(!rdx_ble_server_is_stream_tx_ready()) {
        g_record_cmd_retry_cnt++;
        if(g_record_cmd_retry_cnt < RECORD_CMD_MAX_RETRY) {
            r_printf("[REC_DELAY] stream_tx_ready=0, retry %d/%d\r", 
                     g_record_cmd_retry_cnt, RECORD_CMD_MAX_RETRY);
            g_record_cmd_delay_timer = rdx_os_timer_add(rdx_record_cmd_delay_cb, NULL, RECORD_CMD_DELAY_MS);
            return;
        } else {
            r_printf("[REC_DELAY] Max retry reached, abort record cmd!\r");
            g_record_cmd_retry_cnt = 0;
            return;
        }
    }

    y_printf("[REC_DELAY] stream_tx_ready=1, processing record cmd now\r");
    g_record_cmd_retry_cnt = 0;
    rdx_record_cmd_handle(&g_pending_record_info);
}

/**************************************************************************
 * function: rdx_record_cmd_handle
 * description: 
 * param (Record_info) *r_info
 * return (*)
 **************************************************************************/
void rdx_record_cmd_handle(Record_info *r_info)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    uint8_t info_type;
    bool was_busy = false;
    bool pending_needs_schedule = false;
    bool pending_replay_active = false;

    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    extern u8 rdx_ble_server_is_stream_tx_ready(void);
    if(!r_info){
        return;
    }
    info_type = r_info->type - 0x30;
    y_printf("------ %s, r_info->cmd = %c, r_info->formate = %c, r_info->type = %c \r", __FUNCTION__, r_info->cmd, r_info->formate, r_info->type);

    /* Pending replacement and BUSY admission are one atomic decision:
     * whichever command arrives last owns the single pending slot. */
    CPU_CRITICAL_ENTER();
    if(g_pending_busy_replay_posted ||
       (g_pending_busy_record_valid &&
        record_status.process_state == REC_PROCESS_STATE_READY)){
        memcpy(&g_pending_busy_record_info, r_info, sizeof(Record_info));
        g_pending_busy_record_valid = true;
        pending_replay_active = true;
        pending_needs_schedule = !g_pending_busy_replay_posted;
    }else if(record_status.process_state == REC_PROCESS_STATE_BUSY){
        memcpy(&g_pending_busy_record_info, r_info, sizeof(Record_info));
        g_pending_busy_record_valid = true;
        was_busy = true;
    }
    CPU_CRITICAL_EXIT();

    if(pending_replay_active){
        if(pending_needs_schedule){
            rdx_record_pending_busy_replay_schedule();
        }
        y_printf("[REC_BUSY] replay queued; replace with latest cmd=%c\r", r_info->cmd);
        return;
    }

    if(was_busy){
        rdx_record_process_is_busy_check();
        r_printf("====== %s --> record process busy, queue latest cmd=%c \r",
                 __FUNCTION__, r_info->cmd);
        return;
    }

    if(r_info->cmd == (RECORD_STATE_START + 0x30)) {
        if(!rdx_ble_server_is_stream_tx_ready()) {
            if(g_record_cmd_delay_timer) {
                r_printf("[REC_DELAY] Already waiting, ignore duplicate cmd\r");
                return;
            }
            memcpy(&g_pending_record_info, r_info, sizeof(Record_info));
            g_record_cmd_retry_cnt = 0;
            r_printf("[REC_DELAY] stream_tx_ready=0, delay %dms\r", RECORD_CMD_DELAY_MS);
            g_record_cmd_delay_timer = rdx_os_timer_add(rdx_record_cmd_delay_cb, NULL, RECORD_CMD_DELAY_MS);
            return;
        }
    }

    if ((r_info->cmd == '0' || r_info->cmd == '2') &&
        !rdx_record_file_transfer_ready()) {
        r_printf("====== %s --> file transfer state unavailable \r", __func__);
        return;
    }

    if(r_info->cmd - 0x30 == record_status.run){
        //if the cmd is same as last time, do not handle it again.
        y_printf("====== %s --> record cmd job is same as current, cmd = %c \r", __FUNCTION__, r_info->cmd);
        return;
    }
    //set new record format and scene.d
    // OS_ENTER_CRITICAL();
    if(r_info->cmd == '0'){
        //do record start.
        // g_printf("====== %s --> record START", __FUNCTION__);
        record_status.run = RECORD_STATE_START;
        if(info_type == RECORD_SCENE_CHAT){
            record_status.formate = RECORD_FORMATE_OPUS_16K_STERO; 
            record_status.scene = RECORD_SCENE_CHAT;
        }else{
            record_status.formate = RECORD_FORMATE_OPUS_16K_STERO; 
            record_status.scene = RECORD_SCENE_CALL;
        }
        /* V24: 新会话 → 清空上次的 mark 缓冲 + pause 累计 (begin_time 由 run_init 写) */
        rdx_record_clear_marks();
        //for test.
        // rdx_tx_speed_cal_timer_start();
    }else if(r_info->cmd == '1'){
        //do record pause.
        // g_printf("====== %s --> record PAUSE", __FUNCTION__);
        /* V24: 记录 PAUSE 起始时刻，供 active_offset_ms 计算扣除暂停耗时 */
        if(record_status.run == RECORD_STATE_START || record_status.run == RECORD_STATE_RESUME){
            record_status.pause_start_ms = (u32)jiffies_msec();
        }
        record_status.run = RECORD_STATE_PAUSE;

        {
            u16 pause_con_hdl = rdx_ble_server_get_conn_handle();
            if(pause_con_hdl == 0xffff || pause_con_hdl == 0){
                rdx_record_pause_timeout_start();
            }else{
                rdx_record_pause_timeout_stop();
            }
        }
        //for test.
        // rdx_tx_speed_cal_timer_stop();
    }else if(r_info->cmd == '2'){
        //do record resume.
        // g_printf("====== %s --> record RESUME", __FUNCTION__);
        /* V24: 累计本次 PAUSE 耗时到 paused_accumulated_ms，再清 pause_start_ms */
        if(record_status.run == RECORD_STATE_PAUSE && record_status.pause_start_ms > 0){
            u32 now = (u32)jiffies_msec();
            if(now > record_status.pause_start_ms){
                record_status.paused_accumulated_ms += (now - record_status.pause_start_ms);
            }
            record_status.pause_start_ms = 0;
        }
        rdx_record_pause_timeout_stop();
        record_status.run = RECORD_STATE_RESUME;
        if(record_status.scene == RECORD_SCENE_CHAT){
            record_status.formate = RECORD_FORMATE_OPUS_16K_STERO; 
            record_status.scene = RECORD_SCENE_CHAT;
        }else if(record_status.scene == RECORD_SCENE_CALL){
            record_status.formate = RECORD_FORMATE_OPUS_16K_STERO;
            record_status.scene = RECORD_SCENE_CALL;
        }
        //for test.
        // rdx_tx_speed_cal_timer_start();
    }else if(r_info->cmd == '3'){
        //stop record.
        // g_printf("====== %s --> record STOP", __FUNCTION__);
        rdx_record_pause_timeout_stop();
        record_status.run = RECORD_STATE_STOP;
        //for test.
        // rdx_tx_speed_cal_timer_stop();
    }else{
        r_printf("====== %s --> record cmd error, cmd = %c \r", __FUNCTION__, r_info->cmd);
        return;
    }

    rdx_app_emmc_poweron(0);
    
    //start record process.
    rdx_record_process();
}

/**************************************************************************
 * function: rdx_record_stop
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_record_stop(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    RecordStatus* rp = rdx_record_get_status();
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    // if(rp->run != RECORD_STATE_STOP){
        y_printf("====== %s --> record STOP \r", __FUNCTION__);
        rp->run = RECORD_STATE_STOP;
        rdx_record_process();
    // }
}

/**************************************************************************
 * function: rdx_record_start
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_record_start(void* priv)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    RecordStatus* rp = rdx_record_get_status();
    u16 con_hdl = rdx_ble_server_get_conn_handle();
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(rp->run == RECORD_STATE_STOP){
        if (!rdx_record_file_transfer_ready()) {
            r_printf("====== %s --> file transfer state unavailable \r", __func__);
            return;
        }
        //other para remain as last used.
        rp->run = RECORD_STATE_START;
        if(0xffff == con_hdl || 0 == con_hdl){
            rp->mode = RECORD_MODE_OFFLINE;
        }else{
            rp->mode = RECORD_MODE_ONLINE;
        }
        // rp->orig_mode = rp->mode;

        rdx_record_mode_active_check(0);
        rp->formate = RECORD_FORMATE_OPUS_16K_STERO; //背夹目前都是双声道
        rdx_record_process();
    }
}

void rdx_record_finish_tone_play(void)
{
    //dac open once.
    play_tone_file(get_tone_files()->power_off);

}

/**************************************************************************
 * function: rdx_record_ui_notify
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_record_ui_notify(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    RecordStatus* rp = rdx_record_get_status();
    bool in_dut = rdx_app_get_dut_status();
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    y_printf("====== %s --> rp->run: %d, record_status.noshow: %d \r", __FUNCTION__, rp->run, record_status.noshow);
    if(rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME){
        //record start.
        rdx_hook_led_set_scene(RDX_LED_SCENE_RECORD_START);
        if(rp->key_trigger == false){
            rp->key_trigger = true;
            //record start by app.
        #if (RDX_SUPPORT_MOTOR == 1)
            rdx_record_motor_run();
        #endif
        }
    }else if(rp->run == RECORD_STATE_PAUSE || rp->run == RECORD_STATE_STOP){
        //record pause or stop.
        if(record_status.noshow == 0){
            rdx_hook_led_set_scene(RDX_LED_SCENE_RECORD_STOP);
        }else{
            record_status.noshow = 0;
        }
    #if (RDX_SUPPORT_MOTOR == 1)
        //motor twice.
        rdx_record_motor_twice();
    #endif
        if (rdx_os_task_post_callback0("app_core", rdx_record_finish_tone_play) != RDX_OK) {
            log_info("%s record taskq post err \n", __func__);
        }
    }
    //reset trigger.
    if(rp->key_trigger == true){
        rp->key_trigger = false;
    }
}

#if RDX_RECORD_USE_LOCAL_PIPELINE

/**************************************************************************
 * function: rdx_record_auto_run
 * description: 
 * param (RecordStatus*) rp
 * return (*)
 **************************************************************************/
void rdx_record_auto_run(RecordStatus* rp)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    y_printf("====== %s --> rp->run = %d, rp->scene = %d \r", __FUNCTION__, rp->run, rp->scene);
    if(rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME){
        //record init.
        if (rdx_record_run_init() != 0) {
            rp->run = rp->run == RECORD_STATE_RESUME
                    ? RECORD_STATE_PAUSE
                    : RECORD_STATE_STOP;
            rdx_record_set_process_state_ready();
            return;
        }
        //do start or resume during record scene.
        if(rp->scene == RECORD_SCENE_CHAT){
            os_taskq_post_msg(RECORD_TASK_NAME, 2, rp->run, MIC_TO_MONO_OPUS);
            rdx_app_set_record_mode(RDX_RECORD_CHANNAL_DUAL);
        }else if(rp->scene == RECORD_SCENE_CALL){
            os_taskq_post_msg(RECORD_TASK_NAME, 2, rp->run, MIC_DAC_TO_STERO_OPUS);
            rdx_app_set_record_mode(RDX_RECORD_CHANNAL_DUAL);
        }
    }else{
        // //record exit.
        // rdx_record_run_exit();
        //do stop or pause.
        os_taskq_post_msg(RECORD_TASK_NAME, 1, rp->run);
    }
}

/**************************************************************************
 * function: rdx_record_process
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_record_process(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    bool in_dut = rdx_app_get_dut_status();
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    // y_printf("===== %s \r", __FUNCTION__);

    //check record process state.
    if(rdx_record_process_is_busy_check()){
        r_printf("====== %s --> record process change is busy, return \r", __FUNCTION__);
        return;
    }
    if (!rdx_record_file_transfer_admit(&record_status)) {
        return;
    }

    //set process state to busy.
    rdx_record_set_process_state_busy();

    switch(record_status.run){
        case RECORD_STATE_START:
            {
                g_printf("====== %s --> RECORD START \r", __FUNCTION__);

                rdx_app_record_state_upload_timer_stop();

            #if (TCFG_USER_TWS_ENABLE && TCFG_APP_BT_EN)     
                //tws stop role switch.
                tws_api_auto_role_switch_disable();
                tws_play_tone_file(get_tone_files()->record_on, 400);   
            #endif

                //LED控制：录音时呼吸灯
                rdx_hook_led_set_scene(RDX_LED_SCENE_RECORD_START);

                // if(record_status.scene == RECORD_SCENE_CHAT){
                //     os_taskq_post_msg(RECORD_TASK_NAME, 2, record_status.run, MIC_TO_MONO_OPUS);      
                // }else if(record_status.scene == RECORD_SCENE_CALL){
                //     os_taskq_post_msg(RECORD_TASK_NAME, 2, record_status.run, MIC_DAC_TO_STERO_OPUS); 
                // }
            #ifdef RECORD_HEARTBEAT_SUPPORT
                //start timer to check record is running well.
                rdx_record_keep_alive_check_start();
            #endif

                //disbale shutdown timer.
                rdx_ble_server_auto_shut_down_enable(0);
            }
            break;

        case RECORD_STATE_RESUME:
            {
                g_printf("====== %s --> RECORD RESUME \r", __FUNCTION__);

                rdx_app_record_state_upload_timer_stop();

            #if (TCFG_USER_TWS_ENABLE && TCFG_APP_BT_EN) 
                //tws stop role switch.
                tws_api_auto_role_switch_disable();
            #endif

                //LED控制：录音时呼吸灯
                rdx_hook_led_set_scene(RDX_LED_SCENE_RECORD_START);

                // if(record_status.scene == RECORD_SCENE_CHAT){
                //     os_taskq_post_msg(RECORD_TASK_NAME, 2, record_status.run, MIC_TO_MONO_OPUS);  //MIC_TO_MONO_OPUS
                // }
                // else if(record_status.scene == RECORD_SCENE_CALL){
                //     os_taskq_post_msg(RECORD_TASK_NAME, 2, record_status.run, MIC_DAC_TO_STERO_OPUS); 
                // }
            #ifdef RECORD_HEARTBEAT_SUPPORT
                //start timer to check record is running well.
                rdx_record_keep_alive_check_start();
            #endif
                //disbale shutdown timer.
                rdx_ble_server_auto_shut_down_enable(0);
            }
            break;

        case RECORD_STATE_PAUSE:
            {
                g_printf("====== %s --> RECORD PAUSE \r", __FUNCTION__);
                rdx_record_set_filter_cnt(0);

            #ifdef RECORD_HEARTBEAT_SUPPORT
                //stop record alive check timer.
                rdx_record_keep_alive_check_stop();
            #endif
            #if (TCFG_USER_TWS_ENABLE && TCFG_APP_BT_EN) 
                //tws stop role switch.
                tws_api_auto_role_switch_disable();
            #endif
                //close translation.
                // os_taskq_post_msg(RECORD_TASK_NAME, 1, record_status.run);

                //enable shutdown timer.
                rdx_ble_server_auto_shut_down_enable(1);
            }
            break;

        case RECORD_STATE_STOP:
            {
                g_printf("====== %s --> RECORD STOP \r", __FUNCTION__);
                rdx_record_set_filter_cnt(0);

            #if (TCFG_USER_TWS_ENABLE && TCFG_APP_BT_EN) 
                //play record tone.
                tws_play_tone_file(get_tone_files()->record_off, 400); 
            #endif
            #ifdef RECORD_HEARTBEAT_SUPPORT
                //stop record alive check timer.
                rdx_record_keep_alive_check_stop();
            #endif
                //close translation.
                // os_taskq_post_msg(RECORD_TASK_NAME, 1, record_status.run);

                //LED控制：录音结束后恢复系统灯效
                rdx_hook_led_set_scene(RDX_LED_SCENE_RECORD_STOP);

            #if (TCFG_USER_TWS_ENABLE && TCFG_APP_BT_EN) 
                //tws start role switch.
                tws_api_auto_role_switch_enable();
            #endif

                //enable shutdown timer.
                rdx_ble_server_auto_shut_down_enable(1);
            }
            break;
        
        default:
            log_info("====== %s --> RECORD state error! \r", __FUNCTION__);
            rdx_record_set_process_state_ready();
            return;
    }

    //auto jugde record channal and start record.
    rdx_record_auto_run(&record_status);
    
#if (TCFG_USER_TWS_ENABLE && TCFG_APP_BT_EN) 
    //syn status to tws.
    extern void rdx_app_tws_record_state_sync(void);
    rdx_app_tws_record_state_sync();
#endif
}

#else

/**************************************************************************
 * function: rdx_record_process
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_record_process(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    bool in_dut = rdx_app_get_dut_status();
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    // y_printf("===== %s \r", __FUNCTION__);

    //check record process state.
    if(rdx_record_process_is_busy_check()){
        r_printf("====== %s --> record process change is busy, return \r", __FUNCTION__);
        return;
    }
    if (!rdx_record_file_transfer_admit(&record_status)) {
        return;
    }
    //send record state to app.
    u16 con_hdl = rdx_ble_server_get_conn_handle();
    if(con_hdl != 0xffff && con_hdl != 0){
        //send state to app.
        if (rdx_os_task_post_callback0("app_core", rdx_protocol_record_state_indicate) != RDX_OK) {
            log_info("%s record taskq post err \n", __func__);
        }
    }

    //set process state to busy.
    rdx_record_set_process_state_busy();

    switch(record_status.run){
        case RECORD_STATE_START:
            {
                g_printf("====== %s --> RECORD START \r", __FUNCTION__);

                rdx_app_record_state_upload_timer_stop();

            #if (TCFG_USER_TWS_ENABLE && TCFG_APP_BT_EN)     
                //tws stop role switch.
                tws_api_auto_role_switch_disable();
                tws_play_tone_file(get_tone_files()->record_on, 400);   
            #endif

                if(record_status.scene == RECORD_SCENE_CHAT){
                    os_taskq_post_msg(RECORD_TASK_NAME, 2, record_status.run, MIC_TO_MONO_OPUS);      
                }else if(record_status.scene == RECORD_SCENE_CALL){
                    os_taskq_post_msg(RECORD_TASK_NAME, 2, record_status.run, MIC_DAC_TO_STERO_OPUS); 
                }
            #ifdef RECORD_HEARTBEAT_SUPPORT
                //start timer to check record is running well.
                rdx_record_keep_alive_check_start();
            #endif

                //disbale shutdown timer.
                rdx_ble_server_auto_shut_down_enable(0);
            }
            break;

        case RECORD_STATE_RESUME:
            {
                g_printf("====== %s --> RECORD RESUME \r", __FUNCTION__);

                rdx_app_record_state_upload_timer_stop();

            #if (TCFG_USER_TWS_ENABLE && TCFG_APP_BT_EN) 
                //tws stop role switch.
                tws_api_auto_role_switch_disable();
            #endif

                if(record_status.scene == RECORD_SCENE_CHAT){
                    os_taskq_post_msg(RECORD_TASK_NAME, 2, record_status.run, MIC_TO_MONO_OPUS);  //MIC_TO_MONO_OPUS
                }
                else if(record_status.scene == RECORD_SCENE_CALL){
                    os_taskq_post_msg(RECORD_TASK_NAME, 2, record_status.run, MIC_DAC_TO_STERO_OPUS); 
                }
            #ifdef RECORD_HEARTBEAT_SUPPORT
                //start timer to check record is running well.
                rdx_record_keep_alive_check_start();
            #endif
                //disbale shutdown timer.
                rdx_ble_server_auto_shut_down_enable(0);
            }
            break;

        case RECORD_STATE_PAUSE:
            {
                g_printf("====== %s --> RECORD PAUSE \r", __FUNCTION__);
                rdx_protocol_uploadFileInfo_clean();

                rdx_record_set_filter_cnt(0);

            #ifdef RECORD_HEARTBEAT_SUPPORT
                //stop record alive check timer.
                rdx_record_keep_alive_check_stop();
            #endif
            #if (TCFG_USER_TWS_ENABLE && TCFG_APP_BT_EN) 
                //tws stop role switch.
                tws_api_auto_role_switch_disable();
            #endif
                //close translation.
                os_taskq_post_msg(RECORD_TASK_NAME, 1, record_status.run);

                //enable shutdown timer.
                rdx_ble_server_auto_shut_down_enable(1);
            }
            break;

        case RECORD_STATE_STOP:
            {
                g_printf("====== %s --> RECORD STOP \r", __FUNCTION__);
                rdx_protocol_uploadFileInfo_clean();

                rdx_record_set_filter_cnt(0);

            #if (TCFG_USER_TWS_ENABLE && TCFG_APP_BT_EN) 
                //play record tone.
                tws_play_tone_file(get_tone_files()->record_off, 400); 
            #endif
            #ifdef RECORD_HEARTBEAT_SUPPORT
                //stop record alive check timer.
                rdx_record_keep_alive_check_stop();
            #endif
                //close translation.
                os_taskq_post_msg(RECORD_TASK_NAME, 1, record_status.run);

            #if (TCFG_USER_TWS_ENABLE && TCFG_APP_BT_EN) 
                //tws start role switch.
                tws_api_auto_role_switch_enable();
            #endif

                //enable shutdown timer.
                rdx_ble_server_auto_shut_down_enable(1);
            }
            break;
        
        default:
            log_info("====== %s --> RECORD state error! \r", __FUNCTION__);
            rdx_record_set_process_state_ready();
            return;
    }

#if (TCFG_USER_TWS_ENABLE && TCFG_APP_BT_EN) 
    //syn status to tws.
    extern void rdx_app_tws_record_state_sync(void);
    rdx_app_tws_record_state_sync();
#endif
}

#endif

/**************************************************************************
 * function: rdx_record_task
 * description: 
 * param (void) *arg
 * return (*)
 **************************************************************************/
static void rdx_record_task(void *arg)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    int ret;
    int msg[16];
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    log_info("rdx_record_task  \n");
    rdx_record_init();

    while(1){
        ret = os_taskq_pend(NULL, msg, ARRAY_SIZE(msg));
        if (ret == OS_TASKQ) {
            b_printf("%s : pRS->run = %d, pRS->scene = %d \r", __func__, msg[1], msg[2]);
            switch (msg[0]) {
				case Q_MSG:
					{
                        if(msg[1] == RECORD_STATE_START || msg[1] == RECORD_STATE_RESUME){
                            int open_ret = translation_ear_recoder_open_all(msg[2]);
                            if(open_ret != 0){
                                log_info("rdx_record_task: recorder open failed, ret=%d\r", open_ret);
                                rdx_record_set_process_state_ready();
                            }
                        }else if(msg[1] == RECORD_STATE_STOP || msg[1] == RECORD_STATE_PAUSE){
                            translation_ear_recoder_close_all();
                        }
					}
					break;

				default:
					break;
            }
        }
    }
}

/**************************************************************************
 * function: rdx_record_task_create
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
int rdx_record_task_create(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
 
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    if(is_record_task_created){
        r_printf("?????? rdx_record_task_create: task is created\r");
        return -1;
    }
    int err = os_task_create(rdx_record_task, NULL, 3, 512, 256, RECORD_TASK_NAME);
    if (err != OS_NO_ERR) {
        return -EINVAL;
    }

    is_record_task_created = true;

    y_printf("$$$$$$ rdx_record_task_create success!!!");
    
    return 0;
}

/**************************************************************************
 * function: rdx_record_task_free
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
int rdx_record_task_free(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(!is_record_task_created){
        r_printf("?????? task is not created \r");
        return -1;
    }
    os_task_del(RECORD_TASK_NAME);

    is_record_task_created = false;
    return 0;
}

//--------------------------------------------------------------------------------

static void rdx_record_mic_gain_fill_defaults(MicGainPara *gain)
{
    memset(gain, 0, sizeof(*gain));
    gain->chat_mic0_gain = RECORD_CHAT_GAIN_DEFAULT_MIC0;
    gain->chat_mic1_gain = RECORD_CHAT_GAIN_DEFAULT_MIC3;
    gain->call_mic0_gain = RECORD_CALL_GAIN_DEFAULT_MIC2;
    gain->call_mic1_gain = RECORD_CALL_GAIN_DEFAULT_MIC3;
}

static bool rdx_record_mic_gain_value_readable(bool configured, u8 value)
{
    return value <= RECORD_MIC_DB_VALUE_MAX ||
           (!configured && value == 0xff);
}

static bool rdx_record_mic_gain_values_readable(const MicGainPara *gain)
{
    return gain &&
           rdx_record_mic_gain_value_readable(gain->chat_mic_flag, gain->chat_mic0_gain) &&
           rdx_record_mic_gain_value_readable(gain->chat_mic_flag, gain->chat_mic1_gain) &&
           rdx_record_mic_gain_value_readable(gain->call_mic_flag, gain->call_mic0_gain) &&
           rdx_record_mic_gain_value_readable(gain->call_mic_flag, gain->call_mic1_gain);
}

static bool rdx_record_mic_gain_values_valid_for_write(const MicGainPara *gain)
{
    return gain &&
           gain->chat_mic0_gain <= RECORD_MIC_DB_VALUE_MAX &&
           gain->chat_mic1_gain <= RECORD_MIC_DB_VALUE_MAX &&
           gain->call_mic0_gain <= RECORD_MIC_DB_VALUE_MAX &&
           gain->call_mic1_gain <= RECORD_MIC_DB_VALUE_MAX;
}

static u8 rdx_record_mic_gain_legacy_value(u8 mic_index, u8 fallback,
                                           bool use_adc_value)
{
    u8 value;

    if(use_adc_value &&
       rdx_audio_adc_file_get_gain_checked(mic_index, &value) == 0){
        return value;
    }
    return fallback;
}

static bool rdx_record_mic_gain_normalize_legacy(MicGainPara *gain,
                                                  int active_mode)
{
    bool changed = false;
    bool chat_changed = false;
    bool call_changed = false;

    if(!gain){
        return false;
    }

    /* 0 is a legal configured gain.  It is a legacy sentinel only while
     * the corresponding mode has never been configured (flag == false). */
    if(!gain->chat_mic_flag){
        if(gain->chat_mic0_gain == 0 || gain->chat_mic0_gain == 0xff){
            gain->chat_mic0_gain = rdx_record_mic_gain_legacy_value(
                RECORD_MIC_0, RECORD_CHAT_GAIN_DEFAULT_MIC0,
                active_mode == RDX_RECORD_MIC_MODE_CHAT);
            changed = true;
            chat_changed = true;
        }
        if(gain->chat_mic1_gain == 0 || gain->chat_mic1_gain == 0xff){
            gain->chat_mic1_gain = rdx_record_mic_gain_legacy_value(
                RECORD_MIC_3, RECORD_CHAT_GAIN_DEFAULT_MIC3,
                active_mode == RDX_RECORD_MIC_MODE_CHAT);
            changed = true;
            chat_changed = true;
        }
    }
    if(!gain->call_mic_flag){
        if(gain->call_mic0_gain == 0 || gain->call_mic0_gain == 0xff){
            gain->call_mic0_gain = rdx_record_mic_gain_legacy_value(
                RECORD_MIC_2, RECORD_CALL_GAIN_DEFAULT_MIC2,
                active_mode == RDX_RECORD_MIC_MODE_CALL);
            changed = true;
            call_changed = true;
        }
        if(gain->call_mic1_gain == 0 || gain->call_mic1_gain == 0xff){
            gain->call_mic1_gain = rdx_record_mic_gain_legacy_value(
                RECORD_MIC_3, RECORD_CALL_GAIN_DEFAULT_MIC3,
                active_mode == RDX_RECORD_MIC_MODE_CALL);
            changed = true;
            call_changed = true;
        }
    }

    /* A migrated mode is now initialized.  Persist its flag together with
     * the normalized values in the caller's single verified VM write. */
    if(chat_changed){
        gain->chat_mic_flag = true;
    }
    if(call_changed){
        gain->call_mic_flag = true;
    }

    return changed;
}


/**************************************************************************
 * function: rdx_record_mic_gain_read_from_vm
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
MicGainPara* rdx_record_mic_gain_read_from_vm(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    rdx_err_t ret;
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    ret = rdx_storage_read(RDX_STORAGE_KEY_MIC_GAIN,
                           (u8 *)&mic_gain, sizeof(MicGainPara));
    if (ret == RDX_OK && rdx_record_mic_gain_values_readable(&mic_gain)) {
        y_printf("===> current VM mic gain: chat=%d/%d call=%d/%d \r",
                 mic_gain.chat_mic0_gain, mic_gain.chat_mic1_gain,
                 mic_gain.call_mic0_gain, mic_gain.call_mic1_gain);
        return &mic_gain;
    }
    log_info("rdx_record_mic_gain_read_from_vm fail, err=%d\r", ret);
    return NULL;
}

/**************************************************************************
 * function: rdx_record_mic_gain_write_into_vm
 * description: 
 * param (MicGainPara*) gain
 * return (*)
 **************************************************************************/
int rdx_record_mic_gain_write_into_vm(MicGainPara* gain)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    rdx_err_t ret;
    rdx_err_t read_ret;
    MicGainPara verify;
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(!rdx_record_mic_gain_values_valid_for_write(gain)){
        log_info("rdx_record_mic_gain_write_into_vm invalid data\r");
        return -1;
    }

    log_info("===> %s \r", __func__);
    ret = rdx_storage_write(RDX_STORAGE_KEY_MIC_GAIN,
                            (const u8 *)gain, sizeof(MicGainPara));
    if (ret != RDX_OK) {
        log_info("rdx_record_mic_gain_write_into_vm fail, err=%d\r", ret);
        return -1;
    }

    memset(&verify, 0, sizeof(verify));
    read_ret = rdx_storage_read(RDX_STORAGE_KEY_MIC_GAIN,
                                (u8 *)&verify, sizeof(verify));
    if(read_ret != RDX_OK || memcmp(&verify, gain, sizeof(verify)) != 0){
        log_info("rdx_record_mic_gain_write_into_vm verify fail, err=%d\r", read_ret);
        return -1;
    }

    memcpy(&mic_gain, &verify, sizeof(mic_gain));
    log_info("mic gain stored and verified: chat=%d/%d call=%d/%d\r",
             verify.chat_mic0_gain, verify.chat_mic1_gain,
             verify.call_mic0_gain, verify.call_mic1_gain);
    return sizeof(MicGainPara);
}

static int rdx_record_mic_gain_apply(int mode, const MicGainPara *gain)
{
    int ret1;
    int ret2;

    if(!gain){
        return -1;
    }

    if(mode == RDX_RECORD_MIC_MODE_CHAT){
        ret1 = rdx_audio_adc_file_set_gain_checked(RECORD_MIC_0, gain->chat_mic0_gain);
        ret2 = rdx_audio_adc_file_set_gain_checked(RECORD_MIC_3, gain->chat_mic1_gain);
    }else if(mode == RDX_RECORD_MIC_MODE_CALL){
        ret1 = rdx_audio_adc_file_set_gain_checked(RECORD_MIC_2, gain->call_mic0_gain);
        ret2 = rdx_audio_adc_file_set_gain_checked(RECORD_MIC_3, gain->call_mic1_gain);
    }else{
        return -1;
    }

    return (ret1 == 0 && ret2 == 0) ? 0 : -1;
}

/**************************************************************************
 * function: rdx_record_mic_gain_query
 * description: 见 rdx_record.h 注释 (业务封装供 app 协议事件层调用)
 **************************************************************************/
int rdx_record_mic_gain_query(int mode, int* gain1, int* gain2)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    MicGainPara* pn = NULL;
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(!gain1 || !gain2) return -1;
    *gain1 = 0;
    *gain2 = 0;

    pn = rdx_record_mic_gain_read_from_vm();
    if(!pn) return -1;

    if(mode == RDX_RECORD_MIC_MODE_CHAT){
        *gain1 = pn->chat_mic0_gain;
        *gain2 = pn->chat_mic1_gain;
    }else if(mode == RDX_RECORD_MIC_MODE_CALL){
        *gain1 = pn->call_mic0_gain;
        *gain2 = pn->call_mic1_gain;
    }else{
        r_printf("%s --> bad mode=%d \r", __func__, mode);
        return -1;
    }
    return 0;
}

/**************************************************************************
 * function: rdx_record_mic_gain_set
 * description: 见 rdx_record.h 注释 (业务封装供 app 协议事件层调用)
 **************************************************************************/
int rdx_record_mic_gain_set(int mode, int* gain1, int* gain2)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    MicGainPara  gain_para;
    MicGainPara* pn = NULL;
    u8 hit = 0;
    int ret = 0;
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(!gain1 || !gain2) return -1;

    pn = rdx_record_mic_gain_read_from_vm();
    if(pn){
        memcpy(&gain_para, pn, sizeof(MicGainPara));
        rdx_record_mic_gain_normalize_legacy(&gain_para, -1);
    }else{
        rdx_record_mic_gain_fill_defaults(&gain_para);
    }

    if(mode != RDX_RECORD_MIC_MODE_CHAT && mode != RDX_RECORD_MIC_MODE_CALL){
        r_printf("%s --> bad mode=%d \r", __func__, mode);
        *gain1 = 0;
        *gain2 = 0;
        return -1;
    }

    if(mode == RDX_RECORD_MIC_MODE_CHAT){
        if(*gain1 >= RECORD_MIC_DB_VALUE_MIN && *gain1 <= RECORD_MIC_DB_VALUE_MAX){
            gain_para.chat_mic0_gain = (u8)(*gain1); hit++;
        }
        if(*gain2 >= RECORD_MIC_DB_VALUE_MIN && *gain2 <= RECORD_MIC_DB_VALUE_MAX){
            gain_para.chat_mic1_gain = (u8)(*gain2); hit++;
        }
        if(hit == 2) gain_para.chat_mic_flag = true;
    }else if(mode == RDX_RECORD_MIC_MODE_CALL){
        if(*gain1 >= RECORD_MIC_DB_VALUE_MIN && *gain1 <= RECORD_MIC_DB_VALUE_MAX){
            gain_para.call_mic0_gain = (u8)(*gain1); hit++;
        }
        if(*gain2 >= RECORD_MIC_DB_VALUE_MIN && *gain2 <= RECORD_MIC_DB_VALUE_MAX){
            gain_para.call_mic1_gain = (u8)(*gain2); hit++;
        }
        if(hit == 2) gain_para.call_mic_flag = true;
    }else{
        r_printf("%s --> bad mode=%d \r", __func__, mode);
        *gain1 = 0;
        *gain2 = 0;
        return -1;
    }

    ret = rdx_record_mic_gain_write_into_vm(&gain_para);
    if(ret > 0){
        if((record_status.run == RECORD_STATE_START || record_status.run == RECORD_STATE_RESUME) &&
           ((mode == RDX_RECORD_MIC_MODE_CHAT && record_status.scene == RECORD_SCENE_CHAT) ||
            (mode == RDX_RECORD_MIC_MODE_CALL && record_status.scene == RECORD_SCENE_CALL)) &&
           rdx_record_mic_gain_apply(mode, &gain_para) != 0){
            log_info("rdx_record_mic_gain_set: ADC not ready, defer apply until record start\r");
        }
        return 0;
    }

    /* 写失败: 把 VM 中实际的当前值回填给调用方, 便于打 ack 包 */
    MicGainPara* prn = rdx_record_mic_gain_read_from_vm();
    if(prn){
        if(mode == RDX_RECORD_MIC_MODE_CHAT){
            *gain1 = prn->chat_mic0_gain;
            *gain2 = prn->chat_mic1_gain;
        }else{
            *gain1 = prn->call_mic0_gain;
            *gain2 = prn->call_mic1_gain;
        }
    }else{
        *gain1 = 0;
        *gain2 = 0;
    }
    return -1;
}

/**************************************************************************
 * function: rdx_record_mic_gain_set_default
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_record_mic_gain_set_default(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    MicGainPara gain_para;
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    rdx_record_mic_gain_fill_defaults(&gain_para);
    rdx_record_mic_gain_write_into_vm(&gain_para);    
}

/**************************************************************************
 * function: rdx_record_mic_gain_check
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
static int rdx_record_mic_gain_apply_current(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    RecordStatus* rp = rdx_record_get_status();  
    MicGainPara* p;
    MicGainPara defaults;
    int mode;
    int actual1;
    int actual2;
    int expected1;
    int expected2;
    u8 actual_gain1;
    u8 actual_gain2;
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    mode = (rp->scene == RECORD_SCENE_CHAT) ?
           RDX_RECORD_MIC_MODE_CHAT : RDX_RECORD_MIC_MODE_CALL;

    p = rdx_record_mic_gain_read_from_vm();
    if(!p){
        rdx_record_mic_gain_fill_defaults(&defaults);
        if(rdx_record_mic_gain_write_into_vm(&defaults) <= 0){
            log_info("rdx_record_mic_gain_check: use runtime defaults; VM repair failed\r");
            p = &defaults;
        }else{
            p = &mic_gain;
        }
    }else if(rdx_record_mic_gain_normalize_legacy(p, mode)){
        if(rdx_record_mic_gain_write_into_vm(p) <= 0){
            log_info("rdx_record_mic_gain_check: legacy gain repair persist failed\r");
        }
    }

    if(rdx_record_mic_gain_apply(mode, p) != 0){
        log_info("rdx_record_mic_gain_check: ADC apply failed, mode=%d\r", mode);
        return -1;
    }

    if(mode == RDX_RECORD_MIC_MODE_CHAT){
        expected1 = p->chat_mic0_gain;
        expected2 = p->chat_mic1_gain;
        if(rdx_audio_adc_file_get_gain_checked(RECORD_MIC_0, &actual_gain1) != 0 ||
           rdx_audio_adc_file_get_gain_checked(RECORD_MIC_3, &actual_gain2) != 0){
            return -1;
        }
    }else{
        expected1 = p->call_mic0_gain;
        expected2 = p->call_mic1_gain;
        if(rdx_audio_adc_file_get_gain_checked(RECORD_MIC_2, &actual_gain1) != 0 ||
           rdx_audio_adc_file_get_gain_checked(RECORD_MIC_3, &actual_gain2) != 0){
            return -1;
        }
    }

    actual1 = actual_gain1;
    actual2 = actual_gain2;

    if(actual1 != expected1 || actual2 != expected2){
        log_info("rdx_record_mic_gain_check verify fail, mode=%d expected=%d/%d actual=%d/%d\r",
                 mode, expected1, expected2, actual1, actual2);
        return -1;
    }

    y_printf("===> mic gain applied, mode=%d gain=%d/%d\r", mode, actual1, actual2);
    return 0;
}

void rdx_record_mic_gain_check(void)
{
    if(rdx_record_mic_gain_apply_current() != 0){
        log_info("rdx_record_mic_gain_check: apply/verify failed; recording continues\r");
    }
}

/**************************************************************************
 * function: rdx_record_max_time_deal
 * description: 
 * param (void) *p
 * return (*)
 **************************************************************************/
void rdx_record_max_time_deal(void *p)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(rdx_record_max_timer){
        rdx_os_timer_del(rdx_record_max_timer);
        rdx_record_max_timer = 0;
    }
    rdx_record_stop();
}

/**************************************************************************
 * function: rdx_record_max_timer_stop
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_record_max_timer_stop(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(rdx_record_max_timer){
        rdx_os_timer_del(rdx_record_max_timer);
        rdx_record_max_timer = 0;
    }
}

/**************************************************************************
 * function: rdx_record_max_timer_start
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_record_max_timer_start(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(rdx_record_max_timer == 0){
        rdx_record_max_timer = rdx_os_timer_add(rdx_record_max_time_deal, NULL, RDX_RECORD_LIMIT_TIME);
    }
}

/**************************************************************************
 * V24: PAUSE + BLE 断开 兜底 30 分钟超时
 **************************************************************************/
static void rdx_record_pause_timeout_cb(void *p)
{
    /* 进入回调后 SDK 会自动释放句柄, 但我们也清一遍, 防止上层 stop 重复调度. */
    record_status.pause_timeout_timer = 0;
    r_printf("====== %s --> PAUSE+BLE_OFF timeout %u ms reached, force STOP+save \r",
             __FUNCTION__, (unsigned)RDX_RECORD_PAUSE_TIMEOUT_MS);

    /* 用 cmd_handle 路径模拟 STOP, 不用裸 rdx_record_stop(), 否则会跳过
     *   process_busy_check / max_timer_stop / save_gen / auto_shut_down 等联动.
     * formate/type 用 record_status 当前值拼回去 (cmd_handle 用 '0' + 数值, 0~9 区间),
     * 这部分 cmd_handle 内只用来重写 record_status.formate/scene, STOP 分支并不读, 兜底安全. */
    Record_info stop_cmd = {0};
    stop_cmd.cmd    = '3';
    stop_cmd.formate = (u8)('0' + record_status.formate);
    stop_cmd.type   = (u8)('0' + record_status.scene);
    rdx_record_cmd_handle(&stop_cmd);
}

void rdx_record_pause_timeout_stop(void)
{
    if(record_status.pause_timeout_timer){
        rdx_os_timer_del(record_status.pause_timeout_timer);
        record_status.pause_timeout_timer = 0;
        y_printf("[PAUSE_TIMEOUT] stopped\r");
    }
}

void rdx_record_pause_timeout_start(void)
{
    rdx_record_pause_timeout_stop();
    record_status.pause_timeout_timer = rdx_os_timer_add(rdx_record_pause_timeout_cb,
                                                        NULL,
                                                        RDX_RECORD_PAUSE_TIMEOUT_MS);
    y_printf("[PAUSE_TIMEOUT] started, %u ms countdown\r",
             (unsigned)RDX_RECORD_PAUSE_TIMEOUT_MS);
}

/**************************************************************************
 * V24: BLE 连接状态变化通知 (供 rdx_ble_server connect/disconnect handle 调用)
 **************************************************************************/
void rdx_record_on_ble_conn_changed(u8 connected)
{
    /* 仅 PAUSE 时参与决策; START/RESUME/STOP 一律 no-op, 保持原有 BLE 断开
     * 切离线继续录音 (或正常 auto_shut_down) 的行为不变. */
    if(record_status.run != RECORD_STATE_PAUSE){
        return;
    }
    if(connected){
        rdx_record_pause_timeout_stop();
    }else{
        rdx_record_pause_timeout_start();
    }
}

/**************************************************************************
 * function: rdx_record_stream_interrupt
 * description: Set stream_discont to true when BLE disconnects during recording
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_record_stream_interrupt(void)
{ 
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    RecordStatus* rp = rdx_record_get_status();
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    // Only set stream_discont when recording is in progress
    y_printf("====== %s --> rp->run: %d, rp->stream_discont: %d \r", __func__, rp->run, rp->stream_discont);
    if ((rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME) 
        && (rp->stream_discont != true)) {
        rp->stream_discont = true;
    }
}

/**************************************************************************
 * function: rdx_record_stream_resume
 * description: Set stream_discont to false when BLE reconnects after 3s delay
 * param (void*) priv - timer callback parameter
 * return (*)
 **************************************************************************/
void rdx_record_stream_resume(void* priv)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    RecordStatus* rp = rdx_record_get_status();
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    y_printf();("====== %s --> rp->run: %d, rp->stream_discont: %d \r", __func__, rp->run, rp->stream_discont);
    // Only reset stream_discont when recording is in progress
    if ((rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME) 
        && (rp->stream_discont != false)) {
        rp->stream_discont = false;
    }
    rdx_hook_led_restore_system_state();
    stream_resume_timer = 0;
}

/**************************************************************************
 * function: rdx_record_stream_resume_delayed
 * description: Start a 3-second timer to resume stream after BLE reconnection
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_record_stream_resume_delayed(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    RecordStatus* rp = rdx_record_get_status();
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    // Only start timer when recording is in progress
    if (rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME) {
        if (stream_resume_timer) {
            // If timer is already running, delete it first
            rdx_os_timer_del(stream_resume_timer);
            stream_resume_timer = 0;
        }
        
        stream_resume_timer = rdx_os_timer_add(rdx_record_stream_resume, NULL, 3000);
    }
}

#if RDX_RECORD_USE_LOCAL_PIPELINE

/**************************************************************************
 * function: rdx_record_run_init
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
int rdx_record_run_init(void)
{ 
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    u16 con_hdl = rdx_ble_server_get_conn_handle();
    RecordStatus* rp = rdx_record_get_status();
    rdx_file_transfer_state_t state;
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    y_printf("====== %s \r", __func__);
    if (rdx_file_transfer_get_state(&state) != RDX_OK ||
        state == RDX_FILE_TRANSFER_STATE_UNAVAILABLE) {
        r_printf("====== %s --> file transfer state unavailable \r", __func__);
        return -1;
    }
    if (state == RDX_FILE_TRANSFER_STATE_BUSY) {
        rdx_protocol_file_cmd_handle(RDX_APP_FILE_CMD_STOP);
    }

    memset(au_buf, 0, sizeof(au_buf));
    au_len = 0;

    rp->ui_notify();

    //stop limit timer.
    rdx_record_max_timer_stop();
    //check way of record.
    if(0xffff == con_hdl || 0 == con_hdl){
        rp->mode = RECORD_MODE_OFFLINE;
        rp->orig_mode = RECORD_MODE_OFFLINE;
        y_printf("%s --> not connected, rp->mode = %d, rp->scene = %d \r", __FUNCTION__, rp->mode, rp->scene);
    }else{
        rp->mode = RECORD_MODE_ONLINE;
        rp->orig_mode = RECORD_MODE_ONLINE;
        y_printf("%s --> connected, rp->mode = %d \r", __FUNCTION__, rp->mode);
    }
    // 现在 RECORD_SCENE_xxx 与 COMMAND_RECORD_SCENE_xxx 值一致，无需映射
    u8 scene = rp->scene;

    //power on force online.
    rdx_app_emmc_poweroff_check_timer_stop();
    rdx_app_emmc_poweron(0);
    int err = dev_manager_add("sd0");
    if (err != 0) {
        r_printf("sd add fail\n");
    }else{
        g_printf("====== %s --> sd add success \n", __func__);
    }

    if(rp->run == RECORD_STATE_RESUME){
        y_printf("[RESUME] skip dat_1_gen: keep sn=%u name='%s' begin_time=%u paused_acc=%u marks=%u\r",
                 (unsigned)rdx_uxfile_get_operateFile_info()->sn,
                 rdx_uxfile_get_operateFile_info()->filename,
                 (unsigned)rp->begin_time,
                 (unsigned)rp->paused_accumulated_ms,
                 (unsigned)s_cur_mark_count);
    }else{
        // rdx_uxfile_mssg_1_generate(scene);
        rdx_uxfile_dat_1_gen(scene);
        if(rp->begin_time == 0){
            rp->begin_time = jiffies_msec();
        }
    }

    //send record state to app.
    if(con_hdl != 0xffff && con_hdl != 0){
        //send state to app.
        rdx_protocol_record_state_indicate();
    }

    //start max record time.
    rdx_record_max_timer_start();

    return 0;
}

/**************************************************************************
 * function: rdx_record_run_data_handle
 * description: 
 * param (u8*) d
 * param (u32) len
 * return (*)
 **************************************************************************/
int rdx_record_run_data_handle(u8* d, u32 len)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    u16 con_hdl = rdx_ble_server_get_conn_handle();
    u8 rd_cnt = rdx_record_get_filter_cnt();
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(rd_cnt < 10){
        rd_cnt++;
        rdx_record_set_filter_cnt(rd_cnt);
        return 1;
    }
    RecordStatus* rp = rdx_record_get_status();
    rdx_record_set_process_state_ready();

    //online stream send.
    rdx_ble_server_info_t* pd = rdx_ble_server_get_info();
    if(0xffff != con_hdl && 0 != con_hdl && rp->stream_discont == false &&
       pd->ccc_configured == TRUE && pd->stream_tx_ready == TRUE){
        rdx_protocol_audio_data_indicate(d, len);
    }

    //local save.
    if(au_len + len < AUDIO_SEND_BUF_SIZE){
        memcpy(au_buf + au_len, d, len);
        au_len += len;
    }else{
        if(au_len + len > AUDIO_SEND_BUF_SIZE){
            memcpy(au_buf + au_len, d, AUDIO_SEND_BUF_SIZE - au_len);
            au_len = AUDIO_SEND_BUF_SIZE;
        }else{
            memcpy(au_buf + au_len, d, len);
            au_len += len;
        }
        int r = rdx_uxfile_raw_write((u8*)au_buf, (u32)au_len, rp->scene);
        if(r < 0){
            r_printf("!!! stream write fail, stop record! \n");
            rdx_record_stop();
            memset(au_buf, 0, sizeof(au_buf));
            au_len = 0;
            //show error.
        }
        memset(au_buf, 0, sizeof(au_buf));
        au_len = 0;
    }
    return 0;
}

/**************************************************************************
 * function: rdx_record_run_exit
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
int rdx_record_run_exit(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    RecordStatus* rp = rdx_record_get_status();
    u16 con_hdl = rdx_ble_server_get_conn_handle();    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(au_len > 0){
        int r = rdx_uxfile_raw_write((u8*)au_buf, (u32)au_len, rp->scene);
        if(r < 0){
            r_printf("!!! stream write fail, stop record! \n");
            rdx_record_stop();
            memset(au_buf, 0, sizeof(au_buf));
            au_len = 0;
        }
        memset(au_buf, 0, sizeof(au_buf));
        au_len = 0;        
    }
    //clear filter cnt.
	rdx_record_set_filter_cnt(0);

    //send ack of record state.
    if(con_hdl != 0xffff && con_hdl != 0){
        rdx_protocol_record_state_indicate();
    }

    y_printf("%s --> con_hdl = %d, rp->mode = %d \r", __FUNCTION__, con_hdl, rp->mode);

    if(rp->run == RECORD_STATE_PAUSE){
        y_printf("[PAUSE_EXIT] keep open: sn=%u name='%s', will resume append same raw\r",
                 (unsigned)rdx_uxfile_get_operateFile_info()->sn,
                 rdx_uxfile_get_operateFile_info()->filename);
        rdx_record_set_process_state_ready();
        rp->stream_discont = false;
        return 0;
    }

    // if(rp->orig_mode == RECORD_MODE_OFFLINE){
        // rdx_uxfile_mssg_1_save();
        rdx_uxfile_dat_1_save_gen();
        /* V24: begin_time 仅在彻底 STOP 时清零, PAUSE 路径下保留原 START 时刻,
         * 否则 RESUME 后 run_init 会重新写一个 jiffies, 录音标记 offset_ms 失真 */
        if(rp->run == RECORD_STATE_STOP){
            rp->begin_time = 0;
            rp->paused_accumulated_ms = 0;
            rp->pause_start_ms = 0;
        }
    // }
    
    rdx_record_set_process_state_ready();
    rp->orig_mode = rp->mode;

    rdx_record_max_timer_stop();

    rp->ui_notify();

    //
    // rdx_protocol_record_file_info_indicate();

    rdx_app_emmc_poweroff_check();

    rdx_uxfile_operate_file_init();

    rp->stream_discont = false;
    
    return 0;
}

#else
/**************************************************************************
 * function: rdx_record_run_init
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
int rdx_record_run_init(void)
{ 
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    u16 con_hdl = rdx_ble_server_get_conn_handle();
    RecordStatus* rp = rdx_record_get_status();
    rdx_file_transfer_state_t state;
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    y_printf("====== %s \r", __func__);
    if (rdx_file_transfer_get_state(&state) != RDX_OK ||
        state == RDX_FILE_TRANSFER_STATE_UNAVAILABLE) {
        r_printf("====== %s --> file transfer state unavailable \r", __func__);
        return -1;
    }
    if (state == RDX_FILE_TRANSFER_STATE_BUSY) {
        rdx_protocol_file_cmd_handle(RDX_APP_FILE_CMD_STOP);
    }

    memset(au_buf, 0, sizeof(au_buf));
    au_len = 0;

    rp->ui_notify();

    //stop limit timer.
    rdx_record_max_timer_stop();
    //check way of record.
    if(0xffff == con_hdl || 0 == con_hdl){
        rp->mode = RECORD_MODE_OFFLINE;
        rp->orig_mode = RECORD_MODE_OFFLINE;
        // 现在 RECORD_SCENE_xxx 与 COMMAND_RECORD_SCENE_xxx 值一致，无需映射
        u8 scene = rp->scene;
        y_printf("%s --> not connected, rp->mode = %d, rp->scene = %d \r", __FUNCTION__, rp->mode, rp->scene);

        //power on force online.
        rdx_app_emmc_poweroff_check_timer_stop();
        rdx_app_emmc_poweron(0);
        int err = dev_manager_add("sd0");
        if (err != 0) {
		    r_printf("sd add fail\n");
        }else{
            g_printf("====== %s --> sd add success \n", __func__);
        }

        // rdx_uxfile_mssg_1_generate(scene);
        rdx_uxfile_dat_1_gen(scene);

        /* V24: begin_time 只在初始 START 时设置, RESUME 不重置 */
        if(rp->begin_time == 0){
            rp->begin_time = jiffies_msec();
        }
    }else{
        rp->mode = RECORD_MODE_ONLINE;
        rp->orig_mode = RECORD_MODE_ONLINE;
        y_printf("%s --> connected, rp->mode = %d \r", __FUNCTION__, rp->mode);
    }

    //set gain.
    // rdx_record_mic_gain_check();

    //start max record time.
    rdx_record_max_timer_start();

    return 0;
}

/**************************************************************************
 * function: rdx_record_run_data_handle
 * description: 
 * param (u8*) d
 * param (u32) len
 * return (*)
 **************************************************************************/
int rdx_record_run_data_handle(u8* d, u32 len)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    u16 con_hdl = rdx_ble_server_get_conn_handle();
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    u8 rd_cnt = rdx_record_get_filter_cnt();
    // r_printf("%s --> rd_cnt = %d \r", __func__, rd_cnt);
    if(rd_cnt < 10){
        rd_cnt++;
        rdx_record_set_filter_cnt(rd_cnt);
        // r_printf("%s --> rd_cnt = %d \r", __func__, rd_cnt);
        return 1;
    }

    // b_printf("%s --> len = %d \r", __func__, len);
    // put_buf(d, len);

    RecordStatus* rp = rdx_record_get_status();
    rdx_record_set_process_state_ready();

    if(rp->orig_mode == RECORD_MODE_OFFLINE){
        if(au_len + len < AUDIO_SEND_BUF_SIZE){
            memcpy(au_buf + au_len, d, len);
            au_len += len;
        }else{
            if(au_len + len > AUDIO_SEND_BUF_SIZE){
                memcpy(au_buf + au_len, d, AUDIO_SEND_BUF_SIZE - au_len);
                au_len = AUDIO_SEND_BUF_SIZE;
            }else{
                memcpy(au_buf + au_len, d, len);
                au_len += len;
            }
            // b_printf("\r写文件前 --> au_len = %d, data_len = %d \r", au_len, len);
            // unsigned long rb_timestamp = jiffies_msec();
            int r = rdx_uxfile_raw_write((u8*)au_buf, (u32)au_len, rp->scene);
            // unsigned long ra_timestamp = jiffies_msec();
            // b_printf("写文件后 --> time = %d \r", ra_timestamp - rb_timestamp);
            if(r < 0){
                r_printf("!!! stream write fail, stop record! \n");
                rdx_record_stop();
                memset(au_buf, 0, sizeof(au_buf));
                au_len = 0;
                //show error.
                // os_taskq_post_msg("oled_show_task", 1, OLED_SHOW_MEM_ERR);
            }
            memset(au_buf, 0, sizeof(au_buf));
            au_len = 0;
            // r_printf("\n写文件后 --> au_len = %d, data_len = %d \r", au_len, len);
        }
    }else{
        rdx_ble_server_info_t* pd = rdx_ble_server_get_info();
        if(0xffff != con_hdl && 0 != con_hdl && rp->stream_discont == false &&
           pd->ccc_configured == TRUE && pd->stream_tx_ready == TRUE){
            rdx_protocol_audio_data_indicate(d, len);
        }
    }

    return 0;
}

/**************************************************************************
 * function: rdx_record_run_exit
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
int rdx_record_run_exit(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    RecordStatus* rp = rdx_record_get_status();
    u16 con_hdl = rdx_ble_server_get_conn_handle();
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(au_len > 0){
        int r = rdx_uxfile_raw_write((u8*)au_buf, (u32)au_len, rp->scene);
        if(r < 0){
            r_printf("!!! stream write fail, stop record! \n");
            rdx_record_stop();
            memset(au_buf, 0, sizeof(au_buf));
            au_len = 0;
        }
        memset(au_buf, 0, sizeof(au_buf));
        au_len = 0;        
    }
	rdx_record_set_filter_cnt(0);
	
    y_printf("%s --> con_hdl = %d, rp->mode = %d \r", __FUNCTION__, con_hdl, rp->mode);
    if(rp->orig_mode == RECORD_MODE_OFFLINE){
        // rdx_uxfile_mssg_1_save();
        rdx_uxfile_dat_1_save_gen();
        /* V24: begin_time 仅在彻底 STOP 时清零, PAUSE 路径下保留原 START 时刻 */
        if(rp->run == RECORD_STATE_STOP){
            rp->begin_time = 0;
        }
    }
    
    rdx_record_set_process_state_ready();
    rp->orig_mode = rp->mode;

    rdx_record_max_timer_stop();

    rp->ui_notify();

    if(rp->rerun == true){
        rp->rerun = false;
        // rp->run = RECORD_STATE_START;
        // rdx_record_process();
        rdx_os_timer_add(rdx_record_start, NULL, 1000);
    }else{
        rdx_app_emmc_poweroff_check();
    }

    rdx_uxfile_operate_file_init();

    return 0;
}

#endif

/**************************************************************************
 * function: rdx_record_err_reboot_flag_read_from_vm
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
u8 rdx_record_err_reboot_flag_read_from_vm(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    u8 err_reboot_flag = 0xff;
    rdx_err_t ret;
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    ret = rdx_storage_read(RDX_STORAGE_KEY_REC_ERR_REBOOT,
                           &err_reboot_flag, sizeof(err_reboot_flag));
    if (ret == RDX_OK) {
        y_printf("===> read err reboot flag ok, err_reboot_flag: %d \r", err_reboot_flag);
    }
    return err_reboot_flag;
}

/**************************************************************************
 * function: rdx_record_err_reboot_flag_write_into_vm
 * description: 
 * param:(u8) err_reboot_flag
 * return (*)
 **************************************************************************/
int rdx_record_err_reboot_flag_write_into_vm(u8 err_reboot_flag)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    rdx_err_t ret;
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    log_info("===> %s --> err_reboot_flag: %d \r", __func__, err_reboot_flag);
    ret = rdx_storage_write(RDX_STORAGE_KEY_REC_ERR_REBOOT,
                            &err_reboot_flag, sizeof(err_reboot_flag));
    if (ret == RDX_OK) {
        log_info("rdx_record_err_reboot_flag_write_into_vm success \r");
        return sizeof(err_reboot_flag);
    }else{
        log_info("rdx_record_err_reboot_flag_write_into_vm fail \r");
    }
    return -1;
}

#endif
