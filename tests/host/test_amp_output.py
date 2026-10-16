"""PE5 跟随 DAC 实际输出的策略与 BR28 回调 ABI 校验；不模拟模拟电路硬件。"""
import re
import subprocess
import tempfile
from pathlib import Path

from host_c_test_lib import ROOT, run_c_checks


def strip_includes(text):
    return re.sub(r'^#include[^\n]*', '', text, flags=re.M)


def ir_function(source, name):
    match = re.search(r'^define [^\n]*@' + re.escape(name) +
                      r'\([^\n]*\).*?^}', source, re.M | re.S)
    assert match, f'Missing SDK function: {name}'
    return match[0]


def check_sdk_hooks(product):
    """直接检查随工程发布的 media.a，而不是缓存 IR 或伪造的 SDK 模型。"""
    archive = ROOT / 'SDK/cpu/br28/liba/media.a'
    actual = {}
    with tempfile.TemporaryDirectory(prefix='amp-sdk-') as tmp:
        for name in ('dac_base', 'dac_channel', 'audio_dac'):
            bc = Path(tmp) / (name + '.bc')
            ll = bc.with_suffix('.ll')
            bc.write_bytes(subprocess.check_output(
                ['C:/JL/pi32/bin/llvm-ar.exe', 'p', str(archive), name + '.c.o'],
                timeout=60))
            subprocess.run(['C:/JL/pi32/bin/clang.exe', '-target', 'pi32v2', '-mcpu=r3',
                            '-S', '-emit-llvm', str(bc), '-o', str(ll)],
                           check=True, capture_output=True, timeout=60)
            actual[name] = ll.read_text(encoding='utf-8')

    base, channel, analog = (actual[n] for n in ('dac_base', 'dac_channel', 'audio_dac'))
    assert 'define weak void @_audio_dac_status_hook(i8 zeroext' in base
    for name, symbol, value in [('audio_dac_init', 'INIT', 0),
                               ('audio_dac_fifo_start', 'START', 2),
                               ('audio_dac_stop', 'STOP', 4)]:
        assert re.search(r'RDX_DAC_STATUS_' + symbol + r'\s*=\s*' + str(value), product)
        calls = re.findall(r'call void @_audio_dac_status_hook\(i8 zeroext (\d+)\)',
                           ir_function(base, name))
        assert calls == [str(value)], f'Changed SDK callback ABI: {name}: {calls}'
    assert len(re.findall(r'call void @_audio_dac_status_hook\(', base)) == 3

    start = ir_function(base, 'audio_dac_fifo_start')
    assert start.index('@_audio_dac_status_hook(') < start.index('%fade_handler =')
    stop = ir_function(base, 'audio_dac_stop')
    assert stop.index('@audio_dac_buf_frames_fade_out(') < stop.index('@_audio_dac_status_hook(')
    for name, callee in [('audio_dac_channel_start', 'audio_dac_fifo_start'),
                         ('audio_dac_channel_close', 'audio_dac_stop')]:
        body = ir_function(channel, name)
        assert body.index('@os_mutex_pend(') < body.index('@' + callee + '(')
        assert body.index('@' + callee + '(') < body.index('@os_mutex_post(')

    # 关闭路径只在最后一个通道关闭时调用全局 stop。
    close = ir_function(channel, 'audio_dac_channel_close')
    count = re.search(r'(%\w+) = .*@audio_cfifo_channel_num[^\n]*', close)
    assert count
    compare = re.search(r'(%\w+) = icmp eq i32 ' + re.escape(count[1]) + r', 0', close)
    assert compare
    branch = re.search(r'br i1 ' + re.escape(compare[1]) +
                       r', label %([\w.]+), label %([\w.]+)', close)
    assert branch
    zero_block = re.search(r'^' + re.escape(branch[1]) + r':.*?(?=^[\w.]+:|^})',
                           close, re.M | re.S)
    assert zero_block and '@audio_dac_stop(' in zero_block[0]

    # 热启动不会再次发出模拟通知，因此保留模拟就绪时仅凭数字 START 即可重新打开 PE5。
    analog_init = ir_function(analog, 'dac_analog_init')
    assert analog_init.index('%analog_inited =') < analog_init.index('@audio_dac_power_state(')
    assert re.search(r'br i1 %\w+, label %if.end, label %return', analog_init)
    timer = ir_function(base, 'audio_dac_delay_off_timeout')
    assert timer.index('@os_mutex_pend(') < timer.index('@audio_event_notify(')
    assert timer.index('@audio_event_notify(') < timer.index('@audio_dac_close(')
    print('PE5 SDK ABI: init/start/stop, final-channel guard, fade and mutex order passed.')


STUBS = r'''
typedef unsigned char u8;
#define TCFG_T2620_AMP_ENABLE_IO 5
#define IO_PORT_SPILT(x) (x)
#define PORT_OUTPUT_HIGH 1
#define PORT_OUTPUT_LOW 0
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static int pe5, writes, irq_depth, violation, busy_count, bt_idle;
void local_irq_disable(void) { ++irq_depth; }
void local_irq_enable(void) { if (--irq_depth < 0) ++violation; }
void gpio_set_mode(int pin, int level) {
    if (pin != 5 || irq_depth < 1) ++violation;
    pe5 = level; ++writes;
}
int low_power_sys_not_idle_cnt(void) { return busy_count; }
int lower_power_bt_group_query(void) { return bt_idle; }
#define CFG_VBG_TRIM 1
#define CFG_DACLDO_TRIM 2
int syscfg_write(int id, void *p, int n) { return n; }
int syscfg_read(int id, void *p, int n) { return n; }
'''


CHECKS = r'''
static void reset(void) {
    pe5 = writes = irq_depth = violation = 0;
    busy_count = 1; bt_idle = 1;
    g_rdx_amp_enabled = g_rdx_amp_analog_ready = g_rdx_amp_output_running = 0;
    _audio_dac_status_hook(RDX_DAC_STATUS_INIT);
}
static void analog_open(void) {
    audio_dac_power_state(DAC_ANALOG_OPEN_PREPARE);
    audio_dac_power_state(DAC_ANALOG_OPEN_FINISH);
}
int check_cold_start_and_analog_only(void) {
    reset(); CHECK(!pe5);
    analog_open(); CHECK(!pe5 && !writes);
    _audio_dac_status_hook(RDX_DAC_STATUS_START); CHECK(pe5 && writes == 1);
    _audio_dac_status_hook(RDX_DAC_STATUS_START); CHECK(pe5 && writes == 1);
    audio_dac_power_state(DAC_ANALOG_OPEN_FINISH); CHECK(pe5 && writes == 1);
    _audio_dac_status_hook(RDX_DAC_STATUS_STOP); CHECK(!pe5 && writes == 2);
    CHECK(g_rdx_amp_analog_ready && !g_rdx_amp_output_running);
    CHECK(!violation && !irq_depth);
    return 0;
}
int check_retained_analog_recording_and_restart(void) {
    reset(); analog_open();
    busy_count = 2; /* 录音占用：只关 PE5，不改变 SDK 的模拟关闭投票。 */
    for (int i = 0; i < 20; ++i) {
        _audio_dac_status_hook(RDX_DAC_STATUS_START); CHECK(pe5);
        CHECK(!audio_event_notify(AUDIO_LIB_EVENT_DAC_LOWPOWER, 0, 0));
        _audio_dac_status_hook(RDX_DAC_STATUS_STOP); CHECK(!pe5);
        CHECK(!audio_event_notify(AUDIO_LIB_EVENT_DAC_LOWPOWER, 0, 0));
        CHECK(g_rdx_amp_analog_ready && !g_rdx_amp_output_running);
        _audio_dac_status_hook(RDX_DAC_STATUS_STOP); CHECK(!pe5);
    }
    CHECK(writes == 40 && !violation && !irq_depth);
    busy_count = 1;
    CHECK(audio_event_notify(AUDIO_LIB_EVENT_DAC_LOWPOWER, 0, 0));
    audio_dac_power_state(DAC_ANALOG_CLOSE_PREPARE); CHECK(!pe5);
    audio_dac_power_state(DAC_ANALOG_CLOSE_FINISH); CHECK(!pe5);
    analog_open(); CHECK(!pe5);
    _audio_dac_status_hook(RDX_DAC_STATUS_START); CHECK(pe5);
    return 0;
}
int check_global_close_admission_matrix(void) {
    reset(); analog_open();
    for (busy_count = 0; busy_count <= 5; ++busy_count) {
        for (bt_idle = -1; bt_idle <= 2; ++bt_idle) {
            _audio_dac_status_hook(RDX_DAC_STATUS_START); CHECK(pe5);
            _audio_dac_status_hook(RDX_DAC_STATUS_STOP); CHECK(!pe5);
            CHECK(audio_event_notify(AUDIO_LIB_EVENT_DAC_LOWPOWER, 0, 0) ==
                  (busy_count == 1 && bt_idle > 0));
        }
    }
    CHECK(!violation && !irq_depth);
    return 0;
}
int check_analog_close_invalidates_output(void) {
    const u8 states[] = {DAC_ANALOG_OPEN_PREPARE, DAC_ANALOG_CLOSE_PREPARE,
                         DAC_ANALOG_CLOSE_FINISH, 255};
    for (int i = 0; i < 4; ++i) {
        reset(); analog_open(); _audio_dac_status_hook(RDX_DAC_STATUS_START);
        CHECK(pe5);
        audio_dac_power_state(states[i]); CHECK(!pe5);
        CHECK(!g_rdx_amp_analog_ready && !g_rdx_amp_output_running);
        audio_dac_power_state(DAC_ANALOG_OPEN_FINISH); CHECK(!pe5);
        _audio_dac_status_hook(RDX_DAC_STATUS_START); CHECK(pe5);
        CHECK(!violation && !irq_depth);
    }
    return 0;
}
int check_unknown_init_and_callback_order(void) {
    reset();
    audio_dac_power_state(DAC_ANALOG_OPEN_PREPARE);
    _audio_dac_status_hook(RDX_DAC_STATUS_START); CHECK(!pe5);
    audio_dac_power_state(DAC_ANALOG_OPEN_FINISH); CHECK(pe5);
    _audio_dac_status_hook(255); CHECK(!pe5);
    audio_dac_power_state(DAC_ANALOG_OPEN_FINISH); CHECK(!pe5);
    _audio_dac_status_hook(RDX_DAC_STATUS_START); CHECK(pe5);
    _audio_dac_status_hook(RDX_DAC_STATUS_INIT); CHECK(!pe5);
    _audio_dac_status_hook(RDX_DAC_STATUS_START); CHECK(!pe5);
    _audio_dac_status_hook(RDX_DAC_STATUS_STOP);
    analog_open(); CHECK(!pe5);
    /* 回调不能破坏调用者已持有的临界区。 */
    local_irq_disable();
    _audio_dac_status_hook(RDX_DAC_STATUS_START); CHECK(pe5 && irq_depth == 1);
    _audio_dac_status_hook(RDX_DAC_STATUS_STOP); CHECK(!pe5 && irq_depth == 1);
    local_irq_enable();
    CHECK(!violation && !irq_depth);
    return 0;
}
'''


def run():
    source = (ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol/'
              'rdx_peripheral_power.c').read_text(encoding='utf-8-sig')
    amp = source[source.index('#if TCFG_T2620_AMP_POWER_ENABLE\n'):]
    check_sdk_hooks(amp)
    dac_header = (ROOT / 'SDK/interface/media/cpu/br28/asm/dac.h').read_text(encoding='utf-8-sig')
    defines = '\n'.join(re.findall(r'^#define DAC_ANALOG_\w+[^\n]*', dac_header, re.M))
    events = strip_includes((ROOT / 'SDK/interface/media/audio_event_manager.h').read_text(encoding='utf-8-sig'))
    handler = strip_includes((ROOT / 'SDK/audio/common/audio_event_handler.c').read_text(encoding='utf-8-sig'))
    checks = re.findall(r'^int (check_\w+)\(void\)', CHECKS, re.M)
    with tempfile.TemporaryDirectory(prefix='amp-output-') as tmp:
        path = Path(tmp) / 'amp.c'
        path.write_text('#define TCFG_T2620_AMP_POWER_ENABLE 1\n' + defines + '\n' +
                        STUBS + events + amp + handler + CHECKS, encoding='utf-8')
        run_c_checks(path, checks)
        disabled = Path(tmp) / 'amp_disabled.c'
        disabled.write_text('#define TCFG_T2620_AMP_POWER_ENABLE 0\n' + STUBS + amp + r'''
int check_disabled(void) {
    rdx_peripheral_power_amp_set(1);
    rdx_peripheral_power_amp_set(0);
    CHECK(!rdx_peripheral_power_amp_is_enabled() && !writes && !irq_depth);
    return 0;
}
''', encoding='utf-8')
        run_c_checks(disabled, ['check_disabled'])
        traced = Path(tmp) / 'amp_trace.c'
        traced.write_text('#define TCFG_T2620_AMP_POWER_ENABLE 1\n#define TCFG_T2620_AMP_TRACE_ENABLE 1\n' +
                          defines + '\n' + STUBS + r'''
#define Q_CALLBACK 1
typedef unsigned int u32;
static int posts, prints, post_fail;
static int queued_msg;
unsigned int sys_timer_get_ms(void) { return 1234; }
int os_taskq_post_type(const char *name, int type, int argc, int *argv) {
    if (irq_depth) ++violation;
    if (post_fail) return -1;
    ++posts; queued_msg = argv[0]; return 0;
}
#define r_printf(...) (++prints)
''' + amp + r'''
int check_trace(void) {
    audio_dac_power_state(DAC_ANALOG_OPEN_FINISH);
    CHECK(!posts && !prints);
    _audio_dac_status_hook(RDX_DAC_STATUS_START); CHECK(pe5 && posts == 1);
    _audio_dac_status_hook(RDX_DAC_STATUS_START); CHECK(posts == 1 && g_rdx_amp_trace_count == 1);
    _audio_dac_status_hook(RDX_DAC_STATUS_STOP); CHECK(!pe5 && posts == 1 && g_rdx_amp_trace_count == 2);
    ((void (*)(void))queued_msg)();
    CHECK(prints == 2 && !g_rdx_amp_trace_count && !g_rdx_amp_trace_queued);
    post_fail = 1;
    _audio_dac_status_hook(RDX_DAC_STATUS_START); CHECK(pe5 && !g_rdx_amp_trace_queued);
    post_fail = 0;
    for (int i = 0; i < 20; ++i) {
        _audio_dac_status_hook(i & 1 ? RDX_DAC_STATUS_START : RDX_DAC_STATUS_STOP);
    }
    CHECK(g_rdx_amp_trace_count == RDX_AMP_TRACE_SIZE && g_rdx_amp_trace_dropped);
    ((void (*)(void))queued_msg)();
    CHECK(!g_rdx_amp_trace_count && !g_rdx_amp_trace_dropped && !violation && !irq_depth);
    return 0;
}
''', encoding='utf-8')
        run_c_checks(traced, ['check_trace'])
    print(f'PE5 output policy: {len(checks)} groups + disabled + trace configurations passed; '
          'MIC, amplifier timing and current still require board tests.')


if __name__ == '__main__':
    run()
