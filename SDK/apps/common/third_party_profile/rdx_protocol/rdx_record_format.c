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
#define RF_MAGIC 0x31464d52u
#define RF_MAX_FILES 300
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
} rf_entry;

static rf_meta rf_current;
static volatile int rf_error;
static u8 rf_boot_started;
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
    printf("[REC_FORMAT] storage format error line=%u; admission closed\n", line);
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
    if (rf_error || !file || !file->sn || !rf_frame_bytes(format) ||
        !rf_name_valid(file->filename)) {
        return rf_fail();
    }
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
        else if (c == '{') ++depth;
        else if (c == '}') --depth;
    }
    buf[n] = 0;
    return 0;
}

static int rf_emit(FILE *dst, cJSON *obj, u32 *count)
{
    char *buf = rf_json;
    if (*count >= RF_MAX_FILES ||
        !cJSON_PrintPreallocated(obj, buf, RF_OBJECT_SIZE, 0)) return -1;
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

static int rf_fix_object(cJSON *obj, rf_entry *entries, u32 total, int *changed)
{
    cJSON *name = cJSON_GetObjectItemCaseSensitive(obj, "name");
    cJSON *sn = cJSON_GetObjectItemCaseSensitive(obj, "sn");
    if (!cJSON_IsString(name) || !cJSON_IsNumber(sn)) return -1;
    for (u32 i = 0; i < total; ++i) {
        rf_entry *e = &entries[i];
        if (strcmp(name->valuestring, e->meta.name)) {
            if (sn->valuedouble == e->meta.sn) return -1;
            continue;
        }
        if (e->found || sn->valuedouble != e->meta.sn) return -1;
        e->found = 1;
        cJSON *frame = cJSON_GetObjectItemCaseSensitive(obj, "frame_size");
        cJSON *opus = cJSON_GetObjectItemCaseSensitive(obj, "opus");
        if (!cJSON_IsNumber(frame) ||
            frame->valuedouble != rf_frame_bytes(e->meta.format) ||
            !cJSON_IsNumber(opus) ||
            opus->valuedouble != rdx_protocol_calc_opus_format(e->meta.format)) {
            *changed = 1;
        }
        if (rf_number(obj, "frame_size", rf_frame_bytes(e->meta.format)) ||
            rf_number(obj, "opus", rdx_protocol_calc_opus_format(e->meta.format))) return -1;
    }
    return 0;
}

static cJSON *rf_restore_object(const rf_entry *e)
{
    char path[40], crc_text[16];
    u8 buf[512];
    u32 crc = 0, remaining = e->size;
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
        rf_number(obj, "end_time", e->meta.start + e->size / rf_frame_bytes(e->meta.format) / 50) ||
        rf_number(obj, "frame_size", rf_frame_bytes(e->meta.format)) ||
        rf_number(obj, "opus", rdx_protocol_calc_opus_format(e->meta.format))) {
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
            if (!obj || rf_fix_object(obj, entries, total, &changed) || rf_emit(dst, obj, &count)) ok = 0;
            cJSON_Delete(obj);
            c = rf_nonspace(src);
            if (c != ',') break;
            c = rf_nonspace(src);
            if (c != '{') ok = 0;
            wdt_clear();
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

int rdx_record_format_boot(void)
{
    /* Repeated app task initialization must never edit a live library cache. */
    if (rf_boot_started) return rf_error;
    rf_boot_started = 1;
    rdx_app_emmc_poweron(0);
    sd_set_power_user(1);
    if (!dev_manager_list_check_by_logo("sd0") && dev_manager_add("sd0")) return rf_fail();
    if (rf_replay()) return rf_fail();
    struct vfscan *scan = fscan(RF_ROOT, "-tMTA -sn", 1);
    if (!scan) return rf_fail();
    u32 total = 0;
    int ok = 1;
    rf_entry *entries = NULL;
    if (scan->file_number) entries = zalloc(sizeof(*entries) * RF_MAX_FILES);
    if (scan->file_number && !entries) ok = 0;
    for (u32 i = 1; ok && i <= scan->file_number; ++i) {
        FILE *f = fselect(scan, FSEL_BY_NUMBER, i);
        rf_meta meta;
        if (!f) { ok = 0; break; }
        char name[12] = {0};
        int name_len = fget_name(f, (u8 *)name, sizeof(name) - 1);
        int valid = flen(f) == sizeof(meta) && fread(&meta, 1, sizeof(meta), f) == sizeof(meta);
        if (fclose(f)) valid = 0;
        if (name_len < 7 || name_len > 10) { ok = 0; break; }
        for (u32 j = 0; j < sizeof(name) && name[j]; ++j) {
            if (name[j] >= 'A' && name[j] <= 'Z') name[j] += 'a' - 'A';
        }
        if (strcmp(name + name_len - 4, ".mta")) { ok = 0; break; }
        strcpy(name + name_len - 3, "raw");
        if (!rf_name_valid(name)) { ok = 0; break; }
        char path[40];
        rf_path(path, name, 0);
        f = fopen(path, "r");
        /* Metadata is durable BEFORE RAW. A cut during its write may leave
         * an incomplete orphan: it cannot describe any saved audio. */
        if (!f) continue;
        if (!valid || meta.magic != RF_MAGIC || !meta.sn || meta.ready != 1 ||
            meta.name[sizeof(meta.name) - 1] || !rf_name_valid(meta.name) ||
            strcmp(meta.name, name) ||
            !rf_frame_bytes(meta.format) ||
            meta.hash != rf_hash(&meta, sizeof(meta) - sizeof(meta.hash))) {
            fclose(f); ok = 0; break;
        }
        u32 size = flen(f), frame = rf_frame_bytes(meta.format);
        u8 first[80];
        valid = size >= frame && fread(first, 1, frame, f) == frame &&
                rf_hash(first, frame) == meta.first_hash;
        if (fclose(f)) { ok = 0; break; }
        if (!valid) { ok = 0; break; }
        if (size % frame || total == RF_MAX_FILES) { ok = 0; break; }
        for (u32 j = 0; j < total; ++j) {
            if (entries[j].meta.sn == meta.sn || !strcmp(entries[j].meta.name, meta.name)) ok = 0;
        }
        if (!ok) break;
        entries[total].meta = meta;
        entries[total++].size = size;
        wdt_clear();
    }
    fscan_release(scan);
    if (ok && total) {
        int changed = rf_merge(entries, total, 0);
        if (changed < 0 || (changed && rf_merge(entries, total, 1))) ok = 0;
    }
    free(entries);
    if (!ok) return rf_fail();
    printf("[REC_FORMAT] boot reconciled files=%u\n", total);
    return 0;
}
