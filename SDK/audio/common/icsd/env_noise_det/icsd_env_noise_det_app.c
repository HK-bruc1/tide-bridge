#include "app_config.h"
#if ((defined TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN) && TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN && \
	 TCFG_AUDIO_ANC_ENABLE && TCFG_AUDIO_ANC_ENV_ADAPTIVE_GAIN_ENABLE)
#include "system/includes.h"
#include "icsd_adt_app.h"
#include "audio_anc.h"
#include "asm/anc.h"
#include "tone_player.h"
#include "audio_config.h"
#include "icsd_anc_user.h"
#include "sniff.h"

#if TCFG_AUDIO_ANC_REAL_TIME_ADAPTIVE_ENABLE
#include "rt_anc_app.h"
#endif

#if 1
#define env_log printf
#else
#define env_log(...)
#endif

/*打开anc增益自适应*/
int audio_anc_env_adaptive_gain_open()
{
    env_log("%s", __func__);
    u8 adt_mode = ADT_ENV_NOISE_DET_MODE;
    return audio_icsd_adt_sync_open(adt_mode);
}

/*关闭anc增益自适应*/
int audio_anc_env_adaptive_gain_close()
{
    env_log("%s", __func__);
    u8 adt_mode = ADT_ENV_NOISE_DET_MODE;
    int ret = audio_icsd_adt_sync_close(adt_mode, 0);
    return ret;
}

/*anc增益自适应使用demo*/
void audio_anc_env_adaptive_gain_demo()
{
    env_log("%s", __func__);
    if (audio_icsd_adt_open_permit(ADT_ENV_NOISE_DET_MODE) == 0) {
        return;
    }

    if ((get_icsd_adt_mode() & ADT_ENV_NOISE_DET_MODE) == 0) {
        /*打开提示音*/
        /* icsd_adt_tone_play(ICSD_ADT_TONE_WINDDET_ON); */
        audio_anc_env_adaptive_gain_open();
    } else {
        /*关闭提示音*/
        /* icsd_adt_tone_play(ICSD_ADT_TONE_WINDDET_OFF); */
        audio_anc_env_adaptive_gain_close();
    }
}

static wind_lvl_det_t anc_env_noise_det = {
    .lvl1_thr = 30,         //阈值1
    .lvl2_thr = 60,         //阈值2
    .lvl3_thr = 70,         //阈值3
    .lvl4_thr = 80,         //阈值4
    .lvl5_thr = 90,         //阈值5
    .dithering_step = 1,    //消抖风噪等级间距
    .last_lvl = 0,
    .cur_lvl = 0,
};

static wind_info_t anc_env_noise_lvl_fade = {
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
    .fade_in_time = 4, //设置噪声等级变大检测时间，单位s，误差1s
    .fade_out_time = 4, //设置噪声等级变小检测时间，单位s，误差1s
    .ratio_thr = 0.9f,
};

static struct anc_fade_handle anc_env_gain_fade = {
    .timer_ms = 100, //配置定时器周期
    .timer_id = 0, //记录定时器id
    .cur_gain = 16384, //记录当前增益
    .fade_setp = 0,//记录淡入淡出步进
    .target_gain = 16384, //记录目标增益
    .fade_gain_mode = 0,//记录当前设置fade gain的模式
};

/*重置环境自适应增益的初始参数*/
void audio_anc_env_adaptive_fade_param_reset(void)
{
    anc_env_noise_det.last_lvl = 0;
    anc_env_noise_det.cur_lvl = 0;

    if (anc_env_noise_lvl_fade.fade_timer) {
        sys_s_hi_timer_del(anc_env_noise_lvl_fade.fade_timer);
        anc_env_noise_lvl_fade.fade_timer = 0;
    }
    anc_env_noise_lvl_fade.fade_in_cnt = 0;
    anc_env_noise_lvl_fade.fade_out_cnt = 0;
    anc_env_noise_lvl_fade.lvl_unchange_cnt = 0;
    anc_env_noise_lvl_fade.wind_process_flag = 0;
    anc_env_noise_lvl_fade.last_lvl = 0;

    if (anc_env_gain_fade.timer_id) {
        sys_s_hi_timer_del(anc_env_gain_fade.timer_id);
        anc_env_gain_fade.timer_id = 0;
    }
    anc_env_gain_fade.cur_gain = 16384;
    anc_env_gain_fade.target_gain = 16384;
}

/*设置环境自适应增益，带淡入淡出功能*/
void audio_anc_env_adaptive_fade_gain_set(int fade_gain, int fade_time)
{
    audio_anc_gain_fade_process(&anc_env_gain_fade, ANC_FADE_MODE_ENV_ADAPTIVE_GAIN, fade_gain, fade_time);
}

/*环境噪声处理*/
int audio_env_noise_event_process(u8 spldb_iir)
{
    u8 anc_env_noise_lvl = 0;

    /* env_log("spldb_iir %d", spldb_iir); */

    /*anc模式下才改anc增益*/
    if (anc_mode_get() != ANC_OFF) {
        /*划分环境噪声等级*/
        anc_env_noise_lvl = get_icsd_anc_wind_noise_lvl(&anc_env_noise_det, spldb_iir);

        /*做淡入淡出时间处理，返回0表示不做处理维持原来的增益不变*/
        anc_env_noise_lvl = audio_anc_wind_noise_process_fade(&anc_env_noise_lvl_fade, anc_env_noise_lvl);
        if (anc_env_noise_lvl == 0) {
            return -1;
        }
    } else {
        return -1;
    }

    u16 anc_fade_gain = 16384;
    u16 anc_fade_time = 3000; //ms
    /*根据风噪等级改变anc增益*/
    switch (anc_env_noise_lvl) {
    case ANC_WIND_NOISE_LVL0:
        anc_fade_gain = 1500;
        anc_fade_time = 3000;
        break;
    case ANC_WIND_NOISE_LVL1:
        anc_fade_gain = 8192;
        anc_fade_time = 3000;
        break;
    case ANC_WIND_NOISE_LVL2:
        anc_fade_gain = 16384;
        anc_fade_time = 3000;
        break;
    case ANC_WIND_NOISE_LVL3:
        anc_fade_gain = 16384;
        anc_fade_time = 3000;
        break;
    case ANC_WIND_NOISE_LVL4:
        anc_fade_gain = 16384;
        anc_fade_time = 3000;
        break;
    case ANC_WIND_NOISE_LVL5:
    case ANC_WIND_NOISE_LVL_MAX:
        anc_fade_gain = 16384;
        anc_fade_time = 3000;
        break;
    default:
        anc_fade_gain = 16384;
        anc_fade_time = 3000;
        break;
    }

    if (anc_env_gain_fade.target_gain == anc_fade_gain) {
        return anc_fade_gain;
    }

    env_log("ENV_NOISE_STATE:RUN, lvl %d, gain %d\n", anc_env_noise_lvl, anc_fade_gain);

    u8 data[5] = {0};
    data[0] = SYNC_ICSD_ADT_SET_ANC_FADE_GAIN;
    data[1] = anc_fade_gain & 0xff;
    data[2] = (anc_fade_gain >> 8) & 0xff;
    data[3] = ANC_FADE_MODE_ENV_ADAPTIVE_GAIN;
    data[4] = anc_fade_time / 100; //缩小100倍传参
    audio_icsd_adt_info_sync(data, 5);

    return anc_fade_gain;
}

#endif /*(defined TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN) && TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN*/
