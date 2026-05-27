#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".ai_voice_recoder.data.bss")
#pragma data_seg(".ai_voice_recoder.data")
#pragma const_seg(".ai_voice_recoder.text.const")
#pragma code_seg(".ai_voice_recoder.text")
#endif
#include "jlstream.h"
#include "ai_voice_recoder.h"
#include "encoder_node.h"

struct ai_voice_recoder {
    struct jlstream *stream;
};

static struct ai_voice_recoder *g_ai_voice_recoder = NULL;


static void ai_voice_recoder_callback(void *private_data, int event)
{
    struct ai_voice_recoder *recoder = g_ai_voice_recoder;
    struct jlstream *stream = (struct jlstream *)private_data;

    switch (event) {
    case STREAM_EVENT_START:
        break;
    }
}

int ai_voice_recoder_open(u32 code_type, u8 ai_type)
{
    int err;
    struct stream_fmt fmt;
    struct encoder_fmt enc_fmt;
    struct ai_voice_recoder *recoder;

    if (g_ai_voice_recoder) {
        return -EBUSY;
    }
    u16 uuid = jlstream_event_notify(STREAM_EVENT_GET_PIPELINE_UUID, (int)"ai_voice");
    if (uuid == 0) {
        return -EFAULT;
    }

    recoder = malloc(sizeof(*recoder));
    if (!recoder) {
        return -ENOMEM;
    }

    recoder->stream = jlstream_pipeline_parse(uuid, NODE_UUID_ADC);
    if (!recoder->stream) {
        err = -ENOMEM;
        goto __exit0;
    }
    switch (code_type) {
    case AUDIO_CODING_OPUS:
        //1. quality:bitrate     16000~80000.
        //   quality:format_mode    //0:百度_无头.                   1:酷狗_eng+range. 2:ogg封装,pc软件可播放.  3:size+rangeFinal. 源码可兼容版本.
        //   quality:complexity //0.1.2.3.    3质量最好.速度要求最高.
        //   quality:frame_ms //20|40|60|80|100ms.
        //2. sample_rate         sample_rate=16k         ignore
        enc_fmt.bit_rate = 32000;//16000,320000,640000 这三个码率分别对应非ogg解码库的 OPUS_SRINDEX 值为0,1,2
        enc_fmt.complexity = 0 | ai_type/*| LOW_COMPLEX*/;
        enc_fmt.sample_rate = 16000;
        enc_fmt.format =  0; //0:百度_无头.                   1:酷狗_eng+range. 2:ogg封装,pc软件可播放.  3:size+rangeFinal. 源码可兼容版本.
        /* enc_fmt.frame_len = 20 * 10;//与工具保持一致，要乘以10,表示20ms */
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
    err = jlstream_node_ioctl(recoder->stream, NODE_UUID_ENCODER, NODE_IOC_SET_PRIV_FMT, (int)(&enc_fmt));
    err += jlstream_node_ioctl(recoder->stream, NODE_UUID_AI_TX, NODE_IOC_SET_FMT, (int)(&fmt));

    //设置ADC的中断点数
    err += jlstream_node_ioctl(recoder->stream, NODE_UUID_SOURCE, NODE_IOC_SET_PRIV_FMT, 320);
    if (err) {
        goto __exit1;
    }
    jlstream_set_callback(recoder->stream, recoder->stream, ai_voice_recoder_callback);
    jlstream_set_scene(recoder->stream, STREAM_SCENE_AI_VOICE);
    err = jlstream_start(recoder->stream);
    if (err) {
        goto __exit1;
    }

    g_ai_voice_recoder = recoder;

    return 0;

__exit1:
    jlstream_release(recoder->stream);
__exit0:
    free(recoder);
    return err;
}

void ai_voice_recoder_close()
{
    struct ai_voice_recoder *recoder = g_ai_voice_recoder;

    if (!recoder) {
        return;
    }
    jlstream_stop(recoder->stream, 0);
    jlstream_release(recoder->stream);

    free(recoder);
    g_ai_voice_recoder = NULL;

    jlstream_event_notify(STREAM_EVENT_CLOSE_RECODER, (int)"ai_voice");
}



