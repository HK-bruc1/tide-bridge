#include "rdx_factory_audio.h"
#include "rdx_cdc_source.h"
#if TCFG_T2620_FACTORY_USB_CDC_ENABLE && !TCFG_T2620_FACTORY_USB_CDC_TEST_ENABLE
#include "system/includes.h"
#include "usb/device/usb_factory_cdc.h"
#include "rdx_record.h"
#include "rdx_record_format.h"
#include "rdx_dut.h"
#include "rdx_app.h"
#include "rdx_rtc.h"
#include "rdx_uxfile.h"
#include "rdx_storage_lifecycle.h"
#include "fs/fs.h"

extern bool rdx_app_get_dut_status(void);

/* app_core owns commands and TX. Encoder copies only 40 bytes under IRQ lock.
 * File worker owns its private read handle, never the BLE library's handle.
 * 64 frames = 1.28 s / 2560 bytes; one file block = 80 bytes.
 */
#define AUDIO_DEPTH 64
#define AUDIO_MAX_MS 15000
#define FILE_TASK "rdx_cdc_file"
static struct {
    u32 generation, negotiated, timed, id, stop, since, produced, sent, crc, sn, start;
    u32 tx_since;
    u8 frames[AUDIO_DEPTH][40];
    u8 head, count, running, started, complete, end_sent, saved, error, stop_reason;
    char name[12];
} audio;
static struct {
    volatile u8 busy, command, done, abort;
    u8 created, state, pending, close_error;
    int result;
    u32 generation, id, size, crc, offset, length, touched;
    u8 data[80];
    OS_SEM wake;
    FILE *handle;
} file;
/* file state: 0 closed, 1 opening, 2 ready, 3 failed; worker command 1/2/3. */
extern void rdx_app_emmc_poweroff_check_timer_stop(void);

u32 rdx_factory_audio_crc(u32 crc, const u8 *data, u32 length)
{
    crc = ~crc;
    while (length--) {
        crc ^= *data++;
        for (u8 bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

int rdx_factory_audio_file_busy(void) { return file.busy; }
int rdx_factory_audio_owned(void) { return audio.running; }
int rdx_factory_audio_needs_service(void)
{
    return (audio.running && !audio.complete) ||
           (file.busy && !(file.close_error && file.state == 3 && !file.pending));
}
int rdx_factory_audio_tx_pending(u32 generation)
{
    return audio.generation == generation && (audio.count || (audio.complete && !audio.end_sent));
}
int rdx_factory_audio_started(void) { return audio.started; }
int rdx_factory_audio_ready(u32 generation)
{
    return generation && generation == audio.negotiated && generation == audio.timed &&
        generation == factory_cdc_generation() && !audio.running && !file.busy &&
        (!audio.id || audio.generation != generation || audio.end_sent);
}

int rdx_factory_audio_begin(u32 generation, u32 id)
{
    if (!rdx_factory_audio_ready(generation)) return -1;
    local_irq_disable();
    memset(&audio, 0, sizeof(audio));
    audio.generation = audio.negotiated = audio.timed = generation;
    audio.id = id;
    audio.since = audio.tx_since = sys_timer_get_ms();
    audio.running = 1;
    local_irq_enable();
    return 0;
}

void rdx_factory_audio_frame(const u8 *data, u32 length)
{
    if (!audio.running || audio.complete) return;
    local_irq_disable();
    u8 first = !audio.started;
    if (!audio.started) {
        audio.since = audio.tx_since = sys_timer_get_ms();
        uxfile_data_t *info = rdx_uxfile_get_operateFile_info();
        if (info->sn && strlen(info->filename) < sizeof(audio.name)) {
            audio.sn = info->sn; audio.start = info->start_time;
            strcpy(audio.name, info->filename);
        }
    }
    audio.started = 1;
    ++audio.produced;
    if (length != 40 || audio.count == AUDIO_DEPTH ||
        audio.generation != factory_cdc_generation()) {
        audio.error = 1;
    } else if (!audio.error) {
        memcpy(audio.frames[(audio.head + audio.count) % AUDIO_DEPTH], data, 40);
        ++audio.count;
        audio.crc = rdx_factory_audio_crc(audio.crc, data, 40);
    }
    local_irq_enable();
    if (first) rdx_dut_service_wake();
}

/* Called before the recorder clears its file context, after finish_record. */
void rdx_factory_audio_saved(int result)
{
    if (!audio.running) return;
    audio.saved = result == 0 && audio.sn && audio.name[0];
    if (!audio.saved && !audio.error) audio.error = 2;
}

/* app_core observes the recorder's FIFO ticket after MIC/storage cleanup. */
void rdx_factory_audio_stopped(u32 id, int result)
{
    if (!audio.running) return;
    audio.stop = id;
    if ((result || !audio.saved || !audio.started) && !audio.error) audio.error = 2;
    local_irq_disable();
    audio.complete = 1;
    audio.running = 0;
    local_irq_enable();
}

static int decimal(const char *text, u32 *value, const char **end)
{
    u32 n = 0;
    if (*text < '0' || *text > '9') return -1;
    do {
        u32 digit = *text++ - '0';
        if (n > (0xffffffffu - digit) / 10) return -1;
        n = n * 10 + digit;
    } while (*text >= '0' && *text <= '9');
    *value = n; *end = text;
    return 0;
}

static void file_work(void)
{
    u8 command = file.command;
    int result = -1;
    if (command == 1 && !file.abort) {
        file.handle = rdx_record_format_factory_open(audio.sn, audio.name, audio.start,
                                                     &file.size, &file.crc);
        result = file.handle ? 0 : -1;
    } else if (command == 2 && file.handle && !file.abort) {
        u32 n = MIN(sizeof(file.data), file.size - file.offset);
        if (n && fread(file.data, 1, n, file.handle) == n) {
            file.length = n;
            result = 0;
        }
    }
    if (command == 3 || file.abort || result < 0) {
        int closed = file.handle ? fclose(file.handle) : 0;
        file.handle = NULL;
        if (closed) file.close_error = 1;
        if (command == 3) result = file.close_error ? -1 : 0;
    }
    file.result = result;
    local_irq_disable();
    file.done = 1;
    local_irq_enable();
    /* done is the final publication; no late completion may touch a new request. */
}

static void file_worker(void *unused)
{
    for (;;) {
        os_sem_pend(&file.wake, 0);
        file_work();
    }
}

static void file_post(u8 command)
{
    file.command = command;
    file.done = 0;
    file.pending = 1;

    os_sem_post(&file.wake);
}

static int file_create(void)
{
    if (file.created) return 0;
    if (os_sem_create(&file.wake, 0)) return -1;

    if (os_task_create(file_worker, NULL, 2, 768, 0, FILE_TASK)) {
        os_sem_del(&file.wake, 0); return -1;
    }
    file.created = 1;
    return 0;
}

/* One deadline covers both an in-flight operation and its final close. */
static int file_shutdown_wait(u32 since)
{
    while (file.pending && !file.done) {
        if ((u32)(sys_timer_get_ms() - since) >= 500) return -1;
        os_time_dly(1);
    }
    return 0;
}

int rdx_factory_audio_shutdown(void)
{
    if (rdx_cdc_source_shutdown()) return -1;
    /* Caller has already joined the recorder task; no producer remains.
     * Failure retains the lease. Caller must reset instead of powering SD off. */
    audio.negotiated = audio.timed = 0;
    if (audio.running) {
        audio.error = 1;
        rdx_factory_audio_stopped(0, -1);
    }
    if (!file.created) return 0;
    u32 since = sys_timer_get_ms();
    file.abort = 1;
    if (file_shutdown_wait(since) || file.close_error) goto failed;
    /* Never overwrite an unfinished worker command or retry a failed close. */
    if (!file.pending || file.command != 3) file_post(3);
    if (file_shutdown_wait(since) || file.close_error || file.result < 0) goto failed;
    file.busy = file.pending = file.length = 0;
    file.state = 0;
    /* Worker remains allocated, allowing ON-mode reuse after idle. */
    return 0;
failed:
    file.busy = file.close_error = 1;
    file.state = 3;
    return -1;
}

int rdx_factory_audio_command(u32 generation, const char *line, char *out, unsigned capacity)
{
    const char *reply = NULL, *end;
    u32 id = 0, offset = 0;
    if (!strcmp(line, "GET_AUDIO_CAPS")) {
        reply = "OK AUDIO=1;CODEC=OPUS;RATE=16000;CHANNELS=1;FRAME=40;MS=20;MAX_MS=15000\r\n";
    } else if (!strcmp(line, "AUDIO_OPEN 1")) {
        if (audio.running || file.busy) reply = "ERR BUSY\r\n";
        else { audio.negotiated = generation; reply = "OK AUDIO=1\r\n"; }
    } else if (!strncmp(line, "SET_TIME ", 9)) {
        if (decimal(line + 9, &id, &end) || *end || id < 1577836800u || id > 2145916799u)
            reply = "ERR ARG\r\n";
        else if (audio.negotiated != generation || rdx_record_get_status()->run != RECORD_STATE_STOP || file.busy)
            reply = "ERR STATE\r\n";
        else if (rdx_rtc_set_timestamp(id)) reply = "ERR IO\r\n";
        else { audio.timed = generation; reply = "OK TIME=1\r\n"; }
    } else if (!strcmp(line, "GET_RECORD_RESULT")) {
        if (audio.generation != generation || !audio.id) reply = "ERR STATE\r\n";
        else snprintf(out, capacity,
            "OK REC=%u;STOP=%u;STATE=%s;SAVED=%u;SN=%u;NAME=%s;FRAMES=%u;ERROR=%u;REASON=%u\r\n",
            audio.id, audio.stop, audio.complete ? (audio.error ? "FAILED" : "DONE") : "RUNNING",
            audio.saved, audio.sn, audio.name[0] ? audio.name : "NONE", audio.produced, audio.error, audio.stop_reason);
    } else if (!strncmp(line, "FILE_OPEN ", 10)) {
        if (decimal(line + 10, &id, &end) || *end) reply = "ERR ARG\r\n";
        else if ((file.id == id && file.generation == generation) ||
                 audio.generation != generation || audio.id != id || !audio.complete || !audio.saved || !audio.end_sent)
            reply = "ERR STATE\r\n";
        else if (file.busy || rdx_app_get_dut_status() || rdx_storage_lifecycle_business_blocked() || rdx_app_factory_file_is_busy())
            reply = "ERR BUSY\r\n";
        else if (file_create()) reply = "ERR IO\r\n";
        else {
            file.busy = 1;
            file.abort = file.close_error = 0; file.state = 1;
            file.id = id; file.generation = generation;
            file.offset = file.size = file.crc = file.length = 0;
            file.touched = sys_timer_get_ms();
            rdx_app_emmc_poweroff_check_timer_stop();
            file_post(1);
            reply = "OK STATE=RUNNING\r\n";
        }
    } else if (!strcmp(line, "GET_FILE_STATUS")) {
        if (!file.id || file.generation != generation) reply = "ERR STATE\r\n";
        else snprintf(out, capacity, "OK REC=%u;STATE=%s;SIZE=%u;CRC=%u;OFFSET=%u\r\n", file.id,
             file.state == 1 ? "OPENING" : file.state == 2 ? "READY" : file.state == 3 ? "FAILED" : "CLOSED",
             file.size, file.crc, file.offset);
    } else if (!strncmp(line, "FILE_READ ", 10)) {
        if (decimal(line + 10, &id, &end) || *end++ != ' ' || decimal(end, &offset, &end) || *end)
            reply = "ERR ARG\r\n";
        else if (file.generation != generation || file.id != id || file.state != 2 || !file.busy || file.abort)
            reply = "ERR STATE\r\n";
        else if (file.pending || file.length) reply = "ERR BUSY\r\n";
        else if (offset != file.offset || offset >= file.size) reply = "ERR ARG\r\n";
        else { file.touched = sys_timer_get_ms(); file_post(2); reply = "OK STATE=RUNNING\r\n"; }
    } else if (!strncmp(line, "FILE_CLOSE ", 11)) {
        if (decimal(line + 11, &id, &end) || *end) reply = "ERR ARG\r\n";
        else if (file.id != id || file.generation != generation) reply = "ERR STATE\r\n";
        else { file.abort = 1; reply = "OK STATE=RUNNING\r\n"; }
    } else return 0;
    if (reply) snprintf(out, capacity, "%s", reply);
    return 1;
}

static int send_frame(u32 generation, char *line)
{
    u32 length = strlen(line);
    u32 crc = rdx_factory_audio_crc(0, (u8 *)line, length);
    snprintf(line + length, 12, "*%08X\r\n", crc);
    return factory_cdc_send(generation, (u8 *)line, length + 11);
}

static void hex(char *out, const u8 *data, u32 count)
{
    static const char digits[] = "0123456789ABCDEF";
    for (u32 i = 0; i < count; ++i) {
        out[i*2] = digits[data[i] >> 4]; out[i*2+1] = digits[data[i] & 15];
    }
    out[count*2] = 0;
}

void rdx_factory_audio_send(u32 generation)
{
    char line[258];
    if (audio.generation == generation && audio.count) {
        int n = snprintf(line, sizeof(line), "@A,%u,%u,40,", audio.id, audio.sent);
        hex(line + n, audio.frames[audio.head], 40);
        if (send_frame(generation, line) == FACTORY_CDC_OK) {
            local_irq_disable(); audio.head = (audio.head + 1) % AUDIO_DEPTH; --audio.count; local_irq_enable();
            ++audio.sent; audio.tx_since = sys_timer_get_ms();
        }
    } else if (audio.generation == generation && audio.complete && !audio.end_sent) {
        snprintf(line, sizeof(line), "@E,%u,%u,%u,%u,%u", audio.id, audio.produced,
                 audio.sent * 40, audio.error == 2 ? 0 : audio.error, audio.crc);
        if (send_frame(generation, line) == FACTORY_CDC_OK) audio.end_sent = 1;
    }
    if (file.generation == generation && file.length && !file.pending && !file.abort) {
        int n = snprintf(line, sizeof(line), "@F,%u,%u,%u,", file.id, file.offset, file.length);
        hex(line + n, file.data, file.length);
        if (send_frame(generation, line) == FACTORY_CDC_OK) {
            file.offset += file.length; file.length = 0; file.touched = sys_timer_get_ms();
        }
    }
}

void rdx_factory_audio_service(u32 generation)
{
    u32 now = sys_timer_get_ms();
    if (audio.negotiated != generation) { audio.negotiated = audio.timed = 0; }
    if (audio.running && !audio.complete) {
        if (audio.generation != generation) { audio.error = 1; audio.stop_reason = 3; }
        if ((u32)(now - audio.since) >= AUDIO_MAX_MS) audio.stop_reason = 2;
        if (!audio.started && (u32)(now - audio.since) >= 5000) audio.error = 3;
        if (audio.count && (u32)(now - audio.tx_since) >= 2000) audio.error = 1;
        if (audio.error || audio.stop_reason) {
            /* Shared DUT stop ticket also handles disconnected CDC owners. */
            rdx_dut_factory_record_stop();
        }
    }
    if (file.busy) {
        if (file.close_error && file.state == 3 && !file.pending) return; /* Reboot-only quarantine. */
        if (file.generation != generation || (u32)(now - file.touched) >= 10000) file.abort = 1;
        if (file.pending && file.done) {
            file.pending = 0;
            if (file.command == 3) {
                file.busy = file.close_error ? 1 : 0; file.length = 0;
                file.state = file.result < 0 || file.state == 3 ? 3 : 0;
            }
            else if (file.result < 0) { file.state = 3; file.abort = 1; }
            else if (file.command == 1) file.state = 2;
        }
        if (file.abort && file.busy && !file.pending && !file.close_error) file_post(3);
    }
}
#endif
