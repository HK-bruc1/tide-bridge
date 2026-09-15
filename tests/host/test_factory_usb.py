"""Run the stage-1 factory USB C coordinator with deterministic hardware stubs.

Requires the repository's JL clang and llvmlite, as do the existing IR harnesses.
This checks policy/resource ordering, not physical USB enumeration or NAK timing.
"""
import ctypes
import re
import subprocess
from pathlib import Path

from llvmlite import binding as llvm

ROOT = Path(__file__).resolve().parents[2]
STUBS = r'''
typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned char usb_dev;
#define NULL ((void *)0)
#define FALSE 0
#define TCFG_T2620_FACTORY_USB_CDC_ENABLE 1
#define TCFG_USB_SLAVE_MSD_ENABLE 1
#define TCFG_USB_SLAVE_CDC_ENABLE 1
#define TCFG_USB_CDC_BACKGROUND_RUN 1
#define TCFG_SD0_ENABLE 1
#define APP_MODE_PC 1
#define SLAVE_MODE 1
#define USBSTACK_CDC_BACKGROUND 1
#define USBSTACK_STOP 2
#define USBSTACK_FACTORY_SHUTDOWN 3
#define MASSSTORAGE_CLASS 1
#define CDC_CLASS 2
#define log_info(...) ((void)0)
#define g_printf(...) ((void)0)
int on, vbus, role, pc, business, blocked, deferred, host_owned, dut_ready;
int rdx_dut_factory_usb_ready(void);
int post_fail, init_fail, posts, starts, stops, hw_class, violation, masked;
struct { int goto_poweroff_flag; } app_var;
int get_power_on_status(void) { return on; }
int get_charge_online_flag(void) { return vbus; }
int usb_otg_online(int id) { return role; }
int app_in_mode(int mode) { return pc; }
int rdx_app_business_started(void) { return business; }
int rdx_dip_switch_business_blocked(void) { return blocked; }
int rdx_dip_switch_shutdown_deferred(void) { return deferred; }
int rdx_dut_factory_usb_ready(void) {
    return dut_ready && on && business && !app_var.goto_poweroff_flag && !blocked && !deferred;
}
int rdx_dip_switch_pc_allowed(void) { return !on && vbus && role == SLAVE_MODE && !business && !app_var.goto_poweroff_flag; }
int pc_storage_usb_ready(void) { return host_owned && pc; }
void usb_stop(usb_dev id);
int usb_message_to_stack(int msg, void *arg, int sync) {
    ++posts;
    if (sync) violation=10;
    if (post_fail) return -1;
    if (sync && msg == USBSTACK_STOP) usb_stop(0);
    return 0;
}
void local_irq_disable(void) {}
void local_irq_enable(void) {}
void usb_write_intr_usbe(usb_dev id, int value) { masked=1; }
void usb_clr_intr_txe(usb_dev id, int value) {}
void usb_clr_intr_rxe(usb_dev id, int value) {}
void usb_sie_disable(usb_dev id) {}
void usb_sie_close(usb_dev id) {}
int usb_device_mode(usb_dev id, u32 cls) {
    if (cls) {
        if (hw_class) violation=1;
        if (init_fail) return -1;
        hw_class=cls; ++starts; masked=0;
    } else {
        if (hw_class && !masked) violation=2;
        hw_class=0; ++stops;
    }
    return 0;
}
void msd_register_disk(const char *name, void *p) { if (hw_class != MASSSTORAGE_CLASS) violation=3; }
void usb_msd_wakeup(void) {}
void usb_msd_reset_wakeup(void) {}
int msd_set_wakeup_handle(void (*p)(void)) { return 1; }
void msd_set_reset_wakeup_handle(void (*p)(void)) {}
void msd_unregister_all(void) {}
'''

TESTS = r'''
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
void boot(void) {
    dut_ready=1;
    on=vbus=role=business=1; pc=blocked=deferred=host_owned=0;
    post_fail=init_fail=posts=starts=stops=hw_class=violation=masked=0;
    app_var.goto_poweroff_flag=0;
    factory_cdc_requested=factory_usb_state=factory_usb_shutdown=0;
}
int test_all(void) {
    boot(); dut_ready=0;
    usb_factory_service(); usb_cdc_background_run(0);
    CHECK(!hw_class && !posts); /* normal boot is invisible */
    dut_ready=1; usb_factory_service(); usb_cdc_background_run(0);
    CHECK(hw_class==CDC_CLASS); /* key entry with cable already attached */
    dut_ready=0; usb_cdc_background_run(0); CHECK(!hw_class); /* exit before policy tick */
    boot(); usb_factory_service(); dut_ready=0;
    usb_cdc_background_run(0); CHECK(!hw_class); /* queued start after exit */
    boot(); vbus=0; usb_factory_service(); usb_cdc_background_run(0); CHECK(!hw_class);
    vbus=1; usb_factory_service(); usb_cdc_background_run(0); CHECK(hw_class==CDC_CLASS);
    boot(); business=0;
    CHECK(usb_cdc_background_standby(0)==0 && !hw_class);
    usb_factory_service(); CHECK(posts==0);
    business=1; usb_factory_service(); CHECK(posts==1 && !hw_class);
    usb_cdc_background_run(0); CHECK(hw_class==CDC_CLASS && starts==1);
    usb_factory_service(); usb_cdc_background_run(0);
    CHECK(starts==1 && !violation);
    on=0; usb_cdc_background_run(0); CHECK(!hw_class);
    usb_start(0); CHECK(!hw_class); /* warm runtime must not export */
    boot(); on=0; business=0; pc=1;
    usb_start(0); CHECK(!hw_class); /* no SD takeover acknowledgment */
    host_owned=1; usb_start(0); CHECK(hw_class==MASSSTORAGE_CLASS);
    usb_start(0); CHECK(starts==1);
    CHECK(usb_cdc_background_standby(0)==0 && starts==1 && hw_class==MASSSTORAGE_CLASS);
    on=1; usb_factory_service(); usb_cdc_background_run(0);
    CHECK(hw_class==MASSSTORAGE_CLASS); /* cannot replace live MSC */
    usb_stop(0); pc=0; host_owned=0; blocked=1; business=1;
    usb_factory_service(); usb_cdc_background_run(0); CHECK(!hw_class);
    blocked=0; usb_factory_service(); usb_cdc_background_run(0);
    CHECK(hw_class==CDC_CLASS && !violation);
    boot(); usb_factory_service(); on=0;
    usb_cdc_background_run(0); CHECK(!hw_class); /* stale request */
    boot(); usb_factory_service(); role=0;
    usb_cdc_background_run(0); CHECK(!hw_class);
    boot(); usb_factory_service(); vbus=0;
    usb_cdc_background_run(0); CHECK(!hw_class);
    boot(); post_fail=1; usb_factory_service(); CHECK(posts==1 && !hw_class);
    post_fail=0; usb_factory_service(); CHECK(posts==2);
    init_fail=1; usb_cdc_background_run(0); CHECK(!usb_factory_cdc_started());
    init_fail=0; usb_factory_service(); usb_cdc_background_run(0);
    CHECK(usb_factory_cdc_started());
    blocked=1; usb_factory_service(); usb_cdc_background_run(0); CHECK(!hw_class);
    boot(); usb_factory_service(); usb_cdc_background_run(0);
    deferred=1; usb_cdc_background_run(0); CHECK(!hw_class);
    boot(); usb_factory_service(); usb_cdc_background_run(0);
    post_fail=1; usb_factory_shutdown(); CHECK(hw_class==CDC_CLASS);
    post_fail=0; usb_factory_shutdown();
    usb_factory_shutdown_process(); CHECK(!hw_class);
    usb_factory_service(); usb_cdc_background_run(0);
    CHECK(!hw_class && !violation); /* shutdown is latched */
    boot(); vbus=role=0;
    usb_factory_shutdown(); CHECK(!posts && !stops);
    usb_factory_shutdown_process(); CHECK(!posts && !stops);
    usb_cdc_background_run(0); CHECK(!hw_class);
    boot(); usb_factory_service(); /* start queued before shutdown */
    usb_factory_shutdown(); usb_cdc_background_run(0);
    CHECK(!hw_class && !stops);
    boot(); on=business=0; pc=host_owned=1;
    usb_start(0); CHECK(hw_class==MASSSTORAGE_CLASS);
    int old_stops=stops;
    usb_factory_shutdown(); usb_factory_shutdown_process();
    CHECK(hw_class==MASSSTORAGE_CLASS && stops==old_stops);
    boot(); usb_factory_service(); usb_cdc_background_run(1); CHECK(!hw_class);
    return 0;
}
'''


def main():
    source = (ROOT / 'SDK/apps/common/device/usb/device/task_pc.c').read_text(encoding='utf-8')
    policy = source[source.index('enum { FACTORY_USB_NONE'):source.index('\n\n\n#if TCFG_USB_SLAVE_MSD_ENABLE')]
    # Remove the outer factory-config closing directive; keep its real body.
    policy = policy[:policy.rindex('#endif')]
    functions = source[source.index('void usb_start('):source.index('int pc_device_event_handler(')]
    audit = ROOT / 'cache/factory-usb-test'
    audit.mkdir(parents=True, exist_ok=True)
    c_path, ir_path = audit / 'test.c', audit / 'test.ll'
    dut = (ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_dut.c').read_text(encoding='utf-8')
    key_entry = dut[dut.index('static volatile u8 factory_key_dut_ready;'):dut.index('static void rdx_dut_motor_timer_cb(void *priv);')]
    lifecycle_stubs = r'''
#define DUT_LOG(...) ((void)0)
int dut_mode, key_disabled, entry_fail;
int rdx_dut_is_in_mode(void) { return dut_mode; }
int rdx_dut_is_key_dut_disabled(void) { return key_disabled; }
void rdx_dut_msg_handle(void) { if (!entry_fail) dut_mode = !dut_mode; }
'''
    lifecycle_tests = r'''
int cleanup_saw_ready, delayed_poweroff;
void rdx_dut_close_current_func(void) { cleanup_saw_ready = factory_key_dut_ready; }
#define RDX_LED_SCENE_DUT_EXIT 0
void rdx_led_ctrl_set_scene(int scene) {}
void rdx_app_normal_poweroff(void) {}
int sys_timeout_add(void *p, void (*cb)(void *), int delay) { delayed_poweroff=1; return 0; }
POWER_OFF_BODY
int test_key_entry(void) {
    boot(); factory_key_dut_ready=0; dut_mode=key_disabled=entry_fail=0;
    entry_fail=1; rdx_dut_key_mode_handle(); CHECK(!rdx_dut_factory_usb_ready());
    entry_fail=0; key_disabled=1; rdx_dut_key_mode_handle(); CHECK(!dut_mode);
    key_disabled=0; on=0; rdx_dut_key_mode_handle(); CHECK(!dut_mode);
    on=1; blocked=1; rdx_dut_key_mode_handle(); CHECK(!dut_mode);
    blocked=0; rdx_dut_msg_handle(); CHECK(dut_mode && !rdx_dut_factory_usb_ready());
    rdx_dut_msg_handle(); rdx_dut_key_mode_handle();
    CHECK(dut_mode && rdx_dut_factory_usb_ready());
    business=0; CHECK(!rdx_dut_factory_usb_ready()); business=1;
    dut_mode=0; CHECK(!rdx_dut_factory_usb_ready()); dut_mode=1;
    rdx_dut_poweroff();
    CHECK(!factory_key_dut_ready && !dut_mode && !cleanup_saw_ready && delayed_poweroff);
    dut_mode=1; CHECK(!rdx_dut_factory_usb_ready()); /* no revival if shutdown never completes */
    return 0;
}
'''
    poweroff = dut[dut.index('void rdx_dut_poweroff(void)'):]
    poweroff = poweroff[:poweroff.index('\n}') + 2]
    lifecycle_tests = lifecycle_tests.replace('POWER_OFF_BODY', poweroff.replace('rdx_dut_info.dut_mode', 'dut_mode'))
    for name in ['void rdx_dut_poweroff(void)' , 'void rdx_dut_msg_handle(void)']:
        body = dut[dut.index(name):]
        assert body.index('rdx_dut_factory_usb_revoke();') < body.index('rdx_dut_info.dut_mode = FALSE;')
    app = (ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_app.c').read_text(encoding='utf-8')
    assert 'rdx_dut_key_mode_handle();' in app
    # Execute the real poweroff gate with a delayed USB worker, not a synchronous stub.
    gates = []
    for filename in ('poweroff.c', 'tws_poweroff.c'):
        text = (ROOT / 'SDK/apps/earphone/mode/bt' / filename).read_text(encoding='utf-8')
        start = text.index('    extern void usb_factory_shutdown(void);')
        gates.append(text[start:text.index('#endif', start)])
    assert gates[0] == gates[1], 'TWS and non-TWS shutdown gates diverged'
    gate_tests = r'''
static int gate_continued;
static void real_poweroff_gate(int reason) {
GATE_BODY
    ++gate_continued;
}
int test_poweroff_gate(void) {
    boot(); vbus=role=0; gate_continued=0;
    real_poweroff_gate(1);
    CHECK(gate_continued==1 && !posts && !stops && !violation);
    boot(); usb_factory_service(); usb_cdc_background_run(0);
    gate_continued=0;
    real_poweroff_gate(0); /* worker never acknowledges */
    CHECK(gate_continued==1 && hw_class==CDC_CLASS);
    boot(); usb_factory_service(); usb_cdc_background_run(0);
    gate_continued=0; post_fail=1;
    real_poweroff_gate(1); /* queue full */
    CHECK(gate_continued==1 && factory_usb_shutdown && !violation);
    return 0;
}
'''.replace('GATE_BODY', gates[0])
    c_path.write_text(STUBS + policy + functions + TESTS + lifecycle_stubs +
                     key_entry.replace('rdx_dut_factory_usb_ready', 'real_factory_usb_ready') +
                     lifecycle_tests.replace('rdx_dut_factory_usb_ready', 'real_factory_usb_ready') + gate_tests, encoding='utf-8')
    result = subprocess.run(['C:/JL/pi32/bin/clang.exe', '-target', 'pi32v2', '-mcpu=r3',
                             '-O0', '-S', '-emit-llvm', str(c_path), '-o', str(ir_path)],
                            capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr)
    ir = re.sub(r' "target-(?:cpu|features)"="[^"]*"', '', ir_path.read_text(encoding='utf-8'))
    llvm.initialize_native_target()
    llvm.initialize_native_asmprinter()
    target = llvm.Target.from_default_triple().create_target_machine()
    module = llvm.parse_assembly(ir)
    module.triple = llvm.get_default_triple()
    module.data_layout = str(target.target_data)
    module.verify()
    engine = llvm.create_mcjit_compiler(module, target)
    engine.finalize_object()
    result = ctypes.CFUNCTYPE(ctypes.c_int)(engine.get_function_address('test_all'))()
    assert result == 0, f'Factory USB scenario failed at generated C line {result}: {c_path}'
    result = ctypes.CFUNCTYPE(ctypes.c_int)(engine.get_function_address('test_key_entry'))()
    assert result == 0, f'Key DUT admission failed at generated C line {result}'
    result = ctypes.CFUNCTYPE(ctypes.c_int)(engine.get_function_address('test_poweroff_gate'))()
    assert result == 0, f'Poweroff gate failed at generated C line {result}'
    print('PASS: shutdown continues without USB, worker ACK or queue capacity; MSC preserved; TWS matches')
    print('PASS: real key entry completion, failed entry, disabled key, OFF/storage rejection, BLE cannot grant CDC')
    print('PASS: actual factory USB C: SD ownership, stale requests, retry, idempotence, IRQ-before-release and shutdown')
    check_final_config(audit)


def check_final_config(audit):
    """Use the real SDK include tree and build defines, including tool conflicts."""
    makefile = (ROOT / 'SDK/Makefile').read_text(encoding='utf-8')

    def words(name):
        lines = makefile.splitlines()
        start = next(i for i, line in enumerate(lines) if line.startswith(name + ' :='))
        text = lines[start].split(':=', 1)[1]
        i = start
        while text.rstrip().endswith('\\'):
            i += 1
            text = text.rstrip()[:-1] + ' ' + lines[i]
        return text.replace('$(SYS_INC_DIR)', 'C:/JL/pi32/pi32v2-include').split()

    base = ['C:/JL/pi32/bin/clang.exe'] + words('CFLAGS') + words('DEFINES') + words('INCLUDES')
    for enabled, comm in [(0, None), (1, None), (1, 1), (1, 3)]:
        overrides = '' if comm is None else (
            '#include "sdk_config.h"\n#undef TCFG_CFG_TOOL_ENABLE\n#define TCFG_CFG_TOOL_ENABLE 1\n'
            f'#undef TCFG_COMM_TYPE\n#define TCFG_COMM_TYPE {comm}\n')
        path = audit / f'config-{enabled}-{comm}.c'
        path.write_text(overrides + '#include "app_config.h"\n#include "usb/otg.h"\n' +
                        '#if TCFG_T2620_FACTORY_USB_CDC_ENABLE && !(TCFG_OTG_MODE & OTG_SLAVE_MODE)\n'
                        '#error "missing OTG slave"\n#endif\n', encoding='utf-8')
        result = subprocess.run(base + [f'-DTCFG_T2620_FACTORY_USB_CDC_ENABLE={enabled}',
                                       '-E', '-dM', str(path)], cwd=ROOT / 'SDK',
                                capture_output=True, text=True)
        (audit / f'config-{enabled}-{comm}.log').write_text(result.stdout + result.stderr, encoding='utf-8')
        if comm is not None:
            assert result.returncode and 'conflicts with the online configuration tool' in result.stderr
        else:
            assert result.returncode == 0, result.stderr
            macros = dict(re.findall(r'^#define (\w+) (.*)$', result.stdout, re.M))
            assert macros['TCFG_USB_SLAVE_MSD_ENABLE'] == '1'
            assert macros['TCFG_USB_SLAVE_CDC_ENABLE'] == str(enabled)
            assert macros['TCFG_APP_PC_EN'] == '1'
            if enabled:
                assert macros['CDC_DATA_EP_IN'] == '1' and macros['CDC_DATA_EP_OUT'] == '1'
                assert macros['CDC_INTR_EP_IN'] == '2' and macros['CDC_INTR_EP_ENABLE'] == '1'
    print('PASS: actual SDK preprocessing: CDC off/on, MSC retained, endpoint layout, UART/USB tool conflicts rejected')


if __name__ == '__main__':
    main()
