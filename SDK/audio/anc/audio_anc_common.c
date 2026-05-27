#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".audio_anc_common.data.bss")
#pragma data_seg(".audio_anc_common.data")
#pragma const_seg(".audio_anc_common.text.const")
#pragma code_seg(".audio_anc_common.text")
#endif

/*
 ****************************************************************
 *							AUDIO ANC COMMON
 * File  : audio_anc_comon_plug.c
 * By    :
 * Notes : 存放ANC共用流程
 *
 ****************************************************************
 */

#include "app_config.h"

#if TCFG_AUDIO_ANC_ENABLE
#include "audio_anc.h"
#include "audio_anc_common_plug.h"

#if TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN
#include "icsd_adt.h"
#include "icsd_adt_app.h"
#endif /*TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN*/

#if 1
#define anc_log	printf
#else
#define anc_log (...)
#endif


struct anc_common_t {
    u8 production_mode;
};

static struct anc_common_t common_hdl;
#define __this  (&common_hdl)

//ANC进入产测模式
int audio_anc_production_enter(void)
{
    if (__this->production_mode == 0) {
        anc_log("ANC in Production mode enter\n");
        __this->production_mode = 1;
        //挂起产测互斥功能

#if ANC_EAR_ADAPTIVE_EN
        //耳道自适应将ANC参数修改为默认参数
        if (audio_anc_coeff_mode_get()) {
            audio_anc_coeff_adaptive_set(0, 0);
        }
#endif

#if ANC_ADAPTIVE_EN
        //关闭场景自适应功能
        audio_anc_power_adaptive_suspend();
#endif

#if TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN
        //关闭风噪检测、智能免摘、广域点击
        if (audio_icsd_adt_is_running()) {
            audio_icsd_adt_close(0, 1);
        }

        printf("======================= %d", audio_icsd_adt_is_running());
        int cnt;
        //adt关闭时间较短，预留100ms
        for (cnt = 0; cnt < 10; cnt++) {
            if (!audio_icsd_adt_is_running()) {
                break;
            }
            os_time_dly(1);  //  等待ADT 关闭
        }
        if (cnt == 10) {
            printf("Err:dot_suspend adt wait timeout\n");
        }
#endif /*TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN*/

#if ANC_MUSIC_DYNAMIC_GAIN_EN
        //关闭ANC防破音
        audio_anc_music_dynamic_gain_suspend();
#endif
        //关闭啸叫抑制、DRC、自适应DCC等功能；
    }
    return 0;
}

//ANC退出产测模式
int audio_anc_production_exit(void)
{
    if (__this->production_mode == 1) {
        anc_log("ANC in Production mode exit\n");
        __this->production_mode = 0;
        //恢复产测互斥功能
    }
    return 0;
}

#endif
