#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".a2dp_streamctrl.data.bss")
#pragma data_seg(".a2dp_streamctrl.data")
#pragma const_seg(".a2dp_streamctrl.text.const")
#pragma code_seg(".a2dp_streamctrl.text")
#endif
/*************************************************************************************************/
/*!
*  \file       a2dp_streamctrl.c
*
*  \brief      a2dp plug stream control file.
*
*  Copyright (c) 2011-2022 ZhuHai Jieli Technology Co.,Ltd.
*
*/
/*************************************************************************************************/
#include "btstack/a2dp_media_codec.h"
#include "sync/audio_syncts.h"
#include "system/timer.h"
#include "media/audio_base.h"
#include "source_node.h"
#include "jiffies.h"
#include "a2dp_streamctrl.h"
#include "app_config.h"
#include "audio_time.h"

extern const int CONFIG_A2DP_DELAY_TIME_AAC;
extern const int CONFIG_A2DP_DELAY_TIME_SBC;
extern const int CONFIG_A2DP_ADAPTIVE_MAX_LATENCY;
extern const int CONFIG_A2DP_DELAY_TIME_AAC_LO;
extern const int CONFIG_A2DP_DELAY_TIME_SBC_LO;
extern const int CONFIG_JL_DONGLE_PLAYBACK_LATENCY;
extern const int CONFIG_DONGLE_SPEAK_ENABLE;
extern const int CONFIG_A2DP_MAX_BUF_SIZE;
extern const int CONFIG_A2DP_AAC_MAX_BUF_SIZE;
extern const int CONFIG_A2DP_SBC_MAX_BUF_SIZE;
extern const int CONFIG_A2DP_LHDC_MAX_BUF_SIZE;
extern const int CONFIG_A2DP_LDAC_MAX_BUF_SIZE;
#if (defined(TCFG_BT_SUPPORT_LDAC) && TCFG_BT_SUPPORT_LDAC)
_WEAK_ const int CONFIG_A2DP_DELAY_TIME_LDAC = 300;
_WEAK_ const int CONFIG_A2DP_DELAY_TIME_LDAC_LO = 300;
#endif
#if (defined(TCFG_BT_SUPPORT_LHDC) && TCFG_BT_SUPPORT_LHDC)
_WEAK_ const int CONFIG_A2DP_DELAY_TIME_LHDC = 300;
_WEAK_ const int CONFIG_A2DP_DELAY_TIME_LHDC_LO = 300;
#endif
extern u32 bt_audio_reference_clock_time(u8 network);

#define A2DP_FLUENT_DETECT_INTERVAL         90000//ms 流畅播放延时检测时长
#define JL_DONGLE_FLUENT_DETECT_INTERVAL    30000//ms
#define A2DP_ADAPTIVE_DELAY_ENABLE          1

#define A2DP_STREAM_NO_ERR                  0
#define A2DP_STREAM_UNDERRUN                1
#define A2DP_STREAM_OVERRUN                 2
#define A2DP_STREAM_MISSED                  3
#define A2DP_STREAM_DECODE_ERR              4
#define A2DP_STREAM_LOW_UNDERRUN            5

#define RB16(b)    (u16)(((u8 *)b)[0] << 8 | (((u8 *)b))[1])
#define bt_time_to_msecs(clk)   (((clk) * 625) / 1000)

#define DELAY_INCREMENT                     150
#define LOW_LATENCY_DELAY_INCREMENT         50

#define DELAY_DECREMENT                     50
#define LOW_LATENCY_DELAY_DECREMENT         10

#define MAX_DELAY_INCREMENT                 150
struct a2dp_stream_control {
    u8 plan;
    u8 stream_error;
    u8 frame_free;
    u8 first_in;
    u8 low_latency;
    u8 jl_dongle;
    u8 repair_frame[2];
    u16 timer;
    u16 seqn;
    u16 overrun_seqn;
    u16 missed_num;
    s16 repair_num;
    s16 initial_latency;
    s16 adaptive_latency;
    s16 adaptive_max_latency;
    s16 max_rx_interval;
    u32 detect_timeout;
    u32 codec_type;
    u32 frame_time;
    void *stream;
    void *sample_detect;
    u32 next_timestamp;
    void *underrun_signal;
    void (*underrun_callback)(void *);
};


void a2dp_stream_mark_next_timestamp(void *_ctrl, u32 next_timestamp)
{
    struct a2dp_stream_control *ctrl = (struct a2dp_stream_control *)_ctrl;
    if (ctrl) {
        ctrl->next_timestamp = next_timestamp;
    }
}

int a2dp_stream_max_buf_size(u32 codec_type)
{
    int max_buf_size = CONFIG_A2DP_MAX_BUF_SIZE;
    switch (codec_type) {
    case A2DP_CODEC_SBC:
        max_buf_size = CONFIG_A2DP_SBC_MAX_BUF_SIZE;
        break;
    case A2DP_CODEC_MPEG24:
        max_buf_size = CONFIG_A2DP_AAC_MAX_BUF_SIZE;
        break;
#if (defined(TCFG_BT_SUPPORT_LDAC) && TCFG_BT_SUPPORT_LDAC)
    case A2DP_CODEC_LDAC:
        max_buf_size = CONFIG_A2DP_LDAC_MAX_BUF_SIZE;
        break;
#endif
#if (defined(TCFG_BT_SUPPORT_LHDC) && TCFG_BT_SUPPORT_LHDC)
    case A2DP_CODEC_LHDC:
        max_buf_size = CONFIG_A2DP_LHDC_MAX_BUF_SIZE;
        break;
#endif
#if (defined(TCFG_BT_SUPPORT_LHDC_V5) && TCFG_BT_SUPPORT_LHDC_V5)
    case A2DP_CODEC_LHDC_V5:
        max_buf_size = CONFIG_A2DP_LHDC_MAX_BUF_SIZE;
        break;
#endif
    }

    return max_buf_size;
}

void a2dp_stream_bandwidth_detect_handler(void *_ctrl, int frame_len, int pcm_frames, int sample_rate)
{
    struct a2dp_stream_control *ctrl = (struct a2dp_stream_control *)_ctrl;
    int max_latency = 0;

    if (ctrl->low_latency) {
        return;
    }

    if (frame_len) {
        int max_buf_size = a2dp_stream_max_buf_size(ctrl->codec_type);
        max_latency = (max_buf_size * pcm_frames / frame_len) * 1000 / sample_rate * 9 / 10;
    }

    if (!max_latency) {
        return;
    }

    if (max_latency < ctrl->adaptive_max_latency) {
        ctrl->adaptive_max_latency = max_latency;
    }

    if (ctrl->adaptive_latency > ctrl->adaptive_max_latency) {
        ctrl->adaptive_latency = ctrl->adaptive_max_latency;
        a2dp_media_update_delay_report_time(ctrl->stream, ctrl->adaptive_latency);
    }
}

static void a2dp_stream_underrun_adaptive_handler(struct a2dp_stream_control *ctrl)
{
#if A2DP_ADAPTIVE_DELAY_ENABLE
    ctrl->detect_timeout = jiffies + msecs_to_jiffies(ctrl->jl_dongle ? JL_DONGLE_FLUENT_DETECT_INTERVAL : A2DP_FLUENT_DETECT_INTERVAL);

    if (!ctrl->low_latency) {
        ctrl->adaptive_latency = ctrl->adaptive_max_latency;
    } else {
        ctrl->adaptive_latency += LOW_LATENCY_DELAY_INCREMENT;

        if (ctrl->adaptive_latency < ctrl->max_rx_interval) {
            ctrl->adaptive_latency = ctrl->max_rx_interval;
        }

        if (ctrl->adaptive_latency > ctrl->adaptive_max_latency) {
            ctrl->adaptive_latency = ctrl->adaptive_max_latency;
        }
    }
    a2dp_media_update_delay_report_time(ctrl->stream, ctrl->adaptive_latency);
    /*printf("---underrun, adaptive : %dms---\n", ctrl->adaptive_latency);*/
#endif
}

/*
 * 自适应延时策略
 */
static void a2dp_stream_adaptive_detect_handler(struct a2dp_stream_control *ctrl, u8 new_frame, u32 new_frame_time)
{
#if A2DP_ADAPTIVE_DELAY_ENABLE
    if (ctrl->stream_error || !new_frame) {
        return;
    }
    int rx_interval = 0;
    if (ctrl->frame_time) {
        rx_interval = bt_time_to_msecs((new_frame_time - ctrl->frame_time) & 0x7ffffff) + 1;
    }
    ctrl->frame_time = new_frame_time;

    if (rx_interval > ctrl->max_rx_interval) {
        if (CONFIG_DONGLE_SPEAK_ENABLE && ctrl->jl_dongle) {
            return;
        }
        ctrl->max_rx_interval = rx_interval;
        if (ctrl->max_rx_interval > ctrl->initial_latency + MAX_DELAY_INCREMENT) {
            ctrl->max_rx_interval = ctrl->initial_latency + MAX_DELAY_INCREMENT;
        }
        if (ctrl->adaptive_latency < ctrl->max_rx_interval) {
            ctrl->adaptive_latency = ctrl->max_rx_interval;
            a2dp_media_update_delay_report_time(ctrl->stream, ctrl->adaptive_latency);
            ctrl->detect_timeout = jiffies + msecs_to_jiffies(A2DP_FLUENT_DETECT_INTERVAL);
            ctrl->max_rx_interval = ctrl->initial_latency;
            return;
        }
        /*printf("---rx interval, adaptive : %dms, %dms---\n", ctrl->adaptive_latency, ctrl->max_rx_interval);*/
    }

    if (time_after(jiffies, ctrl->detect_timeout)) {
        ctrl->adaptive_latency -= DELAY_DECREMENT;
        if (ctrl->adaptive_latency < ctrl->max_rx_interval) {
            ctrl->adaptive_latency = ctrl->max_rx_interval;
        }

        if (ctrl->adaptive_latency < ctrl->initial_latency) {
            ctrl->adaptive_latency = ctrl->initial_latency;
        }
        /*printf("---adaptive detect : %dms, %dms---\n", ctrl->adaptive_latency, ctrl->max_rx_interval);*/
        if (ctrl->low_latency) {
            ctrl->max_rx_interval -= 10;//ctrl->initial_latency;
        } else {
            ctrl->max_rx_interval = ctrl->initial_latency;
        }
        ctrl->detect_timeout = jiffies + msecs_to_jiffies(A2DP_FLUENT_DETECT_INTERVAL);
        a2dp_media_update_delay_report_time(ctrl->stream, ctrl->adaptive_latency);
    }
#endif
}


void *a2dp_stream_control_plan_select(void *stream, int low_latency, u32 codec_type, u8 plan)
{
    struct a2dp_stream_control *ctrl = (struct a2dp_stream_control *)zalloc(sizeof(struct a2dp_stream_control));

    if (!ctrl) {
        return NULL;
    }

    switch (plan) {

    case A2DP_STREAM_JL_DONGLE_CONTROL:
        if (CONFIG_DONGLE_SPEAK_ENABLE) {
            ctrl->initial_latency = CONFIG_JL_DONGLE_PLAYBACK_LATENCY;
            ctrl->adaptive_latency = ctrl->initial_latency;
            ctrl->max_rx_interval = ctrl->initial_latency;
            ctrl->adaptive_max_latency = low_latency ? (ctrl->initial_latency + MAX_DELAY_INCREMENT) : CONFIG_A2DP_ADAPTIVE_MAX_LATENCY;
            ctrl->detect_timeout = jiffies + msecs_to_jiffies(A2DP_FLUENT_DETECT_INTERVAL);
            ctrl->jl_dongle = 1;
        }
        break;
    default:
        if (low_latency) {
            if (codec_type == A2DP_CODEC_MPEG24) {
                ctrl->initial_latency = CONFIG_A2DP_DELAY_TIME_AAC_LO;
#if (defined(TCFG_BT_SUPPORT_LDAC) && TCFG_BT_SUPPORT_LDAC)
            } else if (codec_type == A2DP_CODEC_LDAC) {
                ctrl->initial_latency = CONFIG_A2DP_DELAY_TIME_LDAC_LO;
#endif
#if (defined(TCFG_BT_SUPPORT_LHDC) && TCFG_BT_SUPPORT_LHDC)
            } else if (codec_type == A2DP_CODEC_LHDC) {
                ctrl->initial_latency = CONFIG_A2DP_DELAY_TIME_LHDC_LO;
#endif
            } else {
                ctrl->initial_latency = CONFIG_A2DP_DELAY_TIME_SBC_LO;
            }
        } else {
            if (codec_type == A2DP_CODEC_MPEG24) {
                ctrl->initial_latency = CONFIG_A2DP_DELAY_TIME_AAC;
#if (defined(TCFG_BT_SUPPORT_LDAC) && TCFG_BT_SUPPORT_LDAC)
            } else if (codec_type == A2DP_CODEC_LDAC) {
                ctrl->initial_latency = CONFIG_A2DP_DELAY_TIME_LDAC;
#endif
#if (defined(TCFG_BT_SUPPORT_LHDC) && TCFG_BT_SUPPORT_LHDC)
            } else if (codec_type == A2DP_CODEC_LHDC) {
                ctrl->initial_latency = CONFIG_A2DP_DELAY_TIME_LHDC;
#endif
            } else {
                ctrl->initial_latency = CONFIG_A2DP_DELAY_TIME_SBC;
            }
        }
        ctrl->adaptive_max_latency = low_latency ? (ctrl->initial_latency + MAX_DELAY_INCREMENT) : CONFIG_A2DP_ADAPTIVE_MAX_LATENCY;
        ctrl->adaptive_latency = ctrl->initial_latency;
        ctrl->max_rx_interval = ctrl->initial_latency;
        ctrl->detect_timeout = jiffies + msecs_to_jiffies(A2DP_FLUENT_DETECT_INTERVAL);
        break;
    }

    ctrl->repair_frame[0] = 0x02;
    ctrl->repair_frame[1] = 0x00;
    ctrl->low_latency = low_latency;
    ctrl->first_in = 1;
    ctrl->codec_type = codec_type;
    ctrl->plan = plan;
    ctrl->stream = stream;
    a2dp_media_update_delay_report_time(ctrl->stream, ctrl->adaptive_latency);
    return ctrl;
}

static void a2dp_stream_underrun_signal(void *arg)
{
    struct a2dp_stream_control *ctrl = (struct a2dp_stream_control *)arg;

    local_irq_disable();
    if (ctrl->underrun_callback) {
        ctrl->underrun_callback(ctrl->underrun_signal);
    }
    ctrl->timer = 0;
    local_irq_enable();
}

static int a2dp_audio_is_underrun(struct a2dp_stream_control *ctrl)
{
    int underrun_time = ctrl->low_latency ? 2 : 30;
    if (ctrl->next_timestamp) {
        u32 reference_clock = bt_audio_reference_clock_time(0);
        if (reference_clock == (u32) - 1) {
            return true;
        }
        u32 reference_time = reference_clock * 625 * TIMESTAMP_US_DENOMINATOR;
        int distance_time = ctrl->next_timestamp - reference_time;
        if (distance_time > 67108863L || distance_time < -67108863L) {
            if (ctrl->next_timestamp > reference_time) {
                distance_time = ctrl->next_timestamp - 0xffffffff - reference_time;
            } else {
                distance_time = 0xffffffff - reference_time + ctrl->next_timestamp;
            }
        }
        distance_time = distance_time / 1000 / TIMESTAMP_US_DENOMINATOR;
        /*printf("distance_time %d %u %u\n", distance_time, ctrl->next_timestamp, reference_time);*/
        if (distance_time <= underrun_time) { //判断是否已经超时，如果已经超时，返回欠载(临界时，也返回欠载)，
            return true;                      //如果没有欠载，下面设置自检timer,
        }

        local_irq_disable();
        if (ctrl->timer) {
            sys_hi_timeout_del(ctrl->timer);
            ctrl->timer = 0;
        }
        ctrl->timer = sys_hi_timeout_add(ctrl, a2dp_stream_underrun_signal, distance_time - underrun_time);
        local_irq_enable();
    }

    return 0;
}

static int a2dp_stream_underrun_handler(struct a2dp_stream_control *ctrl, struct a2dp_media_frame *frame)
{
    if (!a2dp_audio_is_underrun(ctrl)) {
        /*putchar('x');*/
        return 0;
    }
    putchar('X');

    a2dp_stream_underrun_adaptive_handler(ctrl);
    if (ctrl->stream_error != A2DP_STREAM_UNDERRUN) {
        if (!ctrl->stream_error) {

        }
        ctrl->stream_error = A2DP_STREAM_UNDERRUN;
    }
    ctrl->repair_num++;
    frame->packet = ctrl->repair_frame;

    return sizeof(ctrl->repair_frame);
}

static int a2dp_stream_overrun_handler(struct a2dp_stream_control *ctrl, struct a2dp_media_frame *frame, int *len)
{
    while (1) {
        if (1) {//!ctrl->low_latency) {
            int msecs = a2dp_media_get_remain_play_time(ctrl->stream, 1);
            if (msecs < ctrl->adaptive_latency) {
                /*printf("adaptive latency %d, msecs %d\n", ctrl->adaptive_latency, msecs);*/
                break;
            }
        }

        int rlen = a2dp_media_try_get_packet(ctrl->stream, frame);
        if (rlen <= 0) {
            break;
        }
        *len = rlen;
        putchar('n');
        return 1;
    }

    *len = sizeof(ctrl->repair_frame);
    frame->packet = ctrl->repair_frame;
    putchar('o');
    return 0;
}

static int a2dp_stream_missed_handler(struct a2dp_stream_control *ctrl, struct a2dp_media_frame *frame, int *len)
{
    printf("missed num : %d\n", ctrl->missed_num);
    if (--ctrl->missed_num == 0) {
        return 1;
    }

    *len = sizeof(ctrl->repair_frame);
    frame->packet = ctrl->repair_frame;
    return 0;
}

static int a2dp_stream_error_filter(struct a2dp_stream_control *ctrl, struct a2dp_media_frame *frame, int len)
{
    int err = 0;
    u8 repair = 0;

    if (len == 2) {
        repair = 1;
    }

    if (!repair) {
        int header_len = a2dp_media_get_rtp_header_len(ctrl->codec_type, frame->packet, len);
        if (header_len >= len) {
            printf("##A2DP header error : %d\n", header_len);
            a2dp_media_free_packet(ctrl->stream, frame->packet);
            return -EFAULT;
        }
    }

    u16 seqn = repair ? ctrl->seqn : RB16(frame->packet + 2);
    if (ctrl->first_in) {
        ctrl->first_in = 0;
        goto __exit;
    }
    if (seqn != ctrl->seqn) {
        if (ctrl->stream_error == A2DP_STREAM_UNDERRUN) {
            int missed_frames = (u16)(seqn - ctrl->seqn) - 1;
            if (missed_frames > ctrl->repair_num) {
                ctrl->stream_error = A2DP_STREAM_MISSED;
                ctrl->missed_num = 2;//missed_frames - ctrl->repair_num + 1;
                /*printf("case 0 : %d, %d\n", missed_frames, ctrl->repair_num);*/
                err = -EAGAIN;
            } else if (missed_frames < ctrl->repair_num) {
                ctrl->stream_error = A2DP_STREAM_OVERRUN;
                ctrl->overrun_seqn = seqn + ctrl->repair_num - missed_frames;
                /*printf("case 1 : %d, %d, seqn : %d, %d\n", missed_frames, ctrl->repair_num, seqn, ctrl->overrun_seqn);*/
                err = -EAGAIN;
            }
        } else if (!ctrl->stream_error && (u16)(seqn - ctrl->seqn) > 1) {
            err = -EAGAIN;
            /*if ((u16)(seqn - ctrl->seqn) > 32768) {
                return err;
            }*/
            ctrl->stream_error = A2DP_STREAM_MISSED;
            ctrl->missed_num = 2;//(u16)(seqn - ctrl->seqn);
            /*printf("case 2 : %d, %d, %d\n", seqn, ctrl->seqn, ctrl->missed_num);*/
        }
        ctrl->repair_num = 0;
    }

__exit:
    ctrl->seqn = seqn;
    return err;
}


int a2dp_stream_control_pull_frame(void *_ctrl, struct a2dp_media_frame *frame, int *len)
{
    struct a2dp_stream_control *ctrl = (struct a2dp_stream_control *)_ctrl;

    if (!ctrl) {
        *len = 0;
        return 0;
    }

    int rlen = 0;
    int err = 0;
    u8 new_packet = 0;

try_again:
    switch (ctrl->stream_error) {
    case A2DP_STREAM_OVERRUN:
        new_packet = a2dp_stream_overrun_handler(ctrl, frame, len);
        break;
    case A2DP_STREAM_MISSED:
        new_packet = a2dp_stream_missed_handler(ctrl, frame, len);
        if (!new_packet) {
            break;
        }
    //注意：这里不break是因为很有可能由于位流的错误导致补包无法再正常补上
    default:
        rlen = a2dp_media_try_get_packet(ctrl->stream, frame);
        if (rlen <= 0) {
            rlen = a2dp_stream_underrun_handler(ctrl, frame);
        } else {
            new_packet = 1;
        }
        *len = rlen;
        break;
    }

    if (*len <= 0) {
        return 0;
    }
    err = a2dp_stream_error_filter(ctrl, frame, *len);
    if (err) {
        if (-err == EAGAIN) {
            a2dp_media_put_packet(ctrl->stream, frame->packet);
            goto try_again;
        }
        *len = 0;
        return 0;
    }

    a2dp_stream_adaptive_detect_handler(ctrl, new_packet, frame->clkn);

    if (ctrl->stream_error) {
        if (new_packet) {
            ctrl->stream_error = 0;
            return FRAME_FLAG_RESET_TIMESTAMP_BIT;
        }
        return FRAME_FLAG_FILL_PACKET;
    }

    return 0;
}

void a2dp_stream_control_free_frame(void *_ctrl, struct a2dp_media_frame *frame)
{
    struct a2dp_stream_control *ctrl = (struct a2dp_stream_control *)_ctrl;

    if ((u8 *)frame->packet != (u8 *)ctrl->repair_frame) {
        a2dp_media_free_packet(ctrl->stream, frame->packet);
    }
}

void a2dp_stream_control_set_underrun_callback(void *_ctrl, void *priv, void (*callback)(void *priv))
{
    struct a2dp_stream_control *ctrl = (struct a2dp_stream_control *)_ctrl;

    ctrl->underrun_signal = priv;
    ctrl->underrun_callback = callback;
}

int a2dp_stream_control_delay_time(void *_ctrl)
{
    struct a2dp_stream_control *ctrl = (struct a2dp_stream_control *)_ctrl;

    if (ctrl) {
        return ctrl->adaptive_latency;
    }

    return CONFIG_A2DP_DELAY_TIME_AAC;
}

void a2dp_stream_control_free(void *_ctrl)
{
    struct a2dp_stream_control *ctrl = (struct a2dp_stream_control *)_ctrl;

    if (!ctrl) {
        return;
    }

    local_irq_disable();
    if (ctrl->timer) {
        sys_hi_timeout_del(ctrl->timer);
        ctrl->timer = 0;
    }
    local_irq_enable();
    free(ctrl);
}
