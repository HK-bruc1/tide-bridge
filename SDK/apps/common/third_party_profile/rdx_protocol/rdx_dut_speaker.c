#include "system/includes.h"
#include "audio_dac.h"
#include "audio_config.h"
#include "audio_def.h"
#include "audio_general.h"
#include "automute.h"
#include "../../../../audio/common/pcm_data/sine_pcm.h"
#include "rdx_dut_speaker.h"

/* A private DAC channel, never the demo's global/default channel. The worker
 * only writes PCM. DAC control stays on app_core: SDK start/close paths can
 * schedule sys_timeout callbacks on their caller, which must outlive playback. */
static volatile u8 speaker_state;
static volatile u8 speaker_cancel;
static s16 speaker_old_volume;
static u8 speaker_old_mute;
static int speaker_old_rate;
static u8 speaker_channel_created;
static struct audio_dac_channel speaker_channel;
static u8 speaker_pink;
/* Voss-McCartney rows plus a full-rate white term. Integer-only state lives
 * outside the small worker stack and survives PCM block boundaries. */
static struct {
    u32 random, counter;
    s32 sum, previous, highpass;
    s16 rows[16];
} pink;
static union {
    s16 pcm16[480 * 2];
    s32 pcm24[480 * 2];
} speaker_pcm;

static s16 pink_white(void)
{
    u32 x = pink.random;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    pink.random = x;
    return (s16)((int)(x >> 16) - 32768);
}

static void pink_reset(void)
{
    memset(&pink, 0, sizeof(pink));
    pink.random = 0x6d2b79f5u;
    for (int i = 0; i < 16; ++i) {
        pink.rows[i] = pink_white();
        pink.sum += pink.rows[i];
    }
}

static s16 pink_sample(void)
{
    u32 count = ++pink.counter;
    int row = 0;
    while (row < 16 && !(count & 1)) {
        ++row;
        count >>= 1;
    }
    if (row < 16) {
        pink.sum -= pink.rows[row];
        pink.rows[row] = pink_white();
        pink.sum += pink.rows[row];
    }
    s32 sample = (pink.sum + pink_white()) / 17;
    /* Remove DC/subsonic energy (~30 Hz pole at 48 kHz), then cap PCM
     * peaks at half scale independently of the DAC volume table. */
    pink.highpass += sample - pink.previous - pink.highpass / 256;
    pink.previous = sample;
    s32 output = pink.highpass / 2;
    if (output > 16383) output = 16383;
    if (output < -16383) output = -16383;
    return (s16)output;
}

static void speaker_fill(int channels, int wide, int fade)
{
    for (int frame = 0; frame < 480; ++frame) {
        s32 sample = speaker_pink ? pink_sample() : sin1k_48k_16bit[frame % 48];
        if (fade > 0) sample = sample * frame / 479;
        if (fade < 0) sample = sample * (479 - frame) / 479;
        for (int ch = 0; ch < channels; ++ch) {
            int i = frame * channels + ch;
            if (wide) speaker_pcm.pcm24[i] = sample * 256;
            else speaker_pcm.pcm16[i] = sample;
        }
    }
}

static void speaker_worker(void *priv)
{
    int offset = 0;
    int channels = dac_hdl.channel;
    int wide = audio_general_out_dev_bit_width() == DATA_BIT_WIDE_24BIT;
    int bytes = 480 * channels * (wide ? sizeof(s32) : sizeof(s16));
    int fading_out = 0;
    int stop_stalls = 0;
    speaker_fill(channels, wide, speaker_pink ? 1 : 0);
    while (!speaker_cancel || speaker_pink) {
        if (speaker_pink && speaker_cancel && offset == 0 && !fading_out) {
            speaker_fill(channels, wide, -1);
            fading_out = 1;
        }
        int written = audio_dac_channel_write(&speaker_channel,
                          (u8 *)&speaker_pcm + offset, bytes - offset);
        if (written < 0 || written > bytes - offset) {
            break;
        }
        /* A stalled DAC must not prevent DUT exit or power-off. */
        if (speaker_cancel && written == 0 && ++stop_stalls >= 10) break;
        if (written) {
            speaker_state = DUT_AUDIO_PLAYING;
            offset = (offset + written) % bytes;
            if (offset == 0 && speaker_pink) {
                if (fading_out) break;
                if (!speaker_cancel) speaker_fill(channels, wide, 0);
            }
        }
        /* SDK cfifo BLOCK means preserve unwritten data (partial write),
         * not spinning until space exists. Keep phase across partial writes. */
        if (written == 0 || offset != 0) {
            os_time_dly(1);
        }
    }
    speaker_state = DUT_AUDIO_DONE;
    /* app_core deletes the finished worker before the next instance opens. */
    while (1) {
        os_time_dly(100);
    }
}

static void speaker_restore_volume(void)
{
    /* Volume APIs schedule callbacks on the caller's task: keep them on
     * app_core, not the PCM worker which only writes and sleeps. */
    app_audio_set_volume(APP_AUDIO_STATE_MUSIC, speaker_old_volume, 0);
    audio_dac_set_volume(&dac_hdl, speaker_old_volume);
    app_audio_state_exit(APP_AUDIO_STATE_MUSIC);
    app_audio_mute(speaker_old_mute ? AUDIO_MUTE_DEFAULT : AUDIO_UNMUTE_DEFAULT);
}

/* app_core only, after the PCM worker has stopped touching the channel. */
static void speaker_close(void)
{
    if (speaker_channel_created) {
        audio_dac_channel_close(&speaker_channel);
        speaker_channel_created = 0;
    }
    if (speaker_old_rate > 0 && !audio_dac_is_working(&dac_hdl)) {
        audio_dac_set_sample_rate(&dac_hdl, speaker_old_rate);
    }
    speaker_restore_volume();
}

static int speaker_open(u8 use_pink)
{
    if (speaker_state != DUT_AUDIO_IDLE || audio_dac_is_working(&dac_hdl) ||
        app_audio_get_state() != APP_AUDIO_STATE_IDLE ||
        (dac_hdl.channel != 1 && dac_hdl.channel != 2)) {
        return -1;
    }
    memset(&speaker_channel, 0, sizeof(speaker_channel));
    speaker_old_volume = app_audio_get_volume(APP_AUDIO_STATE_MUSIC);
    speaker_old_mute = app_audio_get_dac_digital_mute();
    speaker_old_rate = audio_dac_get_sample_rate(&dac_hdl);
    speaker_channel_created = 0;
    s16 maximum = app_audio_volume_max_query(AppVol_BT_MUSIC);
    /* Half of the configured volume steps, NOT a claim of half acoustic
     * power. Keep the existing factory sine at its original maximum. */
    s16 volume = use_pink ? RDX_DUT_REMINDER_VOLUME(maximum) : maximum;
    app_audio_state_switch(APP_AUDIO_STATE_MUSIC, maximum, NULL);
    app_audio_mute(AUDIO_UNMUTE_DEFAULT);
    app_audio_set_volume(APP_AUDIO_STATE_MUSIC, volume, 0);
    audio_dac_set_volume(&dac_hdl, volume);
    speaker_pink = use_pink;
    if (use_pink) pink_reset();
    speaker_cancel = 0;
    speaker_state = DUT_AUDIO_STARTING;
    struct audio_dac_channel_attr attr = {
        .write_mode = WRITE_MODE_BLOCK, .delay_time = 30, .protect_time = 10,
    };
    if (audio_dac_set_sample_rate(&dac_hdl, 48000) ||
        audio_dac_new_channel(&dac_hdl, &speaker_channel)) {
        goto fail;
    }
    speaker_channel_created = 1;
    if (audio_dac_channel_set_attr(&speaker_channel, &attr) ||
        audio_dac_start(&dac_hdl)) {
        goto fail;
    }
    audio_dac_channel_start(&speaker_channel);
    if (os_task_create(speaker_worker, NULL, 2, 1024, 0, "dut_sine")) {
        goto fail;
    }
    return 0;
fail:
    speaker_close();
    speaker_state = DUT_AUDIO_IDLE;
    return -1;
}

int rdx_dut_speaker_open(void)
{
    return speaker_open(0);
}

int rdx_dut_speaker_open_pink(void)
{
    return speaker_open(1);
}

void rdx_dut_speaker_cancel(void)
{
    speaker_cancel = 1;
}

int rdx_dut_speaker_state(void)
{
    return speaker_state;
}

void rdx_dut_speaker_reap(void)
{
    if (speaker_state == DUT_AUDIO_DONE) {
        os_task_del("dut_sine");
        speaker_close();
        speaker_state = DUT_AUDIO_IDLE;
    }
}

static u8 dut_speaker_idle_query(void)
{
    return speaker_state == DUT_AUDIO_IDLE;
}

REGISTER_LP_TARGET(dut_speaker_lp_target) = {
    .name = "dut_sine",
    .is_idle = dut_speaker_idle_query,
};
