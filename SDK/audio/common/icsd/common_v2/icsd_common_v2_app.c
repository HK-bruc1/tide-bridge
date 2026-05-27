
#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".icsd_common.data.bss")
#pragma data_seg(".icsd_common.data")
#pragma const_seg(".icsd_common.text.const")
#pragma code_seg(".icsd_common.text")
#endif

#include "app_config.h"
#include "audio_config_def.h"

#if (TCFG_AUDIO_ANC_ENABLE && \
	 TCFG_AUDIO_ANC_EXT_VERSION == ANC_EXT_V2)

#include "icsd_common_v2_app.h"
#include "audio_anc_common.h"
#include "audio_anc.h"
#if TCFG_AUDIO_FREQUENCY_GET_ENABLE
#include "icsd_afq_app.h"
#endif

#if TCFG_AUDIO_ANC_EAR_ADAPTIVE_EN
#include "icsd_anc_v2_interactive.h"
#endif

#if 1
#define common_log printf
#else
#define common_log(...)
#endif

struct afq_common_hdl {
    u8 state;						//状态
    u8 fre_use_cnt;					//AFQ频响APP应用记录，0 无应用；!0 有应用
    enum audio_adaptive_fre_sel data_sel;					//数据来源
    struct list_head head;
    OS_MUTEX mutex;
    struct audio_afq_output fre_out;//输出频响存储位置，应用完毕释放
};

struct afq_common_hdl *common_hdl = NULL;

static void audio_afq_common_task(void *p);
static int audio_afq_common_output_process(void);

int audio_afq_common_init(void)
{
    common_hdl = zalloc(sizeof(struct afq_common_hdl));
    INIT_LIST_HEAD(&common_hdl->head);
    os_mutex_create(&common_hdl->mutex);
    return 0;
}

int audio_afq_common_start(void)
{
    task_create(audio_afq_common_task, NULL, "afq_common");
    switch (common_hdl->data_sel) {
    case AUDIO_ADAPTIVE_FRE_SEL_AFQ:
#if TCFG_AUDIO_FREQUENCY_GET_ENABLE
#endif
        break;
    case AUDIO_ADAPTIVE_FRE_SEL_ANC:
#if TCFG_AUDIO_ANC_EAR_ADAPTIVE_EN
#endif
        break;
    }
    return 0;
}

static void audio_afq_common_fre_out_free(void)
{
    if (common_hdl) {
        if (common_hdl->fre_out.sz_l.out) {
            free(common_hdl->fre_out.sz_l.out);
            common_hdl->fre_out.sz_l.out = NULL;
        }
        if (common_hdl->fre_out.sz_l.msc) {
            free(common_hdl->fre_out.sz_l.msc);
            common_hdl->fre_out.sz_l.msc = NULL;
        }
        if (common_hdl->fre_out.sz_r.out) {
            free(common_hdl->fre_out.sz_r.out);
            common_hdl->fre_out.sz_r.out = NULL;
        }
        if (common_hdl->fre_out.sz_r.msc) {
            free(common_hdl->fre_out.sz_r.msc);
            common_hdl->fre_out.sz_r.msc = NULL;
        }
    }
}

int audio_afq_common_close(void)
{
    common_log("%s\n", __func__);

    if (strcmp(os_current_task(), "afq_common") == 0) {
        common_log("afq_common close post to app_core\n");
        int msg[2];
        msg[0] = (int)audio_afq_common_close;
        msg[1] = 1;
        int ret = os_taskq_post_type("app_core", Q_CALLBACK, 2, msg);
        if (ret) {
            common_log("afq_common taskq_post err\n");
        }
        return 0;
    }
    audio_afq_common_fre_out_free();
    task_kill("afq_common");

    switch (common_hdl->data_sel) {
    case AUDIO_ADAPTIVE_FRE_SEL_AFQ:
#if TCFG_AUDIO_FREQUENCY_GET_ENABLE
        audio_icsd_afq_close();
#endif
        break;
    case AUDIO_ADAPTIVE_FRE_SEL_ANC:
#if TCFG_AUDIO_ANC_EAR_ADAPTIVE_EN
        anc_ear_adaptive_close();
#endif
        break;
    }
    return 0;
}

//异步处理，先存数据
int audio_afq_common_output_post_msg(__afq_output *out)
{
    struct audio_afq_output *p = &common_hdl->fre_out;

    if (!common_hdl->fre_use_cnt) {	//有人使用才存数据
        return 0;
    }
    p->state = out->state;

    //先释放上一次内存
    audio_afq_common_fre_out_free();
    if (out->sz_l) {
        /* for (int i = 0; i < out->sz_l->len; i++) { */
        /* common_log("%d\n", (int)(out->sz_l->out[i] * 1000)); */
        /* } */
        p->sz_l.len = out->sz_l->len;
        if (out->sz_l->out) {
            p->sz_l.out = malloc(p->sz_l.len * sizeof(float));
            memcpy(p->sz_l.out, out->sz_l->out, p->sz_l.len * sizeof(float));
        }
        if (out->sz_l->msc) {
            p->sz_l.msc = malloc(p->sz_l.len * sizeof(float));
            memcpy(p->sz_l.msc, out->sz_l->msc, p->sz_l.len * sizeof(float));
        }
    }

    if (out->sz_r) {
        /* for (int i = 0; i < out->sz_r->len; i++) { */
        /* common_log("%d\n", (int)(out->sz_r->out[i] * 1000)); */
        /* } */
        p->sz_r.len = out->sz_r->len;
        if (out->sz_r->out) {
            p->sz_r.out = malloc(p->sz_r.len * sizeof(float));
            memcpy(p->sz_r.out, out->sz_r->out, p->sz_r.len * sizeof(float));
        }
        if (out->sz_r->msc) {
            p->sz_r.msc = malloc(p->sz_r.len * sizeof(float));
            memcpy(p->sz_r.msc, out->sz_r->msc, p->sz_r.len * sizeof(float));
        }
    }
    memcpy(p->sz_l.out, out->sz_l->out, p->sz_l.len * sizeof(float));

    //printf("get sz_freqs, %d, %d \n", (int)(p->sz_l.out[2] * 1000), (int)(p->sz_l.out[3] * 1000));
    os_taskq_post_msg("afq_common", 1, AFQ_COMMON_MSG_RUN);
    return 0;
}

static int audio_afq_common_output_process(void)
{
    struct audio_afq_bulk *bulk;
    struct audio_afq_output *p = &common_hdl->fre_out;

    os_mutex_pend(&common_hdl->mutex, 0);
    list_for_each_entry(bulk, &common_hdl->head, entry) {
        bulk->output(p);
    }
    os_mutex_post(&common_hdl->mutex);

    return 0;
}

/*
*********************************************************************
*                  Audio ANC Output Callback
* Description: 注册ANC DMA输出回调函数
* Arguments  : name		回调名称
*			   output  	采样输出回调
* Return	 : None.
*********************************************************************
*/
int audio_afq_common_add_output_handler(const char *name, int data_sel, void (*output)(struct audio_afq_output *p))
{
    if (common_hdl) {
        struct audio_afq_bulk *bulk = zalloc(sizeof(struct audio_afq_bulk));
        struct audio_afq_bulk *tmp;

        common_hdl->data_sel = data_sel;

        bulk->name = name;
        bulk->output = output;
        os_mutex_pend(&common_hdl->mutex, 0);
        list_for_each_entry(tmp, &common_hdl->head, entry) {
            if (tmp->name == name) {
                free(bulk);
                goto __exit;
            }
        }
        list_add_tail(&bulk->entry, &common_hdl->head);
        if (!common_hdl->fre_use_cnt) {
            audio_afq_common_start();
        }
        common_hdl->fre_use_cnt++;
__exit:
        os_mutex_post(&common_hdl->mutex);
    }
    return 0;
}

/*
*********************************************************************
*                  Audio ANC Output Callback
* Description: 删除ANC DMA输出回调函数
* Arguments  : name		回调名称
*			   output  	dma输出回调
* Return	 : None.
* Note(s)    : 对应功能关闭的时候，对应的回调也要同步删除，防止内存释
*              放出现非法访问情况
*********************************************************************
*/
int audio_afq_common_del_output_handler(const char *name)
{
    struct audio_afq_bulk *bulk;

    if (common_hdl) {
        os_mutex_pend(&common_hdl->mutex, 0);
        list_for_each_entry(bulk, &common_hdl->head, entry) {
            if (!strcmp(bulk->name, name)) {
                list_del(&bulk->entry);
                free(bulk);
                if (!(--common_hdl->fre_use_cnt)) {
                    audio_afq_common_close();
                }
                break;
            }
        }
        os_mutex_post(&common_hdl->mutex);
    }
    return 0;
}

static void audio_afq_common_task(void *p)
{
    int res;
    int msg[16];
    common_log(">>>AFQ COMMON TASK<<<\n");
    while (1) {
        res = os_taskq_pend(NULL, msg, ARRAY_SIZE(msg));
        if (res == OS_TASKQ) {
            switch (msg[1]) {
            case AFQ_COMMON_MSG_RUN:
                audio_afq_common_output_process();
                break;
            }
        } else {
            common_log("res:%d,%d", res, msg[1]);
        }
    }
}

u8 audio_afq_common_app_is_active(void)
{
    if (common_hdl) {
        return common_hdl->fre_use_cnt;
    }
    return 0;
}

#endif
