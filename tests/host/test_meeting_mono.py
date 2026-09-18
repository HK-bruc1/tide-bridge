"""Production mono/selector integration with mocked DSP, not acoustic validation."""
import tempfile
from pathlib import Path

from host_c_test_lib import ROOT, function, run_c_checks


def main():
    node = (ROOT / 'SDK/audio/framework/nodes/effect_dev2_node.c').read_text(encoding='utf-8-sig')
    record = (ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_record.c').read_text(encoding='utf-8-sig')
    recorder = (ROOT / 'SDK/audio/interface/recoder/translation_ear_recoder.c').read_text(encoding='utf-8-sig')
    # Integration boundary: the historical MONO graph is the dual-MIC CHAT
    # graph. Validate it independently of the codec mocks below.
    assert 'uuid != PIPELINE_UUID_TRANSLATION_MONO' in recorder
    assert recorder.index('NODE_IOC_SET_PRIV_FMT, (int)&policy') < recorder.index('err = jlstream_start(')
    assert 'global_ch_mode != AUDIO_CH_LR' in recorder
    assert 'fmt.channel_mode = AUDIO_CH_MIX;' in recorder
    assert record.count('rdx_record_format_frame(rdx_uxfile_get_operateFile_info(), rp->formate, d, len)') == 2
    assert record.count('rdx_record_format_for_session(RECORD_SCENE_CHAT,') == 2
    policy = (ROOT / 'SDK/audio/effect/meeting_mono.h').read_text(encoding='utf-8-sig')
    codec = (ROOT / 'SDK/audio/st_opus_enc/opus_stenc_api.h').read_text(encoding='utf-8-sig')
    struct_start = node.index('struct effect_dev2_node_hdl {')
    struct_end = node.index('\n};', struct_start) + 3
    stubs = r'''
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef short s16;
#define NULL ((void *)0)
#define EINVAL 22
#define ENOMEM 12
#define printf(...) ((void)0)
#define log_error(...) ((void)0)
#define log_debug(...) ((void)0)
#define TCFG_T2620_MEETING_MONO_DEBUG_MIC 1
#define RECORD_SCENE_CHAT 0
#define RECORD_SCENE_CALL 1
#define RECORD_FORMATE_OPUS_16K_MONO 1
#define RECORD_FORMATE_OPUS_16K_STERO 2
#define AUDIO_CH_LR 2
#define AUDIO_CH_MIX 1
#define AUDIO_CH_NUM(x) (x)
#define NEGO_STA_ACCPTED 1
#define NEGO_STA_CONTINUE 2
#define CHANNEL_ADAPTER_TYPE 0
#define CHANNEL_ADAPTER_2TO4 1
#define CHANNEL_ADAPTER_1TO2 2
#define EFFECT_DEV2_FRAME_POINTS 320
#define THIRD_PARTY_PROTOCOLS_SEL 1
#define RDX_EN 1
#define Q_CALLBACK 1
void *memcpy(void *, const void *, unsigned int);
void *memset(void *, int, unsigned int);
struct stream_fmt { u32 sample_rate; u8 channel_mode, bit_wide, Qval; };
struct stream_oport { struct stream_fmt fmt; };
struct stream_node;
struct stream_iport { struct stream_node *node; struct stream_oport *prev; };
struct stream_node { void *private_data; struct stream_oport *oport; struct stream_iport *iport; int uuid, subid; };
struct stream_frame { u32 len; void *data; };
struct stream_note { int unused; };
struct user_effect_tool_param { int int_param[8]; float float_param[8]; };
struct packet_ctrl {
    u8 in_ch_num, out_ch_num, bit_width, qval;
    u32 sample_rate;
    void *node_hdl, *remain_buf;
    u32 (*effect_run)(void *, s16 *, s16 *, u32);
};
static u8 pool[4][32768];
static int used[4], alloc_calls, alloc_fail_at, bad;
void *test_malloc(u32 n) {
    ++alloc_calls;
    if (alloc_calls == alloc_fail_at) return NULL;
    if (n > sizeof(pool[0])) { bad = 1; return NULL; }
    for (int i=0; i<4; ++i) if (!used[i]) { used[i]=1; return pool[i]; }
    bad=1; return NULL;
}
void test_free(void *p) {
    for (int i=0; i<4; ++i) if (p==pool[i]) { if (!used[i]) bad=1; used[i]=0; return; }
    bad=1;
}
#define malloc test_malloc
#define free test_free
static struct stream_node test_node;
#define hdl_node(h) (&test_node)
static int config_audio_cfg_online_enable, read_fail;
int jlstream_read_node_data_new(int a, int b, void *c, char *d) { return !read_fail; }
int jlstream_read_effects_online_param(int a, char *b, void *c, u32 d) { return 0; }
void effect_dev_init(struct packet_ctrl *p, u32 n) { p->remain_buf=test_malloc(n*4); }
void effect_dev_close(struct packet_ctrl *p) { if(p->remain_buf) test_free(p->remain_buf); p->remain_buf=NULL; }
static int legacy_calls;
void effect_dev_process(struct packet_ctrl *p, struct stream_iport *i, struct stream_note *n) { ++legacy_calls; }
static struct stream_frame input_frame, output_frame;
static u8 output_data[1280];
static int pending, pulled, freed, pushed, frame_alloc_fail;
static int output_lengths[4];
struct stream_frame *jlstream_pull_frame(struct stream_iport *i, struct stream_note *n) {
    if (!pending) return NULL;
    --pending; ++pulled; return &input_frame;
}
struct stream_frame *jlstream_get_frame(struct stream_oport *o, u32 n) {
    if (frame_alloc_fail) return NULL;
    if(n!=1280) bad=1;
    output_frame.data=output_data; return &output_frame;
}
void jlstream_free_frame(struct stream_frame *f) { ++freed; }
void jlstream_push_frame(struct stream_oport *o, struct stream_frame *f) { output_lengths[pushed++]=f->len; }
static int fault_calls, fault_reason;
static u32 fault_epoch;
void report_fault(u32 epoch, int reason) { ++fault_calls; fault_epoch=epoch; fault_reason=reason; }
static u32 mono_epoch, mono_binding;
static u8 mono_running;
static volatile int mono_fault;
static int binding_valid=1, stop_calls, queue_fail, queue_calls, worker_busy;
int rdx_record_binding_token_is_current(u32 token) { return binding_valid && token==11; }
int rdx_record_audio_fault_stop(void) { ++stop_calls; return !worker_busy; }
int os_taskq_post_type(const char *t, int q, int n, int *msg) { ++queue_calls; return queue_fail; }
'''
    codec_stubs = r'''

#define OS_ENTER_CRITICAL() ((void)0)
#define OS_EXIT_CRITICAL() ((void)0)
#define MS_MAX_MICS 4
typedef struct { int sample_rate, num_mics, analysis_samples; } mic_select_config_t;
static int select_live, select_create_fail, select_process_fail, select_destroy_calls;
static int source_health;
static u32 now_ms;
u32 sys_timer_get_ms(void) { return now_ms; }
int source_dev1_pair_health(void) { return source_health; }
int rdx_record_mono_debug_mic(void) { return TCFG_T2620_MEETING_MONO_DEBUG_MIC; }
void sl_mic_select_config_init(mic_select_config_t *p) { memset(p, 0, sizeof(*p)); }
int sl_mic_select_create(const mic_select_config_t *p) {
    if (select_live || select_create_fail) return -1;
    if(p->sample_rate!=16000 || p->num_mics!=2 || p->analysis_samples!=320) bad=1;
    select_live=1; return 0;
}
void sl_mic_select_destroy(void) { if(!select_live) bad=1; select_live=0; ++select_destroy_calls; }
int sl_mic_select_process(const s16 *l, const s16 *r, const s16 *c, const s16 *d,
                          u32 n, s16 *o, int *sel) {
    if (!select_live || !l || !r || c || d || n!=320 || sel || o==l || o==r) bad=1;
    for(int i=0;i<320;++i) { if(l[i]!=i+100 || r[i]!=-i-100) bad=1; o[i]=r[i]; }
    return select_process_fail ? -2 : 320;
}
void sl_mic_select_get_channel_rms(float *r) { r[0]=100; r[1]=200; }
void sl_mic_select_get_channel_state(int *s) { s[0]=s[1]=1; }
int sl_mic_select_get_active_channel(void) { return 2; }
static OPUS_ENC_PARA opened;
static int open_fail, run_fail, run_calls, next_len=43;
static int want_right, want_selected;
static u32 workspace=30000;
u32 need_buf(OPUS_ENC_PARA *p) { return workspace; }
u32 enc_open(u8 *p, OPUS_STEN_FILE_IO *io, OPUS_ENC_PARA *s) { opened=*s; return open_fail; }
u32 enc_run(u8 *p, short *l, short *r, u8 *out, int *len) {
    ++run_calls;
    if (!!r != !!want_right) bad=1;
    for (int i=0; i<320; ++i) {
        if (l[i] != (want_selected ? -i-100 : i+100)) bad=1;
        if (r && r[i] != -i-100) bad=1;
    }
    *len=next_len;
    for (int i=0; i<next_len && i<1280; ++i) out[i]=(u8)i;
    return run_fail;
}
static OPUS_STENC_OPS ops={need_buf, enc_open, NULL, enc_run};
OPUS_STENC_OPS *get_opus_stenc_ops(void) { return &ops; }
'''
    funcs = '\n'.join(function(node, name) for name in (
        'audio_effect_dev2_init(', 'effect_dev2_fail(', 'audio_effect_dev2_run(',
        'audio_effect_dev2_exit(', 'effect_dev2_handle_frame(',
        'effect_dev2_ioc_negotiate(', 'effect_dev2_ioc_start(', 'effect_dev2_ioc_stop('))
    funcs = 'static struct effect_dev2_node_hdl *selector_owner;\n' + funcs
    funcs += function(record, 'rdx_record_format_for_session(')
    funcs += '\n'.join(function(recorder, name) for name in (
        'translation_mono_fault(', 'translation_mono_stop_on_app_core(', 'translation_mono_fault_poll('))
    tests = r'''
#define CHECK(x) do { if (!(x)) return __LINE__; } while(0)
static s16 pcm[640], out[640];
static struct effect_dev2_node_hdl h;
static struct stream_oport input_port, output_port;
static struct stream_iport input;
void setup(int mic) {
    memset(&h,0,sizeof(h)); h.mono.mic=mic; h.mono.epoch=7; h.mono.fault=report_fault;
    input_port.fmt.sample_rate=output_port.fmt.sample_rate=16000;
    input_port.fmt.channel_mode=2; input_port.fmt.bit_wide=0;
    output_port.fmt.channel_mode=mic ? 1 : 2;
    test_node.private_data=&h; test_node.oport=&output_port; test_node.iport=&input;
    input.node=&test_node; input.prev=&input_port;
    alloc_calls=alloc_fail_at=bad=read_fail=open_fail=run_fail=0;
    run_calls=fault_calls=pushed=freed=pulled=pending=frame_alloc_fail=0;
    next_len=43; workspace=30000;
    source_health=select_create_fail=select_process_fail=0;
    want_selected=mic>=2; want_right=mic==0;
    for (int i=0; i<320; ++i) { pcm[i]=i+100; pcm[i+320]=-i-100; }
    input_frame.data=pcm; input_frame.len=1280;
}
int test_mono(void) {
    CHECK(rdx_record_format_for_session(0,1)==(TCFG_T2620_MEETING_MONO_DEBUG_MIC?1:2));
    CHECK(rdx_record_format_for_session(0,0)==(TCFG_T2620_MEETING_MONO_DEBUG_MIC?1:2));
    CHECK(rdx_record_format_for_session(1,1)==2);
    for(int mic=0; mic<=3; ++mic) {
        setup(mic);
        CHECK(effect_dev2_ioc_negotiate(&input)==NEGO_STA_ACCPTED);
        CHECK(h.dev.in_ch_num==2 && h.dev.out_ch_num==(mic?1:2));
        CHECK(effect_dev2_ioc_start(&h)==0 && h.started);
        CHECK(opened.nch==(mic?1:2) && opened.br==(mic?16000:32000));
        CHECK(opened.sr==16000 && opened.format_mode==0 && opened.frame_ms==20);
        CHECK(audio_effect_dev2_run(&h,pcm,out,1280)==43 && !bad);
        CHECK(effect_dev2_ioc_start(&h)==0); /* no duplicate allocation */
        if(mic) {
            pending=2; effect_dev2_handle_frame(&input,NULL);
            CHECK(pushed==2 && output_lengths[0]==43 && output_lengths[1]==43 && freed==2);
            CHECK(audio_effect_dev2_run(&h,pcm,out,1279)==0 && fault_calls==1);
            CHECK(fault_epoch==7 && fault_reason==-1);
            CHECK(audio_effect_dev2_run(&h,pcm,out,1280)==0 && fault_calls==1);
        }
        effect_dev2_ioc_stop(&h); effect_dev2_ioc_stop(&h);
        CHECK(!used[0] && !used[1] && !used[2] && !bad);
    }

    setup(3); effect_dev2_ioc_negotiate(&input); select_create_fail=1;
    int destroyed=select_destroy_calls;
    CHECK(effect_dev2_ioc_start(&h)!=0 && !h.selector_owned && !select_live);
    CHECK(!selector_owner && select_destroy_calls==destroyed && !used[0] && !used[1]);
    setup(3); effect_dev2_ioc_negotiate(&input); CHECK(effect_dev2_ioc_start(&h)==0);
    struct effect_dev2_node_hdl other;
    memset(&other,0,sizeof(other)); other.selector_owned=0; other.mono.mic=3; other.dev.out_ch_num=1;
    CHECK(audio_effect_dev2_init(&other)!=0);
    audio_effect_dev2_exit(&other);
    CHECK(select_live && selector_owner==&h && select_destroy_calls==destroyed);
    select_process_fail=1;
    CHECK(audio_effect_dev2_run(&h,pcm,out,1280)==0 && !run_calls && fault_reason==-5);
    effect_dev2_ioc_stop(&h); effect_dev2_ioc_stop(&h);
    CHECK(!select_live && !selector_owner && select_destroy_calls==destroyed+1 && !bad);
    for(int reason=-21; reason<=-20; ++reason) {
        setup(3); effect_dev2_ioc_negotiate(&input); CHECK(effect_dev2_ioc_start(&h)==0);
        source_health=reason;
        CHECK(audio_effect_dev2_run(&h,pcm,out,1280)==0 && !run_calls && fault_reason==reason);
        effect_dev2_ioc_stop(&h); CHECK(!select_live && !bad);
    }
    for(int fail=1; fail<=2; ++fail) {
        setup(1); effect_dev2_ioc_negotiate(&input); alloc_fail_at=fail;
        CHECK(effect_dev2_ioc_start(&h)==-ENOMEM && !h.started);
        CHECK(!used[0] && !used[1] && !bad);
    }
    setup(1); effect_dev2_ioc_negotiate(&input); open_fail=1;
    CHECK(effect_dev2_ioc_start(&h)!=0 && !used[0] && !used[1]);
    setup(1); effect_dev2_ioc_negotiate(&input); input_port.fmt.sample_rate=8000;
    CHECK(effect_dev2_ioc_start(&h)==-EINVAL && !alloc_calls);
    setup(1); effect_dev2_ioc_negotiate(&input); input_port.fmt.bit_wide=1;
    CHECK(effect_dev2_ioc_start(&h)==-EINVAL && !alloc_calls);
    setup(1); input_port.fmt.channel_mode=1;
    CHECK(effect_dev2_ioc_negotiate(&input)==NEGO_STA_CONTINUE && input_port.fmt.channel_mode==2);
    for (int mode=0; mode<5; ++mode) {
        setup(1); effect_dev2_ioc_negotiate(&input); CHECK(effect_dev2_ioc_start(&h)==0);
        if(mode==0) run_fail=1;
        if(mode==1) next_len=-1;
        if(mode==2) next_len=0;
        if(mode==3) next_len=1281;
        if(mode==4) frame_alloc_fail=1;
        pending=2; effect_dev2_handle_frame(&input,NULL);
        CHECK(!pushed && fault_calls==1 && h.failed && freed>=2);
        effect_dev2_ioc_stop(&h); CHECK(!bad && !used[0] && !used[1]);
    }
    setup(1); effect_dev2_ioc_negotiate(&input); CHECK(effect_dev2_ioc_start(&h)==0);
    struct effect_dev2_node_hdl second = {0};
    second.selector_owned=0; /* Explicit tail init: target IR uses 32-bit sizeof, host pointers are 64-bit. */
    second.dev.in_ch_num=2; second.dev.out_ch_num=1;
    CHECK(audio_effect_dev2_init(&second)==0);
    CHECK(second.run_buf!=h.run_buf && second.pcm!=h.pcm);
    audio_effect_dev2_exit(&second);
    CHECK(audio_effect_dev2_run(&h,pcm,out,1280)==43 && !bad);
    effect_dev2_ioc_stop(&h);
    CHECK(!used[0] && !used[1] && !used[2] && !used[3]);
    mono_epoch=8; mono_binding=11; mono_running=1; mono_fault=0;
    translation_mono_fault(7,-1); CHECK(!mono_fault);
    translation_mono_fault(8,-2); CHECK(mono_fault==-2);
    queue_fail=1; translation_mono_fault_poll((void *)8); CHECK(queue_calls==1 && mono_fault);
    queue_fail=0; translation_mono_fault_poll((void *)8); CHECK(queue_calls==2);
    translation_mono_stop_on_app_core((void *)7); CHECK(!stop_calls);
    binding_valid=0; translation_mono_stop_on_app_core((void *)8); CHECK(!stop_calls);
    binding_valid=1; worker_busy=1;
    translation_mono_stop_on_app_core((void *)8); CHECK(stop_calls==1 && mono_fault);
    worker_busy=0;
    translation_mono_stop_on_app_core((void *)8); CHECK(stop_calls==2 && !mono_fault);
    translation_mono_stop_on_app_core((void *)8); CHECK(stop_calls==2);
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix='t2620-mono-') as work:
        path = Path(work) / 'meeting_mono.c'
        report_stubs = r'''
typedef unsigned char u8;
typedef struct { int run, scene, formate; } RecordStatus;
#define RECORD_STATE_START 0
#define RECORD_STATE_PAUSE 1
#define RECORD_STATE_RESUME 2
#define RECORD_STATE_STOP 3
#define RECORD_SCENE_CHAT 0
#define RECORD_SCENE_CALL 1
#define RECORD_FORMATE_OPUS_16K_MONO 1
#define RDX_RECORD_CHANNAL_SINGLE 0
#define RDX_RECORD_CHANNAL_DUAL 1
#define RECORD_TASK_NAME "record"
#define MIC_TO_MONO_OPUS 10
#define MIC_DAC_TO_STERO_OPUS 11
#define y_printf(...) ((void)0)
static int mode, mode_at_post, posts;
void rdx_app_set_record_mode(int value) { mode = value; }
int rdx_record_binding_token_capture(void) { return 7; }
int os_taskq_post_msg(const char *name, int count, int run, ...) {
    mode_at_post = mode; ++posts; return 0;
}
'''
        report_tests = r'''
int test_report(void) {
    RecordStatus status;
    int scene, format, run;
    for (scene=0; scene<2; ++scene)
    for (format=1; format<=2; ++format)
    for (run=0; run<4; ++run) {
        status.scene=scene; status.formate=format; status.run=run;
        mode=9; posts=0;
        rdx_record_auto_run(&status);
        if (posts != 1) return __LINE__;
        int expected = (run==0 || run==2) ?
            ((scene==0 && format==1) ? 0 : 1) : 9;
        if (mode != expected || mode_at_post != expected) return __LINE__;
    }
    return 0;
}
'''
        path.write_text(report_stubs + function(record, 'void rdx_record_auto_run(') + report_tests, encoding='utf-8')
        run_c_checks(path, ['test_report'])
        source = (ROOT / 'SDK/audio/framework/plugs/source/source_dev1_file.c').read_text(encoding='utf-8-sig')
        health_stubs = r"""
typedef unsigned int u32;
#define NULL ((void *)0)
struct source_dev1_file_hdl { int start; };
static struct source_dev1_file_hdl device, *hdl_p;
static u32 source_drop_count[2], source_last_pair_ms, now_ms;
static int source_pair_fault;
u32 sys_timer_get_ms(void) { return now_ms; }
"""
        health_tests = r"""
#define CHECK(x) do { if(!(x)) return __LINE__; } while(0)
int test_health(void) {
    CHECK(source_dev1_pair_health()==0);
    hdl_p=&device; device.start=1; source_last_pair_ms=100;
    now_ms=599; CHECK(source_dev1_pair_health()==0);
    now_ms=600; CHECK(source_dev1_pair_health()==-21);
    source_last_pair_ms=600; CHECK(source_dev1_pair_health()==-21);
    source_pair_fault=0; source_drop_count[1]=1;
    CHECK(source_dev1_pair_health()==-20);
    source_drop_count[1]=0; CHECK(source_dev1_pair_health()==-20);
    device.start=0; CHECK(source_dev1_pair_health()==0);
    device.start=1; source_pair_fault=0;
    source_last_pair_ms=0xffffff00u; now_ms=243;
    CHECK(source_dev1_pair_health()==0);
    now_ms=244; CHECK(source_dev1_pair_health()==-21);
    return 0;
}
"""
        path.write_text(health_stubs + function(source, 'int source_dev1_pair_health(') + health_tests, encoding='utf-8')
        run_c_checks(path, ['test_health'])
        for mode in (0, 1, 2, 3):
            configured = stubs.replace('#define TCFG_T2620_MEETING_MONO_DEBUG_MIC 1',
                                       f'#define TCFG_T2620_MEETING_MONO_DEBUG_MIC {mode}')
            path.write_text(configured + policy + codec + node[struct_start:struct_end] + codec_stubs + funcs + tests, encoding='utf-8')
            run_c_checks(path, ['test_mono'])
    metadata_stubs = r"""
typedef unsigned char u8;
typedef struct { unsigned int frame_size; } uxfile_data_t;
#define RECORD_FORMATE_OPUS_16K_MONO 1
#define RECORD_FORMATE_OPUS_16K_STERO 2
static uxfile_data_t file;
static int missing;
uxfile_data_t *rdx_uxfile_get_operateFile_info(void) { return missing ? 0 : &file; }
"""
    metadata_tests = r"""
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
int test_raw_metadata(void) {
    file.frame_size=80;
    rdx_record_init_raw_metadata(1); CHECK(file.frame_size==40);
    rdx_record_init_raw_metadata(2); CHECK(file.frame_size==80);
    file.frame_size=123;
    rdx_record_init_raw_metadata(0); CHECK(file.frame_size==123);
    rdx_record_init_raw_metadata(3); CHECK(file.frame_size==123);
    rdx_record_init_raw_metadata(255); CHECK(file.frame_size==123);
    missing=1; rdx_record_init_raw_metadata(1);
    CHECK(file.frame_size==123);
    return 0;
}
"""
    with tempfile.TemporaryDirectory(prefix='t2620-raw-metadata-') as work:
        path = Path(work) / 'raw_metadata.c'
        path.write_text(metadata_stubs + function(record, 'static void rdx_record_init_raw_metadata(')
                        + metadata_tests, encoding='utf-8')
        run_c_checks(path, ['test_raw_metadata'])
    # Both recording implementations update only immediately after new-file
    # generation; the existing generation guard preserves PAUSE/RESUME state.
    assert record.count('rdx_record_init_raw_metadata(rp->formate);') == 2
    for scene in ('rp->scene', 'scene'):
        assert ('rdx_uxfile_dat_1_gen(' + scene + ');\n'
                '            rdx_record_init_raw_metadata(rp->formate);') in record
    playback = (ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_playback.c').read_text(encoding='utf-8-sig')
    playback_stubs = r"""
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef int bool;
#define true 1
#define false 0
static u16 pb_frame_bytes=80, pb_pending_len, pb_pending_off;
static u8 pb_channels=2, pb_stream_buf[560];
static u32 pb_total_written, free_bytes, offered, consumed;
static struct { u32 seek_base_frame, duration_frames; } pb;
u32 source_dev0_get_free_space(void) { return free_bytes; }
u32 source_dev0_input_write(void *data, u32 len) { offered=len; return len; }
u32 source_dev0_get_consumed_bytes(void) { return consumed; }
"""
    playback_tests = r"""
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
int test_raw_playback(void) {
    CHECK(pb_raw_profile(0,&pb_frame_bytes,&pb_channels));
    CHECK(pb_frame_bytes==80 && pb_channels==2);
    CHECK(!pb_raw_profile(79,&pb_frame_bytes,&pb_channels));
    CHECK(pb_frame_bytes==80 && pb_channels==2);
    CHECK(!pb_raw_profile(0xffffffffu,&pb_frame_bytes,&pb_channels));
    for (u32 channels=1; channels<=2; ++channels) {
        CHECK(pb_raw_profile(channels*40,&pb_frame_bytes,&pb_channels));
        CHECK(pb_channels==channels);
        pb_pending_len=560; pb_pending_off=0; pb_total_written=0;
        free_bytes=pb_frame_bytes-1; offered=0;
        CHECK(pb_write_pending()==0 && offered==0 && pb_pending_off==0);
        free_bytes=pb_frame_bytes*3+1;
        CHECK(pb_write_pending()==pb_frame_bytes*3);
        CHECK(offered==pb_frame_bytes*3 && pb_pending_off==offered);
        free_bytes=560;
        CHECK(pb_write_pending()==560-pb_frame_bytes*3);
        CHECK(pb_pending_len==0 && pb_pending_off==0 && pb_total_written==560);
        pb.seek_base_frame=250; pb.duration_frames=1000;
        consumed=pb_frame_bytes*100;
        CHECK(pb_current_frame()==350);
        consumed=pb_frame_bytes*1000;
        CHECK(pb_current_frame()==1000);
    }
    return 0;
}
"""
    with tempfile.TemporaryDirectory(prefix='t2620-raw-playback-') as work:
        path = Path(work) / 'raw_playback.c'
        funcs = ''.join(function(playback, name) for name in
                        ('static bool pb_raw_profile(', 'static u32 pb_write_pending(',
                         'static u32 pb_current_frame('))
        path.write_text(playback_stubs + funcs + playback_tests, encoding='utf-8')
        run_c_checks(path, ['test_raw_playback'])
    assert 'base_frame * candidate_frame_bytes' in playback
    assert 'target_frame * pb_frame_bytes' in playback
    assert playback.index('pb_close_track();', playback.index('static pb_candidate_result_t pb_start_candidate_at_frame')) < playback.index('pb_frame_bytes = candidate_frame_bytes;')
    source = (ROOT / 'SDK/audio/framework/plugs/source/source_dev0_file.c').read_text(encoding='utf-8-sig')
    source_stubs = r"""
void *memcpy(void *, const void *, unsigned int);
void *memset(void *, int, unsigned int);
int memcmp(const void *, const void *, unsigned int);
#define NULL ((void *)0)
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
#define TCFG_RDX_LOCAL_PLAYBACK_ENABLE 1
#define SOURCE_DEV0_MSBC_TEST_ENABLE 0
#define AUDIO_CODING_STENC_OPUS 0xB0000000u
#define AUDIO_CODING_OPUS 0x100000u
#define AUDIO_CH_LR 37
#define AUDIO_CH_MIX 20
#define OUTPUT_BUFF_SIZE 2048
struct source_dev0_file_hdl { u8 ch_num; };
struct stream_fmt { u32 sample_rate, coding_type, channel_mode, bit_rate, frame_dms; };
static u8 output[88], data_ok, input[560];
static u32 available, cursor, output_cbuf_h;
u32 cbuf_get_data_len(void *p) { return available; }
u32 source_input_read(u8 *dst, u16 n) {
    if (available<n) return 0;
    memcpy(dst,input+cursor,n); cursor+=n; available-=n; return n;
}
int putchar(int c) { return c; }
"""
    constants = '\n'.join(line for line in source.splitlines() if line.startswith('#define OPUS_'))
    # The disabled-product fallback repeats MAX_FRAME_BYTES; retain local profile.
    constants = constants.replace('#define OPUS_MAX_FRAME_BYTES        OPUS_MONO_FRAME_BYTES', '')
    source_tests = r"""
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
int test_decoder_packets(void) {
    for (u32 ch=1; ch<=2; ++ch) {
        struct source_dev0_file_hdl h={ch}; struct stream_fmt f={0};
        source_dev0_get_fmt(&h,&f);
        CHECK(f.coding_type==AUDIO_CODING_STENC_OPUS);
        CHECK(f.sample_rate==48000 && f.channel_mode==AUDIO_CH_LR);
        CHECK(f.bit_rate==16000*ch && f.frame_dms==200);
        for (u32 i=0;i<560;++i) input[i]=(u8)(i*19);
        available=560; cursor=0; data_ok=0;
        while (available>=40*ch) {
            u32 before=cursor, len=0; u8 *p=source_dev0_get_packet(&h,&len);
            CHECK(p && len==40*ch+8);
            CHECK(p[0]==0 && p[1]==0 && p[2]==0 && p[3]==40*ch);
            CHECK(p[4]==0 && p[5]==0 && p[6]==0 && p[7]==0);
            CHECK(!memcmp(p+8,input+before,40*ch));
        }
        u32 len=0; CHECK(!source_dev0_get_packet(&h,&len));
        CHECK(cursor==560 && available==0);
    }
    return 0;
}
"""
    with tempfile.TemporaryDirectory(prefix='t2620-raw-decoder-') as work:
        path = Path(work) / 'decoder_packets.c'
        funcs = ''.join(function(source, name) for name in
                        ('static u8 *source_dev0_get_packet(', 'static void source_dev0_get_fmt('))
        path.write_text(source_stubs + constants + '\n' + funcs + source_tests, encoding='utf-8')
        run_c_checks(path, ['test_decoder_packets'])
    assert 'source_dev0_add_consumed_bytes(len - OPUS_RAW_HEADER_BYTES)' in source
    config = (ROOT / 'SDK/apps/earphone/log_config/lib_media_config.c').read_text(encoding='utf-8-sig')
    assert 'CONFIG_OGG_OPUS_DEC_SET_RAW_MODE = TCFG_RDX_LOCAL_PLAYBACK_ENABLE ? 1 : 0' in config
    assert 'CONFIG_OGG_OPUS_DEC_SET_CBR_PACKET_LEN = TCFG_RDX_LOCAL_PLAYBACK_ENABLE ? 0 : 80' in config
    player = (ROOT / 'SDK/audio/interface/player/dev_flow_player.c').read_text(encoding='utf-8-sig')
    close_stubs = r"""
typedef unsigned char u8;
typedef int OS_SEM;
#define NULL ((void *)0)
#define ENOMEM 12
#define KILL_WAIT 0
#define STREAM_EVENT_CLOSE_PLAYER 1
struct jlstream { int value; };
struct dev_flow_player { struct jlstream *stream; OS_SEM stop_done; u8 stopping, stopped; };
static struct dev_flow_player *g_dev_flow_player;
static void (*work_fn)(void *); static void *work_arg;
static int fail_create, fail_fork, stops, releases, frees, notices, joins;
int os_sem_create(OS_SEM *s, int n) { *s=n; return fail_create; }
int os_sem_post(OS_SEM *s) { ++*s; return 0; }
int os_sem_query(OS_SEM *s) { return *s; }
int os_sem_del(OS_SEM *s, int x) { return 0; }
void jlstream_stop(struct jlstream *s, int fade) { ++stops; }
void jlstream_release(struct jlstream *s) { ++releases; }
void free(void *p) { ++frees; }
void jlstream_event_notify(int event, int arg) { ++notices; }
int os_task_create(void (*fn)(void *),void *arg,int p,int s,int q,const char *n) {
    if (fail_fork) return 1;
    work_fn=fn; work_arg=arg; return 0;
}
void complete_worker(void) { if (work_fn) { void (*fn)(void *)=work_fn; work_fn=NULL; fn(work_arg); } }
int os_sem_pend(OS_SEM *s,int t) { complete_worker(); --*s; return 0; }
void os_task_del(const char *n) { ++joins; }
#define os_time_dly(x) return
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
"""
    close_tests = r"""
int test_async_close(void) {
    struct jlstream stream={0};
    struct dev_flow_player p={&stream,0,0,0}; g_dev_flow_player=&p;
    fail_create=1; CHECK(dev_flow_player_drain_close()<0 && !p.stopping);
    fail_create=0; fail_fork=1;
    CHECK(dev_flow_player_drain_close()<0 && !p.stopping && !stops);
    fail_fork=0;
    CHECK(dev_flow_player_drain_close()==0 && p.stopping && !stops);
    CHECK(dev_flow_player_drain_close()==0 && !releases && g_dev_flow_player==&p);
    complete_worker(); CHECK(stops==1 && !releases);
    CHECK(dev_flow_player_drain_close()==1);
    CHECK(dev_flow_player_drain_close()==1);
    dev_flow_player_close();
    CHECK(stops==1 && releases==1 && frees==1 && notices==1 && joins==1 && !g_dev_flow_player);
    dev_flow_player_close(); CHECK(releases==1);
    p.stopping=p.stopped=0; g_dev_flow_player=&p;
    CHECK(dev_flow_player_drain_close()==0);
    dev_flow_player_close(); /* explicit stop races pending EOF: join first */
    CHECK(stops==2 && releases==2 && joins==2 && !g_dev_flow_player);
    p.stopping=p.stopped=0; g_dev_flow_player=&p;
    dev_flow_player_close(); CHECK(stops==3 && releases==3 && joins==2);
    return 0;
}
"""
    with tempfile.TemporaryDirectory(prefix='t2620-playback-close-') as work:
        path = Path(work) / 'close.c'
        funcs = ''.join(function(player, name) for name in
                        ('static void dev_flow_stop_worker(', 'int dev_flow_player_drain_close(',
                         'void dev_flow_player_close('))
        path.write_text(close_stubs + funcs + close_tests, encoding='utf-8')
        run_c_checks(path, ['test_async_close'])
    print('Meeting mono and RAW playback behavioral checks passed (mock codec/selector).')


if __name__ == '__main__':
    main()
