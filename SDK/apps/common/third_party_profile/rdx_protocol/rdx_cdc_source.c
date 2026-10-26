#include "rdx_cdc_source.h"
#include "rdx_record_format.h"
#include "rdx_storage_lifecycle.h"
#include "rdx_app.h"
#include "rdx_dut.h"
#include "rdx_factory_audio.h"
#include "rdx_factory_cdc.h"
#include "rdx_dip_switch.h"
#include "system/includes.h"
#include "fs/fs.h"

#if TCFG_T2620_CDC_FILE_ENABLE
#define SOURCE_BLOCK 4096
static struct {
    OS_SEM wake;
    FILE *handle;
    volatile u8 busy, abort, closed, error, detached, operation;
    u8 created, head, tail, opened;
    volatile u8 ready[2];
    u32 length[2], size, offset, sn;
    u32 open_ms, read_ms, read_max_ms, reads;
    volatile u32 since;
    char name[12];
    u8 data[2][SOURCE_BLOCK];
} source;

static void source_operation(u8 operation)
{
    local_irq_disable();
    source.since = sys_timer_get_ms();
    source.operation = operation;
    local_irq_enable();
}

static void source_close(void)
{
    source_operation(3);
    if (source.handle) {
        if (fclose(source.handle)) {
            source.error = 5;
            source.closed = 2;
        } else {
            source.handle = NULL;
        }
    }
#if TCFG_T2620_FACTORY_USB_CDC_LOG_LEVEL
    printf("[CDC_FILE] source sn=%u size=%u read=%u reads=%u open_ms=%u read_ms=%u max_read_ms=%u close_ms=%u error=%u\n",
           source.sn, source.size, source.offset, source.reads, source.open_ms, source.read_ms,
           source.read_max_ms, (u32)(sys_timer_get_ms() - source.since), source.error);
#endif
    local_irq_disable();
    if (!source.closed) source.closed = 1;
    source.operation = 0;
    local_irq_enable();
    rdx_factory_cdc_wake();
}

static void source_work(void)
{
    if (!source.busy || source.closed) return;
    if (!source.opened && !source.abort) {
        source_operation(1);
        void *handle = NULL;
        int result = source.sn ?
                     rdx_record_format_download_open(source.sn, source.name, &handle, &source.size) :
                     rdx_record_format_download_list_open(&handle, &source.size);
        source.open_ms = (u32)(sys_timer_get_ms() - source.since);
        source.handle = handle;
        source.opened = 1;
        if (result) {
            source.error = result == -3 ? 3 : 5;
            if (result == -2) source.closed = 2;
        }
        source.operation = 0;
    }
    while (!source.abort && !source.error && source.offset < source.size) {
        u8 slot = source.tail;
        if (source.ready[slot]) return;
        u32 length = MIN(SOURCE_BLOCK, source.size - source.offset);
        source_operation(2);
        int read = fread(source.data[slot], 1, length, source.handle);
        u32 elapsed = (u32)(sys_timer_get_ms() - source.since);
        source.read_ms += elapsed;
        if (elapsed > source.read_max_ms) source.read_max_ms = elapsed;
        ++source.reads;
        source.operation = 0;
        if (read != length) {
            source.error = 5;
            break;
        }
        if (source.abort) break;
        source.offset += length;
        source.length[slot] = length;
        local_irq_disable();
        source.ready[slot] = 1;
        source.tail ^= 1;
        local_irq_enable();
        rdx_factory_cdc_wake();
    }
    source_close();
}

static void source_worker(void *unused)
{
    for (;;) {
        os_sem_pend(&source.wake, 0);
        source_work();
    }
}

int rdx_cdc_source_busy(void)
{
    return source.busy;
}

int rdx_cdc_source_stream_blocked(void)
{
    /* Admission only: never service, wake, cancel or release a reader here. */
    local_irq_disable();
    int blocked = source.busy &&
                  (source.error || source.abort || source.closed == 2 ||
                   (source.operation && (u32)(sys_timer_get_ms() - source.since) >=
                    (source.operation == 1 ? 8000 : 3000)));
    local_irq_enable();
    return blocked;
}

int rdx_cdc_source_begin(u32 sn, const char *name)
{
    if (source.busy && source.closed == 2) return 8;
    if (source.busy || rdx_dut_is_in_mode() || rdx_factory_audio_owned() ||
        rdx_app_cdc_file_is_busy()) return 2;
    if (rdx_dip_switch_shutdown_pending() || rdx_storage_lifecycle_business_blocked()) return 8;
    if (!source.created) {
        if (os_sem_create(&source.wake, 0)) return 8;
        if (os_task_create(source_worker, NULL, 2, 2048, 0, "cdc_reader")) {
            os_sem_del(&source.wake, 0);
            return 8;
        }
        source.created = 1;
    }
    if (rdx_record_format_download_acquire()) return 2;
    if (rdx_app_cdc_file_conflict()) {
        rdx_record_format_download_release();
        return 2;
    }
    source.abort = source.closed = source.error = source.detached = source.operation = 0;
    source.ready[0] = source.ready[1] = source.head = source.tail = source.opened = 0;
    source.size = source.offset = 0;
    source.open_ms = source.read_ms = source.read_max_ms = source.reads = 0;
    source.sn = sn;
    strcpy(source.name, name);
    source.handle = NULL;
    source.busy = 1;
    extern void rdx_app_emmc_poweroff_check_timer_stop(void);
    rdx_app_emmc_poweroff_check_timer_stop();
    os_sem_post(&source.wake);
    return 0;
}

void rdx_cdc_source_cancel(void)
{
    if (!source.busy || source.abort) return;
    source.abort = 1;
    os_sem_post(&source.wake);
}

int rdx_cdc_source_list_begin(void)
{
    return rdx_cdc_source_begin(0, "");
}

void rdx_cdc_source_release(void)
{
    if (!source.busy) return;
    source.detached = 1;
    rdx_cdc_source_cancel();
    rdx_cdc_source_service();
}

void rdx_cdc_source_service(void)
{
    if (!source.busy) return;
    local_irq_disable();
    u8 operation = source.operation;
    u32 since = source.since;
    local_irq_enable();
    if (operation && (u32)(sys_timer_get_ms() - since) >= (operation == 1 ? 8000 : 3000)) {
        source.error = 7;
        rdx_cdc_source_cancel();
    }
    if (source.closed == 1 && source.detached) {
        rdx_record_format_download_release();
        source.busy = 0;
    }
}

int rdx_cdc_source_take(u8 *data, u32 *length, u32 *size)
{
    *length = 0;
    *size = source.size;
    if (source.error) return -source.error;
    u8 slot = source.head;
    if (!source.ready[slot]) return 0;
    *length = source.length[slot];
    memcpy(data, source.data[slot], *length);
    local_irq_disable();
    source.ready[slot] = 0;
    source.head ^= 1;
    local_irq_enable();
    os_sem_post(&source.wake);
    return 1;
}

int rdx_cdc_source_closed(void)
{
    return source.closed;
}

int rdx_cdc_source_shutdown(void)
{
    rdx_cdc_source_release();
    u32 since = sys_timer_get_ms();
    while (source.busy) {
        rdx_cdc_source_service();
        if ((u32)(sys_timer_get_ms() - since) >= 500 || source.closed == 2) return -1;
        os_time_dly(1);
    }
    return 0;
}
#endif
