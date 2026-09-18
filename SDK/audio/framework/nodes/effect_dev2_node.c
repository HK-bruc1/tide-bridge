#include "jlstream.h"
#include "media/audio_base.h"
#include "effects/effects_adj.h"
#include "app_config.h"
#include "effect_dev_node.h"
#include "effects_dev.h"
#include "audio_splicing.h"
#include "st_opus_enc/opus_stenc_api.h"
#include "meeting_mono.h"
#include "sl_mic_select.h"
#include "system/timer.h"

#if TCFG_EFFECT_DEV2_NODE_ENABLE

#define LOG_TAG_CONST EFFECTS
#define LOG_TAG     "[EFFECT_dev2-NODE]"
#define LOG_ERROR_ENABLE
#define LOG_INFO_ENABLE
#define LOG_DUMP_ENABLE
#include "debug.h"

#ifdef SUPPORT_MS_EXTENSIONS
#pragma   code_seg(".effect_dev2.text")
#pragma   data_seg(".effect_dev2.data")
#pragma  const_seg(".effect_dev2.text.const")
#endif

/* 音效算法处理帧长
 * 0   : 等长输入输出，输入数据算法需要全部处理完
 * 非0 : 按照帧长输入数据到算法处理接口
 */
#define EFFECT_DEV2_FRAME_POINTS  (FRAME_POINT)

/*
 *声道转换类型选配
 *支持立体声转4声道协商使能,须在第三方音效节点后接入声道拆分节点
 * */
#define CHANNEL_ADAPTER_AUTO   0 //自动协商,通常用于无声道数转换的场景,结果随数据流配置自动适配
#define CHANNEL_ADAPTER_2TO4   1 //立体声转4声道协商使能,支持2to4,结果随数据流配置自动适配
#define CHANNEL_ADAPTER_1TO2   2 //单声道转立体声协商使能,支持1to2,结果随数据流配置自动适配
#define CHANNEL_ADAPTER_TYPE   CHANNEL_ADAPTER_AUTO //默认无声道转换

struct effect_dev2_node_hdl {
    char name[16];
    void *user_priv;//用户可使用该变量做模块指针传递
    struct user_effect_tool_param cfg;//工具界面参数
    struct packet_ctrl dev;
    OPUS_STENC_OPS* opus_stenc;
    void *run_buf;
    s16 *pcm;
    struct meeting_mono_policy mono;
    u8 selector_owned;
    u32 selector_max_ms;
    u8 started;
    u8 failed;
    u32 encoded_frames;
    u32 encoded_bytes;
    u16 min_encoded_len;
    u16 max_encoded_len;
};

typedef char mic_select_config_abi_must_be_44[(sizeof(mic_select_config_t) == 44) ? 1 : -1];
static struct effect_dev2_node_hdl *selector_owner;

/* 自定义算法，初始化
 * hdl->dev.sample_rate:采样率
 * hdl->dev.in_ch_num:通道数，单声道 1，立体声 2, 四声道 4
 * hdl->dev.out_ch_num:通道数，单声道 1，立体声 2, 四声道 4
 * hdl->dev.bit_width:位宽 0，16bit  1，32bit
 **/
static int audio_effect_dev2_init(struct effect_dev2_node_hdl *hdl)
{
    OPUS_ENC_PARA opuset = {0};
    hdl->opus_stenc = get_opus_stenc_ops();
    if (!hdl->opus_stenc) {
        return -EINVAL;
    }
    opuset.sr = OPUS_SR;
    opuset.br = OPUS_SR * hdl->dev.out_ch_num * sizeof(s16) / OPUS_CR * 8;
    opuset.nch = hdl->dev.out_ch_num;
    opuset.format_mode = 0;
    opuset.complexity = 1;
    opuset.frame_ms = OPUS_FS;
    u32 needsz = hdl->opus_stenc->need_buf(&opuset);
    printf("[opus] sr=%d br=%d nch=%d frame_ms=%d need=%u mono_mic=%u\n",
           opuset.sr, opuset.br, opuset.nch, opuset.frame_ms, needsz, hdl->mono.mic);
    if (!needsz) {
        return -EINVAL;
    }
    hdl->run_buf = malloc(needsz);
    hdl->pcm = malloc(FRAME_SIZE * 2);
    if (!hdl->run_buf || !hdl->pcm) {
        return -ENOMEM;
    }
    if (hdl->opus_stenc->open(hdl->run_buf, NULL, &opuset)) {
        return -EINVAL;
    }
    if (hdl->mono.mic == 3) {
        OS_ENTER_CRITICAL();
        int busy = selector_owner != NULL;
        if (!busy) {
            selector_owner = hdl;
        }
        OS_EXIT_CRITICAL();
        if (busy) {
            return -EINVAL;
        }
        mic_select_config_t config;
        sl_mic_select_config_init(&config);
        config.sample_rate = OPUS_SR;
        config.num_mics = 2;
        config.analysis_samples = FRAME_POINT;
        int result = sl_mic_select_create(&config);
        if (result) {
            OS_ENTER_CRITICAL();
            selector_owner = NULL;
            OS_EXIT_CRITICAL();
            return -EINVAL;
        }
        hdl->selector_owned = 1;
        hdl->selector_max_ms = 0;
        printf("[mic select] ready rate=16000 mics=2 points=320 map=1:MIC0,2:MIC3\n");
    }
    return 0;
}

static void effect_dev2_fail(struct effect_dev2_node_hdl *hdl, int reason)
{
    if (!hdl->failed) {
        hdl->failed = 1;
        printf("[opus] frame failed: %d\n", reason);
        if (hdl->mono.fault) {
            hdl->mono.fault(hdl->mono.epoch, reason);
        }
    }
}

/* 自定义算法，运行
 * hdl->dev.sample_rate:采样率
 * hdl->dev.in_ch_num:通道数，单声道 1，立体声 2, 四声道 4
 * hdl->dev.out_ch_num:通道数，单声道 1，立体声 2, 四声道 4
 * hdl->dev.bit_width:位宽 0，16bit  1，32bit
 * *indata:输入数据地址
 * *outdata:输出数据地址
 * indata_len :输入数据长度,byte
 * */
static u32 audio_effect_dev2_run(struct effect_dev2_node_hdl *hdl, s16 *indata, s16 *outdata, u32 indata_len)
{
    int enc_len = 0;
    u32 expected = hdl->dev.in_ch_num >= 2 ? FRAME_SIZE * 2 : FRAME_SIZE;
    if (hdl->failed || !hdl->pcm || !hdl->run_buf) {
        return 0;
    }
    if ((hdl->mono.mic && indata_len != FRAME_SIZE * 2) ||
        indata_len < expected) {
        effect_dev2_fail(hdl, -1);
        return 0;
    }
    s16 *left = hdl->pcm;
    s16 *right = left + FRAME_POINT;
    const s16 *selected = indata;
    if (hdl->mono.mic == 2) {
        selected += FRAME_POINT;
    }
    if (hdl->mono.mic == 3) {
        int health = source_dev1_pair_health();
        if (health || !hdl->selector_owned) {
            effect_dev2_fail(hdl, health ? health : -4);
            return 0;
        }
        u32 began = sys_timer_get_ms();
        int count = sl_mic_select_process(indata, indata + FRAME_POINT,
                                         NULL, NULL, FRAME_POINT, left, NULL);
        u32 elapsed = sys_timer_get_ms() - began;
        if (elapsed > hdl->selector_max_ms) {
            hdl->selector_max_ms = elapsed;
        }
        if (count != FRAME_POINT) {
            effect_dev2_fail(hdl, -5);
            return 0;
        }
        if (hdl->encoded_frames % 50 == 0) {
            float rms[MS_MAX_MICS];
            int states[MS_MAX_MICS];
            sl_mic_select_get_channel_rms(rms);
            sl_mic_select_get_channel_state(states);
            printf("[mic select] frame=%u active=%d rms=%d/%d state=%d/%d max_ms=%u\n",
                   hdl->encoded_frames, sl_mic_select_get_active_channel(),
                   (int)rms[0], (int)rms[1], states[0], states[1], hdl->selector_max_ms);
        }
    } else {
        memcpy(left, selected, FRAME_SIZE);
    }
    if (hdl->dev.out_ch_num == 2) {
        memcpy(right, hdl->dev.in_ch_num >= 2 ? indata + FRAME_POINT : indata, FRAME_SIZE);
    }
    int ret = hdl->opus_stenc->run_wb(hdl->run_buf, left,
                                     hdl->dev.out_ch_num == 2 ? right : NULL,
                                     (u8 *)outdata, &enc_len);
    if (ret || enc_len <= 0 || enc_len > FRAME_SIZE * 2) {
        effect_dev2_fail(hdl, -2);
        return 0;
    }
    if (!hdl->encoded_frames || enc_len < hdl->min_encoded_len) {
        hdl->min_encoded_len = enc_len;
    }
    if (enc_len > hdl->max_encoded_len) {
        hdl->max_encoded_len = enc_len;
    }
    ++hdl->encoded_frames;
    hdl->encoded_bytes += enc_len;
    return enc_len;
}

static void audio_effect_dev2_exit(struct effect_dev2_node_hdl *hdl)
{
    if (hdl->selector_owned) {
        sl_mic_select_destroy();
        hdl->selector_owned = 0;
        OS_ENTER_CRITICAL();
        selector_owner = NULL;
        OS_EXIT_CRITICAL();
    }
    if (hdl->run_buf) {
        free(hdl->run_buf);
        hdl->run_buf = NULL;
    }
    if (hdl->pcm) {
        free(hdl->pcm);
        hdl->pcm = NULL;
    }
}

/* 自定义算法，更新参数
 **/
static void audio_effect_dev2_update(struct effect_dev2_node_hdl *hdl)
{
    //打印在线调音发送下来的参数
    printf("effect dev2 name : %s \n", hdl->name);
    for (int i = 0 ; i < 8; i++) {
        printf("cfg.int_param[%d] %d\n", i, hdl->cfg.int_param[i]);
    }
    for (int i = 0 ; i < 8; i++) {
        printf("cfg.float_param[%d] %d.%02d\n", i, (int)hdl->cfg.float_param[i], debug_digital(hdl->cfg.float_param[i]));
    }
    //do something

}
/*节点输出回调处理，可处理数据或post信号量*/
static void effect_dev2_handle_frame(struct stream_iport *iport, struct stream_note *note)
{

    struct effect_dev2_node_hdl *hdl = (struct effect_dev2_node_hdl *)iport->node->private_data;


    if (hdl->mono.mic) {
        struct stream_frame *frame;
        while ((frame = jlstream_pull_frame(iport, note)) != NULL) {
            if (!hdl->started || hdl->failed) {
                jlstream_free_frame(frame);
                continue;
            }
            /* One Source_Dev1 packet is one complete planar PCM pair.
             * Allocate for the codec's output, not a PCM channel-ratio stride. */
            struct stream_frame *out = jlstream_get_frame(iport->node->oport, FRAME_SIZE * 2);
            if (!out) {
                effect_dev2_fail(hdl, -3);
                jlstream_free_frame(frame);
                continue;
            }
            out->len = audio_effect_dev2_run(hdl, (s16 *)frame->data,
                                             (s16 *)out->data, frame->len);
            jlstream_free_frame(frame);
            if (out->len) {
                jlstream_push_frame(iport->node->oport, out);
            } else {
                jlstream_free_frame(out);
            }
        }
        return;
    }
    effect_dev_process(&hdl->dev, iport,  note); //音效处理
}

/*节点预处理-在ioctl之前*/
static int effect_dev2_adapter_bind(struct stream_node *node, u16 uuid)
{
    struct effect_dev2_node_hdl *hdl = (struct effect_dev2_node_hdl *)node->private_data;

    memset(hdl, 0, sizeof(*hdl));
    return 0;
}

/*打开改节点输入接口*/
static void effect_dev2_ioc_open_iport(struct stream_iport *iport)
{
    iport->handle_frame = effect_dev2_handle_frame;				//注册输出回调
}

/*节点参数协商*/
static int effect_dev2_ioc_negotiate(struct stream_iport *iport)
{
    int ret = 0;
    ret = NEGO_STA_ACCPTED;
    struct stream_oport *oport = iport->node->oport;
    struct stream_fmt *in_fmt = &iport->prev->fmt;
    struct effect_dev2_node_hdl *hdl = (struct effect_dev2_node_hdl *)iport->node->private_data;

    if (hdl->mono.mic) {
        if (in_fmt->channel_mode != AUDIO_CH_LR) {
            in_fmt->channel_mode = AUDIO_CH_LR;
            ret = NEGO_STA_CONTINUE;
        }
        oport->fmt.channel_mode = AUDIO_CH_MIX;
        hdl->dev.in_ch_num = 2;
        hdl->dev.out_ch_num = 1;
        return ret;
    }

    if (oport->fmt.channel_mode == 0xff) {
        return 0;
    }

    hdl->dev.out_ch_num = AUDIO_CH_NUM(oport->fmt.channel_mode);
    hdl->dev.in_ch_num = AUDIO_CH_NUM(in_fmt->channel_mode);
#if (CHANNEL_ADAPTER_TYPE == CHANNEL_ADAPTER_2TO4)
    if (hdl->dev.out_ch_num == 4) {
        if (hdl->dev.in_ch_num != 2) {
            in_fmt->channel_mode = AUDIO_CH_LR;
            ret = NEGO_STA_CONTINUE;
        }
    }
#elif (CHANNEL_ADAPTER_TYPE == CHANNEL_ADAPTER_1TO2)
    if (hdl->dev.out_ch_num == 2) {
        if (hdl->dev.in_ch_num != 1) {
            in_fmt->channel_mode = AUDIO_CH_MIX;
            ret = NEGO_STA_CONTINUE;
        }
    }
#endif
    printf(" effecs_dev2 in_ch_num %d, out_ch_num %d\n", hdl->dev.in_ch_num, hdl->dev.out_ch_num);


    return ret;
}

/*节点start函数*/
static int effect_dev2_ioc_start(struct effect_dev2_node_hdl *hdl)
{
    if (hdl->started) {
        return 0;
    }
    struct stream_fmt *fmt = &hdl_node(hdl)->oport->fmt;
    /* struct jlstream *stream = jlstream_for_node(hdl_node(hdl)); */


    hdl->dev.sample_rate = fmt->sample_rate;


    /*
     *获取配置文件内的参数,及名字
     * */
    int len = jlstream_read_node_data_new(hdl_node(hdl)->uuid, hdl_node(hdl)->subid, (void *)&hdl->cfg, hdl->name);
    if (!len) {
        log_error("%s, read node data err\n", __FUNCTION__);
        return -EINVAL;
    }

    /*
     *获取在线调试的临时参数
     * */
    if (config_audio_cfg_online_enable) {
        if (jlstream_read_effects_online_param(hdl_node(hdl)->uuid, hdl->name, &hdl->cfg, sizeof(hdl->cfg))) {
            log_debug("get effect dev2 online param\n");
        }
    }
    printf("effect dev2 name : %s \n", hdl->name);
    for (int i = 0 ; i < 8; i++) {
        printf("cfg.int_param[%d] %d\n", i, hdl->cfg.int_param[i]);
    }
    for (int i = 0 ; i < 8; i++) {
        printf("cfg.float_param[%d] %d.%02d\n", i, (int)hdl->cfg.float_param[i], debug_digital(hdl->cfg.float_param[i]));
    }
    hdl->dev.bit_width = hdl_node(hdl)->iport->prev->fmt.bit_wide;
    hdl->dev.qval = hdl_node(hdl)->iport->prev->fmt.Qval;
    printf("effect_dev2_ioc_start, sr: %d, in_ch: %d, out_ch: %d, bitw: %d, %d", hdl->dev.sample_rate, hdl->dev.in_ch_num, hdl->dev.out_ch_num, hdl->dev.bit_width, hdl->dev.qval);

    hdl->dev.node_hdl = hdl;
    hdl->dev.effect_run = (u32 (*)(void *, s16 *, s16 *, u32))audio_effect_dev2_run;
    if (hdl->mono.mic && (hdl->dev.sample_rate != OPUS_SR || hdl->dev.bit_width ||
                          hdl_node(hdl)->iport->prev->fmt.sample_rate != OPUS_SR ||
                          hdl->dev.in_ch_num != 2 || hdl->dev.out_ch_num != 1)) {
        return -EINVAL;
    }
    int err = audio_effect_dev2_init(hdl);
    if (err) {
        audio_effect_dev2_exit(hdl);
        return err;
    }
    if (!hdl->mono.mic) {
        effect_dev_init(&hdl->dev, EFFECT_DEV2_FRAME_POINTS);
        if (!hdl->dev.remain_buf) {
            audio_effect_dev2_exit(hdl);
            return -ENOMEM;
        }
    }
    hdl->failed = 0;
    hdl->encoded_frames = hdl->encoded_bytes = 0;
    hdl->min_encoded_len = hdl->max_encoded_len = 0;
    hdl->started = 1;
    return 0;
}


/*节点stop函数*/
static void effect_dev2_ioc_stop(struct effect_dev2_node_hdl *hdl)
{
    if (hdl->started && hdl->mono.mic) {
        printf("[meeting mono] mic=%u frames=%u bytes=%u encLen=%u..%u failed=%u\n",
               hdl->mono.mic, hdl->encoded_frames, hdl->encoded_bytes,
               hdl->min_encoded_len, hdl->max_encoded_len, hdl->failed);
    }
    hdl->started = 0;
    audio_effect_dev2_exit(hdl);
    effect_dev_close(&hdl->dev);
}

static int effect_dev2_ioc_update_parm(struct effect_dev2_node_hdl *hdl, int parm)
{
    int ret = false;
    return ret;
}
static int get_effect_dev2_ioc_parm(struct effect_dev2_node_hdl *hdl, int parm)
{
    int ret = 0;
    return ret;
}

static int effect_ioc_update_parm(struct effect_dev2_node_hdl *hdl, int parm)
{
    int ret = false;
    struct user_effect_tool_param *cfg = (struct user_effect_tool_param *)parm;
    if (hdl) {
        memcpy(&hdl->cfg, cfg, sizeof(struct user_effect_tool_param));

        audio_effect_dev2_update(hdl);

        ret = true;
    }

    return ret;
}
/*节点ioctl函数*/
static int effect_dev2_adapter_ioctl(struct stream_iport *iport, int cmd, int arg)
{
    int ret = 0;
    struct effect_dev2_node_hdl *hdl = (struct effect_dev2_node_hdl *)iport->node->private_data;

    switch (cmd) {
    case NODE_IOC_OPEN_IPORT:
        effect_dev2_ioc_open_iport(iport);
        break;
    case NODE_IOC_OPEN_OPORT:
        break;
    case NODE_IOC_CLOSE_IPORT:
        break;
    case NODE_IOC_SET_SCENE:
        break;
    case NODE_IOC_NEGOTIATE:
        *(int *)arg |= effect_dev2_ioc_negotiate(iport);
        break;
    case NODE_IOC_SET_PRIV_FMT:
        if (hdl->started || !arg) {
            return -EINVAL;
        }
        struct meeting_mono_policy *policy = (struct meeting_mono_policy *)arg;
        if (policy->mic > 3 || (policy->mic && (!policy->epoch || !policy->fault))) {
            return -EINVAL;
        }
        hdl->mono = *policy;
        break;
    case NODE_IOC_START:
        return effect_dev2_ioc_start(hdl);
    case NODE_IOC_SUSPEND:
    case NODE_IOC_STOP:
        effect_dev2_ioc_stop(hdl);
        break;
    case NODE_IOC_NAME_MATCH:
        if (!strcmp((const char *)arg, hdl->name)) {
            ret = 1;
        }
        break;

    case NODE_IOC_SET_PARAM:
        ret = effect_ioc_update_parm(hdl, arg);
        break;
    }

    return ret;
}

/*节点用完释放函数*/
static void effect_dev2_adapter_release(struct stream_node *node)
{
    effect_dev2_ioc_stop((struct effect_dev2_node_hdl *)node->private_data);
}

/*节点adapter 注意需要在sdk_used_list声明，否则会被优化*/
REGISTER_STREAM_NODE_ADAPTER(effect_dev2_node_adapter) = {
    .name       = "effect_dev2",
    .uuid       = NODE_UUID_EFFECT_DEV2,
    .bind       = effect_dev2_adapter_bind,
    .ioctl      = effect_dev2_adapter_ioctl,
    .release    = effect_dev2_adapter_release,
    .hdl_size   = sizeof(struct effect_dev2_node_hdl),
#if (CHANNEL_ADAPTER_TYPE != CHANNEL_ADAPTER_AUTO) || TCFG_T2620_MEETING_MONO_DEBUG_MIC
    .ability_channel_out = 0x80 | 1 | 2,
    .ability_channel_convert = 1,
#endif

};

REGISTER_ONLINE_ADJUST_TARGET(effect_dev2) = {
    .uuid = NODE_UUID_EFFECT_DEV2,
};

#endif
