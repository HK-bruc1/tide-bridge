#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".icsd_adt.data.bss")
#pragma data_seg(".icsd_adt.data")
#pragma const_seg(".icsd_adt.text.const")
#pragma code_seg(".icsd_adt.text")
#endif

#include "app_config.h"

#if ( TCFG_AUDIO_ANC_ENABLE && TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN  && \
	TCFG_AUDIO_ADAPTIVE_DCC_ENABLE )

#include "icsd_adjdcc.h"
#include "icsd_adt.h"
#include "audio_anc_common.h"
#include "anc_ext_tool.h"

int (*dcc_printf)(const char *format, ...) = _dcc_printf;

void anc_core_ff_adjdcc_par_set(u8 dc_par);

const float err_overload_list[] = {90, 90, 85, 80};

const float adjdcc_param_table[] = {
    0, 100, 100,
    2,  20,  -1,
    4,  15,  -2,
    6,  10,  -4,
};

float adjdcc_thr_list_up[] = {
    100, 105, 110, 120, 130, 140,
};

float adjdcc_thr_list_down[] = {
    80, 90, 95, 100, 90, 100,
};

const float adjdcc_iir_coef = 0.9;
const u8 adjdcc_steps = 4;

void adjdcc_config_init(__adjdcc_config *_adjdcc_config)
{
    __adjdcc_config *adjdcc_config = _adjdcc_config;

#if TCFG_AUDIO_ANC_EXT_TOOL_ENABLE
    struct __anc_ext_adaptive_dcc_cfg *cfg = anc_ext_ear_adaptive_cfg_get()->adaptive_dcc_cfg;
    if (cfg) {
        adjdcc_config->release_cnt = cfg->refmic_mp_thr;
        adjdcc_config->wind_lvl_thr = cfg->refmic_max_thr;
        adjdcc_config->steps = adjdcc_steps;

        for (int i = 0; i < 5; i++) {
            adjdcc_thr_list_up[i] = cfg->param1[i];
            adjdcc_thr_list_down[i] = cfg->param1[i + 5];
        }
        adjdcc_config->thr_list_up = (float *)(&adjdcc_thr_list_up);
        adjdcc_config->thr_list_down = (float *)(&adjdcc_thr_list_down);

        adjdcc_config->iir_coef = cfg->err_overload_list[0];
        adjdcc_config->param_table = (float *)(&cfg->err_overload_list[2]);
    } else
#endif
    {
        adjdcc_config->release_cnt = 40;
        adjdcc_config->err_overload_list = (float *)(&err_overload_list);
        adjdcc_config->refmic_max_thr = 300;
        adjdcc_config->refmic_mp_thr = 10;
        adjdcc_config->ff_dc_par = 6;

        adjdcc_config->steps = adjdcc_steps; // max:10
        adjdcc_config->param_table = (float *)(&adjdcc_param_table);
        adjdcc_config->thr_list_up = (float *)(&adjdcc_thr_list_up);
        adjdcc_config->thr_list_down = (float *)(&adjdcc_thr_list_down);
        adjdcc_config->iir_coef = adjdcc_iir_coef;
        adjdcc_config->wind_lvl_thr = 30;
    }

#if 0
    printf("adjdcc confit %d, %d, %d\n", adjdcc_config->release_cnt, adjdcc_config->wind_lvl_thr, adjdcc_config->steps);
    printf("iir_coef : %d\n", (int)(adjdcc_config->iir_coef * 100));

    for (int i = 0; i < 5; i++) {
        printf("thrh:%d, thrl:%d\n", (int)adjdcc_config->thr_list_up[i], (int)adjdcc_config->thr_list_down[i]);
    }

    for (int i = 0; i < 12; i++) {
        printf("%d\n", (int)(adjdcc_config->param_table[i]));
    }


#endif
}

const struct adjdcc_function ADJDCC_FUNC_t = {
    .adjdcc_config_init = adjdcc_config_init,
    .ff_adjdcc_par_set = anc_core_ff_adjdcc_par_set,
    .adjdcc_trigger_update = icsd_adt_alg_adjdcc_trigger_update,
    .rtanc_adjdcc_flag_set = icsd_adt_alg_rtanc_adjdcc_flag_set,
    .rtanc_adjdcc_flag_get = icsd_adt_alg_rtanc_adjdcc_flag_get,
    .get_wind_lvl = icsd_adt_alg_rtanc_get_wind_lvl,
};

struct adjdcc_function *ADJDCC_FUNC = (struct adjdcc_function *)(&ADJDCC_FUNC_t);

void icsd_adjdcc_demo()
{
    static int *alloc_addr = NULL;
    struct icsd_adjdcc_libfmt libfmt;
    struct icsd_adjdcc_infmt  fmt;
    icsd_adjdcc_get_libfmt(&libfmt);
    dcc_printf("ADJDCC RAM SIZE:%d\n", libfmt.lib_alloc_size);
    if (alloc_addr == NULL) {
        alloc_addr = zalloc(libfmt.lib_alloc_size);
    }
    fmt.alloc_ptr = alloc_addr;
    icsd_adjdcc_set_infmt(&fmt);
}

#endif

