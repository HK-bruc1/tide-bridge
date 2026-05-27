#include "app_config.h"
#if ((defined TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN) && TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN && \
        TCFG_AUDIO_ANC_ENABLE && TCFG_AUDIO_VOLUME_ADAPTIVE_ENABLE)
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

/*打开音量自适应*/
int audio_icsd_adaptive_vol_open()
{
    printf("%s", __func__);
    u8 adt_mode = ADT_ENV_NOISE_DET_MODE;
    return audio_icsd_adt_sync_open(adt_mode);
}

/*关闭音量自适应*/
int audio_icsd_adaptive_vol_close()
{
    printf("%s", __func__);
    u8 adt_mode = ADT_ENV_NOISE_DET_MODE;
    int ret = audio_icsd_adt_sync_close(adt_mode, 0);
    /*关闭音量自适应后恢复音量*/
    audio_app_set_vol_offset_dB(0);
    return ret;
}

/*音量自适应使用demo*/
void audio_icsd_adaptive_vol_demo()
{
    printf("%s", __func__);
    if (audio_icsd_adt_open_permit(ADT_ENV_NOISE_DET_MODE) == 0) {
        return;
    }

    if ((get_icsd_adt_mode() & ADT_ENV_NOISE_DET_MODE) == 0) {
        /*打开提示音*/
        /* icsd_adt_tone_play(ICSD_ADT_TONE_WINDDET_ON); */
        audio_icsd_adaptive_vol_open();
    } else {
        /*关闭提示音*/
        /* icsd_adt_tone_play(ICSD_ADT_TONE_WINDDET_OFF); */
        audio_icsd_adaptive_vol_close();
    }
}

struct adaptive_vol_param {
    u8 noise_lvl_thr;/*噪声阈值*/
    float offset_dB;/*小于噪声阈值的音量偏移*/
};

/*将音量划分5个不同的区间，每一个区间下，不同大小的噪声，设置不同的音量偏移大小*/
#define VOLUME_LEVEL    5
/*将噪声大小划分6个等级*/
#define NOISE_LEVEL     6

static const struct adaptive_vol_param avc_parm[VOLUME_LEVEL][NOISE_LEVEL + 1] = {
    /*第1区间的音量: cur_vol < max_vol * 1/VOLUME_LEVEL*/
    {
        {40, 0},//噪声小于40，音量+0dB
        {45, 8},//噪声在40~45，音量+8dB
        {50, 16},//噪声在45~50，音量+16dB
        {60, 20},//噪声在50~60，音量+20dB
        {70, 25},
        {80, 30},
        {90, 35}
    },
#if (VOLUME_LEVEL >= 2)
    /*第2区间的音量: cur_vol < max_vol * 2/VOLUME_LEVEL , cur_vol >= max_vol * 1/VOLUME_LEVEL*/
    {
        {40, 0},
        {45, 4},
        {50, 8},
        {60, 10},
        {70, 14},
        {80, 18},
        {90, 22}
    },
#endif
#if (VOLUME_LEVEL >= 3)
    /*第3区间的音量: cur_vol < max_vol * 3/VOLUME_LEVEL, cur_vol >= max_vol * 2/VOLUME_LEVEL*/
    {
        {40, 0},
        {45, 2},
        {50, 4},
        {60, 8},
        {70, 10},
        {80, 12},
        {90, 15}
    },
#endif
#if (VOLUME_LEVEL >= 4)
    /*第4区间的音量: cur_vol < max_vol * 4/VOLUME_LEVEL, cur_vol >= max_vol * 3/VOLUME_LEVEL*/
    {
        {40, 0},
        {45, 2},
        {50, 3},
        {60, 4},
        {70, 5},
        {80, 10},
        {90, 12}
    },
#endif
#if (VOLUME_LEVEL >= 5)
    /*第5区间的音量: cur_vol < max_vol * 5/VOLUME_LEVELi, cur_vol >= max_vol * 4/VOLUME_LEVEL*/
    {
        {40, 0},
        {45, 1},
        {50, 3},
        {60, 6},
        {70, 9},
        {80, 9},
        {90, 10}
    }
#endif
};

/*音量偏移处理*/
void audio_icsd_adptive_vol_event_process(u8 spldb_iir)
{
    /* printf("%s, spldb_iir:%d", __func__, (u8)(spldb_iir)) */
    int i = 0;
    int j = 0;

    /*获取最大音量等级*/
    s16 max_vol = app_audio_get_max_volume();
    s16 cur_vol = app_audio_get_volume(APP_AUDIO_CURRENT_STATE);
    /* printf("cur_vol %d, max_vol %d", cur_vol, max_vol); */

    for (i = 0; i < VOLUME_LEVEL; i++) {
        /*查找当前音量在哪个音量区间*/
        if (cur_vol <= (max_vol * (i + 1) / VOLUME_LEVEL)) {

            for (j = 0; j < NOISE_LEVEL; j++) {
                /*判断当前噪声在哪个等级*/
                if (spldb_iir <= avc_parm[i][j].noise_lvl_thr) {
                    audio_app_set_vol_offset_dB(avc_parm[i][j].offset_dB);
                    /* printf("vol_lvl:%d, noise_lvl: %d, noise_lvl_thr: %d, dB: %d", i, j, avc_parm[i][j].noise_lvl_thr, (int)avc_parm[i][j].offset_dB); */
                    break;
                }
            }
            /*处于最大噪声等级*/
            audio_app_set_vol_offset_dB(avc_parm[i][j].offset_dB);
            /* printf("vol_lvl:%d, noise_lvl: %d, noise_lvl_thr: %d, dB: %d", i, j, avc_parm[i][j].noise_lvl_thr, (int)avc_parm[i][j].offset_dB); */
            break;
        }
    }
}

#endif /*(defined TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN) && TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN*/
