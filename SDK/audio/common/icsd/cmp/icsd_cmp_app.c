#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".icsd_cmp.data.bss")
#pragma data_seg(".icsd_cmp.data")
#pragma const_seg(".icsd_cmp.text.const")
#pragma code_seg(".icsd_cmp.text")
#endif

#include "app_config.h"
#include "audio_config_def.h"
#if (TCFG_AUDIO_ANC_EAR_ADAPTIVE_EN && \
	 TCFG_AUDIO_ANC_ENABLE && \
	 TCFG_AUDIO_ANC_EAR_ADAPTIVE_VERSION == ANC_EXT_V2)

#include "audio_anc.h"

#if ANC_EAR_ADAPTIVE_CMP_EN

#include "icsd_cmp.h"
#include "icsd_cmp_app.h"
#include "a2dp_player.h"
#include "esco_player.h"
#include "icsd_cmp_config.h"
#include "anc_ext_tool.h"

#if TCFG_AUDIO_ANC_REAL_TIME_ADAPTIVE_ENABLE
#include "rt_anc_app.h"
#endif

#if 0
#define cmp_log printf
#else
#define cmp_log(...)
#endif

#define ANC_ADAPTIVE_CMP_RUN_TIME_DEBUG			0		//CMP 算法运行时间测试

#define ANC_ADAPTIVE_CMP_OUTPUT_SIZE		((1 + 3 * ANC_ADAPTIVE_CMP_ORDER) * sizeof(float))

struct icsd_cmp_param {
    u8 type[ANC_ADAPTIVE_CMP_ORDER];		//滤波器类型
    _cmp_output *output;					//算法输出
    float gain;								//增益
    anc_fr_t fr[ANC_ADAPTIVE_CMP_ORDER];	//输出整理
    double *coeff;							//IIR_COEFF
};

struct icsd_cmp_hdl {
    u8 state;
    u8 busy;
    u8 data_from;
    void *lib_alloc_ptr;

    struct icsd_cmp_param l_param;
#if ANC_CONFIG_RFB_EN
    struct icsd_cmp_param r_param;
#endif
};

int (*cmp_printf)(const char *format, ...) = _cmp_printf;

struct icsd_cmp_hdl *cmp_hdl = NULL;

static int audio_anc_ear_adaptive_cmp_close(void);
extern void audio_anc_biquad2ab_double(anc_fr_t *iir, double *out_coeff, u8 order, int alogm);

void icsd_cmp_force_exit(void)
{
    if (cmp_hdl) {
        DeAlorithm_disable();
    }
}

static int anc_ear_adaptive_cmp_run_ch(struct icsd_cmp_param *p, float *sz, int sz_len, u8 ch)
{
    float *sz_tmp;
    _cmp_output *output;
    s8 sz_sign = anc_ext_ear_adaptive_sz_calr_sign_get();
    /* cmp_log("sz_sign %d\n", sz_sign); */
    if (sz && sz_len) {
        struct anc_ext_ear_adaptive_param *ext_cfg = anc_ext_ear_adaptive_cfg_get();
        __adpt_cmp_cfg *cmp_cfg = zalloc(sizeof(__adpt_cmp_cfg));
        icsd_cmp_cfg_set(cmp_cfg, ext_cfg, ch, ANC_ADAPTIVE_CMP_ORDER);
        memcpy(p->type, cmp_cfg->type, ANC_ADAPTIVE_CMP_ORDER);

#if ANC_ADAPTIVE_CMP_RUN_TIME_DEBUG
        u32 last = jiffies_usec();
#endif
        //CMP 算法要求输入SZ的符号必须为正，因此需要单独备份SZ的数据，以便修改
        sz_tmp = (float *)zalloc(sz_len * sizeof(float));
        memcpy((u8 *)sz_tmp, (u8 *)sz, sz_len * sizeof(float));
        //CMP来自实时自适应时，SZ符合一定为正，不需要翻转符号
        if ((sz_sign == -1) && (cmp_hdl->data_from != CMP_FROM_RTANC)) {
            for (int j = 0; j < sz_len; j++) {
                sz_tmp[j] = 0 -  sz_tmp[j];
            }
        }
        output = icsd_cmp_run(sz_tmp, cmp_cfg);

        cmp_log("ANC_ADAPTIVE_CMP_OUTPUT_SIZE %lu\n", ANC_ADAPTIVE_CMP_OUTPUT_SIZE);

        p->output->state = output->state;
        memcpy((u8 *)p->output->fgq, (u8 *)output->fgq, ANC_ADAPTIVE_CMP_OUTPUT_SIZE);

        //CMP增益符号与SZ符号 负相关
        p->output->fgq[0] = (abs(p->output->fgq[0])) * (sz_sign) * (-1);

#if ANC_ADAPTIVE_CMP_RUN_TIME_DEBUG
        cmp_log("ANC ADAPTIVE CMP RUN time: %d us\n", (int)(jiffies_usec2offset(last, jiffies_usec())));
#endif

#if 0	//CMP结果输出打印
        float *fgq = p->output->fgq;
        g_printf("Global %d/10000\n", (int)(fgq[0] * 10000));
        for (int i = 0; i < ANC_ADAPTIVE_CMP_ORDER; i++) {
            g_printf("SEG[%d]:Type %d, FGQ/10000:%d %d %d\n", i, p->type[i], \
                     (int)(fgq[i * 3 + 1] * 10000), (int)(fgq[i * 3 + 2] * 10000), \
                     (int)(fgq[i * 3 + 3] * 10000));
        }
#endif

        free(sz_tmp);
        free(cmp_cfg);
    }
    return p->output->state;
}

int audio_anc_ear_adaptive_cmp_run(void *output)
{
    int ret = 0;
    u8 l_state = 0, r_state = 0;
    struct icsd_cmp_libfmt libfmt;
    struct icsd_cmp_infmt  fmt;

    if (!cmp_hdl) {
        return -1;
    }
#if (ANC_ADAPTIVE_CMP_ONLY_IN_MUSIC_UPDATE && TCFG_AUDIO_ANC_REAL_TIME_ADAPTIVE_ENABLE)
    //非通话/播歌状态 不更新
    if (!(a2dp_player_runing() || esco_player_runing())) {
        return -1;
    }
#endif
    if ((cmp_hdl->state == ANC_EAR_ADAPTIVE_CMP_STATE_CLOSE) || cmp_hdl->busy) {
        return -1;
    }
    cmp_hdl->busy = 1;
    wdt_clear();
    cmp_log("ICSD_CMP_STATE:RUN");

    //申请算法资源
    icsd_cmp_get_libfmt(&libfmt);
    //cmp_log("CMP RAM SIZE:%d %d %d\n", libfmt.lib_alloc_size, cmp_IIR_NUM_FIX, cmp_IIR_NUM_FLEX);
    cmp_hdl->lib_alloc_ptr = fmt.alloc_ptr = zalloc(libfmt.lib_alloc_size);
    cmp_log("%p--%x \n", cmp_hdl->lib_alloc_ptr, ((u32)cmp_hdl->lib_alloc_ptr) + libfmt.lib_alloc_size);
    icsd_cmp_set_infmt(&fmt);

    struct anc_ext_ear_adaptive_param *ext_cfg = anc_ext_ear_adaptive_cfg_get();

    if (cmp_hdl->data_from == CMP_FROM_RTANC) {
        struct audio_afq_output *p = (struct audio_afq_output *)output;
        l_state = anc_ear_adaptive_cmp_run_ch(&cmp_hdl->l_param, p->sz_l.out, p->sz_l.len, 0);

#if ANC_CONFIG_RFB_EN
        r_state = anc_ear_adaptive_cmp_run_ch(&cmp_hdl->r_param, p->sz_r.out, p->sz_r.len,, 1);
#endif
    } else {
        __afq_output *p2 = (__afq_output *)output;
        if (p2->sz_l) {
            l_state = anc_ear_adaptive_cmp_run_ch(&cmp_hdl->l_param, p2->sz_l->out, p2->sz_l->len, 0);
        }

#if ANC_CONFIG_RFB_EN
        if (p2->sz_r) {
            r_state = anc_ear_adaptive_cmp_run_ch(&cmp_hdl->r_param, p2->sz_r->out, p2->sz_r->len, 1);
        }
#endif
    }
    if (l_state || r_state) {
        printf("CMP OUTPUT ERR!! STATE : L %d, R %d\n", l_state, r_state);
        ret = -1;
    }
    cmp_hdl->busy = 0;

    //释放算法资源
    free(cmp_hdl->lib_alloc_ptr);

    cmp_log("ICSD_CMP_STATE:RUN FINISH");
    return ret;
}

static void audio_anc_ear_adaptive_cmp_afq_output_hdl(struct audio_afq_output *p)
{
    if (cmp_hdl) {
#if TCFG_AUDIO_ANC_REAL_TIME_ADAPTIVE_ENABLE
        if (!audio_anc_real_time_adaptive_state_get()) {
            printf("ICSD_CMP: RTANC_CLOSE by now, exit!\n");
            return;
        }
#endif
        if (!audio_anc_ear_adaptive_cmp_run(p)) {
            //更新参数，更新工具信息
#if TCFG_AUDIO_ANC_REAL_TIME_ADAPTIVE_ENABLE
#if AUDIO_RT_ANC_EXPORT_TOOL_DATA_DEBUG
            audio_rtanc_cmp_data_packet();
#endif
            audio_rtanc_cmp_update();
        } else {
            //异常处理
            cmp_hdl->l_param.output->state = -1;
#if ANC_CONFIG_RFB_EN
            cmp_hdl->r_param.output->state = -1;
#endif
            audio_anc_real_time_adaptive_resume();
#endif
        }
    }
}

int audio_anc_ear_adaptive_cmp_open(u8 data_from)
{
    if (cmp_hdl) {
        return -1;
    }
    cmp_hdl = zalloc(sizeof(struct icsd_cmp_hdl));
    enum audio_adaptive_fre_sel fre_sel = AUDIO_ADAPTIVE_FRE_SEL_ANC;

    cmp_log("ICSD_CMP_STATE:OPEN");
    cmp_hdl->state = ANC_EAR_ADAPTIVE_CMP_STATE_OPEN;
    cmp_hdl->data_from = data_from;
    cmp_hdl->l_param.output = malloc(sizeof(_cmp_output));
    cmp_hdl->l_param.output->fgq = malloc(ANC_ADAPTIVE_CMP_OUTPUT_SIZE);
    cmp_log("%p %p\n", cmp_hdl->l_param.output, cmp_hdl->l_param.output->fgq);

#if ANC_CONFIG_RFB_EN
    cmp_hdl->r_param.output = malloc(sizeof(_cmp_output));
    cmp_hdl->r_param.output->fgq = malloc(ANC_ADAPTIVE_CMP_OUTPUT_SIZE);
#endif

    if (data_from == CMP_FROM_RTANC) {
        audio_afq_common_add_output_handler("ANC_CMP", fre_sel, audio_anc_ear_adaptive_cmp_afq_output_hdl);
    }
    return 0;
}

int audio_anc_ear_adaptive_cmp_close(void)
{
    if (cmp_hdl) {
        cmp_log("ICSD_CMP_STATE:CLOSE");
        cmp_hdl->state = ANC_EAR_ADAPTIVE_CMP_STATE_CLOSE;
        icsd_cmp_force_exit();
        u8 wait_cnt = 10;
        while (cmp_hdl->busy && wait_cnt) {
            os_time_dly(1);
            wait_cnt--;
        }

        if (cmp_hdl->data_from == CMP_FROM_RTANC) {
            audio_afq_common_del_output_handler("ANC_CMP");
        }
        if (cmp_hdl->l_param.coeff) {
            free(cmp_hdl->l_param.coeff);
        }
        if (cmp_hdl->l_param.output) {
            free(cmp_hdl->l_param.output->fgq);
            free(cmp_hdl->l_param.output);
        }
#if ANC_CONFIG_RFB_EN
        if (cmp_hdl->r_param.coeff) {
            free(cmp_hdl->r_param.coeff);
        }
        if (cmp_hdl->r_param.output) {
            free(cmp_hdl->r_param.output->fgq);
            free(cmp_hdl->r_param.output);
        }
#endif
        free(cmp_hdl);
        cmp_hdl = NULL;
        cmp_log("ICSD_CMP_STATE:CLOSE FINISH");
    }
    return 0;
}

struct icsd_cmp_hdl *audio_anc_ear_adaptive_cmp_hdl_get(void)
{
    return cmp_hdl;
}

//获取算法输出
float *audio_anc_ear_adaptive_cmp_output_get(enum ANC_EAR_ADAPTIVE_CMP_CH ch)
{
    if (cmp_hdl) {
        switch (ch) {
        case ANC_EAR_ADAPTIVE_CMP_CH_L:
            if (cmp_hdl->l_param.output) {
                return cmp_hdl->l_param.output->fgq;
            }
            break;
        case ANC_EAR_ADAPTIVE_CMP_CH_R:
#if ANC_CONFIG_RFB_EN
            if (cmp_hdl->r_param.output) {
                return cmp_hdl->r_param.output->fgq;
            }
#endif
            break;
        }
    }
    return NULL;
}

u8 *audio_anc_ear_adaptive_cmp_type_get(enum ANC_EAR_ADAPTIVE_CMP_CH ch)
{
    if (cmp_hdl) {
        switch (ch) {
        case ANC_EAR_ADAPTIVE_CMP_CH_L:
            return cmp_hdl->l_param.type;
        case ANC_EAR_ADAPTIVE_CMP_CH_R:
#if ANC_CONFIG_RFB_EN
            return cmp_hdl->r_param.type;
#endif
            break;
        }
    }
    return NULL;
}

static void audio_rtanc_adaptive_cmp_data_format(struct icsd_cmp_param *p, double *coeff)
{
    u8 *cmp_dat = (u8 *)&p->gain;
    audio_anc_fr_format(cmp_dat, p->output->fgq, ANC_ADAPTIVE_CMP_ORDER, p->type);
    /* if (p->coeff == NULL) { */
    /* p->coeff = malloc(ANC_ADAPTIVE_CMP_ORDER * sizeof(double) * 5); */
    /* } */
    int alogm = audio_anc_gains_alogm_get(ANC_CMP_TYPE);
    audio_anc_biquad2ab_double(p->fr, coeff, ANC_ADAPTIVE_CMP_ORDER, alogm);
}

//获取Total gain & IIR COEFF, 外部传参准备好coeff 空间
int audio_rtanc_adaptive_cmp_output_get(struct anc_cmp_param_output *output)
{
    if (cmp_hdl) {
        audio_rtanc_adaptive_cmp_data_format(&cmp_hdl->l_param, output->l_coeff);
        output->l_gain = (cmp_hdl->l_param.gain < 0) ? (0 - cmp_hdl->l_param.gain) : cmp_hdl->l_param.gain;
        /* output->l_coeff = cmp_hdl->l_param.coeff; */
#if ANC_CONFIG_RFB_EN
        audio_rtanc_adaptive_cmp_data_format(&cmp_hdl->r_param, output->r_coeff);
        output->r_gain = (cmp_hdl->r_param.gain < 0) ? (0 - cmp_hdl->r_param.gain) : cmp_hdl->r_param.gain;
        /* output->r_coeff = cmp_hdl->r_param.coeff; */
#endif
    }
    return 0;
}

u8 audio_anc_ear_adaptive_cmp_result_get(void)
{
    u8 result = 0;
    if (cmp_hdl) {
        if (!cmp_hdl->l_param.output->state) {
            result |= ANC_ADAPTIVE_RESULT_LCMP;
        }
#if ANC_CONFIG_RFB_EN
        if (!cmp_hdl->r_param.output->state) {
            result |= ANC_ADAPTIVE_RESULT_RCMP;
        }
#endif
    }
    return result;
}

#endif
#endif


