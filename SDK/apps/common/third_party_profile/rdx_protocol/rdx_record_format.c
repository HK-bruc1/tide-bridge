#include "app_config.h"
#include "system/includes.h"
#include "fs/fs.h"
#include "cJSON.h"
#include "rdx_record_format.h"
#include "rdx_record.h"
#include "rdx_factory_audio.h"
#include "rdx_protocol.h"
#include "rdx_app.h"
#include "dev_manager.h"
#include "rdx_util.h"
#include "rdx_record_storage.h"

/* RAW and the App's DAT schema are unchanged. MTA is firmware-only metadata.
 * A durable first-frame fingerprint binds it to the RAW, including after a
 * filename is reused. Recovery edits DAT before the library cache is loaded,
 * including the exclusive reload after an explicit format.
 */
extern void rdx_app_emmc_poweron(u8 check_en);
extern void sd_set_power_user(u8 en);

#define RF_ROOT "storage/sd0/C/"
#define RF_DAT RF_ROOT "uxfile.dat"
#define RF_TMP RF_ROOT "rfmt.tmp"
#define RF_COMMIT RF_ROOT "rfmt.cmt"
#define RF_DELETE RF_ROOT "rfdel.cmt"
#define RF_MAGIC 0x31464d52u
#define RF_MAX_FILES 65535
#define RF_OBJECT_SIZE 640

typedef struct {
    u32 magic, sn, start, scene, format, first_hash, ready;
    char name[12]; /* existing generator: 3..6 hex digits + .raw */
    u32 hash;
} rf_meta;

typedef struct {
    rf_meta meta;
    u32 size;
    u8 found;
    u8 health; /* 0 可信，1 不完整，2 格式未知，3 旧版文件 */
} rf_entry;

static rf_meta rf_current;
static volatile int rf_error;
static u8 rf_boot_started;
static volatile int rf_service = 1;
static volatile u32 rf_io_users;
static volatile u8 rf_download;
static char rf_deleted_name[12];
static volatile u8 rf_erasing;

u32 rdx_record_format_capacity(void)
{
    struct vfs_partition *part = fget_partition(RF_ROOT);
    return part ? part->total_size : 0;
}

int rdx_record_format_space(u32 *audio_bytes)
{
    *audio_bytes = 0;
    struct vfs_partition *part = fget_partition(RF_ROOT);
    u32 free_kb = 0xffffffffu;
    if (!part || !part->total_size) return -1;
    /* The SDK documents the return value as meaningless. Validate the output,
     * including an unchanged sentinel, instead of assuming ISO/FatFs errors. */
    fget_free_space(RF_ROOT, &free_kb);
    if (free_kb == 0xffffffffu || free_kb > part->total_size) return -1;
    FILE *dat = fopen(RF_DAT, "r");
    if (!dat) return -1;
    u32 index_bytes = flen(dat);
    if (fclose(dat)) return -1;
    /* One new DAT entry, two transactional index copies, and 64 KiB for
     * directories/MTA/cluster rounding and all accepted audio (<19 KiB).
     * Keep at least 1 MiB unavailable to RAW, also on a nearly empty index. */
    unsigned long long reserve = 2ull * (index_bytes + (unsigned long long)RF_OBJECT_SIZE) + 65536;
    if (reserve < 1048576) reserve = 1048576;
    unsigned long long free_bytes = (unsigned long long)free_kb * 1024;
    if (free_bytes > reserve) {
        unsigned long long budget = free_bytes - reserve;
        *audio_bytes = budget > 0xffffffffu ? 0xffffffffu : (u32)budget;
    }
    return 0;
}

int rdx_record_format_erase_enter(void)
{
    if (rdx_app_format_is_busy() || rdx_record_storage_active()) return -1;
    local_irq_disable();
    int busy = rf_erasing || rf_service > 0 || rf_io_users || rf_download ||
               rdx_record_storage_active() ||
               rdx_factory_audio_file_busy();
    if (!busy) {
        rf_erasing = 1;
        rf_service = 1;
    }
    local_irq_enable();
    if (!busy) {
        rdx_app_emmc_poweron(0);
        sd_set_power_user(1);
    }
    return busy ? -1 : 0;
}

void rdx_record_format_erased(void)
{
    /* Only after f_format succeeds, while every ordinary file entry is closed.
     * Never replay a pre-format transaction or retain a failed boot latch. */
    rf_boot_started = 0;
    rf_error = 0;
    memset(&rf_current, 0, sizeof(rf_current));
    rf_deleted_name[0] = 0;
}

void rdx_record_format_erase_leave(int result)
{
    if (!result) {
        rdx_record_storage_reset();
        rdx_record_storage_recovered();
    } else {
        rf_error = -1;
    }
    local_irq_disable();
    rf_service = result ? -1 : 0;
    rf_erasing = 0;
    local_irq_enable();
    rdx_record_format_service_ready(result);
}

int rdx_record_format_service_status(void)
{
    return rf_erasing ? 1 : rf_service ? rf_service : ((rf_download || rdx_factory_audio_file_busy()) ? 1 : 0);
}

int rdx_record_format_io_enter(void)
{
    local_irq_disable();
    int blocked = rdx_record_format_service_status();
    if (!blocked) ++rf_io_users;
    local_irq_enable();
    return blocked;
}

void rdx_record_format_command_enter(void)
{
    local_irq_disable();
    ++rf_io_users;
    local_irq_enable();
}

void rdx_record_format_io_leave(void)
{
    local_irq_disable();
    if (rf_io_users) --rf_io_users;
    local_irq_enable();
}

int rdx_record_format_download_busy(void)
{
    return rf_download;
}

int rdx_record_format_download_conflict(const char *operation)
{
    if (!rf_download) return 0;
    printf("[STORAGE_BUSY] owner=CDC_FILE request=%s\n", operation);
    return 1;
}

int rdx_record_format_protocol_busy(void)
{
    return rdx_record_format_download_conflict("BLE_STORAGE");
}

int rdx_record_format_download_acquire(void)
{
    local_irq_disable();
    int blocked = rf_error || rf_erasing || rf_service || rf_download ||
                  rf_io_users || rdx_factory_audio_file_busy();
    if (!blocked) rf_download = 1;
    local_irq_enable();
    return blocked ? -1 : 0;
}

void rdx_record_format_download_release(void)
{
    local_irq_disable();
    rf_download = 0;
    local_irq_enable();
}

void rdx_record_format_service_ready(int result)
{
    rf_service = result < 0 ? -1 : result > 0 ? 1 : 0;
    printf("[REC_FORMAT] file service=%d\n", rf_service);
}
/* Boot is serialized before RDX initialization; cJSON owns parsed strings. */
static char rf_json[RF_OBJECT_SIZE];
typedef struct {
    u8 data[256];
    u32 next, size, remaining;
    int error;
} rf_reader_state;
static rf_reader_state rf_reader;

static u32 rf_hash(const void *data, u32 len)
{
    const u8 *p = data;
    u32 h = 2166136261u;
    while (len--) {
        h = (h ^ *p++) * 16777619u;
    }
    return h;
}

static u32 rf_frame_bytes(u8 format)
{
    return format == RECORD_FORMATE_OPUS_16K_MONO ? 40 :
           format == RECORD_FORMATE_OPUS_16K_STERO ? 80 : 0;
}

static int rf_name_valid(const char *name)
{
    u32 n = 0;
    while (n < 7 && ((name[n] >= '0' && name[n] <= '9') ||
                    (name[n] >= 'a' && name[n] <= 'f') ||
                    (name[n] >= 'A' && name[n] <= 'F'))) {
        ++n;
    }
    return n >= 3 && n <= 6 && !strcmp(name + n, ".raw");
}

static void rf_path(char *path, const char *name, int metadata)
{
    sprintf(path, RF_ROOT "%s", name);
    if (metadata) {
        strcpy(path + strlen(path) - 3, "mta");
    }
}

static int rf_fail_at(unsigned line)
{
    rf_error = -1;
    printf("[REC_FORMAT] file service error line=%u; BLE/power remain available\n", line);
    return -1;
}

#define rf_fail() rf_fail_at(__LINE__)

void rdx_record_format_storage_fault(void)
{
    rf_error = -1;
}

int rdx_record_format_status(void)
{
    return rf_error;
}

static int rf_write(const char *path, const void *data, u32 len)
{
    FILE *f = fopen(path, "w+");
    if (!f) {
        return -1;
    }
    int written = fwrite((void *)data, 1, len, f);
    int truncated = ftruncate(f, len);
    int closed = fclose(f);
    /* JL VFS resolves a mounted filesystem path, not a device logo. */
    int flushed = f_flush_wbuf(RF_ROOT);
    if (written != len || truncated || closed || flushed) {
        printf("[REC_FORMAT] write failed path=%s bytes=%d/%u truncate=%d close=%d flush=%d\n",
               path, written, len, truncated, closed, flushed);
        return -1;
    }
    return 0;
}

static int rf_save_meta(rf_meta *meta)
{
    char path[40];
    meta->hash = rf_hash(meta, sizeof(*meta) - sizeof(meta->hash));
    rf_path(path, meta->name, 1);
    if (rf_write(path, meta, sizeof(*meta))) {
        return rf_fail();
    }
    /* Read back before allowing the first audio write. */
    rf_meta check;
    FILE *f = fopen(path, "r");
    if (!f) {
        return rf_fail();
    }
    int ok = fread(&check, 1, sizeof(check), f) == sizeof(check) &&
             !memcmp(&check, meta, sizeof(check));
    if (fclose(f)) {
        ok = 0;
    }
    return ok ? 0 : rf_fail();
}

int rdx_record_format_begin(const uxfile_data_t *file, u8 format)
{
    if (!file || !file->sn || !rf_frame_bytes(format) ||
        !rf_name_valid(file->filename)) {
        return rf_fail();
    }
    rf_error = 0; /* 上一次会话的失败不得影响新会话。 */
    memset(&rf_current, 0, sizeof(rf_current));
    rf_current.magic = RF_MAGIC;
    rf_current.sn = file->sn;
    rf_current.start = file->start_time;
    rf_current.scene = file->record_scene;
    rf_current.format = format;
    strcpy(rf_current.name, file->filename);
    /* No RAW is produced before frame() commits the complete metadata. */
    return 0;
}

int rdx_record_format_frame(const uxfile_data_t *file, u8 format,
                            const u8 *data, u32 len)
{
    if (rf_error || !file || !data || !len ||
        rf_current.sn != file->sn || strcmp(rf_current.name, file->filename) ||
        rf_current.format != format || len != rf_frame_bytes(format) ||
        file->frame_size != len) {
        return rf_fail();
    }
    if (!rf_current.ready) {
        rf_current.first_hash = rf_hash(data, len);
        rf_current.ready = 1;
        if (rf_save_meta(&rf_current)) {
            return -1;
        }
        printf("[REC_FORMAT] pinned sn=%u format=%u frame=%u\n",
               file->sn, format, len);
    }
    return 0;
}

/* A completed temp file plus its checked commit record is replayable after
 * power loss during DAT replacement. Never treat a partial temp as committed.
 */
static void rf_cooperate(u32 bytes)
{
    static u32 budget;
    budget += bytes;
    if (budget >= 16384) { budget = 0; os_time_dly(1); }
}

static int rf_file_hash(const char *path, u32 *hash, u32 *size)
{
    u8 buf[256];
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    u32 remaining = flen(f), total = remaining, h = 2166136261u;
    int ok = 1;
    while (remaining) {
        u32 n = remaining > sizeof(buf) ? sizeof(buf) : remaining;
        if (fread(buf, 1, n, f) != n) { ok = 0; break; }
        for (u32 i = 0; i < n; ++i) h = (h ^ buf[i]) * 16777619u;
        remaining -= n;
        wdt_clear();
        rf_cooperate(n);
    }
    if (fclose(f)) ok = 0;
    *hash = h;
    *size = total;
    return ok ? 0 : -1;
}

static int rf_replay(void)
{
    u32 commit[3], h, size;
    FILE *f = fopen(RF_COMMIT, "r");
    if (!f) return 0;
    int ok = flen(f) == sizeof(commit) &&
             fread(commit, 1, sizeof(commit), f) == sizeof(commit);
    if (fclose(f)) ok = 0;
    if (!ok || commit[0] != RF_MAGIC ||
        rf_file_hash(RF_TMP, &h, &size) ||
        commit[1] != h || commit[2] != size) return -1;
    FILE *src = fopen(RF_TMP, "r");
    if (!src) return -1;
    FILE *dst = fopen(RF_DAT, "w+");
    if (!dst) { fclose(src); return -1; }
    u8 buf[256];
    u32 remaining = size;
    while (remaining) {
        u32 n = remaining > sizeof(buf) ? sizeof(buf) : remaining;
        if (fread(buf, 1, n, src) != n || fwrite(buf, 1, n, dst) != n) {
            ok = 0; break;
        }
        remaining -= n;
        wdt_clear();
        rf_cooperate(n);
    }
    if (ok && ftruncate(dst, size)) ok = 0;
    if (fclose(src)) ok = 0;
    if (fclose(dst)) ok = 0;
    if (f_flush_wbuf(RF_ROOT)) ok = 0;
    if (!ok || rf_file_hash(RF_DAT, &h, &size) ||
        h != commit[1] || size != commit[2]) return -1;
    /* Retain temp until removal of the commit marker is durable. */
    if (fdelete_by_name(RF_COMMIT) || f_flush_wbuf(RF_ROOT)) return -1;
    return 0;
}

/* Streaming JSON reader: one object at a time, preserving marks and all
 * legacy fields. No full-index cJSON tree on the embedded heap. */
static int rf_reader_getc(FILE *handle, rf_reader_state *reader)
{
    if (reader->error) return -1;
    if (reader->next == reader->size) {
        u32 count = reader->remaining;
        if (!count) return -1;
        if (count > sizeof(reader->data)) count = sizeof(reader->data);
        if (fread(reader->data, 1, count, handle) != count) {
            reader->error = 1;
            return -1;
        }
        reader->remaining -= count;
        reader->size = count;
        reader->next = 0;
    }
    return reader->data[reader->next++];
}

static int rf_reader_nonspace(FILE *handle, rf_reader_state *reader)
{
    int character;
    do {
        character = rf_reader_getc(handle, reader);
    } while (character == ' ' || character == '\r' || character == '\n' || character == '\t');
    return character;
}

static int rf_reader_object(FILE *handle, rf_reader_state *reader, char *buffer)
{
    int depth = 1, quoted = 0, escape = 0;
    u32 length = 1;
    buffer[0] = '{';
    while (depth) {
        int character = rf_reader_getc(handle, reader);
        if (character < 0 || length >= RF_OBJECT_SIZE - 1 || !character) return -1;
        buffer[length++] = character;
        if (quoted) {
            if (escape) escape = 0;
            else if (character == '\\') escape = 1;
            else if (character == '"') quoted = 0;
        } else if (character == '"') quoted = 1;
        else if (character == '{' || character == '[') { if (++depth > 8) return -1; }
        else if (character == '}' || character == ']') --depth;
    }
    buffer[length] = 0;
    return 0;
}

static int rf_nonspace(FILE *handle)
{
    return rf_reader_nonspace(handle, &rf_reader);
}

static int rf_object(FILE *handle, char *buffer)
{
    return rf_reader_object(handle, &rf_reader, buffer);
}

static int rf_emit(FILE *dst, cJSON *obj, u32 *count)
{
    char *buf = rf_json;
    if (!cJSON_PrintPreallocated(obj, buf, RF_OBJECT_SIZE, 0)) return -1;
    u32 n = strlen(buf);
    if (dst && *count && fwrite(",", 1, 1, dst) != 1) return -1;
    if (dst && fwrite(buf, 1, n, dst) != n) return -1;
    ++*count;
    return 0;
}

static int rf_number(cJSON *obj, const char *key, u32 value)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (item) {
        if (!cJSON_IsNumber(item)) return -1;
        cJSON_SetNumberValue(item, value);
        return 0;
    }
    return cJSON_AddNumberToObject(obj, key, value) ? 0 : -1;
}

static u32 rf_entry_frame(const rf_entry *e)
{
    /* 明确标记为不支持的格式；旧解码器会将 0 解释为旧版双声道。 */
    return e->health ? 1 : rf_frame_bytes(e->meta.format);
}

static u32 rf_entry_opus(const rf_entry *e)
{
    return e->health ? 0 : rdx_protocol_calc_opus_format(e->meta.format);
}

static int rf_fix_object(cJSON *obj, rf_entry *entries, u32 total, int *changed)
{
    cJSON *name = cJSON_GetObjectItemCaseSensitive(obj, "name");
    cJSON *sn = cJSON_GetObjectItemCaseSensitive(obj, "sn");
    if (!cJSON_IsString(name) || !cJSON_IsNumber(sn) ||
        sn->valuedouble < 1 || sn->valuedouble > 0x7ffffffeu ||
        sn->valuedouble != (u32)sn->valuedouble) return -1;
    char safe[12];
    u32 length = strlen(name->valuestring);
    if (length >= sizeof(safe) || length < 7) return -1;
    strcpy(safe, name->valuestring);
    if (!strcmp(safe + length - 4, ".mta")) strcpy(safe + length - 3, "raw");
    if (!rf_name_valid(safe)) return -1;
    for (u32 i = 0; i < total; ++i) {
        rf_entry *e = &entries[i];
        if (strcmp(name->valuestring, e->meta.name)) {

            continue;
        }
        if (e->found) return -1;
        /* 以现有有效 DAT 中的管理标识为准，MTA 可能已经过期。 */
        e->meta.sn = (u32)sn->valuedouble;
        if (e->health == 3) { e->found = 1; continue; }
        e->found = 1;
        cJSON *frame = cJSON_GetObjectItemCaseSensitive(obj, "frame_size");
        cJSON *opus = cJSON_GetObjectItemCaseSensitive(obj, "opus");
        if (!cJSON_IsNumber(frame) ||
            frame->valuedouble != rf_entry_frame(e) ||
            !cJSON_IsNumber(opus) ||
            opus->valuedouble != rf_entry_opus(e)) {
            *changed = 1;
        }
        if (rf_number(obj, "frame_size", rf_entry_frame(e)) ||
            rf_number(obj, "opus", rf_entry_opus(e))) return -1;
    }
    return 0;
}

static cJSON *rf_restore_object(const rf_entry *e)
{
    char path[40], crc_text[16];
    u8 buf[512];
    u32 crc = 0, remaining = e->health ? 0 : e->size;
    rf_path(path, e->meta.name, 0);
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    int ok = 1;
    while (remaining) {
        u32 n = remaining > sizeof(buf) ? sizeof(buf) : remaining;
        if (fread(buf, 1, n, f) != n) { ok = 0; break; }
        crc = rdx_util_crc32(buf, n, &crc);
        remaining -= n;
        wdt_clear();
        rf_cooperate(n);
    }
    if (fclose(f)) ok = 0;
    if (!ok) return NULL;
    cJSON *obj = cJSON_CreateObject();
    if (!obj) return NULL;
    sprintf(crc_text, "%u", crc);
    if (!cJSON_AddStringToObject(obj, "name", e->meta.name) ||
        !cJSON_AddStringToObject(obj, "crc", crc_text) ||
        !cJSON_AddArrayToObject(obj, "marks") ||
        rf_number(obj, "sn", e->meta.sn) || rf_number(obj, "size", e->size) ||
        rf_number(obj, "scene", e->meta.scene) ||
        rf_number(obj, "start_time", e->meta.start) ||
        rf_number(obj, "end_time", e->meta.start + (rf_frame_bytes(e->meta.format) ? e->size / rf_frame_bytes(e->meta.format) / 50 : 0)) ||
        rf_number(obj, "frame_size", rf_entry_frame(e)) ||
        rf_number(obj, "opus", rf_entry_opus(e))) {
        cJSON_Delete(obj);
        return NULL;
    }
    return obj;
}

/* First pass is read-only: a consistent DAT must not be rewritten each boot.
 * The second pass uses the same parser and the existing durable transaction.
 * Both passes run before the archive owns the index. */
/* 已输出的 DAT 优先保留编号；新增或冲突条目只取未占用编号。 */
static int rf_claim_sn(u32 *sn, rf_entry *entries, u32 total,
                       u32 **used, u32 *count)
{
    u32 candidate = *sn;
    for (u32 i = 0; i < *count; ++i) {
        if ((*used)[i] != candidate) continue;
        candidate = 1;
        for (;;) {
            int occupied = 0;
            for (u32 j = 0; j < *count; ++j)
                if ((*used)[j] == candidate) occupied = 1;
            for (u32 j = 0; j < total; ++j)
                if (entries[j].meta.sn == candidate) occupied = 1;
            if (!occupied) break;
            if (++candidate >= 0x7ffffffeu) return -1;
        }
        break;
    }
    if (*count >= RF_MAX_FILES) return -1;
    if (!(*count % 32)) {
        /* JL 的 realloc 不支持空指针，首次分配使用 malloc。 */
        u32 bytes = (*count + 32) * sizeof(u32);
        u32 *grown = *used ? realloc(*used, bytes) : malloc(bytes);
        if (!grown) return -1;
        *used = grown;
    }
    (*used)[(*count)++] = candidate;
    *sn = candidate;
    return 0;
}

static int rf_merge(rf_entry *entries, u32 total, int persist)
{
    for (u32 i = 0; i < total; ++i) entries[i].found = 0;
    FILE *src = fopen(RF_DAT, "r");
    FILE *dst = persist ? fopen(RF_TMP, "w+") : NULL;
    if (persist && !dst) { if (src) fclose(src); return -1; }
    int changed = !src;
    int ok = !persist || fwrite("[", 1, 1, dst) == 1;
    u32 count = 0, used_count = 0;
    u32 *used = NULL;
    if (src && ok) {
        memset(&rf_reader, 0, sizeof(rf_reader));
        rf_reader.remaining = flen(src);
        int c = rf_nonspace(src);
        if (c != '[') ok = 0;
        c = rf_nonspace(src);
        while (ok && c == '{') {
            char *buf = rf_json;
            if (rf_object(src, buf)) { ok = 0; break; }
            cJSON *obj = cJSON_ParseWithOpts(buf, NULL, 1);
            cJSON *name = obj ? cJSON_GetObjectItemCaseSensitive(obj, "name") : NULL;
            if (cJSON_IsString(name) && rf_deleted_name[0] &&
                !strcmp(name->valuestring, rf_deleted_name)) {
                changed = 1;
            } else {
                cJSON *id = obj ? cJSON_GetObjectItemCaseSensitive(obj, "sn") : NULL;
                if (!cJSON_IsNumber(id) || id->valuedouble < 1 ||
                    id->valuedouble > 0x7ffffffeu ||
                    id->valuedouble != (u32)id->valuedouble) {
                    ok = 0;
                } else {
                    u32 sn = (u32)id->valuedouble;
                    if (rf_claim_sn(&sn, entries, total, &used, &used_count)) ok = 0;
                    if (sn != (u32)id->valuedouble) {
                        cJSON_SetNumberValue(id, sn);
                        changed = 1;
                    }
                    if (ok && (rf_fix_object(obj, entries, total, &changed) ||
                               rf_emit(dst, obj, &count))) ok = 0;
                }
            }
            cJSON_Delete(obj);
            c = rf_nonspace(src);
            if (c != ',') break;
            c = rf_nonspace(src);
            if (c != '{') ok = 0;
            wdt_clear();
        rf_cooperate(RF_OBJECT_SIZE);
        }
        if (c != ']' || rf_nonspace(src) != -1 || rf_reader.error) ok = 0;
    }
    if (src && fclose(src)) ok = 0;
    for (u32 i = 0; ok && i < total; ++i) {
        if (entries[i].found) continue;
        changed = 1;
        if (rf_claim_sn(&entries[i].meta.sn, entries, total, &used, &used_count)) {
            ok = 0;
            break;
        }
        if (!persist) {
            if (count >= RF_MAX_FILES) ok = 0;
            else ++count;
            continue;
        }
        cJSON *obj = rf_restore_object(&entries[i]);
        if (!obj || rf_emit(dst, obj, &count)) ok = 0;
        cJSON_Delete(obj);
    }
    free(used);
    if (!persist) return ok ? changed : -1;
    if (ok && fwrite("]", 1, 1, dst) != 1) ok = 0;
    /* JL w+ need not truncate an existing file. */
    if (ok && ftruncate(dst, ftell(dst))) ok = 0;
    if (fclose(dst)) ok = 0;
    if (f_flush_wbuf(RF_ROOT)) ok = 0;
    if (!ok) return -1;
    u32 commit[3] = { RF_MAGIC, 0, 0 };
    if (rf_file_hash(RF_TMP, &commit[1], &commit[2]) ||
        rf_write(RF_COMMIT, commit, sizeof(commit))) return -1;
    return rf_replay();
}


/* 使用未占用的名称保留损坏索引或事务，不覆盖之前的故障现场。
 * 重命名结果刷写完成后，再构建替代文件。 */
static int rf_preserve(const char *path)
{
    FILE *src = fopen(path, "r");
    if (!src) return 0;
    char name[16], full[40];
    for (u32 n = 0; n < 1000; ++n) {
        sprintf(name, "rf%06u.bak", n);
        sprintf(full, RF_ROOT "%s", name);
        FILE *old = fopen(full, "r");
        if (old) { fclose(old); continue; }
        int result = frename(src, name);
        if (fclose(src)) result = -1;
        if (f_flush_wbuf(RF_ROOT)) result = -1;
        return result;
    }
    fclose(src);
    return -1;
}

static int rf_manage_name(const char *name)
{
    char raw[12];
    u32 n = name ? strlen(name) : 0;
    if (n < 7 || n >= sizeof(raw)) return 0;
    strcpy(raw, name);
    if (!strcmp(raw + n - 4, ".mta")) strcpy(raw + n - 3, "raw");
    return rf_name_valid(raw);
}

static int rf_unlink(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return 0; /* 兼容上次已完成删除后的重试 */
    if (fclose(f)) return -1;
    return fdelete_by_name(path);
}

static int rf_delete_replay(void)
{
    rf_meta mark = {0};
    FILE *f = fopen(RF_DELETE, "r");
    if (!f) return 0;
    int valid = flen(f) == sizeof(mark) && fread(&mark, 1, sizeof(mark), f) == sizeof(mark);
    if (fclose(f)) valid = 0;
    if (!valid || mark.magic != RF_MAGIC || mark.name[sizeof(mark.name)-1] ||
        !rf_manage_name(mark.name) || mark.hash != rf_hash(&mark, sizeof(mark)-sizeof(mark.hash))) {
        return rf_preserve(RF_DELETE);
    }
    char path[40];
    rf_path(path, mark.name, 0);
    if (rf_unlink(path)) return -1;
    if (strstr(mark.name, ".raw")) {
        rf_path(path, mark.name, 1);
        if (rf_unlink(path)) return -1;
    }
    if (f_flush_wbuf(RF_ROOT)) return -1;
    strcpy(rf_deleted_name, mark.name);
    int result = rf_merge(NULL, 0, 1);
    rf_deleted_name[0] = 0;
    if (result || rf_unlink(RF_DELETE) || f_flush_wbuf(RF_ROOT)) return -1;
    return 0;
}

/* 仅由文件工作任务在持有库 DAT 互斥锁且刷写脏缓存后调用。
 * 创建持久化删除意图前，必须同时校验两个身份字段。 */
int rdx_record_format_delete(u32 sn, const char *name)
{
    if (!rf_manage_name(name) || !sn) return RDX_RECORD_DELETE_REJECTED;
    if (rf_replay() || rf_delete_replay()) return -1;
    FILE *src = fopen(RF_DAT, "r");
    if (!src) return -1;
    memset(&rf_reader, 0, sizeof(rf_reader));
    rf_reader.remaining = flen(src);
    int ok = rf_nonspace(src) == '[', found = 0, mismatch = 0;
    int c = rf_nonspace(src);
    while (ok && c == '{') {
        if (rf_object(src, rf_json)) { ok = 0; break; }
        cJSON *obj = cJSON_ParseWithOpts(rf_json, NULL, 1);
        cJSON *n = obj ? cJSON_GetObjectItemCaseSensitive(obj, "name") : NULL;
        cJSON *id = obj ? cJSON_GetObjectItemCaseSensitive(obj, "sn") : NULL;
        if (!cJSON_IsString(n) || !cJSON_IsNumber(id)) ok = 0;
        else if (!strcmp(n->valuestring, name)) {
            if (id->valuedouble == sn) found = 1;
            else mismatch = 1;
        }
        cJSON_Delete(obj);
        c = rf_nonspace(src);
        if (c != ',') break;
        c = rf_nonspace(src);
        if (c != '{') ok = 0;
    }
    if (c != ']' || rf_nonspace(src) != -1 || rf_reader.error) ok = 0;
    if (fclose(src)) ok = 0;
    if (!ok) return -1;
    if (mismatch) return RDX_RECORD_DELETE_REJECTED;
    if (!found) {
        char path[40];
        rf_path(path, name, 0);
        FILE *existing = fopen(path, "r");
        if (existing) { fclose(existing); return RDX_RECORD_DELETE_REJECTED; }
        return 0; /* 幂等重试；不得删除未纳入索引的替代文件 */
    }
    rf_meta mark = {0};
    mark.magic = RF_MAGIC;
    mark.sn = sn;
    strcpy(mark.name, name);
    mark.hash = rf_hash(&mark, sizeof(mark)-sizeof(mark.hash));
    if (rf_write(RF_DELETE, &mark, sizeof(mark))) return -1;
    return rf_delete_replay();
}

/* 传输层已将 DAT 分为每包 460 字节；此处读取完整的持久化管理索引，
 * 避免只返回库缓存中的前 300 条记录。调用前，
 * 库须持有 DAT 互斥锁并刷写脏条目。 */
uxfile_datfile_info_t *rdx_record_format_list(uxfile_datfile_info_t *info)
{
    if (info->data_len) return info;
    FILE *f = fopen(RF_DAT, "r");
    if (!f) return info;
    u32 size = flen(f);
    u8 *data = size && size < 0x7fffffffu ? malloc(size + 1) : NULL;
    int ok = data && fread(data, 1, size, f) == size;
    if (fclose(f)) ok = 0;
    if (!ok) {
        printf("[REC_FORMAT] list unavailable bytes=%u allocated=%u\n", size, data != NULL);
        free(data);
        return info;
    }
    data[size] = 0;
    free(info->data);
    info->data = data;
    info->data_len = size;
    /* 列表应答和分包请求均使用 divpack 作为总包数。 */
    info->divpack = (size + 459) / 460;
    return info;
}

static int rf_scan_entries(rf_entry **out, u32 *total)
{
    u32 capacity = *total;
    /* 独立于 MTA 枚举 RAW，防止元数据缺失或损坏导致用户录音
     * 被隐藏；第二轮保留孤立元数据，以便用户删除。 */
    for (int pass = 0; pass < 2; ++pass) {
        struct vfscan *scan = fscan(RF_ROOT, pass ? "-tMTA -sn" : "-tRAW -sn", 1);
        if (!scan) return -1;
        int ok = 1;
        for (u32 i = 1; i <= scan->file_number; ++i) {
            FILE *f = fselect(scan, FSEL_BY_NUMBER, i);
            if (!f) { ok = 0; break; }
            rf_entry e = {0};
            int n = fget_name(f, (u8 *)e.meta.name, sizeof(e.meta.name) - 1);
            e.size = flen(f);
            if (fclose(f)) { ok = 0; break; }
            if (n < 7 || n > 10) continue;
            for (int j = 0; j < n; ++j) {
                if (e.meta.name[j] >= 'A' && e.meta.name[j] <= 'Z') e.meta.name[j] += 'a' - 'A';
            }
            char raw[12], path[40];
            strcpy(raw, e.meta.name);
            strcpy(raw + n - 3, "raw");
            if (!rf_name_valid(raw)) continue;
            if (pass) {
                rf_path(path, raw, 0);
                f = fopen(path, "r");
                if (f) { fclose(f); continue; }
                e.health = 2;
            } else {
                rf_meta m = {0};
                rf_path(path, raw, 1);
                f = fopen(path, "r");
                e.health = f ? 2 : 3;
                if (f) {
                    int valid = flen(f) == sizeof(m) && fread(&m, 1, sizeof(m), f) == sizeof(m);
                    if (fclose(f)) valid = 0;
                    if (valid && m.magic == RF_MAGIC && m.sn && m.sn < 0x7ffffffeu &&
                        m.ready == 1 && !m.name[sizeof(m.name)-1] &&
                        !strcmp(m.name, raw) && rf_frame_bytes(m.format) &&
                        m.hash == rf_hash(&m, sizeof(m)-sizeof(m.hash))) {
                        u8 first[80];
                        u32 frame = rf_frame_bytes(m.format);
                        rf_path(path, raw, 0);
                        f = fopen(path, "r");
                        valid = f && e.size >= frame && fread(first, 1, frame, f) == frame &&
                                rf_hash(first, frame) == m.first_hash;
                        if (f && fclose(f)) valid = 0;
                        if (valid) { e.meta = m; e.health = e.size % frame ? 1 : 0; }
                    }
                }
            }
            if (e.health >= 2) {
                /* 根据固件使用的十六进制文件名生成唯一标识；
                 * 孤立 MTA 使用独立编号范围，不信任损坏的 SN。 */
                e.meta.sn = (pass ? 0x12000000u : 0x10000000u) + strtoul(raw, NULL, 16);
            }
            for (u32 j = 0; j < *total; ++j) {
                if ((*out)[j].meta.sn == e.meta.sn) {
                    if (++e.meta.sn >= 0x7ffffffeu) { ok = 0; break; }
                    j = (u32)-1; /* 重新与此前所有标识逐一检查冲突 */
                }
            }
            if (!ok) break;
            if (*total >= RF_MAX_FILES) { ok = 0; break; }
            if (*total == capacity) {
                /* 分批扩容，避免每枚举一个文件都搬移整个数组。
                 * 分配失败必须终止本轮恢复，不发布不完整索引。 */
                u32 next = capacity + 32;
                if (next > RF_MAX_FILES) next = RF_MAX_FILES;
                /* JL 的 realloc 不支持空指针，首次分配使用 malloc。 */
                u32 bytes = next * sizeof(e);
                rf_entry *grown = *out ? realloc(*out, bytes) : malloc(bytes);
                if (!grown) {
                    printf("[REC_FORMAT] scan allocation failed entries=%u\n", next);
                    ok = 0;
                    break;
                }
                *out = grown;
                capacity = next;
            }
            (*out)[(*total)++] = e;
            if (e.health) printf("[REC_FORMAT] manageable name=%s health=%u size=%u\n", e.meta.name, e.health, e.size);
            os_time_dly(1);
        }
        fscan_release(scan);
        if (!ok) return -1;
    }
    return 0;
}

int rdx_record_format_boot(void)
{
    if (rf_boot_started) return rf_error;
    rf_boot_started = 1;
    rdx_app_emmc_poweron(0);
    sd_set_power_user(1);
    if (!dev_manager_list_check_by_logo("sd0") && dev_manager_add("sd0")) return rf_fail();
    if (rf_replay()) {
        /* 过期提交记录不得在下次启动时恢复已删除条目。 */
        if (rf_preserve(RF_COMMIT) || rf_preserve(RF_TMP)) return rf_fail();
    }
    /* 若删除中断后遇到损坏的 DAT，先在下方重建索引，再
     * 完成删除意图；不得恢复已经删除的 RAW。 */
    int delete_pending = rf_delete_replay();
    rf_entry *entries = NULL;
    u32 total = 0;
    int ok = !rf_scan_entries(&entries, &total);
    if (ok) {
        int changed = rf_merge(entries, total, 0);
        if (changed < 0) {
            /* 保留原件，仅将经过校验的重建结果交给
             * 闭源解析器，原始音频文件保持不变。 */
            ok = !rf_preserve(RF_DAT);
            changed = 1;
        }
        if (ok && changed && rf_merge(entries, total, 1)) ok = 0;
    }
    free(entries);
    if (ok && delete_pending && rf_delete_replay()) ok = 0;
    if (!ok) return rf_fail();
    rf_error = 0;
    printf("[REC_FORMAT] boot reconciled files=%u\n", total);
    return 0;
}

#if TCFG_T2620_FACTORY_USB_CDC_ENABLE && !TCFG_T2620_FACTORY_USB_CDC_TEST_ENABLE
static int rf_read_meta(const char *name, rf_meta *meta)
{
    if (!name || !meta || !rf_name_valid(name)) return -1;
    char path[40];
    rf_path(path, name, 1);
    FILE *handle = fopen(path, "r");
    if (!handle) return -1;
    int valid = flen(handle) == sizeof(*meta) &&
                fread(meta, 1, sizeof(*meta), handle) == sizeof(*meta);
    if (fclose(handle)) return -2;
    if (!valid || meta->magic != RF_MAGIC || !meta->sn ||
        meta->ready != 1 || meta->name[sizeof(meta->name) - 1] ||
        strcmp(meta->name, name) ||
        meta->hash != rf_hash(meta, sizeof(*meta) - sizeof(meta->hash))) return -1;
    return 0;
}

static int rf_open_verified_raw(const rf_meta *meta, u32 maximum,
                                FILE **handle, u32 *size)
{
    *handle = NULL;
    *size = 0;
    if (!meta || !rf_name_valid(meta->name) ||
        (meta->format != RECORD_FORMATE_OPUS_16K_MONO &&
         meta->format != RECORD_FORMATE_OPUS_16K_STERO)) return -1;
    u32 frame_bytes = rf_frame_bytes(meta->format);
    char path[40];
    u8 first[80];
    rf_path(path, meta->name, 0);
    FILE *opened = fopen(path, "r");
    if (!opened) return -1;
    u32 length = flen(opened);
    int valid = length && length <= maximum && !(length % frame_bytes) &&
                fread(first, 1, frame_bytes, opened) == frame_bytes &&
                rf_hash(first, frame_bytes) == meta->first_hash;
    if (!valid || fseek(opened, 0, SEEK_SET)) {
        return fclose(opened) ? -2 : -1;
    }
    *handle = opened;
    *size = length;
    return 0;
}

static cJSON *rf_unique_field(cJSON *object, const char *name)
{
    cJSON *found = NULL;
    for (cJSON *field = object ? object->child : NULL; field; field = field->next) {
        if (field->string && !strcmp(field->string, name)) {
            if (found) return NULL;
            found = field;
        }
    }
    return found;
}

static int rf_index_u32(cJSON *object, const char *name, u32 *value)
{
    cJSON *field = rf_unique_field(object, name);
    if (!cJSON_IsNumber(field) || !(field->valuedouble >= 0 &&
        field->valuedouble <= 0xffffffffu) ||
        field->valuedouble != (u32)field->valuedouble) return -1;
    *value = (u32)field->valuedouble;
    return 0;
}

static int rf_read_index_identity(u32 sn, const rf_meta *meta, u32 *size)
{
    *size = 0;
    if (!sn || sn > 0x7ffffffeu || !meta || !rf_name_valid(meta->name) ||
        meta->format != RECORD_FORMATE_OPUS_16K_MONO) return -1;
    FILE *handle = fopen(RF_DAT, "r");
    if (!handle) return -1;
    rf_reader_state reader = {0};
    char object_text[RF_OBJECT_SIZE];
    reader.remaining = flen(handle);
    int valid = reader.remaining && reader.remaining <= RF_MAX_FILES * RF_OBJECT_SIZE &&
                rf_reader_nonspace(handle, &reader) == '[';
    int character = valid ? rf_reader_nonspace(handle, &reader) : -1;
    u32 count = 0, matches = 0, fixed_size = 0;
    while (valid && character == '{') {
        if (++count > RF_MAX_FILES || rf_reader_object(handle, &reader, object_text)) {
            valid = 0;
            break;
        }
        cJSON *object = cJSON_ParseWithOpts(object_text, NULL, 1);
        cJSON *name = rf_unique_field(object, "name");
        u32 indexed_sn = 0;
        if (!cJSON_IsObject(object) || !cJSON_IsString(name) ||
            rf_index_u32(object, "sn", &indexed_sn) ||
            !indexed_sn || indexed_sn > 0x7ffffffeu) {
            valid = 0;
        } else if (indexed_sn == sn || !strcmp(name->valuestring, meta->name)) {
            u32 start = 0, end = 0, frame = 0, opus = 0;
            if (++matches != 1 || indexed_sn != sn ||
                strcmp(name->valuestring, meta->name) ||
                rf_index_u32(object, "size", &fixed_size) ||
                rf_index_u32(object, "start_time", &start) ||
                rf_index_u32(object, "end_time", &end) ||
                rf_index_u32(object, "frame_size", &frame) ||
                rf_index_u32(object, "opus", &opus) ||
                !fixed_size || fixed_size % 40 || start != meta->start || end < start ||
                frame != 40 || opus != rdx_protocol_calc_opus_format(meta->format)) {
                valid = 0;
            }
        }
        cJSON_Delete(object);
        if (!valid) break;
        character = rf_reader_nonspace(handle, &reader);
        if (character != ',') break;
        character = rf_reader_nonspace(handle, &reader);
        if (character != '{') valid = 0;
        wdt_clear();
        rf_cooperate(RF_OBJECT_SIZE);
    }
    if (character != ']' || rf_reader_nonspace(handle, &reader) != -1 || reader.error) valid = 0;
    if (fclose(handle)) return -2;
    if (!valid) return -1;
    if (!matches) return -3;
    *size = fixed_size;
    return 0;
}

static int rf_open_indexed_raw(u32 sn, const char *name, u32 maximum,
                               FILE **handle, u32 *size)
{
    if (!handle || !size) return -1;
    *handle = NULL;
    *size = 0;
    rf_meta meta = {0};
    int result = rf_read_meta(name, &meta);
    if (result) return result;
    u32 indexed_size = 0;
    result = rf_read_index_identity(sn, &meta, &indexed_size);
    if (result) return result;
    if (indexed_size > maximum) return -1;
    result = rf_open_verified_raw(&meta, maximum, handle, size);
    if (result) return result;
    if (*size == indexed_size) return 0;
    int closed = fclose(*handle);
    *handle = NULL;
    *size = 0;
    return closed ? -2 : -1;
}

int rdx_record_format_download_list_open(void **handle, u32 *size)
{
    extern int rdx_uxfile_download_snapshot(void);
    if (!handle || !size) return -1;
    *handle = NULL;
    *size = 0;
    if (!rf_download || rf_error || rf_service) return -1;
    rdx_app_emmc_poweron(0);
    sd_set_power_user(1);
    int snapshot = rdx_uxfile_download_snapshot();
    if (snapshot) return snapshot;
    FILE *opened = fopen(RF_DAT, "r");
    if (!opened) return -1;
    u32 length = flen(opened);
    if (!length || length > 16777216u) return fclose(opened) ? -2 : -1;
    *handle = opened;
    *size = length;
    return 0;
}

int rdx_record_format_download_open(u32 sn, const char *name, void **handle, u32 *size)
{
    extern int rdx_uxfile_download_snapshot(void);
    if (!handle || !size) return -1;
    *handle = NULL;
    *size = 0;
    if (!rf_download || rf_error || rf_service) return -1;
    rdx_app_emmc_poweron(0);
    sd_set_power_user(1);
    int snapshot = rdx_uxfile_download_snapshot();
    if (snapshot) return snapshot;
    FILE *opened = NULL;
    /* 按现有 u32 文件协议的表示范围流式读取合法 RAW，不另设业务大小上限。 */
    int result = rf_open_indexed_raw(sn, name, 0xffffffffu, &opened, size);
    *handle = opened;
    return result;
}

void *rdx_record_format_factory_open(u32 sn, const char *name, u32 start, u32 *size, u32 *crc)
{
    if (!rdx_factory_audio_file_busy() || !name || !sn || !size || !crc ||
        rf_error || rf_service) return NULL;
    rf_meta meta = {0};
    if (rf_read_meta(name, &meta) || meta.sn != sn || meta.start != start ||
        meta.format != RECORD_FORMATE_OPUS_16K_MONO) return NULL;
    FILE *handle = NULL;
    u32 length = 0;
    if (rf_open_verified_raw(&meta, 40000, &handle, &length)) return NULL;
    u32 remaining = length, hash = 0;
    u8 buf[240];
    int valid = 1;
    while (valid && remaining) {
        u32 count = MIN(remaining, sizeof(buf));
        if (fread(buf, 1, count, handle) != count) { valid = 0; break; }
        hash = rdx_factory_audio_crc(hash, buf, count);
        remaining -= count;
        wdt_clear();
        os_time_dly(1);
    }
    if (!valid || fseek(handle, 0, SEEK_SET)) { fclose(handle); return NULL; }
    *size = length; *crc = hash;
    return handle;
}
#endif
