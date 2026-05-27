#include "app_main.h"
#include "system/includes.h"
#include "generic/lbuf.h"

#define DLOG_OUTPUT_LBUF_MAX_SIZE       (1 * 1024)

#define DLOG_OUTPUT_BY_UART     (1 << 0)
#define DLOG_OUTPUT_BY_SPP      (1 << 1)

#define DLOG_OUT_CHANNEL        0 //(DLOG_OUTPUT_BY_UART | DLOG_OUTPUT_BY_SPP)

#define DLOG_OUT_MERGE_EN       1   //短时间内的log进行合并，主要用于处理SPP发送太频繁，uart可以不开
#define DLOG_OUT_MERGE_TIME     100 //短时间内的log合并成一条。单位:ms
#define DLOG_OUT_MERGE_SIZE     512 //当合并的log超过该长度时立即发送

#if DLOG_OUT_CHANNEL
void dlog_output_resume();

extern const struct dlog_output_channel_s dlog_out_channel_begin[];
extern const struct dlog_output_channel_s dlog_out_channel_end[];

#define list_for_each_dlog_channel(p) \
    for (p = dlog_out_channel_begin; p < dlog_out_channel_end; p++)

struct dlog_output {
    struct lbuff_head *lbuf_head;
    struct logbuf *last_log;
    u16 total_size;
    u16 timer;
} g_dlog_output = {0};

static DEFINE_SPINLOCK(g_dlog_output_lock);

int dlog_output_channel_init()
{
    //串口DMA要求4 byte对齐，且为连续ram，因此用dma_malloc和lbuf
    printf("%s", __func__);

    g_dlog_output.lbuf_head = dma_malloc(DLOG_OUTPUT_LBUF_MAX_SIZE);

    if (g_dlog_output.lbuf_head) {
        lbuf_init(g_dlog_output.lbuf_head, DLOG_OUTPUT_LBUF_MAX_SIZE, 4, 0);
    }
    return 0;
}
late_initcall(dlog_output_channel_init);

static void dlog_output_timeout_handler()
{
    g_dlog_output.timer = 0;
    dlog_output_resume();
    /* int msg[] = {(int)dlog_output_resume, 0}; */
    /* os_taskq_post_type("app_core", Q_CALLBACK, ARRAY_SIZE(msg), msg); */
}

void dlog_output_resume()
{
    if (NULL == g_dlog_output.lbuf_head) {
        return;
    }

    if (g_dlog_output.timer) {
        return;
    }

    const struct dlog_output_channel_s *p = NULL;

    spin_lock(&g_dlog_output_lock);
    list_for_each_dlog_channel(p) { //等所有通道都发送完成后再继续发
        if (p->is_busy && p->is_busy()) {
            spin_unlock(&g_dlog_output_lock);
            return;
        }
    }

    if (g_dlog_output.last_log) {
        lbuf_free(g_dlog_output.last_log);
        g_dlog_output.last_log = NULL;
    }

    if (lbuf_empty(g_dlog_output.lbuf_head)) {
        spin_unlock(&g_dlog_output_lock);
        return;
    }

    struct logbuf *log = (struct logbuf *)lbuf_pop((void *)g_dlog_output.lbuf_head, 1);
    u16 offset = log->buf_len;

#if DLOG_OUT_MERGE_EN
    if (log->buf_len < g_dlog_output.total_size) { //合并输出
        u8 *merge_buf = malloc(g_dlog_output.total_size);

        if (merge_buf) {
            offset = 0;

            //合并lbuf
            while (1) {
                memcpy(merge_buf + offset, log->buf, log->buf_len);
                offset += log->buf_len;
                lbuf_free(log);

                if (offset >= g_dlog_output.total_size) {
                    break;
                }

                log = (struct logbuf *)lbuf_pop((void *)g_dlog_output.lbuf_head, 1);
                if (NULL == log) {
                    break;
                }
            }

            if (offset != g_dlog_output.total_size) {
                printf("dlog_merge, offset:%d, total_size:%d", offset, g_dlog_output.total_size);
            }

            log = (struct logbuf *)lbuf_alloc((void *)g_dlog_output.lbuf_head, offset + sizeof(struct logbuf));
            if (log) {
                log->len = 0; //暂时未用到
                log->buf_len = offset;
                memcpy(log->buf, merge_buf, offset);
            }
            free(merge_buf);
        }
    }
#endif

    g_dlog_output.total_size -= offset;

    list_for_each_dlog_channel(p) {
        if (p->send) {
            p->send((const char *)log->buf, log->buf_len);
        }
    }
    g_dlog_output.last_log = log;
    spin_unlock(&g_dlog_output_lock);
}

int dlog_output_direct(void *buf, u16 len)
{
    if (NULL == g_dlog_output.lbuf_head || !len) {
        return -1;
    }

    spin_lock(&g_dlog_output_lock);

    struct logbuf *new_p = (struct logbuf *)lbuf_alloc((void *)g_dlog_output.lbuf_head, len + sizeof(struct logbuf));

    if (new_p) {
        g_dlog_output.total_size += len;

        new_p->len = 0; //暂时未用到
        new_p->buf_len = len;
        memcpy(new_p->buf, buf, len);
        lbuf_push((void *)new_p, 1);

#if DLOG_OUT_MERGE_EN
        if (g_dlog_output.timer) {
            if (g_dlog_output.total_size >= DLOG_OUT_MERGE_SIZE) {
                usr_timeout_del(g_dlog_output.timer);
                g_dlog_output.timer = 0;
                dlog_output_resume();
            }
        } else {
            dlog_output_resume();
            g_dlog_output.timer = usr_timeout_add(NULL, dlog_output_timeout_handler, DLOG_OUT_MERGE_TIME, 1);
        }
#else
        dlog_output_resume();
#endif
    } else {
        printf("%s, alloc_fail", __FUNCTION__);
        put_buf(buf, len);
    }

    spin_unlock(&g_dlog_output_lock);
    return 0;
}
#endif

#if DLOG_OUT_CHANNEL & DLOG_OUTPUT_BY_UART
#include "chargestore/chargestore.h"
#include "asm/charge.h"

#define DLOG_UART_TX_PIN        IO_PORT_LDOIN

struct dlog_uart_s {
    int uart_num;
    u8  busy;
} dlog_uart = {
    .uart_num   = -1,
};

static void dlog_uart_isr(uart_dev uart_num, enum uart_event event)
{
    if (event == UART_EVENT_TX_DONE) {
        dlog_uart.busy = 0;
        dlog_output_resume();
    }
}

int dlog_uart_is_busy()
{
    return dlog_uart.busy;
}

int dlog_uart_send(const void *buf, u16 len)
{
    if (dlog_uart.uart_num == -1) {
        return -1;
    }
    dlog_uart.busy = 1;
    int ret = uart_send_bytes(dlog_uart.uart_num, buf, len);
    return 0;
}

//使能UART实时解析dlog
void dlog_uart_init()
{
    printf("%s", __func__);

    if (dlog_uart.uart_num != -1) {
        return;
    }

    struct uart_config config = {0};
    struct uart_dma_config dma = {0};
    int ret = 0;

    if (DLOG_UART_TX_PIN == TCFG_CHARGESTORE_PORT) {
        chargestore_api_stop();
    }
    if (DLOG_UART_TX_PIN == IO_PORT_LDOIN) {
        charge_module_stop();
    }

    config.baud_rate = 2000000;
    config.tx_pin = DLOG_UART_TX_PIN;
    config.rx_pin = 0xFFFF;
    dlog_uart.uart_num = uart_init(-1, &config);
    ASSERT(dlog_uart.uart_num >= 0, "%s, uart_init_fail: %d", __func__, dlog_uart.uart_num);

    dma.rx_timeout_thresh = 20 * 1000000 / config.baud_rate; //us
    dma.frame_size = DLOG_OUTPUT_LBUF_MAX_SIZE;
    dma.event_mask = UART_EVENT_TX_DONE;
    dma.irq_priority = 2;
    dma.irq_callback = dlog_uart_isr;
    ret = uart_dma_init(dlog_uart.uart_num, &dma);
    ASSERT(ret >= 0, "%s, uart_dma_init_fail: %d", __func__, ret);
}

//关闭UART实时解析dlog
void dlog_uart_deinit()
{
    printf("%s", __func__);

    if (!dlog_uart.uart_num) {
        return;
    }

    uart_deinit(dlog_uart.uart_num);
    dlog_uart.uart_num = -1;

    if (DLOG_UART_TX_PIN == TCFG_CHARGESTORE_PORT) {
        chargestore_api_restart();
    }
    if (DLOG_UART_TX_PIN == IO_PORT_LDOIN) {
        charge_module_restart();
    }
}

int dlog_uart_running()
{
    return dlog_uart.uart_num >= 0;
}

REGISTER_DLOG_OUT_CHANNLE(uart) = {
    .send       = dlog_uart_send,
    .is_busy    = dlog_uart_is_busy,
};
#endif

#if DLOG_OUT_CHANNEL & DLOG_OUTPUT_BY_SPP
static u8 dlog_output_spp = 0;

void dlog_output_by_spp_set(u8 enable)
{
    dlog_output_spp = enable;
}

u8 dlog_output_by_spp_get()
{
    return dlog_output_spp;
}

int dlog_spp_send(const void *buf, u16 len)
{
    //一拖二时需要传递远端地址
    if (dlog_output_spp) {
        bt_cmd_prepare_for_addr(NULL, USER_CTRL_SPP_SEND_DATA, len, (u8 *)buf);
    }
    return 0;
}

int dlog_spp_is_busy()
{
    return 0;
}

REGISTER_DLOG_OUT_CHANNLE(spp) = {
    .send       = dlog_spp_send,
    .is_busy    = dlog_spp_is_busy,
};
#endif

