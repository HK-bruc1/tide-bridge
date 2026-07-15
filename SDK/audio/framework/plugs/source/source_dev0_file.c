#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".source_dev0_file.data.bss")
#pragma data_seg(".source_dev0_file.data")
#pragma const_seg(".source_dev0_file.text.const")
#pragma code_seg(".source_dev0_file.text")
#endif
#include "classic/hci_lmp.h"
#include "source_node.h"
#include "jlstream.h"
#include "media/audio_base.h"
#include "app_config.h"
#include "source_dev0.h"

/*
   若源节点为中断节点，则需打开SOURCE_DEV0_IRQ_ENABLE
   并且在中断中调用source_dev0_packet_rx_notify, 用于中断收到数据时，主动唤醒数据流
*/
#define SOURCE_DEV0_IRQ_ENABLE		0		//中断节点使能

#define SOURCE_DEV0_MSBC_TEST_ENABLE	0		//MSBC解码测试, 使用MSBC固定数据测试


#if TCFG_SOURCE_DEV0_NODE_ENABLE

struct source_dev0_file_hdl {
    u8 start;					//启动标志
    struct stream_node *node;	//节点句柄
    u8 bt_addr[6];				//蓝牙MAC地址
    u8 ch_num;
};

#include "circular_buf.h"

static cbuffer_t output_cbuf_h;
static u8 *output_buff = NULL;
static u32 source_dev0_consumed_bytes = 0;
#define OUTPUT_BUFF_SIZE    (2048)
#define OPUS_MONO_FRAME_BYTES       (40u)
#if defined(TCFG_RDX_LOCAL_PLAYBACK_ENABLE) && TCFG_RDX_LOCAL_PLAYBACK_ENABLE
#define OPUS_STEREO_FRAME_BYTES     (80u)
#define OPUS_MAX_FRAME_BYTES        OPUS_STEREO_FRAME_BYTES
#else
#define OPUS_MAX_FRAME_BYTES        OPUS_MONO_FRAME_BYTES
#endif
#define OPUS_SAMPLE_RATE            (16000u)
#if defined(TCFG_RDX_LOCAL_PLAYBACK_ENABLE) && TCFG_RDX_LOCAL_PLAYBACK_ENABLE
#define OPUS_STEREO_DEC_SAMPLE_RATE (48000u)
#endif
#define OPUS_FRAME_DMS              (200u)
#define OPUS_MONO_BIT_RATE          (16000u)
#if defined(TCFG_RDX_LOCAL_PLAYBACK_ENABLE) && TCFG_RDX_LOCAL_PLAYBACK_ENABLE
#define OPUS_STEREO_BIT_RATE        (32000u)
#endif

static void output_buff_init(void)
{
    if(output_buff == NULL){
        output_buff = malloc(OUTPUT_BUFF_SIZE);
        ASSERT(output_buff);
        cbuf_init(&output_cbuf_h, output_buff, OUTPUT_BUFF_SIZE);
    }
}

static void output_buff_free(void)
{
    if(output_buff){
        OS_ENTER_CRITICAL();
        free(output_buff);
        output_buff = NULL;
        OS_EXIT_CRITICAL();
    }
}

static u32 source_input_write(u8 *data, u16 len)
{
    if(output_buff == NULL){
        return 0;
    }
    u32 ret = cbuf_write(&output_cbuf_h, data, len);
    if(ret != len){
        putchar('N');
    }
    return ret;
}

static u32 source_input_read(u8 *data, u16 frame_len)
{
    if(output_buff == NULL){
        return 0;
    }
    if(cbuf_get_data_len(&output_cbuf_h) < frame_len){
        return 0;
    }
    return cbuf_read(&output_cbuf_h, data, frame_len);
}

static void source_dev0_add_consumed_bytes(u32 len)
{
    OS_ENTER_CRITICAL();
    source_dev0_consumed_bytes += len;
    OS_EXIT_CRITICAL();
}

//输入到解码
u32 source_dev0_input_write(u8 *data, u16 len)
{
    return source_input_write(data, len);
}

u32 source_dev0_get_free_space(void)
{
    if (output_buff == NULL) {
        return 0;
    }
    return OUTPUT_BUFF_SIZE - cbuf_get_data_len(&output_cbuf_h);
}

bool source_dev0_is_empty(void)
{
    if (output_buff == NULL) {
        return true;
    }
    return cbuf_get_data_len(&output_cbuf_h) == 0;
}

u32 source_dev0_get_consumed_bytes(void)
{
    u32 ret;

    OS_ENTER_CRITICAL();
    ret = source_dev0_consumed_bytes;
    OS_EXIT_CRITICAL();

    return ret;
}

void source_dev0_reset_consumed_bytes(void)
{
    OS_ENTER_CRITICAL();
    source_dev0_consumed_bytes = 0;
    OS_EXIT_CRITICAL();
}

#if SOURCE_DEV0_MSBC_TEST_ENABLE
static unsigned char source_test_data[60] = {
    0x01, 0x08, 0xAD, 0x00, 0x00, 0x35, 0xC3, 0x31, 0x01, 0x00, 0x7F, 0xEF, 0x76, 0xDF, 0xFB, 0x9D,
    0xB8, 0x05, 0x08, 0x6E, 0x04, 0x4E, 0x0B, 0x83, 0xF6, 0xBA, 0x62, 0xD0, 0x62, 0xB9, 0x1B, 0x27,
    0x6E, 0x5F, 0xB9, 0xDB, 0x9E, 0x2F, 0x76, 0xE9, 0x1B, 0xDD, 0xBA, 0xAA, 0xF7, 0x4E, 0xC3, 0xBD,
    0xDB, 0xB7, 0x2F, 0x74, 0xEF, 0x5B, 0xDD, 0x3C, 0x3A, 0xF7, 0x6C, 0x00
};
#endif

static u8 output[OPUS_MAX_FRAME_BYTES];

static u8 data_ok = 0;
/*
   输入源节点数据
   	hdl 节点私有句柄
   	*len 源节点数据长度
   	return 源节点数据指针
*/
static u8 *source_dev0_get_packet(struct source_dev0_file_hdl *hdl, u32 *len)
{
    u8 *packet = NULL;
    u32 packet_len = 0;
    u16 frame_len = OPUS_MONO_FRAME_BYTES;
#if defined(TCFG_RDX_LOCAL_PLAYBACK_ENABLE) && TCFG_RDX_LOCAL_PLAYBACK_ENABLE
    if (hdl->ch_num == 2) {
        frame_len = OPUS_STEREO_FRAME_BYTES;
    }
#endif

    if(data_ok == 0 && cbuf_get_data_len(&output_cbuf_h) < (OUTPUT_BUFF_SIZE / 4)){
        return NULL;
    }
    data_ok = 1;
    //do something
    packet = (u8 *)output;
    // putchar('b');
    packet_len = source_input_read(output, frame_len);
#if SOURCE_DEV0_MSBC_TEST_ENABLE
    u8 test_buf[4] = {0x08, 0x38, 0xc8, 0xf8};
    packet_len = sizeof(source_test_data);
    packet = source_test_data;
    static u8 i = 0;
    packet[1] = test_buf[i++];
    if (i > 3) {
        i = 0;
    }
#endif

    if(packet_len == 0){
        putchar('K');
        data_ok = 0;
    }
    *len = packet_len;
    return packet_len ? packet : NULL;
}

//释放源节点数据
static int source_dev0_free_packet(struct source_dev0_file_hdl *hdl, u8 *packet)
{
    /* free(packet); */
    return 0;
}

//自定义源节点挂起
static void source_dev0_suspend(struct source_dev0_file_hdl *hdl)
{
    /*
       do something
    	1、启动丢数-避免suspend时 自定义源节点数据溢出
    	2、挂起时，源节点需要的其他流程
    */
}

//自定义源节点 初始化
static void source_dev0_open(struct source_dev0_file_hdl *hdl)
{
    /*
       do something
    	1、关闭丢数-用于suspend流程保护
    	2、(中断节点需要)注册自定义源节点收包回调 source_dev0_packet_rx_notify
    	3、自定义源节点启动流程
    */
    data_ok = 0;
    source_dev0_reset_consumed_bytes();
    output_buff_init();
}

//自定义源节点 停止
static void source_dev0_close(struct source_dev0_file_hdl *hdl)
{
    //do something
    data_ok = 0;
    source_dev0_reset_consumed_bytes();
    output_buff_free();
}

/*
   自定义源节点 参数设置
   *fmt 目标参数,如采样率，数据类型，通道模式
*/
static void source_dev0_get_fmt(struct source_dev0_file_hdl *hdl, struct stream_fmt *fmt)
{
#if SOURCE_DEV0_MSBC_TEST_ENABLE
    fmt->sample_rate = 16000;				//采样率
    fmt->coding_type = AUDIO_CODING_MSBC;	//数据类型
    fmt->channel_mode = AUDIO_CH_LR;		//通道模式
#endif
    fmt->frame_dms = OPUS_FRAME_DMS;
#if defined(TCFG_RDX_LOCAL_PLAYBACK_ENABLE) && TCFG_RDX_LOCAL_PLAYBACK_ENABLE
    if (hdl->ch_num == 2) {
        /* JL's stereo Opus decoder always emits 48 kHz PCM. */
        fmt->sample_rate = OPUS_STEREO_DEC_SAMPLE_RATE;
        fmt->coding_type = AUDIO_CODING_STENC_OPUS;
        fmt->channel_mode = AUDIO_CH_LR;
        fmt->bit_rate = OPUS_STEREO_BIT_RATE;
        return;
    }
#endif
    {
        fmt->sample_rate = OPUS_SAMPLE_RATE;
        fmt->coding_type = AUDIO_CODING_OPUS;
        fmt->channel_mode = hdl->ch_num == 2 ? AUDIO_CH_LR : AUDIO_CH_MIX;
        fmt->bit_rate = OPUS_MONO_BIT_RATE;
    }
}

/*
	(当前节点为中断节点使用)
   自定义源节点收包回调
   - 用于自定义源节点收到数时, 唤醒数据流
*/
static void source_dev0_packet_rx_notify(void *_hdl)
{
    struct source_dev0_file_hdl *hdl = (struct source_dev0_file_hdl *)_hdl;
    //启动状态才触发
    if (hdl->start) {
        jlstream_wakeup_thread(NULL, hdl->node, NULL);
    }
}


static enum stream_node_state source_dev0_get_frame(void *_hdl, struct stream_frame **_frame)
{
    u32 len = 0;
    struct source_dev0_file_hdl *hdl = (struct source_dev0_file_hdl *)_hdl;
    struct stream_frame *frame;

    //1、获取当前节点数据
    u8 *packet = source_dev0_get_packet(hdl, &len);
    if (!packet) {
        *_frame = NULL;
        //表示源节点获取不到数据
        return NODE_STA_RUN | NODE_STA_SOURCE_NO_DATA;
    }
    //2、申请数据流frame空间
    frame = jlstream_get_frame(hdl->node->oport, len);
    frame->len = len;

    //3、将当前节点数据拷贝到frame
    memcpy(frame->data, packet, len);
    source_dev0_add_consumed_bytes(len);

    //4、释放当前节点数据
    source_dev0_free_packet(hdl, packet);

    *_frame = frame;

    return NODE_STA_RUN;
}

static void *source_dev0_init(void *priv, struct stream_node *node)
{
    struct source_dev0_file_hdl *hdl = zalloc(sizeof(*hdl));
    printf("source_dev0_init");
    hdl->node = node;
#if SOURCE_DEV0_IRQ_ENABLE
    node->type |= NODE_TYPE_IRQ;
#endif/*SOURCE_DEV0_IRQ_ENABLE*/
    return hdl;
}

//获取当前蓝牙地址
static int source_dev0_ioc_set_bt_addr(struct source_dev0_file_hdl *hdl, u8 *bt_addr)
{
    memcpy(hdl->bt_addr, bt_addr, 6);
    return 0;
}

//获取当前节点数据参数
static void source_dev0_ioc_get_fmt(struct source_dev0_file_hdl *hdl, struct stream_fmt *fmt)
{
    source_dev0_get_fmt(hdl, fmt);
}

static void source_dev0_ioc_suspend(struct source_dev0_file_hdl *hdl)
{
    printf("source_dev0_ioc_suspend");
    hdl->start = 0;
    source_dev0_suspend(hdl);
}

static void source_dev0_ioc_start(struct source_dev0_file_hdl *hdl)
{
    printf("source_dev0_ioc_start");
    source_dev0_open(hdl);
    hdl->start = 1;
}

static void source_dev0_ioc_stop(struct source_dev0_file_hdl *hdl)
{
    printf("source_dev0_ioc_stop");
    hdl->start = 0;
    source_dev0_close(hdl);
}

static int source_dev0_ioctl(void *_hdl, int cmd, int arg)
{
    struct source_dev0_file_hdl *hdl = (struct source_dev0_file_hdl *)_hdl;

    switch (cmd) {
    case NODE_IOC_SET_BTADDR:
        source_dev0_ioc_set_bt_addr(hdl, (u8 *)arg);
        break;
    case NODE_IOC_GET_FMT:
        source_dev0_ioc_get_fmt(hdl, (struct stream_fmt *)arg);
        break;
    case NODE_IOC_SET_SCENE:
        break;
    case NODE_IOC_SUSPEND:
        source_dev0_ioc_suspend(hdl);
        break;
    case NODE_IOC_START:
        source_dev0_ioc_start(hdl);
        break;
    case NODE_IOC_STOP:
        source_dev0_ioc_stop(hdl);
        break;
    case NODE_IOC_SET_PRIV_FMT:
        hdl->ch_num = (u8)arg;
        printf("source_dev0 NODE_IOC_SET_PRIV_FMT %d\n", hdl->ch_num);
        break;
    }

    return 0;
}

static void source_dev0_release(void *_hdl)
{
    struct source_dev0_file_hdl *hdl = (struct source_dev0_file_hdl *)_hdl;
    printf("source_dev0_release");

    free(hdl);
}


REGISTER_SOURCE_NODE_PLUG(source_dev0_file_plug) = {
    .uuid       = NODE_UUID_SOURCE_DEV0,
    .init       = source_dev0_init,
    .get_frame  = source_dev0_get_frame,
    .ioctl      = source_dev0_ioctl,
    .release    = source_dev0_release,
};

#endif




