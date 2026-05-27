#include "sdk_config.h"
#include "jlstream.h"
#include "encoder_node.h"
#include "st_opus_enc/opus_stenc_api.h"
#include "clock_manager/clock_manager.h"

 
#include "rdx_app_config.h"


#include "audio_dac.h"
extern bool esco_player_runing();

struct translation_ear_recoder {
    struct jlstream *stream;
};

static struct translation_ear_recoder *g_translation_ear_recoder[2] = {NULL,NULL};//一共需要两条流，一条[0]用于mic采样，一条[1]用于收集数据和编码


static void translation_ear_recoder_callback(void *private_data, int event)
{
    struct translation_ear_recoder *recoder = (struct translation_ear_recoder *)private_data;
    struct jlstream *stream = recoder->stream;

    switch (event) {
    case STREAM_EVENT_START:
        break;
    }
}

static u8 global_ch_mode = AUDIO_CH_LR;//AUDIO_CH_L：mic；AUDIO_CH_R：dac；AUDIO_CH_LR：mic+dac

u8 get_global_ch_mode(void)
{
    return global_ch_mode;
}

/**
 * @brief 设置需要编码的声音
 * 
 * @param mode AUDIO_CH_L：mic；AUDIO_CH_R：dac；AUDIO_CH_LR：mic+dac
 */
void set_global_ch_mode(u8 mode)
{
    global_ch_mode = mode;
}
/**
 * @brief 启动数据上传
 * 
 * @param enc_type 启动的场景，见stream_type
 * @param source_uuid 上传的数据源，根据需要选择，上传mic选NODE_UUID_ADC，上传dac选NODE_UUID_SOURCE_DEV1
 * @param code_type 编码格式，见audio_base.h
 * @param ai_type 固定为0
 * @return int 
 */
int translation_ear_recoder_open(stream_type enc_type, u16 source_uuid, u32 code_type, u8 ai_type)
{
    int err = 0;
    struct stream_fmt fmt;
    struct encoder_fmt enc_fmt;
    struct translation_ear_recoder *recoder;

    u8 idx = 0;//一共需要两条流，一条[0]用于mic采样，一条[1]用于收集数据和编码
    if ((enc_type & 0x0f) == MIC) {
        idx = 0;
    }
    else {
        idx = 1;
    }

    if (g_translation_ear_recoder[idx]) {
        return -EBUSY;
    }

    u16 uuid = 0;//jlstream_event_notify(STREAM_EVENT_GET_PIPELINE_UUID, (int)pipeline_name);
    if ((enc_type & 0x0f) == MIC && esco_player_runing()) {
        return -EFAULT;//在蓝牙通话场景下不用开mic
    }
    if (AUDIO_CH_NUM(enc_type) == 2){
        uuid = PIPELINE_UUID_TRANSLATION;//在翻译耳机场景下启动流程
    }
    if (AUDIO_CH_NUM(enc_type) == 1){
        uuid = PIPELINE_UUID_TRANSLATION_MONO;//在翻译耳机单声道场景下启动流程
    }
    if (uuid == 0) {
        return -EFAULT;
    }

    recoder = malloc(sizeof(*recoder));
    if (!recoder) {
        return -ENOMEM;
    }

    recoder->stream = jlstream_pipeline_parse(uuid, source_uuid);
    if (!recoder->stream) {
        err = -ENOMEM;
        goto __exit0;
    }
    switch (code_type) {
    case AUDIO_CODING_OPUS:
        //1. quality:bitrate     0:16kbps    1:32kbps    2:64kbps
        //   quality: MSB_2:(bit7_bit6)     format_mode    //0:百度_无头.                   1:酷狗_eng+range.
        //   quality:LMSB_2:(bit5_bit4)     low_complexity //0:高复杂度,高质量.兼容之前库.  1:低复杂度,低质量.
        //2. sample_rate         sample_rate=16k         ignore
        enc_fmt.bit_rate = 16000;//16000,320000,640000 这三个码率分别对应非ogg解码库的 OPUS_SRINDEX 值为0,1,2
        enc_fmt.quality = 1 | ai_type/*| LOW_COMPLEX*/;
        enc_fmt.sample_rate = 16000;
        fmt.sample_rate = 16000;
        fmt.coding_type = AUDIO_CODING_OPUS;
        break;
    case AUDIO_CODING_SPEEX:
        enc_fmt.quality = 5;
        enc_fmt.complexity = 2;
        fmt.sample_rate = 16000;
        fmt.coding_type = AUDIO_CODING_SPEEX;
        break;
    default:
        printf("do not support this type !!!\n");
        err = -ENOMEM;
        goto __exit1;
        break;
    }

    if(idx == 1){
        if(uuid == PIPELINE_UUID_TRANSLATION_MONO){
            //单声道模式要用sdk的编码器节点，需要配置参数
            err = jlstream_node_ioctl(recoder->stream, NODE_UUID_ENCODER, NODE_IOC_SET_PRIV_FMT, (int)(&enc_fmt));
        }
        fmt.channel_mode = global_ch_mode;
        err += jlstream_node_ioctl(recoder->stream, NODE_UUID_SINK_DEV1, NODE_IOC_SET_FMT, (int)(&fmt));
        if (err && err != -ENOENT) {
            goto __exit2;
        }
    }
    else{
        // err = jlstream_node_ioctl(recoder->stream, NODE_UUID_ENCODER, NODE_IOC_SET_PRIV_FMT, (int)(&enc_fmt));
        err += jlstream_node_ioctl(recoder->stream, NODE_UUID_SINK_DEV0, NODE_IOC_SET_FMT, (int)(&fmt));
        //设置ADC的中断点数
        err += jlstream_node_ioctl(recoder->stream, NODE_UUID_SOURCE, NODE_IOC_SET_PRIV_FMT, 256);
        if (err && err != -ENOENT) {
            goto __exit2;
        }
        extern struct audio_dac_hdl dac_hdl;
        u32 ref_sr = audio_dac_get_sample_rate(&dac_hdl);
        if(audio_dac_is_working(&dac_hdl) == 0){
            ref_sr = TCFG_AUDIO_GLOBAL_SAMPLE_RATE ? TCFG_AUDIO_GLOBAL_SAMPLE_RATE : 16000;
        }
        u16 get_cvp_node_uuid();
        u16 node_uuid = get_cvp_node_uuid();
        //根据回音消除的类型，将配置传递到对应的节点
        if (node_uuid) {
            err = jlstream_node_ioctl(recoder->stream, node_uuid, NODE_IOC_SET_FMT, (int)ref_sr);
        }
        if (node_uuid) {
            err = jlstream_node_ioctl(recoder->stream, node_uuid, NODE_IOC_SET_PRIV_FMT, source_uuid);
        }
        if (err && (err != -ENOENT)) {	//兼容没有cvp节点的情况
            goto __exit2;
        }
    }

    jlstream_set_callback(recoder->stream, recoder, translation_ear_recoder_callback);
    jlstream_set_scene(recoder->stream, STREAM_SCENE_AI_VOICE);
    err = jlstream_start(recoder->stream);
    if (err) {
        goto __exit1;
    }

    g_translation_ear_recoder[idx] = recoder;

    return 0;

__exit2:
    printf("ext2 %d\n", err);
__exit1:
    printf("ext1\n");
    jlstream_release(recoder->stream);
__exit0:
    printf("ext0\n");
    free(recoder);
    return err;
}

/**
 * @brief 关闭上传流程
 * 
 * @param type MIC or DAC
 */
void translation_ear_recoder_close(u8 type)
{
    u8 idx = 0;
    if (type == MIC) {
        idx = 0;
    }
    else {
        idx = 1;
    }
    struct translation_ear_recoder *recoder = g_translation_ear_recoder[idx];

    if (!recoder) {
        return;
    }
    putchar('O');
    jlstream_stop(recoder->stream, 0);
    putchar('G');
    jlstream_release(recoder->stream);

    free(recoder);
    g_translation_ear_recoder[idx] = NULL;

    jlstream_event_notify(STREAM_EVENT_CLOSE_RECODER, (int)"translation_ear");
}

void translation_ear_recoder_close_all(void)
{
    putchar('1');
    translation_ear_recoder_close(MIC);
    putchar('2');
    translation_ear_recoder_close(DAC);
    putchar('3');
    clock_free("translation_ear");
}

/**
 * @brief 打开opus编码
 * 
 * @param ch_mode 见stream_type
 * @return int 0：成功；其他：失败
 */
int translation_ear_recoder_open_all(u8 ch_mode)
{
    translation_ear_recoder_close_all();
    int ret = 0;
    clock_alloc("translation_ear", 24 * 1000000UL);
    u8 ch = AUDIO_CH_LR;

    switch (ch_mode)
    {
    case MIC_TO_MONO_OPUS:
	#if (RDX_SUPPORT_ALGORITHM == 1)
        ch = AUDIO_CH_LR;
	#else
		ch = AUDIO_CH_L;
	#endif
        break;
    case DAC_TO_MONO_OPUS:
        ch = AUDIO_CH_R;
        break;
    case MIC_TO_STERO_OPUS:
        ch = AUDIO_CH_L;
        break;
    case DAC_TO_STERO_OPUS:
        ch = AUDIO_CH_R;
        break;
    case MIC_DAC_TO_STERO_OPUS:
        ch = AUDIO_CH_LR;
        break;
    
    default:
        return -1;
    }
    set_global_ch_mode(ch);//先设置声道
    // if(ch_mode & MIC){
        ret += translation_ear_recoder_open(MIC|(AUDIO_CH_NUM(ch_mode) << 4), NODE_UUID_ADC, AUDIO_CODING_OPUS, 0);
    // }
    ret += translation_ear_recoder_open(DAC|(AUDIO_CH_NUM(ch_mode) << 4), NODE_UUID_SOURCE_DEV1, AUDIO_CODING_OPUS, 0);

    return ret;
}

#include "system/includes.h"
static u8 ai_idle_query(void)
{
    return !g_translation_ear_recoder[0] && !g_translation_ear_recoder[1];
}
REGISTER_LP_TARGET(ai_target) = {
    .name = "ai",
    .is_idle = ai_idle_query,
};


