#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".icsd_adt.data.bss")
#pragma data_seg(".icsd_adt.data")
#pragma const_seg(".icsd_adt.text.const")
#pragma code_seg(".icsd_adt.text")
#endif

#include "app_config.h"
#if ((defined TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN) && TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN && \
	 TCFG_AUDIO_ANC_ENABLE)
#include "icsd_wind.h"
#include "icsd_adt.h"
#include "anc_ext_tool.h"
#include "audio_anc_common.h"

//====================风噪检测配置=====================
const u8 ICSD_WIND_PHONE_TYPE  = SDK_WIND_PHONE_TYPE;
const u8 ICSD_WIND_MIC_TYPE    = SDK_WIND_MIC_TYPE;
const u8 ICSD_WIND_ALG_BT_INF  = 1;
const u8 ICSD_WIND_DATA_BT_INF = 0;

int (*win_printf)(const char *format, ...) = _win_printf;

void wind_config_init(__wind_config *_wind_config)
{
    __wind_config *wind_config = _wind_config;

#if TCFG_AUDIO_ANC_WIND_NOISE_DET_ENABLE && TCFG_AUDIO_ANC_EXT_TOOL_ENABLE
    struct __anc_ext_wind_det_cfg *cfg = anc_ext_ear_adaptive_cfg_get()->wind_det_cfg;
    if (cfg) {
        wind_config->msc_lp_thr    = cfg->msc_lp_thr;
        wind_config->msc_mp_thr    = cfg->msc_mp_thr;
        wind_config->corr_thr      = cfg->corr_thr;//tws 不需要
        wind_config->cpt_1p_thr  = cfg->cpt_1p_thr;
        wind_config->ref_pwr_thr   = cfg->ref_pwr_thr;
        wind_config->wind_iir_alpha = cfg->wind_iir_alpha;
        wind_config->wind_lvl_scale = cfg->wind_lvl_scale;
        wind_config->icsd_wind_num_thr2 = cfg->icsd_wind_num_thr2;
        wind_config->icsd_wind_num_thr1 = cfg->icsd_wind_num_thr1;
#if 0
        printf("cfg->msc_lp_thr:%d\n", (int)(100 * cfg->msc_lp_thr));
        printf("cfg->msc_mp_thr:%d\n", (int)(100 * cfg->msc_mp_thr));
        printf("cfg->corr_thr:%d\n", (int)(100 * cfg->corr_thr));
        printf("cfg->cpt_1p_thr:%d\n", (int)(100 * cfg->cpt_1p_thr));
        printf("cfg->ref_pwr_thr:%d\n", (int)(100 * cfg->ref_pwr_thr));
        printf("cfg->wind_iir_alpha:%d\n", (int)(100 * cfg->wind_iir_alpha));
        printf("cfg->wind_lvl_scale:%d\n", (int)(100 * cfg->wind_lvl_scale));
        printf("cfg->icsd_wind_num_thr2:%d\n", (int)(100 * cfg->icsd_wind_num_thr2));
        printf("cfg->icsd_wind_num_thr1:%d\n", (int)(100 * cfg->icsd_wind_num_thr1));
#endif
    } else
#endif
    {
        wind_config->msc_lp_thr    = 2.4;
        wind_config->msc_mp_thr    = 2.4;
        wind_config->corr_thr      = 0.4;//tws 不需要
        wind_config->cpt_1p_thr  = 20;
        wind_config->ref_pwr_thr   = 30;
        wind_config->wind_iir_alpha = 16;
        wind_config->wind_lvl_scale = 2;
        wind_config->icsd_wind_num_thr2 = 3;
        wind_config->icsd_wind_num_thr1 = 1;
    }

}

void icsd_wind_demo()
{
    static int *alloc_addr = NULL;
    struct icsd_win_libfmt libfmt;
    struct icsd_win_infmt  fmt;
    icsd_wind_get_libfmt(&libfmt);
    win_printf("WIND RAM SIZE:%d\n", libfmt.lib_alloc_size);
    if (alloc_addr == NULL) {
        alloc_addr = zalloc(libfmt.lib_alloc_size);
    }
    fmt.alloc_ptr = alloc_addr;
    icsd_wind_set_infmt(&fmt);
}


const struct wind_function WIND_FUNC_t = {
    .wind_config_init = wind_config_init,
    .HanningWin_pwr_float = icsd_HanningWin_pwr_float,
    .HanningWin_pwr_s1 = icsd_HanningWin_pwr_s1,
    .FFT_radix256 = icsd_FFT_radix256,
    .FFT_radix128 = icsd_FFT_radix128,
    .complex_mul  = icsd_complex_mul_v2,
    .complex_div  = icsd_complex_div_v2,
    .pxydivpxxpyy = icsd_pxydivpxxpyy,
    .log10_float  = icsd_log10_anc,
    .cal_score    = icsd_cal_score,
    .icsd_adt_tws_msync = icsd_adt_tws_msync,
    .icsd_adt_tws_ssync = icsd_adt_tws_ssync,
    .icsd_wind_run_part2_cmd = icsd_wind_run_part2_cmd,
    .icsd_adt_wind_part1_rx = icsd_adt_wind_part1_rx,
};

struct wind_function *WIND_FUNC = (struct wind_function *)(&WIND_FUNC_t);
#endif/*TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN*/
