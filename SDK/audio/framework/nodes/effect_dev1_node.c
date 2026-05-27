#include "jlstream.h"
#include "media/audio_base.h"
#include "effects/effects_adj.h"
#include "app_config.h"
#include "effect_dev_node.h"
#include "effects_dev.h"
#include "audio_splicing.h"

#if TCFG_EFFECT_DEV1_NODE_ENABLE

#define LOG_TAG_CONST EFFECTS
#define LOG_TAG     "[EFFECT_dev1-NODE]"
#define LOG_ERROR_ENABLE
#define LOG_INFO_ENABLE
#define LOG_DUMP_ENABLE
#include "debug.h"
#include "Resample_api.h"
#include "st_opus_enc/opus_stenc_api.h"

#ifdef SUPPORT_MS_EXTENSIONS
#pragma   code_seg(".effect_dev1.text")
#pragma   data_seg(".effect_dev1.data")
#pragma  const_seg(".effect_dev1.text.const")
#endif

/* 音效算法处理帧长
 * 0   : 等长输入输出，输入数据算法需要全部处理完
 * 非0 : 按照帧长输入数据到算法处理接口
 */
#define EFFECT_DEV1_FRAME_POINTS  (320) //(256)


/*
 *声道转换类型选配
 *支持立体声转4声道协商使能,须在第三方音效节点后接入声道拆分节点
 * */
#define CHANNEL_ADAPTER_AUTO   0 //自动协商,通常用于无声道数转换的场景,结果随数据流配置自动适配
#define CHANNEL_ADAPTER_2TO4   1 //立体声转4声道协商使能,支持2to4,结果随数据流配置自动适配
#define CHANNEL_ADAPTER_1TO2   2 //单声道转立体声协商使能,支持1to2,结果随数据流配置自动适配
#define CHANNEL_ADAPTER_TYPE   CHANNEL_ADAPTER_AUTO //默认无声道转换

struct effect_dev1_node_hdl {
    char name[16];
    void *user_priv;//用户可使用该变量做模块指针传递
    struct user_effect_tool_param cfg;//工具界面参数
    struct packet_ctrl dev;
    RS_STUCT_API *sw_src_api;   //变采样接口
    void *sw_src_buf;           //变采样接口
    u16 src_in_sr;              //变采样输入采样率
    u8 src_busy;
};

static u16 input_sr = 44100;//变采样输入采样率
static s16 *temp;//[FRAME_POINT * 2];
/**
 * @brief 重新设置输入采样率
 * 
 * @param in_sr 
 */
void src1_reset_sw_src_in_sample_rate(u16 in_sr)
{
    // static u16 sample = 44100 / 1000;
    // if((sample == in_sr / 1000) || in_sr == 0){
    //     return;
    // }
    if(input_sr != in_sr){
        printf("%s %d -> %d\n", __func__, input_sr, in_sr);
    }
    // sample = in_sr / 1000;
    input_sr = in_sr;
}

/* 自定义算法，初始化
 * hdl->dev.sample_rate:采样率
 * hdl->dev.in_ch_num:通道数，单声道 1，立体声 2, 四声道 4
 * hdl->dev.out_ch_num:通道数，单声道 1，立体声 2, 四声道 4
 * hdl->dev.bit_width:位宽 0，16bit  1，32bit
 **/
static void audio_effect_dev1_init(struct effect_dev1_node_hdl *hdl)
{
    //do something
    if(hdl->dev.sample_rate == OPUS_SR){
        return;
    }
    hdl->sw_src_api = get_rs16_context();
    printf("sw_src_api:0x%x\n", hdl->sw_src_api);
    ASSERT(hdl->sw_src_api);
    u32 sw_src_need_buf = hdl->sw_src_api->need_buf();
    printf("sw_src_buf:%d\n", sw_src_need_buf);
    hdl->sw_src_buf = malloc(sw_src_need_buf);
    temp = malloc(FRAME_POINT * 2 * 2);
    ASSERT(hdl->sw_src_buf);
    RS_PARA_STRUCT rs_para_obj;
    rs_para_obj.nch = 1;
    rs_para_obj.new_insample = hdl->dev.sample_rate;
    rs_para_obj.new_outsample = OPUS_SR;
    rs_para_obj.dataTypeobj.IndataBit = 0;
    rs_para_obj.dataTypeobj.OutdataBit = 0;
    rs_para_obj.dataTypeobj.IndataInc = (rs_para_obj.nch == 1) ? 1 : 2;
    rs_para_obj.dataTypeobj.OutdataInc = (rs_para_obj.nch == 1) ? 1 : 2;
    rs_para_obj.dataTypeobj.Qval = AUDIO_QVAL_16BIT;
    hdl->src_in_sr = hdl->dev.sample_rate;
    hdl->src_busy = 0;
    printf("sw src,ch = %d, in = %d,out = %d\n", rs_para_obj.nch, rs_para_obj.new_insample, rs_para_obj.new_outsample);
    hdl->sw_src_api->open(hdl->sw_src_buf, &rs_para_obj);
}

#include "audio_splicing.h"
static s16 mono_buff[512] ALIGNED(4);
u32 source_dev1_input_write(u8 idx, u8 *data, u16 len);
void src1_reset_sw_src_in_sample_rate(u16 in_sr);
u8 source_dev1_get_idx_enable(u8 idx);
/* 自定义算法，运行
 * hdl->dev.sample_rate:采样率
 * hdl->dev.in_ch_num:通道数，单声道 1，立体声 2, 四声道 4
 * hdl->dev.out_ch_num:通道数，单声道 1，立体声 2, 四声道 4
 * hdl->dev.bit_width:位宽 0，16bit  1，32bit
 * *indata:输入数据地址
 * *outdata:输出数据地址
 * indata_len :输入数据长度,byte
 * */
static u32 audio_effect_dev1_run(struct effect_dev1_node_hdl *hdl,  s16 *indata, s16 *outdata, u32 indata_len)
{
#if 0
    //test 2to4
    if (hdl->dev.bit_width && ((hdl->dev.out_ch_num == 4) && (hdl->dev.in_ch_num == 2))) {
        pcm_dual_to_qual_with_slience_32bit(outdata, indata, indata_len, 0);
    }
#endif
    //do something
    u8 *data = (u8 *)indata;
    u32 data_len = indata_len;

    if(hdl == NULL || source_dev1_get_idx_enable(1) == 0){
        return indata_len;
    }
    u8 ch_num = hdl->dev.in_ch_num;
    u16 sample_rate = hdl->dev.sample_rate;
    if(ch_num == 2){
        u16 point = (data_len >> 1) / ch_num;
        pcm_dual_to_single(mono_buff, data, data_len);
        data = mono_buff;
        data_len = point << 1;
    }
    if(ch_num == 4){
        u16 point = (data_len >> 1) / ch_num;
        pcm_qual_to_single(mono_buff, data, data_len);
        data = mono_buff;
        data_len = point << 1;
    }
    // src1_reset_sw_src_in_sample_rate(sample_rate);
    u16 len = data_len;
    hdl->src_busy = 1;
    if(hdl->sw_src_api && hdl->sw_src_buf && len){//变采样流程
        if(hdl->src_in_sr != sample_rate){
            printf("hdl->sw_src_api->set_sr %d -> %d\n", hdl->src_in_sr, sample_rate);
            hdl->src_in_sr = sample_rate;
            hdl->sw_src_api->set_sr(hdl->sw_src_buf, hdl->src_in_sr);//更新采样率
        }
        data_len = hdl->sw_src_api->run(hdl->sw_src_buf, data, len >> 1, temp);
        data_len <<= 1;
        data = temp;
    }
    hdl->src_busy = 0;
    source_dev1_input_write(1, data, data_len);
    /* printf("effect dev1 do something here\n"); */
    return indata_len;
}
/* 自定义算法，关闭
 **/
static void audio_effect_dev1_exit(struct effect_dev1_node_hdl *hdl)
{
    //do something
    // OS_ENTER_CRITICAL();
    int wait = 0;
    while(hdl && hdl->src_busy){
        os_time_dly(1);
        wait++;
        if(wait > 10){
            break;
        }
    }
    if(hdl && hdl->sw_src_buf){
        void *p = hdl->sw_src_buf;
        hdl->sw_src_buf = NULL;
        hdl->sw_src_api = NULL;
        free(p);
    }
    input_sr = 44100;
    if(temp){
        free(temp);
        temp = NULL;
    }
    // OS_EXIT_CRITICAL();
}


/* 自定义算法，更新参数
 **/
static void audio_effect_dev1_update(struct effect_dev1_node_hdl *hdl)
{
    //打印在线调音发送下来的参数
    printf("effect dev1 name : %s \n", hdl->name);
    for (int i = 0 ; i < 8; i++) {
        printf("cfg.int_param[%d] %d\n", i, hdl->cfg.int_param[i]);
    }
    for (int i = 0 ; i < 8; i++) {
        printf("cfg.float_param[%d] %d.%02d\n", i, (int)hdl->cfg.float_param[i], debug_digital(hdl->cfg.float_param[i]));
    }
    //do something

}
/*节点输出回调处理，可处理数据或post信号量*/
static void effect_dev1_handle_frame(struct stream_iport *iport, struct stream_note *note)
{

    struct effect_dev1_node_hdl *hdl = (struct effect_dev1_node_hdl *)iport->node->private_data;


    effect_dev_process(&hdl->dev, iport,  note); //音效处理
}

/*节点预处理-在ioctl之前*/
static int effect_dev1_adapter_bind(struct stream_node *node, u16 uuid)
{
    struct effect_dev1_node_hdl *hdl = (struct effect_dev1_node_hdl *)node->private_data;

    memset(hdl, 0, sizeof(*hdl));
    return 0;
}

/*打开改节点输入接口*/
static void effect_dev1_ioc_open_iport(struct stream_iport *iport)
{
    iport->handle_frame = effect_dev1_handle_frame;				//注册输出回调
}

/*节点参数协商*/
static int effect_dev1_ioc_negotiate(struct stream_iport *iport)
{
    int ret = 0;
    ret = NEGO_STA_ACCPTED;
    struct stream_oport *oport = iport->node->oport;
    struct stream_fmt *in_fmt = &iport->prev->fmt;
    struct effect_dev1_node_hdl *hdl = (struct effect_dev1_node_hdl *)iport->node->private_data;

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
    printf(" effecs_dev1 in_ch_num %d, out_ch_num %d\n", hdl->dev.in_ch_num, hdl->dev.out_ch_num);


    return ret;
}

/*节点start函数*/
static void effect_dev1_ioc_start(struct effect_dev1_node_hdl *hdl)
{
    struct stream_fmt *fmt = &hdl_node(hdl)->oport->fmt;
    /* struct jlstream *stream = jlstream_for_node(hdl_node(hdl)); */


    hdl->dev.sample_rate = fmt->sample_rate;


    /*
     *获取配置文件内的参数,及名字
     * */
    int len = jlstream_read_node_data_new(hdl_node(hdl)->uuid, hdl_node(hdl)->subid, (void *)&hdl->cfg, hdl->name);
    if (!len) {
        log_error("%s, read node data err\n", __FUNCTION__);
        return;
    }

    /*
     *获取在线调试的临时参数
     * */
    if (config_audio_cfg_online_enable) {
        if (jlstream_read_effects_online_param(hdl_node(hdl)->uuid, hdl->name, &hdl->cfg, sizeof(hdl->cfg))) {
            log_debug("get effect dev1 online param\n");
        }
    }
    printf("effect dev1 name : %s \n", hdl->name);
    for (int i = 0 ; i < 8; i++) {
        printf("cfg.int_param[%d] %d\n", i, hdl->cfg.int_param[i]);
    }
    for (int i = 0 ; i < 8; i++) {
        printf("cfg.float_param[%d] %d.%02d\n", i, (int)hdl->cfg.float_param[i], debug_digital(hdl->cfg.float_param[i]));
    }

    hdl->dev.bit_width = hdl_node(hdl)->iport->prev->fmt.bit_wide;
    hdl->dev.qval = hdl_node(hdl)->iport->prev->fmt.Qval;
    printf("effect_dev1_ioc_start, sr: %d, in_ch: %d, out_ch: %d, bitw: %d %d", hdl->dev.sample_rate, hdl->dev.in_ch_num, hdl->dev.out_ch_num, hdl->dev.bit_width, hdl->dev.qval);


    hdl->dev.node_hdl = hdl;
    hdl->dev.effect_run = (u32 (*)(void *, s16 *, s16 *, u32))audio_effect_dev1_run;
    effect_dev_init(&hdl->dev, EFFECT_DEV1_FRAME_POINTS);
    audio_effect_dev1_init(hdl);
}


/*节点stop函数*/
static void effect_dev1_ioc_stop(struct effect_dev1_node_hdl *hdl)
{
    audio_effect_dev1_exit(hdl);
    effect_dev_close(&hdl->dev);
}

static int effect_dev1_ioc_update_parm(struct effect_dev1_node_hdl *hdl, int parm)
{
    int ret = false;
    return ret;
}
static int get_effect_dev1_ioc_parm(struct effect_dev1_node_hdl *hdl, int parm)
{
    int ret = 0;
    return ret;
}

static int effect_ioc_update_parm(struct effect_dev1_node_hdl *hdl, int parm)
{
    int ret = false;
    struct user_effect_tool_param *cfg = (struct user_effect_tool_param *)parm;
    if (hdl) {
        memcpy(&hdl->cfg, cfg, sizeof(struct user_effect_tool_param));

        audio_effect_dev1_update(hdl);

        ret = true;
    }

    return ret;
}
/*节点ioctl函数*/
static int effect_dev1_adapter_ioctl(struct stream_iport *iport, int cmd, int arg)
{
    int ret = 0;
    struct effect_dev1_node_hdl *hdl = (struct effect_dev1_node_hdl *)iport->node->private_data;

    switch (cmd) {
    case NODE_IOC_OPEN_IPORT:
        effect_dev1_ioc_open_iport(iport);
        break;
    case NODE_IOC_OPEN_OPORT:
        break;
    case NODE_IOC_CLOSE_IPORT:
        break;
    case NODE_IOC_SET_SCENE:
        break;
    case NODE_IOC_NEGOTIATE:
        *(int *)arg |= effect_dev1_ioc_negotiate(iport);
        break;
    case NODE_IOC_START:
        effect_dev1_ioc_start(hdl);
        break;
    case NODE_IOC_SUSPEND:
    case NODE_IOC_STOP:
        effect_dev1_ioc_stop(hdl);
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
static void effect_dev1_adapter_release(struct stream_node *node)
{
}

/*节点adapter 注意需要在sdk_used_list声明，否则会被优化*/
REGISTER_STREAM_NODE_ADAPTER(effect_dev1_node_adapter) = {
    .name       = "effect_dev1",
    .uuid       = NODE_UUID_EFFECT_DEV1,
    .bind       = effect_dev1_adapter_bind,
    .ioctl      = effect_dev1_adapter_ioctl,
    .release    = effect_dev1_adapter_release,
    .hdl_size   = sizeof(struct effect_dev1_node_hdl),
#if (CHANNEL_ADAPTER_TYPE != CHANNEL_ADAPTER_AUTO)
    .ability_channel_out = 0x80 | 1 | 2 | 4,
    .ability_channel_convert = 1,
#endif

};

REGISTER_ONLINE_ADJUST_TARGET(effect_dev1) = {
    .uuid = NODE_UUID_EFFECT_DEV1,
};

#endif

