#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".sink_dev1_node.data.bss")
#pragma data_seg(".sink_dev1_node.data")
#pragma const_seg(".sink_dev1_node.text.const")
#pragma code_seg(".sink_dev1_node.text")
#endif

#include "jlstream.h"
#include "media/audio_base.h"
#include "app_config.h"
#include "system/timer.h"
#include "rcsp_cmd_user.h"

#include "rdx_app_config.h"

// #define BIT_RATE_TEST

#define SINK_DEV1_MSBC_TEST_ENABLE	0		//MSBC编码输出测试

#if TCFG_SINK_DEV1_NODE_ENABLE

struct sink_dev1_hdl {
    struct stream_fmt fmt;		//节点参数
};


extern int rdx_record_run_exit(void);
extern int rdx_record_run_init(void);
extern int rdx_record_run_data_handle(u8 *data, u16 len);

u32 source_dev0_input_write(u8 *data, u16 len);

#ifdef BIT_RATE_TEST
static int tx_speed_cal_timer = 0;
u32 tx_speed = 0;
u32 enc_bitrate = 0;
void tx_speed_cal_deal(void *p)
{
    // int usage[2] = { 0, 0 };
    // int a = os_cpu_usage(NULL, usage);
    // r_printf("===> cpu: usage[0] = %d, usage[1] = %d \n", usage[0], usage[1]);
    // void os_system_info_output(void);
    // os_system_info_output();
    // mem_stats(); //dons++
    printf("===> enc_bitrate %d, tx_speed_cal %d B/s\n", enc_bitrate / 5, tx_speed / 5);
    tx_speed = 0;
    enc_bitrate = 0;
}
#endif

#if (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN)
extern int get_buffer_vaild_len(void);
extern int rdx_protocol_audio_data_indicate(uint8_t* d, uint16_t len);
void send_data(u8 *buff, u16 len)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    int ret = 0;
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(!buff || len == 0){
        return;
    }
    
    // if(!ret){
    //     tx_speed += len;
    // }
    // enc_bitrate += len * 8;
}
#else
_WEAK_ JL_ERR rcsp_user_data_send(u8 *data, u16 len)
{
    return 0;
}

_WEAK_ void send_data(u8 *buff, u16 len)
{
    int ret = rcsp_user_data_send(buff, len);
//     int ret = custom_demo_ble_send(buff, len);
#ifdef BIT_RATE_TEST
    if(!ret){
        tx_speed += len;
    }
    enc_bitrate += len * 8;
#endif
}

#endif

/*
   自定义输出节点 初始化
	hdl:节点私有句柄
*/
static int sink_dev1_init(struct sink_dev1_hdl *hdl)
{
    u8 ch_num = AUDIO_CH_NUM(hdl->fmt.channel_mode);	//声道数
    u32 sample_rate = hdl->fmt.sample_rate;				//采样率
    u32 coding_type = hdl->fmt.coding_type;				//数据格式

    // printf("sink_dev1_init,ch_num=%x,sr=%d,coding_type=%x\n", ch_num, sample_rate, coding_type);

#if (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN)
#if (RDX_AI_SEL_APP & APP_TINGNAO_EN) || (RDX_AI_SEL_APP & APP_AITIR_EN) || (RDX_AI_SEL_APP & APP_TURING_EN) || (RDX_AI_SEL_APP & APP_ZENCHORD_EN)
    //do init record run.
    rdx_record_run_init();
#endif

    //set gain.
    rdx_record_mic_gain_check();
#endif

#ifdef BIT_RATE_TEST
    tx_speed = 0;
    enc_bitrate = 0;
    if(tx_speed_cal_timer == 0){
        tx_speed_cal_timer = sys_timer_add(NULL, tx_speed_cal_deal, 5000);
    }
#endif
    return 0;
}

/*
   自定义输出节点 关闭
	hdl:节点私有句柄
*/
static int sink_dev1_exit(struct sink_dev1_hdl *hdl)
{
#ifdef BIT_RATE_TEST
    sys_timer_del(tx_speed_cal_timer);
#endif

#if (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN)
    rdx_record_run_exit();

    // extern void rdx_sd_rw_test(void);
    // // rdx_sd_rw_test();

    // sys_timeout_add(NULL, rdx_sd_rw_test, 10000);
#endif

#ifdef BIT_RATE_TEST
    //do something
    if(tx_speed_cal_timer){
        sys_timer_del(tx_speed_cal_timer);
        tx_speed_cal_timer = 0;
    }
#endif
    return 0;
}

/*
   自定义输出节点 运行
	hdl:节点私有句柄
 	*data:输入数据地址,位宽16bit
  	data_len :输入数据长度,byte
*/
static void sink_dev1_run(struct sink_dev1_hdl *hdl, void *data, int data_len)
{
    //do something
    // get_sine0_data(&tx1_s_cnt, tmp_data, sizeof(tmp_data)/2/2, 1);
    // putchar('V');

#if (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN)
    //record data handle.
    rdx_record_run_data_handle(data, data_len);
#endif

    // put_buf(data, data_len);
#if !(THIRD_PARTY_PROTOCOLS_SEL & RDX_EN)
    source_dev0_input_write(data, data_len);//自编自解，一般测试才用
#endif
#ifdef BIT_RATE_TEST
    tx_speed += data_len;
    enc_bitrate += data_len * 8;
#endif
}

static void sink_dev1_handle_frame(struct stream_iport *iport, struct stream_note *note)
{
    struct sink_dev1_hdl *hdl = (struct sink_dev1_hdl *)iport->node->private_data;
    struct stream_frame *frame;

    while (1) {
        //获取上一个节点的输出
        frame = jlstream_pull_frame(iport, note);
        if (!frame) {
            break;
        }
        sink_dev1_run(hdl, frame->data, frame->len);
        //释放资源
        jlstream_free_frame(frame);
    }
}

static int sink_dev1_bind(struct stream_node *node, u16 uuid)
{
    printf("sink_dev1_bind");
    /* struct sink_dev1_hdl *hdl = (struct sink_dev1_hdl *)node->private_data; */
    return 0;
}

static void sink_dev1_open_iport(struct stream_iport *iport)
{
    iport->handle_frame = sink_dev1_handle_frame;
}

/*
*********************************************************************
*                  sink_dev1_ioc_fmt_nego
* Description: sink_dev1 参数协商
* Arguments  : iport 输入端口句柄
* Return	 : 节点参数协商结果
* Note(s)    : 目的在于检查与上一个节点的参数是否匹配，不匹配则重新协商;
   			   根据输出节点的参数特性，区分为
   				1、固定参数, 或者通过NODE_IOC_SET_FMT获取fmt信息,
   				协商过程会将此参数向前级节点传递, 直至协商成功;
   				2、可变参数，继承上一节点的参数
*********************************************************************
*/
static int sink_dev1_ioc_fmt_nego(struct stream_iport *iport)
{
    struct stream_fmt *in_fmt = &iport->prev->fmt;	//上一个节点的参数
    struct sink_dev1_hdl *hdl = (struct sink_dev1_hdl *)iport->node->private_data;

    //1、固定节点参数, 向前级节点传递
#if SINK_DEV1_MSBC_TEST_ENABLE
    in_fmt->coding_type = AUDIO_CODING_MSBC;		//数据格式
    in_fmt->sample_rate = 16000;					//采样率
    in_fmt->channel_mode = AUDIO_CH_MIX;			//数据通道
#endif
    in_fmt->coding_type = hdl->fmt.coding_type;
    in_fmt->sample_rate = hdl->fmt.sample_rate;
    in_fmt->channel_mode = hdl->fmt.channel_mode;
    printf("sink_dev1 nego,type=%x,sr=%d,ch_mode=%x\n", in_fmt->coding_type, in_fmt->sample_rate, in_fmt->channel_mode);

    //2、继承前一节点的参数
    /* hdl->fmt = &in_fmt; */
    return NEGO_STA_ACCPTED;
}

static int sink_dev1_ioc_start(struct sink_dev1_hdl *hdl)
{
    printf("sink_dev1_ioc_start");
    sink_dev1_init(hdl);
    return 0;
}

static int sink_dev1_ioc_stop(struct sink_dev1_hdl *hdl)
{
    printf("sink_dev1_ioc_stop");
    sink_dev1_exit(hdl);
    return 0;
}

static int sink_dev1_ioctl(struct stream_iport *iport, int cmd, int arg)
{
    struct sink_dev1_hdl *hdl = (struct sink_dev1_hdl *)iport->node->private_data;

    switch (cmd) {
    case NODE_IOC_OPEN_IPORT:
        sink_dev1_open_iport(iport);
        break;
    case NODE_IOC_START:
        sink_dev1_ioc_start(hdl);
        break;
    case NODE_IOC_STOP:
        sink_dev1_ioc_stop(hdl);
        break;
    case NODE_IOC_SET_FMT:
        //外部传参, 设置当前节点的参数
        /* struct stream_fmt *fmt = (struct stream_fmt *)arg; */
        /* hdl->fmt.coding_type = fmt->coding_type; */
        struct stream_fmt *fmt = (struct stream_fmt *)arg;
        hdl->fmt.coding_type = fmt->coding_type;
        hdl->fmt.channel_mode = fmt->channel_mode;
        hdl->fmt.sample_rate = fmt->sample_rate;
        printf("NODE_IOC_SET_FMT %d %d %d\n", hdl->fmt.coding_type, hdl->fmt.channel_mode, hdl->fmt.sample_rate);
        break;
    case NODE_IOC_NEGOTIATE:
        *(int *)arg |= sink_dev1_ioc_fmt_nego(iport);
        break;
    case NODE_IOC_GET_DELAY:
        break;
    }

    return 0;
}

//释放当前节点资源
static void sink_dev1_release(struct stream_node *node)
{
    printf("sink_dev1_release");
}

REGISTER_STREAM_NODE_ADAPTER(sink_dev1_adapter) = {
    .name       = "sink_dev1",
    .uuid       = NODE_UUID_SINK_DEV1,
    .bind       = sink_dev1_bind,
    .ioctl      = sink_dev1_ioctl,
    .release    = sink_dev1_release,
    .hdl_size   = sizeof(struct sink_dev1_hdl),
};

#endif

