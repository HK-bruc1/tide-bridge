/*
 	用于管理与audio_anc.c交互部分的自适应代码
 */

#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".icsd_anc_interactive.data.bss")
#pragma data_seg(".icsd_anc_interactive.data")
#pragma const_seg(".icsd_anc_interactive.text.const")
#pragma code_seg(".icsd_anc_interactive.text")
#endif

#include "app_config.h"
#include "audio_config_def.h"
#if (TCFG_AUDIO_ANC_EAR_ADAPTIVE_EN && \
	 TCFG_AUDIO_ANC_ENABLE && \
	 TCFG_AUDIO_ANC_EAR_ADAPTIVE_VERSION == ANC_EXT_V1)

#include "audio_anc.h"
#include "icsd_anc_app.h"
#include "clock_manager/clock_manager.h"
#include "audio_config.h"
#include "app_tone.h"
#include "esco_player.h"
#include "a2dp_player.h"
#include "le_audio_player.h"
#include "dac_node.h"
#include "adc_file.h"
#if TCFG_USER_TWS_ENABLE
#include "bt_tws.h"
#endif/*TCFG_USER_TWS_ENABLE*/
#include "anc_ext_tool.h"

#if ANC_MULT_ORDER_ENABLE
#include "audio_anc_mult_scene.h"
#endif/*ANC_MULT_ORDER_ENABLE*/

#if TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN
#include "icsd_adt_app.h"
#endif

#if 1
#define anc_log	printf
#else
#define anc_log(...)
#endif/*log_en*/

enum EAR_ADAPTIVE_STATE {
    EAR_ADAPTIVE_STATE_STOP = 0,
    EAR_ADAPTIVE_STATE_INIT,
    EAR_ADAPTIVE_STATE_START,

    EAR_ADAPTIVE_STATE_PNC1_OPEN,
    EAR_ADAPTIVE_STATE_PNC1_STABLE,

    EAR_ADAPTIVE_STATE_BYPASS_OPEN,
    EAR_ADAPTIVE_STATE_BYPASS_STABLE,

    EAR_ADAPTIVE_STATE_PNC2_OPEN,
    EAR_ADAPTIVE_STATE_PNC2_STABLE,
};

enum EAR_ADAPTIVE_TONE_STATE {
    EAR_ADAPTIVE_TONE_STATE_STOP = 0,
    EAR_ADAPTIVE_TONE_STATE_START,
    EAR_ADAPTIVE_TONE_STATE_NO_SLIENCE,
};

struct anc_ear_adaptive_param {
    u8 tws_sync;         	 		/*左右耳平衡使能*/
    u8 seq;							/*seq计数*/
    u8 dac_check_slience_flag;		/*DAC静音检测标志*/
    u8 ear_adaptive_data_from;		/*滤波器数据来源*/
    u8 busy;						/*检测繁忙标记*/
    u8 tone_state;					/*TONE state*/
    u8 adaptive_state;				/*ADAPTIVE state*/
    u8 forced_exit_default_flag;	/*强退恢复默认ANC标志*/
    audio_anc_t *param;
    anc_adaptive_iir_t adaptive_iir;

};

anc_packet_data_t *anc_adaptive_data = NULL;
static struct anc_ear_adaptive_param *hdl = NULL;

static void audio_anc_adaptive_poweron_catch_data(anc_adaptive_iir_t *iir);
static void audio_anc_adaptive_data_format(anc_adaptive_iir_t *iir);
static int audio_anc_coeff_adaptive_set_base(u32 mode, u8 tone_play, u8 ignore_busy);

/* 参数初始化 */
void anc_ear_adaptive_init(audio_anc_t *param)
{
    //内部使用句柄
    hdl = zalloc(sizeof(struct anc_ear_adaptive_param));
    hdl->ear_adaptive_data_from = ANC_ADAPTIVE_DATA_EMPTY;
    //库关联句柄
    hdl->param = param;
    param->adaptive = zalloc(sizeof(anc_ear_adaptive_param_t));
    ASSERT(param->adaptive);
    param->adaptive->ff_yorder = ANC_ADAPTIVE_FF_ORDER;
    param->adaptive->fb_yorder = ANC_ADAPTIVE_FB_ORDER;
    param->adaptive->cmp_yorder = ANC_ADAPTIVE_CMP_ORDER;
    param->adaptive->dma_done_cb = icsd_anc_dma_done;
}

/* ANC TASK 命令回调处理 */
void anc_ear_adaptive_cmd_handler(audio_anc_t *param, int *msg)
{
    if (hdl == NULL) {
        return;
    }
    switch (msg[1]) {
    case ANC_MSG_ICSD_ANC_CMD:
        icsd_anc_anctask_cmd_handle((u8)msg[2]);
        break;
    case ANC_MSG_USER_TRAIN_INIT:
        //自适应V1版本是低频间歇性处理，时钟越高，自适应时间越短
        clock_alloc("ANC_ADAP", 160 * 1000000L);
        if (++hdl->seq == 0xff) {
            hdl->seq = 0;
        }
        icsd_anc_init(hdl->param, 1, hdl->seq, ANC_ADAPTIVE_TONE_DELAY, hdl->tws_sync);
        break;
    case ANC_MSG_USER_TRAIN_RUN:
        icsd_anc_run();
        break;
    case ANC_MSG_USER_TRAIN_SETPARAM:
        icsd_anc_setparam();
        break;
    case ANC_MSG_USER_TRAIN_TIMEOUT:
        icsd_anc_timeout_handler();
        break;
    case ANC_MSG_USER_TRAIN_END:
        icsd_anc_end(hdl->param);
        clock_free("ANC_ADAP");
        break;
    }
}

/* 耳道自适应提示音启动接口 */
void anc_ear_adaptive_tone_play_start(void)
{
    anc_log("EAR_ADAPTIVE_TONE_STATE_START\n");
    hdl->tone_state = EAR_ADAPTIVE_TONE_STATE_START;
    audio_mic_pwr_ctl(MIC_PWR_ON);
    audio_anc_dac_open(hdl->param->gains.dac_gain, hdl->param->gains.dac_gain);
#ifdef CONFIG_CPU_BR28
    hdl->param->adc_set_buffs_cb();
#endif
    audio_anc_mic_open(hdl->param->mic_param, 0, hdl->param->adc_ch);
#if ANC_USER_TRAIN_TONE_MODE
    if (hdl->param->mode == ANC_ON) {
        anc_log("EAR_ADAPTIVE_STATE:START\n");
        hdl->adaptive_state = EAR_ADAPTIVE_STATE_START;
        icsd_anc_tone_play_start();
    }
    //准备播提示音才开始起标志
    hdl->dac_check_slience_flag = 1;
#endif/*ANC_USER_TRAIN_TONE_MODE*/
}

/* 耳道自适应提示音播放异常回调接口 */
void anc_ear_adaptive_tone_play_err_cb(void)
{
    anc_log("EAR_ADAPTIVE_TONE_STATE_ERR \n");
    icsd_anc_forced_exit();
}

/* 耳道自适应提示音播放结束回调接口 */
void anc_ear_adaptive_tone_play_cb(void)
{
    anc_log("EAR_ADAPTIVE_TONE_STATE_STOP\n");
    hdl->tone_state = EAR_ADAPTIVE_TONE_STATE_STOP;
    anc_user_train_tone_play_cb();
}

/* 耳道自适应开发者模式设置 */
void anc_ear_adaptive_develop_set(u8 mode)
{
    if (hdl->param->mode != ANC_OFF) {
        anc_mode_switch(ANC_OFF, 0);									//避免右耳参数异常，让调试者感受不适
    }
    if (hdl->param->anc_coeff_mode == ANC_COEFF_MODE_ADAPTIVE) {
        return;															//自适应模式不更新参数
    }
    if (mode) { //0 -> 1
        audio_anc_param_map(1, 1);										//映射ANC参数
        audio_anc_adaptive_poweron_catch_data(&hdl->adaptive_iir);	//打包自适应数据,等待工具读取
    } else {    //1 -> 0
        audio_anc_db_cfg_read();										//读取ANC配置区的数据
        if (anc_adaptive_data) {
            free_anc_data_packet(anc_adaptive_data);
            anc_adaptive_data = NULL;
        }
    }
}


/* DAC非静音数据回调 */
static void audio_anc_dac_check_slience_cb(void *buf, int len)
{
    if (hdl->dac_check_slience_flag) {
        s16 *check_buf = (s16 *)buf;
        for (int i = 0; i < len / 2; i++) {
            if (check_buf[i]) {
                hdl->dac_check_slience_flag = 0;
                anc_log("EAR_ADAPTIVE_TONE_STATE_NO_SLIENCE\n");
                hdl->tone_state = EAR_ADAPTIVE_TONE_STATE_NO_SLIENCE;
                icsd_anctone_dacon(i, dac_hdl.sample_rate);
                dac_node_write_callback_del("ANC_ADAP");
                break;
            }
        }
    }
}

/* 耳道自适应启动限制 */
int audio_anc_mode_ear_adaptive_permit(void)
{
    if (hdl->param->mode != ANC_ON) {	//非ANC模式
        return 1;
    }
    if (hdl->busy) { //重入保护
        return 1;
    }
    if (adc_file_is_runing()) { //通话不支持
        /* if (esco_player_runing()) { //通话不支持 */
        return 1;
    }
    if (anc_mode_switch_lock_get()) { //其他切模式过程不支持
        return 1;
    }
    return 0;
}

int audio_anc_ear_adaptive_open(void)
{
#if ANC_MULT_ORDER_ENABLE
    //载入场景参数-提供自适应训练过程使用
    u8 scene_id = ANC_MULT_ADPTIVE_TRAIN_USE_ID ? ANC_MULT_ADPTIVE_TRAIN_USE_ID : audio_anc_mult_scene_get();
    anc_mult_scene_set(scene_id);
#endif/*ANC_MULT_ORDER_ENABLE*/

    //ANC切模式流程自适应标志，播放完提示音会清0
    audio_ear_adaptive_en_set(1);

    //加载训练参数
    hdl->param->anc_coeff_mode = ANC_COEFF_MODE_ADAPTIVE;	//切到自适应的参数
    hdl->busy = 1;
    audio_anc_ear_adaptive_param_init(hdl->param);
#if ANC_USER_TRAIN_TONE_MODE
    hdl->dac_check_slience_flag = 1;
    dac_node_write_callback_add("ANC_ADAP", 0XFF, audio_anc_dac_check_slience_cb);	//注册DAC回调接口-静音检测
#endif/*ANC_USER_TRAIN_TONE_MODE*/
    /*自适应时需要挂起adt*/
#if TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN
    audio_anc_mode_switch_in_adt(ANC_ON);
#endif
    anc_mode_switch(ANC_ON, ANC_USER_TRAIN_TONE_MODE);
    return 0;
}

int audio_anc_ear_adaptive_a2dp_suspend_cb(void)
{
    if (hdl->adaptive_state == EAR_ADAPTIVE_STATE_INIT) {
        audio_anc_ear_adaptive_open();
    }
    return 0;
}

/*自适应模式-重新检测
 * param: tws_sync_en          1 TWS同步自适应，支持TWS降噪平衡，需左右耳一起调用此接口
 *                             0 单耳自适应, 不支持TWS降噪平衡，可TWS状态下单耳自适应
 */
int audio_anc_mode_ear_adaptive(u8 tws_sync_en)
{
    hdl->tws_sync = tws_sync_en;
    if (audio_anc_mode_ear_adaptive_permit()) {
#if (RCSP_ADV_EN && RCSP_ADV_ANC_VOICE && RCSP_ADV_ADAPTIVE_NOISE_REDUCTION)
        set_adaptive_noise_reduction_reset_callback(0);		//	无法进入自适应，返回失败结果
#endif
        return 1;
    }
    anc_log("EAR_ADAPTIVE_STATE:INIT\n");
    hdl->adaptive_state = EAR_ADAPTIVE_STATE_INIT;

    if (a2dp_player_runing()
#if LE_AUDIO_STREAM_ENABLE
        || le_audio_player_is_playing()
#endif
       ) {	//当前处于音乐播放状态, 注册解码任务打断，进入自适应
        jlstream_global_pause();
    } else {
        audio_anc_ear_adaptive_open();
    }

    return 0;
}

/*自适应结束回调函数*/
void anc_user_train_cb(u8 mode, u8 result, u8 forced_exit)
{
    anc_log("anc_adaptive end result 0x%x , coeff_use 0x%x\n", result, hdl->adaptive_iir.result);
    audio_anc_t *param = hdl->param;
    /* hdl->busy = 0; */	//gali

    /* audio_ear_adaptive_train_app_resume();	//恢复APP互斥功能 */// gali
    if (forced_exit == 1) {
        //强制打断，自适应定义为失败，使用默认参数
        hdl->adaptive_iir.result = 0;
        //删除自适应DAC回调接口
        hdl->dac_check_slience_flag = 0;
        dac_node_write_callback_del("ANC_ADAP");
        clock_free("ANC_ADAP");
    }

    if (hdl->adaptive_iir.result) {	//自适应成功或部分成功
        if (hdl->ear_adaptive_data_from == ANC_ADAPTIVE_DATA_FROM_VM) {		//释放掉原来VM使用的资源
            if (hdl->param->adaptive->lff_coeff) {
                free(hdl->param->adaptive->lff_coeff);
            };
            if (hdl->param->adaptive->lfb_coeff) {
                free(hdl->param->adaptive->lfb_coeff);
            };
            if (hdl->param->adaptive->lcmp_coeff) {
                free(hdl->param->adaptive->lcmp_coeff);
            };
            if (hdl->param->adaptive->rff_coeff) {
                free(hdl->param->adaptive->rff_coeff);
            };
            if (hdl->param->adaptive->rfb_coeff) {
                free(hdl->param->adaptive->rfb_coeff);
            };
            if (hdl->param->adaptive->rcmp_coeff) {
                free(hdl->param->adaptive->rcmp_coeff);
            };
        }
        hdl->ear_adaptive_data_from = ANC_ADAPTIVE_DATA_FROM_ALOGM;	//自适应参数更新
#if (TCFG_AUDIO_ANC_CH & ANC_L_CH)
        param->adaptive->lff_coeff = param->lff_coeff;
        param->adaptive->lfb_coeff = param->lfb_coeff;
#if ANC_EAR_ADAPTIVE_CMP_EN
        param->adaptive->lcmp_coeff = param->lcmp_coeff;
#endif/*ANC_EAR_ADAPTIVE_CMP_EN*/
#endif/*(TCFG_AUDIO_ANC_CH & ANC_L_CH)*/
#if (TCFG_AUDIO_ANC_CH & ANC_R_CH)
        param->adaptive->rff_coeff = param->rff_coeff;
        param->adaptive->rfb_coeff = param->rfb_coeff;
#if ANC_EAR_ADAPTIVE_CMP_EN
        param->adaptive->rcmp_coeff = param->rcmp_coeff;
#endif/*ANC_EAR_ADAPTIVE_CMP_EN*/
#endif/*(TCFG_AUDIO_ANC_CH & ANC_R_CH)*/
        if (result != ANC_SUCCESS) {	//非FF+FB同时成功
            g_printf("no all success");
            audio_anc_coeff_adaptive_set_base(ANC_COEFF_MODE_NORMAL, 0, 1);	//先覆盖默认参数
            audio_anc_coeff_adaptive_set_base(ANC_COEFF_MODE_ADAPTIVE, 0, 1);	//根据自适应结果选择覆盖参数
        }

#if (!ANC_POWEOFF_SAVE_ADAPTIVE_DATA) || ANC_DEVELOPER_MODE_EN
        audio_anc_adaptive_data_save(&hdl->adaptive_iir);
#endif/*ANC_POWEOFF_SAVE_ADAPTIVE_DATA*/
    } else {	//自适应失败使用默认的参数
        audio_anc_coeff_adaptive_set_base(ANC_COEFF_MODE_NORMAL, 0, 1);
    }
    jlstream_global_resume();

    //强退模式设置不恢复默认ANC
    if (!hdl->forced_exit_default_flag && forced_exit) {
        anc_mode_switch_lock_clean();
        anc_ear_adaptive_mode_end();
        return;
    }
    anc_mode_switch_deal(mode);
}

/*ANC滤波器模式循环切换*/
int audio_anc_coeff_adaptive_switch()
{
    if (hdl->param->mode == ANC_ON && !hdl->busy && !anc_mode_switch_lock_get()) {
        hdl->param->anc_coeff_mode ^= 1;
        audio_anc_coeff_adaptive_update(hdl->param->anc_coeff_mode, 1);
    }
    return 0;
}

/*当前ANC滤波器模式获取 0:普通参数 1:自适应参数*/
int audio_anc_coeff_mode_get(void)
{
    if (!hdl) {
        return 0;
    }
    return hdl->param->anc_coeff_mode;
}

/*
   自适应/普通参数切换(只切换参数，不更新效果)
	param:  mode 		0 使用普通参数; 1 使用自适应参数
			tone_play 	0 不播放提示音；1 播放提示音
 */
int audio_anc_coeff_adaptive_set(u32 mode, u8 tone_play)
{
    return audio_anc_coeff_adaptive_set_base(mode, tone_play, 0);
}

/*
   自适应/普通参数切换(切换参数，并立即更新效果)
	param:  mode 		0 使用普通参数; 1 使用自适应参数
			tone_play 	0 不播放提示音；1 播放提示音
 */
int audio_anc_coeff_adaptive_update(u32 mode, u8 tone_play)
{
    int ret = audio_anc_coeff_adaptive_set(mode, tone_play);
    audio_anc_param_reset(1);
    return ret;
}

static int audio_anc_coeff_adaptive_set_base(u32 mode, u8 tone_play, u8 ignore_busy)
{
    u8 result = hdl->adaptive_iir.result;
    if (hdl->busy && !ignore_busy) {
        return 0;
    }
    hdl->param->anc_coeff_mode = mode;
    switch (mode) {
    case 0:
        if (tone_play) {
            if (ANC_TONE_PREEMPTION) {
                play_tone_file_alone(get_tone_files()->anc_normal_coeff);
            } else {
                play_tone_file(get_tone_files()->anc_normal_coeff);
            }
            /* tone_play_index(IDEX_TONE_ANC_NORMAL_COEFF, ANC_TONE_PREEMPTION); */
        }
#if ANC_COEFF_SAVE_ENABLE
#if ANC_MULT_ORDER_ENABLE
        //自适应切默认参数，回到当前场景
        anc_mult_scene_set(audio_anc_mult_scene_get());
#else
        if (audio_anc_db_cfg_read()) {
            return 1;
        }
#endif/*ANC_MULT_ORDER_ENABLE*/
#endif/*ANC_COEFF_SAVE_ENABLE*/
        break;
    case 1:
        if (((!hdl->param->adaptive->lff_coeff) && hdl->param->lff_en && (result & ANC_ADAPTIVE_RESULT_LFF)) || \
            ((!hdl->param->adaptive->lfb_coeff) && hdl->param->lfb_en && (result & ANC_ADAPTIVE_RESULT_LFB)) || \
            ((!hdl->param->adaptive->rff_coeff) && hdl->param->rff_en && (result & ANC_ADAPTIVE_RESULT_RFF)) || \
            ((!hdl->param->adaptive->rfb_coeff) && hdl->param->rfb_en && (result & ANC_ADAPTIVE_RESULT_RFB))) {
            g_printf("anc no adaptive data\n");
            return 1;
        }
        if (tone_play) {
            if (ANC_TONE_PREEMPTION) {
                play_tone_file_alone(get_tone_files()->anc_adaptive_coeff);
            } else {
                play_tone_file(get_tone_files()->anc_adaptive_coeff);
            }
            /* tone_play_index(IDEX_TONE_ANC_ADAPTIVE_COEFF, ANC_TONE_PREEMPTION); */
        }
#if ANC_MULT_ORDER_ENABLE
        //载入场景参数-提供自适应部分成功时，匹配使用。
        u8 scene_id = ANC_MULT_ADPTIVE_MATCH_USE_ID ? ANC_MULT_ADPTIVE_MATCH_USE_ID : audio_anc_mult_scene_get();
        anc_mult_scene_set(scene_id);
#endif/*ANC_MULT_ORDER_ENABLE*/
        audio_anc_ear_adaptive_param_init(hdl->param);	//初始化自适应固定配置参数
#if TCFG_AUDIO_ANC_CH & ANC_L_CH
        if (result & ANC_ADAPTIVE_RESULT_LFF) {
            anc_log("adaptive coeff set lff\n");
            hdl->param->gains.l_ffgain = (hdl->adaptive_iir.lff_gain < 0) ? (0 - hdl->adaptive_iir.lff_gain) : hdl->adaptive_iir.lff_gain;
            hdl->param->lff_yorder = ANC_ADAPTIVE_FF_ORDER;
            hdl->param->lff_coeff = hdl->param->adaptive->lff_coeff;
        }
        if (result & ANC_ADAPTIVE_RESULT_LFB) {
            anc_log("adaptive coeff set lfb\n");
            hdl->param->gains.l_fbgain = (hdl->adaptive_iir.lfb_gain < 0) ? (0 - hdl->adaptive_iir.lfb_gain) : hdl->adaptive_iir.lfb_gain;
            hdl->param->lfb_yorder = ANC_ADAPTIVE_FB_ORDER;
            hdl->param->lfb_coeff = hdl->param->adaptive->lfb_coeff;
        }
#if ANC_EAR_ADAPTIVE_CMP_EN
        if (result & ANC_ADAPTIVE_RESULT_LCMP) {
            anc_log("adaptive coeff set lcmp\n");
            hdl->param->gains.l_cmpgain = (hdl->adaptive_iir.lcmp_gain < 0) ? (0 - hdl->adaptive_iir.lcmp_gain) : hdl->adaptive_iir.lcmp_gain;
            hdl->param->lcmp_coeff = hdl->param->adaptive->lcmp_coeff;
            hdl->param->lcmp_yorder = ANC_ADAPTIVE_CMP_ORDER;
        }
#endif/*ANC_EAR_ADAPTIVE_CMP_EN*/
#endif/*TCFG_AUDIO_ANC_CH*/

#if TCFG_AUDIO_ANC_CH & ANC_R_CH
        if (result & ANC_ADAPTIVE_RESULT_RFF) {
            anc_log("adaptive coeff set rff\n");
            hdl->param->gains.r_ffgain = (hdl->adaptive_iir.rff_gain < 0) ? (0 - hdl->adaptive_iir.rff_gain) : hdl->adaptive_iir.rff_gain;
            hdl->param->rff_yorder = ANC_ADAPTIVE_FF_ORDER;
            hdl->param->rff_coeff = hdl->param->adaptive->rff_coeff;
        }
        if (result & ANC_ADAPTIVE_RESULT_RFB) {
            anc_log("adaptive coeff set rfb\n");
            hdl->param->gains.r_fbgain = (hdl->adaptive_iir.rfb_gain < 0) ? (0 - hdl->adaptive_iir.rfb_gain) : hdl->adaptive_iir.rfb_gain;
            hdl->param->rfb_yorder = ANC_ADAPTIVE_FB_ORDER;
            hdl->param->rfb_coeff = hdl->param->adaptive->rfb_coeff;
        }
#if ANC_EAR_ADAPTIVE_CMP_EN
        if (result & ANC_ADAPTIVE_RESULT_RCMP) {
            anc_log("adaptive coeff set rcmp\n");
            hdl->param->gains.r_cmpgain = (hdl->adaptive_iir.rcmp_gain < 0) ? (0 - hdl->adaptive_iir.rcmp_gain) : hdl->adaptive_iir.rcmp_gain;
            hdl->param->rcmp_coeff = hdl->param->adaptive->rcmp_coeff;
            hdl->param->rcmp_yorder = ANC_ADAPTIVE_CMP_ORDER;
        }
#endif/*ANC_EAR_ADAPTIVE_CMP_EN*/
#endif/*TCFG_AUDIO_ANC_CH*/

        break;
    default:
        return 1;
    }
    //put_buf(param->lff_coeff, ANC_ADAPTIVE_FF_ORDER * 5 * 8);
    //put_buf(param->lfb_coeff, ANC_ADAPTIVE_FB_ORDER * 5 * 8);
    //put_buf(param->lcmp_coeff, ANC_ADAPTIVE_CMP_ORDER * 5 * 8);
    return 0;
}

void audio_anc_adaptive_data_format(anc_adaptive_iir_t *iir)
{
    if (hdl->ear_adaptive_data_from == ANC_ADAPTIVE_DATA_EMPTY) {
        hdl->ear_adaptive_data_from = ANC_ADAPTIVE_DATA_FROM_VM;
        if (hdl->param->lff_en) {
            hdl->param->adaptive->lff_coeff = malloc(ANC_ADAPTIVE_FF_ORDER * sizeof(double) * 5);
        }
        if (hdl->param->lfb_en) {
            hdl->param->adaptive->lfb_coeff = malloc(ANC_ADAPTIVE_FB_ORDER * sizeof(double) * 5);
#if ANC_EAR_ADAPTIVE_CMP_EN
            hdl->param->adaptive->lcmp_coeff = malloc(ANC_ADAPTIVE_CMP_ORDER * sizeof(double) * 5);
#endif/*ANC_EAR_ADAPTIVE_CMP_EN*/
        }
        if (hdl->param->rff_en) {
            hdl->param->adaptive->rff_coeff = malloc(ANC_ADAPTIVE_FF_ORDER * sizeof(double) * 5);
        }
        if (hdl->param->rfb_en) {
            hdl->param->adaptive->rfb_coeff = malloc(ANC_ADAPTIVE_FB_ORDER * sizeof(double) * 5);
#if ANC_EAR_ADAPTIVE_CMP_EN
            hdl->param->adaptive->rcmp_coeff = malloc(ANC_ADAPTIVE_CMP_ORDER * sizeof(double) * 5);
#endif/*ANC_EAR_ADAPTIVE_CMP_EN*/
        }

        anc_iir_t iir_lib;	//	库与外部指针分离
#if ANC_CONFIG_LFF_EN
        iir_lib.lff = hdl->adaptive_iir.lff;
#endif/*ANC_CONFIG_LFF_EN*/
#if ANC_CONFIG_LFB_EN
        iir_lib.lfb = hdl->adaptive_iir.lfb;
#if ANC_EAR_ADAPTIVE_CMP_EN
        iir_lib.lcmp = hdl->adaptive_iir.lcmp;
#endif/*ANC_EAR_ADAPTIVE_CMP_EN*/
#endif/*ANC_CONFIG_LFB_EN*/
#if ANC_CONFIG_RFF_EN
        iir_lib.rff = hdl->adaptive_iir.rff;
#endif/*ANC_CONFIG_RFF_EN*/
#if ANC_CONFIG_RFB_EN
        iir_lib.rfb = hdl->adaptive_iir.rfb;
#if ANC_EAR_ADAPTIVE_CMP_EN
        iir_lib.rcmp = hdl->adaptive_iir.rcmp;
#endif/*ANC_EAR_ADAPTIVE_CMP_EN*/
#endif/*ANC_CONFIG_RFB_EN*/
        audio_anc_adaptive_data_analsis(&iir_lib);
    }
}

/* 保存VM参数 */
int audio_anc_adaptive_data_save(anc_adaptive_iir_t *iir)
{
    printf("%s\n", __func__);
#if ANC_DEVELOPER_MODE_EN
    //保存前做数据格式处理，避免数据异常导致上电转换数据死机
    hdl->ear_adaptive_data_from = ANC_ADAPTIVE_DATA_EMPTY;
    audio_anc_adaptive_data_format(iir);
#endif/*ANC_DEVELOPER_MODE_EN*/
    int ret = syscfg_write(CFG_ANC_ADAPTIVE_DATA_ID, (u8 *)iir, sizeof(anc_adaptive_iir_t));
    /* printf("vmdata_num %d\n", *vmdata_num); */
    /* put_buf((u8*)vmdata_FL, EAR_RECORD_MAX*25*4); */
    /* put_buf((u8*)vmdata_FR, EAR_RECORD_MAX*25*4); */
    g_printf("ret %d, %d\n", ret, sizeof(anc_adaptive_iir_t));
    return ret;
}

/* 读取VM参数 */
int audio_anc_adaptive_data_read(void)
{
    if (hdl == NULL) {
        return -1;
    }
    anc_adaptive_iir_t *iir = &hdl->adaptive_iir;
    int ret = syscfg_read(CFG_ANC_ADAPTIVE_DATA_ID, (u8 *)iir, sizeof(anc_adaptive_iir_t));
    if (ret == sizeof(anc_adaptive_iir_t)) {	//能读取到参数, 则默认进入自适应
        if (!CRC16((u8 *)iir, sizeof(anc_adaptive_iir_t))) {	//读到的数全为0
            r_printf("adaptive data is emtpy!!!\n");
            return -1;
        }
        g_printf("do adaptive, ret %d\n", ret);

    } else {
        return -1;
        //给予自适应默认参数
    }

    audio_anc_adaptive_poweron_catch_data(iir);	//开机需确认左右耳
    audio_anc_adaptive_data_format(iir);
    audio_anc_coeff_adaptive_set(ANC_COEFF_MODE_ADAPTIVE, 0);

#if ANC_EAR_RECORD_EN
    icsd_anc_vmdata_init(hdl->adaptive_iir.record_FL, hdl->adaptive_iir.record_FR, &hdl->adaptive_iir.record_num);
#endif/*ANC_EAR_RECORD_EN*/

    return 0;
}

/* 自适应默认参数更新 */
void audio_anc_ear_adaptive_param_init(audio_anc_t *param)
{
    param->gains.ff_1st_dcc = 8;
    param->gains.ff_2nd_dcc = 4;
    param->gains.fb_1st_dcc = 8;
    param->gains.fb_2nd_dcc = 1;
}

void audio_anc_adaptive_fr_fail_fill(u8 *dat, u8 order, u8 result)
{
    u8 fr_fail[13] = {0x2, 0x0,  0x0, 0xc8, 0x42, \
                      0x0,  0x0, 0x0,  0x0, \
                      0x0,  0x0, 0x80, 0x3f
                     };
    if (!result) {
        for (int i = 0; i < order; i++) {
            memcpy(dat + 4 + i * 13, fr_fail, 13);
        }
    }
}

extern void anc_packet_show(anc_packet_data_t *packet);
void audio_anc_adaptive_data_packet(struct icsd_anc_tool_data *TOOL_DATA)
{
    int len = TOOL_DATA->h_len;
    int fb_yorder = TOOL_DATA->yorderb;
    int ff_yorder = TOOL_DATA->yorderf;
    int cmp_yorder = TOOL_DATA->yorderc;

    int ff_dat_len =  sizeof(anc_fr_t) * ff_yorder + 4;
    int fb_dat_len =  sizeof(anc_fr_t) * fb_yorder + 4;
    int cmp_dat_len =  sizeof(anc_fr_t) * cmp_yorder + 4;
    anc_adaptive_iir_t *iir = &hdl->adaptive_iir;
    u8 *ff_dat, *fb_dat, *cmp_dat, *rff_dat, *rfb_dat, *rcmp_dat;
    u8 result = icsd_anc_train_result_get(TOOL_DATA);

    hdl->adaptive_iir.result = result;	//记录当前参数的保存结果;

#if ANC_CONFIG_LFF_EN
    ff_dat = (u8 *)&iir->lff_gain;
    audio_anc_fr_format(ff_dat, TOOL_DATA->data_out5, ff_yorder, ff_iir_type);
    audio_anc_adaptive_fr_fail_fill(ff_dat, ff_yorder, (result & ANC_ADAPTIVE_RESULT_LFF));
#endif/*ANC_CONFIG_LFF_EN*/
#if ANC_CONFIG_LFB_EN
    fb_dat = (u8 *)&iir->lfb_gain;
    audio_anc_fr_format(fb_dat, TOOL_DATA->data_out4, fb_yorder, fb_iir_type);
    audio_anc_adaptive_fr_fail_fill(fb_dat, fb_yorder, (result & ANC_ADAPTIVE_RESULT_LFB));
#if ANC_EAR_ADAPTIVE_CMP_EN
    cmp_dat = (u8 *)&iir->lcmp_gain;
    audio_anc_fr_format(cmp_dat, TOOL_DATA->data_out11, cmp_yorder, cmp_iir_type);
    audio_anc_adaptive_fr_fail_fill(cmp_dat, cmp_yorder, (result & ANC_ADAPTIVE_RESULT_LCMP));
#endif/*ANC_EAR_ADAPTIVE_CMP_EN*/
#endif/*ANC_CONFIG_LFB_EN*/
#if ANC_CONFIG_RFF_EN
    rff_dat = (u8 *)&iir->rff_gain;
    if (!ANC_CONFIG_LFF_EN) { 	//TWS 单R声道使用
        ff_dat = rff_dat;
        audio_anc_fr_format(ff_dat, TOOL_DATA->data_out5, ff_yorder, ff_iir_type);
        audio_anc_adaptive_fr_fail_fill(ff_dat, ff_yorder, (result & ANC_ADAPTIVE_RESULT_LFF));
    } else {
        audio_anc_fr_format(rff_dat, TOOL_DATA->data_out10, ff_yorder, ff_iir_type);
        audio_anc_adaptive_fr_fail_fill(rff_dat, ff_yorder, (result & ANC_ADAPTIVE_RESULT_RFF));
    }
#endif/*ANC_CONFIG_RFF_EN*/
#if ANC_CONFIG_RFB_EN
    rfb_dat = (u8 *)&iir->rfb_gain;
    if (!ANC_CONFIG_LFB_EN) {	//TWS 单R声道使用
        fb_dat = rfb_dat;
        audio_anc_fr_format(fb_dat, TOOL_DATA->data_out4, fb_yorder, fb_iir_type);
        audio_anc_adaptive_fr_fail_fill(fb_dat, fb_yorder, (result & ANC_ADAPTIVE_RESULT_LFB));
    } else {
        audio_anc_fr_format(rfb_dat, TOOL_DATA->data_out9, fb_yorder, fb_iir_type);
        audio_anc_adaptive_fr_fail_fill(rfb_dat, fb_yorder, (result & ANC_ADAPTIVE_RESULT_RFB));
    }
#if ANC_EAR_ADAPTIVE_CMP_EN
    rcmp_dat = (u8 *)&iir->rcmp_gain;
    if (!ANC_CONFIG_LFB_EN) {	//TWS 单R声道使用
        cmp_dat = rcmp_dat;
        audio_anc_fr_format(cmp_dat, TOOL_DATA->data_out11, cmp_yorder, cmp_iir_type);
        audio_anc_adaptive_fr_fail_fill(cmp_dat, cmp_yorder, (result & ANC_ADAPTIVE_RESULT_LCMP));
    } else {
        audio_anc_fr_format(rcmp_dat, TOOL_DATA->data_out12, cmp_yorder, cmp_iir_type);
        audio_anc_adaptive_fr_fail_fill(rcmp_dat, cmp_yorder, (result & ANC_ADAPTIVE_RESULT_RCMP));
    }
#endif/*ANC_EAR_ADAPTIVE_CMP_EN*/
#endif/*ANC_CONFIG_RFB_EN*/

    if (hdl->param->developer_mode) {
        printf("-- len = %d\n", len);
        printf("-- ff_yorder = %d\n", ff_yorder);
        printf("-- fb_yorder = %d\n", fb_yorder);
        printf("-- cmp_yorder = %d\n", cmp_yorder);
        /* 先统一申请空间，因为下面不知道什么情况下调用函数 anc_data_catch 时令参数 init_flag 为1 */
        anc_adaptive_data = anc_data_catch(anc_adaptive_data, NULL, 0, 0, 1);

#if (TCFG_AUDIO_ANC_CH == (ANC_L_CH | ANC_R_CH))	//头戴式
        r_printf("ANC ear-adaptive send data\n");
        anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)TOOL_DATA->h_freq, len * 4, ANC_R_ADAP_FRE, 0);
        anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)TOOL_DATA->data_out6, len * 8, ANC_R_ADAP_SZPZ, 0);
        anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)TOOL_DATA->data_out7, len * 8, ANC_R_ADAP_PZ, 0);
        anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)TOOL_DATA->data_out8, len * 8, ANC_R_ADAP_TARGET, 0);
        anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)TOOL_DATA->data_out14, len * 8, ANC_R_ADAP_TARGET_CMP, 0);
        anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)rff_dat, ff_dat_len, ANC_R_FF_IIR, 0);  //R_ff
        anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)rfb_dat, fb_dat_len, ANC_R_FB_IIR, 0);  //R_fb

        anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)TOOL_DATA->h_freq, len * 4, ANC_L_ADAP_FRE, 0);
        anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)TOOL_DATA->data_out1, len * 8, ANC_L_ADAP_SZPZ, 0);
        anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)TOOL_DATA->data_out2, len * 8, ANC_L_ADAP_PZ, 0);
        anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)TOOL_DATA->data_out3, len * 8, ANC_L_ADAP_TARGET, 0);
        anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)TOOL_DATA->data_out13, len * 8, ANC_L_ADAP_TARGET_CMP, 0);
        anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)ff_dat, ff_dat_len, ANC_L_FF_IIR, 0);  //L_ff
        anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)fb_dat, fb_dat_len, ANC_L_FB_IIR, 0);  //L_fb
#if ANC_EAR_ADAPTIVE_CMP_EN
        anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)cmp_dat, cmp_dat_len, ANC_L_CMP_IIR, 0);  //L_cmp
        anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)rcmp_dat, cmp_dat_len, ANC_R_CMP_IIR, 0);  //R_cmp
#endif/*ANC_EAR_ADAPTIVE_CMP_EN*/
#else  //入耳式
#if TCFG_USER_TWS_ENABLE
        extern char bt_tws_get_local_channel(void);
        if (bt_tws_get_local_channel() == 'R') {
            r_printf("ANC ear-adaptive send data, ch:R\n");
            anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)TOOL_DATA->h_freq, len * 4, ANC_R_ADAP_FRE, 0);
            anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)TOOL_DATA->data_out1, len * 8, ANC_R_ADAP_SZPZ, 0);
            anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)TOOL_DATA->data_out2, len * 8, ANC_R_ADAP_PZ, 0);
            anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)TOOL_DATA->data_out3, len * 8, ANC_R_ADAP_TARGET, 0);
            anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)TOOL_DATA->data_out13, len * 8, ANC_R_ADAP_TARGET_CMP, 0);
            anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)ff_dat, ff_dat_len, ANC_R_FF_IIR, 0);  //R_ff
            anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)fb_dat, fb_dat_len, ANC_R_FB_IIR, 0);  //R_fb
#if ANC_EAR_ADAPTIVE_CMP_EN
            anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)cmp_dat, cmp_dat_len, ANC_R_CMP_IIR, 0);  //R_cmp
#endif/*ANC_EAR_ADAPTIVE_CMP_EN*/
        } else
#endif/*TCFG_USER_TWS_ENABLE*/
        {
            r_printf("ANC ear-adaptive send data, ch:L\n");
            anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)TOOL_DATA->h_freq, len * 4, ANC_L_ADAP_FRE, 0);
            anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)TOOL_DATA->data_out1, len * 8, ANC_L_ADAP_SZPZ, 0);
            anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)TOOL_DATA->data_out2, len * 8, ANC_L_ADAP_PZ, 0);
            anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)TOOL_DATA->data_out3, len * 8, ANC_L_ADAP_TARGET, 0);
            anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)TOOL_DATA->data_out13, len * 8, ANC_L_ADAP_TARGET_CMP, 0);
            anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)ff_dat, ff_dat_len, ANC_L_FF_IIR, 0);  //L_ff
            anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)fb_dat, fb_dat_len, ANC_L_FB_IIR, 0);  //L_fb
#if ANC_EAR_ADAPTIVE_CMP_EN
            anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)cmp_dat, cmp_dat_len, ANC_L_CMP_IIR, 0);  //L_cmp
#endif/*ANC_EAR_ADAPTIVE_CMP_EN*/
        }
#endif  /* TCFG_AUDIO_ANC_CH == (ANC_L_CH | ANC_R_CH) */

    }

}

//开机自适应数据拼包，预备工具读取
void audio_anc_adaptive_poweron_catch_data(anc_adaptive_iir_t *iir)
{
    if (hdl->param->developer_mode) {
        int i;
        int ff_dat_len =  sizeof(anc_fr_t) * ANC_ADAPTIVE_FF_ORDER + 4;
        int fb_dat_len =  sizeof(anc_fr_t) * ANC_ADAPTIVE_FB_ORDER + 4;
        int cmp_dat_len =  sizeof(anc_fr_t) * ANC_ADAPTIVE_CMP_ORDER + 4;
        u8 *ff_dat, *fb_dat, *cmp_dat, *rff_dat, *rfb_dat, *rcmp_dat;
        audio_anc_t *param = hdl->param;
#if ANC_CONFIG_LFF_EN
        ff_dat = (u8 *)&iir->lff_gain;
#endif/*ANC_CONFIG_LFF_EN*/
#if ANC_CONFIG_LFB_EN
        fb_dat = (u8 *)&iir->lfb_gain;
#if ANC_EAR_ADAPTIVE_CMP_EN
        cmp_dat = (u8 *)&iir->lcmp_gain;
#endif/*ANC_EAR_ADAPTIVE_CMP_EN*/
#endif/*ANC_CONFIG_LFB_EN*/
#if ANC_CONFIG_RFF_EN
        rff_dat = (u8 *)&iir->rff_gain;
        if (ff_dat == NULL) { //TWS ANC_R_CH使用
            ff_dat = rff_dat;
        }
#endif/*ANC_CONFIG_RFF_EN*/
#if ANC_CONFIG_RFB_EN
        rfb_dat = (u8 *)&iir->rfb_gain;
        if (fb_dat == NULL) {	//TWS ANC_R_CH使用
            fb_dat = rfb_dat;
        }
#if ANC_EAR_ADAPTIVE_CMP_EN
        rcmp_dat = (u8 *)&iir->rcmp_gain;
        if (cmp_dat == NULL) {	//TWS ANC_R_CH使用
            cmp_dat = rcmp_dat;
        }
#endif/*ANC_EAR_ADAPTIVE_CMP_EN*/
#endif/*ANC_CONFIG_RFB_EN*/

        /* 先统一申请空间，因为下面不知道什么情况下调用函数 anc_data_catch 时令参数 init_flag 为1 */
        anc_adaptive_data = anc_data_catch(anc_adaptive_data, NULL, 0, 0, 1);
#if (TCFG_AUDIO_ANC_CH == (ANC_L_CH | ANC_R_CH))	//头戴式的耳机
        r_printf("ANC ear-adaptive send data\n");
        if (param->lff_en && param->rff_en) {
            if (iir->result & ANC_ADAPTIVE_RESULT_LFF) {
                anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)ff_dat, ff_dat_len, ANC_L_FF_IIR, 0);  //L_ff
            }
            if (iir->result & ANC_ADAPTIVE_RESULT_RFF) {
                anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)rff_dat, ff_dat_len, ANC_R_FF_IIR, 0);  //R_ff
            }
        }
        if (iir->result & ANC_ADAPTIVE_RESULT_LFB) {
            anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)fb_dat, fb_dat_len, ANC_L_FB_IIR, 0);  //L_fb
        }
        if (iir->result & ANC_ADAPTIVE_RESULT_RFB) {
            anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)rfb_dat, fb_dat_len, ANC_R_FB_IIR, 0);  //R_fb
        }

#if ANC_EAR_ADAPTIVE_CMP_EN
        if (iir->result & ANC_ADAPTIVE_RESULT_LCMP) {
            anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)cmp_dat, cmp_dat_len, ANC_L_CMP_IIR, 0);  //L_cmp
        }
        if (iir->result & ANC_ADAPTIVE_RESULT_RCMP) {
            anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)rcmp_dat, cmp_dat_len, ANC_R_CMP_IIR, 0);  //R_cmp
        }
#endif/*ANC_EAR_ADAPTIVE_CMP_EN*/
#else	//入耳式的
#if TCFG_USER_TWS_ENABLE
        extern char bt_tws_get_local_channel(void);
        if (bt_tws_get_local_channel() == 'R') {
            r_printf("ANC ear-adaptive send data, ch:R\n");
            if (param->lff_en || param->rff_en) {
                if (iir->result & ANC_ADAPTIVE_RESULT_RFF) {
                    anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)ff_dat, ff_dat_len, ANC_R_FF_IIR, 0);  //R_ff
                }
            }
            if (iir->result & ANC_ADAPTIVE_RESULT_RFB) {
                anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)fb_dat, fb_dat_len, ANC_R_FB_IIR, 0);  //R_fb
            }
#if ANC_EAR_ADAPTIVE_CMP_EN
            if (iir->result & ANC_ADAPTIVE_RESULT_RCMP) {
                anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)cmp_dat, cmp_dat_len, ANC_R_CMP_IIR, 0);  //R_cmp
            }
#endif/*ANC_EAR_ADAPTIVE_CMP_EN*/
        } else
#endif/*TCFG_USER_TWS_ENABLE*/
        {
            r_printf("ANC ear-adaptive send data, ch:L\n");
            if (param->lff_en || param->rff_en) {
                if (iir->result & ANC_ADAPTIVE_RESULT_LFF) {
                    anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)ff_dat, ff_dat_len, ANC_L_FF_IIR, 0);  //L_ff
                }
            }
            if (iir->result & ANC_ADAPTIVE_RESULT_LFB) {
                anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)fb_dat, fb_dat_len, ANC_L_FB_IIR, 0);  //L_fb
            }
#if ANC_EAR_ADAPTIVE_CMP_EN
            if (iir->result & ANC_ADAPTIVE_RESULT_LCMP) {
                anc_adaptive_data = anc_data_catch(anc_adaptive_data, (u8 *)cmp_dat, cmp_dat_len, ANC_L_CMP_IIR, 0);  //L_cmp
            }
#endif/*ANC_EAR_ADAPTIVE_CMP_EN*/
        }
#endif	/* TCFG_AUDIO_ANC_CH == (ANC_L_CH | ANC_R_CH) */
    }
}

//左右参数映射
void audio_anc_param_map(u8 coeff_en, u8 gain_en)
{
#if TCFG_USER_TWS_ENABLE && (TCFG_AUDIO_ANC_CH != (ANC_L_CH | ANC_R_CH))
    if (hdl->param->developer_mode) {	//开发者模式左右耳区分使用参数
        if (coeff_en && (!hdl->param->lff_coeff || !hdl->param->rff_coeff || \
                         !hdl->param->lfb_coeff || !hdl->param->rfb_coeff)) {
#if ANC_DEVELOPER_MODE_EN
            ASSERT(0, "(ERROR)ANC adaptive coeff map %x, %x, %x, %x, Maybe anc_coeff.bin is ERR!", \
                   (int)hdl->param->lff_coeff, (int)hdl->param->rff_coeff, \
                   (int)hdl->param->lfb_coeff, (int)hdl->param->rfb_coeff);
#else
            anc_log("(ERROR)ANC adaptive coeff map %x, %x, %x, %x, Maybe anc_coeff.bin is ERR!", \
                    (int)hdl->param->lff_coeff, (int)hdl->param->rff_coeff, \
                    (int)hdl->param->lfb_coeff, (int)hdl->param->rfb_coeff);
#endif/*ANC_DEVELOPER_MODE_EN*/
        }
        if ((TCFG_AUDIO_ANC_CH == ANC_L_CH) && (bt_tws_get_local_channel() == 'R')) {
            anc_log("ANC MAP TWS_RIGHT_CHANNEL\n");
            if (gain_en) {
                hdl->param->gains.l_ffgain = hdl->param->gains.r_ffgain;
                hdl->param->gains.l_fbgain = hdl->param->gains.r_fbgain;
                hdl->param->gains.l_transgain = hdl->param->gains.r_transgain;
                hdl->param->gains.l_cmpgain = hdl->param->gains.r_cmpgain;
                hdl->param->gains.l_ffmic_gain = hdl->param->gains.r_ffmic_gain;
                hdl->param->gains.l_fbmic_gain = hdl->param->gains.r_fbmic_gain;
                hdl->param->gains.gain_sign = (hdl->param->gains.gain_sign >> 4);
            }
            if (coeff_en) {
                hdl->param->lff_coeff = hdl->param->rff_coeff;
                hdl->param->lfb_coeff = hdl->param->rfb_coeff;
                hdl->param->ltrans_coeff = hdl->param->rtrans_coeff;
                hdl->param->lcmp_coeff = hdl->param->rcmp_coeff;

                hdl->param->lff_yorder = hdl->param->rff_yorder;
                hdl->param->lfb_yorder = hdl->param->rfb_yorder;
                hdl->param->ltrans_yorder = hdl->param->rtrans_yorder;
                hdl->param->lcmp_yorder = hdl->param->rcmp_yorder;
            }
        } else if ((TCFG_AUDIO_ANC_CH == ANC_R_CH) && (bt_tws_get_local_channel() == 'L')) {
            anc_log("ANC MAP TWS_LEFT_CHANNEL\n");
            if (gain_en) {
                hdl->param->gains.r_ffgain = hdl->param->gains.l_ffgain;
                hdl->param->gains.r_fbgain = hdl->param->gains.l_fbgain;
                hdl->param->gains.r_transgain = hdl->param->gains.l_transgain;
                hdl->param->gains.r_cmpgain = hdl->param->gains.l_cmpgain;
                hdl->param->gains.r_ffmic_gain = hdl->param->gains.l_ffmic_gain;
                hdl->param->gains.r_fbmic_gain = hdl->param->gains.l_fbmic_gain;
                hdl->param->gains.gain_sign = (hdl->param->gains.gain_sign << 4);
            }
            if (coeff_en) {
                hdl->param->rff_coeff = hdl->param->lff_coeff;
                hdl->param->rfb_coeff = hdl->param->lfb_coeff;
                hdl->param->rtrans_coeff = hdl->param->ltrans_coeff;
                hdl->param->rcmp_coeff = hdl->param->lcmp_coeff;

                hdl->param->rff_yorder = hdl->param->lff_yorder;
                hdl->param->rfb_yorder = hdl->param->lfb_yorder;
                hdl->param->rtrans_yorder = hdl->param->ltrans_yorder;
                hdl->param->rcmp_yorder = hdl->param->lcmp_yorder;
            }
        }
    }
#endif/*TCFG_USER_TWS_ENABLE*/

}

/*
   强制中断自适应
   param: default_flag		1 退出后恢复默认ANC效果； 0 退出后保持ANC_OFF(避免与下一个切模式流程冲突)
 */
void anc_ear_adaptive_forced_exit(u8 default_flag)
{
    if (hdl) {
        if (hdl->busy) {
            hdl->forced_exit_default_flag = default_flag;
            icsd_anc_forced_exit();
            tone_player_stop();
        }
    }
}

//ANC自适应训练失败处理回调函数
void audio_anc_adaptive_fail_callback(u8 anc_err)
{
#if (RCSP_ADV_EN && RCSP_ADV_ANC_VOICE && RCSP_ADV_ADAPTIVE_NOISE_REDUCTION)
    extern void set_adaptive_noise_reduction_reset_callback(u8 result);
    set_adaptive_noise_reduction_reset_callback(!anc_err);
#endif
    if (anc_err == 0) {
        /* anc_log("ANC_ADAPTIVE SUCCESS==========================\n"); */
    }
    if (anc_err & ANC_ERR_LR_BALANCE) {
        anc_log("ANC_ERR_LR_BALANCE==========================\n");
    }
    if (anc_err & ANC_ERR_SOUND_PRESSURE) {
        anc_log("ANC_ERR_SOUND_PRESSURE==========================\n");
    }
    if (anc_err & ANC_ERR_SHAKE) {
        anc_log("ANC_ERR_SHAKE==========================\n");
    }
}

/* 关机数据保存 */
void anc_ear_adaptive_power_off_save_data(void)
{
#if ANC_POWEOFF_SAVE_ADAPTIVE_DATA
    if (hdl->ear_adaptive_data_from == ANC_ADAPTIVE_DATA_FROM_ALOGM) {	//表示数据已经更新
        audio_anc_adaptive_data_save(&hdl->adaptive_iir);
    }
#endif/*ANC_POWEOFF_SAVE_ADAPTIVE_DATA*/
}


//------------------------------- 外部参数读写接口 ------------------------------
u8 anc_ear_adaptive_tws_sync_en_get(void)
{
    if (hdl) {
        return hdl->tws_sync;
    }
    return 0;
}

void anc_ear_adaptive_seq_set(u8 seq)
{
    if (hdl) {
        hdl->seq = seq;
    }
}

u8 anc_ear_adaptive_seq_get(void)
{
    if (hdl) {
        return hdl->seq;
    }
    return 0;
}

/* 判断当前是否处于训练中 */
u8 anc_ear_adaptive_busy_get(void)
{
    if (hdl) {
        return hdl->busy;
    }
    return 0;
}

void anc_ear_adaptive_mode_end(void)
{
    if (hdl) {
        hdl->busy = 0;
    }
}

//临时接口
/* void icsd_audio_adc_mic_open(u8 mic_idx, u8 gain, u16 sr, u8 mic_2_dac) */
/* { */

/* } */

#endif/*ANC_EAR_ADAPTIVE_EN*/
