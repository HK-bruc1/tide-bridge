#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".icsd_afq.data.bss")
#pragma data_seg(".icsd_afq.data")
#pragma const_seg(".icsd_afq.text.const")
#pragma code_seg(".icsd_afq.text")
#endif

#include "app_config.h"
#include "audio_config_def.h"
#if TCFG_AUDIO_ANC_ENABLE
#include "audio_anc_common.h"
#endif

#if (TCFG_AUDIO_FREQUENCY_GET_ENABLE && \
	 TCFG_AUDIO_ANC_EXT_VERSION == ANC_EXT_V2)
#include "icsd_afq_app.h"
#include "audio_anc.h"
#include "app_tone.h"
#include "dac_node.h"
#include "clock_manager/clock_manager.h"
#include "icsd_common_v2_app.h"

#if TCFG_USER_TWS_ENABLE
#include "bt_tws.h"
#include "tws_tone_player.h"
#endif/*TCFG_USER_TWS_ENABLE*/

#if TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN
#include "icsd_adt.h"
#include "icsd_adt_app.h"
#endif /*TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN*/

#if 1
#define afq_log printf
#else
#define afq_log(...)
#endif

//AFQ提示音文件选择
#define AUDIO_AFQ_TONE_FILE_SEL			get_tone_files()->anc_adaptive

struct audio_afq_hdl {
    volatile u8 state;				//状态
    volatile u8 tone_state;			//提示音状态
    u8 last_anc_mode;				//记录ANC上一个模式
    u8 last_alogm;					//ANC采样率
    u8 dac_check_flag;				//DAC静音校验标志
#if TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN
    u8 icsd_adt_suspend;
#endif
    void *lib_alloc_ptr;			//afq申请的内存指针
};

static struct audio_afq_hdl *afq_hdl = NULL;

extern struct audio_dac_hdl dac_hdl;

static int audio_icsd_afq_close();
static void audio_icsd_afq_anc_dma_done(void);

static void audio_icsd_afq_dac_check_slience_cb(void *buf, int len)
{
    if (afq_hdl) {
        if (afq_hdl->dac_check_flag) {
            s16 *check_buf = (s16 *)buf;
            for (int i = 0; i < len / 2; i++) {
                if (check_buf[i]) {
                    afq_hdl->dac_check_flag = 0;
                    //触发
                    afq_log("%s ok\n", __func__);
                    icsd_afq_run(i, dac_hdl.sample_rate, icsd_afq_output);
                    dac_node_write_callback_del("ANC_AFQ");
                    break;
                }
            }
        }
    }
}

static int audio_afq_tone_play_cb(void *priv, enum stream_event event)
{
    afq_log("%s: %d\n", __func__, event);
    if ((event != STREAM_EVENT_START) && (event != STREAM_EVENT_STOP) && (event != STREAM_EVENT_INIT)) {
        /*没有提示音，或者提示音打断*/
        if (afq_hdl) {
            afq_hdl->state = ICSD_AFQ_STATE_CLOSE;
            afq_hdl->tone_state = ICSD_AFQ_STATE_TONE_STOP;
            audio_icsd_afq_close();
        }
    }

    //提示音播放结束
    if (event == STREAM_EVENT_STOP) {
#if ICSD_AFQ_EN_MODE_NEXT_SW
        if (afq_hdl) {
            afq_hdl->tone_state = ICSD_AFQ_STATE_TONE_STOP;
            audio_icsd_afq_close();
        }
#endif
    }
    return 0;
}

static int audio_icsd_afq_stop(void)
{
    printf("audio_icsd_afq_stop\n");
    if (afq_hdl) {
        afq_hdl->state = ICSD_AFQ_STATE_CLOSE;
        audio_icsd_afq_close();
    }
    return 0;
}

static void tws_afq_tone_callback(int priv, enum stream_event event)
{
    r_printf("tws_afq_tone_callback: %d\n",  event);
    audio_afq_tone_play_cb(NULL, event);
}

#define TWS_AFQ_TONE_STUB_FUNC_UUID   0xD14A6159
REGISTER_TWS_TONE_CALLBACK(tws_anc_tone_stub) = {
    .func_uuid  = TWS_AFQ_TONE_STUB_FUNC_UUID,
    .callback   = tws_afq_tone_callback,
};

//提示音播放
static int audio_icsd_afq_tone_play(void)
{
    int ret = 0;
    afq_log("%s\n", __func__);

#if ICSD_AFQ_EN_MODE_NEXT_SW
    afq_hdl->tone_state = ICSD_AFQ_STATE_TONE_START;
#endif

#if TCFG_USER_TWS_ENABLE
    if (get_tws_sibling_connect_state()) {
        if (tws_api_get_role() == TWS_ROLE_MASTER) {
            ret = tws_play_tone_file_callback(AUDIO_AFQ_TONE_FILE_SEL, 400, TWS_AFQ_TONE_STUB_FUNC_UUID);
        }
    } else {
        ret = tws_play_tone_file_callback(AUDIO_AFQ_TONE_FILE_SEL, 400, TWS_AFQ_TONE_STUB_FUNC_UUID);
    }
#else
    ret = play_tone_file_callback(AUDIO_AFQ_TONE_FILE_SEL, NULL, audio_afq_tone_play_cb);
#endif
    return ret;
}

//保留现场及功能互斥
static void audio_icsd_afq_mutex_suspend(void)
{
    if (afq_hdl) {
#if TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN
        /*进入AFQ前关闭adt*/
        afq_hdl->icsd_adt_suspend = audio_icsd_adt_is_running();
        if (afq_hdl->icsd_adt_suspend) {
            audio_icsd_adt_close(0, 1);
        }

        printf("======================= %d", audio_icsd_adt_is_running());
        int cnt;
        //adt关闭时间较短，预留100ms
        for (cnt = 0; cnt < 10; cnt++) {
            if (!audio_icsd_adt_is_running()) {
                break;
            }
            os_time_dly(1);  //  等待ADT 关闭
        }
        if (cnt == 10) {
            printf("Err:afq_suspend adt wait timeout\n");
        }
#endif /*TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN*/

        afq_hdl->last_alogm = get_anc_gains_alogm();
        afq_hdl->last_anc_mode = anc_mode_get();
    }
}

//恢复现场及功能互斥
static void audio_icsd_afq_mutex_resume(void)
{
    if (afq_hdl) {
        //恢复ANC相关状态
        set_anc_gains_alogm(afq_hdl->last_alogm);			//设置ANC采样率
        anc_mode_switch(afq_hdl->last_anc_mode, 0);

        //按ANC最低采样率的淡出时间预留1.5s, 常用配置150ms以内
        int cnt;
        for (cnt = 0; cnt < 150; cnt++) {
            if (!anc_mode_switch_lock_get()) {
                break;
            }
            os_time_dly(1);  //  等待ANC模式切换完毕
        }
        if (cnt == 150) {
            printf("Err:afq_resume switch_lock wait timeout\n");
        }

#if TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN
        if (afq_hdl->icsd_adt_suspend) {
            audio_icsd_adt_open(0);
        }
#endif /*TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN*/
    }
}

int audio_icsd_afq_init(void)
{
    afq_hdl = zalloc(sizeof(struct audio_afq_hdl));
    return 0;
}


int audio_icsd_afq_output(__afq_output *out)
{
    audio_afq_common_output_post_msg(out);

    //释放afq 算法资源
    audio_anc_dma_del_output_handler("ANC_AFQ");
    clock_free("ANC_AFQ");
    free(afq_hdl->lib_alloc_ptr);
    afq_hdl->lib_alloc_ptr = NULL;

    //处理结束，关闭AFQ资源
    audio_icsd_afq_stop();

    return 0;
}


int audio_icsd_afq_permit(void)
{
    //ANC训练过程不允许打开
    if (anc_train_open_query()) {
        return 1;
    }
#if TCFG_AUDIO_ANC_EAR_ADAPTIVE_EN
    //耳道自适应过程不允许打开
    if (anc_ear_adaptive_busy_get()) {
        return 1;
    }
#endif
    return 0;
}

//音频频响(SZ/PZ)获取启动
int audio_icsd_afq_open(u8 tone_en)
{
    if (afq_hdl->state != ICSD_AFQ_STATE_CLOSE) {
        afq_log("icsd afq busy now!\n");
        return 0;
    }
    if (audio_icsd_afq_permit()) {
        afq_log("icsd_afq_permit, open fail\n");
        return 1;
    }

    afq_log("===================icsd_afq_open===================\n");
    mem_stats();

    struct icsd_afq_libfmt libfmt;
    struct icsd_afq_infmt infmt;

    /* afq_hdl->result_cb = result_cb; */

    //1.保留现场及功能互斥
    audio_icsd_afq_mutex_suspend();

    icsd_afq_get_libfmt(&libfmt);
    afq_log("AFQ RAM SIZE:%d\n", libfmt.lib_alloc_size);
    afq_hdl->lib_alloc_ptr = zalloc(libfmt.lib_alloc_size);
    if (!afq_hdl->lib_alloc_ptr) {
        return -1;
    }
    infmt.alloc_ptr = afq_hdl->lib_alloc_ptr;
    infmt.tone_delay = 300;
    icsd_afq_set_infmt(&infmt);

    //2.注册ANC DMA回调函数
    audio_anc_dma_add_output_handler("ANC_AFQ", audio_icsd_afq_anc_dma_done);
    //3.初始化算法
    clock_alloc("ANC_AFQ", 160 * 1000000L);

    //4.ANC初始化
    /* set_anc_gains_alogm(afq_hdl->libfmt.alogm);			//设置ANC采样率 */
    set_anc_gains_alogm(5);			//设置ANC采样率
    anc_mode_switch(ANC_EXT, 0);	//进入ANC扩展模式
    afq_hdl->state = ICSD_AFQ_STATE_OPEN;

    //按ANC最低采样率的淡出时间预留1.5s, 常用配置150ms以内
    int cnt;
    for (cnt = 0; cnt < 150; cnt++) {
        if (!anc_mode_switch_lock_get()) {
            break;
        }
        os_time_dly(1);  //  等待ANC模式切换完毕
    }
    if (cnt == 150) {
        printf("Err:afq_open switch_lock wait timeout\n");
    }
    //4.播放提示音
    if (tone_en) {
        audio_icsd_afq_tone_play();
    }

    dac_node_write_callback_add("ANC_AFQ", 0XFF, audio_icsd_afq_dac_check_slience_cb);	//注册DAC回调接口-静音检测
    afq_hdl->dac_check_flag = 1;

    //cppcheck-suppress memleak
    return 0;
}

int audio_icsd_afq_close()
{
    afq_log("%s\n", __func__);
    if (afq_hdl) {
        if (afq_hdl->state || afq_hdl->tone_state || audio_afq_common_app_is_active()) {
            return 1;
        }
        if (strcmp(os_current_task(), "anc") == 0) {
            //afq close在ANC线程执行会造成死锁，需改为在APP任务执行
            afq_log("afq close post to app_core\n");
            int msg[2];
            msg[0] = (int)audio_icsd_afq_close;
            msg[1] = 1;
            int ret = os_taskq_post_type("app_core", Q_CALLBACK, 2, msg);
            if (ret) {
                afq_log("afq taskq_post err\n");
            }
            return 0;
        }
        afq_log("%s, ok\n", __func__);
        //恢复ANC相关状态
        audio_icsd_afq_mutex_resume();
    }
    return 0;
}

//查询afq是否活动中
u8 audio_icsd_afq_is_running(void)
{
    if (afq_hdl) {
        return (afq_hdl->state | afq_hdl->tone_state);
    }
    return ICSD_AFQ_STATE_CLOSE;
}

static void audio_icsd_afq_anc_dma_done(void)
{
    if (afq_hdl) {
        if (afq_hdl->state) {
            icsd_afq_dma_handler();
        }
    }
}

#endif
