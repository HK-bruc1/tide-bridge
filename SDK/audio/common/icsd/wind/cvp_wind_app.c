#include "app_config.h"
#include "audio_cvp.h"
#include "system/includes.h"
#include "media/includes.h"
#if TCFG_USER_TWS_ENABLE
#include "bt_tws.h"
#endif/*TCFG_USER_TWS_ENABLE*/
#if ((defined TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN) && TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN && \
        TCFG_AUDIO_ANC_ENABLE && TCFG_AUDIO_ANC_WIND_NOISE_DET_ENABLE)
#include "icsd_adt_app.h"
#include "audio_anc.h"
#if TCFG_AUDIO_ANC_REAL_TIME_ADAPTIVE_ENABLE
#include "rt_anc_app.h"
#endif

static wind_lvl_det_t wind_lvl_det_anc = {
    .lvl1_thr = 80,
    .lvl2_thr = 88,
    .lvl3_thr = 92,
    .lvl4_thr = 95,
    .lvl5_thr = 97,
    .dithering_step = 1,
    .last_lvl = 0,
    .cur_lvl = 0,
};

static wind_info_t wind_info_anc = {
    .time = 1000, //ms
    .fade_timer = 0,
    .wind_cnt = 0,
    .wind_eng = 0,
    .last_lvl = 0,
    .preset_lvl = 0,
    .fade_in_cnt = 0,
    .fade_out_cnt = 0,
    .lvl_unchange_cnt = 0,
    .wind_process_flag = 0,
    .fade_in_time = 4, //s
    .fade_out_time = 10, //s
    .ratio_thr = 0.8f,
};

static struct anc_fade_handle anc_wind_gain_fade = {
    .timer_ms = 100, //配置定时器周期
    .timer_id = 0, //记录定时器id
    .cur_gain = 16384, //记录当前增益
    .fade_setp = 0,//记录淡入淡出步进
    .target_gain = 0, //记录目标增益
    .fade_gain_mode = 0,//记录当前设置fade gain的模式
};

struct cvp_icsd_wind_handle {
    u8 adt_wind_suspend_rtanc;
    u8 cvp_wind_lvl_det_state;
};

struct cvp_icsd_wind_handle *cvp_icsd_wind_hdl = NULL;


/*重置风噪检测的初始参数*/
void audio_cvp_icsd_wind_fade_param_reset(void)
{
    wind_lvl_det_anc.last_lvl = 0;
    wind_lvl_det_anc.cur_lvl = 0;

    if (wind_info_anc.fade_timer) {
        sys_s_hi_timer_del(wind_info_anc.fade_timer);
        wind_info_anc.fade_timer = 0;
    }
    wind_info_anc.fade_in_cnt = 0;
    wind_info_anc.fade_out_cnt = 0;
    wind_info_anc.lvl_unchange_cnt = 0;
    wind_info_anc.wind_process_flag = 0;
    wind_info_anc.last_lvl = 0;

    if (anc_wind_gain_fade.timer_id) {
        sys_s_hi_timer_del(anc_wind_gain_fade.timer_id);
        anc_wind_gain_fade.timer_id = 0;
    }
    anc_wind_gain_fade.cur_gain = 16384;
}

int audio_cvp_icsd_wind_det_open()
{
    if (cvp_icsd_wind_hdl) {
        return 0;
    }
    cvp_icsd_wind_hdl = zalloc(sizeof(struct cvp_icsd_wind_handle));
    ASSERT(cvp_icsd_wind_hdl);
    cvp_icsd_wind_hdl->cvp_wind_lvl_det_state = 1;
    return 0;
}

int audio_cvp_icsd_wind_det_close()
{
    if (cvp_icsd_wind_hdl) {
        cvp_icsd_wind_hdl->cvp_wind_lvl_det_state = 0;
        audio_cvp_icsd_wind_fade_param_reset();
#if TCFG_AUDIO_ANC_REAL_TIME_ADAPTIVE_ENABLE
        //恢复RT_ANC 相关标志/状态
        if (cvp_icsd_wind_hdl->adt_wind_suspend_rtanc) {
            audio_anc_real_time_adaptive_resume();
        }
#endif
        free(cvp_icsd_wind_hdl);
        cvp_icsd_wind_hdl = NULL;
    }
    return 0;
}

u8 get_cvp_icsd_wind_lvl_det_state()
{

    struct cvp_icsd_wind_handle *hdl = cvp_icsd_wind_hdl;
    if (hdl == NULL) {
        return 0;
    }
    return hdl->cvp_wind_lvl_det_state;
}

void audio_cvp_icsd_wind_det_demo()
{
    printf("%s", __func__);
    if (get_cvp_icsd_wind_lvl_det_state() == 0) {
        /*打开提示音*/
        /* icsd_adt_tone_play(ICSD_ADT_TONE_WINDDET_ON); */
        audio_cvp_icsd_wind_det_open();
    } else {
        /*关闭提示音*/
        /* icsd_adt_tone_play(ICSD_ADT_TONE_WINDDET_OFF); */
        audio_cvp_icsd_wind_det_close();
    }
}

/*设置风噪检测设置增益，带淡入淡出功能*/
void audio_cvp_icsd_wind_fade_gain_set(int fade_gain, int fade_time)
{
    audio_anc_gain_fade_process(&anc_wind_gain_fade, ANC_FADE_MODE_WIND_NOISE, fade_gain, fade_time);
}

static void audio_cvp_wind_lvl_sync(u8 *data, int len);
static void audio_cvp_wind_lvl_process(u8 wind_lvl)
{
    struct cvp_icsd_wind_handle *hdl = cvp_icsd_wind_hdl;
    if (hdl == NULL) {
        return;
    }
    u8 anc_wind_noise_lvl = 0;
    if (anc_mode_get() == ANC_ON) {
        /*划分风噪等级*/
        anc_wind_noise_lvl = get_icsd_anc_wind_noise_lvl(&wind_lvl_det_anc, wind_lvl);

        /*做淡入淡出时间处理，返回0表示不做处理维持原来的增益不变*/
        anc_wind_noise_lvl = audio_anc_wind_noise_process_fade(&wind_info_anc, anc_wind_noise_lvl);
        if (anc_wind_noise_lvl == 0) {
            return;
        }

    } else {
        return;
    }
    printf("======================================= wind_noise_lvl【%d】\n", anc_wind_noise_lvl - 1);

    u16 anc_fade_gain = 16384;
    /*根据风噪等级改变anc增益*/
    switch (anc_wind_noise_lvl) {
    case ANC_WIND_NOISE_LVL0:
        anc_fade_gain = 16384;
        break;
    case ANC_WIND_NOISE_LVL1:
        anc_fade_gain = 10000;
        break;
    case ANC_WIND_NOISE_LVL2:
        anc_fade_gain = 8000;
        break;
    case ANC_WIND_NOISE_LVL3:
        anc_fade_gain = 6000;
        break;
    case ANC_WIND_NOISE_LVL4:
        anc_fade_gain = 3000;
        break;
    case ANC_WIND_NOISE_LVL5:
    case ANC_WIND_NOISE_LVL_MAX:
        anc_fade_gain = 0;
        break;
    default:
        anc_fade_gain = 0;
        break;
    }

#if TCFG_AUDIO_ANC_REAL_TIME_ADAPTIVE_ENABLE
    //触发风噪检测之后需要挂起RTANC
    if (audio_anc_real_time_adaptive_state_get()) {
        if (anc_fade_gain != 16384) {
            if (hdl->adt_wind_suspend_rtanc) {
                hdl->adt_wind_suspend_rtanc = 1;
                audio_anc_real_time_adaptive_suspend();
            }
        } else if (hdl->adt_wind_suspend_rtanc) {
            hdl->adt_wind_suspend_rtanc = 0;
            audio_anc_real_time_adaptive_resume();
        }
    }
#endif

    u8 data[4] = {0};
    data[0] = SYNC_ICSD_ADT_SET_ANC_FADE_GAIN;
    data[1] = anc_fade_gain & 0xff;
    data[2] = (anc_fade_gain >> 8) & 0xff;
    data[3] = ANC_FADE_MODE_WIND_NOISE;
    audio_cvp_wind_lvl_sync(data, 4);
}

static void audio_cvp_wind_lvl_sync_cb(void *_data, u16 len, bool rx)
{
    u8 mode = ((u8 *)_data)[0];
    switch (mode) {
    case SYNC_ICSD_ADT_WIND_LVL_RESULT:
        u8 wind_lvl = ((u8 *)_data)[1];
        printf(" sync wind_lvl %d", wind_lvl);
        audio_cvp_wind_lvl_process(wind_lvl);
        break;
    case SYNC_ICSD_ADT_SET_ANC_FADE_GAIN:
        int anc_fade_gain = (((u8 *)_data)[2] << 8) | ((u8 *)_data)[1];
        u8 mode = ((u8 *)_data)[3];
        printf("mode %d,set anc fade gain : %d", mode, anc_fade_gain);
        icsd_anc_fade_set(mode, anc_fade_gain);
        break;
    }
}

#define TWS_FUNC_ID_CVP_WIND_LVL_SYNC    TWS_FUNC_ID('C', 'V', 'P', 'W')
REGISTER_TWS_FUNC_STUB(audio_cvp_wind_lvl) = {
    .func_id = TWS_FUNC_ID_CVP_WIND_LVL_SYNC,
    .func    = audio_cvp_wind_lvl_sync_cb,
};
static void audio_cvp_wind_lvl_sync(u8 *data, int len)
{
#if TCFG_USER_TWS_ENABLE
    if (get_tws_sibling_connect_state()) {
        int ret = tws_api_send_data_to_sibling(data, len, TWS_FUNC_ID_CVP_WIND_LVL_SYNC);
    } else
#endif
    {
        audio_cvp_wind_lvl_sync_cb(data, len, 0);
    }
}

void audio_cvp_wind_lvl_output_handle(u8 wind_lvl)
{
    /* printf("%s, wind_lvl:%d", __func__, wind_lvl); */
    struct cvp_icsd_wind_handle *hdl = cvp_icsd_wind_hdl;
    if (hdl == NULL) {
        return;
    }
    static u32 next_period = 0;
    /*间隔200ms以上发送一次数据*/
    if (time_after(jiffies, next_period)) {
        next_period = jiffies + msecs_to_jiffies(100);

        u8 data[2] = {0};
        data[0] = SYNC_ICSD_ADT_WIND_LVL_RESULT;
        data[1] = wind_lvl;
        audio_cvp_wind_lvl_sync(data, 2);
    }
}

#endif
