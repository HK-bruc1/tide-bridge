#include "app_config.h"
#include "system/includes.h"
#include "fs/fs.h"
#include "cJSON.h"
#include "rdx_record_format.h"
#include "rdx_record.h"
#include "rdx_protocol.h"
#include "rdx_app.h"
#include "dev_manager.h"
#include "rdx_util.h"

/* RAW and the App's DAT schema are unchanged. MTA is firmware-only metadata.
 * A durable first-frame fingerprint binds it to the RAW, including after a
 * filename is reused. Only boot code edits DAT, before any RDX worker exists.
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
static char rf_deleted_name[12];

int rdx_record_format_service_status(void)
{
    return rf_service;
}

void rdx_record_format_service_ready(int result)
{
    rf_service = result < 0 ? -1 : result > 0 ? 1 : 0;
    printf("[REC_FORMAT] file service=%d\n", rf_service);
}
/* Boot is serialized before RDX initialization; cJSON owns parsed strings. */
static char rf_json[RF_OBJECT_SIZE];
static struct {
    u8 data[256];
    u32 next, size, remaining;
    int error;
} rf_reader;

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
static int rf_getc(FILE *f)
{
    if (rf_reader.error) return -1;
    if (rf_reader.next == rf_reader.size) {
        u32 n = rf_reader.remaining;
        if (!n) return -1;
        if (n > sizeof(rf_reader.data)) n = sizeof(rf_reader.data);
        if (fread(rf_reader.data, 1, n, f) != n) {
            rf_reader.error = 1;
            return -1;
        }
        rf_reader.remaining -= n;
        rf_reader.size = n;
        rf_reader.next = 0;
    }
    return rf_reader.data[rf_reader.next++];
}

static int rf_nonspace(FILE *f)
{
    int c;
    do { c = rf_getc(f); } while (c == ' ' || c == '\r' || c == '\n' || c == '\t');
    return c;
}

static int rf_object(FILE *f, char *buf)
{
    int depth = 1, quoted = 0, escape = 0;
    u32 n = 1;
    buf[0] = '{';
    while (depth) {
        int c = rf_getc(f);
        if (c < 0 || n >= RF_OBJECT_SIZE - 1) return -1;
        buf[n++] = c;
        if (quoted) {
            if (escape) escape = 0;
            else if (c == '\\') escape = 1;
            else if (c == '"') quoted = 0;
        } else if (c == '"') quoted = 1;
        else if (c == '{' || c == '[') { if (++depth > 8) return -1; }
        else if (c == '}' || c == ']') --depth;
    }
    buf[n] = 0;
    return 0;
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
        for (u32 j = 0; j < total; ++j) {
            if (entries[j].found && entries[j].meta.sn == e->meta.sn) {
                e->meta.sn = (strstr(e->meta.name, ".mta") ? 0x12000000u : 0x10000000u) +
                             strtoul(e->meta.name, NULL, 16);
                if (rf_number(obj, "sn", e->meta.sn)) return -1;
                *changed = 1;
                break;
            }
        }
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
static int rf_merge(rf_entry *entries, u32 total, int persist)
{
    for (u32 i = 0; i < total; ++i) entries[i].found = 0;
    FILE *src = fopen(RF_DAT, "r");
    FILE *dst = persist ? fopen(RF_TMP, "w+") : NULL;
    if (persist && !dst) { if (src) fclose(src); return -1; }
    int changed = !src;
    int ok = !persist || fwrite("[", 1, 1, dst) == 1;
    u32 count = 0;
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
            } else if (!obj || rf_fix_object(obj, entries, total, &changed) || rf_emit(dst, obj, &count)) ok = 0;
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
        if (!persist) {
            if (count >= RF_MAX_FILES) ok = 0;
            else ++count;
            continue;
        }
        cJSON *obj = rf_restore_object(&entries[i]);
        if (!obj || rf_emit(dst, obj, &count)) ok = 0;
        cJSON_Delete(obj);
    }
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
    info->orig_pack_num = (size + 459) / 460;
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
                rf_entry *grown = realloc(*out, next * sizeof(e));
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
