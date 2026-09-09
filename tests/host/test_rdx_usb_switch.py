"""Execute the actual RDX switch coordinator with deterministic hardware stubs.

Requires JL clang and llvmlite. No device access, firmware build or flashing.
"""
import ctypes
import re
import subprocess
from pathlib import Path

from llvmlite import binding as llvm

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "SDK/apps/common/third_party_profile/rdx_protocol/rdx_dip_switch.c"
STUBS = r'''
typedef unsigned char u8;
typedef unsigned int u32;
typedef int s32;
#define RDX_WIFI_ENABLE 0
#define TCFG_RDX_LOCAL_PLAYBACK_ENABLE 0
#define RDX_LED_SCENE_USB_SWITCH_WAIT 1
#define RDX_LED_SCENE_USB_SWITCH_FAILED 2
#define r_printf(...) ((void)0)
int mock_on=1, mock_business=1, mock_refresh=0, mock_error=0;
int mock_ota=0, mock_format=0, mock_dut=0, mock_ble=0;
int mock_record_post=0, mock_record=-2, mock_file_post=0, mock_file=-2;
int resets=0, record_posts=0, file_posts=0, advertising=0, led=0;
u32 now=100;
struct { int goto_poweroff_flag; } app_var;
static int s_business_mode_entered=1;
int get_power_on_status(void) { return mock_on; }
int rdx_app_business_started(void) { return mock_business; }
int rdx_uxfile_pc_refresh_status(void) { return mock_refresh; }
int rdx_uxfile_storage_status(void) { return mock_error; }
void rdx_uxfile_pc_returned(void) { mock_refresh=1; }
u8 get_ota_status(void) { return mock_ota; }
int rdx_uxfile_is_formatting(void) { return mock_format; }
int rdx_app_get_dut_status(void) { return mock_dut; }
void sys_auto_shut_down_disable(void) {}
void rdx_app_emmc_poweroff_check_timer_stop(void) {}
void rdx_app_wifi_handle(u8 cmd) {}
void rdx_ble_server_adv_enable(int value) { advertising+=value; }
void rdx_led_ctrl_restore_system_state(void) {}
void rdx_led_ctrl_set_scene(int scene) { led=scene; }
u32 jiffies_msec(void) { return now; }
int rdx_record_usb_quiesce_request(u32 t) { ++record_posts; return mock_record_post; }
int rdx_record_usb_quiesce_poll(u32 t) { return mock_record; }
int rdx_ble_server_usb_quiesce(void) { return mock_ble; }
int rdx_uxfile_fence_request(u32 t) { ++file_posts; return mock_file_post; }
int rdx_uxfile_fence_poll(u32 t) { return mock_file; }
void rdx_cpu_reset(void) { ++resets; }
'''


def main():
    source = SOURCE.read_text(encoding="utf-8")
    begin = source.index("enum { USB_SWITCH_IDLE")
    end = source.index("\n#endif\n\nvoid rdx_dip_switch_note_business_mode", begin)
    extracted = source[begin:end]
    audit = ROOT / "output/phase2b-audit"
    audit.mkdir(parents=True, exist_ok=True)
    c_path = audit / "switch-test.c"
    ir_path = audit / "switch-test.ll"
    c_path.write_text(STUBS + extracted +
                      "\nint test_tick(int on, int vbus) { return rdx_usb_switch_service(on, vbus); }\n",
                      encoding="utf-8")
    subprocess.run(["C:/JL/pi32/bin/clang.exe", "-target", "pi32v2", "-mcpu=r3",
                    "-O0", "-S", "-emit-llvm", str(c_path), "-o", str(ir_path)],
                   check=True, capture_output=True, text=True)
    ir = ir_path.read_text(encoding="utf-8")
    ir = re.sub(r' "target-(?:cpu|features)"="[^"]*"', '', ir)
    llvm.initialize_native_target()
    llvm.initialize_native_asmprinter()
    target = llvm.Target.from_default_triple().create_target_machine()
    module = llvm.parse_assembly(ir)
    module.triple = llvm.get_default_triple()
    module.data_layout = str(target.target_data)
    module.verify()
    engine = llvm.create_mcjit_compiler(module, target)
    engine.finalize_object()

    def fn(name, args=()):
        address = engine.get_function_address(name)
        assert address, name
        return ctypes.CFUNCTYPE(ctypes.c_int, *args)(address)

    def setv(name, value, byte=False):
        address = engine.get_global_value_address(name)
        assert address, name
        typ = ctypes.c_ubyte if byte else ctypes.c_int
        typ.from_address(address).value = value

    def getv(name):
        return ctypes.c_int.from_address(engine.get_global_value_address(name)).value

    tick_fn = fn("test_tick", (ctypes.c_int, ctypes.c_int))
    blocked = fn("rdx_dip_switch_business_blocked")
    deferred = fn("rdx_dip_switch_shutdown_deferred")

    def tick(on=0, vbus=1):
        setv("mock_on", on)
        return tick_fn(on, vbus)

    def boot():
        for name in ("s_usb_switch", "s_wait_busy", "s_pc_refresh_pending"):
            setv(name, 0, byte=True)
        for name in ("mock_refresh", "mock_error", "mock_ota", "mock_format", "mock_dut",
                     "mock_ble", "mock_record_post", "mock_file_post", "resets",
                     "record_posts", "file_posts", "advertising", "s_refresh_previous"):
            setv(name, 0)
        setv("mock_record", -2)
        setv("mock_file", -2)
        setv("now", 100)

    boot()
    assert tick() == 1 and blocked() and deferred()
    assert getv("record_posts") == 1 and getv("file_posts") == 0
    setv("mock_record", 0)
    tick()
    assert getv("file_posts") == 0, "record completion alone is insufficient"
    setv("mock_ble", 1)
    tick()
    assert getv("file_posts") == 1 and getv("resets") == 0
    tick(on=1, vbus=0)
    assert getv("resets") == 0, "changed input cannot skip the file fence"
    setv("mock_file", 0)
    tick(on=1, vbus=0)
    assert getv("resets") == 1

    for fault in ("mock_record_post", "mock_record", "mock_file_post", "mock_file", "mock_error"):
        boot()
        setv("mock_record", 0)
        setv("mock_ble", 1)
        setv(fault, -1)
        tick()
        tick()
        assert getv("resets") == 0 and getv("led") == 2 and blocked(), fault
    boot()
    tick()
    setv("now", 30101)
    tick(vbus=0)
    assert getv("resets") == 0 and getv("led") == 2 and deferred()

    for busy in ("mock_ota", "mock_format", "mock_dut"):
        boot()
        setv(busy, 1)
        tick()
        assert getv("record_posts") == 0 and deferred() and not blocked()
        tick(on=1)
        assert not deferred(), "ON must cancel the pre-drain wait"
    boot()
    setv("s_pc_refresh_pending", 1, byte=True)
    tick(on=1)
    tick(on=1)
    assert getv("advertising") == 1, "fast refresh completion must not lose advertising restart"
    setv("mock_refresh", -1)
    assert blocked()
    print("PASS: actual coordinator: ordered acknowledgments, changed input/unplug, "
          "all failure gates, timeout, OTA/format/DUT wait cancellation and fast refresh")


if __name__ == "__main__":
    main()
