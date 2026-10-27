"""验证原生睡眠时钟配置与实际板级初始化，不替代射频和功耗板测。"""
import re
import subprocess
import tempfile
from pathlib import Path

from host_c_test_lib import ROOT, run_c_checks

CONFIG = ROOT / 'SDK/apps/earphone/include/t2620_project_config.h'
SDK_CONFIG = ROOT / 'SDK/apps/earphone/board/br28/sdk_config.h'
POWER = ROOT / 'SDK/cpu/br28/power/power_config.c'
API = ROOT / 'SDK/interface/driver/cpu/br28/asm/power/power_api.h'
RDX_APP = ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_app.c'
SWITCH = 'TCFG_T2620_LOWPOWER_LRC_ENABLE'
LAB_SWITCHES = ('TCFG_T2620_IDLE_PROBE_ENABLE',
                'TCFG_T2620_IDLE_PROBE_UART_OFF', 'TCFG_T2620_IDLE_PROBE_LRC')


def config_source(overrides=None):
    source = f'#include "{SDK_CONFIG.as_posix()}"\n'
    for key, value in (overrides or {}).items():
        source += f'#undef {key}\n#define {key} {value}\n'
    return source + f'#include "{CONFIG.as_posix()}"\n'


def preprocess(overrides=None, error=None):
    result = subprocess.run(
        ['C:/JL/pi32/bin/clang.exe', '-E', '-dM', '-x', 'c', '-'],
        input=config_source(overrides), capture_output=True, text=True, timeout=60)
    if error:
        assert result.returncode and error in result.stderr, result.stderr
        return None
    assert result.returncode == 0, result.stderr
    return dict(re.findall(r'^#define (\S+)[ \t]*(.*?)$', result.stdout, re.M))


def check_configuration():
    baseline = preprocess({SWITCH: 0})
    trial = preprocess({SWITCH: 1})
    assert baseline['TCFG_LOWPOWER_OSC_TYPE'] == 'OSC_TYPE_BT_OSC'
    assert trial['TCFG_LOWPOWER_OSC_TYPE'] == 'OSC_TYPE_LRC'
    # 工具配置与项目覆盖的全部宏中，只允许产品开关和睡眠时钟类型不同。
    changed = {k for k in baseline.keys() | trial.keys() if baseline.get(k) != trial.get(k)}
    assert changed == {SWITCH, 'TCFG_LOWPOWER_OSC_TYPE'}, changed
    assert preprocess() == trial, 'The accepted low-power baseline must enable standalone LRC'
    assert trial['TCFG_DEBUG_UART_ENABLE'] == '1'
    assert all(key not in trial for key in LAB_SWITCHES)

    for enabled in (0, 1):
        for native in (0, 1, 3):
            preprocess({SWITCH: enabled, 'TCFG_LOWPOWER_LOWPOWER_SEL': native},
                       'requires native low power' if enabled and not native else None)
        for key in LAB_SWITCHES:
            preprocess({SWITCH: enabled, key: 0})
            for value in (-1, 1, 2):
                preprocess({SWITCH: enabled, key: value}, 'idle probe is not supported')
    for invalid in (-1, 2):
        preprocess({SWITCH: invalid}, 'switch must be 0 or 1')
    # 关闭项目覆盖后跟随工具配置，不硬编码另一套时钟选择。
    assert preprocess({SWITCH: 0, 'TCFG_LOWPOWER_OSC_TYPE': 'OSC_TYPE_LRC'})[
        'TCFG_LOWPOWER_OSC_TYPE'] == 'OSC_TYPE_LRC'


BOOT_STUBS = r'''
static int gpio_calls, power_calls, key_calls, mode_calls, snapshots, invalid;
static int default_reads, selected_reads;
static unsigned int selected_clock;
static unsigned int observed_product, observed_cfg, observed_default, observed_selected;
void gpio_config_init(void) { ++gpio_calls; }
void power_init(const struct low_power_param *param) {
    if (gpio_calls != 1 || power_calls || param->osc_type != TCFG_LOWPOWER_OSC_TYPE ||
        param->config != TCFG_LOWPOWER_LOWPOWER_SEL || param->btosc_hz != TCFG_CLOCK_OSC_HZ ||
        param->vddiom_lev != TCFG_LOWPOWER_VDDIOM_LEVEL || param->btosc_disable ||
        param->lpctmu_en != TCFG_LP_TOUCH_KEY_ENABLE) ++invalid;
    ++power_calls;
}
u8 low_power_get_default_osc_type(void) {
    if (power_calls != 1) ++invalid;
    ++default_reads; return TCFG_LOWPOWER_OSC_TYPE;
}
u8 low_power_get_osc_type(void) {
    if (power_calls != 1) ++invalid;
    ++selected_reads; return selected_clock;
}
void key_wakeup_init(void) { if (power_calls != 1) ++invalid; ++key_calls; }
void power_set_mode(int mode) {
    if (mode != TCFG_LOWPOWER_POWER_SEL || key_calls != 1) ++invalid;
    ++mode_calls;
}
static int test_puts(const char *s) { return 0; }
static int test_printf(const char *s, unsigned int product, unsigned int cfg,
                       unsigned int def, unsigned int selected) {
    observed_product=product; observed_cfg=cfg; observed_default=def; observed_selected=selected;
    ++snapshots; return 0;
}
#define puts test_puts
#define printf test_printf
'''

BOOT_TEST = r'''
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
int check_boot(void) {
    /* 原生选择可不同于配置默认值：只观察，不强制切换。 */
    for (selected_clock=0; selected_clock<=1; ++selected_clock) {
        gpio_calls=power_calls=key_calls=mode_calls=snapshots=invalid=0;
        default_reads=selected_reads=0;
        board_power_init();
        CHECK(!invalid && gpio_calls==1 && power_calls==1 && key_calls==1);
        CHECK(mode_calls==!TCFG_CHARGE_ENABLE);
        CHECK(snapshots==1 && default_reads==1 && selected_reads==1);
        CHECK(observed_product==TCFG_T2620_LOWPOWER_LRC_ENABLE);
        CHECK(observed_cfg==TCFG_LOWPOWER_OSC_TYPE && observed_default==observed_cfg);
        CHECK(observed_selected==selected_clock);
    }
    return 0;
}
'''


def check_boot_initialization():
    source = POWER.read_text(encoding='utf-8')
    source = re.sub(r'^#include[^\n]*\n', '', source, flags=re.M)
    api = API.read_text(encoding='utf-8')
    power_struct = re.search(r'struct low_power_param\s*\{.*?\};', api, re.S)[0]
    osc_enum = re.search(r'enum\s*\{\s*OSC_TYPE_LRC.*?\};', api, re.S)[0]
    types = ('typedef unsigned char u8;\ntypedef unsigned int u32;\n'
             '#define VDDIOM_VOL_30V 3\n#define PWR_DCDC15 1\n')
    with tempfile.TemporaryDirectory(prefix='t2620-lrc-') as temp:
        for lrc in (0, 1):
            for charge in (0, 1):
                path = Path(temp) / f'boot_{lrc}_{charge}.c'
                path.write_text(types + power_struct + '\n' + osc_enum + '\n' +
                                config_source({SWITCH: lrc, 'TCFG_CHARGE_ENABLE': charge}) +
                                BOOT_STUBS + source + BOOT_TEST, encoding='utf-8')
                run_c_checks(path, ['check_boot'])


def check_boot_clock_settle():
    # 启动刷新是固定行为：只在 RDX 初始化完成处无条件调用一次，且只走原生调频接口。
    source = RDX_APP.read_text(encoding='utf-8')
    init = re.search(r'\nvoid rdx_app_tasks_init\(void\)\n\{.*?\n\}', source, re.S)[0]
    tail = init[init.index('rdx_dut_init();'):init.index('rdx_app_init_flag = true;')]
    assert tail.count('clock_refurbish();') == 1 and '#if' not in tail, 'Boot settle is unconditional'
    assert source.count('clock_refurbish();') == 1
    assert not re.search(r'clk_set(_api)?\(\s*"sys"', tail)


if __name__ == '__main__':
    check_configuration()
    check_boot_initialization()
    check_boot_clock_settle()
    print('Native LRC configuration and board initialization checks passed.')
