#include "app_config.h"
#include "system/includes.h"
#include "rdx_file_download.h"
#include "rdx_ble_session.h"
#include "rdx_ble_server.h"
#include "rdx_protocol.h"
#include "rdx_record_format.h"
#include "rdx_playback.h"

#define DOWNLOAD_BLOCK_BYTES 24480u
#define DOWNLOAD_TASK "protocol_send_task"

/* These entry points are supplied by the hash-pinned archive patch. */
extern int rdx_protocol_handle_recordFileData_req_legacy(u8 *data, u16 length);
extern ReqFileInfo *rdx_protocol_get_uploadfileInfo(void);
extern void rdx_protocol_download_cleanup(void);
extern void rdx_protocol_download_preempt(u32 generation);
extern int rdx_uxfile_auto_delete_submit(void *context);
extern int rdx_uxfile_delete_checked_guarded(u32 sn, const char *name,
                                            int (*authorize)(void *), void *context);

typedef struct {
    rdx_ble_async_token_t token;
    u32 generation, sn, offset, next, size;
    char name[RECORD_FILE_NAME_SIZE];
    u8 active, auto_delete, identified, failed;
} download_state_t;

typedef struct {
    rdx_ble_async_token_t token;
    u32 sn;
    char name[RECORD_FILE_NAME_SIZE];
    int result;
} download_delete_t;

static download_state_t download;

/* The archive's shared request parser rejects new chunks once a recording run
 * or OTA owns the device. That rejection is a device-side interrupt, not a
 * transport failure: remember it here so the wrapper can answer the APP with
 * one stop indication after it releases file_trans_mutex. Both sides run on
 * protocol_recv_task. */
static u8 download_interrupted;

void rdx_file_download_abort(void)
{
    local_irq_disable();
    download.active = 0;
    if (!++download.generation) ++download.generation;
    local_irq_enable();
    rdx_ble_server_file_transfer_changed();
}

void rdx_file_download_interrupt_notify(void)
{
    download_interrupted = 1;
}

u8 rdx_file_download_take_interrupt(void)
{
    u8 value = download_interrupted;
    download_interrupted = 0;
    return value;
}

u32 rdx_file_download_generation(void)
{
    return download.active ? download.generation : 0;
}

static int download_owner_valid(const rdx_ble_async_token_t *token)
{
    return rdx_ble_session_rdx_token_resolve(token, 1) != NULL;
}

/* Preemption stop fence.
 *
 * Every RDX characteristic send funnels through rdx_ble_server_send_internal
 * (queued packets, bulk download frames, F18 status, F19 flashnote), so the
 * order the APP observes is fixed there:
 *   whole last download frame -> *DEV#filecmd#0# -> recording status.
 * The APP parses a frame by the length in its *DEV#filect# header and treats
 * every notify until then as payload, so the stop must wait for the frame in
 * flight to end (the patched sender no longer interleaves queued packets).
 * ARMED:   admission accepted, transfer not yet cleaned, stop still owed; the
 *          started frame may finish, no new frame may start.
 * SENT:    stop accepted by ATT, cleanup still pending; frames are blocked.
 * PENDING: cleanup done, stop still owed (ATT full or sender busy).
 * While a stop is owed, queued library packets are refused (the library parks
 * them at its queue head) and direct sends such as the F19 flashnote are kept
 * in a small FIFO, reported as sent, and released after the stop. The stop
 * goes straight to ATT so a priority status cannot pass it. */
#define DOWNLOAD_STOP_TEXT "*DEV#filecmd#0#"
#define DOWNLOAD_STOP_LEN (sizeof(DOWNLOAD_STOP_TEXT) - 1)
#define DOWNLOAD_FRAME_TEXT "*DEV#filect#"
#define DOWNLOAD_CONTROL_TEXT "*DEV#"
#define DOWNLOAD_DEFER_SLOTS 4u
#define DOWNLOAD_DEFER_BYTES 160u
/* Upper bound for the frame in flight: 24.5 KB needs ~0.5 s at 53 KB/s. */
#define DOWNLOAD_PREEMPT_WAIT_TICKS 200u

/* Supplied by the archive patch: a started frame still has bytes to send. */
extern int rdx_protocol_bulk_frame_busy(void);

enum { FENCE_IDLE, FENCE_ARMED, FENCE_SENT, FENCE_PENDING };

typedef struct {
    rdx_ble_async_token_t token;
    u32 len;
    u8 data[DOWNLOAD_DEFER_BYTES];
} fence_defer_t;

static struct {
    rdx_ble_async_token_t token;
    u8 state, busy, queued, blocked;
    u8 head, count, draining;
    fence_defer_t defer[DOWNLOAD_DEFER_SLOTS];
} fence;

static u8 fence_owed(u8 state)
{
    return state == FENCE_ARMED || state == FENCE_PENDING;
}

/* Resume packets the gate refused: the library parks them at the queue head
 * and only restarts on ATT_EVENT_CAN_SEND_NOW, which may never come once the
 * link is idle. Caller must not hold local_irq. */
static void fence_kick(void)
{
    local_irq_disable();
    u8 blocked = fence.blocked;
    fence.blocked = 0;
    local_irq_enable();
    if (blocked) rdx_ble_server_rdx_send_kick();
}

/* Release deferred direct sends in order. Caller owns fence.busy. Entries
 * whose owner link is gone are dropped: nothing would reach it. */
static int fence_drain(void)
{
    for (;;) {
        local_irq_disable();
        if (!fence.count) {
            local_irq_enable();
            return 0;
        }
        fence_defer_t *slot = &fence.defer[fence.head];
        fence.draining = 1;
        local_irq_enable();
        int ret = rdx_ble_server_send_for_token(slot->data, slot->len, &slot->token);
        u8 gone = ret && !download_owner_valid(&slot->token);
        local_irq_disable();
        fence.draining = 0;
        if (!ret || gone) {
            fence.head = (fence.head + 1) % DOWNLOAD_DEFER_SLOTS;
            --fence.count;
        }
        local_irq_enable();
        if (ret && !gone) return -1;
    }
}

/* Returns 0 once no stop is owed (sent, or the owner link is gone) and every
 * deferred send reached ATT. */
static int fence_flush(void)
{
    local_irq_disable();
    u8 state = fence.state;
    if (fence.busy) {
        int ret = fence_owed(state) || fence.count ? -1 : 0;
        local_irq_enable();
        return ret;
    }
    if (!fence_owed(state) && !fence.count) {
        local_irq_enable();
        return 0;
    }
    fence.busy = 1;
    rdx_ble_async_token_t token = fence.token;
    local_irq_enable();

    int ret = 0;
    if (fence_owed(state)) {
        /* The last frame first: the stop may not cut it. */
        ret = state == FENCE_ARMED && rdx_protocol_bulk_frame_busy() ? -1 :
              rdx_ble_server_send_for_token((u8 *)DOWNLOAD_STOP_TEXT,
                                            DOWNLOAD_STOP_LEN, &token);
        u8 gone = ret && !download_owner_valid(&token);
        local_irq_disable();
        state = fence.state;
        if (fence_owed(state) && (!ret || gone)) {
            fence.state = state == FENCE_ARMED && !gone ? FENCE_SENT : FENCE_IDLE;
            ret = 0;
            local_irq_enable();
            printf("[DL_PREEMPT] stop %s state=%u\n", gone ? "dropped" : "sent", fence.state);
        } else {
            local_irq_enable();
        }
    }
    if (!ret) ret = fence_drain();
    local_irq_disable();
    fence.busy = 0;
    local_irq_enable();
    if (ret) return -1;
    fence_kick();
    return 0;
}

/* Caller holds local_irq. Nonzero: no room, the sender sees a full buffer. */
static int fence_defer(const u8 *data, u32 len, const rdx_ble_async_token_t *token)
{
    if (!data || !len || len > DOWNLOAD_DEFER_BYTES ||
        fence.count >= DOWNLOAD_DEFER_SLOTS) return -1;
    fence_defer_t *slot = &fence.defer[(fence.head + fence.count) % DOWNLOAD_DEFER_SLOTS];
    slot->token = *token;
    slot->len = len;
    memcpy(slot->data, data, len);
    ++fence.count;
    return 0;
}

/* rdx_ble_server_send_internal: negative refuses this packet for now,
 * positive means it was taken over and reaches ATT when the fence opens.
 * token is NULL for library (queued) sends. */
int rdx_file_download_tx_gate(const u8 *data, u32 len,
                              const rdx_ble_async_token_t *token)
{
    if (fence.state == FENCE_IDLE && !fence.count) return 0;
    if (data && len == DOWNLOAD_STOP_LEN &&
        !memcmp(data, DOWNLOAD_STOP_TEXT, DOWNLOAD_STOP_LEN)) return 0;
    if (fence.draining && data == fence.defer[fence.head].data) return 0;
    u8 control = data && len >= sizeof(DOWNLOAD_CONTROL_TEXT) - 1 &&
                 !memcmp(data, DOWNLOAD_CONTROL_TEXT, sizeof(DOWNLOAD_CONTROL_TEXT) - 1);
    u8 start = control && len >= sizeof(DOWNLOAD_FRAME_TEXT) - 1 &&
               !memcmp(data, DOWNLOAD_FRAME_TEXT, sizeof(DOWNLOAD_FRAME_TEXT) - 1);
    fence_flush();
    /* Decide and record under irq: whoever opens the fence afterwards sees
     * blocked and resumes the library queue. */
    local_irq_disable();
    u8 state = fence.state;
    int verdict = 0;
    if (start) {
        verdict = state != FENCE_IDLE ? -1 : 0;
    } else if (!control) {
        /* Continuation of the frame in flight: only before the stop. */
        verdict = state == FENCE_IDLE || state == FENCE_ARMED ? 0 : -1;
    } else if (fence_owed(state) || fence.count) {
        verdict = token && !fence_defer(data, len, token) ? 1 : -1;
    }
    if (verdict < 0) fence.blocked = 1;
    local_irq_enable();
    return verdict;
}

/* ATT_EVENT_CAN_SEND_NOW: resume the fence. */
void rdx_file_download_tx_ready(void)
{
    if (fence_owed(fence.state) || fence.count) fence_flush();
}

/* Stream audio yields queue room to statuses while sends are held: the
 * library queue holds 15 packets and drops new ones when full. */
u8 rdx_file_download_tx_held(void)
{
    return fence_owed(fence.state) || fence.count;
}

/* protocol_send_task, rdx_protocol_download_preempt prologue (no mutex held):
 * the frame in flight finishes before cleanup, bounded for a stalled link. */
void rdx_file_download_preempt_wait(void)
{
    u32 ticks = 0;
    while (fence.state == FENCE_ARMED && rdx_protocol_bulk_frame_busy() &&
           download_owner_valid(&fence.token) &&
           ticks < DOWNLOAD_PREEMPT_WAIT_TICKS) {
        os_time_dly(1);
        ++ticks;
    }
    printf("[DL_PREEMPT] frame wait=%u busy=%d\n", ticks, rdx_protocol_bulk_frame_busy());
}

/* protocol_send_task, after rdx_protocol_download_preempt released
 * file_trans_mutex. stopped: this callback cleaned the transfer up. */
void rdx_file_download_preempt_done(u32 stopped)
{
    local_irq_disable();
    if (fence.queued) --fence.queued;
    if (stopped) {
        if (fence.state == FENCE_SENT) fence.state = FENCE_IDLE;
        else if (fence.state == FENCE_ARMED) fence.state = FENCE_PENDING;
    } else if (!fence.queued && fence.state != FENCE_PENDING) {
        /* The transfer ended (or was replaced) before this callback ran:
         * it is not ours to stop and its frames must flow again. */
        fence.state = FENCE_IDLE;
    }
    local_irq_enable();
    fence_flush();
    fence_kick();
}

/* Recording outranks file transfer. Admission (app_core) fences the link; the
 * stop goes out at the first frame boundary, before any recording status. The
 * cleanup runs on protocol_send_task under file_trans_mutex (see
 * rdx_protocol_download_preempt): app_core never blocks behind an SD read or a
 * frame. If the post fails the fence is dropped and the parser's recording
 * conflict branch still stops and notifies on the next request. */
int rdx_file_download_preempt(const char *who)
{
    local_irq_disable();
    u32 generation = rdx_file_download_generation();
    u32 sn = download.sn;
    if (!generation || (fence.queued && fence.state != FENCE_IDLE)) {
        local_irq_enable();
        return 0;
    }
    if (fence.state == FENCE_IDLE || fence.state == FENCE_PENDING) {
        fence.token = download.token;
        fence.state = FENCE_ARMED;
    }
    ++fence.queued;
    local_irq_enable();
    int msg[3] = { (int)rdx_protocol_download_preempt, 1, (int)generation };
    int ret = os_taskq_post_type(DOWNLOAD_TASK, Q_CALLBACK, 3, msg);
    printf("[DL_PREEMPT] %s sn=%u gen=%u post=%d frame=%d\n", who, sn,
           generation, ret, rdx_protocol_bulk_frame_busy());
    if (ret) {
        local_irq_disable();
        if (!--fence.queued) fence.state = FENCE_IDLE;
        local_irq_enable();
    }
    fence_flush();
    fence_kick();
    return ret;
}

void rdx_file_download_stop_for_recording(void)
{
    local_irq_disable();
    u8 managed = download.active || fence.queued;
    local_irq_enable();
    if (managed) {
        rdx_file_download_preempt("REC_INIT");
    } else if (rdx_protocol_get_uploadfileInfo()->file_send_busy) {
        rdx_protocol_file_cmd_handle(RDX_APP_FILE_CMD_STOP);
    }
}

/* Parse without touching the library's shared r_file_info. In particular, a
 * late pack=0 for another SN must not destroy a newer transfer's buffers. */
static int download_parse(const u8 *data, u16 length, u32 values[5])
{
    u32 pos = 14, field = 0;
    if (!data || length <= pos || memcmp(data, "*APP#opusfile#", pos)) return -1;
    if (data[length - 1] == 0) --length;
    while (pos < length && field < 5) {
        u32 value = 0, digits = 0;
        while (pos < length && data[pos] >= '0' && data[pos] <= '9') {
            u32 digit = data[pos++] - '0';
            if (value > (0x7ffffffeu - digit) / 10) return -1;
            value = value * 10 + digit;
            ++digits;
        }
        if (!digits || pos >= length || data[pos++] != '#') return -1;
        values[field++] = value;
    }
    return pos == length && field >= 4 && values[0] <= 2 &&
           values[1] && values[3] <= 1 ? 0 : -1;
}

u32 rdx_file_download_chunk(const ReqFileInfo *request,
                            const uxfile_data_t *file)
{
    if (!request || !file || request->pack_num < 1 ||
        request->file_offset < 0 || (u32)request->file_offset >= file->data_len) return 0;
    u32 remaining = file->data_len - request->file_offset;
    u32 block = (u32)request->pack_num - 1;
    if (block > (remaining - 1) / DOWNLOAD_BLOCK_BYTES) return 0;
    remaining -= block * DOWNLOAD_BLOCK_BYTES;
    return MIN(remaining, DOWNLOAD_BLOCK_BYTES);
}

void rdx_file_download_sent(u32 generation, const ReqFileInfo *request,
                            const uxfile_data_t *file, u32 offset,
                            u32 length, int read_result, int send_result)
{
    local_irq_disable();
    if (!download.active || !generation || generation != download.generation) {
        local_irq_enable();
        return;
    }
    if (!request || !file || !download_owner_valid(&download.token) ||
        (u32)request->file_num != download.sn || file->sn != download.sn ||
        (u32)request->file_offset != download.offset ||
        !length || read_result != (int)length || send_result != 1 ||
        offset > file->data_len || length > file->data_len - offset ||
        offset > download.next || !file->filename[0] ||
        !memchr(file->filename, 0, sizeof(file->filename))) {
        download.failed = 1;
    } else {
        if (!download.identified) {
            memcpy(download.name, file->filename, sizeof(download.name));
            download.size = file->data_len;
            download.identified = 1;
        } else if (strcmp(download.name, file->filename) ||
                   download.size != file->data_len) {
            download.failed = 1;
        }
        if (offset + length > download.next) download.next = offset + length;
    }
    local_irq_enable();
}

static int download_delete_authorized(void *context)
{
    download_delete_t *request = context;
    return download_owner_valid(&request->token);
}

static void download_delete_complete(download_delete_t *request)
{
#if TCFG_RDX_LOCAL_PLAYBACK_ENABLE
    if (!request->result && download_owner_valid(&request->token)) {
        rdx_playback_on_file_deleted(request->sn);
    }
#endif
    /* opusfile completion has no delfile ACK; do not invent one. */
    free(request);
}

void rdx_file_download_delete_worker(void *context)
{
    download_delete_t *request = context;
    request->result = RDX_RECORD_DELETE_REJECTED;
    if (download_delete_authorized(request)) {
        request->result = rdx_uxfile_delete_checked_guarded(
            request->sn, request->name, download_delete_authorized, request);
    }
    printf("[AUTO_DELETE] sn=%u name=%s result=%d\n",
           request->sn, request->name, request->result);
    rdx_record_format_delete_release(request->result);
    int msg[3] = { (int)download_delete_complete, 1, (int)request };
    if (os_taskq_post_type("app_core", Q_CALLBACK, 3, msg)) free(request);
}

/* The archive wrapper holds file_trans_mutex across parser/sender/cleanup. */
int rdx_file_download_request(u8 *data, u16 length)
{
    u32 values[5] = {0};
    download_state_t completed;
    download_delete_t *request = NULL;
    rdx_ble_async_token_t token;
    download_interrupted = 0;
    if (download_parse(data, length, values)) return 3;

    local_irq_disable();
    completed = download;
    u8 preempting = completed.active && fence.queued && fence.state != FENCE_IDLE;
    u8 stop_owed = fence.state == FENCE_ARMED;
    local_irq_enable();
    if (preempting && (values[2] || stop_owed)) return values[2] ? 3 : 0;
    if (values[2] == 0) {
        /* Duplicate completion is harmless, even while deletion is queued. */
        if (!completed.active) return 0;
        if (values[1] != completed.sn || !download_owner_valid(&completed.token)) return 3;
        if (completed.auto_delete && values[3] && completed.identified &&
            !completed.failed && completed.next == completed.size &&
            completed.next > completed.offset) {
            request = malloc(sizeof(*request));
            if (request) {
                request->token = completed.token;
                request->sn = completed.sn;
                memcpy(request->name, completed.name, sizeof(request->name));
                if (rdx_record_format_delete_acquire()) {
                    free(request);
                    request = NULL;
                } else {
                    local_irq_disable();
                    int current = download.active &&
                                  download.generation == completed.generation;
                    local_irq_enable();
                    if (!current || !download_owner_valid(&completed.token)) {
                        rdx_record_format_delete_release(RDX_RECORD_DELETE_REJECTED);
                        free(request);
                        request = NULL;
                    }
                }
            }
        }
        /* Reserve the immutable identity and storage/power before cleanup.
         * The worker may run immediately after posting, never before it. */
        if (completed.auto_delete && values[3] && !request) {
            printf("[AUTO_DELETE] sn=%u retained next=%u size=%u failed=%u\n",
                   completed.sn, completed.next, completed.size, completed.failed);
        }
        rdx_protocol_download_cleanup();
        rdx_ble_server_file_transfer_changed();
        if (request && rdx_uxfile_auto_delete_submit(request)) {
            printf("[AUTO_DELETE] sn=%u queue rejected; file retained\n", request->sn);
            rdx_record_format_delete_release(RDX_RECORD_DELETE_REJECTED);
            free(request);
        }
        return 0;
    }

    if (rdx_record_format_service_status() ||
        !rdx_ble_session_rdx_token_capture(&token, 1)) return 3;
    if (completed.active) {
        if (completed.sn != values[1] || completed.offset != values[4] ||
            completed.auto_delete != values[3] ||
            !download_owner_valid(&completed.token)) return 3;
    } else {
        if (values[2] != 1) return 3;
        local_irq_disable();
        u32 generation = download.generation + 1;
        memset(&download, 0, sizeof(download));
        download.generation = generation ? generation : 1;
        download.token = token;
        download.sn = values[1];
        download.offset = download.next = values[4];
        download.auto_delete = values[3];
        download.active = 1;
        /* A new transfer supersedes any stop still owed for the old one;
         * deferred direct sends still drain in order. */
        fence.state = FENCE_IDLE;
        local_irq_enable();
    }
    int result = rdx_protocol_handle_recordFileData_req_legacy(data, length);
    if (result) rdx_protocol_download_cleanup();
    rdx_ble_server_file_transfer_changed();
    return result;
}
