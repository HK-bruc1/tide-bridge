#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".icsd_adt.data.bss")
#pragma data_seg(".icsd_adt.data")
#pragma const_seg(".icsd_adt.text.const")
#pragma code_seg(".icsd_adt.text")
#endif
/*
 ****************************************************************************
 *							ICSD ADT APP API
 *
 *Description	: 智能免摘和anc风噪检测相关接口
 *Notes			:
 ****************************************************************************
 */
#include "app_config.h"
#if ((defined TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN) && TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN && \
	 TCFG_AUDIO_ANC_ENABLE)
#include "system/includes.h"
#include "icsd_adt_app.h"
#include "icsd_adt.h"
#include "system/task.h"
#include "audio_adc.h"
#include "audio_dac.h"
#include "audio_anc.h"
#include "timer.h"
#include "btstack/avctp_user.h"
#include "bt_tws.h"
#include "tone_player.h"
#include "asm/anc.h"
#include "app_anctool.h"
#include "audio_config.h"
#include "icsd_anc_user.h"
#include "sniff.h"
#include "effects/convert_data.h"

#if (ICSD_ADT_WIND_INFO_SPP_DEBUG_EN || ICSD_ADT_VOL_NOISE_LVL_SPP_DEBUG_EN)
#include "spp_user.h"
#endif

#if ICSD_ADT_MIC_DATA_EXPORT_EN
#include "aec_uart_debug.h"
#endif /*ICSD_ADT_MIC_DATA_EXPORT_EN*/

#if TCFG_AUDIO_FIT_DET_ENABLE
#include "icsd_dot_app.h"
#endif

#if TCFG_AUDIO_ANC_REAL_TIME_ADAPTIVE_ENABLE
#include "rt_anc_app.h"
#endif

#if TCFG_AUDIO_ANC_WIND_NOISE_DET_ENABLE && ICSD_ADT_WIND_INFO_SPP_DEBUG_EN && (!TCFG_BT_SUPPORT_SPP || !APP_ONLINE_DEBUG)
#error "please open TCFG_BT_SUPPORT_SPP and APP_ONLINE_DEBUG"
#endif
#if TCFG_AUDIO_ANC_ENV_NOISE_DET_ENABLE && ICSD_ADT_VOL_NOISE_LVL_SPP_DEBUG_EN && (!TCFG_BT_SUPPORT_SPP || !APP_ONLINE_DEBUG)
#error "please open TCFG_BT_SUPPORT_SPP and APP_ONLINE_DEBUG"
#endif
#if ICSD_ADT_MIC_DATA_EXPORT_EN && (TCFG_AUDIO_DATA_EXPORT_DEFINE != AUDIO_DATA_EXPORT_VIA_UART)
#error "please open AUDIO_DATA_EXPORT_VIA_UART"
#endif
#if ICSD_ADT_SHARE_ADC_ENABLE && !defined(TCFG_AUDIO_ADC_ENABLE_ALL_DIGITAL_CH)
#error "please define TCFG_AUDIO_ADC_ENABLE_ALL_DIGITAL_CH or close ICSD_ADT_SHARE_ADC_ENABLE"
#endif

#if 1
#define adt_log printf
#else
#define adt_log(...)
#endif


extern const u8 mic_input_v2;
extern struct audio_dac_hdl dac_hdl;
extern struct audio_adc_hdl adc_hdl;
extern const u8 const_adc_async_en;

struct speak_to_chat_t {
    volatile u8 busy;
    volatile u8 state;
    struct icsd_acoustic_detector_libfmt libfmt;//回去需要的资源
    struct icsd_acoustic_detector_infmt infmt;//转递资源
    void *lib_alloc_ptr;//adt申请的内存指针
    u32 timer;//免摘结束定时齐
    u8 sensitivity;
    u8 voice_state;//免摘检测到声音的状态
    u8 adt_resume;//resume的状态
    u8 adt_suspend;//suspend的状态
    u8 adt_resume_timer;//resume定时器
    u8 wind_lvl;//记录风强信息
    u8 a2dp_state;//触发免摘时是否播歌的状态
    u8 speak_to_chat_state;//智能免摘的状态
    u8 last_anc_state;//记录触发免摘前的anc模式
    u8 anc_switch_flag;//是否是内部免摘触发的anc模式切换
    u8 tws_sync_state;//是否已经接收到同步的信息
    u8 adt_wind_lvl;//风噪等级
    u8 adt_tmp_wind_lvl;//风噪等级
    int adt_wind_gain_lvl;	//当前风噪噪声等级
    u8 adt_wind_suspend_rtanc;	//触发风噪时挂起RT_ANC 标志
    u8 adt_wat_result;//广域点击次数
    u8 adt_param_updata;//是否在resume前更新参数
    u8 adt_wat_ignore_flag;//是否忽略广域点击的响应
#if (ICSD_ADT_WIND_INFO_SPP_DEBUG_EN || ICSD_ADT_VOL_NOISE_LVL_SPP_DEBUG_EN)
    struct spp_operation_t *spp_opt;
    u8 spp_connected_state;
#endif
    u32 wind_cnt;
    int sample_rate;
    int adc_seq;
#if ICSD_ADT_MIC_DATA_EXPORT_EN
    s16 tmpbuf0[256];
    s16 tmpbuf1[256];
    s16 tmpbuf2[256];
#endif /*ICSD_ADT_MIC_DATA_EXPORT_EN*/
    u8 adt_switch_trans_state;//判断是否adt里面切的trans
    int talk_mic_seq;
    int ff_mic_seq;
    int fb_mic_seq;
    s16 *talk_mic_buf;
    s16 *ff_mic_buf;
    s16 *fb_mic_buf;
    u8 tmp_noise_lvl;//音量自适应噪声等级
};
struct speak_to_chat_t *speak_to_chat_hdl = NULL;

typedef struct {
    u8 mode_switch_busy;
    u8 speak_to_chat_state;
    u16 adt_mode;
    u16 speak_to_chat_end_time; //免摘定时结束的时间，单位ms
} adt_info_t;
static adt_info_t adt_info = {
    .mode_switch_busy = 0,
    .speak_to_chat_state = AUDIO_ADT_CLOSE,
    .adt_mode = ADT_MODE_CLOSE,
    .speak_to_chat_end_time = 5000,
};

/*tws同步char状态使用*/
static u8 globle_speak_to_chat_state = 0;
/*用于判断先开anc off再开免摘*/
static u8 adt_open_in_anc = 0;
static u8 anc_env_adaptive_gain_suspend = 0;
u8 get_icsd_adt_mic_ch(struct icsd_acoustic_detector_libfmt *libfmt);

//ADT TWS 信息同步回调处理函数
void audio_icsd_adt_info_sync_cb(void *_data, u16 len, bool rx)
{
    struct speak_to_chat_t *hdl = speak_to_chat_hdl;
    u8 tmp_data[4];
    u8 *data = (u8 *)_data;
    int mode = data[0];
    int voice_state = 0;
    int wind_lvl    = 0;
    int wat_result  = 0;
    int noise_lvl  = 0;
    int val = 0;
    int err = 0;
    int adt_mode = 0;
    int suspend = 0;
    int anc_fade_gain = 0;
    int anc_fade_mode = 0;
    int anc_fade_time = 0;
    // adt_log("audio_icsd_adt_info_sync_cb rx:%d, mode:%d ", rx, mode);
    switch (mode) {
#if TCFG_AUDIO_ANC_WIND_NOISE_DET_ENABLE
    case SYNC_ICSD_ADT_WIND_LVL_CMP:
        if (!hdl || !hdl->state) {
            break;
        }
        hdl->busy = 1;
        wind_lvl  = ((u8 *)_data)[1];

        if (adt_info.adt_mode & ADT_WIND_NOISE_DET_MODE) {
            /*从机本地的风噪和接收到的主机风噪比较，取最大值*/
            if (tws_api_get_role() == TWS_ROLE_SLAVE) {
                if (hdl->adt_tmp_wind_lvl < ((u8)wind_lvl)) {
                    hdl->adt_tmp_wind_lvl = (u8)wind_lvl;
                }
                /*比较完后从机同步最终的风噪信息*/
                tmp_data[0] = SYNC_ICSD_ADT_WIND_LVL_RESULT;
                tmp_data[1] = hdl->adt_tmp_wind_lvl;
                tmp_data[2] = 0;
                tmp_data[3] = 0;
                audio_icsd_adt_info_sync(tmp_data, 4);
            }
        }

        hdl->busy = 0;
        break;
    case SYNC_ICSD_ADT_WIND_LVL_RESULT:
        if (!hdl || !hdl->state) {
            break;
        }
        hdl->busy = 1;
        wind_lvl    = ((u8 *)_data)[1];

        err = os_taskq_post_msg(SPEAK_TO_CHAT_TASK_NAME, 2, ICSD_ADT_WIND_LVL, wind_lvl);
        if (err != OS_NO_ERR) {
            printf("%s err %d", __func__, err);
        }

        hdl->busy = 0;
        break;
#endif /*TCFG_AUDIO_ANC_WIND_NOISE_DET_ENABLE*/
#if TCFG_AUDIO_ANC_ENV_NOISE_DET_ENABLE
    case SYNC_ICSD_ADT_ENV_NOISE_LVL_CMP:
        if (!hdl || !hdl->state) {
            break;
        }
        hdl->busy = 1;
        u8 noise_lvl  = ((u8 *)_data)[1];

        /*从机本地的噪声和接收到的主机噪声比较，取最大值*/
        if (tws_api_get_role() == TWS_ROLE_SLAVE) {
            if (hdl->tmp_noise_lvl < ((u8)noise_lvl)) {
                hdl->tmp_noise_lvl = (u8)noise_lvl;
            }
            /*比较完后从机同步最终的噪声信息*/
            tmp_data[0] = SYNC_ICSD_ADT_ENV_NOISE_LVL_RESULT;
            tmp_data[1] = hdl->tmp_noise_lvl;
            tmp_data[2] = 0;
            tmp_data[3] = 0;
            audio_icsd_adt_info_sync(tmp_data, 4);
        }
        hdl->busy = 0;
        break;
    case SYNC_ICSD_ADT_ENV_NOISE_LVL_RESULT:
        if (!hdl || !hdl->state) {
            break;
        }
        hdl->busy = 1;
        noise_lvl    = ((u8 *)_data)[1];

        err = os_taskq_post_msg(SPEAK_TO_CHAT_TASK_NAME, 2, ICSD_ADT_ENV_NOISE_LVL, noise_lvl);
        if (err != OS_NO_ERR) {
            printf("%s err %d", __func__, err);
        }

        hdl->busy = 0;
        break;
#endif /*TCFG_AUDIO_ANC_ENV_NOISE_DET_ENABLE*/
#if TCFG_AUDIO_SPEAK_TO_CHAT_ENABLE
    case SYNC_ICSD_ADT_VOICE_STATE:
        voice_state    = ((u8 *)_data)[1];
        if ((adt_info.adt_mode & ADT_SPEAK_TO_CHAT_MODE) && voice_state) {
            /* err = os_taskq_post_msg(SPEAK_TO_CHAT_TASK_NAME, 2, ICSD_ADT_VOICE_STATE, voice_state); */
            set_speak_to_chat_voice_state(1);
        }
        break;
#endif /*TCFG_AUDIO_SPEAK_TO_CHAT_ENABLE*/
#if TCFG_AUDIO_WIDE_AREA_TAP_ENABLE
    case SYNC_ICSD_ADT_WAT_RESULT:
        if (!hdl || !hdl->state) {
            break;
        }
        hdl->busy = 1;
        wat_result    = ((u8 *)_data)[1];
#if (TCFG_USER_TWS_ENABLE && !AUDIO_ANC_WIDE_AREA_TAP_EVENT_SYNC)
        /*广域点击:是否响应另一只耳机的点击*/
        if (!rx)
#endif /*AUDIO_ANC_WIDE_AREA_TAP_EVENT_SYNC*/
        {
            if ((adt_info.adt_mode & ADT_WIDE_AREA_TAP_MODE) && wat_result && !hdl->adt_wat_ignore_flag) {
                err = os_taskq_post_msg(SPEAK_TO_CHAT_TASK_NAME, 2, ICSD_ADT_WAT_RESULT, wat_result);
                if (err != OS_NO_ERR) {
                    printf("%s err %d", __func__, err);
                }
            }
        }
        hdl->busy = 0;
        break;
#endif /*TCFG_AUDIO_WIDE_AREA_TAP_ENABLE*/
    case SYNC_ICSD_ADT_SUSPEND:
        audio_speak_to_char_suspend();
        break;
    case SYNC_ICSD_ADT_OPEN:
    case SYNC_ICSD_ADT_CLOSE:
        printf("SYNC_ICSD_ADT_OPEN/CLOSE");
        adt_mode = (((u8 *)_data)[2] << 8) | ((u8 *)_data)[1];
        suspend = ((u8 *)_data)[3];
        printf("L:%d, H:%d, m:%d", ((u8 *)_data)[1], ((u8 *)_data)[2], adt_mode);
        err = os_taskq_post_msg(SPEAK_TO_CHAT_TASK_NAME, 3, mode, adt_mode, suspend);
        if (err != OS_NO_ERR) {
            printf("%s err %d", __func__, err);
        }
        break;
    case SYNC_ICSD_ADT_SET_ANC_FADE_GAIN:
        anc_fade_gain = (((u8 *)_data)[2] << 8) | ((u8 *)_data)[1];
        anc_fade_mode = ((u8 *)_data)[3];
        anc_fade_time = ((u8 *)_data)[4];
        anc_fade_time = anc_fade_time * 100;
        err = os_taskq_post_msg(SPEAK_TO_CHAT_TASK_NAME, 4, mode, anc_fade_gain, anc_fade_mode, anc_fade_time);
        if (err != OS_NO_ERR) {
            printf("%s err %d", __func__, err);
        }
        break;
    default:
        break;
    }
}

#define TWS_FUNC_ID_ICSD_ADT_M2S    TWS_FUNC_ID('I', 'A', 'M', 'S')
REGISTER_TWS_FUNC_STUB(audio_icsd_adt_m2s) = {
    .func_id = TWS_FUNC_ID_ICSD_ADT_M2S,
    .func    = audio_icsd_adt_info_sync_cb,
};

void audio_icsd_adt_info_sync(u8 *data, int len)
{
    struct speak_to_chat_t *hdl = speak_to_chat_hdl;
    // printf("audio_icsd_adt_info_sync , %d, %d, %d", data[0], data[1], data[2]);
#if TCFG_USER_TWS_ENABLE
    if (get_tws_sibling_connect_state()) {
        int ret = tws_api_send_data_to_sibling(data, len, TWS_FUNC_ID_ICSD_ADT_M2S);
    } else
#endif
    {
        audio_icsd_adt_info_sync_cb(data, len, 0);
    }
}

//ADC 中断处理函数
static void audio_mic_output_handle(void *priv, s16 *data, int len)
{
    struct speak_to_chat_t *hdl = speak_to_chat_hdl;
    int err = 0;
    s16 *talk_mic = NULL;
    s16 *ff_mic = NULL;
    s16 *fb_mic = NULL;
    s32 *s32_talk_mic = NULL;
    s32 *s32_ff_mic = NULL;
    s32 *s32_fb_mic = NULL;
    s32 *s32_data = NULL;
    u16 talk_mic_ch = icsd_get_talk_mic_ch();
    u16 ref_mic_l_ch = icsd_get_ref_mic_L_ch();
    u16 fb_mic_l_ch = icsd_get_fb_mic_L_ch();
    u16 icsd_mic_num = audio_get_mic_num(hdl->libfmt.mic_type);
    u16 open_mic_ch = get_icsd_adt_mic_ch(&hdl->libfmt);
    u8 mic_ch_num;
    if (const_adc_async_en) {
        mic_ch_num = audio_max_adc_ch_num_get();
    } else {
        mic_ch_num = icsd_adt_current_mic_num();
    }
    if (hdl && hdl->state) {
#if (ICSD_ADT_SHARE_ADC_ENABLE == 0)//没有共用adc并且使用24bit时
        if (adc_hdl.bit_width != ADC_BIT_WIDTH_16) {
            // putchar('B');
            audio_convert_data_32bit_to_16bit_round((s32 *)data, (s16 *)data, (len >> 2) * mic_ch_num);
            len >>= 1;
        }
#endif

        if (mic_input_v2) {

#if ICSD_ADT_SHARE_ADC_ENABLE
            //共用adc并且使用24bit时
            if (adc_hdl.bit_width != ADC_BIT_WIDTH_16) {
                // putchar('B');
                s32_data = (s32 *)data;
                /*复用adc时，adc的数据需要额外buf存起来*/
                s32_talk_mic = (s32 *)hdl->talk_mic_buf;
                s32_ff_mic   = (s32 *)hdl->ff_mic_buf;
                s32_fb_mic   = (s32 *)hdl->fb_mic_buf;
                talk_mic = hdl->talk_mic_buf;
                ff_mic   = hdl->ff_mic_buf;
                fb_mic   = hdl->fb_mic_buf;

                for (int i = 0; i < len / 4; i++) {
                    if (open_mic_ch & talk_mic_ch) {
                        s32_talk_mic[i] = s32_data[mic_ch_num * i + hdl->talk_mic_seq];
                    }
                    if (open_mic_ch & ref_mic_l_ch) {
                        s32_ff_mic[i]   = s32_data[mic_ch_num * i + hdl->ff_mic_seq];
                    }
                    if (open_mic_ch & fb_mic_l_ch) {
                        s32_fb_mic[i]   = s32_data[mic_ch_num * i + hdl->fb_mic_seq];
                    }
                }
                if (open_mic_ch & talk_mic_ch) {
                    audio_convert_data_32bit_to_16bit_round((s32 *)s32_talk_mic, (s16 *)talk_mic, len >> 2);
                }
                if (open_mic_ch & ref_mic_l_ch) {
                    audio_convert_data_32bit_to_16bit_round((s32 *)s32_ff_mic, (s16 *)ff_mic, len >> 2);
                }
                if (open_mic_ch & fb_mic_l_ch) {
                    audio_convert_data_32bit_to_16bit_round((s32 *)s32_fb_mic, (s16 *)fb_mic, len >> 2);
                }
                len >>= 1;
            } else
#endif
            {
                //共用adc并且使用16bit时 || 不共用adc使用16bit || 不共用adc使用24bit
                /*复用adc时，adc的数据需要额外buf存起来*/
                talk_mic = hdl->talk_mic_buf;
                ff_mic   = hdl->ff_mic_buf;
                fb_mic   = hdl->fb_mic_buf;
                for (int i = 0; i < len / 2; i++) {
                    if (open_mic_ch & talk_mic_ch) {
                        talk_mic[i] = data[mic_ch_num * i + hdl->talk_mic_seq];
                    }
                    if (open_mic_ch & ref_mic_l_ch) {
                        ff_mic[i]   = data[mic_ch_num * i + hdl->ff_mic_seq];
                    }
                    if (open_mic_ch & fb_mic_l_ch) {
                        fb_mic[i]   = data[mic_ch_num * i + hdl->fb_mic_seq];
                    }
                }
            }

            icsd_acoustic_detector_mic_input_hdl_v2(priv, talk_mic, ff_mic, fb_mic, len);

#if ICSD_ADT_MIC_DATA_EXPORT_EN
            if (open_mic_ch & talk_mic_ch) {
                aec_uart_fill(0, talk_mic, len);
            }
            if (open_mic_ch & ref_mic_l_ch) {
                aec_uart_fill(1, ff_mic,   len);
            }
            if (open_mic_ch & fb_mic_l_ch) {
                aec_uart_fill(2, fb_mic,   len);
            }
            /* aec_uart_write(); */
            /*write里面可能会触发pend的动作，所有放到任务去跑*/
            err = os_taskq_post_msg(SPEAK_TO_CHAT_TASK_NAME, 2, ICSD_ADT_EXPORT_DATA_WRITE, 1);
            if (err != OS_NO_ERR) {
                printf("%s err %d", __func__, err);
            }
#endif /*ICSD_ADT_MIC_DATA_EXPORT_EN*/
        } else {
            /*adt使用3mic模式 或者 通话使用3mic时(adc固定开3个)，如果免摘使用2mic，需要重新排列数据顺序*/
            if (mic_ch_num > icsd_mic_num) {
                for (int i = 0; i < len / 2; i++) {
                    for (int j = 0; j < icsd_mic_num; j++) {
                        data[icsd_mic_num * i + j] = data[mic_ch_num * i + hdl->adc_seq + j];
                    }
                }
            }
            icsd_acoustic_detector_mic_input_hdl(priv, data, len);

#if ICSD_ADT_MIC_DATA_EXPORT_EN
            if (icsd_mic_num == 2) {
                for (int i = 0; i < len / 2; i++) {
                    hdl->tmpbuf0[i] = data[2 * i];
                    hdl->tmpbuf1[i] = data[2 * i + 1];
                    /* tmpbuf2[i] = data[3 * i + 2];  */
                }

            } else {
                for (int i = 0; i < len / 2; i++) {
                    hdl->tmpbuf0[i] = data[3 * i];
                    hdl->tmpbuf1[i] = data[3 * i + 1];
                    hdl->tmpbuf2[i] = data[3 * i + 2];
                }
            }
            aec_uart_fill(0, hdl->tmpbuf0, len);
            aec_uart_fill(1, hdl->tmpbuf1, len);
            aec_uart_fill(2, hdl->tmpbuf2, len);
            /* aec_uart_write(); */
            /*write里面可能会触发pend的动作，所有放到任务去跑*/
            err = os_taskq_post_msg(SPEAK_TO_CHAT_TASK_NAME, 2, ICSD_ADT_EXPORT_DATA_WRITE, 1);
            if (err != OS_NO_ERR) {
                printf("%s err %d", __func__, err);
            }
#endif /*ICSD_ADT_MIC_DATA_EXPORT_EN*/
        }
    }
}

/*算法结果输出*
 * voice_state : 是否有说话，0 无讲话，1 有讲话
 * wind_lvl : 风噪等级，0 - 10
 * wat_result : 连击次数，2 双击， 3 三击
 */
extern int tws_in_sniff_state();
void audio_acoustic_detector_output_hdl(u8 voice_state, u8 wind_lvl, u8 wat_result)
{
    struct speak_to_chat_t *hdl = speak_to_chat_hdl;
    u8 data[4] = {SYNC_ICSD_ADT_WIND_LVL_CMP, voice_state, wind_lvl, wat_result};
    static u8 cnt = 0;
    u8 wind_lvl_target_cnt = 0;;

    if (hdl && hdl->state) {
        hdl->busy = 1;
        /*限制少的判断放前面*/
        if ((adt_info.adt_mode & ADT_WIDE_AREA_TAP_MODE) && wat_result && !hdl->adt_wat_ignore_flag) {
            adt_log("%s:%d", __func__, __LINE__);
#if TCFG_USER_TWS_ENABLE
            if (tws_in_sniff_state()) {
                /*如果在蓝牙siniff下需要退出蓝牙sniff再发送*/
                icsd_adt_tx_unsniff_req();
            }
#endif
            /*没有tws时直接更新状态*/
            data[0] = SYNC_ICSD_ADT_WAT_RESULT;
            audio_icsd_adt_info_sync(data, 4);
        } else if ((adt_info.adt_mode & ADT_SPEAK_TO_CHAT_MODE) && voice_state) {
            adt_log("%s:%d", __func__, __LINE__);
#if TCFG_USER_TWS_ENABLE
            if (tws_in_sniff_state() && (tws_api_get_role() == TWS_ROLE_MASTER)) {
                /*如果在蓝牙siniff下需要退出蓝牙sniff再发送*/
                icsd_adt_tx_unsniff_req();
            }
#endif
            /*同步状态*/
            data[0] = SYNC_ICSD_ADT_VOICE_STATE;
            audio_icsd_adt_info_sync(data, 4);

        } else if (adt_info.adt_mode & ADT_WIND_NOISE_DET_MODE) {
            /*独立风噪，500ms一次输出，不需要间隔响应*/
            cnt ++;
#if TCFG_USER_TWS_ENABLE
            if (get_tws_sibling_connect_state()) {
                /*记录本地的风噪强度*/
                hdl->adt_tmp_wind_lvl = wind_lvl;
                /*同步主机风噪和从机风噪比较*/
                if ((tws_api_get_role() == TWS_ROLE_MASTER)) {
                    if (cnt > wind_lvl_target_cnt) {
                        /*间隔wind_lvl_target_cnt次发送一次*/
                        data[0] = SYNC_ICSD_ADT_WIND_LVL_CMP;
                        audio_icsd_adt_info_sync(data, 4);

                        cnt = 0;
                    }
                }
            } else
#endif
            {
                /*没有tws时直接更新状态*/
                hdl->adt_wind_lvl = wind_lvl;
                if (cnt > wind_lvl_target_cnt) {
                    /*间隔wind_lvl_target_cnt次发送一次*/
                    data[0] = SYNC_ICSD_ADT_WIND_LVL_RESULT;
                    audio_icsd_adt_info_sync(data, 4);
                    cnt = 0;
                }
            }
        }

        hdl->busy = 0;
    }
}

//ANC task dma 抄数回调接口
void audio_acoustic_detector_anc_dma_post_msg(void)
{

}
//ANC DMA下采样数据输出回调，用于写卡分析
void audio_acoustic_detector_anc_dma_output_callback(void)
{

}

void audio_icsd_adt_start()
{
    struct speak_to_chat_t *hdl = speak_to_chat_hdl;
    if (hdl->last_anc_state == ANC_ON) {
        //启动,变量优化
        icsd_acoustic_detector_resume(RESUME_ANCMODE, ADT_ANC_ON);
    } else if (hdl->last_anc_state == ANC_TRANSPARENCY) {
        icsd_acoustic_detector_resume(RESUME_BYPASSMODE, ADT_ANC_OFF);
    } else { /*if (hdl->last_anc_state == ANC_OFF)*/
        icsd_acoustic_detector_resume(RESUME_ANCMODE, ADT_ANC_OFF);
    }
}

//获取MIC 类型和数据的对应关系
void icsd_mic_ch_sel(struct icsd_acoustic_detector_infmt *infmt)
{
    struct speak_to_chat_t *hdl = speak_to_chat_hdl;

    infmt->mic0_type = ICSD_ANC_MIC_NULL;
    infmt->mic1_type = ICSD_ANC_MIC_NULL;
    infmt->mic2_type = ICSD_ANC_MIC_NULL;
    infmt->mic3_type = ICSD_ANC_MIC_NULL;
    u8 talk_mic_ch = icsd_get_talk_mic_ch();
    u8 ff_mic_ch = icsd_get_ref_mic_L_ch();
    u8 fb_mic_ch = icsd_get_fb_mic_L_ch();

    /*免摘使用3mic时，talk mic ch读取通话的配置，ff和fb使用anc的配置*/
    if (audio_get_mic_num(hdl->libfmt.mic_type) == 3) {
        /*判断talk mic*/
        switch (talk_mic_ch) {
        case AUDIO_ADC_MIC(0):
            infmt->mic0_type = ICSD_ANC_TALK_MIC;
            break;
        case AUDIO_ADC_MIC(1):
            infmt->mic1_type = ICSD_ANC_TALK_MIC;
            break;
        case AUDIO_ADC_MIC(2):
            infmt->mic2_type = ICSD_ANC_TALK_MIC;
            break;
        case AUDIO_ADC_MIC(3):
            infmt->mic3_type = ICSD_ANC_TALK_MIC;
            break;
        }
    }

    /*无论tws还是头戴式立体声都是用左通道*/
    /*判断ff mic*/
    switch (ff_mic_ch) {
    case AUDIO_ADC_MIC(0):
        infmt->mic0_type = ICSD_ANC_LFF_MIC;
        break;
    case AUDIO_ADC_MIC(1):
        infmt->mic1_type = ICSD_ANC_LFF_MIC;
        break;
    case AUDIO_ADC_MIC(2):
        infmt->mic2_type = ICSD_ANC_LFF_MIC;
        break;
    case AUDIO_ADC_MIC(3):
        infmt->mic3_type = ICSD_ANC_LFF_MIC;
        break;
    }

    /*判断fb mic*/
    switch (fb_mic_ch) {
    case AUDIO_ADC_MIC(0):
        infmt->mic0_type = ICSD_ANC_LFB_MIC;
        break;
    case AUDIO_ADC_MIC(1):
        infmt->mic1_type = ICSD_ANC_LFB_MIC;
        break;
    case AUDIO_ADC_MIC(2):
        infmt->mic2_type = ICSD_ANC_LFB_MIC;
        break;
    case AUDIO_ADC_MIC(3):
        infmt->mic3_type = ICSD_ANC_LFB_MIC;
        break;
    }

}

//将coeff/gain 等信息更新给免摘 : 触发
void set_icsd_adt_param_updata()
{
    struct speak_to_chat_t *hdl = speak_to_chat_hdl;
    if (hdl && hdl->state) {
        hdl->adt_param_updata = 1;
    }
}

//将coeff/gain 等信息更新给免摘 : 执行
int audio_acoustic_detector_updata()
{
    struct speak_to_chat_t *hdl = speak_to_chat_hdl;
    if (hdl && hdl->state) {
        hdl->busy = 1;
        if (hdl->infmt.lfb_coeff) {
            free(hdl->infmt.lfb_coeff);
            hdl->infmt.lfb_coeff = NULL;
        }
        if (hdl->infmt.lff_coeff) {
            free(hdl->infmt.lff_coeff);
            hdl->infmt.lff_coeff = NULL;
        }
        if (hdl->infmt.ltrans_coeff) {
            free(hdl->infmt.ltrans_coeff);
            hdl->infmt.ltrans_coeff = NULL;
        }
        if (hdl->infmt.ltransfb_coeff) {
            free(hdl->infmt.ltransfb_coeff);
            hdl->infmt.ltransfb_coeff = NULL;
        }
        /*获取anc参数和增益*/
        /*获取anc参数和增益*/
        hdl->infmt.gains_l_fbgain = get_anc_gains_l_fbgain();
        hdl->infmt.gains_l_ffgain = get_anc_gains_l_ffgain();
        hdl->infmt.gains_l_transgain = get_anc_gains_l_transgain();
        hdl->infmt.gains_l_transfbgain = get_anc_gains_lfb_transgain();
        hdl->infmt.lfb_yorder     = get_anc_l_fbyorder();
        hdl->infmt.lff_yorder     = get_anc_l_ffyorder();
        hdl->infmt.ltrans_yorder  = get_anc_l_transyorder();
        hdl->infmt.ltransfb_yorder  = get_anc_lfb_transyorder();
        hdl->infmt.trans_alogm    = get_anc_gains_trans_alogm();
        hdl->infmt.alogm          = get_anc_gains_alogm();

        hdl->infmt.lfb_coeff      = zalloc(hdl->infmt.lfb_yorder * 40);
        hdl->infmt.lff_coeff      = zalloc(hdl->infmt.lff_yorder * 40);;
        hdl->infmt.ltrans_coeff      = zalloc(hdl->infmt.ltrans_yorder * 40);;
        hdl->infmt.ltransfb_coeff      = zalloc(hdl->infmt.ltransfb_yorder * 40);;

        memcpy(hdl->infmt.lfb_coeff, get_anc_lfb_coeff(), hdl->infmt.lfb_yorder * 40);
        memcpy(hdl->infmt.lff_coeff, get_anc_lff_coeff(), hdl->infmt.lff_yorder * 40);
        /* memcpy(hdl->infmt.ltrans_coeff, get_anc_ltrans_coeff(), hdl->infmt.ltrans_yorder * 40); */
        /* memcpy(hdl->infmt.ltransfb_coeff, get_anc_ltrans_fb_coeff(), hdl->infmt.ltransfb_yorder * 40); */

        extern u32 get_anc_gains_sign();
        hdl->infmt.gain_sign = get_anc_gains_sign();

        icsd_acoustic_detector_infmt_updata(&hdl->infmt);
        hdl->busy = 0;
    }

    return 0;
}

#if (ICSD_ADT_WIND_INFO_SPP_DEBUG_EN || ICSD_ADT_VOL_NOISE_LVL_SPP_DEBUG_EN)
/*记录spp连接状态*/
void icsd_adt_spp_connect_state_cbk(u8 state)
{
    struct speak_to_chat_t *hdl = speak_to_chat_hdl;
    if (hdl && hdl->state) {
        hdl->spp_connected_state = state;
    }
}
#endif

/*设置talk mic gain和ff mic gain一样*/
void audio_icsd_adt_set_talk_mic_gain(audio_mic_param_t *mic_param)
{
    u8 talk_mic_ch = icsd_get_talk_mic_ch();
    u16 talk_mic_gain = audio_anc_ffmic_gain_get();
    adt_log("talk_mic_ch %d, gain %d", talk_mic_ch, talk_mic_gain);

    for (int i = 0; i < AUDIO_ADC_MAX_NUM; i++) {
        if (talk_mic_ch == AUDIO_ADC_MIC(i)) {
            mic_param->mic_gain[i] = talk_mic_gain;
            break;
        }
    }
}

//注册给DAC，用于当DAC 设置采样率时通知ADT算法做对应处理
void audio_icsd_adt_set_sample(int sample_rate)
{
    struct speak_to_chat_t *hdl = speak_to_chat_hdl;
    //DAC设置采样率时会重新申请buffer，DAC读指针需重新复位
    audio_dac_read_anc_reset();
    if (hdl && hdl->state) {
        if (hdl->sample_rate != sample_rate) {
            adt_log("%s:%d", __func__, __LINE__);
            icsd_adt_set_audio_sample_rate(sample_rate);
        }
        hdl->sample_rate = sample_rate;
    }
}

/*获取免摘需要多少个mic*/
//gali 根据使能的接口获取算法需要开多少个MIC，目前没有独立风噪，这里需要修改
u8 get_icsd_adt_mic_num()
{
    return 3;
#if 0
    //需要根据对应功能获取这里需要多少个MIC
    if ((ICSD_WIND_EN) || ICSD_ENVNL_EN) {
        //独立风噪模式,没有免摘，只需2个 MIC
        return 2;
    } else {
        return 3;
    }
#endif
}

u8 get_icsd_adt_mic_ch(struct icsd_acoustic_detector_libfmt *libfmt)
{
    if (!libfmt) {
        return 0;
    }
    u16 mic_type = libfmt->mic_type;
    u16 mic_ch = 0;
    if (ADT_REFMIC_L & mic_type) {
        /* adt_log("ADT_REFMIC_L\n"); */
        mic_ch |= icsd_get_ref_mic_L_ch();
    }
    if (ADT_REFMIC_R & mic_type) {
        /* adt_log("ADT_REFMIC_R\n"); */
        mic_ch |= icsd_get_ref_mic_R_ch();
    }
    if (ADT_ERRMIC_L & mic_type) {
        /* adt_log("ADT_ERRMIC_L\n"); */
        mic_ch |= icsd_get_fb_mic_L_ch();
    }
    if (ADT_ERRMIC_R & mic_type) {
        /* adt_log("ADT_ERRMIC_R\n"); */
        mic_ch |= icsd_get_fb_mic_R_ch();
    }
    if (ADT_TLKMIC_L & mic_type) {
        /* adt_log("ADT_TLKMIC_L\n"); */
        mic_ch |= icsd_get_talk_mic_ch();
    }
    if (ADT_TLKMIC_R & mic_type) {
        /* adt_log("ADT_TLKMIC_R\n"); */
        mic_ch |= icsd_get_talk_mic_ch();
    }
    return mic_ch;
}

/*打开智能免摘检测*/
int audio_acoustic_detector_open()
{
    printf("audio_acoustic_detector_open\n");
    struct speak_to_chat_t *hdl = speak_to_chat_hdl;
    if (hdl == NULL) {
        return -1;
    }

    /*修改了sniff的唤醒间隔*/
    icsd_set_tws_t_sniff(160);
    //判定RTANC 模式
    int adt_function = adt_info.adt_mode;

#if TCFG_AUDIO_ANC_ENV_ADAPTIVE_GAIN_ENABLE
    if (adt_info.adt_mode & ADT_REAL_TIME_ADAPTIVE_ANC_TIDY_MODE) {
        adt_function &= (~ADT_REAL_TIME_ADAPTIVE_ANC_TIDY_MODE);
        adt_function |= ADT_REAL_TIME_ADAPTIVE_ANC_MODE;
        hdl->libfmt.rtanc_type = tidy_rtanc;
    } else {
        hdl->libfmt.rtanc_type = norm_rtanc;
    }
    if (adt_function & ADT_REAL_TIME_ADAPTIVE_ANC_MODE) {
        //检查到RTANC打开限制，清除RTANC相关模式
        int ret = audio_rtanc_adaptive_init(1);
        if (ret && ret != ANC_EXT_OPEN_FAIL_REENTRY) {	//非重入异常则退出
            adt_info.adt_mode &= ~(ADT_REAL_TIME_ADAPTIVE_ANC_TIDY_MODE | ADT_REAL_TIME_ADAPTIVE_ANC_MODE);
            if (adt_function == ADT_REAL_TIME_ADAPTIVE_ANC_MODE) {
                return -1;
            } else {
                adt_function &= ~(ADT_REAL_TIME_ADAPTIVE_ANC_MODE);
                hdl->libfmt.rtanc_type = norm_rtanc;
            }
        }
    }
#endif

    //启动需要初始化DAC READ相关变量， 避免使用上次遗留参数
    audio_dac_read_anc_reset();
    audio_dac_set_sample_rate_callback(&dac_hdl, audio_icsd_adt_set_sample);
    /*初始化dac read的资源*/
    audio_dac_read_anc_init();

#if ICSD_ADT_SHARE_ADC_ENABLE
    int debug_adc_sr = 16000;
#else
    int debug_adc_sr = 8000;
#endif

#if TCFG_AUDIO_ADAPTIVE_DCC_ENABLE  //RTANC & ADJDCC
    if (adt_function & ADT_REAL_TIME_ADAPTIVE_ANC_MODE) {
        adt_function |= ADT_ADJDCC_EN;
    }
#endif

    hdl->libfmt.adc_sr = debug_adc_sr;//Raymond MIC的采样率由外部决定，通过set函数通知ADT
    icsd_acoustic_detector_get_libfmt(&hdl->libfmt, adt_function);

#if (ICSD_ADT_WIND_INFO_SPP_DEBUG_EN || ICSD_ADT_VOL_NOISE_LVL_SPP_DEBUG_EN)
    /*获取spp发送句柄*/
    if (adt_info.adt_mode & (ADT_WIND_NOISE_DET_MODE | ADT_ENV_NOISE_DET_MODE)) {
        spp_get_operation_table(&hdl->spp_opt);
        if (hdl->spp_opt) {
            /*设置记录spp连接状态回调*/
            hdl->spp_opt->regist_state_cbk(hdl, icsd_adt_spp_connect_state_cbk);
            adt_log("icsd adt spp debug open \n");
        }
    }
#endif

    /*获取需要打开的mic通道*/
    u16 mic_ch = get_icsd_adt_mic_ch(&hdl->libfmt);
    u16 talk_mic_ch = icsd_get_talk_mic_ch();
    u16 ref_mic_l_ch = icsd_get_ref_mic_L_ch();
    u16 fb_mic_l_ch = icsd_get_fb_mic_L_ch();

    g_printf("mode : %x, mic_type : %x, mic_ch : %x, adc_len : %d, sr : %d, size : %d, rtanc_type %d", \
             adt_function, hdl->libfmt.mic_type, mic_ch, hdl->libfmt.adc_isr_len, hdl->libfmt.adc_sr, \
             hdl->libfmt.lib_alloc_size, hdl->libfmt.rtanc_type);
    if (hdl->libfmt.lib_alloc_size) {
        hdl->lib_alloc_ptr = zalloc(hdl->libfmt.lib_alloc_size);
    } else {
        return -1;;
    }
    if (hdl->lib_alloc_ptr == NULL) {
        printf("hdl->lib_alloc_ptr malloc fail !!!");
        return -1;
    }

#if TCFG_AUDIO_ANC_HOWLING_DET_ENABLE
    if (adt_function & ADT_HOWLING_DET_MODE) {
        /*初始化啸叫检测资源*/
        icsd_anc_soft_howling_det_init();
    }
#endif

    if (mic_input_v2 && mic_ch) {
        u8 bit_width_offset = 1;
#if ICSD_ADT_SHARE_ADC_ENABLE
        if (adc_hdl.bit_width != ADC_BIT_WIDTH_16) {
            bit_width_offset = 2;
        }
#endif
        /*复用adc时，adc的数据需要额外申请buf存起来*/
        if (mic_ch & talk_mic_ch) {
            hdl->talk_mic_buf = zalloc(hdl->libfmt.adc_isr_len * sizeof(short) * bit_width_offset);
        }
        if (mic_ch & ref_mic_l_ch) {
            hdl->ff_mic_buf = zalloc(hdl->libfmt.adc_isr_len * sizeof(short) * bit_width_offset);
        }
        if (mic_ch & fb_mic_l_ch) {
            hdl->fb_mic_buf = zalloc(hdl->libfmt.adc_isr_len * sizeof(short) * bit_width_offset);
        }
    }

    /*配置TWS还是头戴式耳机*/
#if TCFG_USER_TWS_ENABLE
    hdl->infmt.adt_mode = ADT_TWS;
#else
    hdl->infmt.adt_mode = ADT_HEADSET;
#endif /*TCFG_USER_TWS_ENABLE*/

#if TCFG_AUDIO_ANC_EXT_TOOL_ENABLE
    hdl->infmt.TOOL_FUNCTION = anc_ext_debug_tool_function_get();
#endif
    /*配置mic通道*/
    icsd_mic_ch_sel(&hdl->infmt);
    /* hdl->infmt.mic0_type = ICSD_ANC_MIC_NULL; */
    /* hdl->infmt.mic1_type = ICSD_ANC_LFF_MIC; */
    /* hdl->infmt.mic2_type = ICSD_ANC_LFB_MIC; */
    /* hdl->infmt.mic3_type = ICSD_ANC_MIC_NULL; */
    adt_log("mic0_type: %d, mic1_type: %d, mic2_type: %d, mic3_type: %d", hdl->infmt.mic0_type, hdl->infmt.mic1_type, hdl->infmt.mic2_type, hdl->infmt.mic3_type);
    /*检测灵敏度
     * 0 : 正常灵敏度
     * 1 : 智能灵敏度*/
    hdl->infmt.sensitivity = 0;
    /*配置低压/高压DAC*/
#if ((TCFG_AUDIO_DAC_MODE == DAC_MODE_L_DIFF) || (TCFG_AUDIO_DAC_MODE == DAC_MODE_L_SINGLE))
    hdl->infmt.dac_mode = ADT_DACMODE_LOW;
#else
    hdl->infmt.dac_mode = ADT_DACMODE_HIGH;
#endif /*TCFG_AUDIO_DAC_MODE*/
    /*获取anc参数和增益*/
    hdl->infmt.gains_l_fbgain = get_anc_gains_l_fbgain();
    hdl->infmt.gains_l_ffgain = get_anc_gains_l_ffgain();
    hdl->infmt.gains_l_transgain = get_anc_gains_l_transgain();
    hdl->infmt.gains_l_transfbgain = get_anc_gains_lfb_transgain();
    hdl->infmt.lfb_yorder     = get_anc_l_fbyorder();
    hdl->infmt.lff_yorder     = get_anc_l_ffyorder();
    hdl->infmt.ltrans_yorder  = get_anc_l_transyorder();
    hdl->infmt.ltransfb_yorder  = get_anc_lfb_transyorder();
    hdl->infmt.trans_alogm    = get_anc_gains_trans_alogm();
    hdl->infmt.alogm          = get_anc_gains_alogm();

    hdl->infmt.lfb_coeff      = zalloc(hdl->infmt.lfb_yorder * 40);
    hdl->infmt.lff_coeff      = zalloc(hdl->infmt.lff_yorder * 40);;
    hdl->infmt.ltrans_coeff      = zalloc(hdl->infmt.ltrans_yorder * 40);;
    hdl->infmt.ltransfb_coeff      = zalloc(hdl->infmt.ltransfb_yorder * 40);;

    memcpy(hdl->infmt.lfb_coeff, get_anc_lfb_coeff(), hdl->infmt.lfb_yorder * 40);
    memcpy(hdl->infmt.lff_coeff, get_anc_lff_coeff(), hdl->infmt.lff_yorder * 40);
    /* memcpy(hdl->infmt.ltrans_coeff, get_anc_ltrans_coeff(), hdl->infmt.ltrans_yorder * 40); */
    /* memcpy(hdl->infmt.ltransfb_coeff, get_anc_ltrans_fb_coeff(), hdl->infmt.ltransfb_yorder * 40); */

    extern u32 get_anc_gains_sign();
    hdl->infmt.gain_sign = get_anc_gains_sign();

    hdl->infmt.alloc_ptr = hdl->lib_alloc_ptr;
    /*数据输出回调*/
    /* hdl->infmt.output_hdl = audio_acoustic_detector_output_hdl; */
    hdl->infmt.anc_dma_post_msg = audio_acoustic_detector_anc_dma_post_msg;
    hdl->infmt.anc_dma_output_callback = audio_acoustic_detector_anc_dma_output_callback;
    hdl->infmt.ff_gain = audio_anc_ffmic_gain_get();
    hdl->infmt.fb_gain = audio_anc_fbmic_gain_get();
    /* hdl->infmt.sample_rate = 16000;//Raymond debug */
    hdl->infmt.sample_rate = 44100;//Raymond debug
    hdl->infmt.ein_state = 0;
    hdl->infmt.adc_sr = debug_adc_sr;//Raymond
    adt_log("ff_gain %d, fb_gain %d", hdl->infmt.ff_gain, hdl->infmt.fb_gain);

#if AUDIO_RT_ANC_EXPORT_TOOL_DATA_DEBUG
    if (anc_ext_tool_online_get()) {
        hdl->infmt.rtanc_tool = zalloc(sizeof(struct icsd_rtanc_tool_data));;
        adt_log("rtanc tool demo:0x%x==================================\n", (int)hdl->infmt.rtanc_tool);
    }
#endif
    icsd_acoustic_detector_set_infmt(&hdl->infmt);
    //set_icsd_adt_dma_done_flag(1);

    extern void icsd_task_create();
    icsd_task_create();
    icsd_acoustic_detector_open();

    ASSERT(audio_get_mic_num(mic_ch) == audio_get_mic_num(hdl->libfmt.mic_type), "adt need %d mic num, but now only open %d mic num, please check your anc mic and esco mic cfg !!!", audio_get_mic_num(hdl->libfmt.mic_type), audio_get_mic_num(mic_ch));
    audio_mic_param_t mic_param = {
        .mic_ch_sel        = mic_ch,
        .sample_rate       = hdl->libfmt.adc_sr,//采样率
        .adc_irq_points    = hdl->libfmt.adc_isr_len,//一次处理数据的数据单元， 单位点 4对齐(要配合mic起中断点数修改)
        .adc_buf_num       = 3,
    };
    for (int i = 0; i < AUDIO_ADC_MAX_NUM; i++) {
        mic_param.mic_gain[i] = audio_anc_mic_gain_get(i);
    }

    if (mic_ch & talk_mic_ch) {
        /*免摘使用3mic时，设置talk mic gain和ff mic gain一样*/
        audio_icsd_adt_set_talk_mic_gain(&mic_param);
    }

    hdl->adc_seq = get_adc_seq(&adc_hdl, mic_ch); //查询模拟mic对应的ADC通道,要求通道连续
    /* if (((adt_info.adt_mode != ADT_REAL_TIME_ADAPTIVE_ANC_TIDY_MODE) && \ */
    /*      (adt_info.adt_mode != ADT_REAL_TIME_ADAPTIVE_ANC_MODE)) || \ */
    /*     (adt_function & ADT_ADJDCC_EN)) { */
    if (mic_ch) {
        int err = audio_mic_en(1, &mic_param, audio_mic_output_handle);
        if (err != 0) {
            printf("open mic fail !!!");
            goto err0;
        }
    }
    if (mic_input_v2) {
        if (mic_ch & talk_mic_ch) {
            hdl->talk_mic_seq = get_adc_seq(&adc_hdl, talk_mic_ch); //查询模拟mic对应的ADC通道,要求通道连续
        }
        if (mic_ch & ref_mic_l_ch) {
            hdl->ff_mic_seq = get_adc_seq(&adc_hdl, ref_mic_l_ch);
        }
        if (mic_ch & fb_mic_l_ch) {
            hdl->fb_mic_seq = get_adc_seq(&adc_hdl, fb_mic_l_ch);
        }
        adt_log("adc seq, talk : %d, ff : %d, fb : %d\n", hdl->talk_mic_seq, hdl->ff_mic_seq, hdl->fb_mic_seq);
    }
    audio_icsd_adt_start();

    hdl->state = 1;
    /*用于TWS同步adt状态*/
    if (adt_info.speak_to_chat_state == AUDIO_ADT_CHAT) {
#if TCFG_AUDIO_SPEAK_TO_CHAT_ENABLE
        audio_speak_to_chat_voice_state_sync();
#endif
        hdl->adt_resume = 0;
    }
    adt_info.speak_to_chat_state = AUDIO_ADT_OPEN;
    hdl->speak_to_chat_state = AUDIO_ADT_OPEN;
    return 0;
err0:
    audio_acoustic_detector_close();
    return -1;
}

/*关闭智能免摘检测*/
int audio_acoustic_detector_close()
{
    struct speak_to_chat_t *hdl = speak_to_chat_hdl;
    if (hdl) {
        audio_dac_del_sample_rate_callback(&dac_hdl, audio_icsd_adt_set_sample);
        audio_mic_en(0, NULL, NULL);
        //set_icsd_adt_dma_done_flag(0);
        if (hdl->lib_alloc_ptr) {
            /*需要先挂起再关闭*/
            icsd_acoustic_detector_suspend();
            icsd_acoustic_detector_close();
            extern void icsd_task_kill();
            icsd_task_kill();

            if (hdl->infmt.lfb_coeff) {
                free(hdl->infmt.lfb_coeff);
                hdl->infmt.lfb_coeff = NULL;
            }
            if (hdl->infmt.lff_coeff) {
                free(hdl->infmt.lff_coeff);
                hdl->infmt.lff_coeff = NULL;
            }
            if (hdl->infmt.ltrans_coeff) {
                free(hdl->infmt.ltrans_coeff);
                hdl->infmt.ltrans_coeff = NULL;
            }
            if (hdl->infmt.ltransfb_coeff) {
                free(hdl->infmt.ltransfb_coeff);
                hdl->infmt.ltransfb_coeff = NULL;
            }

            free(hdl->lib_alloc_ptr);
            hdl->lib_alloc_ptr = NULL;
        }
        if (mic_input_v2) {
            if (hdl->talk_mic_buf) {
                free(hdl->talk_mic_buf);
                hdl->talk_mic_buf = NULL;
            }
            if (hdl->ff_mic_buf) {
                free(hdl->ff_mic_buf);
                hdl->ff_mic_buf = NULL;
            }
            if (hdl->fb_mic_buf) {
                free(hdl->fb_mic_buf);
                hdl->fb_mic_buf = NULL;
            }
        }
#if AUDIO_RT_ANC_EXPORT_TOOL_DATA_DEBUG
        if (hdl->infmt.rtanc_tool) {
            free(hdl->infmt.rtanc_tool);
            hdl->infmt.rtanc_tool = NULL;
        }
#endif
        /*释放dac read的资源*/
        audio_dac_read_anc_exit();
    }
    return 0;
}

//触发免摘之后对DAC mute 的动作
/*dac数据清零 mute*/
void audio_adt_dac_check_slience_cb(void *buf, int len)
{
    /* memset((u8 *)buf, 0x0, len); */
}
void audio_adt_dac_mute_start(void *priv)
{
    /* dac_node_write_callback_add("ANC_ADT", 0XFF, audio_adt_dac_check_slience_cb);	//注册DAC回调接口-静音检测 */
}
void audio_adt_dac_mute_stop(void)
{
    /* dac_node_write_callback_del("ANC_ADT"); */
}


//免摘触发切模式行为
void adt_anc_mode_switch(u8 mode, u8 tone_play)
{
    struct speak_to_chat_t *hdl = speak_to_chat_hdl;
    if (hdl) {
        anc_mode_switch(ANC_ON, 0);
    }
}


//普通挂起行为
void audio_icsd_adt_suspend()
{
    struct speak_to_chat_t *hdl = speak_to_chat_hdl;
    if (hdl && hdl->state) {
        adt_log("%s : %d", __func__, __LINE__);
        icsd_acoustic_detector_suspend();
        hdl->adt_suspend = 0;
        /*删除定时器*/
        if (hdl->timer) {
            sys_s_hi_timeout_del(hdl->timer);
            hdl->timer = 0;
        }
    }
}

/*char 定时结束后从通透同步恢复anc on /anc off*/
//1、a2dp player 点击播歌,挂起免摘，恢复到ANC ON/OFF
void audio_speak_to_char_suspend(void)
{
    struct speak_to_chat_t *hdl = speak_to_chat_hdl;
    int err = 0;
    if (hdl && hdl->state) {
        adt_log("%s : %d", __func__, __LINE__);
        /*tws同步suspend时，防止二次调用*/
        if (hdl->adt_suspend) {
            return;
        }
        hdl->busy = 1;
        hdl->adt_suspend = 1;
        adt_log("%s : %d", __func__, hdl->last_anc_state);
        /*tws同步后，删除定时器*/
        if (hdl->timer) {
            sys_s_hi_timeout_del(hdl->timer);
            hdl->timer = 0;
        }
        hdl->speak_to_chat_state = AUDIO_ADT_OPEN;
        adt_info.speak_to_chat_state = AUDIO_ADT_OPEN;
        icsd_acoustic_detector_suspend();
        /*播放结束提示音的标志*/
        hdl->anc_switch_flag = 1;
        if (hdl->last_anc_state == ANC_ON) {
            if (anc_mode_get() == ANC_ON) {
                icsd_acoustic_detector_resume(RESUME_ANCMODE, ADT_ANC_ON);
#if SPEAK_TO_CHAT_PLAY_TONE_EN
                /*免摘结束退出通透提示音*/
                err = os_taskq_post_msg(SPEAK_TO_CHAT_TASK_NAME, 2, ICSD_ADT_TONE_PLAY, (int)ICSD_ADT_TONE_NUM0);
#else
                hdl->adt_suspend = 0;
#endif /*SPEAK_TO_CHAT_PLAY_TONE_EN*/
                hdl->anc_switch_flag = 0;
            } else {
                /* anc_mode_switch(ANC_ON, 0); */
                err = os_taskq_post_msg(SPEAK_TO_CHAT_TASK_NAME, 3, ICSD_ANC_MODE_SWITCH, ANC_ON, 0);
            }
        } else if (hdl->last_anc_state == ANC_TRANSPARENCY) {
            if (anc_mode_get() == ANC_TRANSPARENCY) {
                icsd_acoustic_detector_resume(RESUME_BYPASSMODE, ADT_ANC_OFF);
#if SPEAK_TO_CHAT_PLAY_TONE_EN
                /*免摘结束退出通透提示音*/
                err = os_taskq_post_msg(SPEAK_TO_CHAT_TASK_NAME, 2, ICSD_ADT_TONE_PLAY, (int)ICSD_ADT_TONE_NUM0);
#else
                hdl->adt_suspend = 0;
#endif /*SPEAK_TO_CHAT_PLAY_TONE_EN*/
                hdl->anc_switch_flag = 0;
            } else {
                /* anc_mode_switch(ANC_TRANSPARENCY, 0); */
                err = os_taskq_post_msg(SPEAK_TO_CHAT_TASK_NAME, 3, ICSD_ANC_MODE_SWITCH, ANC_TRANSPARENCY, 0);
            }
        } else { /*if (hdl->last_anc_state == ANC_OFF*/
            if (anc_mode_get() == ANC_OFF) {
                icsd_acoustic_detector_resume(RESUME_ANCMODE, ADT_ANC_OFF);
#if SPEAK_TO_CHAT_PLAY_TONE_EN
                /*免摘结束退出通透提示音*/
                err = os_taskq_post_msg(SPEAK_TO_CHAT_TASK_NAME, 2, ICSD_ADT_TONE_PLAY, (int)ICSD_ADT_TONE_NUM0);
#else
                hdl->adt_suspend = 0;
#endif /*SPEAK_TO_CHAT_PLAY_TONE_EN*/
                hdl->anc_switch_flag = 0;
            } else {
                adt_open_in_anc = 1;
                /* anc_mode_switch(ANC_OFF, 0); */
                err = os_taskq_post_msg(SPEAK_TO_CHAT_TASK_NAME, 3, ICSD_ANC_MODE_SWITCH, ANC_OFF, 0);
            }
        }

        audio_adt_dac_mute_stop();
        if (hdl->a2dp_state && (bt_a2dp_get_status() != BT_MUSIC_STATUS_STARTING)) {
            adt_log("send PLAY cmd");
            bt_cmd_prepare(USER_CTRL_AVCTP_OPID_PLAY, 0, NULL);
        }
        hdl->a2dp_state = 0;
        hdl->busy = 0;
    }
}

void audio_speak_to_char_sync_suspend(void)
{
    adt_log("%s", __func__);
    struct speak_to_chat_t *hdl = speak_to_chat_hdl;
    u8 data[4];
    if (hdl && hdl->state) {
        hdl->adt_suspend = 0;
#if TCFG_USER_TWS_ENABLE
        if (get_tws_sibling_connect_state()) {
            data[0] = SYNC_ICSD_ADT_SUSPEND;
            audio_icsd_adt_info_sync(data, 4);
        } else {
            audio_speak_to_char_suspend();
        }
#else
        audio_speak_to_char_suspend();
#endif/*TCFG_USER_TWS_ENABLE*/
    }
}

//suspend  记录ANC的状态, 免摘进入通透  定时器相关处理
void audio_anc_mode_switch_in_adt(u8 anc_mode)
{
    struct speak_to_chat_t *hdl = speak_to_chat_hdl;
    if (hdl && hdl->state) {
        icsd_acoustic_detector_suspend();
        if (anc_mode == ANC_TRANSPARENCY && hdl->adt_resume) {
            /*说话触发的通透不记录*/
            return;
        }
        hdl->busy = 1;
        adt_log("%s : %d", __func__, __LINE__);
        /*每次切anc模式都在resume前更新参数*/
        set_icsd_adt_param_updata();
        /*进入免摘后，如果切换anc模式主动退出免摘*/
        if (hdl->speak_to_chat_state == AUDIO_ADT_CHAT) {
            /* hdl->adt_suspend = 0; */
            /*删除定时器*/
            if (hdl->timer) {
                sys_s_hi_timeout_del(hdl->timer);
                hdl->timer = 0;
            }
        }
        if ((anc_mode == ANC_OFF) && (hdl->speak_to_chat_state != AUDIO_ADT_CLOSE)) {
            adt_open_in_anc = 1;
        }
        /*记录外部切换的anc模式*/
        hdl->last_anc_state = anc_mode;
        hdl->busy = 0;
    }
}

//触发免摘之后恢复至默认状态的定时器
void audio_speak_to_chat_timer(void *p)
{
    struct speak_to_chat_t *hdl = speak_to_chat_hdl;
    if (hdl && hdl->state) {
        adt_log("%s", __func__);
        if (hdl->timer) {
            sys_s_hi_timeout_del(hdl->timer);
        }
        hdl->timer = 0;
        audio_speak_to_char_sync_suspend();
    }
}

/*切换anc 或者通透完成后，恢复免摘*/
void audio_icsd_adt_resume_timer(void *p)
{
    struct speak_to_chat_t *hdl = speak_to_chat_hdl;
    if (hdl && hdl->state) {
        hdl->busy = 1;
        adt_log("%s : %d", __func__, __LINE__);
        /*更新参数*/
        if (hdl->adt_param_updata) {
            hdl->adt_param_updata = 0;
            audio_acoustic_detector_updata();
        }
        /*adt挂起完成，准备退出挂起*/
        hdl->adt_suspend = 0;
        if (hdl->adt_resume) {
            /*智能免摘切换通透挂起恢复*/
            if (anc_mode_get() == ANC_TRANSPARENCY) {
                icsd_acoustic_detector_resume(RESUME_BYPASSMODE, ADT_ANC_OFF);
            }
            hdl->adt_resume = 0;
        } else {
            /*智能免摘定时结束触发的挂起恢复*/
            if (anc_mode_get() == ANC_ON) {
                icsd_acoustic_detector_resume(RESUME_ANCMODE, ADT_ANC_ON);
            } else if (anc_mode_get() == ANC_TRANSPARENCY) {
                icsd_acoustic_detector_resume(RESUME_BYPASSMODE, ADT_ANC_OFF);
            } else { /*if (anc_mode_get() == ANC_OFF*/
                icsd_acoustic_detector_resume(RESUME_ANCMODE, ADT_ANC_OFF);
            }
            hdl->speak_to_chat_state = AUDIO_ADT_OPEN;
            adt_info.speak_to_chat_state = AUDIO_ADT_OPEN;
        }
        hdl->adt_resume_timer = 0;
        hdl->busy = 0;
    }
}

//普通恢复行为
void audio_icsd_adt_resume()
{
    struct speak_to_chat_t *hdl = speak_to_chat_hdl;
    if (hdl && hdl->state) {
        hdl->busy = 1;
        adt_log("%s : %d", __func__, __LINE__);
        adt_open_in_anc = 0;
        /*延时1s，等待ANC数据稳定在恢复*/
        if (hdl->adt_resume_timer == 0) {
            hdl->adt_resume_timer = sys_s_hi_timerout_add(NULL, audio_icsd_adt_resume_timer, 500);
        }
        if (hdl->adt_resume) {
            /*智能免摘定时结束触发的挂起恢复*/
            if (anc_mode_get() == ANC_TRANSPARENCY) {
            }
        } else {
            if (hdl->anc_switch_flag) {
#if SPEAK_TO_CHAT_PLAY_TONE_EN
                /*免摘结束退出通透提示音*/
                icsd_adt_tone_play(ICSD_ADT_TONE_NUM0);
#endif /*SPEAK_TO_CHAT_PLAY_TONE_EN*/
                hdl->anc_switch_flag = 0;
            }
        }
        hdl->busy = 0;
    }
}

u8 audio_speak_to_chat_is_running()
{
    return (speak_to_chat_hdl != NULL);
}

/*adt是否在跑*/
u8 audio_icsd_adt_is_running()
{
    return (speak_to_chat_hdl != NULL);
}

/*获取智能免摘的状态*/
u8 get_speak_to_chat_state()
{
    struct speak_to_chat_t *hdl = speak_to_chat_hdl;
    if (hdl) {
        return hdl->speak_to_chat_state;
    } else {
        return 0;
    }
}

/*获取adt的模式*/
u16 get_icsd_adt_mode()
{
    struct speak_to_chat_t *hdl = speak_to_chat_hdl;
    return adt_info.adt_mode;
}

void icsd_adt_rt_anc_mode_set(int mode)
{
    adt_info.adt_mode |= mode;
}

/*获取ADT模式切换是否繁忙*/
u8 get_icsd_adt_mode_switch_busy()
{
    return adt_info.mode_switch_busy;
}

/*获取进入免摘前anc的模式*/
u8 get_icsd_adt_anc_mode()
{
    struct speak_to_chat_t *hdl = speak_to_chat_hdl;
    if (hdl && hdl->state) {
        return hdl->last_anc_state;
    } else {
        return anc_new_target_mode_get();
    }
}

/*记录adt切换trans的状态*/
u8 set_adt_switch_trans_state(u8 state)
{
    struct speak_to_chat_t *hdl = speak_to_chat_hdl;
    if (hdl && hdl->state) {
        hdl->adt_switch_trans_state = state;
        return hdl->adt_switch_trans_state;
    } else {
        return 0;
    }
}

/*获取adt切换trans的状态*/
u8 get_adt_switch_trans_state()
{
    struct speak_to_chat_t *hdl = speak_to_chat_hdl;
    if (hdl && hdl->state) {
        return hdl->adt_switch_trans_state;
    } else {
        return 0;
    }
}

/*同步前关闭ADT*/
void audio_icsd_adt_state_sync_done(u16 adt_mode, u8 speak_to_chat_state)
{

    int close_adt = ADT_ALL_FUNCTION_ENABLE;
    audio_icsd_adt_res_close(close_adt, 0);

    adt_log("%s, adt %d, chat %d", __func__, adt_mode, speak_to_chat_state);
    struct speak_to_chat_t *hdl = speak_to_chat_hdl;
    adt_info.speak_to_chat_state = speak_to_chat_state;
    if (hdl && hdl->state) {
        /*tws同步时重置记录的anc模式*/
        hdl->last_anc_state = anc_mode_get();
    }
    adt_info.adt_mode = adt_mode;
    /*同步动作前已经关闭adt了，这里只需要判断打开，避免没有开adt在anc off里面开了anc*/
    if (adt_info.adt_mode) {
        /*在anc跑完后面执行同步adt状态*/
        adt_open_in_anc = 1;
    }
}

/*同步tws配对时，同步adt的状态*/
static u8 sync_data[5];
void audio_anc_icsd_adt_state_sync(u8 *data)
{
    printf("audio_anc_icsd_adt_state_sync");
    memcpy(sync_data, data, sizeof(sync_data));
    int err = os_taskq_post_msg(SPEAK_TO_CHAT_TASK_NAME, 2, ICSD_ADT_STATE_SYNC, (int)sync_data);
    if (err != OS_NO_ERR) {
        printf("%s err %d", __func__, err);
    }
}

//ADT 播放完提示音回调接口
void icsd_adt_task_play_tone_cb(void *priv)
{
    struct speak_to_chat_t *hdl = speak_to_chat_hdl;
    /*播放完提示音*/
    if (hdl && hdl->state) {
        //为啥要清0
        hdl->adt_suspend = 0;
    }
}

static void audio_icsd_adt_task(void *p)
{
    printf("%s", __func__);
    struct speak_to_chat_t *hdl = speak_to_chat_hdl;
    int msg[16];
    char tmpbuf[25];
    int res;
    int ret;
    u16 anc_fade_gain;

    while (1) {
        res = os_taskq_pend("taskq", msg, ARRAY_SIZE(msg));
        if (res == OS_TASKQ) {
            switch (msg[1]) {
#if TCFG_AUDIO_SPEAK_TO_CHAT_ENABLE
            case ICSD_ADT_VOICE_STATE:
                hdl = speak_to_chat_hdl;
                if (hdl && hdl->state) {
                    hdl->busy = 1;
                    /* hdl->adt_resume == 0 ：上一次检测到语音，挂起切换通透还没完成恢复的时候，不能做下一次动作
                     * hdl->adt_suspend == 0 : 结束定时挂起切换anc模式的时候，还没完成模式切换，不能做检测作动*/
                    if (hdl->voice_state && (hdl->adt_resume == 0) && (hdl->adt_suspend == 0)) {
                        if (hdl->speak_to_chat_state == AUDIO_ADT_CHAT) {
                            adt_log("hdl->speak_to_chat_state == AUDIO_ADT_CHAT)");
                        } else {
                            adt_log("%s", __func__);
                            /*检测到语音切换到通透*/
                            if (anc_mode_get() != ANC_TRANSPARENCY) {
                                hdl->adt_resume = 1;/*调用顺序不可改*/
                                hdl->speak_to_chat_state = AUDIO_ADT_OPEN;
                                adt_info.speak_to_chat_state = AUDIO_ADT_OPEN;
                                icsd_acoustic_detector_suspend();
                                set_adt_switch_trans_state(1);
                                anc_mode_switch(ANC_TRANSPARENCY, 0);
                            }

                            /*如果在播歌暂停播歌*/
                            if (bt_a2dp_get_status() == BT_MUSIC_STATUS_STARTING) {
                                adt_log("send PAUSE cmd");
                                hdl->a2dp_state = 1;
                                bt_cmd_prepare(USER_CTRL_AVCTP_OPID_PAUSE, 0, NULL);
                            }
#if SPEAK_TO_CHAT_PLAY_TONE_EN
                            /*结束免摘后检测播放提示音*/
                            icsd_adt_tone_play(ICSD_ADT_TONE_NORMAL);
#endif /*SPEAK_TO_CHAT_PLAY_TONE_EN*/
                            /* tone_play_index_with_callback(IDEX_TONE_NORMAL, 1, audio_adt_dac_mute_start, NULL); */
                        }
#if SPEAK_TO_CHAT_TEST_TONE_EN
                        /*每次检测到说话都播放提示音*/
                        icsd_adt_tone_play(ICSD_ADT_TONE_NORMAL);
#endif /*SPEAK_TO_CHAT_TEST_TONE_EN*/

                        /*重新定时*/
                        if (hdl->timer) {
                            sys_s_hi_timeout_del(hdl->timer);
                            hdl->timer = 0;
                        }
                        hdl->timer = sys_s_hi_timerout_add(NULL, audio_speak_to_chat_timer, adt_info.speak_to_chat_end_time);
                        hdl->speak_to_chat_state = AUDIO_ADT_CHAT;
                        adt_info.speak_to_chat_state = AUDIO_ADT_CHAT;
                        hdl->voice_state = 0;
                    }
                    hdl->busy = 0;
                }
                break;
#endif /*TCFG_AUDIO_SPEAK_TO_CHAT_ENABLE*/
#if TCFG_AUDIO_ANC_WIND_NOISE_DET_ENABLE
            case ICSD_ADT_WIND_LVL:
                hdl = speak_to_chat_hdl;
                if (hdl && hdl->state) {
                    hdl->busy = 1;
                    u8 wind_lvl = (u8)msg[2];
                    hdl->adt_wind_lvl = wind_lvl;
                    static u32 next_period = 0;
                    /*间隔200ms以上发送一次数据*/
                    if (time_after(jiffies, next_period)) {
                        next_period = jiffies + msecs_to_jiffies(200);
                        adt_log("[task]wind_lvl : %d", wind_lvl);
                        /*蓝牙spp发送风噪值*/
#if ICSD_ADT_WIND_INFO_SPP_DEBUG_EN
                        /*spp是否连接 && spp是否初始化 && spp发送是否初始化*/
                        if (hdl->spp_opt && hdl->spp_opt->send_data) {
                            memset(tmpbuf, 0x20, sizeof(tmpbuf));//填充空格
                            sprintf(tmpbuf, "wind lvl:%d\r\n", hdl->adt_wind_lvl);
                            /*关闭蓝牙sniff，再发送数据*/
                            bt_sniff_disable();
#if TCFG_USER_TWS_ENABLE
                            if (get_tws_sibling_connect_state() && (tws_api_get_role() == TWS_ROLE_MASTER)) {
                                /*主机发送*/
                                hdl->spp_opt->send_data(NULL, tmpbuf, sizeof(tmpbuf));
                            } else
#endif /*TCFG_USER_TWS_ENABLE*/
                            {
                                hdl->spp_opt->send_data(NULL, tmpbuf, sizeof(tmpbuf));

                            }
                        }
#endif /*ICSD_ADT_WIND_INFO_SPP_DEBUG_EN*/
                    }
                    /*风噪处理*/
                    ret = audio_anc_wind_noise_process(hdl->adt_wind_lvl);
                    if (ret != -1) {
                        anc_fade_gain = ret;
                        hdl->adt_wind_gain_lvl = ret;
                    }

#if TCFG_AUDIO_ANC_REAL_TIME_ADAPTIVE_ENABLE
                    //触发风噪检测之后需要挂起RTANC
                    if (audio_anc_real_time_adaptive_state_get() && (ret != -1)) {
                        if (anc_fade_gain != 16384) {
                            if (!speak_to_chat_hdl->adt_wind_suspend_rtanc) {
                                speak_to_chat_hdl->adt_wind_suspend_rtanc = 1;
                                audio_anc_real_time_adaptive_suspend();
                            }
                        } else if (speak_to_chat_hdl->adt_wind_suspend_rtanc) {
                            speak_to_chat_hdl->adt_wind_suspend_rtanc = 0;
                            audio_anc_real_time_adaptive_resume();
                        }
                    }
#endif

                    hdl->busy = 0;
                }
                break;
#endif /*TCFG_AUDIO_ANC_WIND_NOISE_DET_ENABLE*/
#if TCFG_AUDIO_WIDE_AREA_TAP_ENABLE
            case ICSD_ADT_WAT_RESULT:
                hdl = speak_to_chat_hdl;
                if (hdl && hdl->state) {
                    hdl->busy = 1;
                    u8 wat_result = (u8)msg[2];
                    adt_log("[task]wat_result : %d", wat_result);
                    hdl->adt_wat_result = wat_result;
                    audio_wat_area_tap_event_handle(hdl->adt_wat_result);
                    hdl->busy = 0;
                }
                break;
#endif /*TCFG_AUDIO_WIDE_AREA_TAP_ENABLE*/
#if TCFG_AUDIO_ANC_ENV_NOISE_DET_ENABLE
            case ICSD_ADT_ENV_NOISE_LVL:
                hdl = speak_to_chat_hdl;
                if (hdl && hdl->state) {
                    hdl->busy = 1;
                    u8 noise_lvl = (u8)msg[2];
                    u8 out_lvl = 0;
                    static u32 next_period = 0;
                    /*间隔200ms以上发送一次数据*/
                    if (time_after(jiffies, next_period)) {
                        next_period = jiffies + msecs_to_jiffies(200);
                        adt_log("[task]vol noise_lvl : %d", noise_lvl);
                        /*蓝牙spp发送风噪值*/
#if ICSD_ADT_VOL_NOISE_LVL_SPP_DEBUG_EN
                        /*spp是否连接 && spp是否初始化 && spp发送是否初始化*/
                        if (hdl->spp_opt && hdl->spp_opt->send_data) {
                            memset(tmpbuf, 0x20, sizeof(tmpbuf));//填充空格
                            sprintf(tmpbuf, "noise lvl:%d\r\n", noise_lvl);
                            /*关闭蓝牙sniff，再发送数据*/
                            bt_sniff_disable();
#if TCFG_USER_TWS_ENABLE
                            if (get_tws_sibling_connect_state() && (tws_api_get_role() == TWS_ROLE_MASTER)) {
                                /*主机发送*/
                                hdl->spp_opt->send_data(NULL, tmpbuf, sizeof(tmpbuf));
                            } else
#endif /*TCFG_USER_TWS_ENABLE*/
                            {
                                hdl->spp_opt->send_data(NULL, tmpbuf, sizeof(tmpbuf));

                            }
                        }
#endif /*ICSD_ADT_WIND_INFO_SPP_DEBUG_EN*/
                    }
#if TCFG_AUDIO_VOLUME_ADAPTIVE_ENABLE
                    audio_icsd_adptive_vol_event_process(noise_lvl);
#endif
#if TCFG_AUDIO_ANC_ENV_ADAPTIVE_GAIN_ENABLE
                    ret = audio_env_noise_event_process(noise_lvl);
                    if (ret != -1) {
                        hdl->adt_wind_gain_lvl = ret;
                        anc_fade_gain = ret;
                    }
#endif

                    hdl->busy = 0;
                }
                break;
#endif /*TCFG_AUDIO_ANC_ENV_NOISE_DET_ENABLE*/
            case ICSD_ADT_TONE_PLAY:
                /*播放提示音,定时器里面需要播放提示音时，可发消息到这里播放*/
                u8 index = (u8)msg[2];
                int err = icsd_adt_tone_play_callback(index, icsd_adt_task_play_tone_cb, NULL);
                if (hdl && (err != 0)) {
                    /*如果播放提示音失败，suspend提前置0*/
                    hdl->adt_suspend = 0;
                }
                break;
            case SPEAK_TO_CHAT_TASK_KILL:
                /*清掉队列消息*/
                os_taskq_flush();
                os_sem_post((OS_SEM *)msg[2]);
                break;
            case ICSD_ANC_MODE_SWITCH:
                adt_log("ICSD_ANC_MODE_SWITCH");
                anc_mode_switch((u8)msg[2], (u8)msg[3]);
                break;
            case SYNC_ICSD_ADT_OPEN:
                printf("SYNC_ICSD_ADT_OPEN");
                adt_info.mode_switch_busy = 1;
                audio_icsd_adt_open(msg[2]);
                adt_info.mode_switch_busy = 0;
                break;
            case SYNC_ICSD_ADT_CLOSE:
                printf("SYNC_ICSD_ADT_CLOSE");
                adt_info.mode_switch_busy = 1;
                audio_icsd_adt_res_close(msg[2], msg[3]);
                adt_info.mode_switch_busy = 0;
                break;
            case SYNC_ICSD_ADT_SET_ANC_FADE_GAIN:
                adt_log("mode %d set anc fade gain : %d, fade_time : %d\n", msg[3], msg[2], msg[4]);
                if (msg[3] == ANC_FADE_MODE_ENV_ADAPTIVE_GAIN) {
#if TCFG_AUDIO_ANC_ENV_ADAPTIVE_GAIN_ENABLE
                    audio_anc_env_adaptive_fade_gain_set(msg[2], msg[4]);
#endif
                } else if (msg[3] == ANC_FADE_MODE_WIND_NOISE) {
#if TCFG_AUDIO_ANC_WIND_NOISE_DET_ENABLE
                    audio_anc_wind_noise_fade_gain_set(msg[2], msg[4]);
#endif
                } else {
                    icsd_anc_fade_set(msg[3], msg[2]);
                }
                break;
            case ICSD_ADT_STATE_SYNC:
                adt_log("ICSD_ADT_STATE_SYNC");
                u8 *s_data = (u8 *)msg[2];
                /*先关闭adt，同步adt状态，然后同步anc，在anc里面做判断开adt*/
                audio_icsd_adt_state_sync_done(s_data[3], s_data[4]);
                anc_mode_sync(s_data);
                break;
#if ICSD_ADT_MIC_DATA_EXPORT_EN
            case ICSD_ADT_EXPORT_DATA_WRITE:
                aec_uart_write();
                break;
#endif /*ICSD_ADT_MIC_DATA_EXPORT_EN*/
            default:
                break;
            }
        }
    }
}

/*
   ADT 关联模块启动限制
   return 0 不允许打开，return 1 允许打开
 */
int audio_icsd_adt_open_permit(u16 adt_mode)
{
    /*通话的时候不允许打开*/
    if ((adt_mode != ADT_REAL_TIME_ADAPTIVE_ANC_TIDY_MODE) && (adt_mode != ADT_REAL_TIME_ADAPTIVE_ANC_MODE)) {
        if (adc_file_is_runing()) {
            /* if (esco_player_runing()) { */
            printf("esco open !!!");
            return 0;
        }
    }
#if TCFG_ANC_TOOL_DEBUG_ONLINE
    /*连接anc spp 工具的时候不允许打开*/
    if (get_app_anctool_spp_connected_flag()) {
        printf("anctool spp connected !!!");
        //由于免摘需要无线调试，所以支持打开在线调试
        //return 0;
    }
#endif
#if ((defined TCFG_AUDIO_FIT_DET_ENABLE) && TCFG_AUDIO_FIT_DET_ENABLE)
    //贴合度检测状态下不允许其他模式切换
    if (audio_icsd_dot_is_running()) {
        printf("audio icsd_dot is running!!\n");
        return 0;
    }
#endif
#if ((defined TCFG_AUDIO_ANC_EAR_ADAPTIVE_EN) && TCFG_AUDIO_ANC_EAR_ADAPTIVE_EN)
    //自适应过程下不允许打开或者切换其他模式
    if (anc_ear_adaptive_busy_get()) {
        printf("audio anc adaptive is running!!\n");
        return 0;
    }
#endif
    return 1;
}

//adt early init
int anc_adt_init()
{
    /*先创建任务，开关adt的操作在任务里面处理*/
    task_create(audio_icsd_adt_task, NULL, SPEAK_TO_CHAT_TASK_NAME);
    /*注册DMA 中断回调*/
    audio_anc_dma_add_output_handler("ANC_ADT", icsd_adt_dma_done);
    return 0;
}

int audio_icsd_adt_init()
{
    adt_log("%s", __func__);
    struct speak_to_chat_t *hdl = NULL;

    if (speak_to_chat_hdl) {
        printf("speak_to_chat_hdl  is areadly open !!");
        return 0;
    }
    speak_to_chat_hdl = zalloc(sizeof(struct speak_to_chat_t));
    if (speak_to_chat_hdl == NULL) {
        printf("speak_to_chat_hdl malloc fail !!!");
        adt_info.adt_mode = ADT_MODE_CLOSE;
        return -1;
    }
    hdl = speak_to_chat_hdl;

    hdl->last_anc_state = anc_mode_get();
    adt_log("last_anc_state %d", hdl->last_anc_state);

#if ICSD_ADT_MIC_DATA_EXPORT_EN
    aec_uart_open(3, 512);
#endif /*ICSD_ADT_MIC_DATA_EXPORT_EN*/

    /* task_create(audio_icsd_adt_task, hdl, SPEAK_TO_CHAT_TASK_NAME); */
    audio_acoustic_detector_open();
    icsd_adt_clock_add();
    /*关闭蓝牙sniff*/
#if TCFG_USER_TWS_ENABLE
    icsd_bt_sniff_set_enable(0);
#endif
    return 0;
}

//启动ADT
int audio_icsd_adt_open(u16 adt_mode)
{
    adt_log("%s:%x\n", __func__, adt_mode);

    if (audio_icsd_adt_open_permit(adt_mode) == 0) {
        return -1;
    }

    if (!(~adt_info.adt_mode & adt_mode) && adt_mode) {
        /*判断传进来的模式中有哪些没有打开的*/
        return 0;
    }
    if (adt_info.adt_mode && adt_mode) {
        /*如果当前有其他模块已经打开时，需要关闭在重新打开*/
        adt_info.adt_mode |= adt_mode;
        //gali 需要修改 考虑能否一个独立挂起的接口，与挂起流程独立，减少交叉
        audio_icsd_adt_res_close(0, 0);
        return 0;
    }
    adt_info.adt_mode |= adt_mode;

    if (anc_mode_get() == ANC_ON) {
        adt_open_in_anc = 0;
        audio_icsd_adt_init();
    } else if (anc_mode_get() == ANC_OFF) {
        adt_open_in_anc = 1;
        anc_mode_switch_in_anctask(ANC_OFF, 0);
    } else if (anc_mode_get() == ANC_TRANSPARENCY) {
        adt_open_in_anc = 0;
        audio_icsd_adt_init();
    }
    return 0;
}

/*同步打开，
 *ag: audio_icsd_adt_sync_open(ADT_SPEAK_TO_CHAT_MODE | ADT_WIDE_AREA_TAP_MODE | ADT_WIND_NOISE_DET_MODE) */
int audio_icsd_adt_sync_open(u16 adt_mode)
{
    printf("%s, mode:%d", __func__, adt_mode);
    u8 data[4] = {0};
    data[0] = SYNC_ICSD_ADT_OPEN;
    data[1] = adt_mode & 0x00FF;
    data[2] = (adt_mode >> 8) & 0x00FF;

    if (audio_icsd_adt_open_permit(adt_mode) == 0) {
        return -1;
    }
#if TCFG_USER_TWS_ENABLE
    if (get_tws_sibling_connect_state()) {
        if ((tws_api_get_role() == TWS_ROLE_MASTER)) {
            /*同步主机状态打开*/
            audio_icsd_adt_info_sync(data, 4);
        }
    } else
#endif
    {
        audio_icsd_adt_info_sync(data, 4);
    }
    return 0;
}

/*同步关闭，
 *ag: audio_icsd_adt_sync_close(ADT_SPEAK_TO_CHAT_MODE | ADT_WIDE_AREA_TAP_MODE | ADT_WIND_NOISE_DET_MODE) */
int audio_icsd_adt_sync_close(u16 adt_mode, u8 suspend)
{
    u8 data[4] = {0};
    data[0] = SYNC_ICSD_ADT_CLOSE;
    data[1] = adt_mode & 0x00FF;
    data[2] = (adt_mode >> 8) & 0x00FF;
    data[3] = suspend;
#if TCFG_USER_TWS_ENABLE
    if (get_tws_sibling_connect_state()) {
        if ((tws_api_get_role() == TWS_ROLE_MASTER)) {
            /*同步主机状态打开*/
            audio_icsd_adt_info_sync(data, 4);
        }
    } else
#endif
    {
        audio_icsd_adt_info_sync(data, 4);
    }
    return 0;
}

/*获取是否需要先开anc再开免摘的状态*/
u8 get_adt_open_in_anc_state()
{
    return adt_open_in_anc;
}

//ANC_OFF  再ANC_MSG_RUN 初始化ADT
int audio_speak_to_chat_open_in_anc_done()
{
    if (adt_open_in_anc) {
        adt_open_in_anc = 0;
        if (adt_info.adt_mode) {
            audio_icsd_adt_init();
        } else {
            audio_icsd_adt_res_close(0, 0);
        }
    }
    return 0;
}

/*
	关闭ADT
	adt_mode 对应模式；suspend 挂起标志
*/
int audio_icsd_adt_close(u16 adt_mode, u8 suspend)
{
    int err = 0;
    err = os_taskq_post_msg(SPEAK_TO_CHAT_TASK_NAME, 3, SYNC_ICSD_ADT_CLOSE, adt_mode, suspend);
    if (err != OS_NO_ERR) {
        printf("%s err %d", __func__, err);
    }
    return err;
}

//总退出接口
static int audio_icsd_adt_res_close(u16 adt_mode, u8 suspend)
{
    adt_log("%s: %x", __func__, adt_mode);
    struct speak_to_chat_t *hdl = speak_to_chat_hdl;
    if (hdl && hdl->state) {
        if (!(adt_info.adt_mode & adt_mode) && adt_mode) {
            printf("adt mode : 0x%x is alreadly closed !!!", adt_mode);
            return 0;
        }
        adt_info.adt_mode &= ~adt_mode;

        hdl->state = 0;
#if TCFG_AUDIO_ANC_REAL_TIME_ADAPTIVE_ENABLE
        DeAlorithm_disable();
        while (hdl->busy || audio_anc_real_time_adaptive_run_busy_get()) {
#else
        while (hdl->busy) {
#endif
            putchar('w');
            os_time_dly(1);
        }
        audio_acoustic_detector_close();

#if TCFG_AUDIO_ANC_REAL_TIME_ADAPTIVE_ENABLE
        if (adt_mode & (ADT_REAL_TIME_ADAPTIVE_ANC_MODE | ADT_REAL_TIME_ADAPTIVE_ANC_TIDY_MODE)) {
            audio_rtanc_adaptive_exit();
        }
#endif

#if TCFG_AUDIO_ANC_HOWLING_DET_ENABLE
        if (adt_mode & ADT_HOWLING_DET_MODE) {
            /*关闭啸叫检测资源*/
            icsd_anc_soft_howling_det_exit();
        }
#endif

        if (hdl->timer) {
            sys_s_hi_timeout_del(hdl->timer);
            hdl->timer = 0;
        }
        hdl->speak_to_chat_state = AUDIO_ADT_CLOSE;
        adt_info.speak_to_chat_state = AUDIO_ADT_CLOSE;

#if ICSD_ADT_MIC_DATA_EXPORT_EN
        aec_uart_close();
#endif /*ICSD_ADT_MIC_DATA_EXPORT_EN*/

#if SPEAK_TO_CHAT_AUTO_OPEN_IN_ANC
        /*在anc off下自动关闭免摘时，只需要在关闭adt后再关闭anc*/
        if ((!adt_info.adt_mode) && (anc_mode_get() == ANC_OFF)) {
            adt_open_in_anc = 0;
            //如果全部模式都关闭的时候，需要anc忽略相同模式的切换，关闭假anc off
            anc_mode_switch(ANC_OFF, 0);
        }
#else
        if (!suspend) {
            /*恢复ANC状态*/
            if (hdl->last_anc_state == ANC_ON) {
                anc_mode_switch(ANC_ON, 0);
            } else if (hdl->last_anc_state == ANC_TRANSPARENCY) {
                anc_mode_switch(ANC_TRANSPARENCY, 0);
            } else {
                adt_open_in_anc = 0;
                //如果全部模式都关闭的时候，需要anc忽略相同模式的切换，关闭假anc off
                anc_mode_switch(ANC_OFF, 0);
            }
        }
#endif

        /*关闭dac mute*/
        audio_adt_dac_mute_stop();

        /*恢复播歌状态*/
        if (hdl->a2dp_state && (bt_a2dp_get_status() != BT_MUSIC_STATUS_STARTING)) {
            adt_log("send PLAY cmd");
            bt_cmd_prepare(USER_CTRL_AVCTP_OPID_PLAY, 0, NULL);
        }
        /*打开蓝牙sniff*/
#if TCFG_USER_TWS_ENABLE
        icsd_bt_sniff_set_enable(1);
#endif

        //关闭风噪检测时恢复现场
        if (adt_mode & ADT_WIND_NOISE_DET_MODE) {
            //恢复风噪检测增益
            /* if (hdl->adt_wind_gain_lvl != 16384) { */
            icsd_anc_fade_set(ANC_FADE_MODE_WIND_NOISE, 16384);
#if TCFG_AUDIO_ANC_WIND_NOISE_DET_ENABLE
            audio_anc_wind_noise_fade_param_reset();
#endif
            /* } */
#if TCFG_AUDIO_ANC_REAL_TIME_ADAPTIVE_ENABLE
            //恢复RT_ANC 相关标志/状态
            if (hdl->adt_wind_suspend_rtanc) {
                audio_anc_real_time_adaptive_resume();
            }
#endif
        }
        if (adt_mode & ADT_ENV_NOISE_DET_MODE) {
            //恢复风噪检测增益
            /* if (hdl->adt_wind_gain_lvl != 16384) { */
            icsd_anc_fade_set(ANC_FADE_MODE_ENV_ADAPTIVE_GAIN, 16384);
#if TCFG_AUDIO_ANC_ENV_ADAPTIVE_GAIN_ENABLE
            audio_anc_env_adaptive_fade_param_reset();
#if TCFG_AUDIO_ANC_REAL_TIME_ADAPTIVE_ENABLE
            if (anc_env_adaptive_gain_suspend) {
                anc_env_adaptive_gain_suspend = 0;
                audio_anc_real_time_adaptive_resume();
            }
#endif
#endif
            /* } */
        }

        free(hdl);
        speak_to_chat_hdl = NULL;
        icsd_adt_clock_del();

    }
    /*如果adt没有全部关闭，需要重新打开
     *如果是通话时关闭的，直接关闭adt*/
    printf("adt close: adt_mode %x, adc run %d, suspend %d\n", adt_info.adt_mode, adc_file_is_runing(), suspend);
    if (adt_info.adt_mode && !adc_file_is_runing() && !suspend) {
        /* if (adt_info.adt_mode && !esco_player_runing() && !suspend) { */
        audio_icsd_adt_open(0);
    }
    return 0;
}

/*关闭所有模块*/
int audio_icsd_adt_close_all()
{
    u16 adt_mode = ADT_ALL_FUNCTION_ENABLE;
    audio_icsd_adt_close(adt_mode, 0);
    return 0;
}

/*打开所有模块*/
int audio_icsd_adt_open_all()
{
    u16 adt_mode = ADT_ALL_FUNCTION_ENABLE;
    audio_icsd_adt_sync_open(adt_mode);
    return 0;
}

/************************* start 智能免摘相关接口 ***********************/
#if TCFG_AUDIO_SPEAK_TO_CHAT_ENABLE
/*检测到讲话状态执行*/
void set_speak_to_chat_voice_state(u8 state)
{
    struct speak_to_chat_t *hdl = speak_to_chat_hdl;
    int err = 0;
    if (hdl && hdl->state) {
        hdl->voice_state = state;
        hdl->tws_sync_state = 0;
        adt_log("%s", __func__);
        err = os_taskq_post_msg(SPEAK_TO_CHAT_TASK_NAME, 2, ICSD_ADT_VOICE_STATE, (int)hdl);
        if (err != OS_NO_ERR) {
            printf("%s err %d", __func__, err);
        }
    }
}

/*检测到讲话状态TWS同步*/
void audio_speak_to_chat_voice_state_sync(void)
{
    adt_log("%s", __func__);
    struct speak_to_chat_t *hdl = speak_to_chat_hdl;
    u8 data[4] = {0};
    if ((hdl->adt_resume == 0) && (hdl->adt_suspend == 0)) {
#if TCFG_USER_TWS_ENABLE
        if (get_tws_sibling_connect_state()) {
            /*hdl->tws_sync_state == 0 : 保证上一次消息还没接收到，不发下一个消息*/
            if (hdl->tws_sync_state == 0) {
                hdl->tws_sync_state = 1;
                data[0] = SYNC_ICSD_ADT_VOICE_STATE;
                data[1] = 1;
                audio_icsd_adt_info_sync(data, 4);
            }
        } else {
            set_speak_to_chat_voice_state(1);
        }
#else
        set_speak_to_chat_voice_state(1);
#endif/*TCFG_USER_TWS_ENABLE*/
    }
}

/*APP需求：设置免摘定时结束的时间，单位ms*/
int audio_speak_to_char_end_time_set(u16 time)
{
    struct speak_to_chat_t *hdl = speak_to_chat_hdl;
    if (hdl && (adt_info.adt_mode & ADT_SPEAK_TO_CHAT_MODE)) {
        adt_info.speak_to_chat_end_time = time;
        return 0;
    } else {
        return -1;
    }
}

/*APP需求：设置智能免摘检测的灵敏度*/
int audio_speak_to_chat_sensitivity_set(u8 sensitivity)
{
    struct speak_to_chat_t *hdl = speak_to_chat_hdl;
    if (hdl && (adt_info.adt_mode & ADT_SPEAK_TO_CHAT_MODE)) {
        return 0;
    } else {
        return -1;
    }
}

#endif /*TCFG_AUDIO_SPEAK_TO_CHAT_ENABLE*/
/************************* end 智能免摘相关接口 ***********************/

/************************* start 广域点击相关接口 ***********************/
#if TCFG_AUDIO_WIDE_AREA_TAP_ENABLE
/*广域点击识别结果输出回调*/
void audio_wat_click_output_handle(u8 wat_result)
{
    /* adt_log("%s, wat_result:%d", __func__, wat_result); */
    u8 data[4] = {SYNC_ICSD_ADT_WAT_RESULT, wat_result, 0, 0};
    struct speak_to_chat_t *hdl = speak_to_chat_hdl;
    if (hdl && hdl->state) {
        hdl->busy = 1;
        if (wat_result && !hdl->adt_wat_ignore_flag) {

#if TCFG_USER_TWS_ENABLE
            if (tws_in_sniff_state()) {
                /*如果在蓝牙siniff下需要退出蓝牙sniff再发送*/
                icsd_adt_tx_unsniff_req();
            }
#endif
            /*没有tws时直接更新状态*/
            audio_icsd_adt_info_sync(data, 4);
        }
        hdl->busy = 0;
    }
}

void audio_wide_area_tap_ingre_flag_timer(void *p)
{
    struct speak_to_chat_t *hdl = speak_to_chat_hdl;
    if (hdl && (adt_info.adt_mode & ADT_WIDE_AREA_TAP_MODE)) {
        /*恢复响应*/
        adt_log("wat ingore end");
        hdl->adt_wat_ignore_flag = 0;
    }
}

/*设置是否忽略广域点击
 * ignore: 1 忽略点击，0 响应点击
 * 忽略点击的时间，单位ms*/
int audio_wide_area_tap_ignore_flag_set(u8 ignore, u16 time)
{
    struct speak_to_chat_t *hdl = speak_to_chat_hdl;
    if (hdl && (adt_info.adt_mode & ADT_WIDE_AREA_TAP_MODE)) {
        hdl->adt_wat_ignore_flag = ignore;
        if (hdl->adt_wat_ignore_flag) {
            /*如果忽略点击，定时恢复响应*/
            adt_log("wat ingore start");
            sys_s_hi_timerout_add(NULL, audio_wide_area_tap_ingre_flag_timer, time);
        }
        return 0;
    } else {
        return -1;
    }
}

#endif /*TCFG_AUDIO_WIDE_AREA_TAP_ENABLE*/
/************************* end 广域点击相关接口 ***********************/


/************************* satrt 风噪检测相关接口 ***********************/
#if TCFG_AUDIO_ANC_WIND_NOISE_DET_ENABLE
/*风噪检测识别结果输出回调*/
void audio_icsd_wind_detect_output_handle(u8 wind_lvl)
{
    /* adt_log("%s, wind_lvl:%d", __func__, wind_lvl); */
    /* audio_acoustic_detector_output_hdl(0, wind_lvl, 0); */
    struct speak_to_chat_t *hdl = speak_to_chat_hdl;
    u8 data[4] = {SYNC_ICSD_ADT_WIND_LVL_CMP, wind_lvl, 0, 0};
    if (hdl && hdl->state) {
        hdl->busy = 1;
        static u32 next_period = 0;
        /*间隔200ms以上发送一次数据*/
        if (time_after(jiffies, next_period)) {
            next_period = jiffies + msecs_to_jiffies(100);
#if TCFG_USER_TWS_ENABLE
            if (tws_in_sniff_state()) {
                /*如果在蓝牙siniff下需要退出蓝牙sniff再发送*/
                icsd_adt_tx_unsniff_req();
            }
            if (get_tws_sibling_connect_state()) {
                /*记录本地的风噪强度*/
                hdl->adt_tmp_wind_lvl = wind_lvl;
                /*同步主机风噪和从机风噪比较*/
                if ((tws_api_get_role() == TWS_ROLE_MASTER)) {
                    data[0] = SYNC_ICSD_ADT_WIND_LVL_CMP;
                    audio_icsd_adt_info_sync(data, 4);

                }
            } else
#endif
            {
                /*没有tws时直接更新状态*/
                hdl->adt_wind_lvl = wind_lvl;
                data[0] = SYNC_ICSD_ADT_WIND_LVL_RESULT;
                audio_icsd_adt_info_sync(data, 4);
            }
        }
    }
    hdl->busy = 0;
}

/*获取风噪等级*/
u8 get_audio_icsd_wind_lvl()
{
    struct speak_to_chat_t *hdl = speak_to_chat_hdl;
    if (hdl) {
        return hdl->adt_wind_lvl;
    } else {
        return 0;
    }
}

#endif /*TCFG_AUDIO_ANC_WIND_NOISE_DET_ENABLE*/
/************************* end 风噪检测相关接口 ***********************/

/************************* satrt 音量自适应相关接口 ***********************/
#if TCFG_AUDIO_ANC_ENV_NOISE_DET_ENABLE
/*音量偏移回调*/
void audio_icsd_adptive_vol_output_handle(__adt_avc_output *_output)
{
    /* adt_log("%s, spldb_iir:%d", __func__, (int)(_output->spldb_iir)); */
    struct speak_to_chat_t *hdl = speak_to_chat_hdl;
    u8 data[4] = {SYNC_ICSD_ADT_ENV_NOISE_LVL_CMP, (u8)(_output->spldb_iir), 0, 0};

    if (hdl && hdl->state) {
        hdl->busy = 1;
        static u32 next_period = 0;
        /*间隔200ms以上发送一次数据*/
        if (time_after(jiffies, next_period)) {
            next_period = jiffies + msecs_to_jiffies(200);
#if TCFG_USER_TWS_ENABLE
            if (tws_in_sniff_state()) {
                /*如果在蓝牙siniff下需要退出蓝牙sniff再发送*/
                icsd_adt_tx_unsniff_req();
            }
            if (get_tws_sibling_connect_state()) {
                /*记录本地的噪声强度*/
                hdl->tmp_noise_lvl = (u8)(_output->spldb_iir);
                /*同步主机风噪和从机噪声比较*/
                if ((tws_api_get_role() == TWS_ROLE_MASTER)) {
                    /*间隔wind_lvl_target_cnt次发送一次*/
                    data[0] = SYNC_ICSD_ADT_ENV_NOISE_LVL_CMP;
                    audio_icsd_adt_info_sync(data, 4);

                }
            } else
#endif
            {
                /*没有tws时直接更新状态*/
                data[0] = SYNC_ICSD_ADT_ENV_NOISE_LVL_RESULT;
                audio_icsd_adt_info_sync(data, 4);
            }
        }
        hdl->busy = 0;
    }

}
#endif /*TCFG_AUDIO_ANC_ENV_NOISE_DET_ENABLE*/
/************************* end 音量自适应相关接口 ***********************/

/*划分风噪等级为 6三个等级*/
u8 get_icsd_anc_wind_noise_lvl(wind_lvl_det_t *wind_lvl_det, u8 wind_lvl)
{

    if ((wind_lvl >= 0) && (wind_lvl <= (wind_lvl_det->lvl1_thr - wind_lvl_det->dithering_step))) {
        /*判断LVL0*/
        wind_lvl_det->cur_lvl = ANC_WIND_NOISE_LVL0;
        wind_lvl_det->last_lvl = ANC_WIND_NOISE_LVL0;
    } else if ((wind_lvl > (wind_lvl_det->lvl1_thr - wind_lvl_det->dithering_step)) && (wind_lvl <= wind_lvl_det->lvl1_thr)) {
        /*LVL0和LVL1阈值处的消抖处理*/
        if (wind_lvl_det->last_lvl > ANC_WIND_NOISE_LVL0) {
            wind_lvl_det->cur_lvl = ANC_WIND_NOISE_LVL1;
        } else {
            wind_lvl_det->cur_lvl = ANC_WIND_NOISE_LVL0;
        }

    } else if ((wind_lvl > wind_lvl_det->lvl1_thr) && (wind_lvl <= (wind_lvl_det->lvl2_thr - wind_lvl_det->dithering_step))) {
        /*判断LVL1*/
        wind_lvl_det->cur_lvl = ANC_WIND_NOISE_LVL1;
        wind_lvl_det->last_lvl = ANC_WIND_NOISE_LVL1;
    } else if ((wind_lvl > (wind_lvl_det->lvl2_thr - wind_lvl_det->dithering_step)) && (wind_lvl <= wind_lvl_det->lvl2_thr)) {
        /*LVL1和LVL2阈值处的消抖处理*/
        if (wind_lvl_det->last_lvl > ANC_WIND_NOISE_LVL1) {
            wind_lvl_det->cur_lvl = ANC_WIND_NOISE_LVL2;
        } else {
            wind_lvl_det->cur_lvl = ANC_WIND_NOISE_LVL1;
        }

    } else if ((wind_lvl > wind_lvl_det->lvl2_thr) && (wind_lvl <= (wind_lvl_det->lvl3_thr - wind_lvl_det->dithering_step))) {
        /*判断LVL2*/
        wind_lvl_det->cur_lvl = ANC_WIND_NOISE_LVL2;
        wind_lvl_det->last_lvl = ANC_WIND_NOISE_LVL2;
    } else if ((wind_lvl > (wind_lvl_det->lvl3_thr - wind_lvl_det->dithering_step)) && (wind_lvl <= wind_lvl_det->lvl3_thr)) {
        /*LVL2和LVL3阈值处的消抖处理*/
        if (wind_lvl_det->last_lvl > ANC_WIND_NOISE_LVL2) {
            wind_lvl_det->cur_lvl = ANC_WIND_NOISE_LVL3;
        } else {
            wind_lvl_det->cur_lvl = ANC_WIND_NOISE_LVL2;
        }

    } else if ((wind_lvl > wind_lvl_det->lvl3_thr) && (wind_lvl <= (wind_lvl_det->lvl4_thr - wind_lvl_det->dithering_step))) {
        /*判断LVL3*/
        wind_lvl_det->cur_lvl = ANC_WIND_NOISE_LVL3;
        wind_lvl_det->last_lvl = ANC_WIND_NOISE_LVL3;
    } else if ((wind_lvl > (wind_lvl_det->lvl4_thr - wind_lvl_det->dithering_step)) && (wind_lvl <= wind_lvl_det->lvl4_thr)) {
        /*LVL3和LVL4阈值处的消抖处理*/
        if (wind_lvl_det->last_lvl > ANC_WIND_NOISE_LVL3) {
            wind_lvl_det->cur_lvl = ANC_WIND_NOISE_LVL4;
        } else {
            wind_lvl_det->cur_lvl = ANC_WIND_NOISE_LVL3;
        }

    } else if ((wind_lvl > wind_lvl_det->lvl4_thr) && (wind_lvl <= (wind_lvl_det->lvl5_thr - wind_lvl_det->dithering_step))) {
        /*判断LVL4*/
        wind_lvl_det->cur_lvl = ANC_WIND_NOISE_LVL4;
        wind_lvl_det->last_lvl = ANC_WIND_NOISE_LVL4;
    } else if ((wind_lvl > (wind_lvl_det->lvl5_thr - wind_lvl_det->dithering_step)) && (wind_lvl <= wind_lvl_det->lvl5_thr)) {
        /*LVL4和LVL5阈值处的消抖处理*/
        if (wind_lvl_det->last_lvl > ANC_WIND_NOISE_LVL4) {
            wind_lvl_det->cur_lvl = ANC_WIND_NOISE_LVL5;
        } else {
            wind_lvl_det->cur_lvl = ANC_WIND_NOISE_LVL4;
        }

    } else {//if ((wind_lvl > MIDDLE_STRONG_WIND_LVL_THR) && (wind_lvl <= 300)) {
        /*判断LVL5*/
        wind_lvl_det->cur_lvl = ANC_WIND_NOISE_LVL5;
        wind_lvl_det->last_lvl = ANC_WIND_NOISE_LVL5;
    }
    return wind_lvl_det->cur_lvl;
}

void audio_adt_wn_process_fade_timer(void *p)
{
    wind_info_t *wind_info = (wind_info_t *)p;

    wind_info->fade_timer = 0;

    /*防止adt任务被卡太久没有跑wind_cnt计数为0导致异常*/
    if (!wind_info->wind_cnt) {
        printf("ADT_ERR wind_cnt:%d", wind_info->wind_cnt);
        return;
    }
    /*计算当前计算帧的噪声等级:求当前计算帧内的平均值*/
    wind_info->preset_lvl = (float)wind_info->wind_eng / (wind_info->wind_cnt * wind_info->ratio_thr);
    /* wind_info->preset_lvl = (float)((float)wind_info->wind_eng / wind_info->wind_cnt) * (1.0 / wind_info->ratio_thr); */
    adt_log("=========================cnt %d, eng %d, avg %d", wind_info->wind_cnt, wind_info->wind_eng, wind_info->preset_lvl);
    wind_info->wind_cnt = 0;
    wind_info->wind_eng = 0;
    if (wind_info->preset_lvl > wind_info->last_lvl) {
        /*与上一次比较，等级变大*/
        wind_info->fade_in_cnt ++;
        wind_info->fade_out_cnt = 0;
        wind_info->lvl_unchange_cnt = 0;
    } else if (wind_info->preset_lvl < wind_info->last_lvl) {
        /*与上一次比较，等级变小*/
        wind_info->fade_in_cnt = 0;
        wind_info->fade_out_cnt ++;;
        wind_info->lvl_unchange_cnt = 0;
    } else {
        /*与上一次比较，没有变化重新计算*/
        wind_info->fade_in_cnt = 0;
        wind_info->fade_out_cnt  = 0;;
        wind_info->lvl_unchange_cnt ++;
    }

    /*判断是否到达淡入淡出的时间*/
    if (wind_info->fade_in_cnt >= (wind_info->fade_in_time * 1000 / wind_info->time)) {
        wind_info->fade_in_cnt = 0;
        wind_info->fade_out_cnt = 0;
        wind_info->lvl_unchange_cnt = 0;
        wind_info->wind_process_flag = 1;
    } else if (wind_info->fade_out_cnt >= (wind_info->fade_out_time * 1000 / wind_info->time)) {
        wind_info->fade_in_cnt = 0;
        wind_info->fade_out_cnt = 0;
        wind_info->lvl_unchange_cnt = 0;
        wind_info->wind_process_flag = 1;
    } else if (wind_info->lvl_unchange_cnt >= (wind_info->fade_in_time * 1000 / wind_info->time)) {
        wind_info->fade_in_cnt = 0;
        wind_info->fade_out_cnt = 0;
        wind_info->lvl_unchange_cnt = 0;
        wind_info->wind_process_flag = 1;
    }

}

int audio_anc_wind_noise_process_fade(wind_info_t *wind_info, u8 anc_wind_noise_lvl)
{
    if (wind_info->fade_timer == 0) {
        wind_info->fade_timer = sys_s_hi_timerout_add(wind_info, audio_adt_wn_process_fade_timer, wind_info->time);
    }

    wind_info->wind_cnt ++;
    wind_info->wind_eng += anc_wind_noise_lvl;
    if (wind_info->wind_process_flag) {
        wind_info->wind_process_flag = 0;
        adt_log("anc_wind_noise_lvl %x : %d", (int)wind_info, wind_info->preset_lvl);
        wind_info->last_lvl = wind_info->preset_lvl;
        return wind_info->preset_lvl;
    }
    return 0;
}

static void anc_gain_fade_timer(void *priv)
{
    struct anc_fade_handle *hdl = (struct anc_fade_handle *)priv;
    if (!hdl) {
        return;
    }
    int fade_setp = hdl->fade_setp;
    int target_gain = hdl->target_gain;
    u8 fade_gain_mode = hdl->fade_gain_mode;
    if (hdl->cur_gain == target_gain) {
        sys_timer_del(hdl->timer_id);
        hdl->timer_id = 0;
#if TCFG_AUDIO_ANC_REAL_TIME_ADAPTIVE_ENABLE
        //环境自适应，当增益等于16384时，才恢复RTANC
        if (anc_env_adaptive_gain_suspend) {
            anc_env_adaptive_gain_suspend = 0;
            audio_anc_real_time_adaptive_resume();
        }
#endif
        return ;
    } else if (hdl->cur_gain > target_gain) {
        hdl->cur_gain -= fade_setp;
        hdl->cur_gain = (hdl->cur_gain < target_gain) ? target_gain : hdl->cur_gain;
    } else if (hdl->cur_gain < target_gain) {
        hdl->cur_gain += fade_setp;
        hdl->cur_gain = (hdl->cur_gain > target_gain) ? target_gain : hdl->cur_gain;
    }
    icsd_anc_fade_set(fade_gain_mode, hdl->cur_gain);
    // adt_log("gain fade: mode %d, hdl->cur_gain %d, target_gain %d, fade_setp %d \n", fade_gain_mode, hdl->cur_gain, target_gain, fade_setp);
}

/*anc增益淡入淡出*/
int audio_anc_gain_fade_process(struct anc_fade_handle *hdl, enum anc_fade_mode_t mode, int target_gain, int fade_time_ms)
{
    adt_log("audio_anc_gain_fade_process \n");
    if (!hdl) {
        return -1;
    }

    if (hdl->timer_id) {
        sys_timer_del(hdl->timer_id);
    }
#if TCFG_AUDIO_ANC_REAL_TIME_ADAPTIVE_ENABLE
    //触发环境自适应，挂起RTANC
    if (!anc_env_adaptive_gain_suspend) {
        anc_env_adaptive_gain_suspend = 1;
        audio_anc_real_time_adaptive_suspend();
    }
#endif

    hdl->target_gain = target_gain;

    if (fade_time_ms == 0) {
        icsd_anc_fade_set(mode, target_gain);
        hdl->cur_gain = target_gain;
        return 0;
    }

    hdl->fade_gain_mode = mode;
    int gain_diff = hdl->cur_gain - target_gain;
    gain_diff = (gain_diff < 0) ? (gain_diff * (-1)) : gain_diff;
    hdl->fade_setp = gain_diff / (fade_time_ms / hdl->timer_ms);
    hdl->timer_id = sys_timer_add((void *)hdl, anc_gain_fade_timer, hdl->timer_ms);
    adt_log("gain fade: mode %d, cur_gain %d, target_gain %d, fade_setp %d \n", hdl->fade_gain_mode, hdl->cur_gain, hdl->target_gain, hdl->fade_setp);
    return 0;
}


/*
	通透模式 广域点击、风噪检测需打开FB功能
	FB默认滤波器，CMP使用ANC 滤波器
	V2后续可考虑优化掉
*/
float anc_trans_lfb_gain = 0.0625f;
float anc_trans_rfb_gain = 0.0625f;
const double anc_trans_lfb_coeff[] = {
    0.195234751212410628795623779296875,
    0.1007948522455990314483642578125,
    0.0427739980514161288738250732421875,
    -1.02504855208098888397216796875,
    0.36385215423069894313812255859375,
    /*
    0.0508266850956715643405914306640625,
    -0.1013386586564593017101287841796875,
    0.0505129448720254004001617431640625,
    -1.99860572628676891326904296875,
    0.9986066981218755245208740234375,
    */
};
const double anc_trans_rfb_coeff[] = {
    0.195234751212410628795623779296875,
    0.1007948522455990314483642578125,
    0.0427739980514161288738250732421875,
    -1.02504855208098888397216796875,
    0.36385215423069894313812255859375,
};

//免摘/广域需要在通透下开ANC FB，提供默认的参数，如果外部使用自然通透，则可以不用
void audio_icsd_adt_trans_fb_param_set(audio_anc_t *param)
{
    anc_gain_param_t *gains = &param->gains;

    param->ltrans_fbgain = anc_trans_lfb_gain;
    param->rtrans_fbgain = anc_trans_rfb_gain;
    param->ltrans_fb_coeff = (double *)anc_trans_lfb_coeff;
    param->rtrans_fb_coeff = (double *)anc_trans_rfb_coeff;

    param->ltrans_fb_yorder = sizeof(anc_trans_lfb_coeff) / 40;
    param->rtrans_fb_yorder = sizeof(anc_trans_rfb_coeff) / 40;
    //use anc cmp param
    param->ltrans_cmpgain = (gains->gain_sign & ANCL_CMP_SIGN) ? (0 - gains->l_cmpgain) : gains->l_cmpgain;
    param->rtrans_cmpgain = (gains->gain_sign & ANCR_CMP_SIGN) ? (0 - gains->r_cmpgain) : gains->r_cmpgain;
    param->ltrans_cmp_coeff = param->lcmp_coeff;
    param->rtrans_cmp_coeff = param->rcmp_coeff;

    param->ltrans_cmp_yorder = param->lcmp_yorder;
    param->rtrans_cmp_yorder = param->rcmp_yorder;
}

//获取当前ADT是否运行
u8 icsd_adt_is_running(void)
{
    if (speak_to_chat_hdl) {
        if (speak_to_chat_hdl->state) {
            return 1;
        }
    }
    return 0;
}

static u8 audio_speak_to_chat_idle_query()
{
    return speak_to_chat_hdl ? 0 : 1;
}

REGISTER_LP_TARGET(speak_to_chat_lp_target) = {
    .name = "speak_to_chat",
    .is_idle = audio_speak_to_chat_idle_query,
};

#endif /*(defined TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN) && TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN*/
