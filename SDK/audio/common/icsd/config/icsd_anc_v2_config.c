#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".icsd_anc.data.bss")
#pragma data_seg(".icsd_anc.data")
#pragma const_seg(".icsd_anc.text.const")
#pragma code_seg(".icsd_anc.text")
#endif

#define USE_BOARD_CONFIG 0
#define EXT_PRINTF_DEBUG 0

#include "app_config.h"
#include "audio_config_def.h"
#if ((TCFG_AUDIO_ANC_EAR_ADAPTIVE_EN || TCFG_AUDIO_ANC_REAL_TIME_ADAPTIVE_ENABLE) && \
	 TCFG_AUDIO_ANC_ENABLE && TCFG_AUDIO_ANC_EAR_ADAPTIVE_VERSION == ANC_EXT_V2)

#include "audio_anc.h"
#include "icsd_anc_v2_config.h"
#include "icsd_common_v2.h"

#if (USE_BOARD_CONFIG == 1)
#include "icsd_anc_v2_board.c"
#endif

#if TCFG_AUDIO_ANC_REAL_TIME_ADAPTIVE_ENABLE
#include "rt_anc_app.h"
#endif

const u8 ICSD_ANC_VERSION = 2;
const u8 ICSD_ANC_TOOL_PRINTF = 0;
const u8 msedif_en = 0;
const u8 target_diff_en = 0;

struct icsd_anc_v2_tool_data *TOOL_DATA = NULL;

const int cmp_idx_begin = 0;
const int cmp_idx_end = 59;
const int cmp_total_len = 60;
const int cmp_en = 0;
const float pz_gain = 0;
const u8 ICSD_ANC_V2_BYPASS_ON_FIRST = 0;//播放提示音过程中打开BYPASS节省BYPASS稳定时间

/***************************************************************/
/****************** ANC MODE bypass fgq ************************/
/***************************************************************/
const float gfq_bypass[] = {
    -10,  5000,  1,
};
const u8 tap_bypass = 1;
const u8 type_bypass[] = {3};
const float fs_bypass = 375e3;
double bbbaa_bypass[1 * 5];

void icsd_anc_v2_board_config()
{
    double a0, a1, a2, b0, b1, b2;
    for (int i = 0; i < tap_bypass; i++) {
        icsd_biquad2ab_out_v2(gfq_bypass[3 * i], gfq_bypass[3 * i + 1], fs_bypass, gfq_bypass[3 * i + 2], &a0, &a1, &a2, &b0, &b1, &b2, type_bypass[i]);
        bbbaa_bypass[5 * i + 0] = b0 / a0;
        bbbaa_bypass[5 * i + 1] = b1 / a0;
        bbbaa_bypass[5 * i + 2] = b2 / a0;
        bbbaa_bypass[5 * i + 3] = a1 / a0;
        bbbaa_bypass[5 * i + 4] = a2 / a0;
    }
}

/***************************************************************/
/**************************** FF *******************************/
/***************************************************************/
const int FF_objFunc_type  = 3;

const float FSTOP_IDX = 2700;
const float FSTOP_IDX2 = 2700;

const float Gold_csv_Perf_Range[] = {
    2.0,   2.0,   2.0,   2.0,   2.0,   2.0,   2.0,   2.0,   2.0,   2.0,   2.0,   2.0,   2.0,   2.0,   2.0,
    2.0,   2.0,   2.0,   2.0,   2.0,   2.0,   2.0,   2.0,   2.0,   2.0,   2.0,   2.0,   2.0,   2.0,   2.0,
    2.0,   2.0,   2.0,   2.0,   2.0,   2.0,   2.0,   2.0,   2.0,   2.0,   2.0,   2.0,   2.0,   2.0,   2.0,
    2.0,   2.0,   2.0,   2.0,   2.0,   2.0,   2.0,   2.0,   2.0,   2.0,   2.0,   2.0,   2.0,   2.0,   2.0,
};

//degree_level, gain_limit, limit_mse_begin, limit_mse_end, over_mse_begin, over_mse_end,biquad_cut
float degree_set0[] = {3, -3, 50, 2200, 50, 2700, 5};
float degree_set1[] = {8, 10, 50, 2200, 50, 2700, 8};
float degree_set2[] = {11, 5, 50, 2200, 50, 2700, 11};


#if EXT_PRINTF_DEBUG
static void de_vrange_printf(float *vrange, int order)
{
    printf(" ff oreder = %d\n", order);
    for (int i = 0; i < order; i++) {
        printf("%d >>>>>>>>>>> g:%d, %d, f:%d, %d, q:%d, %d\n", i, (int)(vrange[6 * i + 0] * 1000), (int)(vrange[6 * i + 1] * 1000)
               , (int)(vrange[6 * i + 2] * 1000), (int)(vrange[6 * i + 3] * 1000)
               , (int)(vrange[6 * i + 4] * 1000), (int)(vrange[6 * i + 5] * 1000));
    }
}

static void de_biquad_printf(float *biquad, int order)
{
    printf(" ff oreder = %d\n", order);
    for (int i = 0; i < order; i++) {
        printf("%d >>>>> g:%d, f:%d, q:%d\n", i, (int)(biquad[3 * i + 0] * 1000), (int)(biquad[3 * i + 1]), (int)(biquad[3 * i + 2] * 1000));
    }
    printf("total gain = %d\n", (int)(biquad[order * 3] * 1000));
}
#endif

const u8 mem_list[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 21, 23, 25, 27, 31, 35, 39, 43, 51, 59};

// ref_gain: liner
// err_gain: liner
// eq_freq range: 40 --> 2.7khz
static void szpz_cmp_get(float *pz_cmp, float *sz_cmp, float ref_gain, float err_gain, struct aeq_default_seg_tab *eq_par, float eq_freq_l, float eq_freq_h, u8 eq_total_gain_en)
{
    const int len = 25;
    float *freq = malloc(len * 4);
    float *eq_hz = malloc(len * 8);
    float *iir_ab = malloc(5 * 10 * 4);	 // eq tap <= 10

    ASSERT(pz_cmp, "pz_cmp input err\n");
    ASSERT(sz_cmp, "sz_cmp input err\n");

    for (int i = 0; i < len; i++) {
        freq[i] = 46875.0 / 1024 * mem_list[i];
        pz_cmp[2 * i + 0] = 1;
        pz_cmp[2 * i + 1] = 0;
        sz_cmp[2 * i + 0] = 1;
        sz_cmp[2 * i + 1] = 0;
    }

    u8 eq_idx_l = 0;
    u8 eq_idx_h = 0;
    for (int i = 0; i < len - 1; i++) {
        if ((eq_freq_l > freq[i]) && (eq_freq_l <= freq[i + 1])) {
            eq_idx_l = i;
        }
        if ((eq_freq_h > freq[i]) && (eq_freq_h <= freq[i + 1])) {
            eq_idx_h = i;
        }
    }
    //printf("freq:%d, %d; ind:%d, %d\n", (int)eq_freq_l, (int)eq_freq_h, eq_idx_l, eq_idx_h);
    if (eq_par) {
        icsd_aeq_fgq2eq(eq_par, iir_ab, eq_hz, freq, 46875, len * 2);
    } else {
        for (int i = 0; i < len; i++) {
            eq_hz[2 * i + 0] = 1;
            eq_hz[2 * i + 1] = 0;
        }
    }

    //for(int i=0; i<len; i++){
    //    printf("%d, %d\n", (int)(eq_hz[2*i]*1e6), (int)(eq_hz[2*i+1]));
    //}


    if (eq_total_gain_en) {
        float eq_gain = 1;
        float cnt = 0;
        float db = 0;
        for (int i = eq_idx_l; i < eq_idx_h; i++) {
            db += 10 * icsd_log10_anc(eq_hz[2 * i] * eq_hz[2 * i] + eq_hz[2 * i + 1] * eq_hz[2 * i + 1]);
            cnt = cnt + 1;
        }
        if (cnt != 0) {
            db = db / cnt;
        }
        eq_gain = eq_gain * icsd_anc_pow10(db / 20);

        float pz_cmp_gain = err_gain;
        float sz_cmp_gain = err_gain;

        if (ref_gain != 0) {
            pz_cmp_gain = err_gain / ref_gain;
        }

        if (eq_gain != 0) {
            sz_cmp_gain = err_gain / eq_gain;
        }

        for (int i = 0; i < len; i++) {
            pz_cmp[2 * i + 0] = pz_cmp_gain;
            pz_cmp[2 * i + 1] = 0;
            sz_cmp[2 * i + 0] = sz_cmp_gain;
            sz_cmp[2 * i + 1] = 0;
        }
    } else {
        float err_tmp[2];
        err_tmp[0] = err_gain;
        err_tmp[1] = 0;

        for (int i = 0; i < len; i++) {
            pz_cmp[2 * i + 0] = err_gain / ref_gain;
            pz_cmp[2 * i + 1] = 0;
            icsd_complex_div_v2(err_tmp, &eq_hz[2 * i], &sz_cmp[2 * i], 2);
        }
    }

    free(freq);
    free(eq_hz);
    free(iir_ab);

}

void icsd_sd_cfg_set(__icsd_anc_config_data *SD_CFG, void *_ext_cfg)
{
    struct anc_ext_ear_adaptive_param *ext_cfg = (struct anc_ext_ear_adaptive_param *)_ext_cfg;
#if (USE_BOARD_CONFIG == 1)

    printf(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>anc use board config\n");
    icsd_anc_config_board_init(SD_CFG);

    return ;
#endif
    if (SD_CFG) {
        //界面信息配置
        SD_CFG->pnc_times = ext_cfg->base_cfg->adaptive_times;
        SD_CFG->vld1 = ext_cfg->base_cfg->vld1;
        SD_CFG->vld2 = ext_cfg->base_cfg->vld2;
        SD_CFG->sz_pri_thr = ext_cfg->base_cfg->sz_pri_thr;
        SD_CFG->bypass_vol = ext_cfg->base_cfg->bypass_vol;
        SD_CFG->sz_calr_sign = ext_cfg->base_cfg->calr_sign[0];
        SD_CFG->pz_calr_sign = ext_cfg->base_cfg->calr_sign[1];
        SD_CFG->bypass_calr_sign = ext_cfg->base_cfg->calr_sign[2];
        SD_CFG->perf_calr_sign = ext_cfg->base_cfg->calr_sign[3];
        SD_CFG->train_mode = ext_cfg->train_mode;	//自适应训练模式设置 */
        SD_CFG->tonel_delay = ext_cfg->base_cfg->tonel_delay;
        SD_CFG->toner_delay = ext_cfg->base_cfg->toner_delay;
        SD_CFG->pzl_delay = ext_cfg->base_cfg->pzl_delay;
        SD_CFG->pzr_delay = ext_cfg->base_cfg->pzr_delay;
        SD_CFG->ear_recorder = ext_cfg->ff_ear_mem_param->ear_recorder;
        SD_CFG->fb_agc_en = 0;
        //其他配置
        SD_CFG->ff_yorder  = ANC_ADAPTIVE_FF_ORDER;
        SD_CFG->fb_yorder  = ANC_ADAPTIVE_FB_ORDER;
        //SD_CFG->cmp_yorder = ANC_ADAPTIVE_CMP_ORDER;
        if (ICSD_ANC_CPU == ICSD_BR28) {
            printf("ICSD BR28 SEL\n");
            SD_CFG->normal_out_sel_l = BR28_ANC_USER_TRAIN_OUT_SEL_L;
            SD_CFG->normal_out_sel_r = BR28_ANC_USER_TRAIN_OUT_SEL_R;
            SD_CFG->tone_out_sel_l   = BR28_ANC_TONE_TRAIN_OUT_SEL_L;
            SD_CFG->tone_out_sel_r   = BR28_ANC_TONE_TRAIN_OUT_SEL_R;
        } else if (ICSD_ANC_CPU == ICSD_BR50) {
            printf("ICSD BR50 SEL\n");
            SD_CFG->normal_out_sel_l = BR50_ANC_USER_TRAIN_OUT_SEL_L;
            SD_CFG->normal_out_sel_r = BR50_ANC_USER_TRAIN_OUT_SEL_R;
            SD_CFG->tone_out_sel_l   = BR50_ANC_TONE_TRAIN_OUT_SEL_L;
            SD_CFG->tone_out_sel_r   = BR50_ANC_TONE_TRAIN_OUT_SEL_R;
        } else {
            printf("CPU ERR\n");
        }
        /***************************************************/
        /**************** left channel config **************/
        /***************************************************/
        SD_CFG->adpt_cfg.pnc_times = ext_cfg->base_cfg->adaptive_times;

        // de alg config
        SD_CFG->adpt_cfg.high_fgq_fix = 1; // gali TODO  需要工具做传参
        SD_CFG->adpt_cfg.de_alg_sel = 1; // gali TODO

        // target配置
        SD_CFG->adpt_cfg.cmp_en = ext_cfg->ff_target_param->cmp_en;
        SD_CFG->adpt_cfg.target_cmp_num = ext_cfg->ff_target_param->cmp_curve_num;
        SD_CFG->adpt_cfg.target_sv = ext_cfg->ff_target_sv->data;
        SD_CFG->adpt_cfg.target_cmp_dat = ext_cfg->ff_target_cmp->data;

        // 算法配置
        SD_CFG->adpt_cfg.IIR_NUM_FLEX = 0;
        int flex_idx = 0;
        int biquad_idx = 0;
        for (int i = 0; i < SD_CFG->ff_yorder; i++) {
            if (ext_cfg->ff_iir_high[i].fixed_en) { // fix
                SD_CFG->adpt_cfg.Biquad_init_H[3 * biquad_idx + 0] = ext_cfg->ff_iir_high[i].def.gain;
                SD_CFG->adpt_cfg.Biquad_init_H[3 * biquad_idx + 1] = ext_cfg->ff_iir_high[i].def.fre;
                SD_CFG->adpt_cfg.Biquad_init_H[3 * biquad_idx + 2] = ext_cfg->ff_iir_high[i].def.q;
                SD_CFG->adpt_cfg.Biquad_init_M[3 * biquad_idx + 0] = ext_cfg->ff_iir_medium[i].def.gain;
                SD_CFG->adpt_cfg.Biquad_init_M[3 * biquad_idx + 1] = ext_cfg->ff_iir_medium[i].def.fre;
                SD_CFG->adpt_cfg.Biquad_init_M[3 * biquad_idx + 2] = ext_cfg->ff_iir_medium[i].def.q;
                SD_CFG->adpt_cfg.Biquad_init_L[3 * biquad_idx + 0] = ext_cfg->ff_iir_low[i].def.gain;
                SD_CFG->adpt_cfg.Biquad_init_L[3 * biquad_idx + 1] = ext_cfg->ff_iir_low[i].def.fre;
                SD_CFG->adpt_cfg.Biquad_init_L[3 * biquad_idx + 2] = ext_cfg->ff_iir_low[i].def.q;
                SD_CFG->adpt_cfg.biquad_type[biquad_idx] = ext_cfg->ff_iir_high[i].type;
                biquad_idx += 1;
            } else { // not fix
                SD_CFG->adpt_cfg.Vrange_H[6 * flex_idx + 0] = ext_cfg->ff_iir_high[i].lower_limit.gain;
                SD_CFG->adpt_cfg.Vrange_H[6 * flex_idx + 1] = ext_cfg->ff_iir_high[i].upper_limit.gain;
                SD_CFG->adpt_cfg.Vrange_H[6 * flex_idx + 2] = ext_cfg->ff_iir_high[i].lower_limit.fre;
                SD_CFG->adpt_cfg.Vrange_H[6 * flex_idx + 3] = ext_cfg->ff_iir_high[i].upper_limit.fre;
                SD_CFG->adpt_cfg.Vrange_H[6 * flex_idx + 4] = ext_cfg->ff_iir_high[i].lower_limit.q;
                SD_CFG->adpt_cfg.Vrange_H[6 * flex_idx + 5] = ext_cfg->ff_iir_high[i].upper_limit.q;
                SD_CFG->adpt_cfg.Vrange_M[6 * flex_idx + 0] = ext_cfg->ff_iir_medium[i].lower_limit.gain;
                SD_CFG->adpt_cfg.Vrange_M[6 * flex_idx + 1] = ext_cfg->ff_iir_medium[i].upper_limit.gain;
                SD_CFG->adpt_cfg.Vrange_M[6 * flex_idx + 2] = ext_cfg->ff_iir_medium[i].lower_limit.fre;
                SD_CFG->adpt_cfg.Vrange_M[6 * flex_idx + 3] = ext_cfg->ff_iir_medium[i].upper_limit.fre;
                SD_CFG->adpt_cfg.Vrange_M[6 * flex_idx + 4] = ext_cfg->ff_iir_medium[i].lower_limit.q;
                SD_CFG->adpt_cfg.Vrange_M[6 * flex_idx + 5] = ext_cfg->ff_iir_medium[i].upper_limit.q;
                SD_CFG->adpt_cfg.Vrange_L[6 * flex_idx + 0] = ext_cfg->ff_iir_low[i].lower_limit.gain;
                SD_CFG->adpt_cfg.Vrange_L[6 * flex_idx + 1] = ext_cfg->ff_iir_low[i].upper_limit.gain;
                SD_CFG->adpt_cfg.Vrange_L[6 * flex_idx + 2] = ext_cfg->ff_iir_low[i].lower_limit.fre;
                SD_CFG->adpt_cfg.Vrange_L[6 * flex_idx + 3] = ext_cfg->ff_iir_low[i].upper_limit.fre;
                SD_CFG->adpt_cfg.Vrange_L[6 * flex_idx + 4] = ext_cfg->ff_iir_low[i].lower_limit.q;
                SD_CFG->adpt_cfg.Vrange_L[6 * flex_idx + 5] = ext_cfg->ff_iir_low[i].upper_limit.q;
                flex_idx += 1;
            }
        }

        SD_CFG->adpt_cfg.Vrange_H[flex_idx * 6 + 0] = -ext_cfg->ff_iir_high_gains->lower_limit_gain;
        SD_CFG->adpt_cfg.Vrange_H[flex_idx * 6 + 1] = -ext_cfg->ff_iir_high_gains->upper_limit_gain;
        SD_CFG->adpt_cfg.Vrange_M[flex_idx * 6 + 0] = -ext_cfg->ff_iir_medium_gains->lower_limit_gain;
        SD_CFG->adpt_cfg.Vrange_M[flex_idx * 6 + 1] = -ext_cfg->ff_iir_medium_gains->upper_limit_gain;
        SD_CFG->adpt_cfg.Vrange_L[flex_idx * 6 + 0] = -ext_cfg->ff_iir_low_gains->lower_limit_gain;
        SD_CFG->adpt_cfg.Vrange_L[flex_idx * 6 + 1] = -ext_cfg->ff_iir_low_gains->upper_limit_gain;


        SD_CFG->adpt_cfg.IIR_NUM_FLEX = flex_idx;
        SD_CFG->adpt_cfg.IIR_NUM_FIX = biquad_idx;

        for (int i = 0; i < SD_CFG->ff_yorder; i++) {
            if (!ext_cfg->ff_iir_high[i].fixed_en) { // not fix
                SD_CFG->adpt_cfg.Biquad_init_H[3 * biquad_idx + 0] = ext_cfg->ff_iir_high[i].def.gain;
                SD_CFG->adpt_cfg.Biquad_init_H[3 * biquad_idx + 1] = ext_cfg->ff_iir_high[i].def.fre;
                SD_CFG->adpt_cfg.Biquad_init_H[3 * biquad_idx + 2] = ext_cfg->ff_iir_high[i].def.q;
                SD_CFG->adpt_cfg.Biquad_init_M[3 * biquad_idx + 0] = ext_cfg->ff_iir_medium[i].def.gain;
                SD_CFG->adpt_cfg.Biquad_init_M[3 * biquad_idx + 1] = ext_cfg->ff_iir_medium[i].def.fre;
                SD_CFG->adpt_cfg.Biquad_init_M[3 * biquad_idx + 2] = ext_cfg->ff_iir_medium[i].def.q;
                SD_CFG->adpt_cfg.Biquad_init_L[3 * biquad_idx + 0] = ext_cfg->ff_iir_low[i].def.gain;
                SD_CFG->adpt_cfg.Biquad_init_L[3 * biquad_idx + 1] = ext_cfg->ff_iir_low[i].def.fre;
                SD_CFG->adpt_cfg.Biquad_init_L[3 * biquad_idx + 2] = ext_cfg->ff_iir_low[i].def.q;
                SD_CFG->adpt_cfg.biquad_type[biquad_idx] = ext_cfg->ff_iir_high[i].type;
                biquad_idx += 1;
            }
        }

        SD_CFG->adpt_cfg.Biquad_init_H[biquad_idx * 3] = -ext_cfg->ff_iir_high_gains->def_total_gain;
        SD_CFG->adpt_cfg.Biquad_init_M[biquad_idx * 3] = -ext_cfg->ff_iir_medium_gains->def_total_gain;
        SD_CFG->adpt_cfg.Biquad_init_L[biquad_idx * 3] = -ext_cfg->ff_iir_low_gains->def_total_gain;

        for (int i = 0; i < 7; i++) {
            SD_CFG->adpt_cfg.degree_set0[i] = degree_set0[i];
            SD_CFG->adpt_cfg.degree_set1[i] = degree_set1[i];
            SD_CFG->adpt_cfg.degree_set2[i] = degree_set2[i];
        }

        SD_CFG->adpt_cfg.degree_set0[0] = ext_cfg->ff_iir_general->biquad_level_l;
        SD_CFG->adpt_cfg.degree_set1[0] = ext_cfg->ff_iir_general->biquad_level_h;
        SD_CFG->adpt_cfg.degree_set2[0] = ext_cfg->ff_target_param->cmp_curve_num;
        SD_CFG->adpt_cfg.degree_set0[6] = ext_cfg->ff_iir_general->biquad_level_l;
        SD_CFG->adpt_cfg.degree_set1[6] = ext_cfg->ff_iir_general->biquad_level_h;
        SD_CFG->adpt_cfg.degree_set2[6] = ext_cfg->ff_target_param->cmp_curve_num;

        SD_CFG->adpt_cfg.degree_set0[1] = ext_cfg->ff_iir_high_gains->iir_target_gain_limit;
        SD_CFG->adpt_cfg.degree_set1[1] = ext_cfg->ff_iir_medium_gains->iir_target_gain_limit;
        SD_CFG->adpt_cfg.degree_set2[1] = ext_cfg->ff_iir_low_gains->iir_target_gain_limit;

        SD_CFG->adpt_cfg.Weight_H = ext_cfg->ff_weight_high->data;
        SD_CFG->adpt_cfg.Weight_M = ext_cfg->ff_weight_medium->data;
        SD_CFG->adpt_cfg.Weight_L = ext_cfg->ff_weight_low->data;
        SD_CFG->adpt_cfg.Gold_csv_H = ext_cfg->ff_mse_high->data;
        SD_CFG->adpt_cfg.Gold_csv_M = ext_cfg->ff_mse_medium->data;
        SD_CFG->adpt_cfg.Gold_csv_L = ext_cfg->ff_mse_low->data;

        SD_CFG->adpt_cfg.total_gain_adj_begin = ext_cfg->ff_iir_general->total_gain_freq_l;
        SD_CFG->adpt_cfg.total_gain_adj_end = ext_cfg->ff_iir_general->total_gain_freq_h;
        SD_CFG->adpt_cfg.gain_limit_all = ext_cfg->ff_iir_general->total_gain_limit;

        // 耳道记忆曲线配置
#if AUDIO_RT_ANC_SZ_PZ_CMP_EN
        SD_CFG->adpt_cfg.pz_table_cmp = audio_rtanc_pz_cmp_get();
        SD_CFG->adpt_cfg.sz_table_cmp = audio_rtanc_sz_cmp_get();
#else
        SD_CFG->adpt_cfg.pz_table_cmp = NULL;
        SD_CFG->adpt_cfg.sz_table_cmp = NULL;
#endif
        SD_CFG->adpt_cfg.mem_curve_nums = ext_cfg->ff_ear_mem_param->mem_curve_nums;
        SD_CFG->adpt_cfg.sz_table = ext_cfg->ff_ear_mem_sz->data;
        SD_CFG->adpt_cfg.pz_table = ext_cfg->ff_ear_mem_pz->data;

        //临时测试流程
#if AUDIO_RT_ANC_SELF_TALK_FLAG  // test self talk coeff
        extern const float pz_coef_table[];
        extern const float sz_coef_table[];
        SD_CFG->adpt_cfg.pz_coef_table = (float *)pz_coef_table;
        SD_CFG->adpt_cfg.sz_coef_table = (float *)sz_coef_table;
#else
        SD_CFG->adpt_cfg.pz_coef_table = NULL;
        SD_CFG->adpt_cfg.sz_coef_table = NULL;
#endif

        // 预置FF滤波器
        extern const float ff_filter[];
        SD_CFG->adpt_cfg.ff_filter = (float *)ff_filter;

#if EXT_PRINTF_DEBUG
        for (int i = 0; i < 60; i++) {
            printf("idx=%d, mse=%d, weight=%d\n", i, (int)SD_CFG->adpt_cfg.Gold_csv_H[i], (int)SD_CFG->adpt_cfg.Weight_H[i]);
        }
        for (int i = 0; i < 60; i++) {
            printf("idx=%d, mse=%d, weight=%d\n", i, (int)SD_CFG->adpt_cfg.Gold_csv_M[i], (int)SD_CFG->adpt_cfg.Weight_M[i]);
        }
        for (int i = 0; i < 60; i++) {
            printf("idx=%d, mse=%d, weight=%d\n", i, (int)SD_CFG->adpt_cfg.Gold_csv_L[i], (int)SD_CFG->adpt_cfg.Weight_L[i]);
        }

        de_vrange_printf(SD_CFG->adpt_cfg.Vrange_H, SD_CFG->adpt_cfg.IIR_NUM_FLEX + SD_CFG->adpt_cfg.IIR_NUM_FIX);
        de_vrange_printf(SD_CFG->adpt_cfg.Vrange_M, SD_CFG->adpt_cfg.IIR_NUM_FLEX + SD_CFG->adpt_cfg.IIR_NUM_FIX);
        de_vrange_printf(SD_CFG->adpt_cfg.Vrange_L, SD_CFG->adpt_cfg.IIR_NUM_FLEX + SD_CFG->adpt_cfg.IIR_NUM_FIX);

        de_biquad_printf(SD_CFG->adpt_cfg.Biquad_init_H, SD_CFG->adpt_cfg.IIR_NUM_FLEX + SD_CFG->adpt_cfg.IIR_NUM_FIX);
        de_biquad_printf(SD_CFG->adpt_cfg.Biquad_init_M, SD_CFG->adpt_cfg.IIR_NUM_FLEX + SD_CFG->adpt_cfg.IIR_NUM_FIX);
        de_biquad_printf(SD_CFG->adpt_cfg.Biquad_init_L, SD_CFG->adpt_cfg.IIR_NUM_FLEX + SD_CFG->adpt_cfg.IIR_NUM_FIX);
#endif

#if TCFG_AUDIO_ANC_CH == (ANC_L_CH | ANC_R_CH)
        /***************************************************/
        /*************** right channel config **************/
        /***************************************************/
        SD_CFG->adpt_cfg_r.pnc_times = ext_cfg->base_cfg->adaptive_times;

        // de alg config
        SD_CFG->adpt_cfg_r.high_fgq_fix = 1; // gali TODO
        SD_CFG->adpt_cfg_r.de_alg_sel = 1; // TODO

        // target配置
        SD_CFG->adpt_cfg_r.cmp_en = ext_cfg->rff_target_param->cmp_en;
        SD_CFG->adpt_cfg_r.target_cmp_num = ext_cfg->rff_target_param->cmp_curve_num;
        SD_CFG->adpt_cfg_r.target_sv = ext_cfg->rff_target_sv->data;
        SD_CFG->adpt_cfg_r.target_cmp_dat = ext_cfg->rff_target_cmp->data;

        // 算法配置
        SD_CFG->adpt_cfg_r.IIR_NUM_FLEX = 0;
        flex_idx = 0;
        biquad_idx = 0;
        for (int i = 0; i < SD_CFG->ff_yorder; i++) {
            if (ext_cfg->rff_iir_high[i].fixed_en) { // fix
                SD_CFG->adpt_cfg_r.Biquad_init_H[3 * biquad_idx + 0] = ext_cfg->rff_iir_high[i].def.gain;
                SD_CFG->adpt_cfg_r.Biquad_init_H[3 * biquad_idx + 1] = ext_cfg->rff_iir_high[i].def.fre;
                SD_CFG->adpt_cfg_r.Biquad_init_H[3 * biquad_idx + 2] = ext_cfg->rff_iir_high[i].def.q;
                SD_CFG->adpt_cfg_r.Biquad_init_M[3 * biquad_idx + 0] = ext_cfg->rff_iir_medium[i].def.gain;
                SD_CFG->adpt_cfg_r.Biquad_init_M[3 * biquad_idx + 1] = ext_cfg->rff_iir_medium[i].def.fre;
                SD_CFG->adpt_cfg_r.Biquad_init_M[3 * biquad_idx + 2] = ext_cfg->rff_iir_medium[i].def.q;
                SD_CFG->adpt_cfg_r.Biquad_init_L[3 * biquad_idx + 0] = ext_cfg->rff_iir_low[i].def.gain;
                SD_CFG->adpt_cfg_r.Biquad_init_L[3 * biquad_idx + 1] = ext_cfg->rff_iir_low[i].def.fre;
                SD_CFG->adpt_cfg_r.Biquad_init_L[3 * biquad_idx + 2] = ext_cfg->rff_iir_low[i].def.q;
                SD_CFG->adpt_cfg_r.biquad_type[biquad_idx] = ext_cfg->rff_iir_high[i].type;
                biquad_idx += 1;
            } else { // not fix
                SD_CFG->adpt_cfg_r.Vrange_H[6 * flex_idx + 0] = ext_cfg->rff_iir_high[i].lower_limit.gain;
                SD_CFG->adpt_cfg_r.Vrange_H[6 * flex_idx + 1] = ext_cfg->rff_iir_high[i].upper_limit.gain;
                SD_CFG->adpt_cfg_r.Vrange_H[6 * flex_idx + 2] = ext_cfg->rff_iir_high[i].lower_limit.fre;
                SD_CFG->adpt_cfg_r.Vrange_H[6 * flex_idx + 3] = ext_cfg->rff_iir_high[i].upper_limit.fre;
                SD_CFG->adpt_cfg_r.Vrange_H[6 * flex_idx + 4] = ext_cfg->rff_iir_high[i].lower_limit.q;
                SD_CFG->adpt_cfg_r.Vrange_H[6 * flex_idx + 5] = ext_cfg->rff_iir_high[i].upper_limit.q;
                SD_CFG->adpt_cfg_r.Vrange_M[6 * flex_idx + 0] = ext_cfg->rff_iir_medium[i].lower_limit.gain;
                SD_CFG->adpt_cfg_r.Vrange_M[6 * flex_idx + 1] = ext_cfg->rff_iir_medium[i].upper_limit.gain;
                SD_CFG->adpt_cfg_r.Vrange_M[6 * flex_idx + 2] = ext_cfg->rff_iir_medium[i].lower_limit.fre;
                SD_CFG->adpt_cfg_r.Vrange_M[6 * flex_idx + 3] = ext_cfg->rff_iir_medium[i].upper_limit.fre;
                SD_CFG->adpt_cfg_r.Vrange_M[6 * flex_idx + 4] = ext_cfg->rff_iir_medium[i].lower_limit.q;
                SD_CFG->adpt_cfg_r.Vrange_M[6 * flex_idx + 5] = ext_cfg->rff_iir_medium[i].upper_limit.q;
                SD_CFG->adpt_cfg_r.Vrange_L[6 * flex_idx + 0] = ext_cfg->rff_iir_low[i].lower_limit.gain;
                SD_CFG->adpt_cfg_r.Vrange_L[6 * flex_idx + 1] = ext_cfg->rff_iir_low[i].upper_limit.gain;
                SD_CFG->adpt_cfg_r.Vrange_L[6 * flex_idx + 2] = ext_cfg->rff_iir_low[i].lower_limit.fre;
                SD_CFG->adpt_cfg_r.Vrange_L[6 * flex_idx + 3] = ext_cfg->rff_iir_low[i].upper_limit.fre;
                SD_CFG->adpt_cfg_r.Vrange_L[6 * flex_idx + 4] = ext_cfg->rff_iir_low[i].lower_limit.q;
                SD_CFG->adpt_cfg_r.Vrange_L[6 * flex_idx + 5] = ext_cfg->rff_iir_low[i].upper_limit.q;
                flex_idx += 1;
            }
        }

        SD_CFG->adpt_cfg_r.Vrange_H[flex_idx * 6 + 0] = -ext_cfg->rff_iir_high_gains->lower_limit_gain;
        SD_CFG->adpt_cfg_r.Vrange_H[flex_idx * 6 + 1] = -ext_cfg->rff_iir_high_gains->upper_limit_gain;
        SD_CFG->adpt_cfg_r.Vrange_M[flex_idx * 6 + 0] = -ext_cfg->rff_iir_medium_gains->lower_limit_gain;
        SD_CFG->adpt_cfg_r.Vrange_M[flex_idx * 6 + 1] = -ext_cfg->rff_iir_medium_gains->upper_limit_gain;
        SD_CFG->adpt_cfg_r.Vrange_L[flex_idx * 6 + 0] = -ext_cfg->rff_iir_low_gains->lower_limit_gain;
        SD_CFG->adpt_cfg_r.Vrange_L[flex_idx * 6 + 1] = -ext_cfg->rff_iir_low_gains->upper_limit_gain;

        SD_CFG->adpt_cfg_r.IIR_NUM_FLEX = flex_idx;
        SD_CFG->adpt_cfg_r.IIR_NUM_FIX = biquad_idx;

        for (int i = 0; i < SD_CFG->ff_yorder; i++) {
            if (!ext_cfg->rff_iir_high[i].fixed_en) { // not fix
                SD_CFG->adpt_cfg_r.Biquad_init_H[3 * biquad_idx + 0] = ext_cfg->rff_iir_high[i].def.gain;
                SD_CFG->adpt_cfg_r.Biquad_init_H[3 * biquad_idx + 1] = ext_cfg->rff_iir_high[i].def.fre;
                SD_CFG->adpt_cfg_r.Biquad_init_H[3 * biquad_idx + 2] = ext_cfg->rff_iir_high[i].def.q;
                SD_CFG->adpt_cfg_r.Biquad_init_M[3 * biquad_idx + 0] = ext_cfg->rff_iir_medium[i].def.gain;
                SD_CFG->adpt_cfg_r.Biquad_init_M[3 * biquad_idx + 1] = ext_cfg->rff_iir_medium[i].def.fre;
                SD_CFG->adpt_cfg_r.Biquad_init_M[3 * biquad_idx + 2] = ext_cfg->rff_iir_medium[i].def.q;
                SD_CFG->adpt_cfg_r.Biquad_init_L[3 * biquad_idx + 0] = ext_cfg->rff_iir_low[i].def.gain;
                SD_CFG->adpt_cfg_r.Biquad_init_L[3 * biquad_idx + 1] = ext_cfg->rff_iir_low[i].def.fre;
                SD_CFG->adpt_cfg_r.Biquad_init_L[3 * biquad_idx + 2] = ext_cfg->rff_iir_low[i].def.q;
                SD_CFG->adpt_cfg_r.biquad_type[biquad_idx] = ext_cfg->rff_iir_high[i].type;
                biquad_idx += 1;
            }
        }

        SD_CFG->adpt_cfg_r.Biquad_init_H[biquad_idx * 3] = -ext_cfg->rff_iir_high_gains->def_total_gain;
        SD_CFG->adpt_cfg_r.Biquad_init_M[biquad_idx * 3] = -ext_cfg->rff_iir_medium_gains->def_total_gain;
        SD_CFG->adpt_cfg_r.Biquad_init_L[biquad_idx * 3] = -ext_cfg->rff_iir_low_gains->def_total_gain;

        for (int i = 0; i < 7; i++) {
            SD_CFG->adpt_cfg_r.degree_set0[i] = degree_set0[i];
            SD_CFG->adpt_cfg_r.degree_set1[i] = degree_set1[i];
            SD_CFG->adpt_cfg_r.degree_set2[i] = degree_set2[i];
        }

        SD_CFG->adpt_cfg_r.degree_set0[0] = ext_cfg->rff_iir_general->biquad_level_l;
        SD_CFG->adpt_cfg_r.degree_set1[0] = ext_cfg->rff_iir_general->biquad_level_h;
        SD_CFG->adpt_cfg_r.degree_set2[0] = ext_cfg->rff_target_param->cmp_curve_num;
        SD_CFG->adpt_cfg_r.degree_set0[6] = ext_cfg->rff_iir_general->biquad_level_l;
        SD_CFG->adpt_cfg_r.degree_set1[6] = ext_cfg->rff_iir_general->biquad_level_h;
        SD_CFG->adpt_cfg_r.degree_set2[6] = ext_cfg->rff_target_param->cmp_curve_num;

        SD_CFG->adpt_cfg_r.degree_set0[1] = ext_cfg->rff_iir_high_gains->iir_target_gain_limit;
        SD_CFG->adpt_cfg_r.degree_set1[1] = ext_cfg->rff_iir_medium_gains->iir_target_gain_limit;
        SD_CFG->adpt_cfg_r.degree_set2[1] = ext_cfg->rff_iir_low_gains->iir_target_gain_limit;

        SD_CFG->adpt_cfg_r.Weight_H = ext_cfg->rff_weight_high->data;
        SD_CFG->adpt_cfg_r.Weight_M = ext_cfg->rff_weight_medium->data;
        SD_CFG->adpt_cfg_r.Weight_L = ext_cfg->rff_weight_low->data;
        SD_CFG->adpt_cfg_r.Gold_csv_H = ext_cfg->rff_mse_high->data;
        SD_CFG->adpt_cfg_r.Gold_csv_M = ext_cfg->rff_mse_medium->data;
        SD_CFG->adpt_cfg_r.Gold_csv_L = ext_cfg->rff_mse_low->data;

        SD_CFG->adpt_cfg_r.total_gain_adj_begin = ext_cfg->rff_iir_general->total_gain_freq_l;
        SD_CFG->adpt_cfg_r.total_gain_adj_end = ext_cfg->rff_iir_general->total_gain_freq_h;
        SD_CFG->adpt_cfg_r.gain_limit_all = ext_cfg->rff_iir_general->total_gain_limit;

        // 耳道记忆曲线配置
        SD_CFG->adpt_cfg_r.mem_curve_nums = ext_cfg->rff_ear_mem_param->mem_curve_nums;
        SD_CFG->adpt_cfg_r.sz_table = ext_cfg->rff_ear_mem_sz->data;
        SD_CFG->adpt_cfg_r.pz_table = ext_cfg->rff_ear_mem_pz->data;
        SD_CFG->adpt_cfg_r.pz_table_cmp = NULL;
        SD_CFG->adpt_cfg_r.sz_table_cmp = NULL;
#if EXT_PRINTF_DEBUG
        for (int i = 0; i < 60; i++) {
            printf("idx=%d, mse=%d, weight=%d\n", i, (int)SD_CFG->adpt_cfg.Gold_csv_H[i], (int)SD_CFG->adpt_cfg.Weight_H[i]);
        }

        de_vrange_printf(SD_CFG->adpt_cfg.Vrange_H, SD_CFG->adpt_cfg.IIR_NUM_FLEX + SD_CFG->adpt_cfg.IIR_NUM_FIX);
        de_vrange_printf(SD_CFG->adpt_cfg.Vrange_M, SD_CFG->adpt_cfg.IIR_NUM_FLEX + SD_CFG->adpt_cfg.IIR_NUM_FIX);
        de_vrange_printf(SD_CFG->adpt_cfg.Vrange_L, SD_CFG->adpt_cfg.IIR_NUM_FLEX + SD_CFG->adpt_cfg.IIR_NUM_FIX);

        de_biquad_printf(SD_CFG->adpt_cfg.Biquad_init_H, SD_CFG->adpt_cfg.IIR_NUM_FLEX + SD_CFG->adpt_cfg.IIR_NUM_FIX);
        de_biquad_printf(SD_CFG->adpt_cfg.Biquad_init_M, SD_CFG->adpt_cfg.IIR_NUM_FLEX + SD_CFG->adpt_cfg.IIR_NUM_FIX);
        de_biquad_printf(SD_CFG->adpt_cfg.Biquad_init_L, SD_CFG->adpt_cfg.IIR_NUM_FLEX + SD_CFG->adpt_cfg.IIR_NUM_FIX);
#endif

#endif
    }
}

void icsd_anc_v2_sz_pz_cmp_calculate(struct sz_pz_cmp_cal_param *p)
{
    float eq_freq_l = 50;
    float eq_freq_h = 500;
    u8 mode = 1;	//1 eq gain; 0 eq 频响

#if 0
    printf("ff_gain %d/100\n", (int)(p->ff_gain * 100.0f));
    printf("fb_gain %d/100\n", (int)(p->fb_gain * 100.0f));

    struct aeq_default_seg_tab *eq_tab = p->spk_eq_tab;
    printf("global_gain %d/100\n", (int)(eq_tab->global_gain * 100.f));
    printf("seg_num %d\n", eq_tab->seg_num);
    for (int i = 0; i < eq_tab->seg_num; i++) {
        printf("index %d\n", eq_tab->seg[i].index);
        printf("iir_type %d\n", eq_tab->seg[i].iir_type);
        printf("freq %d\n", eq_tab->seg[i].freq);
        printf("gain %d/100\n", (int)(eq_tab->seg[i].gain * 100.f));
        printf("q %d/100\n", (int)(eq_tab->seg[i].q * 100.f));
    }
#endif
    szpz_cmp_get(p->pz_cmp_out, p->sz_cmp_out, p->ff_gain, p->fb_gain, \
                 (struct aeq_default_seg_tab *)p->spk_eq_tab, eq_freq_l, eq_freq_h, mode);
#if 0
    printf("sz_pz output\n");
    for (int i = 0; i < 50; i++) {
        printf("pz %d/100\n", (int)(p->pz_cmp_out[i] * 100.0f));
        printf("sz %d/100\n", (int)(p->sz_cmp_out[i] * 100.0f));
    }
#endif
}

const float pz_coef_table[] = {
    0.6309, -35.6217, 2405.4900, 0.9784, 2.1714, 1799.1100, 1.4981, 1.0476, 202.5490, 0.7323, 4.6732, 770.9220, 0.3000, 4.0110, 142.5050, 0.8153,
    0.7252, -39.9994, 3289.7500, 0.9990, -1.1870, 1490.2700, 1.4188, 0.8612, 1392.4400, 0.6970, 3.5370, 575.0420, 0.3000, 3.2454, 149.9670, 0.8000,
    0.7377, -37.7070, 2846.4200, 0.9302, 1.4300, 1799.9900, 0.8536, 1.2857, 930.7790, 1.3418, 3.7174, 573.7440, 0.3111, 3.0004, 145.2640, 0.8012,
    0.7377, -39.5501, 2976.6800, 0.8414, 2.0115, 1799.9900, 1.5000, -1.3149, 484.9280, 1.2478, 6.5708, 717.7790, 0.4033, 2.8818, 149.9520, 0.8001,
    0.7923, -39.9999, 3354.0000, 0.8874, 0.8993, 1154.6000, 1.4993, 1.0890, 859.9730, 1.4935, 3.6678, 712.2030, 0.3001, 2.5910, 148.6280, 0.8000,
    0.7780, -37.8563, 3143.8500, 0.9446, 1.6870, 1793.8900, 1.5000, 1.1921, 205.0860, 0.8283, 3.3771, 760.5070, 0.4991, 2.4997, 131.7680, 0.8205,
    0.8616, -39.2417, 3482.5700, 0.9786, 0.5020, 1799.5000, 1.5000, 2.1440, 921.7950, 1.0211, 1.7336, 309.8810, 0.3017, 1.7925, 92.2495, 0.8610,
    0.8377, -39.8390, 3633.0700, 0.9405, 1.7523, 1259.4200, 0.7865, 1.1651, 726.4010, 1.5000, 1.9267, 301.5120, 0.3045, 2.8973, 54.0851, 0.8024,
    0.9102, -40.0000, 3577.2700, 0.9457, -1.5955, 1325.2000, 1.1630, 3.2084, 1099.7300, 1.0180, 2.0132, 738.1840, 0.3001, 1.6762, 121.1390, 0.8000,
    0.9384, -29.7053, 2592.9900, 0.9375, -4.4759, 1750.5800, 0.6690, 5.1476, 1261.2600, 0.8704, 2.1215, 792.2250, 0.3000, 3.1830, 56.2796, 0.8007,
    0.8974, -37.1190, 3678.6000, 0.9996, -1.7091, 1785.1500, 0.3000, 2.0727, 922.6710, 0.7014, 1.5687, 479.6110, 0.3101, 1.6646, 85.9495, 0.8044,
    0.9216, -31.5536, 2118.0400, 0.9648, 5.8225, 1799.3600, 0.9665, 2.9175, 925.5530, 0.8682, 0.8403, 198.4510, 0.5267, 1.2778, 74.6307, 0.9464,
    0.9913, -27.1542, 2398.1900, 0.9999, -5.0971, 1749.7700, 0.3000, 6.7166, 1219.7000, 0.5624, 1.2765, 799.5670, 0.3050, 0.9591, 149.8340, 0.8132,
    0.9665, -36.1313, 2873.5900, 0.9760, -0.9605, 1366.7200, 1.4880, 5.4105, 1467.3900, 0.5997, 0.4439, 201.7350, 0.7079, 0.7258, 96.0369, 0.8698,
    0.9995, -38.3067, 3312.0300, 0.9492, 4.0306, 1579.8400, 0.5732, 0.8576, 805.6700, 1.4972, 0.3084, 187.1140, 0.3169, 0.3529, 69.6379, 0.9190,
    1.0083, -30.5480, 2512.1700, 0.9999, 3.1625, 1727.0500, 1.4989, 2.1934, 991.4920, 0.9737, 0.3798, 501.8400, 0.3000, 0.7687, 55.4092, 0.8845,
    1.0782, -38.5307, 3633.8300, 0.9455, 1.5344, 1798.5400, 0.3231, 2.2199, 1142.4500, 1.4999, 0.8503, 799.9760, 1.2105, 0.2886, 56.5557, 0.8011,
    1.0937, -35.3407, 2414.8000, 0.8000, -3.2533, 1394.1400, 1.4713, 12.0671, 1277.7600, 0.6522, -0.8996, 540.1360, 0.6842, 0.0144, 50.2329, 0.9991,
    1.0820, -37.3928, 3686.4600, 0.9990, 1.1244, 1146.2000, 1.0026, 2.3376, 1075.2900, 0.8327, -0.3799, 723.7250, 0.6151, 0.0033, 55.3229, 0.8000,
    1.0632, -32.8306, 2783.2700, 0.9992, 4.7155, 1489.1100, 0.6428, -0.4064, 499.4020, 0.8505, -0.1889, 157.2940, 0.8815, 0.0190, 53.4310, 0.9971,
    1.0989, -39.4497, 3654.7700, 0.8641, 4.3456, 1603.5900, 0.9014, 1.6066, 1071.8900, 1.1337, 1.6071, 799.7900, 1.1733, -0.5308, 150.0000, 0.9191,
    1.1059, -37.6223, 3419.7900, 0.8689, 4.2884, 1621.5200, 1.1986, 3.6754, 960.1850, 1.0286, -0.4598, 158.6980, 0.8000, -0.4906, 73.8159, 0.8837,
    1.1394, -35.9876, 3700.0000, 0.9485, -0.9767, 1460.7400, 1.4999, 4.7492, 1185.1500, 0.9566, -0.3459, 302.9650, 0.5118, -0.3504, 144.2910, 0.8312,
    1.1199, -31.7639, 2221.0800, 0.8010, 7.1264, 1713.9200, 0.9268, 4.4621, 1021.5700, 0.8505, -0.5969, 136.5560, 0.5910, -0.3917, 52.3539, 0.8348,
    1.1346, -33.6829, 3156.2900, 1.0000, 2.7403, 1754.4900, 1.2364, 3.0207, 1149.4500, 0.8298, -0.6459, 391.7260, 0.3009, -0.5652, 140.0860, 0.8000,
    1.1104, -36.3691, 3661.0900, 0.9985, 3.4996, 1436.5900, 0.8264, -0.4994, 201.4640, 0.3006, 0.4988, 799.9710, 1.5000, -0.3231, 50.9117, 0.8794,
    1.1943, -28.1539, 2618.8200, 0.9456, 2.0381, 1799.9800, 1.3903, 4.5390, 1137.4800, 1.1632, -0.6411, 160.1120, 0.3006, -0.3057, 150.0000, 0.9119,
    1.1480, -30.3012, 2623.5400, 0.9182, 5.3084, 1699.1800, 1.2019, 3.2078, 1068.2700, 1.0428, -0.7155, 148.5980, 0.3029, -0.5906, 53.0141, 0.8960,
    1.1573, -30.7110, 2569.0400, 0.9817, 3.0728, 1789.0800, 0.7570, 4.8258, 1499.9700, 0.7735, -0.9359, 324.4970, 0.3005, -0.8028, 95.7922, 0.8320,
    1.1735, -30.3142, 3309.2200, 0.9998, 1.9568, 1800.0000, 1.3708, 3.2468, 1192.0100, 1.2108, -0.6360, 160.7840, 0.3072, -0.3862, 73.5547, 0.8460,
    1.1683, -29.2307, 2736.0000, 0.9859, 4.2212, 1737.5200, 1.4188, 3.5553, 1206.4900, 0.9547, -0.8097, 282.3570, 0.3012, -0.7009, 123.6470, 0.8709,
    1.1805, -26.0207, 2675.6500, 0.9963, 3.2775, 1800.0000, 1.0286, 3.3110, 1277.6800, 1.0751, -0.6256, 395.3420, 0.3177, -0.5446, 149.2080, 0.9482,
    1.1968, -25.1495, 2496.2800, 0.8752, 5.0364, 1656.7000, 1.2136, 3.3954, 1141.4300, 1.4062, -0.5548, 253.7520, 1.0339, -0.8491, 146.0630, 0.8499,
    1.1878, -33.5881, 3206.3200, 0.9667, 6.0061, 1663.8500, 1.0668, 2.1161, 1048.2100, 1.1319, -0.8914, 275.9180, 0.3001, -0.9309, 117.4520, 0.8000,
    1.1798, -27.5705, 2311.6800, 0.9989, 5.0618, 1761.6100, 0.8493, 4.5577, 1500.0000, 0.9128, -1.0754, 336.9620, 0.3000, -0.5084, 143.1730, 1.0000,
    1.2109, -28.5440, 3080.9600, 0.9954, 3.8706, 1787.9100, 1.1064, 3.2782, 1217.1800, 1.4988, -0.7692, 244.7270, 0.3716, -0.6035, 149.9510, 1.0000,
    1.1930, -27.1103, 2341.5900, 0.9532, 6.0199, 1761.4700, 1.4796, 4.8994, 1231.8000, 0.9932, -1.0192, 265.3490, 0.3000, -0.6664, 116.3520, 0.9991,
    1.2083, -31.1789, 2907.0300, 0.9993, 8.0037, 1569.7500, 0.8917, -0.5619, 589.5030, 1.2493, -1.1891, 171.1360, 0.3205, -0.9913, 64.1141, 0.9636,
    1.2116, -27.2083, 2910.9700, 0.9853, 4.5850, 1799.9400, 1.0265, 3.3174, 1269.2700, 1.3893, -0.7840, 258.3210, 0.3000, -0.5930, 143.1550, 0.8372,
    1.2068, -32.1789, 3204.2100, 0.9604, 4.5475, 1684.4300, 1.3794, 4.5277, 1256.2600, 0.8774, -1.1833, 437.6330, 0.3055, -1.0487, 150.0000, 0.9327,
    1.1984, -25.9711, 2850.0600, 1.0000, 7.0828, 1619.4300, 0.7588, -0.8058, 200.0000, 0.3000, -1.0852, 757.6030, 0.9034, -0.4454, 91.8044, 0.8103,
    1.2038, -31.0129, 2945.7700, 0.8581, 6.1226, 1733.2600, 1.1060, 4.8907, 1355.9900, 0.7583, -1.0948, 358.2400, 0.3091, -0.8312, 149.1610, 0.9785,
    1.2154, -27.6349, 3110.0100, 0.9237, 8.9321, 1459.9600, 0.7807, 4.5973, 528.4020, 0.5878, -6.5617, 603.6330, 0.4825, -0.8793, 149.9860, 0.8124,
    1.2136, -25.8810, 2758.9800, 0.9866, 8.8902, 1600.5900, 0.8601, -0.6153, 818.6830, 1.4400, -1.1921, 546.9940, 0.3136, -0.9306, 131.4510, 0.8012,
    1.2240, -31.3840, 3698.5500, 0.9912, 7.2710, 1573.5500, 0.8542, -0.8032, 729.7290, 0.8143, -0.7499, 282.0860, 0.3055, -0.6887, 149.8960, 0.9725,
    1.2183, -29.9930, 3411.8500, 0.9967, 6.5522, 1799.3500, 0.9819, 2.2428, 1363.7400, 1.1724, -0.9769, 493.6200, 0.3022, -0.8175, 150.0000, 0.9945,
    1.2057, -27.0334, 3700.0000, 1.0000, 1.4218, 1799.5100, 1.4953, 4.7711, 1499.2500, 1.0318, -0.7527, 377.3690, 0.3067, -0.6641, 149.9860, 0.9225,
    1.1865, -25.6835, 2714.2200, 0.9854, 8.6592, 1791.3500, 1.2420, 1.9020, 1318.6300, 0.9196, -0.9889, 371.0060, 0.3112, -0.8163, 148.4820, 0.9126,
    1.1781, -25.3879, 3188.0300, 0.9987, 7.9361, 1783.9800, 1.2760, 0.5185, 1021.4400, 1.4288, -0.8855, 238.5650, 0.3000, -0.5294, 130.0760, 0.9907,
    1.1927, -24.3124, 3445.6800, 0.9359, 3.8950, 1800.0000, 1.4639, 3.6420, 1424.7900, 1.0498, -0.6941, 360.1870, 0.3003, -0.5380, 149.9870, 0.9380,
    1.1692, -22.5137, 3100.8400, 0.9960, 6.6878, 1800.0000, 1.4620, 1.2379, 1339.8700, 0.8356, -0.8293, 235.6310, 0.3016, -0.4913, 114.1560, 0.9583,
    1.1835, -26.3769, 2464.9000, 0.8995, 13.1633, 1769.0000, 0.9466, 0.2246, 347.4060, 1.1711, -1.4375, 474.6000, 0.4309, -1.1220, 145.5150, 0.8312,
    1.2083, -28.7204, 3494.8000, 0.9998, 9.3066, 1762.6700, 0.9175, -1.3130, 919.0920, 0.6324, -0.9724, 131.2400, 0.3000, -0.1061, 96.0791, 0.9940,
    1.1848, -23.6764, 3030.2500, 0.9943, 8.6490, 1769.0600, 1.4640, 0.8868, 1074.8800, 1.0712, -0.8488, 305.0490, 0.3003, -0.6572, 150.0000, 0.9936,
    1.1807, -25.0628, 3698.4400, 1.0000, 6.5035, 1795.5800, 1.0097, 0.4739, 1360.4400, 1.4996, -0.7326, 449.4500, 0.3000, -0.5606, 150.0000, 0.9999,
    1.1833, -25.0638, 3586.8900, 1.0000, 7.5460, 1798.5400, 1.1095, -0.5736, 200.1780, 0.5699, -0.5420, 613.7180, 0.8250, -0.6150, 126.1000, 0.9116,
    1.1848, -22.0534, 3550.2100, 0.9591, 5.7096, 1799.8700, 1.2927, 1.2697, 1376.7900, 0.9911, -0.6133, 319.5420, 0.3000, -0.4290, 149.9920, 0.9986,
    1.1615, -21.0958, 2817.0200, 0.8629, 9.7617, 1800.0000, 1.4056, 0.7958, 1019.1600, 1.4999, -0.7736, 185.2430, 0.3180, -0.5044, 84.0567, 0.8346,
    1.1811, -15.1868, 2005.2500, 0.9834, 9.8132, 1787.7500, 1.1864, -0.6797, 553.3940, 0.8236, -0.6421, 133.3860, 0.6459, -0.0793, 131.3510, 0.8000,
    1.1824, -15.2760, 2019.6500, 0.9801, 10.3014, 1782.4300, 1.1818, -0.7401, 673.2890, 0.5984, -1.2055, 100.6050, 0.5488, 0.5953, 149.9880, 0.9592,
};
const float sz_coef_table[] = {
    0.8744, -33.2736, 1710.4100, 0.9999, -0.5970, 1736.4500, 0.4804, 8.8650, 1500.0000, 0.4688, 10.7954, 200.1110, 0.3000, -0.8851, 65.6990, 0.9998,
    1.1024, -35.6891, 2426.3000, 0.9995, -1.9548, 1000.5900, 0.3744, 4.0603, 1146.4100, 0.5379, 9.4065, 244.4960, 0.3000, -3.0577, 54.8532, 0.9993,
    1.0800, -39.9602, 3012.6600, 0.8916, 2.5107, 1146.8300, 1.1470, 2.9589, 634.4930, 0.7371, 8.8718, 201.2170, 0.3000, -3.6095, 57.5833, 1.0000,
    0.9564, -36.7409, 2416.1500, 0.9859, -0.8322, 1793.7800, 0.3000, 4.4935, 1499.8500, 0.5477, 9.5640, 229.6040, 0.3000, -4.2073, 50.3275, 0.8703,
    1.0612, -39.9999, 3081.8600, 0.9945, -0.3468, 1716.1400, 1.5000, 1.9787, 1010.2200, 0.6347, 8.6326, 209.4170, 0.3000, -5.3086, 53.4313, 0.8619,
    0.9479, -35.8525, 1963.6000, 0.9998, 6.1436, 1687.6300, 0.5884, 2.5313, 772.6870, 0.4200, 8.7658, 140.6670, 0.3016, -6.5833, 77.2113, 0.8025,
    1.0954, -36.9735, 2436.7900, 0.9234, 1.6944, 1723.9900, 1.1239, 3.6345, 1028.1200, 0.9393, 7.6418, 227.9060, 0.3000, -5.7782, 61.6443, 0.9950,
    1.0188, -38.1955, 2415.5500, 0.9802, 3.0128, 1706.2500, 0.8220, 2.4772, 1033.7300, 0.5758, 7.3452, 200.5230, 0.3004, -6.4833, 64.2537, 0.9003,
    1.1250, -40.0000, 2868.8600, 0.9922, -2.0046, 1385.6900, 1.4966, 4.7198, 1281.6400, 0.9320, 6.5092, 209.7070, 0.3003, -7.3873, 61.0644, 0.8965,
    1.1396, -40.0000, 2754.4200, 0.9325, 1.0751, 1673.5000, 0.9860, 4.0243, 1041.0000, 0.8327, 6.2931, 201.2810, 0.3140, -8.6755, 62.9974, 0.9557,
    1.0388, -39.9119, 2802.9900, 0.9999, 1.7355, 1646.4400, 1.2457, 1.8734, 1138.4900, 0.8843, 6.2946, 219.0600, 0.3000, -7.8133, 65.6887, 1.0000,
    1.0023, -38.8452, 2624.4700, 0.9888, 3.6252, 1600.8800, 1.0945, 1.7297, 954.5030, 1.1506, 6.2116, 204.8080, 0.3077, -8.9741, 62.8095, 0.9886,
    1.1481, -35.3906, 1862.6100, 0.9877, 6.5766, 1702.2500, 0.6601, 3.1345, 1011.3500, 1.1600, 4.8777, 187.2300, 0.3419, -8.9726, 71.8474, 0.9996,
    1.0404, -35.5778, 2413.8800, 0.9677, -2.5926, 1800.0000, 0.3007, 6.2792, 1376.6900, 0.7028, 5.2154, 263.7600, 0.3067, -9.4163, 61.9523, 1.0000,
    1.0333, -39.9679, 2790.3400, 0.9682, 2.3528, 1721.4500, 1.4999, 2.9107, 1116.1300, 1.0611, 4.5607, 231.0690, 0.3001, -9.9933, 69.9046, 0.9390,
    1.0440, -38.8814, 2218.0000, 0.8770, -0.1292, 1605.5300, 1.5000, 7.5604, 1361.0700, 0.6601, 4.2701, 285.2890, 0.3000, -9.9723, 69.5998, 0.9999,
    1.1447, -35.9387, 2149.9400, 0.9093, 7.0984, 1246.7800, 0.7333, 0.7373, 200.0210, 0.9420, 2.6896, 229.4450, 0.3128, -9.9809, 78.5536, 1.0000,
    1.1578, -38.3516, 2107.4300, 0.8142, 2.6853, 1789.2200, 0.7069, 7.2198, 1122.8000, 0.7621, 3.2258, 291.8040, 0.3211, -9.9833, 85.3899, 0.9660,
    1.0285, -39.6693, 3037.1800, 0.9261, 3.4986, 1295.8700, 0.9587, 1.3795, 1052.3000, 1.3987, 3.7689, 210.0090, 0.3043, -9.1734, 81.8662, 1.0000,
    0.9687, -39.9960, 2841.0200, 0.9996, 3.9849, 1549.1700, 0.7636, 1.2003, 1127.6400, 0.6490, 3.2904, 207.1990, 0.5456, -9.9637, 72.5837, 0.9984,
    1.0183, -39.6436, 2875.9100, 0.9961, 3.3280, 1431.1900, 0.9539, 1.1942, 1052.7700, 0.7455, 2.8452, 201.7760, 0.4382, -9.9984, 94.7829, 1.0000,
    0.9761, -34.5509, 2321.4300, 0.9892, 3.9444, 1418.0000, 1.1945, 4.0074, 560.6460, 0.3075, -1.9024, 553.7340, 0.8905, -9.9995, 78.9091, 0.9977,
    1.0509, -29.2095, 1823.6000, 0.9931, 7.0724, 1290.4900, 0.9708, 1.9972, 200.2270, 0.7125, 0.3569, 800.0000, 0.5633, -9.9818, 90.8445, 0.9999,
    0.9264, -30.7840, 2702.1500, 1.0000, -5.4705, 1674.0300, 0.3000, 5.6467, 1319.0400, 0.9596, 3.8299, 498.3610, 0.3013, -9.9852, 85.3953, 0.9992,
    0.9028, -32.7630, 1880.3300, 0.8917, 5.1228, 1538.4000, 0.9969, 5.1166, 1156.3600, 0.6784, 2.2534, 195.1240, 0.5799, -9.4613, 100.2740, 0.9958,
    0.8690, -30.3151, 2063.9100, 0.9746, 7.2501, 1427.7900, 0.9586, 1.7901, 200.5340, 0.5972, 0.2247, 210.3710, 0.3000, -9.9826, 90.7994, 0.9997,
    1.0059, -38.5067, 3180.9900, 0.9730, 5.0534, 1128.4600, 0.7829, -1.3039, 790.1890, 1.3551, 1.9484, 160.1730, 1.4986, -9.9953, 135.0030, 0.9997,
    0.8805, -34.8576, 2623.9500, 0.9966, 1.8256, 1677.3400, 1.4840, 4.0810, 1309.2000, 0.9733, 1.5128, 221.6320, 0.6339, -9.9955, 105.6440, 0.9992,
    0.8135, -35.6203, 2905.8900, 0.9954, 1.8231, 1604.2800, 1.4177, 3.6576, 1335.4700, 1.0552, 1.5101, 196.9560, 0.6943, -9.9986, 110.2590, 0.9978,
    0.8227, -32.2627, 2404.9400, 0.8899, 2.6721, 1678.6500, 1.1550, 5.1381, 1251.0800, 0.9148, 1.3133, 216.8530, 0.6185, -9.9968, 110.1460, 0.9993,
    0.7900, -33.5253, 2620.1400, 0.9659, 2.0707, 1462.6000, 0.8603, 4.2225, 1428.4200, 0.9978, 1.2212, 200.4730, 0.8136, -9.9883, 117.5590, 0.9996,
    0.7532, -28.1315, 2011.4400, 0.9546, 5.1158, 1594.5200, 0.7447, 2.9501, 1344.0500, 1.4568, 1.0484, 200.2110, 0.7382, -9.9990, 107.9310, 0.9994,
    0.7790, -29.9643, 2453.1900, 0.9328, 11.2570, 1256.0000, 0.8292, -5.3991, 1104.2500, 0.8022, 2.2998, 170.0320, 1.4888, -9.9954, 150.0000, 0.9998,
    0.7632, -36.3156, 3129.1600, 0.8720, 4.1467, 1471.1900, 1.2248, 3.0732, 1177.4000, 0.9524, 0.6274, 433.4960, 0.4221, -9.9987, 128.3710, 1.0000,
    0.7063, -28.6766, 2014.4200, 0.8957, 5.8080, 1595.8700, 1.1544, 3.7961, 1228.0700, 1.0743, 0.5988, 545.0680, 0.3185, -9.9832, 126.0640, 1.0000,
    0.6924, -28.5948, 1908.2100, 0.8647, 8.0090, 1480.7200, 0.9514, 2.7432, 1360.4400, 0.7013, -3.4642, 100.0300, 1.1128, -9.9804, 107.4050, 0.9999,
    0.6759, -24.9898, 1840.3400, 0.9511, 4.0384, 1611.9600, 1.4027, 5.7140, 1307.0500, 1.0117, 0.0438, 794.0670, 1.5000, -9.9998, 128.3360, 1.0000,
    0.6641, -32.8820, 2777.7300, 0.9682, 6.2691, 1502.4300, 1.0457, 1.3135, 1333.4400, 1.1101, -6.0633, 100.0620, 0.9080, -9.9953, 100.2750, 1.0000,
    0.6298, -26.9826, 2243.4900, 0.8981, 0.8751, 1633.1400, 0.9430, 7.5137, 1409.9300, 1.0789, -3.2567, 100.0800, 1.0894, -9.9910, 117.5860, 0.9991,
    0.6442, -34.7808, 2871.2000, 0.8618, 9.2776, 1446.8200, 0.8622, 4.7362, 202.9600, 0.5842, -8.0913, 100.0000, 0.3293, -10.0000, 102.1440, 1.0000,
    0.5517, -30.5463, 3199.6400, 0.9314, 0.0341, 1644.2100, 1.3284, 5.7445, 1498.4200, 1.1162, -4.2586, 100.0250, 1.3327, -9.9958, 110.2360, 0.9996,
    0.5635, -31.9514, 2973.8100, 0.9320, 0.6344, 1613.7700, 1.4575, 7.1820, 1485.4800, 1.0072, -8.4921, 100.0240, 1.0504, -9.9759, 99.7296, 0.9999,
    0.5487, -31.2265, 3077.2300, 0.8568, 7.1320, 1470.0000, 1.1719, 0.7724, 1105.4600, 0.3000, -6.7626, 100.0700, 0.9077, -9.9974, 100.2520, 0.9757,
    0.5153, -26.3456, 2748.9200, 0.9103, 4.1669, 1500.9400, 1.3934, 2.9896, 1346.5800, 1.0312, -7.5795, 100.0600, 1.1865, -9.9963, 102.1380, 0.9686,
    0.5142, -24.1366, 2195.2400, 0.9133, 9.3201, 1470.6800, 1.0506, -0.6014, 1172.1900, 0.8128, -9.7195, 100.0010, 1.1673, -9.9976, 100.2910, 1.0000,
    0.4857, -24.6956, 2365.5700, 0.8934, 7.9240, 1531.4700, 1.1748, 0.8614, 1360.6000, 0.4605, -11.3112, 100.0530, 1.2915, -9.9947, 97.3218, 0.9999,
    0.4548, -24.8095, 2685.8900, 0.9883, 6.4484, 1547.6000, 1.0975, -0.3083, 809.7720, 1.4804, -9.1004, 100.0190, 1.1976, -9.9992, 97.3089, 0.9987,
    0.4344, -25.2589, 3088.4600, 0.9911, 0.7310, 1108.7000, 0.3000, 5.3403, 1499.9800, 1.4847, -10.4013, 100.0150, 1.1967, -9.9952, 102.1030, 0.9998,
    0.3899, -25.1406, 2528.7300, 0.9143, 9.1894, 1696.8300, 1.0536, -0.0049, 1452.7500, 1.3812, -8.2436, 100.0100, 1.1040, -9.9983, 100.3030, 1.0000,
    0.3921, -28.6952, 3521.0900, 0.8003, 8.0979, 1621.1100, 1.0731, 1.1238, 227.8420, 0.3176, -10.7659, 100.0330, 0.9205, -9.9942, 97.2933, 0.9992,
    0.3575, -20.0306, 2230.5900, 0.9997, 8.0356, 1657.5300, 1.1997, 8.5185, 200.0000, 0.4847, -13.0277, 100.0010, 0.3000, -9.9974, 97.2090, 1.0000,
    0.3726, -25.0829, 2955.1700, 1.0000, -2.9166, 1233.2900, 1.2691, 9.4691, 1500.0000, 1.0231, -13.8287, 100.0200, 1.3810, -9.9982, 100.3540, 1.0000,
    0.3632, -26.8198, 3471.7600, 0.9542, 6.8773, 1613.2000, 1.0923, 0.4393, 359.9170, 0.3307, -15.6428, 100.0750, 1.4456, -9.9894, 97.3437, 1.0000,
    0.3488, -24.1371, 2814.5500, 0.8266, 8.6545, 1629.1100, 1.0215, 1.4059, 200.2160, 0.5983, -14.0292, 100.0200, 1.0981, -9.9914, 96.9471, 0.9997,
    0.3251, -21.2456, 2663.3700, 0.8107, 7.0779, 1637.0200, 1.3429, 1.6174, 1306.6700, 0.4217, -11.9578, 100.0000, 1.2113, -9.9969, 100.4380, 0.9830,
    0.2932, -21.9157, 2859.3700, 0.8250, 7.3197, 1658.3800, 1.2021, 1.3378, 1316.3900, 0.3036, -15.6146, 100.0220, 1.4427, -9.9928, 102.1760, 1.0000,
    0.2911, -18.6482, 2475.5100, 0.8463, 6.0881, 1671.8600, 1.4663, 2.2313, 1463.6200, 0.4764, -12.9117, 100.0100, 1.3873, -9.9952, 109.6090, 0.9967,
    0.2822, -21.7159, 3119.2800, 0.9953, 6.5230, 1694.3800, 1.2067, 0.7314, 550.5090, 1.0979, -14.7169, 100.0140, 1.4478, -9.9986, 100.2480, 0.9997,
    0.2526, -20.3681, 3032.3600, 0.8747, -3.2607, 1040.9200, 1.0925, 8.5565, 1500.0000, 0.6394, -16.3116, 100.0740, 1.5000, -9.9876, 96.7960, 1.0000,
    0.2317, -25.4247, 3630.5900, 0.9316, 5.8437, 1506.5500, 0.6079, 19.9992, 200.0000, 0.3003, -36.7189, 100.0040, 0.3005, -9.9669, 101.9790, 0.9990,
};

const float ff_filter[] = {
    0.53, 13.00, 17093, 0.50, -22.35, 6186, 1.40, -8.00, 7369, 5.00, 10.07, 51, 1.00, -0.20, 150, 0.73, -4.00, 196, 0.39, -1.09, 417, 0.56, 0.29, 775, 0.89, -0.39, 1965, 0.79, 15.49, 3810, 0.79,
    0.56, 13.00, 17093, 0.50, -22.35, 6186, 1.40, -8.00, 7369, 5.00, 10.88, 53, 1.00, 0.43, 108, 0.62, -4.47, 155, 0.42, -1.77, 492, 0.66, 0.57, 850, 0.90, -0.55, 2169, 0.80, 15.53, 3810, 0.80,
    0.63, 13.00, 17093, 0.50, -22.35, 6186, 1.40, -8.00, 7369, 5.00, 6.89, 70, 1.00, -0.64, 146, 0.74, -4.10, 198, 0.41, -0.99, 443, 0.68, 0.21, 795, 0.79, -0.70, 2192, 0.74, 15.38, 3810, 0.79,
    0.71, 13.00, 17093, 0.50, -22.35, 6186, 1.40, -8.00, 7369, 5.00, 9.97, 54, 1.00, -0.84, 135, 0.71, -4.39, 243, 0.38, -0.66, 498, 0.58, 0.87, 727, 0.71, -1.00, 2116, 0.70, 15.54, 3810, 0.79,
    0.77, 13.00, 17093, 0.50, -22.35, 6186, 1.40, -8.00, 7369, 5.00, 8.98, 66, 1.00, -1.06, 123, 0.70, -4.18, 211, 0.36, -0.49, 455, 0.80, 0.14, 798, 0.86, -1.51, 2059, 0.74, 15.43, 3810, 0.73,
    0.82, 13.00, 17093, 0.50, -22.35, 6186, 1.40, -8.00, 7369, 5.00, 9.42, 58, 1.00, -1.32, 121, 0.63, -1.38, 232, 0.41, -3.60, 448, 0.34, 1.76, 805, 0.81, -0.68, 2104, 0.70, 16.35, 3810, 0.80,
    0.91, 13.00, 17093, 0.50, -22.35, 6186, 1.40, -8.00, 7369, 5.00, 12.03, 55, 1.00, -1.44, 80, 0.73, -3.96, 207, 0.48, -1.45, 470, 0.54, 0.22, 705, 0.63, -2.65, 2146, 0.80, 16.63, 3810, 0.77,
    1.03, 13.00, 17093, 0.50, -22.35, 6186, 1.40, -8.00, 7369, 5.00, 9.90, 60, 1.00, -1.72, 150, 0.67, -1.82, 223, 0.42, -2.46, 426, 0.53, 0.54, 805, 0.78, -2.00, 2171, 0.67, 16.71, 3810, 0.80,
    1.07, 13.00, 17093, 0.50, -22.35, 6186, 1.40, -8.00, 7369, 5.00, 12.70, 60, 1.00, -1.21, 118, 0.75, -1.54, 166, 0.45, -3.24, 462, 0.41, 1.03, 874, 0.88, -1.98, 2135, 0.73, 16.16, 3810, 0.80,
    1.19, 13.00, 17093, 0.50, -22.35, 6186, 1.40, -8.00, 7369, 5.00, 9.47, 67, 1.00, -0.92, 128, 0.60, -3.03, 193, 0.41, -1.93, 400, 0.47, 0.63, 904, 0.80, -1.64, 2017, 0.67, 14.77, 3810, 0.80,
    1.27, 13.00, 17093, 0.50, -22.35, 6186, 1.40, -8.00, 7369, 5.00, 13.81, 53, 1.00, 0.92, 132, 0.76, -5.14, 242, 0.39, -0.09, 492, 0.71, 0.35, 743, 0.65, -1.28, 1841, 0.60, 14.77, 3810, 0.79,
    1.34, 13.00, 17093, 0.50, -22.35, 6186, 1.40, -8.00, 7369, 5.00, 14.61, 51, 1.00, -0.54, 144, 0.68, -2.46, 224, 0.55, -2.37, 524, 0.52, 1.02, 938, 0.71, -3.23, 1950, 0.67, 15.22, 3810, 0.73,
    1.54, 13.00, 17093, 0.50, -22.35, 6186, 1.40, -8.00, 7369, 5.00, 13.51, 59, 1.00, -1.96, 150, 0.69, -1.20, 173, 0.40, -4.14, 549, 0.57, 2.37, 871, 0.60, -2.64, 2003, 0.63, 14.90, 3810, 0.79,
    1.54, 13.00, 17093, 0.50, -22.35, 6186, 1.40, -8.00, 7369, 5.00, 19.36, 50, 1.00, 0.02, 150, 0.60, 0.19, 215, 0.41, -3.83, 418, 0.37, 1.69, 911, 0.88, -3.78, 2200, 0.50, 17.45, 3810, 0.80,
    1.83, 13.00, 17093, 0.50, -22.35, 6186, 1.40, -8.00, 7369, 5.00, 12.98, 64, 1.00, -1.56, 150, 0.77, -0.64, 197, 0.60, -3.27, 413, 0.76, 0.72, 796, 0.89, -2.72, 1859, 0.50, 14.59, 3810, 0.80,
    1.92, 13.00, 17093, 0.50, -22.35, 6186, 1.40, -8.00, 7369, 5.00, 13.57, 69, 1.00, 1.70, 149, 0.80, -4.14, 200, 0.44, -1.74, 578, 0.52, 0.88, 920, 0.60, -2.68, 1873, 0.66, 13.69, 3810, 0.73,
    2.00, 13.00, 17093, 0.50, -22.35, 6186, 1.40, -8.00, 7369, 5.00, 18.47, 66, 1.00, 1.41, 150, 0.80, -2.96, 199, 0.40, -1.86, 467, 0.73, 0.18, 888, 0.85, -3.36, 2157, 0.69, 16.44, 3810, 0.80,
    2.00, 13.00, 17093, 0.50, -22.35, 6186, 1.40, -8.00, 7369, 5.00, 16.53, 63, 1.00, 1.03, 103, 0.75, -1.63, 181, 0.36, -1.90, 491, 0.72, 0.07, 715, 0.84, -2.45, 1917, 0.74, 13.90, 3810, 0.69,
    2.00, 13.00, 17093, 0.50, -22.35, 6186, 1.40, -8.00, 7369, 5.00, 20.48, 52, 1.00, 3.16, 99, 0.78, -1.62, 238, 0.38, -1.12, 583, 0.53, 0.59, 872, 0.90, -1.88, 2200, 0.77, 14.35, 3810, 0.80,
    2.00, 13.00, 17093, 0.50, -22.35, 6186, 1.40, -8.00, 7369, 5.00, 21.00, 62, 1.00, 0.96, 113, 0.68, -0.04, 203, 0.40, -2.29, 586, 0.53, 1.36, 868, 0.73, -3.69, 2185, 0.70, 16.81, 3810, 0.69,
    2.00, 13.00, 17093, 0.50, -22.35, 6186, 1.40, -8.00, 7369, 5.00, 24.27, 61, 1.00, -1.51, 128, 0.60, 4.34, 237, 0.41, -3.91, 401, 0.59, 1.32, 925, 0.69, -4.08, 2125, 0.66, 19.17, 3810, 0.74,
    2.00, 13.00, 17093, 0.50, -22.35, 6186, 1.40, -8.00, 7369, 5.00, 21.88, 70, 1.00, -1.84, 142, 0.61, 4.60, 157, 0.37, -2.82, 435, 0.64, 1.25, 850, 0.89, -3.92, 2176, 0.66, 18.02, 3810, 0.68,
    2.00, 13.00, 17093, 0.50, -22.35, 6186, 1.40, -8.00, 7369, 5.00, 26.76, 59, 1.00, 5.18, 95, 0.76, -0.06, 194, 0.30, 0.46, 454, 0.80, -1.05, 962, 0.60, -4.20, 2148, 0.75, 18.08, 3810, 0.52,
    2.00, 13.00, 17093, 0.50, -22.35, 6186, 1.40, -8.00, 7369, 5.00, 26.75, 66, 1.00, 1.22, 131, 0.67, 2.29, 157, 0.42, -2.44, 585, 0.67, 3.00, 746, 0.61, -3.04, 1970, 0.67, 19.86, 3810, 0.76,
    2.00, 13.00, 17093, 0.50, -22.35, 6186, 1.40, -8.00, 7369, 5.00, 27.38, 65, 1.00, 1.09, 121, 0.80, 3.71, 150, 0.46, 0.27, 400, 0.67, -0.43, 705, 0.62, -4.37, 2200, 0.75, 19.51, 3810, 0.50,
    2.00, 13.00, 17093, 0.50, -22.35, 6186, 1.40, -8.00, 7369, 5.00, 24.49, 54, 1.00, 10.00, 80, 0.60, -0.16, 191, 0.60, 1.99, 442, 0.50, -0.75, 952, 0.90, -3.72, 2189, 0.71, 19.31, 3810, 0.50,
    2.00, 13.00, 17093, 0.50, -22.35, 6186, 1.40, -8.00, 7369, 5.00, 29.86, 66, 1.00, 4.16, 80, 0.78, 3.36, 150, 0.46, 1.03, 451, 0.45, 1.34, 986, 0.77, -3.67, 1874, 0.50, 19.65, 3810, 0.59,
    2.00, 13.00, 17093, 0.50, -22.35, 6186, 1.40, -8.00, 7369, 5.00, 30.00, 64, 1.00, 6.24, 89, 0.71, 1.77, 157, 0.39, 1.85, 419, 0.30, 1.27, 1000, 0.90, -2.75, 1800, 0.70, 20.00, 3810, 0.68,
    2.00, 13.00, 17093, 0.50, -22.35, 6186, 1.40, -8.00, 7369, 5.00, 29.13, 66, 1.00, 6.79, 80, 0.62, 2.17, 173, 0.40, 2.74, 473, 0.46, -0.24, 715, 0.74, -2.67, 1843, 0.79, 19.91, 3810, 0.56,
    2.00, 13.00, 17093, 0.50, -22.35, 6186, 1.40, -8.00, 7369, 5.00, 29.97, 60, 1.00, 10.00, 81, 0.67, 3.16, 212, 0.53, 0.73, 520, 0.70, 1.23, 700, 0.62, -3.08, 1995, 0.79, 19.97, 3810, 0.50,

};

#endif
