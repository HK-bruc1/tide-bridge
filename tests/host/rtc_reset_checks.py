"""RTC factory-reset and hardware/VM consistency regression checks."""
import re
import tempfile
from pathlib import Path

from host_c_test_lib import ROOT, function, run_c_checks


BASE = ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol'


def read(name):
    return (BASE / name).read_text(encoding='utf-8')


def strip_includes(text):
    return re.sub(r'^#include[^\n]*', '', text, flags=re.M)


STUBS = r'''
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef unsigned long long size_t;
typedef int time_t;
typedef int bool;

#define true 1
#define false 0
#define NULL ((void *)0)
#define VM_RDX_RTC_INIT_VALUE 151
#define platform_uninitcall(fn)
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
#define g_printf(...) ((void)0)
#define y_printf(...) ((void)0)

struct sys_time {
    u16 year;
    u8 month;
    u8 day;
    u8 hour;
    u8 min;
    u8 sec;
};

struct _rtc_trim {
    u64 sfr_time_to_sec;
    u64 now_time_to_sec;
    u64 last_time_remain;
};

void *memset(void *, int, size_t);
void *memcpy(void *, const void *, size_t);
size_t strlen(const char *);
int sprintf(char *, const char *, ...);
int syscfg_read(u16, void *, u16);
int syscfg_write(u16, const void *, u16);
bool read_p11_sys_time(struct sys_time *, struct _rtc_trim *);
void rtc_read_time(struct sys_time *);
void rtc_write_time(const struct sys_time *);
void poweroff_save_rtc_time(void);
int sys_timer_add(void *, void (*)(void *), u32);
void sys_timer_del(u16);
unsigned long jiffies_msec(void);
int jiffies_msec2offset(unsigned long, unsigned long);
void cpu_reset(void);

const struct sys_time def_sys_time = {2000, 1, 1, 0, 0, 0};

static struct sys_time s_hw_time;
static struct sys_time s_p11_time;
static bool s_p11_valid;
static time_t s_vm_time;
static int s_vm_read_result;
static int s_vm_read_copy;
static int s_vm_write_result;
static int s_vm_writes;
static int s_vm_reads;
static int s_hw_reads;
static int s_hw_writes;
static int s_poweroff_saves;
static int s_invalidate_on_poweroff_save;
static int s_cpu_resets;
static int s_timer_id;
static unsigned long s_jiffies;

int syscfg_read(u16 id, void *buffer, u16 length)
{
    if (id != VM_RDX_RTC_INIT_VALUE) {
        return -1;
    }
    ++s_vm_reads;
    if (s_vm_read_copy) {
        memcpy(buffer, &s_vm_time, sizeof(s_vm_time));
    }
    return s_vm_read_result;
}

int syscfg_write(u16 id, const void *buffer, u16 length)
{
    if (id != VM_RDX_RTC_INIT_VALUE || length != sizeof(s_vm_time)) {
        return -1;
    }
    ++s_vm_writes;
    if (s_vm_write_result == length) {
        memcpy(&s_vm_time, buffer, sizeof(s_vm_time));
    }
    return s_vm_write_result;
}

bool read_p11_sys_time(struct sys_time *time_value, struct _rtc_trim *trim)
{
    memset(trim, 0, sizeof(*trim));
    if (s_p11_valid) {
        *time_value = s_p11_time;
    }
    return s_p11_valid;
}

void rtc_read_time(struct sys_time *time_value)
{
    ++s_hw_reads;
    *time_value = s_hw_time;
}

void rtc_write_time(const struct sys_time *time_value)
{
    ++s_hw_writes;
    s_hw_time = *time_value;
}

void poweroff_save_rtc_time(void)
{
    ++s_poweroff_saves;
    if (s_invalidate_on_poweroff_save) {
        memset(&s_hw_time, 0, sizeof(s_hw_time));
    }
}

int sys_timer_add(void *priv, void (*callback)(void *), u32 interval)
{
    (void)priv;
    (void)callback;
    (void)interval;
    s_timer_id = 1;
    return s_timer_id;
}

void sys_timer_del(u16 id)
{
    if (id == s_timer_id) {
        s_timer_id = 0;
    }
}

unsigned long jiffies_msec(void)
{
    return s_jiffies;
}

int jiffies_msec2offset(unsigned long begin, unsigned long now)
{
    return (int)(now - begin);
}

void cpu_reset(void)
{
    ++s_cpu_resets;
}
'''


COMMON_CHECKS = r'''
static void set_sys_time(struct sys_time *dst, time_t timestamp)
{
    DateTime dt;

    dt = rdx_rtc_timestamp_to_datetime(timestamp);
    dst->year = (u16)dt.year;
    dst->month = (u8)dt.month;
    dst->day = (u8)dt.day;
    dst->hour = (u8)dt.hour;
    dst->min = (u8)dt.minute;
    dst->sec = (u8)dt.second;
}

static time_t sys_time_to_timestamp(const struct sys_time *value)
{
    DateTime dt;

    dt.year = value->year;
    dt.month = value->month;
    dt.day = value->day;
    dt.hour = value->hour;
    dt.minute = value->min;
    dt.second = value->sec;
    dt.weekday = 0;
    dt.weekday_name = NULL;
    return rdx_rtc_datetime_to_timestamp(dt);
}

static void seed(time_t hardware_timestamp)
{
    s_p11_valid = false;
    s_p11_time = s_hw_time;
    s_vm_time = 0;
    s_vm_read_result = sizeof(s_vm_time);
    s_vm_read_copy = sizeof(s_vm_time);
    s_vm_write_result = sizeof(s_vm_time);
    s_vm_writes = 0;
    s_vm_reads = 0;
    s_hw_reads = 0;
    s_hw_writes = 0;
    s_poweroff_saves = 0;
    s_invalidate_on_poweroff_save = 0;
    s_cpu_resets = 0;
    s_timer_id = 0;
    s_jiffies = 0;
    rdx_rtc_boot_time_valid = 0;
    rdx_rtc_initialized = 0;
    set_sys_time(&s_hw_time, hardware_timestamp);
}
'''


HARDWARE_CHECKS = r'''
static const time_t default_ts = 946684800; /* 2000-01-01 00:00:00 */
static const time_t synced_ts = 1793239603;  /* APP *APP#rtc# log value */

static void seed_vm(time_t timestamp, int read_result, int copy_bytes)
{
    s_vm_time = timestamp;
    s_vm_read_result = read_result;
    s_vm_read_copy = copy_bytes;
}

static void seed_p11(time_t timestamp, bool valid)
{
    s_p11_valid = valid;
    set_sys_time(&s_p11_time, timestamp);
}

int check_stale_p11_cannot_mask_default_hardware(void)
{
    seed(default_ts);
    seed_p11(synced_ts, true);
    seed_vm(synced_ts, sizeof(s_vm_time), sizeof(s_vm_time));

    rdx_rtc_init();

    CHECK(sys_time_to_timestamp(&s_hw_time) == synced_ts);
    CHECK(s_hw_writes == 1);
    CHECK(rdx_rtc_get() == synced_ts);
    seed_p11(default_ts + 2, true);
    set_sys_time(&s_hw_time, synced_ts + 2);
    CHECK(rdx_rtc_get() == synced_ts + 2);
    CHECK(s_hw_writes == 1);
    return 0;
}

int check_running_hardware_is_not_rewritten(void)
{
    seed(synced_ts);
    seed_p11(default_ts, true);
    seed_vm(synced_ts, sizeof(s_vm_time), sizeof(s_vm_time));

    rdx_rtc_init();

    CHECK(s_hw_writes == 0);
    CHECK(sys_time_to_timestamp(&s_hw_time) == synced_ts);
    CHECK(rdx_rtc_get() == synced_ts);

    seed(synced_ts);
    seed_p11(0, false);
    seed_vm(synced_ts, sizeof(s_vm_time), sizeof(s_vm_time));

    rdx_rtc_init();

    CHECK(s_hw_writes == 0);
    CHECK(rdx_rtc_get() == synced_ts);
    return 0;
}

int check_empty_vm_does_not_invent_time(void)
{
    seed(default_ts);
    seed_p11(0, false);
    seed_vm(0, -1, 0);

    rdx_rtc_init();

    CHECK(s_hw_writes == 0);
    CHECK(sys_time_to_timestamp(&s_hw_time) == default_ts);
    CHECK(rdx_rtc_get() == default_ts);
    return 0;
}

int check_set_and_get_use_live_hardware(void)
{
    seed(synced_ts);
    seed_p11(default_ts, true);
    seed_vm(0, sizeof(s_vm_time), sizeof(s_vm_time));

    rdx_rtc_init();
    CHECK(!rdx_rtc_set_timestamp(synced_ts + 60));
    CHECK(sys_time_to_timestamp(&s_hw_time) == synced_ts + 60);
    CHECK(s_vm_time == synced_ts + 60);
    CHECK(rdx_rtc_get() == synced_ts + 60);
    return 0;
}

int check_short_vm_read_is_rejected(void)
{
    seed(default_ts);
    seed_p11(0, false);
    seed_vm(synced_ts, 1, sizeof(s_vm_time));

    rdx_rtc_init();

    CHECK(s_hw_writes == 0);
    CHECK(sys_time_to_timestamp(&s_hw_time) == default_ts);
    CHECK(rdx_rtc_get() == default_ts);
    return 0;
}

int check_saved_time_survives_reboot(void)
{
    seed(synced_ts);
    seed_p11(0, false);
    seed_vm(0, sizeof(s_vm_time), sizeof(s_vm_time));

    rdx_rtc_init();
    CHECK(!rdx_rtc_set_timestamp(synced_ts + 120));
    rdx_rtc_store_timestamp();
    CHECK(s_vm_time == synced_ts + 120);

    set_sys_time(&s_hw_time, default_ts);
    seed_p11(0, false);
    rdx_rtc_boot_time_valid = 0;
    rdx_rtc_initialized = 0;
    rdx_rtc_init();

    CHECK(sys_time_to_timestamp(&s_hw_time) == synced_ts + 120);
    CHECK(rdx_rtc_get() == synced_ts + 120);
    return 0;
}

int check_reset_saves_before_hardware_reset(void)
{
    seed(synced_ts);
    seed_p11(default_ts, true);
    seed_vm(0, sizeof(s_vm_time), sizeof(s_vm_time));

    rdx_rtc_init();
    set_sys_time(&s_hw_time, synced_ts + 21);
    s_invalidate_on_poweroff_save = 1;
    rdx_cpu_reset();

    CHECK(s_vm_time == synced_ts + 21);
    CHECK(s_poweroff_saves == 1);
    CHECK(s_cpu_resets == 1);
    set_sys_time(&s_hw_time, default_ts);
    seed_p11(synced_ts + 19, true);
    rdx_rtc_initialized = rdx_rtc_boot_time_valid = 0;
    rdx_rtc_init();
    CHECK(rdx_rtc_get() == synced_ts + 21);
    return 0;
}

int check_reset_before_init_does_not_read_rtc(void)
{
    seed(synced_ts);
    seed_vm(synced_ts, sizeof(s_vm_time), sizeof(s_vm_time));

    rdx_cpu_reset();

    CHECK(s_hw_reads == 0);
    CHECK(s_vm_reads == 0);
    CHECK(s_vm_writes == 0);
    CHECK(s_poweroff_saves == 0);
    CHECK(s_cpu_resets == 1);
    return 0;
}

int check_reset_stores_vm_before_native_save(void)
{
    seed(0);
    seed_vm(0, sizeof(s_vm_time), sizeof(s_vm_time));
    set_sys_time(&s_hw_time, synced_ts);
    rdx_rtc_initialized = 1;
    s_invalidate_on_poweroff_save = 1;

    rdx_cpu_reset();

    CHECK(s_vm_time == synced_ts);
    CHECK(s_poweroff_saves == 1);
    CHECK(s_cpu_resets == 1);
    return 0;
}

int check_store_does_not_replace_vm_with_default(void)
{
    seed(default_ts);
    seed_vm(synced_ts, sizeof(s_vm_time), sizeof(s_vm_time));
    rdx_rtc_initialized = 1;

    rdx_rtc_store_timestamp();

    CHECK(s_vm_time == synced_ts);
    CHECK(s_vm_writes == 0);
    return 0;
}

int check_store_does_not_create_vm_from_invalid_hardware(void)
{
    seed(default_ts);
    seed_vm(0, sizeof(s_vm_time), sizeof(s_vm_time));
    s_hw_time.year = 0;
    rdx_rtc_initialized = 1;

    rdx_rtc_store_timestamp();

    CHECK(s_vm_time == 0);
    CHECK(s_vm_writes == 0);

    seed(default_ts);
    seed_vm(synced_ts, -1, 0);
    s_hw_time.year = 0;
    rdx_rtc_store_timestamp();
    CHECK(s_vm_time == synced_ts);
    CHECK(s_vm_writes == 0);
    return 0;
}
'''


SOFTWARE_CHECK = r'''
int check_software_path(void)
{
    seed(0);
    s_hw_time.year = 0;
    s_vm_time = 1793239603;
    s_vm_read_result = sizeof(s_vm_time);
    s_vm_read_copy = sizeof(s_vm_time);
    rdx_rtc_boot_time_valid = 0;

    rdx_rtc_init();

    CHECK(s_vm_time == 1793239603);
    CHECK(s_timer_id == 1);
    CHECK(rdx_rtc_get() == 1793239603);
    s_jiffies = 2500;
    CHECK(rdx_rtc_get() == 1793239605);
    return 0;
}

int check_software_reset_saves(void)
{
    seed(0);
    s_vm_time = 1793239603;
    s_vm_read_result = sizeof(s_vm_time);
    s_vm_read_copy = sizeof(s_vm_time);
    rdx_rtc_boot_time_valid = 0;

    rdx_rtc_init();
    s_jiffies = 0;
    rdx_cpu_reset();

    CHECK(s_vm_time == 1793239603);
    CHECK(s_poweroff_saves == 0);
    CHECK(s_cpu_resets == 1);
    return 0;
}
'''


POWEROFF_STUBS = r'''
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef int bool;

#define POWEROFF_NORMAL 0
#define POWEROFF_RESET 1
#define POWEROFF_POWER_KEEP 2
#define APP_MODE_IDLE 1
#define IDLE_MODE_PLAY_POWEROFF 1
#define APP_MSG_GOTO_MODE 2
#define SYS_DEFAULT_VOL 1
#define TCFG_CHARGE_ENABLE 0
#define DUT_AUDIO_IDLE 0
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
#define log_info(...) ((void)0)
#define printf(...) ((void)0)

struct app_var_t {
    int goto_poweroff_cnt;
    int music_volume;
};
struct app_var_t app_var;

int a2dp_player_runing(void);
int esco_player_runing(void);
int rdx_dut_speaker_state(void);
void lmp_hci_reset(void);
void os_time_dly(int);
void sys_timer_del(int);
void app_send_message(int, int);
void app_charge_power_off_keep_mode(void);
void cpu_reset(void);
void rdx_cpu_reset(void);

static int g_bt_detach_timer;
static int g_user_wait_timer;
static int s_a2dp_busy;
static int s_esco_busy;
static int s_rdx_dut_state;
static int s_native_resets;
static int s_rdx_resets;
static int s_app_messages;

int a2dp_player_runing(void) { return s_a2dp_busy; }
int esco_player_runing(void) { return s_esco_busy; }
int rdx_dut_speaker_state(void) { return s_rdx_dut_state; }
void lmp_hci_reset(void) {}
void os_time_dly(int ticks) { (void)ticks; }
void sys_timer_del(int timer) { (void)timer; }
void app_send_message(int message, int arg)
{
    (void)message;
    (void)arg;
    ++s_app_messages;
}
void app_charge_power_off_keep_mode(void) {}
void cpu_reset(void) { ++s_native_resets; }
void rdx_cpu_reset(void) { ++s_rdx_resets; }

static void seed_poweroff(void)
{
    app_var.goto_poweroff_cnt = 0;
    g_user_wait_timer = 0;
    s_a2dp_busy = 0;
    s_esco_busy = 0;
    s_rdx_dut_state = DUT_AUDIO_IDLE;
    s_native_resets = 0;
    s_rdx_resets = 0;
    s_app_messages = 0;
}
'''


POWEROFF_CHECKS = {
    True: r'''
int check_poweroff_rdx_reset(void)
{
    seed_poweroff();
    wait_exit_btstack_flag((void *)POWEROFF_RESET);
    CHECK(s_rdx_resets == 1 && s_native_resets == 0);
    return 0;
}

int check_poweroff_timeout_stays_native(void)
{
    seed_poweroff();
    s_a2dp_busy = 1;
    app_var.goto_poweroff_cnt = 200;
    wait_exit_btstack_flag((void *)POWEROFF_RESET);
    CHECK(s_rdx_resets == 0 && s_native_resets == 1);
    return 0;
}
''',
    False: r'''
int check_poweroff_rdx_reset(void)
{
    seed_poweroff();
    wait_exit_btstack_flag((void *)POWEROFF_RESET);
    CHECK(s_rdx_resets == 0 && s_native_resets == 1);
    return 0;
}

int check_poweroff_timeout_stays_native(void)
{
    seed_poweroff();
    s_a2dp_busy = 1;
    app_var.goto_poweroff_cnt = 200;
    wait_exit_btstack_flag((void *)POWEROFF_RESET);
    CHECK(s_rdx_resets == 0 && s_native_resets == 1);
    return 0;
}
''',
}


def check_poweroff_sources():
    for name in ('poweroff.c', 'tws_poweroff.c'):
        source = (
            ROOT / 'SDK/apps/earphone/mode/bt' / name
        ).read_text(encoding='utf-8')
        wait_exit = function(source, 'wait_exit_btstack_flag')
        for rdx_enabled in (True, False):
            code = (
                f'#define THIRD_PARTY_PROTOCOLS_SEL 1\n'
                f'#define RDX_EN {1 if rdx_enabled else 0}\n'
                + POWEROFF_STUBS
                + wait_exit
                + POWEROFF_CHECKS[rdx_enabled]
            )
            with tempfile.TemporaryDirectory(
                prefix='rtc-poweroff-'
            ) as tmp:
                path = Path(tmp) / f'{name}.c'
                path.write_text(code, encoding='utf-8')
                run_c_checks(
                    path,
                    [
                        'check_poweroff_rdx_reset',
                        'check_poweroff_timeout_stays_native',
                    ],
                    native=True,
                )


def build_source(path_sel, checks):
    header = strip_includes(read('rdx_rtc.h'))
    source = read('rdx_rtc.c')
    return (
        '#define RDX_RTC_PATH_SOFTWARE 0\n'
        '#define RDX_RTC_PATH_HARDWARE 1\n'
        f'#define RDX_RTC_PATH_SEL {path_sel}\n'
        + STUBS + header + strip_includes(source) + COMMON_CHECKS + checks
    )


def run():
    with tempfile.TemporaryDirectory(prefix='rtc-reset-') as tmp:
        path = Path(tmp) / 'hardware_checks.c'
        path.write_text(build_source(1, HARDWARE_CHECKS), encoding='utf-8')
        run_c_checks(
            path,
            [
                'check_stale_p11_cannot_mask_default_hardware',
                'check_running_hardware_is_not_rewritten',
                'check_empty_vm_does_not_invent_time',
                'check_set_and_get_use_live_hardware',
                'check_short_vm_read_is_rejected',
                'check_saved_time_survives_reboot',
                'check_reset_saves_before_hardware_reset',
                'check_reset_before_init_does_not_read_rtc',
                'check_reset_stores_vm_before_native_save',
                'check_store_does_not_replace_vm_with_default',
                'check_store_does_not_create_vm_from_invalid_hardware',
            ],
            native=True,
        )

        path = Path(tmp) / 'software_checks.c'
        path.write_text(build_source(0, SOFTWARE_CHECK), encoding='utf-8')
        run_c_checks(
            path,
            ['check_software_path', 'check_software_reset_saves'],
            native=True,
        )
    check_poweroff_sources()


if __name__ == '__main__':
    run()
    print('RTC reset: hardware/VM consistency and reboot persistence passed.')
