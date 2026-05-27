/*************************************************************************************************/
/*!
*  \file      le_audio_file.c
*
*  \brief
*
*  Copyright (c) 2011-2023 ZhuHai Jieli Technology Co.,Ltd.
*
*/
/*************************************************************************************************/
#include "source_node.h"
#include "le_audio_stream.h"
#include "reference_time.h"
#include "app_config.h"

#if LE_AUDIO_STREAM_ENABLE

extern int CONFIG_LE_AUDIO_PLAY_LATENCY;
struct le_audio_file_handle {
    u8 start;
    u8 step;
    u8 reference;
    u8 timestamp_enable;
    void *file;
    struct stream_node *node;
    int play_latency;
};

static enum stream_node_state le_audio_get_frame(void *file, struct stream_frame **pframe)
{
    struct le_audio_file_handle *hdl = (struct le_audio_file_handle *)file;
    struct le_audio_frame *le_audio_frame = NULL;
    struct stream_frame *frame;

    /* if (hdl->step == 0) {  */
    /* if (!hdl->timestamp_enable && le_audio_stream_get_frame_num(hdl->file) < 3) { */
    /* *pframe = NULL; */
    /* return NODE_STA_RUN | NODE_STA_SOURCE_NO_DATA; */
    /* } */
    /* hdl->step = 1; */
    /* } */
    le_audio_frame = le_audio_stream_get_frame(hdl->file);
    if (!le_audio_frame) {
        *pframe = NULL;
        return NODE_STA_RUN | NODE_STA_SOURCE_NO_DATA;
    }

    frame = jlstream_get_frame(hdl->node->oport, le_audio_frame->len);
    if (frame == NULL) {
        *pframe = NULL;
        return NODE_STA_RUN;
    }

    memcpy(frame->data, le_audio_frame->data, le_audio_frame->len);
    /*put_buf(frame->data, 16);*/
    frame->len = le_audio_frame->len;
    frame->timestamp = ((le_audio_frame->timestamp + hdl->play_latency) & 0xfffffff) * TIMESTAMP_US_DENOMINATOR;
    frame->flags |= FRAME_FLAG_TIMESTAMP_ENABLE | FRAME_FLAG_UPDATE_TIMESTAMP;

    le_audio_stream_free_frame(hdl->file, le_audio_frame);
    *pframe = frame;

    if (le_audio_stream_get_frame_num(hdl->file) == 0) {
        return NODE_STA_RUN | NODE_STA_SOURCE_NO_DATA;
    }

    return NODE_STA_RUN;

}

static void *le_audio_file_init(void *priv, struct stream_node *node)
{
    struct le_audio_file_handle *hdl = (struct le_audio_file_handle *)zalloc(sizeof(struct le_audio_file_handle));

    hdl->node = node;
    node->type |= NODE_TYPE_IRQ | NODE_TYPE_FLOW_CTRL;

    return hdl;
}

static int le_audio_file_set_bt_addr(struct le_audio_file_handle *hdl, void *bt_addr)
{
    hdl->file = (void *)bt_addr;

    return 0;
}
static void le_audio_rx_tick_handler(void *priv)
{
    struct le_audio_file_handle *hdl = (struct le_audio_file_handle *)priv;

    if (hdl->start) {
        jlstream_wakeup_thread(NULL, hdl->node, NULL);
    }
}

static int le_audio_file_get_fmt(struct le_audio_file_handle *hdl, struct stream_fmt *fmt)
{
    struct le_audio_stream_format le_audio_fmt = {0};

    if (hdl->file) {
        int err = le_audio_stream_get_rx_format(hdl->file, &le_audio_fmt);
        if (err) {
            return -EAGAIN;
        }
    }
    fmt->frame_dms = le_audio_fmt.frame_dms;
    fmt->bit_rate = le_audio_fmt.bit_rate;
    fmt->coding_type = le_audio_fmt.coding_type;
    fmt->sample_rate = le_audio_fmt.sample_rate;
    fmt->channel_mode = le_audio_fmt.nch == 2 ? AUDIO_CH_LR : AUDIO_CH_MIX;
    return 0;
}

static int le_audio_file_start(struct le_audio_file_handle *hdl)
{
    if (hdl->start) {
        return 0;
    }

    le_audio_stream_set_rx_tick_handler(hdl->file, hdl, le_audio_rx_tick_handler);

    hdl->start = 1;
    int err = stream_node_ioctl(hdl->node, NODE_UUID_BT_AUDIO_SYNC, NODE_IOC_SYNCTS, 0);
    if (err) {
        return 0;
    }
    hdl->play_latency = CONFIG_LE_AUDIO_PLAY_LATENCY;
    hdl->timestamp_enable = 1;
    hdl->reference = audio_reference_clock_select(hdl->file, 2);
    return 0;
}

static int le_audio_file_stop(struct le_audio_file_handle *hdl)
{
    if (hdl->start) {
        le_audio_stream_set_rx_tick_handler(hdl->file, NULL, NULL);

        if (hdl->reference) {
            audio_reference_clock_exit(hdl->reference);
        }
        hdl->start = 0;
    }

    return 0;
}

static int le_audio_file_ioctl(void *file, int cmd, int arg)
{
    int err = 0;
    struct le_audio_file_handle *hdl = (struct le_audio_file_handle *)file;

    switch (cmd) {
    case NODE_IOC_SET_BTADDR:
        le_audio_file_set_bt_addr(hdl, (void *)arg);
        break;
    case NODE_IOC_GET_FMT:
        le_audio_file_get_fmt(hdl, (struct stream_fmt *)arg);
        break;
    case NODE_IOC_START:
        le_audio_file_start(hdl);
        break;
    case NODE_IOC_SUSPEND:
    case NODE_IOC_STOP:
        le_audio_file_stop(hdl);
        break;
    }

    return 0;
}

static void le_audio_file_release(void *file)
{
    struct le_audio_file_handle *hdl = (struct le_audio_file_handle *)file;

    free(hdl);
}

REGISTER_SOURCE_NODE_PLUG(le_audio_file_plug) = {
    .uuid       = NODE_UUID_LE_AUDIO_SINK,
    .init       = le_audio_file_init,
    .get_frame  = le_audio_get_frame,
    .ioctl      = le_audio_file_ioctl,
    .release    = le_audio_file_release,
};

#endif/*LE_AUDIO_STREAM_ENABLE*/

