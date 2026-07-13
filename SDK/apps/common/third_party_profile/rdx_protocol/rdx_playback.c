#include "rdx_playback.h"

#if TCFG_RDX_LOCAL_PLAYBACK_ENABLE

#include "system/includes.h"
#include "rdx_app.h"
#include "rdx_record.h"
#include "rdx_uxfile.h"
#include "dev_flow_player.h"
#include "fs/fs.h"
#include "os/os_api.h"
#include "app_msg.h"
#include "node_uuid.h"
#include "source_dev0.h"

#define LOG_TAG                                             "[PB]"
#define LOG_ERROR_ENABLE
#define LOG_INFO_ENABLE
#define LOG_CLI_ENABLE
#include "debug.h"

#define PB_LOG(fmt, ...)  log_info( fmt, ##__VA_ARGS__)

// SD 卡通过 dev_manager 挂载后的录音文件根目录
#define PB_STORAGE_ROOT         "storage/sd0/C/"
#define PB_OPUS_CHANNELS        (2u)
#define PB_OPUS_FRAME_BYTES     (80u)
#define PB_PUMP_INTERVAL_MS     (10u)
#define PB_DRAIN_TIMEOUT_MS     (3000u)                     // 3 s 排空看门狗
#define PB_CHUNK_BYTES          (560u)                      // fread block, 7 stereo frames ~= 140 ms

// 来自 uxfile 模块的外部函数，掉电保护用
extern u8 rdx_is_file_transfer_active(void);
extern u8 rdx_is_file_sync_busy(void);

static rdx_playback_t pb = { 0 };
static u32 pb_max_sn = 0;
static FILE *pb_file = NULL;
static u16 pb_pump_timer = 0;
static u8 pb_stream_buf[PB_CHUNK_BYTES];
static u16 pb_pending_len = 0;
static u16 pb_pending_off = 0;
static u8 pb_eof = 0;
static u16 pb_drain_ticks = 0;
static u32 pb_total_read = 0;
static u32 pb_total_written = 0;

// ---- 纯读取（不触发初始化，不修改状态）----------------------------------

static u32 current_max_sn(void)
{
    return rdx_uxfile_get_the_largest_fileSn();
}

// ---- 文件路径 --------------------------------------------------------

static bool pb_open_file(const char *fname, FILE **out_f)
{
    *out_f = NULL;

    const char *name = (fname[0] == '/') ? fname + 1 : fname;

    char path[RECORD_FILE_NAME_SIZE + 32];
    snprintf(path, sizeof(path), PB_STORAGE_ROOT "%s", name);

    FILE *f = fopen(path, "r");
    if (f && flen(f) > 0) {
        PB_LOG("open ok: %s, flen=%d", path, flen(f));
        *out_f = f;
        return true;
    }
    if (f) { fclose(f); f = NULL; }
    PB_LOG("open fail: %s", path);
    return false;
}

// ---- 数据就绪保证 ----------------------------------------------------

// 确保 pb_max_sn / pb.total_count 反映 DAT 缓存最新状态。
// 返回 true 表示已有可用文件列表，false 表示数据未就绪（同步未完成 / 缓存空 / SD 未挂载）。
static bool pb_cache_ready(void)
{
    // 同步仍在进行 → 数据不可用
    if (rdx_uxfile_is_sync_in_progress()) {
        PB_LOG("blocked: sync in progress");
        return false;
    }

    u16 cnt = rdx_uxfile_get_dat_cout();
    u32 max_sn = current_max_sn();

    // 缓存为空 → 数据未就绪（SD 可能未挂载 / DAT 损坏 / 尚无录音）
    if (cnt == 0 || max_sn == 0) {
        PB_LOG("no files in cache (cnt=%u, max_sn=%u)", cnt, max_sn);
        return false;
    }

    // 首次初始化或数据有变化 → 刷新本地缓存
    if (pb.total_count == 0 || cnt != pb.total_count || max_sn != pb_max_sn) {
        pb_max_sn = max_sn;
        pb.total_count = cnt;
        PB_LOG("refresh: total=%u files, max_sn=%u", cnt, pb_max_sn);
    }

    return true;
}

// ---- Source_Dev0 数据泵 ------------------------------------------------

static void pb_reset_stream_state(void)
{
    pb_pending_len = 0;
    pb_pending_off = 0;
    pb_eof = 0;
    pb_drain_ticks = 0;
    pb_total_read = 0;
    pb_total_written = 0;
}

static void pb_cancel_pump_timer(void)
{
    if (pb_pump_timer) {
        sys_timer_del(pb_pump_timer);
        pb_pump_timer = 0;
    }
}

static bool pb_schedule_pump(u32 delay_ms);

// ---- 播放器生命周期 --------------------------------------------------

static void cleanup_playback(void)
{
    if (!pb_file && !dev_flow_player_runing()) return;

    pb_cancel_pump_timer();

    pb.intent = PB_INTENT_STOP;
    if (dev_flow_player_runing()) {
        dev_flow_player_close();
    }
    pb.status = PB_STATUS_STOP;
    pb.intent = PB_INTENT_NONE;

    if (pb_file) {
        fclose(pb_file);
        pb_file = NULL;
    }
    pb_reset_stream_state();
}

static u32 pb_write_pending(void)
{
    if (pb_pending_off >= pb_pending_len) {
        pb_pending_len = 0;
        pb_pending_off = 0;
        return 0;
    }

    u16 remain = pb_pending_len - pb_pending_off;
    u32 free_space = source_dev0_get_free_space();
    if (free_space < PB_OPUS_FRAME_BYTES) {
        return 0;
    }

    u16 write_len = remain;
    if (free_space < write_len) {
        write_len = (u16)free_space;
    }
    write_len = (write_len / PB_OPUS_FRAME_BYTES) * PB_OPUS_FRAME_BYTES;
    if (write_len == 0) {
        return 0;
    }

    u32 written = source_dev0_input_write(pb_stream_buf + pb_pending_off, write_len);
    if (written > write_len) {
        written = write_len;
    }

    pb_pending_off += written;
    pb_total_written += written;

    if (pb_pending_off >= pb_pending_len) {
        pb_pending_len = 0;
        pb_pending_off = 0;
    }

    return written;
}

static bool pb_load_next_chunk(u16 max_len)
{
    if (!pb_file || pb_eof || pb_pending_len) {
        return false;
    }

    u16 read_len = (max_len / PB_OPUS_FRAME_BYTES) * PB_OPUS_FRAME_BYTES;
    if (read_len == 0) {
        return false;
    }
    if (read_len > sizeof(pb_stream_buf)) {
        read_len = sizeof(pb_stream_buf);
    }

    int rlen = fread(pb_stream_buf, read_len, 1, pb_file);
    if (rlen <= 0) {
        pb_eof = 1;
        return false;
    }

    u16 aligned_len = ((u16)rlen / PB_OPUS_FRAME_BYTES) * PB_OPUS_FRAME_BYTES;
    if (aligned_len == 0) {
        PB_LOG("drop short tail: %d bytes", rlen);
        pb_eof = 1;
        return false;
    }
    if (aligned_len < (u16)rlen) {
        PB_LOG("drop unaligned tail: %d -> %u bytes", rlen, aligned_len);
        pb_eof = 1;
    }

    pb_pending_len = aligned_len;
    pb_pending_off = 0;
    pb_total_read += aligned_len;
    return true;
}

static bool pb_pump_fill(void)
{
    while (pb_file && pb.status == PB_STATUS_PLAYING) {
        if (pb_pending_len) {
            u32 written = pb_write_pending();
            if (written == 0) {
                return false;               // cbuf 满, 下个 tick 再试
            }
            continue;
        }

        if (pb_eof) {
            return false;                   // 文件已读完
        }

        if (!pb_load_next_chunk(PB_CHUNK_BYTES)) {
            return false;                   // fread 失败 / EOF
        }
    }

    return false;
}

static void pb_finish_natural(void)
{
    if (!pb_file || pb.status != PB_STATUS_PLAYING) {
        return;
    }

    bool auto_next = (pb.intent != PB_INTENT_STOP && pb.intent != PB_INTENT_SWITCH);
    PB_LOG("eof: SN=%u, read=%u, written=%u", pb.cur_sn, pb_total_read, pb_total_written);
    cleanup_playback();

    if (auto_next) {
        PB_LOG("auto next");
        rdx_playback_next();
    }
}

static void pb_pump_timer_cb(void *priv)
{
    pb_pump_timer = 0;

    if (!pb_file || pb.status != PB_STATUS_PLAYING) {
        return;
    }

    if (!pb_eof || pb_pending_len) {
        pb_pump_fill();
    }

    if (pb_eof && pb_pending_len == 0) {
        if (source_dev0_is_empty()) {
            PB_LOG("drain complete: cbuf empty");
            pb_finish_natural();
            return;
        }
        if (++pb_drain_ticks > (PB_DRAIN_TIMEOUT_MS / PB_PUMP_INTERVAL_MS)) {
            PB_LOG("drain watchdog: force finish");
            pb_finish_natural();
            return;
        }
    } else {
        pb_drain_ticks = 0;
    }

    pb_schedule_pump(PB_PUMP_INTERVAL_MS);
}

static bool pb_schedule_pump(u32 delay_ms)
{
    if (pb_pump_timer || !pb_file || pb.status != PB_STATUS_PLAYING) {
        return true;
    }

    pb_pump_timer = sys_timeout_add_2_task(NULL, pb_pump_timer_cb, delay_ms, "app_core");
    if (!pb_pump_timer) {
        PB_LOG("error: schedule pump failed");
        return false;
    }
    return true;
}

// ---- 播放互斥 --------------------------------------------------------

bool rdx_playback_can_start(void)
{
    RecordStatus *rp = rdx_record_get_status();
    if (rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME) {
        PB_LOG("blocked: recording");
        return false;
    }
    if (rdx_uxfile_is_datFileInfo_loading()) {
        PB_LOG("blocked: dat file info loading");
        return false;
    }
    if (rdx_is_file_transfer_active()) {
        PB_LOG("blocked: file transfer active");
        return false;
    }
    if (rdx_is_file_sync_busy()) {
        PB_LOG("blocked: file sync busy");
        return false;
    }
    return true;
}

void rdx_playback_stop(void)
{
    if (pb_file || dev_flow_player_runing()) {
        cleanup_playback();
        PB_LOG("stop");
    }
}

// ---- 初始化 ----------------------------------------------------------

void rdx_playback_init(void)
{
    memset(&pb, 0, sizeof(pb));
    pb_file = NULL;
    pb_max_sn = 0;
    pb_pump_timer = 0;
    pb_reset_stream_state();
    // 不读 DAT —— init 时同步可能尚未完成。
    // pb_cache_ready() 在首次 prev/next 时做真正的懒初始化。
}

// ---- 播放单个文件 ----------------------------------------------------

// 返回 false 表示文件打不开，调用者应尝试下一个 SN
static bool rdx_playback_play_file(uxfile_data_t *fi)
{
    if (!fi) return false;

    const char *fname = fi->filename;
    if (fname[0] == '\0') {
        PB_LOG("skip: empty filename for SN=%u", fi->sn);
        return false;
    }

    FILE *f = NULL;
    if (!pb_open_file(fname, &f)) {
        PB_LOG("skip: fopen failed for '%s'", fname);
        return false;
    }

    cleanup_playback();

    pb_file = f;
    pb.cur_sn = fi->sn;
    pb.status = PB_STATUS_PLAYING;
    pb.intent = PB_INTENT_NONE;

    int err = dev_flow_player_open(PB_OPUS_CHANNELS, NODE_UUID_SOURCE_DEV0);
    if (err) {
        PB_LOG("error: dev_flow_player_open failed, err=%d, SN=%u", err, fi->sn);
        cleanup_playback();
        return false;
    }

    pb_pump_fill();
    if (!pb_schedule_pump(PB_PUMP_INTERVAL_MS)) {
        cleanup_playback();
        return false;
    }

    PB_LOG("play: SN=%u, flen=%d", fi->sn, flen(pb_file));
    return true;
}

// ---- 导航：上一个 ----------------------------------------------------

void rdx_playback_prev(void)
{
    if (pb_file && pb.status == PB_STATUS_STOP) {
        cleanup_playback();
    }

    if (!rdx_playback_can_start()) return;

    if (!pb_cache_ready()) {
        PB_LOG("prev: data not ready");
        return;
    }

    u32 start = (pb.cur_sn == 0) ? (pb_max_sn + 1) : pb.cur_sn;

    u32 sn;
    for (sn = start - 1; sn >= 1; sn--) {
        uxfile_data_t *fi = rdx_uxfile_get_file_data_by_sn(sn, 0, 0);
        if (!fi || fi->filename[0] == '\0') continue;
        if (rdx_playback_play_file(fi)) return;
    }

    if (start <= pb_max_sn) {
        for (sn = pb_max_sn; sn >= start; sn--) {
            uxfile_data_t *fi = rdx_uxfile_get_file_data_by_sn(sn, 0, 0);
            if (!fi || fi->filename[0] == '\0') continue;
            if (rdx_playback_play_file(fi)) return;
        }
    }

    PB_LOG("prev: no playable file found");
}

// ---- 导航：下一个 ----------------------------------------------------

void rdx_playback_next(void)
{
    if (pb_file && pb.status == PB_STATUS_STOP) {
        cleanup_playback();
    }

    if (!rdx_playback_can_start()) return;

    if (!pb_cache_ready()) {
        PB_LOG("next: data not ready");
        return;
    }

    // 首次播放（cur_sn == 0）：从最大 SN（最新录音）开始，向下扫描
    if (pb.cur_sn == 0) {
        u32 sn;
        for (sn = pb_max_sn; sn >= 1; sn--) {
            uxfile_data_t *fi = rdx_uxfile_get_file_data_by_sn(sn, 0, 0);
            if (!fi || fi->filename[0] == '\0') continue;
            if (rdx_playback_play_file(fi)) return;
        }
        PB_LOG("next: no playable file found");
        return;
    }

    // 播放中：往 SN 更大的方向找（越新），到头后 wraparound
    u32 start = pb.cur_sn;

    u32 sn;
    for (sn = start + 1; sn <= pb_max_sn; sn++) {
        uxfile_data_t *fi = rdx_uxfile_get_file_data_by_sn(sn, 0, 0);
        if (!fi || fi->filename[0] == '\0') continue;
        if (rdx_playback_play_file(fi)) return;
    }

    for (sn = 1; sn < start; sn++) {
        uxfile_data_t *fi = rdx_uxfile_get_file_data_by_sn(sn, 0, 0);
        if (!fi || fi->filename[0] == '\0') continue;
        if (rdx_playback_play_file(fi)) return;
    }

    PB_LOG("next: no playable file found");
}

// ---- 快进快退 --------------------------------------------------------

void rdx_playback_ff(void)
{
    PB_LOG("ff: not implemented");
}

void rdx_playback_fr(void)
{
    PB_LOG("fr: not implemented");
}

#endif
