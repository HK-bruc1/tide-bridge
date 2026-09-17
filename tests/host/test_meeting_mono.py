"""Stage 2A production C with mocked codec/framework, not acoustic validation."""
import tempfile
from pathlib import Path

from test_factory_usb import ROOT, run_c_checks


def function(source, name):
    start = source.index(name)
    start = source.rfind('\n', 0, start) + 1
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


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
    assert 'if(!rdx_record_stream_only_session_is_active()){\n        rdx_record_local_append' in record
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
int rdx_record_stream_only_release(void) { ++stop_calls; return !worker_busy; }
int os_taskq_post_type(const char *t, int q, int n, int *msg) { ++queue_calls; return queue_fail; }
'''
    codec_stubs = r'''
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
    want_selected=mic==2; want_right=mic==0;
    for (int i=0; i<320; ++i) { pcm[i]=i+100; pcm[i+320]=-i-100; }
    input_frame.data=pcm; input_frame.len=1280;
}
int test_mono(void) {
    CHECK(rdx_record_format_for_session(0,1)==(TCFG_T2620_MEETING_MONO_DEBUG_MIC?1:2));
    CHECK(rdx_record_format_for_session(0,0)==2);
    CHECK(rdx_record_format_for_session(1,1)==2);
    for(int mic=0; mic<=2; ++mic) {
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
        for mode in (0, 1, 2):
            configured = stubs.replace('#define TCFG_T2620_MEETING_MONO_DEBUG_MIC 1',
                                       f'#define TCFG_T2620_MEETING_MONO_DEBUG_MIC {mode}')
            path.write_text(configured + policy + codec + node[struct_start:struct_end] + codec_stubs + funcs + tests, encoding='utf-8')
            run_c_checks(path, ['test_mono'])
    print('Meeting mono stage 2A behavioral checks passed (mock codec).')


if __name__ == '__main__':
    main()
