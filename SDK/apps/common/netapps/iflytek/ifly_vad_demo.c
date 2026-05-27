#include "ifly_vad_demo.h"
#include "ifly_vad.h"
#include "timer.h"
#include "ifly_common.h"

#if TCFG_IFLYTEK_ENABLE
static ifly_vad_struct *p_ifly_net = NULL;

static int ifly_vad_event_cb(ifly_vad_event_enum evt, void *param)  //vad任务状态callback
{
    switch (evt) {
    case IFLY_VAD_EVT_AUDIO_START:
        break;
    case IFLY_VAD_EVT_RECV_OK:
        break;
    case IFLY_VAD_EVT_EXIT:
        break;
    default:
        break;
    }
    return 0;
}

static void stop_rec(void *priv)   //8s结束录音,并关闭vad任务
{
    ifly_vad_stop(0, 10);
    if (p_ifly_net->vad_timer) {
        sys_timer_del(p_ifly_net->vad_timer);
        p_ifly_net->vad_timer = 0;
    }
}

void ifly_vad_demo(void)
{
    static int zwz = 0;
    if (zwz) {
        return;
    }
    zwz++;
    if (p_ifly_net == NULL) {
        p_ifly_net = zalloc(sizeof(ifly_vad_struct));
        ASSERT(p_ifly_net);
    }
    p_ifly_net->vad_param.vad_res = p_ifly_net->local_text;
    p_ifly_net->vad_param.vad_res_len = MAX_VAD_LEN;
    p_ifly_net->vad_param.event_cb = ifly_vad_event_cb;
    p_ifly_net->vad_param.vad_res[0] = 0;

    if (!p_ifly_net->vad_timer) {
        p_ifly_net->vad_timer = sys_timer_add(NULL, stop_rec, 8000);
    }
    ifly_vad_start(&p_ifly_net->vad_param);  //创建vad任务
}

#endif
