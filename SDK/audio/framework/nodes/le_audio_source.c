/*************************************************************************************************/
/*!
*  \file      le_audio_source_node.c
*
*  \brief
*
*  Copyright (c) 2011-2023 ZhuHai Jieli Technology Co.,Ltd.
*
*/
/*************************************************************************************************/
#include "jlstream.h"
#include "sync/audio_syncts.h"
#include "le_audio_stream.h"
#include "app_config.h"

#if LE_AUDIO_STREAM_ENABLE

#define LE_AUDIO_TX_SOURCE      0
#define LE_AUDIO_LOCAL_SOURCE   1

void dev_list_add(void *_dev, void *hdl, void *ch, int (*get_delay)(void *hdl, void *ch));
void dev_list_del(void *_dev);
static int get_le_audio_source_buffer_delay_time(void *iport, void *ch);

struct master_dev {
    struct list_head entry;
};

struct le_audio_source_syncts {
    u8 start;
    void *syncts;
    u32 timestamp;
    u32 buffered_time;
    struct list_head entry;
};

struct le_audio_source_context {
    u8 diff_coding_type;
    void *tx_stream;
    void *rx_stream;
    void *le_audio;
    int sample_rate;
    u32 coding_type;
    struct list_head syncts_list;
    spinlock_t lock;
    u32 le_audio_start_usec;
    u32 local_start_usec;
    struct master_dev dev;
};

struct le_audio_source_iport {
    u8 attribute;
    u8 channel_mode;
    u32 coding_type;
    int sample_rate;
    u32 bit_rate;
    u16 frame_dms;
    u16 frame_len;
    u8 bit_width;

    struct stream_frame *frame;
};

static int le_audio_source_bind(struct stream_node *node, u16 uuid)
{
    struct le_audio_source_context *ctx = (struct le_audio_source_context *)node->private_data;

    if (!ctx) {
        return -ENOMEM;
    }

    spin_lock_init(&ctx->lock);
    INIT_LIST_HEAD(&ctx->syncts_list);
    return 0;
}

static int le_audio_source_frame_timestamp_handler(struct le_audio_source_context *ctx, struct stream_frame *frame)
{
    if (!(frame->flags & FRAME_FLAG_TIMESTAMP_ENABLE)) {
        return 0;
    }

    struct le_audio_source_syncts *node;
    int time_diff = 0;
    spin_lock(&ctx->lock);
    list_for_each_entry(node, &ctx->syncts_list, entry) {
        if (node->start) {
            continue;
        }
        time_diff = frame->timestamp - node->timestamp;
        if (time_diff >= 0) {
            sound_pcm_syncts_latch_trigger(node->syncts);
            printf("--le audio tx syncts start : %u, %u--\n", frame->timestamp, node->timestamp);
            node->start = 1;
        }
    }
    spin_unlock(&ctx->lock);

    return 0;
}

static void le_audio_source_handle_frame(struct stream_iport *iport, struct stream_note *note)
{
    struct le_audio_source_iport *hdl = (struct le_audio_source_iport *)iport->private_data;
    struct le_audio_source_context *ctx = (struct le_audio_source_context *)iport->node->private_data;
    struct stream_frame *frame;

    while (1) {
        frame = hdl->frame;
        if (!frame) {
            frame = jlstream_pull_frame(iport, note);
            if (!frame) {
                break;
            }
            hdl->frame = frame;
        }

        if (hdl->attribute == LE_AUDIO_TX_SOURCE) {
            le_audio_source_frame_timestamp_handler(ctx, frame);
            int wlen = le_audio_stream_tx_write(ctx->tx_stream, frame->data, frame->len);
            if (wlen < frame->len) {
                break;
            }
        } else {
            le_audio_source_frame_timestamp_handler(ctx, frame);
            le_audio_stream_rx_write(ctx->rx_stream, frame->data, frame->len);
        }

        jlstream_free_frame(frame);
        hdl->frame = NULL;
    }
}

extern uint32_t bb_le_clk_get_time_us(void);
static u32 le_audio_usec_to_local_usec(struct le_audio_source_context *ctx, u32 usec)
{
    u32 local_usec;

    int time_diff = (usec - ctx->le_audio_start_usec) & 0xfffffff;

    local_usec = ctx->local_start_usec + time_diff;

    ctx->le_audio_start_usec = usec;
    ctx->local_start_usec = local_usec;

    return local_usec;
}

static void le_audio_to_local_usec_init(struct le_audio_source_context *ctx)
{
    spin_lock(&ctx->lock);
    ctx->le_audio_start_usec = bb_le_clk_get_time_us();
    ctx->local_start_usec = audio_jiffies_usec();
    spin_unlock(&ctx->lock);
}

static int le_audio_tx_tick_handler(void *priv, int period, u32 send_timestamp)
{
    struct le_audio_source_context *ctx = (struct le_audio_source_context *)priv;

    int pcm_frames = period * ctx->sample_rate / 1000000;
    u32 buffered_time = 0;
    u32 buffered_frames = 0;
    u32 local_time_usec = 0;

    struct le_audio_source_syncts *node;
    spin_lock(&ctx->lock);
    local_time_usec = le_audio_usec_to_local_usec(ctx, send_timestamp);
    list_for_each_entry(node, &ctx->syncts_list, entry) {
        if (!node->start) {
            continue;
        }
        sound_pcm_update_frame_num_and_time(node->syncts, pcm_frames, local_time_usec, pcm_frames);
    }
    spin_unlock(&ctx->lock);

    /*printf("out : %d, %u, %lu\n", pcm_frames, local_time_usec, audio_jiffies_usec()); */
    return 0;
}

static void le_audio_source_open_iport(struct stream_iport *iport)
{
    struct le_audio_source_context *ctx = (struct le_audio_source_context *)iport->node->private_data;

    iport->handle_frame = le_audio_source_handle_frame;

    le_audio_to_local_usec_init(ctx);
}

static void le_audio_source_set_bt_addr(struct stream_iport *iport, int arg)
{
    struct le_audio_source_context *ctx = (struct le_audio_source_context *)iport->node->private_data;

    ctx->le_audio = (void *)arg;
}

static int le_audio_source_ioc_start(struct stream_iport *iport)
{
    struct le_audio_source_iport *hdl = (struct le_audio_source_iport *)iport->private_data;
    struct le_audio_source_context *ctx = (struct le_audio_source_context *)iport->node->private_data;

    if (!ctx->le_audio) {
        printf("LE Audio source node have no le audio handle.\n");
        return 0;
    }

    if (hdl) {
        le_audio_stream_set_bit_width(ctx->le_audio, hdl->bit_width);
    }

    if (!ctx->diff_coding_type) { /*两个iport非相同格式，说明一个为PCM格式的本地播放源和一个压缩格式的转发源*/
        if (hdl->coding_type == AUDIO_CODING_PCM) {
            if (ctx->rx_stream) {
                return 0;
            }
            ctx->rx_stream = le_audio_stream_rx_open(ctx->le_audio, hdl->coding_type);
            hdl->attribute = LE_AUDIO_LOCAL_SOURCE;
        } else {
            if (ctx->tx_stream) {
                return 0;
            }
            ctx->tx_stream = le_audio_stream_tx_open(ctx->le_audio, hdl->coding_type, NULL, NULL);
            le_audio_stream_set_tx_tick_handler(ctx->le_audio, ctx, le_audio_tx_tick_handler);
            hdl->attribute = LE_AUDIO_TX_SOURCE;
        }
        return 0;
    }

    /*两个iport的格式相同，说明本地播放源和转发源为相同格式，需要区分一个为发送一个为本地存储*/
    if (!ctx->tx_stream) {
        ctx->tx_stream = le_audio_stream_tx_open(ctx->le_audio, hdl->coding_type, NULL, NULL);
        le_audio_stream_set_tx_tick_handler(ctx->le_audio, ctx, le_audio_tx_tick_handler);
        hdl->attribute = LE_AUDIO_TX_SOURCE;
        return 0;
    }

    if (!ctx->rx_stream) {
        ctx->rx_stream = le_audio_stream_rx_open(ctx->le_audio, hdl->coding_type);
        hdl->attribute = LE_AUDIO_LOCAL_SOURCE;
    }

    return 0;
}

static void le_audio_source_ioc_stop(struct stream_iport *iport)
{
    struct le_audio_source_iport *hdl = (struct le_audio_source_iport *)iport->private_data;
    struct le_audio_source_context *ctx = (struct le_audio_source_context *)iport->node->private_data;

    if (hdl->attribute == LE_AUDIO_LOCAL_SOURCE) {
        if (ctx->rx_stream) {
            le_audio_stream_rx_close(ctx->rx_stream);
            ctx->rx_stream = NULL;
        }
    } else {
        le_audio_stream_set_tx_tick_handler(ctx->le_audio, NULL, NULL);
        if (ctx->tx_stream) {
            le_audio_stream_tx_close(ctx->tx_stream);
            ctx->tx_stream = NULL;
        }
    }
}

static int le_audio_source_ioc_fmt_nego(struct stream_iport *iport)
{
    struct le_audio_source_iport *hdl = (struct le_audio_source_iport *)iport->private_data;
    struct le_audio_source_context *ctx = (struct le_audio_source_context *)iport->node->private_data;
    struct stream_fmt *in_fmt = &iport->prev->fmt;

    if (!ctx->coding_type) {
        ctx->coding_type = in_fmt->coding_type;
    }

    if (ctx->coding_type != 0 && in_fmt->coding_type != ctx->coding_type) {
        ctx->diff_coding_type = 1;
    }

    if (in_fmt->coding_type == 0) {
        in_fmt->coding_type = hdl->coding_type;
    }
    hdl->coding_type = in_fmt->coding_type;
    hdl->bit_width = in_fmt->bit_wide;
    in_fmt->channel_mode = hdl->channel_mode;
    in_fmt->sample_rate = hdl->sample_rate;

    return NEGO_STA_ACCPTED;
}

static int le_audio_source_buffer_delay_time(struct stream_iport *iport)
{
    struct le_audio_source_context *ctx = (struct le_audio_source_context *)iport->node->private_data;

    if (ctx->tx_stream) {
        return le_audio_stream_tx_buffered_time(ctx->tx_stream)/*us*/ / 100;
    }

    return 0;
}

static int get_le_audio_source_buffer_delay_time(void *iport, void *ch)
{
    return le_audio_source_buffer_delay_time(iport);
}

static void le_audio_source_set_encode_fmt(struct stream_iport *iport, int arg)
{
    struct le_audio_source_iport *hdl = (struct le_audio_source_iport *)iport->private_data;
    struct le_audio_source_context *ctx = (struct le_audio_source_context *)iport->node->private_data;
    struct stream_enc_fmt *fmt = (struct stream_enc_fmt *)arg;

    hdl->bit_rate = fmt->bit_rate;
    hdl->frame_dms = fmt->frame_dms;
    hdl->sample_rate = fmt->sample_rate;
    hdl->frame_len = hdl->bit_rate * hdl->frame_dms / 8 / 10000 + 2;
    hdl->channel_mode = fmt->channel == 2 ? AUDIO_CH_LR : AUDIO_CH_MIX;

    ctx->sample_rate = fmt->sample_rate;
}

static void le_audio_source_add_syncts(struct le_audio_source_context *ctx, void *syncts, u32 timestamp)
{
    struct le_audio_source_syncts *node = NULL;

    spin_lock(&ctx->lock);
    list_for_each_entry(node, &ctx->syncts_list, entry) {
        if ((u32)node->syncts == (u32)syncts) {
            spin_unlock(&ctx->lock);
            return;
        }
    }
    spin_unlock(&ctx->lock);

    node = (struct le_audio_source_syncts *)zalloc(sizeof(struct le_audio_source_syncts));
    node->syncts = syncts;
    node->timestamp = timestamp;
    spin_lock(&ctx->lock);
    list_add(&node->entry, &ctx->syncts_list);
    spin_unlock(&ctx->lock);
}

static void le_audio_source_remove_syncts(struct le_audio_source_context *ctx, void *syncts)
{
    struct le_audio_source_syncts *node = NULL;

    spin_lock(&ctx->lock);
    list_for_each_entry(node, &ctx->syncts_list, entry) {
        if (node->syncts == syncts) {
            goto remove_node;
        }
    }

    spin_unlock(&ctx->lock);
    return;
remove_node:
    list_del(&node->entry);
    free(node);
    spin_unlock(&ctx->lock);
}
__attribute__((weak))
void dev_list_add(void *_dev, void *hdl, void *ch, int (*get_delay)(void *hdl, void *ch))
{
    printf("%s EMPTY fun\n", __FUNCTION__);
}

__attribute__((weak))
void dev_list_del(void *_dev)
{
    printf("%s EMPTY fun\n", __FUNCTION__);
}
static int le_audio_source_syncts_handler(struct stream_iport *iport, int arg)
{
    struct le_audio_source_context *ctx = (struct le_audio_source_context *)iport->node->private_data;
    struct audio_syncts_ioc_params *params = (struct audio_syncts_ioc_params *)arg;

    if (!params) {
        return 0;
    }

    switch (params->cmd) {
    case AUDIO_SYNCTS_MOUNT_ON_SNDPCM:
        le_audio_source_add_syncts(ctx, (void *)params->data[0], params->data[1]);
        dev_list_add(&ctx->dev, iport, NULL, (int (*)(void *, void *))get_le_audio_source_buffer_delay_time);
        break;
    case AUDIO_SYNCTS_UMOUNT_ON_SNDPCM:
        le_audio_source_remove_syncts(ctx, (void *)params->data[0]);
        dev_list_del((void *)&ctx->dev);
        break;
    }
    return 0;
}

static int le_audio_source_ioctl(struct stream_iport *iport, int cmd, int arg)
{
    switch (cmd) {
    case NODE_IOC_OPEN_IPORT:
        le_audio_source_open_iport(iport);
        break;
    case NODE_IOC_CLOSE_IPORT:
        break;
    case NODE_IOC_SET_BTADDR:
        le_audio_source_set_bt_addr(iport, arg);
        break;
    case NODE_IOC_START:
        le_audio_source_ioc_start(iport);
        break;
    case NODE_IOC_STOP:
        le_audio_source_ioc_stop(iport);
        break;
    case NODE_IOC_NEGOTIATE:
        *(int *)arg |= le_audio_source_ioc_fmt_nego(iport);
        break;
    case NODE_IOC_GET_DELAY:
        return le_audio_source_buffer_delay_time(iport);
    case NODE_IOC_SET_ENC_FMT:
        le_audio_source_set_encode_fmt(iport, arg);
        break;
    case NODE_IOC_SYNCTS:
        le_audio_source_syncts_handler(iport, arg);
        break;
    default:
        break;
    }
    return 0;
}

static void le_audio_source_release(struct stream_node *node)
{
}

REGISTER_STREAM_NODE_ADAPTER(le_audio_source_adapter) = {
    .name       = "le_audio_source",
    .uuid       = NODE_UUID_LE_AUDIO_SOURCE,
    .bind       = le_audio_source_bind,
    .ioctl      = le_audio_source_ioctl,
    .release    = le_audio_source_release,
    .hdl_size = sizeof(struct le_audio_source_context),
    .iport_size = sizeof(struct le_audio_source_iport),
};


#endif/*LE_AUDIO_STREAM_ENABLE*/

