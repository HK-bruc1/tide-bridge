
#include "app_config.h"
#if ((defined TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN) && TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN && \
        TCFG_AUDIO_ANC_ENABLE && TCFG_AUDIO_ANC_WIND_NOISE_DET_ENABLE)
#include "system/includes.h"
#include "icsd_adt_app.h"
#include "audio_anc.h"
#include "asm/anc.h"
#include "tone_player.h"
#include "audio_config.h"
#include "icsd_anc_user.h"
#include "sniff.h"
#include "anc_ext_tool.h"
#include "audio_anc_common.h"

#if TCFG_AUDIO_ANC_REAL_TIME_ADAPTIVE_ENABLE
#include "rt_anc_app.h"
#endif

#if 1
#define wind_log printf
#else
#define wind_log(...)
#endif

/*打开风噪检测*/
int audio_icsd_wind_detect_open()
{
    wind_log("%s", __func__);
    u8 adt_mode = ADT_WIND_NOISE_DET_MODE;
    return audio_icsd_adt_sync_open(adt_mode);
}

/*关闭风噪检测*/
int audio_icsd_wind_detect_close()
{
    wind_log("%s", __func__);
    u8 adt_mode = ADT_WIND_NOISE_DET_MODE;
    return audio_icsd_adt_sync_close(adt_mode, 0);
}

/*风噪检测开关*/
void audio_icsd_wind_detect_en(u8 en)
{
    wind_log("%s en: %d", __func__, en);
    if (audio_icsd_adt_open_permit(ADT_WIND_NOISE_DET_MODE) == 0) {
        return;
    }

    if (((get_icsd_adt_mode() & ADT_WIND_NOISE_DET_MODE) == 0) && en) {
        /*打开提示音*/
        icsd_adt_tone_play(ICSD_ADT_TONE_WINDDET_ON);
        audio_icsd_wind_detect_open();
    } else if ((get_icsd_adt_mode() & ADT_WIND_NOISE_DET_MODE) && (!en)) {
        /*关闭提示音*/
        icsd_adt_tone_play(ICSD_ADT_TONE_WINDDET_OFF);
        audio_icsd_wind_detect_close();
    }
}

/*风噪检测使用demo*/
void audio_icsd_wind_detect_demo()
{
    wind_log("%s", __func__);
    if (audio_icsd_adt_open_permit(ADT_WIND_NOISE_DET_MODE) == 0) {
        return;
    }

    if ((get_icsd_adt_mode() & ADT_WIND_NOISE_DET_MODE) == 0) {
        /*打开提示音*/
        icsd_adt_tone_play(ICSD_ADT_TONE_WINDDET_ON);
        audio_icsd_wind_detect_open();
    } else {
        /*关闭提示音*/
        icsd_adt_tone_play(ICSD_ADT_TONE_WINDDET_OFF);
        audio_icsd_wind_detect_close();
    }
}

static wind_lvl_det_t wind_lvl_det_anc = {
    .lvl1_thr = 30,
    .lvl2_thr = 60,
    .lvl3_thr = 90,
    .lvl4_thr = 120,
    .lvl5_thr = 150,
    .dithering_step = 10,
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
    .target_gain = 16384, //记录目标增益
    .fade_gain_mode = 0,//记录当前设置fade gain的模式
};

/*重置风噪检测的初始参数*/
void audio_anc_wind_noise_fade_param_reset(void)
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
    anc_wind_gain_fade.target_gain = 16384;
}

/*设置风噪检测设置增益，带淡入淡出功能*/
void audio_anc_wind_noise_fade_gain_set(int fade_gain, int fade_time)
{
    audio_anc_gain_fade_process(&anc_wind_gain_fade, ANC_FADE_MODE_WIND_NOISE, fade_gain, fade_time);
}

/*anc风噪检测的处理*/
int audio_anc_wind_noise_process(u8 wind_lvl)
{
    u8 anc_wind_noise_lvl = 0;

    /* wind_log("wind_lvl %d\n", wind_lvl); */

#if TCFG_AUDIO_ANC_EXT_TOOL_ENABLE
    struct __anc_ext_wind_trigger_cfg *trigger_cfg = anc_ext_ear_adaptive_cfg_get()->wind_trigger_cfg;
#else
    struct __anc_ext_wind_trigger_cfg *trigger_cfg = NULL;
#endif
    /*anc模式下才改anc增益*/
    if (anc_mode_get() == ANC_ON) {
        /*划分风噪等级*/
        if (trigger_cfg) { //use anc_ext_tool cfg
            wind_lvl_det_anc.lvl1_thr = trigger_cfg->thr[0];
            wind_lvl_det_anc.lvl2_thr = trigger_cfg->thr[1];
            wind_lvl_det_anc.lvl3_thr = trigger_cfg->thr[2];
            wind_lvl_det_anc.lvl4_thr = trigger_cfg->thr[3];
            wind_lvl_det_anc.lvl5_thr = trigger_cfg->thr[4];
        }
        anc_wind_noise_lvl = get_icsd_anc_wind_noise_lvl(&wind_lvl_det_anc, wind_lvl);

        /*做淡入淡出时间处理，返回0表示不做处理维持原来的增益不变*/
        anc_wind_noise_lvl = audio_anc_wind_noise_process_fade(&wind_info_anc, anc_wind_noise_lvl);
        if (anc_wind_noise_lvl == 0) {
            return -1;
        }
    } else {
        return -1;
    }

    u16 anc_fade_gain = 16384;
    u16 anc_fade_time = 3000; //ms
    /*根据风噪等级改变anc增益*/
    switch (anc_wind_noise_lvl) {
    case ANC_WIND_NOISE_LVL0:
        anc_fade_gain = 16384;
        anc_fade_time = 3000;
        break;
    case ANC_WIND_NOISE_LVL1:
        anc_fade_gain = 10000;
        anc_fade_time = 3000;
        break;
    case ANC_WIND_NOISE_LVL2:
        anc_fade_gain = 8000;
        anc_fade_time = 3000;
        break;
    case ANC_WIND_NOISE_LVL3:
        anc_fade_gain = 6000;
        anc_fade_time = 3000;
        break;
    case ANC_WIND_NOISE_LVL4:
        anc_fade_gain = 3000;
        anc_fade_time = 3000;
        break;
    case ANC_WIND_NOISE_LVL5:
    case ANC_WIND_NOISE_LVL_MAX:
        anc_fade_gain = 0;
        anc_fade_time = 3000;
        break;
    default:
        anc_fade_gain = 0;
        anc_fade_time = 3000;
        break;
    }
    if (trigger_cfg) { //use anc_ext_tool cfg
        if (anc_wind_noise_lvl != ANC_WIND_NOISE_LVL0) {
            int cfg_lvl = anc_wind_noise_lvl - ANC_WIND_NOISE_LVL1;
            //等级计算可能会超，超过最大则用最大
            cfg_lvl = (cfg_lvl > 4) ? 4 : cfg_lvl;
            if (cfg_lvl >= 0) {
                anc_fade_gain = trigger_cfg->gain[cfg_lvl];
            } else {
                printf("ERR: wind_lvl out of range\n");
            }
        }
    }

    if (anc_wind_gain_fade.target_gain == anc_fade_gain) {
        return anc_fade_gain;
    }

    wind_log("WIND_DET_STATE:RUN, lvl %d, gain %d\n", anc_wind_noise_lvl, anc_fade_gain);

    u8 data[5] = {0};
    data[0] = SYNC_ICSD_ADT_SET_ANC_FADE_GAIN;
    data[1] = anc_fade_gain & 0xff;
    data[2] = (anc_fade_gain >> 8) & 0xff;
    data[3] = ANC_FADE_MODE_WIND_NOISE;
    data[4] = anc_fade_time / 100; //缩小100倍传参
    audio_icsd_adt_info_sync(data, 5);

    return anc_fade_gain;
}

#endif /*(defined TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN) && TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN*/
