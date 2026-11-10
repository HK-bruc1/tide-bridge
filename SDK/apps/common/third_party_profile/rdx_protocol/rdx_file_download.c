#include "app_config.h"
#include "system/includes.h"
#include "rdx_file_download.h"
#include "rdx_ble_session.h"
#include "rdx_record_format.h"
#include "rdx_playback.h"

#define DOWNLOAD_BLOCK_BYTES 24480u
#define DOWNLOAD_TASK "protocol_send_task"

/* These entry points are supplied by the hash-pinned archive patch. */
extern int rdx_protocol_handle_recordFileData_req_legacy(u8 *data, u16 length);
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

/* Recording outranks file transfer. The stop runs on protocol_send_task under
 * file_trans_mutex (see rdx_protocol_download_preempt): app_core never blocks
 * behind an SD read, and the APP gets one stop indication right away instead
 * of after its next chunk request. If the post fails, the parser's recording
 * conflict branch still stops and notifies on that next request. */
int rdx_file_download_preempt(const char *who)
{
    u32 generation = rdx_file_download_generation();
    if (!generation) return 0;
    int msg[3] = { (int)rdx_protocol_download_preempt, 1, (int)generation };
    int ret = os_taskq_post_type(DOWNLOAD_TASK, Q_CALLBACK, 3, msg);
    printf("[DL_PREEMPT] %s sn=%u gen=%u post=%d\n", who, download.sn, generation, ret);
    return ret;
}

static int download_owner_valid(const rdx_ble_async_token_t *token)
{
    return rdx_ble_session_rdx_token_resolve(token, 1) != NULL;
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
    local_irq_enable();
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
        local_irq_enable();
    }
    int result = rdx_protocol_handle_recordFileData_req_legacy(data, length);
    if (result) rdx_protocol_download_cleanup();
    return result;
}
