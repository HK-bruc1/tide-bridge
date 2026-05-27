#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".icsd_aeq.data.bss")
#pragma data_seg(".icsd_aeq.data")
#pragma const_seg(".icsd_aeq.text.const")
#pragma code_seg(".icsd_aeq.text")
#endif

#include "app_config.h"
#include "audio_config_def.h"

#if (TCFG_AUDIO_ADAPTIVE_EQ_ENABLE && TCFG_AUDIO_ANC_ENABLE && \
	 TCFG_AUDIO_ANC_EXT_VERSION == ANC_EXT_V2) && \
	(defined TCFG_EQ_ENABLE)

#include "icsd_aeq_app.h"
#include "icsd_aeq_config.h"
#include "audio_anc.h"
#include "jlstream.h"
#include "node_uuid.h"
#include "clock_manager/clock_manager.h"
#include "audio_config.h"
#include "volume_node.h"
#include "audio_anc_debug_tool.h"
#include "icsd_aeq.h"
#include "anc_ext_tool.h"
#include "a2dp_player.h"

#if TCFG_USER_TWS_ENABLE
#include "bt_tws.h"
#endif/*TCFG_USER_TWS_ENABLE*/

#if TCFG_AUDIO_FIT_DET_ENABLE
#include "icsd_dot_app.h"
#endif

#if TCFG_AUDIO_ANC_EAR_ADAPTIVE_EN
#include "icsd_anc_v2_interactive.h"
#endif

#if TCFG_AUDIO_ANC_REAL_TIME_ADAPTIVE_ENABLE
#include "rt_anc_app.h"
#endif

#define ADAPTIVE_EQ_OUTPUT_IN_CUR_VOL_EN		0	//AEQ只根据当前音量生成一组AEQ，其他音量用默认EQ

#define ADAPTIVE_EQ_RUN_TIME_DEBUG				0	//AEQ 算法运行时间测试

#if 0
#define aeq_printf printf
#else
#define aeq_printf(...)
#endif

struct audio_aeq_t {
    u8 eff_mode;							//AEQ效果模式
    u8 real_time_eq_en;						//实时自适应EQ使能
    enum audio_adaptive_fre_sel fre_sel;	//AEQ数据来源
    volatile u8 state;						//状态
    u8 run_busy;							//运行繁忙标志
    s16 now_volume;							//当前EQ参数的音量
    void *lib_alloc_ptr;					//AEQ申请的内存指针
    float *sz_ref;							//金机参考SZ
    float *sz_dut_cmp;						//产测补偿SZ
    struct list_head head;
    spinlock_t lock;
    struct audio_afq_output *fre_out;		//实时SZ输出句柄
    struct eq_default_seg_tab *eq_ref;		//参考EQ
    void (*result_cb)(int msg);			//AEQ训练结束回调
    OS_MUTEX mutex;
    struct __anc_ext_adaptive_eq_thr thr;
    //工具数据
    anc_packet_data_t *tool_debug_data;
    struct anc_ext_subfile_head *tool_sz;
};

struct audio_aeq_bulk {
    struct list_head entry;
    s16 volume;
    struct eq_default_seg_tab *eq_output;
};

/*
   AEQ 根据音量分档表，音量从小到大排列；
   一般建议分3档以下，档位越多，AEQ时间越长，需要空间更大
*/
u8 aeq_volume_grade_list[] = {
    5,	//0-5
    10,	//6-10
    16,	//11-16
};

#if 0
u8 aeq_volume_grade_maxdB_table[3][3] = {
    {
        //佩戴-紧
        22,//0-5
        12,//6-10
        3, //11-16
    },
    {
        //佩戴-正常
        20,
        14,
        2,
    },
    {
        //佩戴-松
        15,
        10,
        1,
    },
};
#endif

static const struct eq_seg_info test_eq_tab_normal[] = {
    {0, EQ_IIR_TYPE_BAND_PASS, 31,    0, 0.7f},
    {1, EQ_IIR_TYPE_BAND_PASS, 62,    0, 0.7f},
    {2, EQ_IIR_TYPE_BAND_PASS, 125,   0, 0.7f},
    {3, EQ_IIR_TYPE_BAND_PASS, 250,   0, 0.7f},
    {4, EQ_IIR_TYPE_BAND_PASS, 500,   0, 0.7f},
    {5, EQ_IIR_TYPE_BAND_PASS, 1000,  0, 0.7f},
    {6, EQ_IIR_TYPE_BAND_PASS, 2000,  0, 0.7f},
    {7, EQ_IIR_TYPE_BAND_PASS, 4000,  0, 0.7f},
    {8, EQ_IIR_TYPE_BAND_PASS, 8000,  0, 0.7f},
    {9, EQ_IIR_TYPE_BAND_PASS, 16000, 0, 0.7f},
};

static struct audio_aeq_t *aeq_hdl = NULL;
struct eq_default_seg_tab default_seg_tab = {
    .global_gain = 0.0f,
    .seg_num = ARRAY_SIZE(test_eq_tab_normal),
    .seg = (struct eq_seg_info *)(test_eq_tab_normal),
};

static void audio_adaptive_eq_afq_output_hdl(struct audio_afq_output *p);
static struct eq_default_seg_tab *audio_adaptive_eq_cur_list_query(s16 volume);
static float audio_adaptive_eq_vol_gain_get(s16 volume);
static struct eq_default_seg_tab *audio_icsd_eq_default_tab_read();
static int audio_icsd_eq_eff_update(struct eq_default_seg_tab *eq_tab);
static void audio_adaptive_eq_sz_data_packet(struct audio_afq_output *p);
static int audio_adaptive_eq_node_check(void);

//保留现场及功能互斥
static void audio_adaptive_eq_mutex_suspend(void)
{
    if (aeq_hdl) {
    }
}

//恢复现场及功能互斥
static void audio_adaptive_eq_mutex_resume(void)
{
    if (aeq_hdl) {
    }
}

static int audio_adaptive_eq_permit(enum audio_adaptive_fre_sel fre_sel)
{
    if (anc_ext_ear_adaptive_cfg_get()->aeq_gains == NULL) {
        printf("ERR: ANC_EXT adaptive eq cfg no enough!\n");
        return ANC_EXT_OPEN_FAIL_CFG_MISS;
    }
#if TCFG_AUDIO_DAC_CONNECT_MODE == DAC_OUTPUT_LR
    if (anc_ext_ear_adaptive_cfg_get()->raeq_gains == NULL) {
        printf("ERR: ANC_EXT adaptive eq cfg no enough!\n");
        return ANC_EXT_OPEN_FAIL_CFG_MISS;
    }
#endif
    if (aeq_hdl->state != ADAPTIVE_EQ_STATE_CLOSE) {
        return ANC_EXT_OPEN_FAIL_REENTRY;
    }
    if (audio_adaptive_eq_node_check()) {
        printf("ERR: adaptive eq node is missing!\n");
        return ANC_EXT_OPEN_FAIL_CFG_MISS;
    }
    return 0;
}

int audio_adaptive_eq_init(void)
{
    aeq_hdl = zalloc(sizeof(struct audio_aeq_t));
    spin_lock_init(&aeq_hdl->lock);
    INIT_LIST_HEAD(&aeq_hdl->head);
    os_mutex_create(&aeq_hdl->mutex);
    ASSERT(aeq_hdl);
    aeq_hdl->eff_mode = AEQ_EFF_MODE_ADAPTIVE;
    return 0;
}

int audio_adaptive_eq_open(enum audio_adaptive_fre_sel fre_sel, void (*result_cb)(int msg))
{
    int ret = audio_adaptive_eq_permit(fre_sel);
    if (ret) {
        aeq_printf("adaptive_eq_permit, open fail %d\n", ret);
        return ret;
    }

    aeq_printf("ICSD_AEQ_STATE:OPEN, real time %d, fre_sel %d\n", aeq_hdl->real_time_eq_en, fre_sel);
    mem_stats();

    //1.保留现场及功能互斥
    audio_adaptive_eq_mutex_suspend();

    if (!aeq_hdl->real_time_eq_en) {	//非实时自适应EQ才申请时钟
        clock_alloc("ANC_AEQ", 160 * 1000000L);
    }

    aeq_hdl->state = ADAPTIVE_EQ_STATE_OPEN;
    aeq_hdl->fre_sel = fre_sel;
    aeq_hdl->result_cb = result_cb;

    //2.准备算法输入参数
    if (aeq_hdl->eq_ref) {
        free(aeq_hdl->eq_ref);
    }
    aeq_hdl->eq_ref = malloc(sizeof(struct eq_default_seg_tab));
    memcpy(aeq_hdl->eq_ref, &default_seg_tab, sizeof(struct eq_default_seg_tab));

    audio_icsd_eq_eff_update(aeq_hdl->eq_ref);
    //输入算法前，dB转线性值
    aeq_hdl->eq_ref->global_gain = eq_db2mag(aeq_hdl->eq_ref->global_gain);

    //3. SZ获取
    audio_afq_common_add_output_handler("ANC_AEQ", fre_sel, audio_adaptive_eq_afq_output_hdl);

    //cppcheck-suppress memleak
    return 0;
}

//(实时)自适应EQ打开
int audio_real_time_adaptive_eq_open(enum audio_adaptive_fre_sel fre_sel, void (*result_cb)(int result))
{
    aeq_hdl->real_time_eq_en = 1;
    int ret = audio_adaptive_eq_open(fre_sel, result_cb);
    //打开失败且非重入场景则清0对应标志
    if (ret && (ret != ANC_EXT_OPEN_FAIL_REENTRY)) {
        aeq_hdl->real_time_eq_en = 0;
    }
    return ret;
}

//(实时)自适应EQ退出
int audio_real_time_adaptive_eq_close(void)
{
    if (aeq_hdl->state == ADAPTIVE_EQ_STATE_CLOSE) {
        return 0;
    }
    aeq_printf("ICSD_AEQ_STATE:FORCE_EXIT");
    aeq_hdl->state = ADAPTIVE_EQ_STATE_FORCE_EXIT;
    if (aeq_hdl->run_busy) {
        icsd_aeq_force_exit();  // RUN过程强制退出，之后执行CLOSE
    } else {
        audio_adaptive_eq_close();
    }
    return 0;
}

int audio_adaptive_eq_close()
{
    aeq_printf("%s\n", __func__);
    if (aeq_hdl) {
        os_mutex_pend(&aeq_hdl->mutex, 0);
        if (aeq_hdl->state != ADAPTIVE_EQ_STATE_CLOSE) {
            if (strcmp(os_current_task(), "afq_common") == 0) {
                //aeq close在AEQ线程执行会造成死锁，需改为在APP任务执行
                aeq_printf("aeq close post to app_core\n");
                int msg[2];
                msg[0] = (int)audio_adaptive_eq_close;
                msg[1] = 1;
                int ret = os_taskq_post_type("app_core", Q_CALLBACK, 2, msg);
                if (ret) {
                    aeq_printf("aeq taskq_post err\n");
                }
                os_mutex_post(&aeq_hdl->mutex);
                return 0;
            }
            //删除频响来源回调，若来源结束，则关闭来源
            audio_afq_common_del_output_handler("ANC_AEQ");

            aeq_printf("%s, ok\n", __func__);
            //恢复ANC相关状态
            audio_adaptive_eq_mutex_resume();

            if (aeq_hdl->eq_ref) {
                free(aeq_hdl->eq_ref);
                aeq_hdl->eq_ref = NULL;
            }
            clock_free("ANC_AEQ");
            aeq_hdl->run_busy = 0;

            //在线更新EQ效果
            if (aeq_hdl->eff_mode == AEQ_EFF_MODE_ADAPTIVE) {
                s16 volume = app_audio_get_volume(APP_AUDIO_STATE_MUSIC);
                aeq_hdl->now_volume = volume;
                audio_icsd_eq_eff_update(audio_adaptive_eq_cur_list_query(volume));
            }
            aeq_hdl->real_time_eq_en = 0;

            aeq_hdl->state = ADAPTIVE_EQ_STATE_CLOSE;
            aeq_printf("ICSD_AEQ_STATE:CLOSE");

            if (aeq_hdl->result_cb) {
                aeq_hdl->result_cb(0);
            }
        }
        os_mutex_post(&aeq_hdl->mutex);
    }
    return 0;
}

//查询AEQ的状态
u8 audio_adaptive_eq_state_get(void)
{
    if (aeq_hdl) {
        return aeq_hdl->state;
    }
    return ADAPTIVE_EQ_STATE_CLOSE;
}


//查询aeq是否活动中
u8 audio_adaptive_eq_is_running(void)
{
    if (aeq_hdl) {
        return aeq_hdl->run_busy;
    }
    return 0;
}

//立即更新EQ参数
static int audio_icsd_eq_eff_update(struct eq_default_seg_tab *eq_tab)
{
    if (!eq_tab) {
        return 1;
    }

    aeq_printf("%s\n", __func__);
    if (cpu_in_irq()) {
        aeq_printf("AEQ_eff update post app_core\n");
        int msg[3];
        msg[0] = (int)audio_icsd_eq_eff_update;
        msg[1] = 1;
        msg[2] = (int)eq_tab;
        int ret = os_taskq_post_type("app_core", Q_CALLBACK, 3, msg);
        if (ret) {
            aeq_printf("aeq taskq_post err\n");
        }
        return 0;
    }
#if 0
    aeq_printf("global_gain %d/100\n", (int)(eq_tab->global_gain * 100.f));
    aeq_printf("seg_num %d\n", eq_tab->seg_num);
    for (int i = 0; i < eq_tab->seg_num; i++) {
        aeq_printf("index %d\n", eq_tab->seg[i].index);
        aeq_printf("iir_type %d\n", eq_tab->seg[i].iir_type);
        aeq_printf("freq %d\n", eq_tab->seg[i].freq);
        aeq_printf("gain %d/100\n", (int)(eq_tab->seg[i].gain * 100.f));
        aeq_printf("q %d/100\n", (int)(eq_tab->seg[i].q * 100.f));
    }
#endif
    /*
     *更新用户自定义总增益
     * */
    struct eq_adj eff = {0};
    char *node_name = ADAPTIVE_EQ_TARGET_NODE_NAME;//节点名称（节点内的第一参数，用户自定义,长度小于等于15byte）
    eff.param.global_gain =  eq_tab->global_gain;       //设置总增益 -5dB
    eff.fade_parm.fade_time = 10;      //ms，淡入timer执行的周期
    eff.fade_parm.fade_step = 0.1f;    //淡入步进

    eff.type = EQ_GLOBAL_GAIN_CMD;      //参数类型：总增益
    int ret = jlstream_set_node_param(NODE_UUID_EQ, node_name, &eff, sizeof(eff));
    if (!ret) {
        puts("error\n");
    }
    /*
     *更新系数表段数(如系数表段数发生改变，需要用以下处理，更新系数表段数)
     * */
    eff.param.seg_num = eq_tab->seg_num;           //系数表段数
    eff.type = EQ_SEG_NUM_CMD;         //参数类型：系数表段数
    ret = jlstream_set_node_param(NODE_UUID_EQ, node_name, &eff, sizeof(eff));
    if (!ret) {
        puts("error\n");
    }
    /*
     *更新用户自定义的系数表
     * */
    eff.type = EQ_SEG_CMD;             //参数类型：滤波器参数
    for (int j = 0; j < eq_tab->seg_num; j++) {
        memcpy(&eff.param.seg, &(eq_tab->seg[j]), sizeof(struct eq_seg_info));//滤波器参数
        jlstream_set_node_param(NODE_UUID_EQ, node_name, &eff, sizeof(eff));
    }
    return 0;
}

//检查是否有AEQ节点
static int audio_adaptive_eq_node_check(void)
{
    struct cfg_info info = {0};
    char *node_name = ADAPTIVE_EQ_TARGET_NODE_NAME;
    return jlstream_read_form_node_info_base(0, node_name, 0, &info);
}

/*
   读取可视化工具stream.bin的配置;
   如复用EQ节点，若用户用EQ节点，并自定义参数，需要在更新参数的时候通知AEQ算法
 */
static struct eq_default_seg_tab *audio_icsd_eq_default_tab_read()
{
#if ADAPTIVE_EQ_TARGET_DEFAULT_CFG_READ
    /*
     *解析配置文件内效果配置
     * */
    struct eq_default_seg_tab *eq_ref = NULL;
    struct cfg_info info  = {0};             //节点配置相关信息（参数存储的目标地址、配置项大小）
    char mode_index       = 0;               //模式序号（当前节点无多参数组，mode_index是0）
    char *node_name       = ADAPTIVE_EQ_TARGET_NODE_NAME;       //节点名称（节点内的第一参数，用户自定义,长度小于等于15byte）
    char cfg_index        = 0;               //目标配置项序号（当前节点无多参数组，cfg_index是0）
    int ret = jlstream_read_form_node_info_base(mode_index, node_name, cfg_index, &info);
    if (!ret) {
        struct eq_tool *tab = zalloc(info.size);
        if (!jlstream_read_form_cfg_data(&info, tab)) {
            printf("[AEQ]user eq cfg parm read err\n");
            free(tab);
            return &default_seg_tab;
        }
#if 0
        aeq_printf("global_gain %d/100\n", (int)(tab->global_gain * 100.f));
        aeq_printf("is_bypass %d\n", tab->is_bypass);
        aeq_printf("seg_num %d\n", tab->seg_num);
        for (int i = 0; i < tab->seg_num; i++) {
            aeq_printf("index %d\n", tab->seg[i].index);
            aeq_printf("iir_type %d\n", tab->seg[i].iir_type);
            aeq_printf("freq %d\n", tab->seg[i].freq);
            aeq_printf("gain %d/100\n", (int)(tab->seg[i].gain * 100.f));
            aeq_printf("q %d/100\n", (int)(tab->seg[i].q * 100.f));
        }
#endif
        //读取EQ格式转换
        eq_ref = malloc(sizeof(struct eq_default_seg_tab));
        int seg_size = sizeof(struct eq_seg_info) * tab->seg_num;
        eq_ref->seg = malloc(seg_size);
        eq_ref->global_gain = tab->global_gain;
        eq_ref->seg_num = tab->seg_num;
        memcpy(eq_ref->seg, tab->seg, seg_size);

        free(tab);
    } else {
        printf("[AEQ]user eq cfg parm read err ret %d\n", ret);
        return &default_seg_tab;
    }

    return eq_ref;
#else

    return &default_seg_tab;
#endif
}

//读取自适应AEQ结果 - 用于播歌更新效果
struct eq_default_seg_tab *audio_icsd_adaptive_eq_read(void)
{
    struct eq_default_seg_tab *eq_tab;
    if (aeq_hdl) {	//自适应模式且没有训练中，才允许读取
        if (aeq_hdl->eff_mode == AEQ_EFF_MODE_ADAPTIVE && \
            (!audio_adaptive_eq_is_running())) {
            s16 volume = app_audio_get_volume(APP_AUDIO_STATE_MUSIC);
            eq_tab = audio_adaptive_eq_cur_list_query(volume);
            aeq_printf("%s\n", __func__);
#if 0
            aeq_printf("global_gain %d/100\n", (int)(eq_tab->global_gain * 100.f));
            aeq_printf("seg_num %d\n", eq_tab->seg_num);
            for (int i = 0; i < eq_tab->seg_num; i++) {
                aeq_printf("index %d\n", eq_tab->seg[i].index);
                aeq_printf("iir_type %d\n", eq_tab->seg[i].iir_type);
                aeq_printf("freq %d\n", eq_tab->seg[i].freq);
                aeq_printf("gain %d/100\n", (int)(eq_tab->seg[i].gain * 100.f));
                aeq_printf("q %d/100\n", (int)(eq_tab->seg[i].q * 100.f));
            }
#endif
            return eq_tab;
        }
    }
    return NULL;	//表示使用默认参数
}

static void audio_adaptive_eq_sz_data_packet(struct audio_afq_output *p)
{
    if (!anc_ext_tool_online_get()) {
        return;
    }
    if (aeq_hdl->tool_sz) {
        free(aeq_hdl->tool_sz);
    }
    aeq_hdl->tool_sz = anc_ext_subfile_catch_init(FILE_ID_ANC_EXT_REF_SZ_DATA);
    aeq_hdl->tool_sz = anc_ext_subfile_catch(aeq_hdl->tool_sz, (u8 *)(p->sz_l.out), AEQ_FLEN * sizeof(float), ANC_EXT_REF_SZ_DATA_ID);
}

int audio_adaptive_eq_tool_sz_data_get(u8 **buf, u32 *len)
{
    if (aeq_hdl->tool_sz == NULL) {
        printf("AEQ sz packet is NULL, return!\n");
        return -1;
    }
    *buf = (u8 *)(aeq_hdl->tool_sz);
    *len = aeq_hdl->tool_sz->file_len;
    return 0;
}

static void audio_adaptive_eq_data_packet(_aeq_output *aeq_output, float *sz, float *fgq, int cnt, __adpt_aeq_cfg *aeq_cfg)
{

    if (!anc_ext_tool_online_get()) {
        return;
    }
    int len = AEQ_FLEN / 2;
    int out_seg_num =  ADAPTIVE_EQ_ORDER;
    int eq_dat_len =  sizeof(anc_fr_t) * out_seg_num + 4;
    u8 tmp_type[out_seg_num];
    u8 *leq_dat = zalloc(eq_dat_len);
    for (int i = 0; i < out_seg_num; i++) {
        tmp_type[i] = aeq_cfg->type[i];
    }

    /* for (int i = 0; i < 120; i++) { */
    /* printf("target:%d\n", (int)aeq_output->target[i]); */
    /* } */
    /* for (int i = 0; i < 60; i++) { */
    /* printf("freq:%d\n", (int)aeq_output->h_freq[i]); */
    /* } */
    audio_anc_fr_format(leq_dat, fgq, out_seg_num, tmp_type);

#if ADAPTIVE_EQ_OUTPUT_IN_CUR_VOL_EN
    aeq_hdl->tool_debug_data = anc_data_catch(aeq_hdl->tool_debug_data, NULL, 0, 0, 1);
#else
    if (cnt == 0) {
        aeq_hdl->tool_debug_data = anc_data_catch(aeq_hdl->tool_debug_data, NULL, 0, 0, 1);
    }
#endif
    if (cnt == 0) {
#if TCFG_USER_TWS_ENABLE
        if (0) {
            /* if (bt_tws_get_local_channel() == 'R') { */
            aeq_printf("AEQ-adaptive send data, ch:R\n");
            aeq_hdl->tool_debug_data = anc_data_catch(aeq_hdl->tool_debug_data, (u8 *)aeq_output->h_freq, len * 4, ANC_R_ADAP_FRE, 0);
            aeq_hdl->tool_debug_data = anc_data_catch(aeq_hdl->tool_debug_data, (u8 *)sz, len * 8, ANC_R_ADAP_SZPZ, 0);
            aeq_hdl->tool_debug_data = anc_data_catch(aeq_hdl->tool_debug_data, (u8 *)aeq_output->target, len * 8, ANC_R_ADAP_TARGET, 0);
            aeq_hdl->tool_debug_data = anc_data_catch(aeq_hdl->tool_debug_data, (u8 *)leq_dat, eq_dat_len, AEQ_R_IIR_VOL_LOW, 0);
        } else
#endif
        {
            aeq_printf("AEQ-adaptive send data, ch:L\n");
            aeq_hdl->tool_debug_data = anc_data_catch(aeq_hdl->tool_debug_data, (u8 *)aeq_output->h_freq, len * 4, ANC_L_ADAP_FRE, 0);
            aeq_hdl->tool_debug_data = anc_data_catch(aeq_hdl->tool_debug_data, (u8 *)sz, len * 8, ANC_L_ADAP_SZPZ, 0);
            aeq_hdl->tool_debug_data = anc_data_catch(aeq_hdl->tool_debug_data, (u8 *)aeq_output->target, len * 8, ANC_L_ADAP_TARGET, 0);
            aeq_hdl->tool_debug_data = anc_data_catch(aeq_hdl->tool_debug_data, (u8 *)leq_dat, eq_dat_len, AEQ_L_IIR_VOL_LOW, 0);
        }
    } else if (cnt == 1) {
#if TCFG_USER_TWS_ENABLE
        if (0) {
            /* if (bt_tws_get_local_channel() == 'R') { */
            aeq_printf("AEQ-adaptive send data, ch:R\n");
            aeq_hdl->tool_debug_data = anc_data_catch(aeq_hdl->tool_debug_data, (u8 *)leq_dat, eq_dat_len, AEQ_R_IIR_VOL_MIDDLE, 0);
        } else
#endif
        {
            aeq_printf("AEQ-adaptive send data, ch:L\n");
            aeq_hdl->tool_debug_data = anc_data_catch(aeq_hdl->tool_debug_data, (u8 *)leq_dat, eq_dat_len, AEQ_L_IIR_VOL_MIDDLE, 0);
        }
    } else if (cnt == 2) {
#if TCFG_USER_TWS_ENABLE
        if (0) {
            /* if (bt_tws_get_local_channel() == 'R') { */
            aeq_printf("AEQ-adaptive send data, ch:R\n");
            aeq_hdl->tool_debug_data = anc_data_catch(aeq_hdl->tool_debug_data, (u8 *)leq_dat, eq_dat_len, AEQ_R_IIR_VOL_HIGH, 0);
        } else
#endif
        {
            aeq_printf("AEQ-adaptive send data, ch:L\n");
            aeq_hdl->tool_debug_data = anc_data_catch(aeq_hdl->tool_debug_data, (u8 *)leq_dat, eq_dat_len, AEQ_L_IIR_VOL_HIGH, 0);
        }
    }

    free(leq_dat);
}

int audio_adaptive_eq_tool_data_get(u8 **buf, u32 *len)
{
    if (aeq_hdl->tool_debug_data == NULL) {
        printf("AEQ packet is NULL, return!\n");
        return -1;
    }
    if (aeq_hdl->tool_debug_data->dat_len == 0) {
        printf("AEQ error: dat_len == 0\n");
        return -1;
    }
    *buf = aeq_hdl->tool_debug_data->dat;
    *len = aeq_hdl->tool_debug_data->dat_len;
    return 0;
}

static int audio_adaptive_eq_cur_list_add(s16 volume, struct eq_default_seg_tab *eq_output)
{
    struct audio_aeq_bulk *bulk = zalloc(sizeof(struct audio_aeq_bulk));
    struct audio_aeq_bulk *tmp;
    spin_lock(&aeq_hdl->lock);
    list_for_each_entry(tmp, &aeq_hdl->head, entry) {
        if (tmp->volume == volume) {
            free(bulk);
            return -1;
        }
    }
    bulk->volume = volume;
    bulk->eq_output = eq_output;
    list_add_tail(&bulk->entry, &aeq_hdl->head);
    spin_unlock(&aeq_hdl->lock);
    return 0;
}

static int audio_adaptive_eq_cur_list_del(void)
{
    struct audio_aeq_bulk *bulk;
    struct audio_aeq_bulk *temp;
    spin_lock(&aeq_hdl->lock);
    list_for_each_entry_safe(bulk, temp, &aeq_hdl->head, entry) {
        free(bulk->eq_output->seg);
        free(bulk->eq_output);
        __list_del_entry(&(bulk->entry));
        free(bulk);
    }
    spin_unlock(&aeq_hdl->lock);
    return 0;
}

static struct eq_default_seg_tab *audio_adaptive_eq_cur_list_query(s16 volume)
{
    struct audio_aeq_bulk *bulk;

#if	ADAPTIVE_EQ_VOLUME_GRADE_EN
    int vol_list_num = sizeof(aeq_volume_grade_list);

    for (u8 i = 0; i < vol_list_num; i++) {
        if (volume <= aeq_volume_grade_list[i]) {
            volume = aeq_volume_grade_list[i];
            break;
        }
    }
#else
    volume = 0;
#endif

    spin_lock(&aeq_hdl->lock);
    list_for_each_entry(bulk, &aeq_hdl->head, entry) {
        if (bulk->volume == volume) {
            spin_unlock(&aeq_hdl->lock);
            return bulk->eq_output;
        }
    }
    spin_unlock(&aeq_hdl->lock);
    //读不到则返回默认参数
    return audio_icsd_eq_default_tab_read();
}

static int audio_adaptive_eq_start(void)
{
    //初始化算法，申请资源
    struct icsd_aeq_libfmt libfmt;	//获取算法库需要的参数
    struct icsd_aeq_infmt infmt;	//输入算法库的参数

    icsd_aeq_get_libfmt(&libfmt);
    aeq_printf("AEQ RAM SIZE:%d\n", libfmt.lib_alloc_size);
    aeq_hdl->lib_alloc_ptr = zalloc(libfmt.lib_alloc_size);
    if (!aeq_hdl->lib_alloc_ptr) {
        return -1;
    }
    infmt.alloc_ptr = aeq_hdl->lib_alloc_ptr;
    infmt.seg_num = AEG_SEG_NUM;
    icsd_aeq_set_infmt(&infmt);
    return 0;

}

static int audio_adaptive_eq_stop(void)
{
    free(aeq_hdl->lib_alloc_ptr);
    aeq_hdl->lib_alloc_ptr = NULL;
    return 0;
}

static struct eq_default_seg_tab *audio_adaptive_eq_run(float maxgain_dB, int cnt)
{
    struct eq_default_seg_tab *eq_cur_tmp = NULL;
    struct anc_ext_ear_adaptive_param *ext_cfg = anc_ext_ear_adaptive_cfg_get();

    float *sz_cur = (float *)zalloc(aeq_hdl->fre_out->sz_l.len * sizeof(float));
    memcpy(sz_cur, aeq_hdl->fre_out->sz_l.out, aeq_hdl->fre_out->sz_l.len * sizeof(float));

    aeq_hdl->sz_ref = (float *)zalloc(AEQ_FLEN * sizeof(float));

    /* memcpy((u8 *)aeq_hdl->sz_ref, sz_ref_test, AEQ_FLEN * sizeof(float)); */
    if (ext_cfg->sz_ref) {
        memcpy((u8 *)aeq_hdl->sz_ref, ext_cfg->sz_ref, AEQ_FLEN * sizeof(float));
    }

    aeq_hdl->sz_dut_cmp = (float *)malloc(AEQ_FLEN * sizeof(float));

    //低频平稳滤波
    for (int i = 0; i < 10; i += 2) {
        aeq_hdl->sz_ref[i] = aeq_hdl->sz_ref[10] * 0.9 ; // i
        aeq_hdl->sz_ref[i + 1] = aeq_hdl->sz_ref[11] * 0.9; //

        sz_cur[i] = (sz_cur[12] + sz_cur[10]) / 2;
        sz_cur[i + 1] = (sz_cur[13] + sz_cur[11]) / 2;
    }

    for (int i = 0; i < AEQ_FLEN; i += 2) {
        //测试代码-目前产测未接入
        aeq_hdl->sz_dut_cmp[i] = 1.0;
        aeq_hdl->sz_dut_cmp[i + 1] = 0;
        /* aeq_hdl->sz_ref[i] = 1.0; */
        /* aeq_hdl->sz_ref[i + 1] = 0; */
        /* sz_in[i  ] = 1.0; */
        /* sz_in[i + 1] = 0; */
    }
    /* put_buf((u8 *)sz_cur, 120 * 4); */

    int output_seg_num = ADAPTIVE_EQ_ORDER;
    float *lib_eq_cur = zalloc(((3 * output_seg_num) + 1) * sizeof(float));
    //需要足够运行时间

    __adpt_aeq_cfg *aeq_cfg = zalloc(sizeof(__adpt_aeq_cfg));
    icsd_aeq_cfg_set(aeq_cfg, ext_cfg, 0, output_seg_num);

    _aeq_output *aeq_output = icsd_aeq_run(aeq_hdl->sz_ref, sz_cur, \
                                           (void *)aeq_hdl->eq_ref, aeq_hdl->sz_dut_cmp, maxgain_dB, lib_eq_cur, aeq_cfg);
    if (aeq_output->state) {
        aeq_printf("AEQ OUTPUT ERR!\n");
        goto __exit;
    }

    audio_adaptive_eq_data_packet(aeq_output, sz_cur, lib_eq_cur, cnt, aeq_cfg);

    //格式化算法输出
    int seg_size = sizeof(struct eq_seg_info) * output_seg_num;
    eq_cur_tmp = malloc(sizeof(struct eq_default_seg_tab));
    eq_cur_tmp->seg = malloc(seg_size);
    struct eq_seg_info *out_seg = eq_cur_tmp->seg;

    //算法输出时，线性值转dB
    eq_cur_tmp->global_gain = 20 * log10_float(lib_eq_cur[0] < 0 ? (0 - lib_eq_cur[0]) : lib_eq_cur[0]);
    eq_cur_tmp->seg_num = output_seg_num;

    /* aeq_printf("Global %d\n", (int)(lib_eq_cur[0] * 10000)); */
    for (int i = 0; i < output_seg_num; i++) {
        out_seg[i].index = i;
        out_seg[i].iir_type = aeq_cfg->type[i];
        out_seg[i].freq = lib_eq_cur[i * 3 + 1];
        out_seg[i].gain = lib_eq_cur[i * 3 + 1 + 1];
        out_seg[i].q 	= lib_eq_cur[i * 3 + 2 + 1];
        /* aeq_printf("SEG[%d]:Type %d, FGQ:%d %d %d\n", i, out_seg[i].iir_type, \ */
        /* (int)(out_seg[i].freq * 10000), (int)(out_seg[i].gain * 10000), \ */
        /* (int)(out_seg[i].q * 10000)); */
    }

__exit:
    free(lib_eq_cur);
    free(aeq_cfg);
    free(sz_cur);

    if (aeq_hdl->sz_dut_cmp) {
        free(aeq_hdl->sz_dut_cmp);
        aeq_hdl->sz_dut_cmp = NULL;
    }
    if (aeq_hdl->sz_ref) {
        free(aeq_hdl->sz_ref);
        aeq_hdl->sz_ref = NULL;
    }
    return eq_cur_tmp;
}

static int audio_adaptive_eq_dot_run(void)
{
#if TCFG_AUDIO_FIT_DET_ENABLE
    float dot_db = audio_icsd_dot_light_open(aeq_hdl->fre_out);
    aeq_printf("========================================= \n");
    aeq_printf("                    dot db = %d/100 \n", (int)(dot_db * 100.0f));
    aeq_printf("========================================= \n");

    audio_anc_debug_app_send_data(ANC_DEBUG_APP_CMD_AEQ, 0x0, (u8 *)&dot_db, 4);

    if (dot_db > aeq_hdl->thr.dot_thr2) {
        aeq_printf(" dot: tight ");
        return AUDIO_FIT_DET_RESULT_TIGHT;
    } else if (dot_db > aeq_hdl->thr.dot_thr1) {
        aeq_printf(" dot: norm ");
        return AUDIO_FIT_DET_RESULT_NORMAL;
    } else { // < loose_thr
        aeq_printf(" dot: loose ");
        return AUDIO_FIT_DET_RESULT_LOOSE;
    }
#endif
    return 0;
}

static void audio_adaptive_eq_afq_output_hdl(struct audio_afq_output *p)
{
    struct eq_default_seg_tab *eq_output;
    float maxgain_dB;

#if TCFG_AUDIO_ANC_REAL_TIME_ADAPTIVE_ENABLE
#if ADAPTIVE_EQ_ONLY_IN_MUSIC_UPDATE
    //非通话/播歌状态 不更新
    if (!(a2dp_player_runing() || esco_player_runing())) {
        if (aeq_hdl->fre_sel == AUDIO_ADAPTIVE_FRE_SEL_ANC) {
            audio_anc_real_time_adaptive_resume();
        }
        return;
    }
#endif
    /*if (aeq_hdl->fre_sel == AUDIO_ADAPTIVE_FRE_SEL_ANC) {
        audio_anc_real_time_adaptive_suspend();
    }*/
#endif

    if (aeq_hdl->state == ADAPTIVE_EQ_STATE_FORCE_EXIT) {
        goto __aeq_close;
    }
    aeq_hdl->fre_out = p;

    audio_adaptive_eq_sz_data_packet(p);

    aeq_printf("ICSD_AEQ_STATE:RUN");
    /* mem_stats(); */

    aeq_hdl->thr = *(anc_ext_ear_adaptive_cfg_get()->aeq_thr);
    aeq_volume_grade_list[0] = aeq_hdl->thr.vol_thr1;
    aeq_volume_grade_list[1] = aeq_hdl->thr.vol_thr2;

    aeq_hdl->state = ADAPTIVE_EQ_STATE_RUN;
    aeq_hdl->run_busy = 1;

#if ADAPTIVE_EQ_RUN_TIME_DEBUG
    u32 last = jiffies_usec();
#endif

    //释放上一次AEQ存储空间
    audio_adaptive_eq_cur_list_del();

#if ADAPTIVE_EQ_VOLUME_GRADE_EN
    //根据参数需求循环跑AEQ

#if ADAPTIVE_EQ_TIGHTNESS_GRADE_EN
    int dot_lvl = audio_adaptive_eq_dot_run();
    /* if(dot_lvl == AUDIO_FIT_DET_RESULT_TIGHT) { */
    /* goto __aeq_close; */
    /* } */
#else
    int dot_lvl = 0;
#endif
#if ADAPTIVE_EQ_OUTPUT_IN_CUR_VOL_EN
    //根据当前音量计算1次AEQ
    s16 target_volume = app_audio_get_volume(APP_AUDIO_STATE_MUSIC);
    int vol_list_num = sizeof(aeq_volume_grade_list);
    for (u8 i = 0; i < vol_list_num; i++) {
        if (target_volume <= aeq_volume_grade_list[i]) {
            target_volume = aeq_volume_grade_list[i];
            maxgain_dB = aeq_hdl->thr.max_dB[dot_lvl][i];
            aeq_printf("max_dB %d/10, lvl %d\n", (int)(maxgain_dB * 10), aeq_volume_grade_list[i]);
            audio_adaptive_eq_start();
            eq_output = audio_adaptive_eq_run(maxgain_dB, i);
            if (!eq_output) {
                printf("aeq forced exit! i=%d", i);
                audio_adaptive_eq_stop();
                break;
            }
            audio_adaptive_eq_cur_list_add(aeq_volume_grade_list[i], eq_output);
            audio_adaptive_eq_stop();
            break;
        }
    }
#else
    int vol_list_num = sizeof(aeq_volume_grade_list);
    for (u8 i = 0; i < vol_list_num; i++) {
        /* maxgain_dB = 0 - audio_adaptive_eq_vol_gain_get(aeq_volume_grade_list[i]); */
        os_time_dly(2); //避免系统跑不过来
        wdt_clear();
        maxgain_dB = aeq_hdl->thr.max_dB[dot_lvl][i];
        aeq_printf("max_dB %d/10, lvl %d\n", (int)(maxgain_dB * 10), aeq_volume_grade_list[i]);
        audio_adaptive_eq_start();
        eq_output = audio_adaptive_eq_run(maxgain_dB, i);
        if (!eq_output) {
            printf("aeq forced exit! i=%d", i);
            audio_adaptive_eq_stop();
            break;
        }
        audio_adaptive_eq_cur_list_add(aeq_volume_grade_list[i], eq_output);
        audio_adaptive_eq_stop();
    }
#endif
#else
    audio_adaptive_eq_start();
    eq_output = audio_adaptive_eq_run(ADAPTIVE_EQ_MAXGAIN_DB, 0);
    if (eq_output) {
        audio_adaptive_eq_cur_list_add(0, eq_output);
    }
    audio_adaptive_eq_stop();

#endif

#if ADAPTIVE_EQ_RUN_TIME_DEBUG
    aeq_printf("ADAPTIVE EQ RUN time: %d us\n", (int)(jiffies_usec2offset(last, jiffies_usec())));
#endif
    aeq_hdl->run_busy = 0;

__aeq_close:
    if (aeq_hdl->state == ADAPTIVE_EQ_STATE_FORCE_EXIT) {
        audio_adaptive_eq_close();
    }
    if (aeq_hdl->real_time_eq_en) {
        //实时AEQ 在线更新EQ效果

        if (aeq_hdl->eff_mode == AEQ_EFF_MODE_ADAPTIVE) {
            s16 volume = app_audio_get_volume(APP_AUDIO_STATE_MUSIC);
            aeq_hdl->now_volume = volume;
            audio_icsd_eq_eff_update(audio_adaptive_eq_cur_list_query(volume));
        }

        aeq_printf("ICSD_AEQ_STATE:RUN FINISH");
#if TCFG_AUDIO_ANC_REAL_TIME_ADAPTIVE_ENABLE
        if (aeq_hdl->fre_sel == AUDIO_ADAPTIVE_FRE_SEL_ANC) {
            audio_anc_real_time_adaptive_resume();
        }
#endif
    } else {
        //单次AEQ 执行关闭
        audio_adaptive_eq_close();
    }
}

int audio_adaptive_eq_eff_set(enum ADAPTIVE_EFF_MODE mode)
{
    if (aeq_hdl) {
        if (audio_adaptive_eq_is_running()) {
            return -1;
        }
        aeq_printf("AEQ EFF mode set %d\n", mode);
        struct eq_default_seg_tab *eq_tab;
        aeq_hdl->eff_mode = mode;
        switch (mode) {
        case AEQ_EFF_MODE_DEFAULT:	//默认效果
            eq_tab = audio_icsd_eq_default_tab_read();
            audio_icsd_eq_eff_update(eq_tab);
            if (eq_tab != &default_seg_tab) {
                free(eq_tab->seg);
                free(eq_tab);
            }
            break;
        case AEQ_EFF_MODE_ADAPTIVE:	//自适应效果
            s16 volume = app_audio_get_volume(APP_AUDIO_STATE_MUSIC);
            audio_adaptive_eq_vol_update(volume);
            break;
        }
    }
    return 0;
}

//根据音量获取对应AEQ效果，并更新
int audio_adaptive_eq_vol_update(s16 volume)
{
    if (audio_adaptive_eq_is_running()) {
        aeq_printf("[AEQ_WARN] vol update fail, AEQ is running\n");
        return -1;
    }
    if (aeq_hdl->eff_mode == AEQ_EFF_MODE_DEFAULT) {
        aeq_printf("[AEQ] vol no need update, AEQ mode is default\n");
        return 0;
    }
    aeq_hdl->now_volume = volume;
    audio_icsd_eq_eff_update(audio_adaptive_eq_cur_list_query(volume));
    return 0;
}

static float audio_adaptive_eq_vol_gain_get(s16 volume)
{
    struct cfg_info info  = {0};             //节点配置相关信息（参数存储的目标地址、配置项大小）
    char mode_index       = 0;               //模式序号（当前节点无多参数组，mode_index是0）
    char *node_name       = ADAPTIVE_EQ_VOLUME_NODE_NAME;       //节点名称（节点内的第一参数，用户自定义,长度小于等于15byte）
    char cfg_index        = 0;               //目标配置项序号（当前节点无多参数组，cfg_index是0）
    int ret = jlstream_read_form_node_info_base(mode_index, node_name, cfg_index, &info);
    if (!ret) {
        struct volume_cfg *vol_cfg = zalloc(info.size);
        if (!jlstream_read_form_cfg_data(&info, (void *)vol_cfg)) {
            aeq_printf("[AEQ]user vol cfg parm read err\n");
            free(vol_cfg);
            return 0;
        }
        /* printf("cfg_level_max = %d\n", vol_cfg->cfg_level_max); */
        /* printf("cfg_vol_min = %d\n", vol_cfg->cfg_vol_min); */
        /* printf("cfg_vol_max = %d\n", vol_cfg->cfg_vol_max); */
        /* printf("vol_table_custom = %d\n", vol_cfg->vol_table_custom); */
        /* printf("cur_vol = %d\n", vol_cfg->cur_vol); */
        /* printf("tab_len = %d\n", vol_cfg->tab_len); */

        if (info.size > sizeof(struct volume_cfg)) { //有自定义音量表, 直接返回对应音量
            return vol_cfg->vol_table[volume];
        } else {
            u16 step = (vol_cfg->cfg_level_max - 1 > 0) ? (vol_cfg->cfg_level_max - 1) : 1;
            float step_db = (vol_cfg->cfg_vol_max - vol_cfg->cfg_vol_min) / (float)step;
            float vol_db = vol_cfg->cfg_vol_min + (volume - 1) * step_db;
            return vol_db;
        }
    } else {
        aeq_printf("[AEQ]user vol cfg parm read err ret %d\n", ret);
    }
    return 0;
}

// (单次)自适应EQ强制退出
int audio_adaptive_eq_force_exit(void)
{
    aeq_printf("func:%s, aeq_hdl->state:%d", __FUNCTION__, aeq_hdl->state);
    switch (aeq_hdl->state) {
    case ADAPTIVE_EQ_STATE_RUN:
        aeq_printf("ICSD_AEQ_STATE:(SINGLE)FORCE_EXIT");
        aeq_hdl->state = ADAPTIVE_EQ_STATE_FORCE_EXIT;
        icsd_aeq_force_exit();  // RUN才跑算法流程
        break;
    case ADAPTIVE_EQ_STATE_OPEN:
        audio_adaptive_eq_close();
        break;
    }
    return 0;
}

#endif/*TCFG_AUDIO_ADAPTIVE_EQ_ENABLE*/
