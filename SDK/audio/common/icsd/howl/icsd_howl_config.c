#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".icsd_adt.data.bss")
#pragma data_seg(".icsd_adt.data")
#pragma const_seg(".icsd_adt.text.const")
#pragma code_seg(".icsd_adt.text")
#endif

#include "app_config.h"
#if ((defined TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN) && TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN && \
        TCFG_AUDIO_ANC_ENABLE && TCFG_AUDIO_ANC_HOWLING_DET_ENABLE)
#include "icsd_adt.h"
#include "icsd_howl.h"

int (*howl_printf)(const char *format, ...) = _howl_printf;

void howl_config_init(__howl_config *_howl_config)
{
    _howl_config->hd_scale = 0.25;
    _howl_config->hd_sat_thr = 9000;
    _howl_config->hd_det_thr = 3;
    _howl_config->hd_diff_pwr_thr = 18;
    _howl_config->hd_maxind_thr1 = 11;
    _howl_config->hd_maxind_thr2 = 20;
}

const struct howl_function HOWL_FUNC_t = {
    .howl_config_init = howl_config_init,
};
struct howl_function *HOWL_FUNC = (struct howl_function *)(&HOWL_FUNC_t);

#endif
