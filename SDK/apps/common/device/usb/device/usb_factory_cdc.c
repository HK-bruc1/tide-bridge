#include "usb/device/usb_factory_cdc_internal.h"
#include "usb/device/usb_factory.h"
#include "usb/usb_task.h"
#include "system/includes.h"

#if TCFG_T2620_FACTORY_USB_CDC_ENABLE
/* One USB task owns all endpoint I/O and release. IRQ exclusion protects
 * short queue operations against reset/control IRQs and task preemption.
 * No mutex, sleeping operation, or application callback inside exclusion. */
struct factory_tx_item {
    u16 length;
    u16 offset;
    u8 bytes[FACTORY_CDC_TX_MAX];
};
static struct {
    u32 generation;
    u32 tx_since;
    u32 queued_generation;
    u16 timer;
    u16 tx_pending_length;
    u8 active, ready, posted, restart;
#if TCFG_T2620_FACTORY_USB_CDC_TEST_ENABLE
    u8 test_open_pending;
#endif
    u8 head, count, rx_length, tx_wait, zlp;
    u8 rx[FACTORY_CDC_PACKET];
    struct factory_tx_item tx[FACTORY_CDC_TX_DEPTH];
    factory_cdc_rx_cb receive;
} transport;

/* Capture under IRQ exclusion; format/output ONLY from usb_stack with IRQs on.
 * A bounded queue makes overload visible without blocking endpoint handling. */
enum cdc_log_event {
    CDC_LOG_START, CDC_LOG_RESET, CDC_LOG_CONFIG, CDC_LOG_CLOSE,
    CDC_LOG_STOP, CDC_LOG_TIMEOUT, CDC_LOG_OPEN, CDC_LOG_RX,
    CDC_LOG_TX_QUEUED, CDC_LOG_TX_REJECT, CDC_LOG_TX_SUBMIT,
    CDC_LOG_TX_DONE
};
#if TCFG_T2620_FACTORY_USB_CDC_LOG_LEVEL
#define CDC_LOG_DEPTH 16
#define CDC_LOG_BYTES 32
static struct {
    u32 ms, generation, length, count;
    u8 event, captured;
    int result;
    u8 bytes[CDC_LOG_BYTES];
} cdc_logs[CDC_LOG_DEPTH];
static u8 cdc_log_head, cdc_log_count;
static u32 cdc_log_dropped;
static u32 cdc_log_since;
#define CDC_LOG_INTERVAL_MS 1000
static void cdc_log_capture(u8 event, u32 generation, const u8 *bytes,
                            u32 length, int result)
{
    if (TCFG_T2620_FACTORY_USB_CDC_LOG_LEVEL < 2 &&
        (event == CDC_LOG_TX_QUEUED || event == CDC_LOG_TX_SUBMIT)) return;
    local_irq_disable();
    /* Summary mode merges endpoint traffic and repeated errors per session.
     * Never copy payload or print here, including calls from control IRQs. */
    if (TCFG_T2620_FACTORY_USB_CDC_LOG_LEVEL == 1 &&
        (event == CDC_LOG_RX || event == CDC_LOG_TX_DONE || event == CDC_LOG_TX_REJECT)) {
        for (u8 i = 0; i < cdc_log_count; ++i) {
            u8 index = (cdc_log_head + i) % CDC_LOG_DEPTH;
            if (cdc_logs[index].event == event &&
                cdc_logs[index].generation == generation && cdc_logs[index].result == result) {
                cdc_logs[index].length += length;
                ++cdc_logs[index].count;
                local_irq_enable();
                return;
            }
        }
    }
    if (cdc_log_count == CDC_LOG_DEPTH) {
        ++cdc_log_dropped;
    } else {
        u8 index = (cdc_log_head + cdc_log_count) % CDC_LOG_DEPTH;
        cdc_logs[index].ms = sys_timer_get_ms();
        cdc_logs[index].generation = generation;
        cdc_logs[index].event = event;
        cdc_logs[index].length = length;
        cdc_logs[index].count = 1;
        cdc_logs[index].result = result;
        u32 n = bytes && TCFG_T2620_FACTORY_USB_CDC_LOG_LEVEL >= 2 ? length : 0;
        if (n > CDC_LOG_BYTES) n = CDC_LOG_BYTES;
        cdc_logs[index].captured = n;
        if (n) memcpy(cdc_logs[index].bytes, bytes, n);
        ++cdc_log_count;
    }
    local_irq_enable();
}
static void cdc_log_flush(int force)
{
    static const char *names[] = {
        "START", "RESET", "CONFIG", "CLOSE", "STOP", "TIMEOUT", "OPEN",
        "RX", "TX_QUEUED", "TX_REJECT", "TX_SUBMIT", "TX_DONE"
    };
    u32 now = sys_timer_get_ms();
    if (!force && (u32)(now - cdc_log_since) < CDC_LOG_INTERVAL_MS) return;
    cdc_log_since = now;
    for (u8 i = 0; i <= CDC_LOG_DEPTH; ++i) {
        local_irq_disable();
        if (!cdc_log_count) {
            u32 dropped = cdc_log_dropped;
            cdc_log_dropped = 0;
            local_irq_enable();
            if (dropped) printf("[FACTORY_CDC] LOG_DROPPED=%u\n", dropped);
            break;
        }
        typeof(cdc_logs[0]) record = cdc_logs[cdc_log_head];
        cdc_log_head = (cdc_log_head + 1) % CDC_LOG_DEPTH;
        --cdc_log_count;
        local_irq_enable();
        char hex[CDC_LOG_BYTES * 3 + 1];
        static const char digits[] = "0123456789ABCDEF";
        for (u8 j = 0; j < record.captured; ++j) {
            hex[j * 3] = digits[record.bytes[j] >> 4];
            hex[j * 3 + 1] = digits[record.bytes[j] & 15];
            hex[j * 3 + 2] = ' ';
        }
        hex[record.captured * 3] = 0;
        printf("[FACTORY_CDC] ms=%u gen=%u %s len=%u count=%u rc=%d %s%s\n",
               record.ms, record.generation, names[record.event],
               (u32)record.length, record.count, record.result, hex,
               record.captured && record.length > record.captured ? "..." : "");
    }
}
#else
#define cdc_log_capture(event, generation, bytes, length, result) ((void)0)
#define cdc_log_flush(force) ((void)0)
#endif

static void factory_cdc_kick(void *unused)
{
    local_irq_disable();
    if (transport.active && !transport.posted) {
        transport.posted = 1;
        transport.queued_generation = transport.generation;
        if (os_taskq_post_msg(USB_TASK_NAME, 1, USBSTACK_FACTORY_CDC_IO)) {
            transport.posted = 0; /* periodic tick compensates a full queue */
        }
    }
    local_irq_enable();
}

void factory_cdc_rx_irq(void)
{
    /* Endpoint IRQ is masked by the driver until the USB task drains it.
     * The periodic kick also covers lost notifications and TX progress. */
    factory_cdc_kick(NULL);
}

void factory_cdc_invalidate(enum factory_cdc_reason reason)
{
    local_irq_disable();
    if (++transport.generation == 0) {
        ++transport.generation;
    }
    cdc_log_capture((u8)reason, transport.generation, NULL, 0, 0);
    transport.ready = 0;
#if TCFG_T2620_FACTORY_USB_CDC_TEST_ENABLE
    transport.test_open_pending = 0;
#endif
    transport.head = transport.count = transport.rx_length = 0;
    transport.tx_wait = transport.zlp = 0;
    transport.tx_pending_length = 0;
    /* A close with in-flight DMA or timeout needs physical reenumeration.
     * Reset must not cancel an already requested rebuild. */
    if (reason == FACTORY_CDC_CLOSE || reason == FACTORY_CDC_TIMEOUT) {
        transport.restart = 1;
    }
    local_irq_enable();
}

void factory_cdc_open(void)
{
    local_irq_disable();
    if (transport.active && !transport.ready && !transport.restart) {
        transport.ready = 1;
        cdc_log_capture(CDC_LOG_OPEN, transport.generation, NULL, 0, 0);
#if TCFG_T2620_FACTORY_USB_CDC_TEST_ENABLE
        if (transport.receive == factory_cdc_test_receive) {
            transport.test_open_pending = 1;
        }
#endif
    }
    local_irq_enable();
}

void factory_cdc_set_rx_handler(factory_cdc_rx_cb handler)
{
    local_irq_disable();
    transport.receive = handler;
    local_irq_enable();
}

u32 factory_cdc_generation(void)
{
    u32 generation;
    local_irq_disable();
    generation = transport.ready ? transport.generation : 0;
    local_irq_enable();
    return generation;
}

int factory_cdc_send(u32 generation, const u8 *bytes, u32 length)
{
    int result = FACTORY_CDC_OK;
    if (!bytes || !length || length > FACTORY_CDC_TX_MAX) {
        cdc_log_capture(CDC_LOG_TX_REJECT, generation, NULL, length, FACTORY_CDC_INVALID);
        return FACTORY_CDC_INVALID;
    }
    local_irq_disable();
    if (!transport.ready || generation != transport.generation) {
        result = FACTORY_CDC_STALE;
    } else if (transport.count == FACTORY_CDC_TX_DEPTH) {
        result = FACTORY_CDC_FULL;
    } else {
        u8 index = (transport.head + transport.count) % FACTORY_CDC_TX_DEPTH;
        struct factory_tx_item *item = &transport.tx[index];
        memcpy(item->bytes, bytes, length);
        item->length = length;
        item->offset = 0;
        if (!transport.count && !transport.tx_wait) {
            transport.tx_since = sys_timer_get_ms();
        }
        ++transport.count;
    }
    cdc_log_capture(result ? CDC_LOG_TX_REJECT : CDC_LOG_TX_QUEUED,
                    generation, bytes, length, result);
    local_irq_enable();
    return result;
}

int factory_cdc_start(void)
{
    local_irq_disable();
    transport.restart = 0;
    factory_cdc_invalidate(FACTORY_CDC_START);
    transport.active = 1;
#if TCFG_T2620_FACTORY_USB_CDC_TEST_ENABLE
    if (!transport.receive) transport.receive = factory_cdc_test_receive;
#endif
    local_irq_enable();
    transport.timer = usr_timer_add(NULL, factory_cdc_kick, 10, 1);
    if (!transport.timer) {
        local_irq_disable();
        factory_cdc_invalidate(FACTORY_CDC_TIMEOUT);
        local_irq_enable();
        return -1;
    }
    return 0;
}

/* Caller-task safe: disable software I/O without touching USB registers,
 * timers, logging or buffers still owned by DMA. usb_stack releases resources. */
void factory_cdc_quiesce(void)
{
    local_irq_disable();
    transport.active = 0;
    transport.ready = 0;
    local_irq_enable();
}

void factory_cdc_stop(void)
{
    local_irq_disable();
    transport.active = 0;
    factory_cdc_invalidate(FACTORY_CDC_STOP);
    local_irq_enable();
    if (transport.timer) {
        usr_timer_del(transport.timer);
        transport.timer = 0;
    }
    cdc_log_flush(1);
    /* A posted message keeps its generation until consumed, even across
     * stop/start. It contains no handle or buffer pointer. */
}

int factory_cdc_needs_restart(void)
{
    local_irq_disable();
    int restart = transport.restart;
    local_irq_enable();
    return restart;
}

void factory_cdc_process(void)
{
    cdc_log_flush(0);
    u8 bytes[FACTORY_CDC_PACKET];
    u32 generation, length = 0;
    factory_cdc_rx_cb receive = NULL;
    local_irq_disable();
    generation = transport.queued_generation;
    transport.posted = 0;
    if (!transport.active || !transport.ready ||
        generation != transport.generation || !cdc_factory_configured()) {
        local_irq_enable();
        return;
    }
#if TCFG_T2620_FACTORY_USB_CDC_TEST_ENABLE
    if (transport.test_open_pending) {
        transport.test_open_pending = 0;
        local_irq_enable();
        factory_cdc_test_open(generation);
        local_irq_disable();
        if (!transport.active || !transport.ready || generation != transport.generation) {
            local_irq_enable();
            return;
        }
    }
#endif
    u32 now = sys_timer_get_ms();
    if ((transport.count || transport.tx_wait) &&
        (u32)(now - transport.tx_since) >= FACTORY_CDC_TIMEOUT_MS) {
        factory_cdc_invalidate(FACTORY_CDC_TIMEOUT);
        local_irq_enable();
        return;
    }
    /* A committed packet remains outstanding until hardware consumes it,
     * including the final packet/ZLP. Timeout covers a stopped host reader. */
    if (transport.tx_wait && !cdc_factory_tx_busy()) {
        if (transport.tx_pending_length) {
            cdc_log_capture(CDC_LOG_TX_DONE, generation, NULL, transport.tx_pending_length, 0);
            transport.tx_pending_length = 0;
        }
        transport.tx_wait = 0;
        transport.tx_since = now;
    }
    if (!transport.tx_wait && transport.zlp) {
        if (cdc_factory_write_packet(NULL, 0) == 0) {
            cdc_log_capture(CDC_LOG_TX_SUBMIT, generation, NULL, 0, 0);
            transport.zlp = 0;
            transport.tx_wait = 1;
        }
    } else if (!transport.tx_wait && transport.count) {
        struct factory_tx_item *item = &transport.tx[transport.head];
        u32 n = item->length - item->offset;
        if (n > FACTORY_CDC_PACKET) n = FACTORY_CDC_PACKET;
        int written = cdc_factory_write_packet(item->bytes + item->offset, n);
        if (written > 0 && written <= n) {
            cdc_log_capture(CDC_LOG_TX_SUBMIT, generation, item->bytes + item->offset, written, 0);
            transport.tx_pending_length = written;
            item->offset += written; /* count before examining any new state */
            transport.tx_wait = 1;
            if (item->offset == item->length) {
                transport.zlp = written == FACTORY_CDC_PACKET;
                transport.head = (transport.head + 1) % FACTORY_CDC_TX_DEPTH;
                --transport.count;
            }
        } else if (written > (int)n) {
            factory_cdc_invalidate(FACTORY_CDC_TIMEOUT);
        }
    }
    /* One packet and one callback per dispatch. A rejecting consumer keeps
     * the packet pending; do not drain hardware into an unbounded queue. */
    if (transport.ready && transport.receive && transport.count < FACTORY_CDC_TX_DEPTH) {
        if (!transport.rx_length) {
            int n = cdc_factory_read_packet(transport.rx);
            if (n > 0 && n <= FACTORY_CDC_PACKET) {
                transport.rx_length = n;
                cdc_log_capture(CDC_LOG_RX, generation, transport.rx, n, 0);
            }
        }
        length = transport.rx_length;
        if (length) {
            memcpy(bytes, transport.rx, length);
            receive = transport.receive;
        }
    }
    local_irq_enable();
    if (receive) {
        int accepted = receive(generation, bytes, length);
        local_irq_disable();
        if (!accepted && generation == transport.generation) {
            transport.rx_length = 0;
        }
        local_irq_enable();
    }
}
#endif
