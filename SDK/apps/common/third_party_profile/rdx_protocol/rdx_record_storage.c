#include "app_config.h"
#include "system/includes.h"
#include "system/timer.h"
#include "rdx_app_config.h"
#include "rdx_record.h"
#include "rdx_record_storage.h"
#include "rdx_record_format.h"

#if (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN)
/* 128 x 20ms = 2.56s including the in-flight frame, for mono AND stereo.
 * This is an engineering budget, to qualify on hardware, not an unlimited
 * stall guarantee. The existing 8000-byte aggregation buffer is separate.
 * Only this worker calls frame()/raw_write() during a live sink session.
 * File generation/finalization remain serialized by sink begin/end.
 */
#define RS_TASK "rdx_rec_store"
#define RS_FRAMES 128
#define RS_FRAME_BYTES 80
#define RS_BLOCK_BYTES 8000
static struct {
    u8 frames[RS_FRAMES][RS_FRAME_BYTES];
    u8 block[RS_BLOCK_BYTES];
    u32 read, write, count, block_len, frame_len;
    u32 generation, accepted, persisted, high_water, max_write_ms, max_meta_ms;
    u32 budget;
    u8 scene, format, created, active, closing, full;
    volatile int error;
    int io_failed;
    OS_SEM wake, done;
} rs;

int rdx_record_storage_active(void)
{
    return rs.active;
}

int rdx_record_storage_error(void)
{
    return rs.error;
}

void rdx_record_storage_reset(void)
{
    if (rs.active) return;
    rs.error = rs.io_failed = rs.full = 0;
}

int rdx_record_storage_check_start(void)
{
    u32 budget = 0;
    if (rdx_record_format_io_enter()) return -1;
    int result = rdx_record_format_space(&budget);
    rdx_record_format_io_leave();
    if (result) return -1;
    if (budget < RS_BLOCK_BYTES) {
        printf("[REC_SPACE] start rejected: reserve reached\n");
        return -2;
    }
    return 0;
}

/* Control-path admission, before any session, tone or audio state changes.
 * Fails closed on every condition rdx_record_storage_begin() would reject. */
int rdx_record_storage_admit(void)
{
    if (!rs.created || rs.active || rs.error || rdx_record_format_status() < 0)
        return -1;
    return rdx_record_storage_check_start();
}

static void rs_space_refresh(void)
{
    u32 available = 0;
    if (rdx_record_format_space(&available)) {
        rs.io_failed = 1;
        rs.error = -26;
        translation_ear_recoder_storage_fault();
        return;
    }
    OS_ENTER_CRITICAL();
    u32 pending = rs.accepted - rs.persisted;
    available = available > pending ? available - pending : 0;
    /* Do not replenish from a stale filesystem free-space snapshot. */
    if (available < rs.budget) rs.budget = available;
    int stop = !rs.full && rs.budget < rs.frame_len;
    if (stop) rs.full = 1;
    OS_EXIT_CRITICAL();
    if (stop) {
        printf("[REC_SPACE] reserve reached: draining accepted audio\n");
        translation_ear_recoder_storage_full();
    }
}

static int rs_flush(void)
{
    if (!rs.block_len || rs.io_failed) return rs.io_failed ? -1 : 0;
    u32 start = sys_timer_get_ms();
    int ret = rdx_uxfile_raw_write(rs.block, rs.block_len, rs.scene);
    u32 elapsed = sys_timer_get_ms() - start;
    if (elapsed > rs.max_write_ms) rs.max_write_ms = elapsed;
    /* The library returns status, not a byte count. Never retry a possibly
     * partially written block: doing so would duplicate the RAW prefix. */
    if (ret < 0) {
        rs.io_failed = 1;
        rs.error = -24;
        translation_ear_recoder_storage_fault();
    } else {
        rs.persisted += rs.block_len;
    }
    rs.block_len = 0;
    if (ret >= 0 && !rs.closing) rs_space_refresh();
    return ret < 0 ? -1 : 0;
}

/* One wake token per accepted frame, plus one for end. The in-flight slot
 * remains occupied until all blocking work finishes; producer cannot reuse
 * storage that is still referenced by metadata or RAW code. */
static void rs_service(void)
{
    u8 *frame = NULL;
    OS_ENTER_CRITICAL();
    if (rs.active && rs.count) frame = rs.frames[rs.read];
    OS_EXIT_CRITICAL();
    if (frame) {
        if (!rs.io_failed) {
            u32 start = sys_timer_get_ms();
            int ret = rdx_record_format_frame(rdx_uxfile_get_operateFile_info(),
                                              rs.format, frame, rs.frame_len);
            u32 elapsed = sys_timer_get_ms() - start;
            if (elapsed > rs.max_meta_ms) rs.max_meta_ms = elapsed;
            if (ret) {
                rs.io_failed = 1;
                rs.error = -25;
                translation_ear_recoder_storage_fault();
            } else {
                /* Both supported frame sizes divide 8000 exactly. */
                memcpy(rs.block + rs.block_len, frame, rs.frame_len);
                rs.block_len += rs.frame_len;
                if (rs.block_len == RS_BLOCK_BYTES) rs_flush();
            }
        }
        OS_ENTER_CRITICAL();
        rs.read = (rs.read + 1) % RS_FRAMES;
        --rs.count;
        OS_EXIT_CRITICAL();
    }
    OS_ENTER_CRITICAL();
    int finish = rs.active && rs.closing && !rs.count;
    OS_EXIT_CRITICAL();
    if (finish) {
        rs_flush();
        /* On overload, still persist every previously accepted frame before
         * latching the admission/storage fence error. No false success. */
        if (rs.error) rdx_record_format_storage_fault();
        printf("[REC_STORE] epoch=%u accepted=%u persisted=%u peak=%u/%u raw_max_ms=%u meta_max_ms=%u full=%u error=%d\n",
               rs.generation, rs.accepted, rs.persisted, rs.high_water,
               RS_FRAMES, rs.max_write_ms, rs.max_meta_ms, rs.full, rs.error);
        OS_ENTER_CRITICAL();
        rs.active = 0;
        OS_EXIT_CRITICAL();
        os_sem_post(&rs.done);
    }
}

static void rs_task(void *arg)
{
    while (1) {
        if (!os_sem_pend(&rs.wake, 0)) rs_service();
    }
}

int rdx_record_storage_create(void)
{
    if (rs.created) return 0;
    if (os_sem_create(&rs.wake, 0)) return -1;
    if (os_sem_create(&rs.done, 0)) {
        os_sem_del(&rs.wake, 0);
        return -1;
    }
    /* Lower priority than audio. No worker callback waits on jlstream or the
     * recorder task, so sink exit can wait here without a lock cycle. */
    if (os_task_create(rs_task, NULL, 2, 768, 0, RS_TASK)) {
        os_sem_del(&rs.done, 0);
        os_sem_del(&rs.wake, 0);
        return -1;
    }
    rs.created = 1;
    return 0;
}

int rdx_record_storage_begin(u32 generation, u8 scene, u8 format)
{
    if (!rs.created || rs.active || rs.error || rdx_record_format_status() < 0)
        return -1;
    u32 len = format == RECORD_FORMATE_OPUS_16K_MONO ? 40 :
              format == RECORD_FORMATE_OPUS_16K_STERO ? 80 : 0;
    if (!len) return -1;
    u32 budget = 0;
    if (rdx_record_format_io_enter()) return -1;
    if (rdx_record_format_space(&budget) || budget < RS_BLOCK_BYTES) {
        rdx_record_format_io_leave();
        return -1;
    }
    os_sem_set(&rs.wake, 0);
    os_sem_set(&rs.done, 0);
    OS_ENTER_CRITICAL();
    rs.read = rs.write = rs.count = rs.block_len = 0;
    rs.accepted = rs.persisted = rs.high_water = 0;
    rs.max_write_ms = rs.max_meta_ms = 0;
    rs.generation = generation;
    rs.scene = scene;
    rs.format = format;
    rs.frame_len = len;
    rs.budget = budget;
    rs.full = 0;
    rs.closing = 0;
    rs.active = 1;
    OS_EXIT_CRITICAL();
    /* Erase observes either this admission lease or active, never a gap. */
    rdx_record_format_io_leave();
    return 0;
}

int rdx_record_storage_push(const u8 *data, u32 len)
{
    int ret = 0;
    int space_stop = 0;
    OS_ENTER_CRITICAL();
    if (!rs.active || rs.closing || rs.error) {
        ret = -1;
    } else if (rs.full) {
        ret = 1;
    } else if (!data || len != rs.frame_len || rs.count == RS_FRAMES) {
        rs.error = -23;
        ret = -1;
    } else if (rs.budget < len) {
        rs.full = 1;
        space_stop = 1;
        ret = 1;
    } else {
        memcpy(rs.frames[rs.write], data, len);
        rs.write = (rs.write + 1) % RS_FRAMES;
        ++rs.count;
        rs.accepted += len;
        rs.budget -= len;
        if (rs.budget < rs.frame_len) {
            rs.full = 1;
            space_stop = 1;
        }
        if (rs.count > rs.high_water) rs.high_water = rs.count;
    }
    OS_EXIT_CRITICAL();
    /* Sink exit is serialized after its last data callback returns. No OS
     * scheduling operation is needed while copying under the IRQ lock. */
    if (space_stop) translation_ear_recoder_storage_full();
    if (ret < 0) translation_ear_recoder_storage_fault();
    if (!ret) os_sem_post(&rs.wake);
    return ret;
}

int rdx_record_storage_end(void)
{
    /* Control path only, after the sink has stopped delivering frames.
     * Never time out and free buffers while a write still owns them. Native
     * power-off retains its separate bounded reset guard. */
    OS_ENTER_CRITICAL();
    int active = rs.active;
    if (active) rs.closing = 1;
    OS_EXIT_CRITICAL();
    if (active) {
        os_sem_post(&rs.wake);
        os_sem_pend(&rs.done, 0);
    }
    return rs.error ? -1 : 0;
}

void rdx_record_storage_destroy(void)
{
    if (!rs.created) return;
    rdx_record_storage_end();
    os_task_del(RS_TASK);
    os_sem_del(&rs.done, 0);
    os_sem_del(&rs.wake, 0);
    rs.created = 0;
}
#endif
