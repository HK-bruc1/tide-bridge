#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".source_dev1_file.data.bss")
#pragma data_seg(".source_dev1_file.data")
#pragma const_seg(".source_dev1_file.text.const")
#pragma code_seg(".source_dev1_file.text")
#endif
#include "classic/hci_lmp.h"
#include "source_node.h"
#include "jlstream.h"
#include "media/audio_base.h"
#include "app_config.h"
#include "Resample_api.h"
#include "st_opus_enc/opus_stenc_api.h"

/*
   若源节点为中断节点，则需打开SOURCE_DEV1_IRQ_ENABLE
   并且在中断中调用source_dev1_packet_rx_notify, 用于中断收到数据时，主动唤醒数据流
*/
#define SOURCE_DEV1_IRQ_ENABLE		1		//中断节点使能

#define SOURCE_DEV1_MSBC_TEST_ENABLE	0		//MSBC解码测试, 使用MSBC固定数据测试


#if TCFG_SOURCE_DEV1_NODE_ENABLE

struct source_dev1_file_hdl {
    u8 start;					//启动标志
    struct stream_node *node;	//节点句柄
    u8 bt_addr[6];				//蓝牙MAC地址
    u8 ch_mode;
};

static struct source_dev1_file_hdl *hdl_p;//数据流句柄
static u8 *output = NULL;//[FRAME_POINT * 2 * 2];

#include "circular_buf.h"
//一共需要两个缓存，[0]用于缓存mic采样数据，[1]用于缓存dac数据
static cbuffer_t output_cbuf_h[2];
static u8 *output_buff[2] = {NULL, NULL};
#define OUTPUT_BUFF_SIZE    (FRAME_SIZE * 8)
static u32 source_frame_count;
static u32 source_wait_count;
static u32 source_drop_count[2];

static void output_buff_init(void)
{
    for (u8 i = 0; i < 2; i++)
    {
        if(output_buff[i] == NULL){
            output_buff[i] = malloc(OUTPUT_BUFF_SIZE);
            ASSERT(output_buff[i]);
            cbuf_init(&output_cbuf_h[i], output_buff[i], OUTPUT_BUFF_SIZE);
        }
    }
}

static void output_buff_free(void)
{
    for (u8 i = 0; i < 2; i++)
    {
        if(output_buff[i]){
            OS_ENTER_CRITICAL();
            free(output_buff[i]);
            output_buff[i] = NULL;
            OS_EXIT_CRITICAL();
        }
    }
}

static void source_dev1_packet_rx_notify(void *_hdl);

static u32 source_input_write(u8 idx, u8 *data, u16 len)
{
    if(output_buff[idx] == NULL){
        return 0;
    }
    if (cbuf_is_write_able(&output_cbuf_h[idx], len) < len) {
        source_drop_count[idx]++;
        return 0;
    }
    u32 ret = cbuf_write(&output_cbuf_h[idx], data, len);
    if(ret != len){
        source_drop_count[idx]++;
    }

    return ret;
}

static u32 source_input_read(u8 idx, u8 *data, u16 len)
{
    if(output_buff[idx] == NULL){
        return 0;
    }
    if(len > cbuf_get_data_len(&output_cbuf_h[idx])){
        return 0;
    }
    return cbuf_read(&output_cbuf_h[idx], data, len);
}

/**
 * @brief 检查声道是否启用
 * 
 * @param idx 0：mic  1：dac
 * @return u8 1：启用  0：关闭
 */
u8 source_dev1_get_idx_enable(u8 idx)
{
    struct source_dev1_file_hdl *hdl = hdl_p;
    if(hdl == NULL){
        return 0;
    }
    if(idx == 0 && (hdl->ch_mode == AUDIO_CH_L || hdl->ch_mode == AUDIO_CH_LR)){
        return 1;
    }
    if(idx == 1 && (hdl->ch_mode == AUDIO_CH_R || hdl->ch_mode == AUDIO_CH_LR)){
        return 1;
    }

    return 0;
}

void wakeup_encoder(void)
{
    struct source_dev1_file_hdl *hdl = hdl_p;
    if(hdl){
        source_dev1_packet_rx_notify(hdl);
    }
}

//输入到编码
u32 source_dev1_input_write(u8 idx, u8 *data, u16 len)
{
    struct source_dev1_file_hdl *hdl = hdl_p;
    if(hdl == NULL || hdl->start == 0 || output_buff[0] == NULL || output_buff[1] == NULL){
        return len;
    }
    int ret = source_input_write(idx, data, len);
    u8 ch_num = AUDIO_CH_NUM(hdl->ch_mode);
    if ((ch_num == 2 &&
         cbuf_get_data_len(&output_cbuf_h[0]) >= FRAME_SIZE &&
         cbuf_get_data_len(&output_cbuf_h[1]) >= FRAME_SIZE) ||
        (ch_num == 1 && cbuf_get_data_len(&output_cbuf_h[idx]) >= FRAME_SIZE)) {
        source_dev1_packet_rx_notify(hdl);
    }
    return ret;
}

#if SOURCE_DEV1_MSBC_TEST_ENABLE
static unsigned char source_test_data[60] = {
    0x01, 0x08, 0xAD, 0x00, 0x00, 0x35, 0xC3, 0x31, 0x01, 0x00, 0x7F, 0xEF, 0x76, 0xDF, 0xFB, 0x9D,
    0xB8, 0x05, 0x08, 0x6E, 0x04, 0x4E, 0x0B, 0x83, 0xF6, 0xBA, 0x62, 0xD0, 0x62, 0xB9, 0x1B, 0x27,
    0x6E, 0x5F, 0xB9, 0xDB, 0x9E, 0x2F, 0x76, 0xE9, 0x1B, 0xDD, 0xBA, 0xAA, 0xF7, 0x4E, 0xC3, 0xBD,
    0xDB, 0xB7, 0x2F, 0x74, 0xEF, 0x5B, 0xDD, 0x3C, 0x3A, 0xF7, 0x6C, 0x00
};
#endif

/*
   输入源节点数据
   	hdl 节点私有句柄
   	*len 源节点数据长度
   	return 源节点数据指针
*/
static u8 *source_dev1_get_packet(struct source_dev1_file_hdl *hdl, u32 *len)
{
    u8 *packet = NULL;
    u32 packet_len = 0;

    //do something
    u16 read_len = 0;
    u8 ch_num = AUDIO_CH_NUM(hdl->ch_mode);
    packet = (u8 *)output;
    // putchar('B');
    memset(output, 0, FRAME_POINT * 2 * 2);
    if(ch_num == 2){
        if (cbuf_get_data_len(&output_cbuf_h[0]) < FRAME_SIZE ||
            cbuf_get_data_len(&output_cbuf_h[1]) < FRAME_SIZE) {
            source_wait_count++;
            return NULL;
        }
        read_len = FRAME_SIZE;
        //立体声编码器，需要的数据是LLLLLLLRRRRRRR格式的
        source_input_read(0, packet, read_len);
        source_input_read(1, packet + read_len, read_len);
        packet_len = read_len * 2;
    }
    else{
        u8 ch = hdl->ch_mode == AUDIO_CH_L ? 0 : 1;
        read_len = cbuf_get_data_len(&output_cbuf_h[ch]);
        read_len = read_len > FRAME_SIZE ? FRAME_SIZE : read_len;
        if(read_len < FRAME_SIZE){
            source_wait_count++;
            return NULL;
        }
        source_input_read(ch, packet, read_len);
        packet_len = read_len;
    }
    // printf("%d\n", packet_len);
    source_frame_count++;
#if SOURCE_DEV1_MSBC_TEST_ENABLE
    u8 test_buf[4] = {0x08, 0x38, 0xc8, 0xf8};
    packet_len = sizeof(source_test_data);
    packet = source_test_data;
    static u8 i = 0;
    packet[1] = test_buf[i++];
    if (i > 3) {
        i = 0;
    }
#endif

    *len = packet_len;
    return packet_len ? packet : NULL;
}

//释放源节点数据
static int source_dev1_free_packet(struct source_dev1_file_hdl *hdl, u8 *packet)
{
    /* free(packet); */
    return 0;
}

//自定义源节点挂起
static void source_dev1_suspend(struct source_dev1_file_hdl *hdl)
{
    /*
       do something
    	1、启动丢数-避免suspend时 自定义源节点数据溢出
    	2、挂起时，源节点需要的其他流程
    */
}

//自定义源节点 初始化
static void source_dev1_open(struct source_dev1_file_hdl *hdl)
{
    /*
       do something
    	1、关闭丢数-用于suspend流程保护
    	2、(中断节点需要)注册自定义源节点收包回调 source_dev1_packet_rx_notify
    	3、自定义源节点启动流程
    */
    output = malloc(FRAME_POINT * 2 * 2);
    output_buff_init();
    source_frame_count = 0;
    source_wait_count = 0;
    source_drop_count[0] = 0;
    source_drop_count[1] = 0;
}

//自定义源节点 停止
static void source_dev1_close(struct source_dev1_file_hdl *hdl)
{
    //do something
    printf("source_dev1 stats: frames=%u wait=%u drop=%u/%u remain=%u/%u\n",
           source_frame_count, source_wait_count,
           source_drop_count[0], source_drop_count[1],
           cbuf_get_data_len(&output_cbuf_h[0]),
           cbuf_get_data_len(&output_cbuf_h[1]));
    output_buff_free();
    free(output);
    output = NULL;
}

/*
   自定义源节点 参数设置
   *fmt 目标参数,如采样率，数据类型，通道模式
*/
static void source_dev1_get_fmt(struct source_dev1_file_hdl *hdl, struct stream_fmt *fmt)
{
#if SOURCE_DEV1_MSBC_TEST_ENABLE
    fmt->sample_rate = 16000;				//采样率
    fmt->coding_type = AUDIO_CODING_MSBC;	//数据类型
    fmt->channel_mode = AUDIO_CH_LR;		//通道模式
#endif
    fmt->sample_rate = 16000;				//采样率
    fmt->coding_type = AUDIO_CODING_PCM;	//数据类型
    fmt->channel_mode = get_global_ch_mode();		//通道模式
    hdl->ch_mode = get_global_ch_mode();
}

/*
	(当前节点为中断节点使用)
   自定义源节点收包回调
   - 用于自定义源节点收到数时, 唤醒数据流
*/
static void source_dev1_packet_rx_notify(void *_hdl)
{
    struct source_dev1_file_hdl *hdl = (struct source_dev1_file_hdl *)_hdl;
    //启动状态才触发
    if (hdl->start) {
        jlstream_wakeup_thread(NULL, hdl->node, NULL);
    }
}


static enum stream_node_state source_dev1_get_frame(void *_hdl, struct stream_frame **_frame)
{
    u32 len = 0;
    struct source_dev1_file_hdl *hdl = (struct source_dev1_file_hdl *)_hdl;
    struct stream_frame *frame;

    //1、获取当前节点数据
    u8 *packet = source_dev1_get_packet(hdl, &len);
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

    //4、释放当前节点数据
    source_dev1_free_packet(hdl, packet);

    *_frame = frame;

    return NODE_STA_RUN;
}

static void *source_dev1_init(void *priv, struct stream_node *node)
{
    struct source_dev1_file_hdl *hdl = zalloc(sizeof(*hdl));
    printf("source_dev1_init");
    hdl->node = node;
#if SOURCE_DEV1_IRQ_ENABLE
    node->type |= NODE_TYPE_IRQ;
#endif/*SOURCE_DEV1_IRQ_ENABLE*/
    hdl_p = hdl;
    return hdl;
}

//获取当前蓝牙地址
static int source_dev1_ioc_set_bt_addr(struct source_dev1_file_hdl *hdl, u8 *bt_addr)
{
    memcpy(hdl->bt_addr, bt_addr, 6);
    return 0;
}

//获取当前节点数据参数
static void source_dev1_ioc_get_fmt(struct source_dev1_file_hdl *hdl, struct stream_fmt *fmt)
{
    source_dev1_get_fmt(hdl, fmt);
}

static void source_dev1_ioc_suspend(struct source_dev1_file_hdl *hdl)
{
    printf("source_dev1_ioc_suspend");
    hdl->start = 0;
    source_dev1_suspend(hdl);
}

static void source_dev1_ioc_start(struct source_dev1_file_hdl *hdl)
{
    printf("source_dev1_ioc_start");
    source_dev1_open(hdl);
    hdl->start = 1;
}

static void source_dev1_ioc_stop(struct source_dev1_file_hdl *hdl)
{
    printf("source_dev1_ioc_stop");
    hdl->start = 0;
    source_dev1_close(hdl);
}

static int source_dev1_ioctl(void *_hdl, int cmd, int arg)
{
    struct source_dev1_file_hdl *hdl = (struct source_dev1_file_hdl *)_hdl;

    switch (cmd) {
    case NODE_IOC_SET_BTADDR:
        source_dev1_ioc_set_bt_addr(hdl, (u8 *)arg);
        break;
    case NODE_IOC_GET_FMT:
        source_dev1_ioc_get_fmt(hdl, (struct stream_fmt *)arg);
        break;
    case NODE_IOC_SET_SCENE:
        break;
    case NODE_IOC_SUSPEND:
        source_dev1_ioc_suspend(hdl);
        break;
    case NODE_IOC_START:
        source_dev1_ioc_start(hdl);
        break;
    case NODE_IOC_STOP:
        source_dev1_ioc_stop(hdl);
        break;
    }

    return 0;
}

static void source_dev1_release(void *_hdl)
{
    struct source_dev1_file_hdl *hdl = (struct source_dev1_file_hdl *)_hdl;
    printf("source_dev1_release");

    hdl_p = NULL;
    free(hdl);
}


REGISTER_SOURCE_NODE_PLUG(source_dev1_file_plug) = {
    .uuid       = NODE_UUID_SOURCE_DEV1,
    .init       = source_dev1_init,
    .get_frame  = source_dev1_get_frame,
    .ioctl      = source_dev1_ioctl,
    .release    = source_dev1_release,
};

#endif






