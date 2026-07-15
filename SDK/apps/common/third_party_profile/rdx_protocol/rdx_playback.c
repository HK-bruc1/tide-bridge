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

// 板载 SD NAND 通过 dev_manager 挂载后的录音文件根目录
#define PB_STORAGE_ROOT         "storage/sd0/C/"
#define PB_OPUS_CHANNELS        (2u)
#define PB_OPUS_FRAME_BYTES     (80u)
#define PB_OPUS_FRAME_MS        (20u)
#define PB_SEEK_STEP_MS         (5000u)
#define PB_SEEK_STEP_FRAMES     (PB_SEEK_STEP_MS / PB_OPUS_FRAME_MS)
#define PB_RESUME_REWIND_MS      (100u)
#define PB_RESUME_REWIND_FRAMES  (PB_RESUME_REWIND_MS / PB_OPUS_FRAME_MS)
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

typedef enum {
    PB_DIRECTION_OLDER = -1,
    PB_DIRECTION_NEWER = 1,
} pb_direction_t;

typedef enum {
    PB_CANDIDATE_SKIP = 0,
    PB_CANDIDATE_STARTED,
    PB_CANDIDATE_FATAL,
} pb_candidate_result_t;

// ---- 纯读取（不触发初始化，不修改状态）----------------------------------

static u32 current_max_sn(void)
{
    return rdx_uxfile_get_the_largest_fileSn();
}

static bool pb_has_active_track(void)
{
    return pb_file && dev_flow_player_runing() && pb.current_sn != 0;
}

static bool pb_state_is_transition(pb_state_t state)
{
    return state == PB_STATE_STARTING ||
           state == PB_STATE_SWITCHING;
}

static bool pb_state_allows_pump(pb_state_t state)
{
    return state == PB_STATE_STARTING ||
           state == PB_STATE_SWITCHING ||
           state == PB_STATE_PLAYING;
}

static void pb_clear_resume_cursor(void)
{
    pb.resume_sn = 0;
    pb.resume_frame = 0;
    pb.resume_duration_frames = 0;
}

// ---- 文件路径 --------------------------------------------------------

static bool pb_open_file(const char *fname, FILE **out_f)
{
    *out_f = NULL;

    const char *name = (fname[0] == '/') ? fname + 1 : fname;

    char path[RECORD_FILE_NAME_SIZE + 32];
    snprintf(path, sizeof(path), PB_STORAGE_ROOT "%s", name);

    FILE *f = fopen(path, "r");
    if (f && flen(f) >= PB_OPUS_FRAME_BYTES) {
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
// 返回 true 表示已有可用文件列表，false 表示数据未就绪。
static bool pb_cache_ready(void)
{
    if (rdx_uxfile_is_sync_in_progress()) {
        PB_LOG("blocked: sync in progress");
        if (!pb_has_active_track() && pb.state != PB_STATE_PAUSED) {
            pb.state = PB_STATE_UNREADY;
        }
        return false;
    }

    u16 cnt = rdx_uxfile_get_dat_cout();
    u32 max_sn = current_max_sn();

    if (cnt == 0 || max_sn == 0) {
        PB_LOG("no files in cache (cnt=%u, max_sn=%u)", cnt, max_sn);
        pb.total_count = 0;
        pb_max_sn = 0;
        pb.playlist_dirty = 0;
        if (!pb_has_active_track()) {
            pb.selected_sn = 0;
            pb_clear_resume_cursor();
            pb.state = PB_STATE_UNREADY;
        }
        return false;
    }

    if (pb.playlist_dirty || pb.total_count == 0 ||
        cnt != pb.total_count || max_sn != pb_max_sn) {
        pb_max_sn = max_sn;
        pb.total_count = cnt;
        pb.playlist_dirty = 0;
        PB_LOG("refresh: total=%u files, max_sn=%u", cnt, pb_max_sn);
    }

    if (pb.state == PB_STATE_UNREADY) {
        pb.state = PB_STATE_STOPPED;
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

static void pb_close_stream(void)
{
    pb_cancel_pump_timer();
    if (dev_flow_player_runing()) {
        dev_flow_player_close();
    }
    source_dev0_reset_consumed_bytes();
    pb_reset_stream_state();
}

static void pb_close_track(void)
{
    pb_close_stream();
    if (pb_file) {
        fclose(pb_file);
        pb_file = NULL;
    }

    pb.current_sn = 0;
    pb.seek_base_frame = 0;
    pb.duration_frames = 0;
}

static void pb_finish_stop(bool clear_selection)
{
    pb.intent = PB_INTENT_STOP;
    pb_close_track();
    pb.pending_sn = 0;
    pb_clear_resume_cursor();
    if (clear_selection) {
        pb.selected_sn = 0;
    }
    pb.state = pb.total_count ? PB_STATE_STOPPED : PB_STATE_UNREADY;
    pb.intent = PB_INTENT_NONE;
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
    while (pb_file && pb_state_allows_pump(pb.state)) {
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
    if (!pb_file ||
        (pb.state != PB_STATE_PLAYING && pb.state != PB_STATE_DRAINING)) {
        return;
    }

    PB_LOG("eof: SN=%u, read=%u, written=%u",
           pb.current_sn, pb_total_read, pb_total_written);
    pb_finish_stop(false);
}

static void pb_pump_timer_cb(void *priv)
{
    pb_pump_timer = 0;

    if (!pb_file ||
        (pb.state != PB_STATE_PLAYING && pb.state != PB_STATE_DRAINING)) {
        return;
    }

    if (!pb_eof || pb_pending_len) {
        pb_pump_fill();
    }

    if (pb_eof && pb_pending_len == 0) {
        pb.state = PB_STATE_DRAINING;
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
    if (pb_pump_timer || !pb_file ||
        (!pb_state_allows_pump(pb.state) && pb.state != PB_STATE_DRAINING)) {
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
    bool was_active = pb_file || dev_flow_player_runing();

    pb_finish_stop(false);
    pb.last_error = PB_RESULT_OK;
    if (was_active) {
        PB_LOG("stop: selected=%u", pb.selected_sn);
    }
}

// ---- 初始化 ----------------------------------------------------------

void rdx_playback_init(void)
{
    memset(&pb, 0, sizeof(pb));
    pb.state = PB_STATE_UNREADY;
    pb.playlist_dirty = 1;
    pb.last_error = PB_RESULT_OK;
    pb_file = NULL;
    pb_max_sn = 0;
    pb_pump_timer = 0;
    pb_reset_stream_state();
    // init 时同步可能尚未完成，首次播放命令再做懒加载。
}

// ---- 播放单个文件 ----------------------------------------------------

static void pb_restore_stable_state(pb_state_t previous_state)
{
    pb.pending_sn = 0;
    pb.intent = PB_INTENT_NONE;

    if (previous_state == PB_STATE_PAUSED &&
        pb.resume_sn != 0 && pb.resume_sn == pb.selected_sn) {
        pb.state = PB_STATE_PAUSED;
        return;
    }

    if (pb_has_active_track()) {
        pb.state = (previous_state == PB_STATE_DRAINING) ?
                   PB_STATE_DRAINING : PB_STATE_PLAYING;
        return;
    }

    pb.state = pb.total_count ? PB_STATE_STOPPED : PB_STATE_UNREADY;
}

static u32 pb_current_frame(void)
{
    u32 consumed_frames = source_dev0_get_consumed_bytes() / PB_OPUS_FRAME_BYTES;
    u32 frame = pb.seek_base_frame + consumed_frames;

    if (pb.duration_frames && frame > pb.duration_frames) {
        frame = pb.duration_frames;
    }

    return frame;
}

static int pb_open_stream_at_frame(u32 base_frame, pb_state_t transition_state)
{
    pb.state = transition_state;
    pb.seek_base_frame = base_frame;
    source_dev0_reset_consumed_bytes();

    int err = dev_flow_player_open(PB_OPUS_CHANNELS, NODE_UUID_SOURCE_DEV0);
    if (err) {
        PB_LOG("error: dev_flow_player_open failed, err=%d, SN=%u",
               err, pb.current_sn ? pb.current_sn : pb.pending_sn);
        pb_close_stream();
        return PB_RESULT_PLAYER_ERROR;
    }

    pb_pump_fill();
    if (!pb_schedule_pump(PB_PUMP_INTERVAL_MS)) {
        pb_close_stream();
        return PB_RESULT_PLAYER_ERROR;
    }

    pb.state = PB_STATE_PLAYING;
    return PB_RESULT_OK;
}

static pb_candidate_result_t pb_start_candidate_at_frame(uxfile_data_t *fi,
                                                          u32 base_frame,
                                                          pb_state_t transition_state,
                                                          pb_intent_t intent)
{
    if (!fi) {
        return PB_CANDIDATE_SKIP;
    }

    const char *fname = fi->filename;
    if (fname[0] == '\0') {
        PB_LOG("skip: empty filename for SN=%u", fi->sn);
        return PB_CANDIDATE_SKIP;
    }

    FILE *candidate_file = NULL;
    if (!pb_open_file(fname, &candidate_file)) {
        PB_LOG("skip: fopen failed for SN=%u, file='%s'", fi->sn, fname);
        return PB_CANDIDATE_SKIP;
    }

    u32 candidate_sn = fi->sn;
    pb.pending_sn = candidate_sn;
    pb.state = transition_state;
    pb.intent = intent;

    // 候选文件已经成功打开后，才释放旧播放资源。
    pb_close_track();
    pb_file = candidate_file;
    pb.duration_frames = (u32)flen(pb_file) / PB_OPUS_FRAME_BYTES;
    if (base_frame >= pb.duration_frames) {
        base_frame = pb.duration_frames - 1;
    }

    int ret = pb_open_stream_at_frame(base_frame, transition_state);
    if (ret != PB_RESULT_OK) {
        pb_close_track();
        pb.pending_sn = 0;
        pb.intent = PB_INTENT_NONE;
        pb.state = pb.total_count ? PB_STATE_STOPPED : PB_STATE_UNREADY;
        pb.last_error = ret;
        return PB_CANDIDATE_FATAL;
    }

    // 文件、播放流和数据泵都成功后，才提交稳定选择。
    pb.selected_sn = candidate_sn;
    pb.current_sn = candidate_sn;
    pb_clear_resume_cursor();
    pb.pending_sn = 0;
    pb.intent = PB_INTENT_NONE;
    pb.last_error = PB_RESULT_OK;

    PB_LOG("play: SN=%u, flen=%d", candidate_sn, flen(pb_file));
    return PB_CANDIDATE_STARTED;
}

static pb_candidate_result_t pb_start_candidate(uxfile_data_t *fi,
                                                 pb_state_t transition_state)
{
    return pb_start_candidate_at_frame(fi, 0, transition_state,
                                       PB_INTENT_SWITCH);
}

// ---- 环形导航 --------------------------------------------------------

static u32 pb_step_probe_sn(u32 sn, pb_direction_t direction)
{
    if (pb_max_sn == 0) {
        return 0;
    }

    if (direction == PB_DIRECTION_NEWER) {
        return (sn == 0 || sn >= pb_max_sn) ? 1 : sn + 1;
    }

    return (sn <= 1 || sn > pb_max_sn) ? pb_max_sn : sn - 1;
}

static int pb_navigate_from(u32 anchor_sn, pb_direction_t direction,
                            u32 excluded_sn, pb_state_t transition_state)
{
    pb_state_t previous_state = pb.state;
    u32 probe_sn = anchor_sn;
    u32 probed_slots = 0;
    u16 candidate_count = 0;

    while (probed_slots < pb_max_sn && candidate_count < pb.total_count) {
        probe_sn = pb_step_probe_sn(probe_sn, direction);
        probed_slots++;

        if (probe_sn == 0 || probe_sn == excluded_sn) {
            continue;
        }

        uxfile_data_t *fi = rdx_uxfile_get_file_data_by_sn(probe_sn, 0, 0);
        if (!fi || fi->filename[0] == '\0') {
            continue;
        }

        candidate_count++;
        pb_candidate_result_t result = pb_start_candidate(fi, transition_state);
        if (result == PB_CANDIDATE_STARTED) {
            return PB_RESULT_OK;
        }
        if (result == PB_CANDIDATE_FATAL) {
            pb_restore_stable_state(previous_state);
            return pb.last_error;
        }
    }

    pb_restore_stable_state(previous_state);
    pb.last_error = PB_RESULT_NO_FILE;
    PB_LOG("navigate: no playable file, dir=%d, anchor=%u, candidates=%u",
           direction, anchor_sn, candidate_count);
    return PB_RESULT_NO_FILE;
}

static int pb_prepare_command(void)
{
    if (pb_state_is_transition(pb.state)) {
        return PB_RESULT_INVALID_STATE;
    }

    if (!rdx_playback_can_start()) {
        return PB_RESULT_BUSY;
    }

    if (!pb_cache_ready()) {
        return PB_RESULT_NOT_READY;
    }

    return PB_RESULT_OK;
}

static int pb_switch_track(pb_direction_t direction)
{
    int ret = pb_prepare_command();
    if (ret != PB_RESULT_OK) {
        pb.last_error = ret;
        return ret;
    }

    pb_state_t transition_state = pb_has_active_track() ?
                                  PB_STATE_SWITCHING : PB_STATE_STARTING;
    if (pb.selected_sn == 0) {
        u32 entry_anchor_sn = (direction == PB_DIRECTION_OLDER) ? 1 : pb_max_sn;
        return pb_navigate_from(entry_anchor_sn, direction, 0, transition_state);
    }

    return pb_navigate_from(pb.selected_sn, direction, 0, transition_state);
}

int rdx_playback_prev(void)
{
    return pb_switch_track(PB_DIRECTION_NEWER);
}

int rdx_playback_next(void)
{
    return pb_switch_track(PB_DIRECTION_OLDER);
}

int rdx_playback_play(void)
{
    if (pb.state == PB_STATE_PLAYING || pb.state == PB_STATE_DRAINING) {
        pb.last_error = PB_RESULT_OK;
        return PB_RESULT_OK;
    }
    if (pb_state_is_transition(pb.state)) {
        pb.last_error = PB_RESULT_BUSY;
        return PB_RESULT_BUSY;
    }

    pb_state_t previous_state = pb.state;
    int ret = pb_prepare_command();
    if (ret != PB_RESULT_OK) {
        pb.last_error = ret;
        return ret;
    }

    if (previous_state == PB_STATE_PAUSED) {
        if (pb.resume_sn == 0 || pb.resume_sn != pb.selected_sn ||
            pb.resume_duration_frames == 0) {
            pb_finish_stop(false);
            pb.last_error = PB_RESULT_INVALID_STATE;
            return PB_RESULT_INVALID_STATE;
        }

        u32 resume_frame = pb.resume_frame;
        if (resume_frame > pb.resume_duration_frames) {
            resume_frame = pb.resume_duration_frames;
        }
        u32 start_frame = (resume_frame > PB_RESUME_REWIND_FRAMES) ?
                          (resume_frame - PB_RESUME_REWIND_FRAMES) : 0;
        uxfile_data_t *fi = rdx_uxfile_get_file_data_by_sn(pb.resume_sn, 0, 0);
        if (!fi || fi->filename[0] == '\0') {
            pb.selected_sn = 0;
            pb_clear_resume_cursor();
            pb.state = pb.total_count ? PB_STATE_STOPPED : PB_STATE_UNREADY;
            pb.playlist_dirty = 1;
            pb.last_error = PB_RESULT_NO_FILE;
            return PB_RESULT_NO_FILE;
        }

        pb_candidate_result_t result = pb_start_candidate_at_frame(
            fi, start_frame, PB_STATE_STARTING, PB_INTENT_PLAY);
        if (result == PB_CANDIDATE_STARTED) {
            return PB_RESULT_OK;
        }

        pb_restore_stable_state(previous_state);
        pb.last_error = (result == PB_CANDIDATE_FATAL) ?
                        pb.last_error : PB_RESULT_IO_ERROR;
        return pb.last_error;
    }

    if (pb.selected_sn == 0) {
        return pb_navigate_from(1, PB_DIRECTION_OLDER, 0, PB_STATE_STARTING);
    }

    uxfile_data_t *fi = rdx_uxfile_get_file_data_by_sn(pb.selected_sn, 0, 0);
    if (!fi || fi->filename[0] == '\0') {
        pb.selected_sn = 0;
        pb.playlist_dirty = 1;
        pb.last_error = PB_RESULT_NO_FILE;
        return PB_RESULT_NO_FILE;
    }

    pb_candidate_result_t result = pb_start_candidate_at_frame(
        fi, 0, PB_STATE_STARTING, PB_INTENT_PLAY);
    if (result == PB_CANDIDATE_STARTED) {
        return PB_RESULT_OK;
    }

    pb_restore_stable_state(previous_state);
    pb.last_error = (result == PB_CANDIDATE_FATAL) ?
                    pb.last_error : PB_RESULT_IO_ERROR;
    return pb.last_error;
}

int rdx_playback_pause(void)
{
    if (pb.state == PB_STATE_PAUSED) {
        pb.last_error = PB_RESULT_OK;
        return PB_RESULT_OK;
    }
    if ((pb.state != PB_STATE_PLAYING && pb.state != PB_STATE_DRAINING) ||
        !pb_has_active_track()) {
        pb.last_error = PB_RESULT_INVALID_STATE;
        return PB_RESULT_INVALID_STATE;
    }
    if (pb.duration_frames == 0) {
        pb_finish_stop(false);
        pb.last_error = PB_RESULT_IO_ERROR;
        return PB_RESULT_IO_ERROR;
    }

    u32 resume_frame = pb_current_frame();
    if (resume_frame > pb.duration_frames) {
        resume_frame = pb.duration_frames;
    }

    pb.intent = PB_INTENT_PAUSE;
    pb.resume_sn = pb.selected_sn;
    pb.resume_frame = resume_frame;
    pb.resume_duration_frames = pb.duration_frames;
    pb_close_track();
    pb.pending_sn = 0;
    pb.state = PB_STATE_PAUSED;
    pb.intent = PB_INTENT_NONE;
    pb.last_error = PB_RESULT_OK;
    PB_LOG("pause: sn=%u, frame=%u", pb.resume_sn, pb.resume_frame);
    return PB_RESULT_OK;
}

void rdx_playback_invalidate_playlist(pb_playlist_invalidate_reason_t reason)
{
    pb.playlist_dirty = 1;

    if (reason == PB_PLAYLIST_STORAGE_UNAVAILABLE ||
        reason == PB_PLAYLIST_FORMATTING) {
        pb.total_count = 0;
        pb_max_sn = 0;
        pb_finish_stop(true);
        pb.state = PB_STATE_UNREADY;
        pb.last_error = PB_RESULT_NOT_READY;
    }
}

void rdx_playback_on_file_deleted(u32 sn)
{
    if (pb.current_sn == sn) {
        rdx_playback_stop();
    }
    if (pb.selected_sn == sn) {
        pb.selected_sn = 0;
        pb_clear_resume_cursor();
    }
    rdx_playback_invalidate_playlist(PB_PLAYLIST_CONTENT_CHANGED);
}

void rdx_playback_get_info(pb_public_info_t *info)
{
    if (!info) {
        return;
    }

    info->selected_sn = pb.selected_sn;
    info->current_sn = pb.current_sn;
    info->pending_sn = pb.pending_sn;
    info->total_count = pb.total_count;
    info->state = pb.state;
    info->last_error = pb.last_error;
    if (pb.state == PB_STATE_PAUSED) {
        info->position_ms = pb.resume_frame * PB_OPUS_FRAME_MS;
        info->duration_ms = pb.resume_duration_frames * PB_OPUS_FRAME_MS;
    } else {
        info->position_ms = pb_current_frame() * PB_OPUS_FRAME_MS;
        info->duration_ms = pb.duration_frames * PB_OPUS_FRAME_MS;
    }
}

// ---- 快进快退 --------------------------------------------------------

static int pb_seek_relative(s32 delta_frames)
{
    if (pb.state != PB_STATE_PLAYING || !pb_has_active_track()) {
        pb.last_error = PB_RESULT_INVALID_STATE;
        return PB_RESULT_INVALID_STATE;
    }

    if (pb.duration_frames == 0) {
        pb.last_error = PB_RESULT_IO_ERROR;
        return PB_RESULT_IO_ERROR;
    }

    if (!rdx_playback_can_start()) {
        pb.last_error = PB_RESULT_BUSY;
        return PB_RESULT_BUSY;
    }

    u32 current_frame = pb_current_frame();
    u32 target_frame;

    if (delta_frames < 0) {
        u32 rewind_frames = (u32)(-delta_frames);
        target_frame = (current_frame > rewind_frames) ?
                       (current_frame - rewind_frames) : 0;
    } else {
        u32 forward_frames = (u32)delta_frames;
        if (current_frame + forward_frames >= pb.duration_frames) {
            PB_LOG("seek end: sn=%u, cur=%u, duration=%u",
                   pb.current_sn, current_frame, pb.duration_frames);
            pb_finish_stop(false);
            pb.last_error = PB_RESULT_OK;
            return PB_RESULT_OK;
        }
        target_frame = current_frame + forward_frames;
    }

    u32 target_offset = target_frame * PB_OPUS_FRAME_BYTES;

    PB_LOG("seek: sn=%u, cur=%u, target=%u, offset=%u",
           pb.current_sn, current_frame, target_frame, target_offset);

    pb_close_stream();

    if (fseek(pb_file, target_offset, SEEK_SET) != 0) {
        PB_LOG("seek error: fseek failed, sn=%u, offset=%u",
               pb.current_sn, target_offset);
        pb_finish_stop(false);
        pb.last_error = PB_RESULT_IO_ERROR;
        return PB_RESULT_IO_ERROR;
    }

    int ret = pb_open_stream_at_frame(target_frame, PB_STATE_STARTING);
    if (ret != PB_RESULT_OK) {
        pb_finish_stop(false);
        pb.last_error = ret;
        return ret;
    }

    pb.last_error = PB_RESULT_OK;
    return PB_RESULT_OK;
}

void rdx_playback_ff(void)
{
    int ret = pb_seek_relative((s32)PB_SEEK_STEP_FRAMES);
    if (ret != PB_RESULT_OK) {
        PB_LOG("ff failed: ret=%d", ret);
    }
}

void rdx_playback_fr(void)
{
    int ret = pb_seek_relative(-((s32)PB_SEEK_STEP_FRAMES));
    if (ret != PB_RESULT_OK) {
        PB_LOG("fr failed: ret=%d", ret);
    }
}

#endif
