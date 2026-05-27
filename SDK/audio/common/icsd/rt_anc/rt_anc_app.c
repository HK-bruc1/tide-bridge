#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".rt_anc.data.bss")
#pragma data_seg(".rt_anc.data")
#pragma const_seg(".rt_anc.text.const")
#pragma code_seg(".rt_anc.text")
#endif

#include "app_config.h"
#include "audio_config_def.h"

#if TCFG_AUDIO_ANC_ENABLE && TCFG_AUDIO_ANC_REAL_TIME_ADAPTIVE_ENABLE
#include "audio_anc.h"
#include "rt_anc.h"
#include "rt_anc_app.h"
#include "clock_manager/clock_manager.h"
#include "icsd_anc_user.h"

#include "icsd_adt.h"
#include "icsd_adt_app.h"
#include "audio_anc_common_plug.h"
#include "audio_anc_debug_tool.h"

#if TCFG_USER_TWS_ENABLE
#include "bt_tws.h"
#endif/*TCFG_USER_TWS_ENABLE*/

#if TCFG_AUDIO_FREQUENCY_GET_ENABLE
#include "icsd_afq_app.h"
#endif

#if ANC_EAR_ADAPTIVE_CMP_EN
#include "icsd_cmp_app.h"
#endif

#if TCFG_AUDIO_ADAPTIVE_EQ_ENABLE
#include "icsd_aeq_app.h"
#endif

#if TCFG_SPEAKER_EQ_NODE_ENABLE
#include "effects/audio_spk_eq.h"
#endif

#if 1
#define rtanc_log	printf
#else
#define rtanc_log(...)
#endif/*log_en*/

#define AUDIO_RTANC_COEFF_SIZE		40 // 5 * sizeof(double)

//管理RTANC 滤波器输出:启动后RAM空间常驻
struct audio_rt_anc_output {
    double *lff_coeff;
    double *lcmp_coeff;

    double *rff_coeff;
    double *rcmp_coeff;
};

struct audio_rt_anc_hdl {
    u8 seq;
    u8 state;
    u8 run_busy;
    u8 reset_busy;
    u8 app_func_en;
    u8 ignore_switch_lock;
    u8 lff_iir_type[10];
    u8 rff_iir_type[10];
    u8 fade_gain_suspend;
    int rtanc_mode;
    int suspend_cnt;
    audio_anc_t *param;
    float *debug_param;
    float *pz_cmp;
    float *sz_cmp;
    struct icsd_rtanc_tool_data *rtanc_tool;	//工具临时数据
    struct rt_anc_fade_gain_ctr fade_ctr;
    struct audio_rt_anc_output out;				//RTANC/CMP IIR 滤波器输出
#if TCFG_SPEAKER_EQ_NODE_ENABLE
    struct eq_default_seg_tab spk_eq_tab_l;
    struct eq_default_seg_tab spk_eq_tab_r;
#endif
#if ANC_DUT_MIC_CMP_GAIN_ENABLE
    struct anc_mic_gain_cmp_cfg *mic_cmp;
#endif
};

static void audio_anc_real_time_adaptive_data_packet(struct icsd_rtanc_tool_data *rtanc_tool);
static void audio_rtanc_sz_pz_cmp_process(void);

static struct audio_rt_anc_hdl	*hdl = NULL;

anc_packet_data_t *rtanc_tool_data = NULL;

void audio_anc_real_time_adaptive_init(audio_anc_t *param)
{
    hdl = zalloc(sizeof(struct audio_rt_anc_hdl));
    hdl->param = param;
    hdl->fade_ctr.lff_gain = 16384;
    hdl->fade_ctr.lfb_gain = 16384;
    hdl->fade_ctr.rff_gain = 16384;
    hdl->fade_ctr.rfb_gain = 16384;

    hdl->rtanc_mode = ADT_REAL_TIME_ADAPTIVE_ANC_MODE;	//默认为正常模式
}

/* Real Time ANC 自适应启动限制 */
int audio_rtanc_permit(u8 sync_mode)
{
    if (anc_ext_ear_adaptive_param_check()) { //没有自适应参数
        return ANC_EXT_OPEN_FAIL_CFG_MISS;
    }
    if (hdl->param->mode != ANC_ON) {	//非ANC模式
        return ANC_EXT_OPEN_FAIL_FUNC_CONFLICT;
    }
    if (audio_anc_real_time_adaptive_state_get()) { //重入保护
        return ANC_EXT_OPEN_FAIL_REENTRY;
    }
    if (anc_mode_switch_lock_get() && (!hdl->ignore_switch_lock) && (!sync_mode)) { //其他切模式过程不支持
        return ANC_EXT_OPEN_FAIL_FUNC_CONFLICT;
    }
    hdl->ignore_switch_lock = 0;
#if TCFG_AUDIO_FREQUENCY_GET_ENABLE
    if (audio_icsd_afq_is_running()) {	//AFQ 运行过程中不支持
        return ANC_EXT_OPEN_FAIL_FUNC_CONFLICT;
    }
#endif
    if (hdl->param->lff_yorder > RT_ANC_MAX_ORDER || \
        hdl->param->lfb_yorder > RT_ANC_MAX_ORDER || \
        hdl->param->lcmp_yorder > RT_ANC_MAX_ORDER || \
        hdl->param->rff_yorder > RT_ANC_MAX_ORDER || \
        hdl->param->rfb_yorder > RT_ANC_MAX_ORDER || \
        hdl->param->rcmp_yorder > RT_ANC_MAX_ORDER) {
        return ANC_EXT_OPEN_FAIL_CFG_MISS;		//最大滤波器个数限制
    }
    if (!anc_ext_rtanc_adaptive_cfg_get()) {	//没有RTANC参数
        return ANC_EXT_OPEN_FAIL_CFG_MISS;
    }
    return 0;
}

//将滤波器等相关信息，整合成库需要的格式
void rt_anc_get_param(__rt_anc_param *rt_param_l, __rt_anc_param *rt_param_r)
{
    g_printf("%s, %d\n", __func__, __LINE__);

    int yorder_size = 5 * sizeof(double);
    audio_anc_t *param = hdl->param;
    //ANC 0, 通透 1(通透只在DAC出声的时候更新)
    /* int anc_mode = (param->mode == ANC_ON) ? 0 : 1; */
    int anc_mode = 0;	//gali test

    if (rt_param_l) {
        if (anc_mode) {
            rt_param_l->ff_yorder = param->ltrans_yorder;
            rt_param_l->fb_yorder = param->ltrans_fb_yorder;
            rt_param_l->cmp_yorder = param->ltrans_cmp_yorder;
            rt_param_l->ffgain = param->gains.l_transgain;
            rt_param_l->fbgain = param->ltrans_fbgain;
            rt_param_l->cmpgain = param->ltrans_cmpgain;

            memcpy(rt_param_l->ff_coeff, param->ltrans_coeff, rt_param_l->ff_yorder * yorder_size);
            memcpy(rt_param_l->fb_coeff, param->ltrans_fb_coeff, rt_param_l->fb_yorder * yorder_size);
            memcpy(rt_param_l->cmp_coeff, param->ltrans_cmp_coeff, rt_param_l->cmp_yorder * yorder_size);
        } else {
            rt_param_l->ff_yorder = param->lff_yorder;
            rt_param_l->fb_yorder = param->lfb_yorder;
            rt_param_l->cmp_yorder = param->lcmp_yorder;
            rt_param_l->ffgain = param->gains.l_ffgain;
            rt_param_l->fbgain = param->gains.l_fbgain;
            rt_param_l->cmpgain = param->gains.l_cmpgain;

            memcpy(rt_param_l->ff_coeff, param->lff_coeff, rt_param_l->ff_yorder * yorder_size);
            memcpy(rt_param_l->fb_coeff, param->lfb_coeff, rt_param_l->fb_yorder * yorder_size);
            memcpy(rt_param_l->cmp_coeff, param->lcmp_coeff, rt_param_l->cmp_yorder * yorder_size);
        }
        rt_param_l->ff_fade_gain = ((float)hdl->fade_ctr.lff_gain) / 16384.0f;
        rt_param_l->fb_fade_gain = ((float)hdl->fade_ctr.lfb_gain) / 16384.0f;
        rt_param_l->anc_mode = anc_mode;
        rt_param_l->param = param;

        rtanc_log("rtanc l order:ff %d, fb %d, cmp %d\n", rt_param_l->ff_yorder, \
                  rt_param_l->fb_yorder, rt_param_l->cmp_yorder);
        rtanc_log("rtanc l gain:ff %d/100, fb %d/100, cmp %d/100\n", (int)(rt_param_l->ffgain * 100.0f), \
                  (int)(rt_param_l->fbgain * 100.0f), (int)(rt_param_l->cmpgain * 100.0f));
        rtanc_log("rtanc l fade_gain:ff %d/100, fb %d/100\n", (int)(rt_param_l->ff_fade_gain * 100.0f), \
                  (int)(rt_param_l->fb_fade_gain * 100.0f));
        /* put_buf((u8*)rt_param_l->ff_coeff,  rt_param_l->ff_yorder * yorder_size); */
        /* put_buf((u8*)rt_param_l->fb_coeff,  rt_param_l->fb_yorder * yorder_size); */
        /* put_buf((u8*)rt_param_l->cmp_coeff, rt_param_l->cmp_yorder * yorder_size); */
    }

    if (rt_param_r) {
        if (anc_mode) {

            rt_param_r->ff_yorder = param->rtrans_yorder;
            rt_param_r->fb_yorder = param->rtrans_fb_yorder;
            rt_param_r->cmp_yorder = param->rtrans_cmp_yorder;
            rt_param_r->ffgain = param->gains.r_transgain;
            rt_param_r->fbgain = param->rtrans_fbgain;
            rt_param_r->cmpgain = param->rtrans_cmpgain;

            memcpy(rt_param_r->ff_coeff, param->rtrans_coeff, rt_param_r->ff_yorder * yorder_size);
            memcpy(rt_param_r->fb_coeff, param->rtrans_fb_coeff, rt_param_r->fb_yorder * yorder_size);
            memcpy(rt_param_r->cmp_coeff, param->rtrans_cmp_coeff, rt_param_r->cmp_yorder * yorder_size);

        } else {
            rt_param_r->ff_yorder = param->rff_yorder;
            rt_param_r->fb_yorder = param->rfb_yorder;
            rt_param_r->cmp_yorder = param->rcmp_yorder;
            rt_param_r->ffgain = param->gains.r_ffgain;
            rt_param_r->fbgain = param->gains.r_fbgain;
            rt_param_r->cmpgain = param->gains.r_cmpgain;

            memcpy(rt_param_r->ff_coeff, param->rff_coeff, rt_param_r->ff_yorder * yorder_size);
            memcpy(rt_param_r->fb_coeff, param->rfb_coeff, rt_param_r->fb_yorder * yorder_size);
            memcpy(rt_param_r->cmp_coeff, param->rcmp_coeff, rt_param_r->cmp_yorder * yorder_size);
        }
        rt_param_r->ff_fade_gain = ((float)hdl->fade_ctr.rff_gain) / 16384.0f;
        rt_param_r->fb_fade_gain = ((float)hdl->fade_ctr.rfb_gain) / 16384.0f;
        rt_param_r->anc_mode = anc_mode;
        rt_param_r->param = param;
    }

}

//设置RTANC初始参数
int audio_adt_rtanc_set_infmt(void *rtanc_tool)
{
    rtanc_log("=================audio_adt_rtanc_set_infmt==================\n");
    if (++hdl->seq == 0xff) {
        hdl->seq = 0;
    }
    struct rt_anc_infmt infmt;
    if (anc_ext_tool_online_get()) {
        hdl->rtanc_tool = rtanc_tool;
    } else {
        hdl->rtanc_tool = NULL;
    }
#if AUDIO_RT_ANC_SZ_PZ_CMP_EN
    audio_rtanc_sz_pz_cmp_process();
#endif
    infmt.id = hdl->seq;
    infmt.param = hdl->param;
    infmt.ext_cfg = (void *)anc_ext_ear_adaptive_cfg_get();
    infmt.anc_param_l = zalloc(sizeof(__rt_anc_param));
    //gali
    infmt.anc_param_r = zalloc(sizeof(__rt_anc_param));
    rt_anc_get_param(infmt.anc_param_l, infmt.anc_param_r);

    struct __anc_ext_rtanc_adaptive_cfg *rtanc_tool_cfg = anc_ext_rtanc_adaptive_cfg_get();
    rt_anc_set_init(&infmt, rtanc_tool_cfg);

    if (infmt.anc_param_l) {
        free(infmt.anc_param_l);
    }
    if (infmt.anc_param_r) {
        free(infmt.anc_param_r);
    }
    return 0;
}

static void audio_rt_anc_param_updata(void *rt_param_l, void *rt_param_r)
{
    rtanc_log("audio_rt_anc_param_updata\n");

    audio_anc_t *param = hdl->param;

    __rt_anc_param *anc_param = (__rt_anc_param *)rt_param_l;
    if (hdl->out.lff_coeff) {
        free(hdl->out.lff_coeff);
    }
    hdl->out.lff_coeff = malloc(param->lff_yorder * AUDIO_RTANC_COEFF_SIZE);
    memcpy(hdl->out.lff_coeff, anc_param->ff_coeff, param->lff_yorder * AUDIO_RTANC_COEFF_SIZE);

    param->gains.l_ffgain = anc_param->ffgain;
    param->lff_coeff = hdl->out.lff_coeff;
    param->gains.l_fbgain = anc_param->fbgain;

    //param->lfb_coeff = &anc_param->lfb_coeff[0];
#if ANC_CONFIG_RFB_EN
    if (rt_param_r) {
        __rt_anc_param *anc_param_r = (__rt_anc_param *)rt_param_r;
        if (hdl->out.rff_coeff) {
            free(hdl->out.rff_coeff);
        }
        hdl->out.rff_coeff = malloc(param->rff_yorder * AUDIO_RTANC_COEFF_SIZE);
        memcpy(hdl->out.rff_coeff, anc_param_r->ff_coeff, param->rff_yorder * AUDIO_RTANC_COEFF_SIZE);

        param->gains.r_ffgain = anc_param_r->ffgain;
        param->rff_coeff = hdl->out.rff_coeff;
        param->gains.r_fbgain = anc_param_r->fbgain;

    }
#endif

    anc_coeff_online_update(hdl->param, 0);			//更新ANC滤波器
}

void audio_rtanc_cmp_update(void)
{
    audio_anc_t *param = hdl->param;

#if ANC_EAR_ADAPTIVE_CMP_EN
    struct anc_cmp_param_output cmp_p;
    if (hdl->out.lcmp_coeff) {
        free(hdl->out.lcmp_coeff);
    }
    hdl->out.lcmp_coeff = malloc(ANC_ADAPTIVE_CMP_ORDER * AUDIO_RTANC_COEFF_SIZE);
    cmp_p.l_coeff = hdl->out.lcmp_coeff;

#if ANC_CONFIG_RFB_EN
    if (hdl->out.rcmp_coeff) {
        free(hdl->out.lcmp_coeff);
    }
    hdl->out.rcmp_coeff = malloc(ANC_ADAPTIVE_CMP_ORDER * AUDIO_RTANC_COEFF_SIZE);
    cmp_p.r_coeff = hdl->out.rcmp_coeff;
#endif

    audio_rtanc_adaptive_cmp_output_get(&cmp_p);
    param->gains.l_cmpgain = cmp_p.l_gain;
    param->lcmp_coeff = cmp_p.l_coeff;

#if ANC_CONFIG_RFB_EN
    param->gains.r_cmpgain = cmp_p.r_gain;
    param->rcmp_coeff = cmp_p.r_coeff;
#endif
    rtanc_log("updata gain = %d, coef = %d, ", (int)(param->gains.l_cmpgain * 100), (int)(param->lcmp_coeff[0] * 100));
#endif

    audio_anc_coeff_smooth_update();

    audio_anc_real_time_adaptive_resume();
}

static void audio_rt_anc_iir_type_get(void)
{
    struct anc_ext_ear_adaptive_param *ext_cfg = anc_ext_ear_adaptive_cfg_get();
    if (ext_cfg) {
        for (int i = 0; i < 10; i++) {
            if (ext_cfg->ff_iir_high) {
                hdl->lff_iir_type[i] = ext_cfg->ff_iir_high[i].type;
            }
            if (ext_cfg->rff_iir_high) {
                hdl->rff_iir_type[i] = ext_cfg->rff_iir_high[i].type;
            }
            /* rtanc_log("get lff_type %d, rff_type %d\n", hdl->lff_iir_type[i], hdl->rff_iir_type[i]); */
        }
    }
}

//实时自适应算法输出接口
void audio_adt_rtanc_output_handle(void *rt_param_l, void *rt_param_r)
{
    //RTANC de 算法强制退出时，将不再输出output
    if (!icsd_adt_is_running()) {
        return;
    }
    rtanc_log("RTANC_STATE:OUTPUT_RUN\n");
    hdl->run_busy = 1;
    //挂起RT_ANC
    audio_anc_real_time_adaptive_suspend();

    //整理SZ数据结构
    __afq_output output = {0};// = zalloc(sizeof(__afq_output));// = &temp;
    output.sz_l = zalloc(sizeof(__afq_data));
    output.sz_l->len = 120;
    output.sz_l->out = zalloc(sizeof(float) * 120);
    icsd_rtanc_alg_get_sz(output.sz_l->out, 0); //0:L  1:R

    //put_buf((u8 *)output.sz_l->out, 120 * 4);

    //根据SZ计算CMP，并更新FF/FB/CMP等滤波器
    audio_rt_anc_param_updata(rt_param_l, rt_param_r);

#if AUDIO_RT_ANC_EXPORT_TOOL_DATA_DEBUG
    audio_anc_real_time_adaptive_data_packet(hdl->rtanc_tool);
#endif

#if ANC_EAR_ADAPTIVE_CMP_EN
    audio_anc_real_time_adaptive_suspend();
#endif

#if TCFG_AUDIO_ADAPTIVE_EQ_ENABLE
    //如果AEQ在运行就挂起RTANC
    if (audio_adaptive_eq_state_get()) {
        audio_anc_real_time_adaptive_suspend();
    }
#endif
    //执行挂在到AFQ 的算法，如AEQ, 内部会挂起RTANC
    audio_afq_common_output_post_msg(&output);

    free(output.sz_l->out);
    free(output.sz_l);

    //恢复RT_ANC
    audio_anc_real_time_adaptive_resume();

    rtanc_log("RTANC_STATE:OUTPUT_RUN END\n");
    hdl->run_busy = 0;
}

//RT ANC挂起接口
void audio_anc_real_time_adaptive_suspend(void)
{
    if (audio_anc_real_time_adaptive_state_get()) {
        hdl->suspend_cnt++;
        if ((hdl->suspend_cnt == 1) && (hdl->state == RT_ANC_STATE_OPEN)) {
            rtanc_log("RTANC_STATE:SUSPEND\n");
            hdl->state = RT_ANC_STATE_SUSPEND;
            if (!hdl->reset_busy) {
                icsd_adt_rtanc_suspend();
            }
            rtanc_log("%s succ\n", __func__);
        }
    }
}

//RT ANC恢复接口
void audio_anc_real_time_adaptive_resume(void)
{
    if (audio_anc_real_time_adaptive_state_get()) {
        if (hdl->suspend_cnt) {
            hdl->suspend_cnt--;
            if ((hdl->suspend_cnt == 0) && (hdl->state == RT_ANC_STATE_SUSPEND)) {
                rtanc_log("RTANC_STATE:SUSPEND->OPEN\n");
                hdl->state = RT_ANC_STATE_OPEN;
                if (!hdl->reset_busy) {
                    icsd_adt_rtanc_resume();
                }
                rtanc_log("%s succ\n", __func__);
            }
        }
    }
}

int audio_anc_real_time_adaptive_suspend_get(void)
{
    if (hdl) {
        return hdl->suspend_cnt;
    }
    return 0;
}

//打开RTANC 前准备，sync_mode 是否TWS同步模式
int audio_rtanc_adaptive_init(u8 sync_mode)
{
    int ret;
    anc_ext_tool_rtanc_suspend_clear();

    rtanc_log("=================rt_anc_open==================\n");
    rtanc_log("rtanc state %d\n", audio_anc_real_time_adaptive_state_get());
    ret = audio_rtanc_permit(sync_mode);
    if (ret) {
        rtanc_log("Err:rt_anc_open permit sync:%d, ret:%d\n", sync_mode, ret);
        return ret;
    }

    hdl->run_busy = 0;
    hdl->suspend_cnt = 0;
    rtanc_log("RTANC_STATE:OPEN\n");
    hdl->state = RT_ANC_STATE_OPEN;
    clock_alloc("ANC_RT_ADAPTIVE", 48 * 1000000L);
    audio_rt_anc_iir_type_get();	//获取工具配置IIR TYPE
    hdl->pz_cmp = zalloc(50 * sizeof(float));
    hdl->sz_cmp = zalloc(50 * sizeof(float));
#if ANC_EAR_ADAPTIVE_CMP_EN
    audio_anc_ear_adaptive_cmp_open(CMP_FROM_RTANC);
#endif
    return 0;
}

int audio_rtanc_adaptive_exit(void)
{
    rtanc_log("================rt_anc_close==================\n");
    if (audio_anc_real_time_adaptive_state_get() == 0) {
        return 1;
    }
    rtanc_log("RTANC_STATE:CLOSE\n");
    hdl->state = RT_ANC_STATE_CLOSE;
    clock_free("ANC_RT_ADAPTIVE");
    free(hdl->pz_cmp);
    free(hdl->sz_cmp);
    hdl->pz_cmp = NULL;
    hdl->sz_cmp = NULL;

#if ANC_EAR_ADAPTIVE_CMP_EN
    audio_anc_ear_adaptive_cmp_close();
#endif
    rtanc_log("rt_anc_close ok\n");
    return 0;
}

int audio_rtanc_adaptive_en(u8 en)
{
    int ret;
    if (en) {
        ret = audio_rtanc_permit(0);
        if (ret) {
            return ret;
        }
        return audio_icsd_adt_sync_open(hdl->rtanc_mode);
    } else {
        audio_icsd_adt_sync_close(ADT_REAL_TIME_ADAPTIVE_ANC_MODE | ADT_REAL_TIME_ADAPTIVE_ANC_TIDY_MODE, 0);
    }
    return 0;
}

//获取用户端是否启用这个功能
u8 audio_rtanc_app_func_en_get(void)
{
    return hdl->app_func_en;
}

int audio_anc_real_time_adaptive_open(void)
{
    hdl->app_func_en = 1;
    return audio_rtanc_adaptive_en(1);
}

int audio_anc_real_time_adaptive_close(void)
{
    int ret;
    hdl->app_func_en = 0;
    ret = audio_rtanc_adaptive_en(0);
    //恢复默认ANC参数
    if ((!ret) && (anc_mode_get() == ANC_ON)) {
#if ANC_MULT_ORDER_ENABLE
        /* audio_anc_mult_scene_set(audio_anc_mult_scene_get()); */
        audio_anc_mult_scene_update(audio_anc_mult_scene_get());
#else
        audio_anc_db_cfg_read();
        audio_anc_coeff_smooth_update();
#endif
    }
    return ret;
}

#if AUDIO_RT_ANC_EXPORT_TOOL_DATA_DEBUG
static void audio_anc_real_time_adaptive_data_packet(struct icsd_rtanc_tool_data *rtanc_tool)
{
    if (!rtanc_tool) {
        return;
    }
    int len = rtanc_tool->h_len;
    int ff_yorder = hdl->param->lff_yorder;

    int ff_dat_len =  sizeof(anc_fr_t) * ff_yorder + 4;


    u8 *ff_dat, *rff_dat;


#if ANC_CONFIG_LFF_EN
    ff_dat = zalloc(ff_dat_len);
    //RTANC 增益没有输出符号，需要对齐工具符号
    rtanc_tool->ff_fgq_l[0] = (hdl->param->gains.gain_sign & ANCL_FF_SIGN) ? \
                              (0 - rtanc_tool->ff_fgq_l[0]) : rtanc_tool->ff_fgq_l[0];
    audio_anc_fr_format(ff_dat, rtanc_tool->ff_fgq_l, ff_yorder, hdl->lff_iir_type);
#endif/*ANC_CONFIG_LFF_EN*/
#if ANC_CONFIG_RFF_EN
    rff_dat = zalloc(ff_dat_len);
    //RTANC 增益没有输出符号，需要对齐工具符号
    rtanc_tool->ff_fgq_r[0] = (hdl->param->gains.gain_sign & ANCR_FF_SIGN) ? \
                              (0 - rtanc_tool->ff_fgq_r[0]) : rtanc_tool->ff_fgq_r[0];
    audio_anc_fr_format(rff_dat, rtanc_tool->ff_fgq_r, ff_yorder, hdl->rff_iir_type);
#endif/*ANC_CONFIG_RFF_EN*/

    rtanc_log("-- len1: %d, len2: %d\n", rtanc_tool->h_len, rtanc_tool->h2_len);
    rtanc_log("-- ff_yorder = %d\n", ff_yorder);
    /* 先统一申请空间，因为下面不知道什么情况下调用函数 anc_data_catch 时令参数 init_flag 为1 */
    rtanc_tool_data = anc_data_catch(rtanc_tool_data, NULL, 0, 0, 1);

#if TCFG_USER_TWS_ENABLE
    if (bt_tws_get_local_channel() == 'R') {
        r_printf("RTANC export send tool_data, ch:R\n");
        if (hdl->param->developer_mode) {
            rtanc_tool_data = anc_data_catch(rtanc_tool_data, (u8 *)rtanc_tool->h_freq, len * 4, ANC_R_ADAP_FRE, 0);
            rtanc_tool_data = anc_data_catch(rtanc_tool_data, (u8 *)rtanc_tool->sz_out_l, len * 8, ANC_R_ADAP_SZPZ, 0);
            rtanc_tool_data = anc_data_catch(rtanc_tool_data, (u8 *)rtanc_tool->pz_out_l, len * 8, ANC_R_ADAP_PZ, 0);
            rtanc_tool_data = anc_data_catch(rtanc_tool_data, (u8 *)rtanc_tool->target_out_l, len * 8, ANC_R_ADAP_TARGET, 0);
            rtanc_tool_data = anc_data_catch(rtanc_tool_data, (u8 *)rtanc_tool->target_out_cmp_l, len * 8, ANC_R_ADAP_TARGET_CMP, 0);
        }
        rtanc_tool_data = anc_data_catch(rtanc_tool_data, (u8 *)ff_dat, ff_dat_len, ANC_R_FF_IIR, 0);  //R_ff

        rtanc_tool_data = anc_data_catch(rtanc_tool_data, (u8 *)rtanc_tool->h2_freq, rtanc_tool->h2_len * 4, ANC_R_ADAP_FRE_2, 0);
        rtanc_tool_data = anc_data_catch(rtanc_tool_data, (u8 *)rtanc_tool->mse_l, rtanc_tool->h2_len * 8, ANC_R_ADAP_MSE, 0);
    } else
#endif/*TCFG_USER_TWS_ENABLE*/
    {
        r_printf("RTANC export send tool_data, ch:L\n");

        /* put_buf((u8 *)rtanc_tool->h_freq, len * 4); */
        /* put_buf((u8 *)rtanc_tool->sz_out_l, len * 8); */
        /* put_buf((u8 *)rtanc_tool->pz_out_l, len * 8); */
        /* put_buf((u8 *)rtanc_tool->target_out_l, len * 8); */
        /* put_buf((u8 *)rtanc_tool->target_out_cmp_l, len * 8); */
        /* put_buf((u8 *)rtanc_tool->h2_freq, rtanc_tool->h2_len * 4); */
        /* put_buf((u8 *)rtanc_tool->mse_l, rtanc_tool->h2_len * 8); */

        /* put_buf((u8 *)ff_dat, ff_dat_len);  //R_ff */
        /* put_buf((u8 *)cmp_dat, cmp_dat_len);  //R_cmp */

        if (hdl->param->developer_mode) {
            rtanc_tool_data = anc_data_catch(rtanc_tool_data, (u8 *)rtanc_tool->h_freq, len * 4, ANC_L_ADAP_FRE, 0);
            rtanc_tool_data = anc_data_catch(rtanc_tool_data, (u8 *)rtanc_tool->sz_out_l, len * 8, ANC_L_ADAP_SZPZ, 0);
            rtanc_tool_data = anc_data_catch(rtanc_tool_data, (u8 *)rtanc_tool->pz_out_l, len * 8, ANC_L_ADAP_PZ, 0);
            rtanc_tool_data = anc_data_catch(rtanc_tool_data, (u8 *)rtanc_tool->target_out_l, len * 8, ANC_L_ADAP_TARGET, 0);
            rtanc_tool_data = anc_data_catch(rtanc_tool_data, (u8 *)rtanc_tool->target_out_cmp_l, len * 8, ANC_L_ADAP_TARGET_CMP, 0);
        }
        rtanc_tool_data = anc_data_catch(rtanc_tool_data, (u8 *)ff_dat, ff_dat_len, ANC_L_FF_IIR, 0);  //R_ff

        rtanc_tool_data = anc_data_catch(rtanc_tool_data, (u8 *)rtanc_tool->h2_freq, rtanc_tool->h2_len * 4, ANC_L_ADAP_FRE_2, 0);
        rtanc_tool_data = anc_data_catch(rtanc_tool_data, (u8 *)rtanc_tool->mse_l, rtanc_tool->h2_len * 8, ANC_L_ADAP_MSE, 0);
    }

#if ANC_CONFIG_LFF_EN
    free(ff_dat);
#endif/*ANC_CONFIG_LFF_EN*/
#if ANC_CONFIG_RFF_EN
    free(rff_dat);
#endif/*ANC_CONFIG_RFF_EN*/

}

void audio_rtanc_cmp_data_packet(void)
{
    if (!hdl->rtanc_tool) {
        return;
    }
#if ANC_EAR_ADAPTIVE_CMP_EN
    int cmp_yorder = ANC_ADAPTIVE_CMP_ORDER;
    int cmp_dat_len =  sizeof(anc_fr_t) * cmp_yorder + 4;
#if ANC_CONFIG_LFB_EN
    float *lcmp_output = audio_anc_ear_adaptive_cmp_output_get(ANC_EAR_ADAPTIVE_CMP_CH_L);
    u8 *lcmp_type = audio_anc_ear_adaptive_cmp_type_get(ANC_EAR_ADAPTIVE_CMP_CH_L);
    u8 *cmp_dat = zalloc(cmp_dat_len);
    audio_anc_fr_format(cmp_dat, lcmp_output, cmp_yorder, lcmp_type);
#endif

#if ANC_CONFIG_RFB_EN
    float *rcmp_output = audio_anc_ear_adaptive_cmp_output_get(ANC_EAR_ADAPTIVE_CMP_CH_R);
    u8 *rcmp_type = audio_anc_ear_adaptive_cmp_type_get(ANC_EAR_ADAPTIVE_CMP_CH_R);
    u8 *rcmp_dat = zalloc(cmp_dat_len);
    audio_anc_fr_format(rcmp_dat, rcmp_output, cmp_yorder, rcmp_type);
#endif

    rtanc_log("cmp_data_packet yorder = %d\n", cmp_yorder);

#if TCFG_USER_TWS_ENABLE
    if (bt_tws_get_local_channel() == 'R') {
        rtanc_log("cmp export send tool_data, ch:R\n");
#if ANC_CONFIG_LFB_EN
        rtanc_tool_data = anc_data_catch(rtanc_tool_data, (u8 *)cmp_dat, cmp_dat_len, ANC_R_CMP_IIR, 0);  //R_cmp
#endif
    } else
#endif/*TCFG_USER_TWS_ENABLE*/
    {
        rtanc_log("cmp export send tool_data, ch:L\n");

        /* put_buf((u8 *)cmp_dat, cmp_dat_len);  //R_cmp */
#if ANC_CONFIG_LFB_EN
        rtanc_tool_data = anc_data_catch(rtanc_tool_data, (u8 *)cmp_dat, cmp_dat_len, ANC_L_CMP_IIR, 0);  //R_cmp
#endif
    }

#if ANC_CONFIG_LFB_EN
    free(cmp_dat);
#endif

#if ANC_CONFIG_RFB_EN
    free(rcmp_dat);
#endif

#endif
}

#endif

int audio_anc_real_time_adaptive_tool_data_get(u8 **buf, u32 *len)
{
    if ((!audio_anc_real_time_adaptive_suspend_get()) && audio_anc_real_time_adaptive_state_get()) {
        rtanc_log("rtanc tool_data err: running! need suspend/close, return!\n");
        return -1;
    }
    if (rtanc_tool_data == NULL) {
        rtanc_log("rtanc tool_data err: packet is NULL, return!\n");
        return -1;
    }
    if (rtanc_tool_data->dat_len == 0) {
        rtanc_log("rtanc tool_data err: dat_len == 0\n");
        return -1;
    }
    g_printf("rtanc_tool_data->dat_len %d\n", rtanc_tool_data->dat_len);
    *buf = rtanc_tool_data->dat;
    *len = rtanc_tool_data->dat_len;
    return 0;
}

int audio_rtanc_fade_gain_suspend(struct rt_anc_fade_gain_ctr *fade_ctr)
{
    g_printf("%s, state %d\n", __func__, audio_anc_real_time_adaptive_state_get());
    if ((!audio_anc_real_time_adaptive_state_get()) || hdl->reset_busy || \
        get_icsd_adt_mode_switch_busy() || anc_mode_switch_lock_get()) {
        hdl->fade_ctr = *fade_ctr;
        return 0;
    }
    if (hdl) {
        if (memcmp(&hdl->fade_ctr, fade_ctr, sizeof(struct rt_anc_fade_gain_ctr))) {
            hdl->fade_gain_suspend = 1;
            audio_anc_real_time_adaptive_suspend();

            struct rtanc_param *rtanc_p;

            rtanc_p = zalloc(sizeof(struct rtanc_param));

            rtanc_p->ffl_filter.fade_gain = ((float)fade_ctr->lff_gain) / 16384.0f;
            rtanc_p->fbl_filter.fade_gain = ((float)fade_ctr->lfb_gain) / 16384.0f;
            rtanc_p->ffr_filter.fade_gain = ((float)fade_ctr->rff_gain) / 16384.0f;
            rtanc_p->fbr_filter.fade_gain = ((float)fade_ctr->rfb_gain) / 16384.0f;

            icsd_adt_rtanc_fadegain_update_run(rtanc_p);
            hdl->fade_ctr = *fade_ctr;
            free(rtanc_p);
        }
    }
    return 0;
}

int audio_rtanc_fade_gain_resume(void)
{
    if (hdl->fade_gain_suspend) {
        hdl->fade_gain_suspend = 0;
        audio_anc_real_time_adaptive_resume();
    }
    return 0;
}

static u8 RTANC_idle_query(void)
{
    if (hdl) {
        return (hdl->state) ? 0 : 1;
    }
    return 1;
}

u8 audio_anc_real_time_adaptive_state_get(void)
{
    return (!RTANC_idle_query());
}

u8 audio_anc_real_time_adaptive_run_busy_get(void)
{
    if (hdl) {
        return hdl->run_busy;
    }
    return 0;
}

REGISTER_LP_TARGET(RTANC_lp_target) = {
    .name       = "RTANC",
    .is_idle    = RTANC_idle_query,
};

//复位RTANC模式，阻塞处理
int audio_anc_real_time_adaptive_reset(int rtanc_mode, u8 wind_close)
{
    static u8 wind_flag = 0;
    int wait_cnt = 0, switch_busy = 0, mode_cmp = 0, close_mode = 0;
    if (hdl->rtanc_mode == rtanc_mode) {
        rtanc_log("Err:rtanc adaptive reset fail, same mode\n");
        return -1;
    }

    if ((get_icsd_adt_mode() & ADT_WIND_NOISE_DET_MODE) && wind_close) {
        wind_flag = 1;
    }

    hdl->rtanc_mode = rtanc_mode;
    if (audio_anc_real_time_adaptive_state_get()) {
        u32 last = jiffies_usec();
        rtanc_log("%s, mode = %x start\n", __func__, rtanc_mode);
        //挂起RTANC
        audio_anc_real_time_adaptive_suspend();
        hdl->reset_busy = 1;
        //设置为默认参数, 但是不更新
        /* audio_anc_mult_scene_update(audio_anc_mult_scene_get()); */
        /* audio_anc_mult_scene_set(audio_anc_mult_scene_get());	//耗时30ms */
        //修改ADT MODE 目标模式
        if (rtanc_mode == ADT_REAL_TIME_ADAPTIVE_ANC_MODE && wind_close && wind_flag) {
            wind_flag = 0;
            rtanc_mode |= ADT_WIND_NOISE_DET_MODE;
        }
        icsd_adt_rt_anc_mode_set(rtanc_mode);
        //复位ADT
        if (rtanc_mode == ADT_REAL_TIME_ADAPTIVE_ANC_TIDY_MODE) {
            close_mode = ADT_REAL_TIME_ADAPTIVE_ANC_MODE;
            if (wind_flag) {
                close_mode |= ADT_WIND_NOISE_DET_MODE;
            }
            /* audio_icsd_adt_sync_close(close_mode, 0); //qhh：icsd的模式需要同步修改，否则修改后不要用同步接口打开adt*/
            audio_icsd_adt_close(close_mode, 0);
        } else {
            close_mode = ADT_REAL_TIME_ADAPTIVE_ANC_TIDY_MODE;
            /* audio_icsd_adt_sync_close(close_mode, 0); //qhh：icsd的模式需要同步修改，否则修改后不要用同步接口打开adt*/
            audio_icsd_adt_close(close_mode, 0);
        }
        //等待切换完毕
        wait_cnt = 200;
        switch_busy = 1;
        while (switch_busy && wait_cnt) {
            mode_cmp = get_icsd_adt_mode() & close_mode;
            switch_busy = get_icsd_adt_mode_switch_busy() | mode_cmp;
            //rtanc_log("adt_mode 0x%x, close_mode %x, s_busy %d\n", get_icsd_adt_mode(), close_mode, get_icsd_adt_mode_switch_busy());
            wait_cnt--;
            os_time_dly(1);
        }
        if (!wait_cnt) {
            rtanc_log("ERR: %s timeout err!!\n", __func__);
        }
        //恢复RTANC
        hdl->reset_busy = 0;
        audio_anc_real_time_adaptive_resume();
        rtanc_log("%s, end, %d\n", __func__, jiffies_usec2offset(last, jiffies_usec()));
    } else {
        return -1;
    }
    return 0;
}

void audio_real_time_adaptive_ignore_switch_lock(void)
{
    if (hdl) {
        hdl->ignore_switch_lock = 1;
    }
}

#if AUDIO_RT_ANC_SZ_PZ_CMP_EN
static void audio_rtanc_sz_pz_cmp_process(void)
{
    struct sz_pz_cmp_cal_param p;

#if TCFG_SPEAKER_EQ_NODE_ENABLE
    hdl->spk_eq_tab_l.global_gain = spk_eq_read_global_gain(0);
    hdl->spk_eq_tab_l.global_gain = eq_db2mag(hdl->spk_eq_tab_l.global_gain);
    hdl->spk_eq_tab_l.seg_num = spk_eq_read_seg_l((u8 **)&hdl->spk_eq_tab_l.seg) / sizeof(struct eq_seg_info);
#endif

#if ANC_DUT_MIC_CMP_GAIN_ENABLE
    hdl->mic_cmp = &hdl->param->mic_cmp;
#endif

#if TCFG_SPEAKER_EQ_NODE_ENABLE
    p.spk_eq_tab = &hdl->spk_eq_tab_l;
#else
    p.spk_eq_tab = NULL;
#endif
    p.pz_cmp_out = hdl->pz_cmp;
    p.sz_cmp_out = hdl->sz_cmp;
    p.ff_gain = hdl->mic_cmp->lff_gain;
    p.fb_gain = hdl->mic_cmp->lfb_gain;
    icsd_anc_v2_sz_pz_cmp_calculate(&p);
}

void audio_rtanc_self_talk_output(u8 flag)
{
#if AUDIO_RT_ANC_SELF_TALK_FLAG
    printf("self talk flag: %d\n", flag);
    audio_anc_debug_app_send_data(ANC_DEBUG_APP_CMD_RTANC, 0x0, &flag, 1);
#endif
}

float *audio_rtanc_pz_cmp_get(void)
{
    if (hdl) {
        return hdl->pz_cmp;
    }
    return NULL;
}

float *audio_rtanc_sz_cmp_get(void)
{
    if (hdl) {
        return hdl->sz_cmp;
    }
    return NULL;
}
#endif

//测试demo
int audio_anc_real_time_adaptive_demo(void)
{
#if 1//tone
    if (hdl->state == RT_ANC_STATE_CLOSE) {
        if (audio_anc_real_time_adaptive_open()) {
            icsd_adt_tone_play(ICSD_ADT_TONE_NUM0);
        } else {
            icsd_adt_tone_play(ICSD_ADT_TONE_NUM1);
        }
    } else {
        icsd_adt_tone_play(ICSD_ADT_TONE_NUM0);
        audio_anc_real_time_adaptive_close();
    }
#else
    if (hdl->state == RT_ANC_STATE_CLOSE) {
        audio_anc_real_time_adaptive_open();
    } else {
        audio_anc_real_time_adaptive_close();
    }
#endif
    return 0;
}

#endif
