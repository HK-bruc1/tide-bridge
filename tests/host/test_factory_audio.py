"""Focused production CDC audio queue/file-worker checks, plus host frame validation."""
import re
import sys
from host_c_test_lib import ROOT, run_c_checks, load_cdc_tool

STUBS = r'''
typedef unsigned long long size_t;
typedef unsigned char bool;
typedef int FILE;
#define NULL ((void *)0)
void *memcpy(void *,const void *,size_t);
void *memset(void *,int,size_t);
size_t strlen(const char *);
char *strcpy(char *,const char *);
char *strstr(const char *,const char *);
int strcmp(const char *,const char *);
int strncmp(const char *,const char *,size_t);
int snprintf(char *,size_t,const char *,...);
typedef unsigned char u8;
typedef unsigned int u32;
typedef int OS_SEM;
int rdx_cdc_source_shutdown(void) { return 0; }
#define TCFG_T2620_FACTORY_USB_CDC_ENABLE 1
#define TCFG_T2620_FACTORY_USB_CDC_TEST_ENABLE 0
#define FACTORY_CDC_OK 0
#define FACTORY_CDC_FULL -2
#define RECORD_STATE_STOP 0
#define MIN(a,b) ((a)<(b)?(a):(b))
static u32 generation=1, now;
static int stops, blocked, dut, full, fail_open, fail_read, fail_close, handles;
static char wire[32768];
static u32 wire_len;
void local_irq_disable(void) {}
void local_irq_enable(void) {}
u32 factory_cdc_generation(void) { return generation; }
u32 sys_timer_get_ms(void) { return now; }
int os_sem_create(OS_SEM *s,int n) { return 0; }
int os_sem_del(OS_SEM *s,int n) { return 0; }
int os_sem_post(OS_SEM *s) { return 0; }
int os_sem_pend(OS_SEM *s,int n) { return 0; }
static void (*delay_work)(void);
void os_time_dly(int n) { now += 10; if (delay_work) delay_work(); }
int os_task_create(void (*f)(void *),void *p,int pr,int stack,int q,const char *name) { return 0; }
int factory_cdc_send(u32 g,const u8 *p,u32 n) {
    if (full) return FACTORY_CDC_FULL;
    if (g!=generation) return -1;
    memcpy(wire+wire_len,p,n); wire_len+=n; wire[wire_len]=0; return 0;
}
typedef struct { u32 sn, start_time; char filename[64]; } uxfile_data_t;
static uxfile_data_t info = {3,1600000000,"abcd.raw"};
uxfile_data_t *rdx_uxfile_get_operateFile_info(void) { return &info; }
static struct { int run; } record;
#define rdx_record_get_status() (&record)
bool rdx_app_get_dut_status(void) { return dut; }
int rdx_storage_lifecycle_business_blocked(void) { return blocked; }
u8 rdx_app_factory_file_is_busy(void) { return blocked; }
void rdx_app_emmc_poweroff_check_timer_stop(void) {}
int rdx_rtc_set_timestamp(u32 t) { return 0; }
void rdx_dut_factory_record_stop(void) { ++stops; }
static int dut_wakes;
void rdx_dut_service_wake(void) { ++dut_wakes; }
void *rdx_record_format_factory_open(u32 sn,const char *name,u32 start,u32 *size,u32 *crc) {
    if (fail_open) return NULL;
    ++handles; *size=160; *crc=123; return (void *)1;
}
int mock_read(void *p,int size,int n,void *h) {
    if (fail_read) return 0;
    memset(p,1,n); return n;
}
int mock_close(void *h) { --handles; return fail_close ? -1 : 0; }
#define fread mock_read
#define fclose mock_close
'''
TESTS = r'''
#define CHECK(x) do { if (!(x)) return __LINE__; } while(0)
static void boot(void) {
    dut_wakes=0;
    memset(&audio,0,sizeof(audio)); memset(&file,0,sizeof(file));
    generation=1; now=0; stops=blocked=dut=full=fail_open=fail_read=fail_close=handles=0;
    wire_len=0; wire[0]=0; delay_work=NULL;
}
static void begin(void) { audio.negotiated=audio.timed=1; rdx_factory_audio_begin(1,7); }
static int command(const char *s,char *out) { return rdx_factory_audio_command(1,s,out,258); }
int test_audio_delivery(void) {
    boot(); CHECK(!rdx_factory_audio_ready(1) && !rdx_factory_audio_needs_service()); begin();
    CHECK(rdx_factory_audio_owned() && rdx_factory_audio_needs_service());
    u8 frame[40]={0}; rdx_factory_audio_frame(frame,40); rdx_factory_audio_frame(frame,40);
    CHECK(dut_wakes==1);
    rdx_factory_audio_saved(0); rdx_factory_audio_stopped(8,0);
    CHECK(audio.saved && audio.complete && !audio.end_sent);
    CHECK(!rdx_factory_audio_needs_service());
    full=1; rdx_factory_audio_send(1); CHECK(audio.count==2 && !wire_len);
    full=0; rdx_factory_audio_send(1); rdx_factory_audio_send(1);
    CHECK(audio.sent==2 && !audio.count && !strstr(wire,"@E"));
    rdx_factory_audio_send(1); CHECK(audio.end_sent && strstr(wire,"@E,7,2,80,0,"));
    CHECK(rdx_factory_audio_crc(0,(u8 *)"123456789",9)==0xcbf43926u);
    CHECK(rdx_factory_audio_ready(1));
    return 0;
}
int test_audio_faults(void) {
    u8 frame[40]={0}; boot(); begin();
    for(int i=0;i<65;++i) rdx_factory_audio_frame(frame,40);
    CHECK(audio.error && audio.produced==65 && audio.count==64);
    rdx_factory_audio_service(1); CHECK(stops==1);
    rdx_factory_audio_saved(1); rdx_factory_audio_stopped(8,0); CHECK(!audio.saved && audio.error);
    boot(); begin(); generation=2; rdx_factory_audio_service(2);
    CHECK(stops==1 && !audio.negotiated && audio.error);
    rdx_factory_audio_send(2); CHECK(!wire_len);
    rdx_factory_audio_stopped(0,-1); audio.negotiated=audio.timed=2;
    CHECK(rdx_factory_audio_ready(2) && rdx_factory_audio_begin(2,9)==0);
    boot(); begin(); now=15001; rdx_factory_audio_service(1); CHECK(stops && audio.stop_reason==2);
    boot(); begin(); now=5001; rdx_factory_audio_service(1); CHECK(stops && audio.error==3);
    return 0;
}
static void saved(void) {
    begin(); u8 frame[40]={0}; rdx_factory_audio_frame(frame,40);
    rdx_factory_audio_saved(0); rdx_factory_audio_stopped(8,0);
    rdx_factory_audio_send(1); rdx_factory_audio_send(1);
}
int test_file_lifetime(void) {
    char reply[258]; boot(); saved();
    CHECK(command("FILE_OPEN 7",reply) && file.busy && file.pending);
    CHECK(!rdx_factory_audio_ready(1)); file_work(); rdx_factory_audio_service(1);
    CHECK(file.state==2 && handles==1 && file.busy);
    command("FILE_READ 7 1",reply); CHECK(strstr(reply,"ERR ARG"));
    command("FILE_READ 7 0",reply); CHECK(file.pending);
    file_work(); rdx_factory_audio_service(1); CHECK(file.length==80);
    full=1; rdx_factory_audio_send(1); CHECK(!file.offset && file.length==80);
    full=0; rdx_factory_audio_send(1); CHECK(file.offset==80 && !file.length && file.busy);
    command("FILE_CLOSE 7",reply); rdx_factory_audio_service(1);
    CHECK(file.busy && file.pending); file_work(); rdx_factory_audio_service(1);
    CHECK(!file.busy && !handles && !file.state);
    return 0;
}
int test_file_faults(void) {
    char reply[258]; boot(); saved(); command("FILE_OPEN 7",reply);
    file_work(); rdx_factory_audio_service(1); now=10001; rdx_factory_audio_service(1);
    CHECK(file.pending && file.busy); file_work(); rdx_factory_audio_service(1); CHECK(!handles && !file.busy);
    boot(); saved(); command("FILE_OPEN 7",reply); generation=2;
    rdx_factory_audio_service(2); file_work(); rdx_factory_audio_service(2);
    if(file.pending) { file_work(); rdx_factory_audio_service(2); }
    CHECK(!handles && !file.busy);
    boot(); saved(); fail_open=1; command("FILE_OPEN 7",reply);
    file_work(); rdx_factory_audio_service(1); file_work(); rdx_factory_audio_service(1);
    CHECK(file.state==3 && !file.busy);
    boot(); saved(); command("FILE_OPEN 7",reply); file_work(); rdx_factory_audio_service(1);
    fail_read=1; command("FILE_READ 7 0",reply); file_work(); rdx_factory_audio_service(1);
    file_work(); rdx_factory_audio_service(1); CHECK(!handles && file.state==3 && !file.busy);
    command("FILE_OPEN 42949672960",reply); CHECK(strstr(reply,"ERR ARG"));
    boot(); saved(); command("FILE_OPEN 7",reply); file_work(); rdx_factory_audio_service(1);
    fail_close=1; command("FILE_CLOSE 7",reply); rdx_factory_audio_service(1);
    file_work(); rdx_factory_audio_service(1);
    CHECK(file.busy && file.state==3 && !file.pending);
    rdx_factory_audio_service(1); CHECK(!file.pending); /* No retry/false release. */
    return 0;
}
static int close_stalls;
static void scheduled_worker(void) {
    if (file.pending && !file.done && !(close_stalls && file.command==3)) file_work();
}
int test_shutdown_lifetime(void) {
    char reply[258]; boot(); CHECK(!rdx_factory_audio_shutdown());
    saved(); command("FILE_OPEN 7",reply); file_work(); rdx_factory_audio_service(1);
    delay_work=scheduled_worker;
    CHECK(!rdx_factory_audio_shutdown() && !handles && !file.busy && !file.pending);
    CHECK(!audio.negotiated && !audio.timed);
    /* Worker survives clean idle and can be reused by a new recording. */
    saved(); command("FILE_OPEN 7",reply); /* same instance cannot be reopened */
    CHECK(strstr(reply,"ERR STATE"));
    CHECK(!rdx_factory_audio_begin(1,9));
    CHECK(audio.running);
    boot(); saved(); command("FILE_OPEN 7",reply);
    delay_work=scheduled_worker;
    CHECK(!rdx_factory_audio_shutdown() && !handles && !file.busy);
    return 0;
}
int test_shutdown_faults(void) {
    char reply[258]; boot(); saved(); command("FILE_OPEN 7",reply);
    u32 started=now;
    CHECK(rdx_factory_audio_shutdown()<0 && now-started==500);
    CHECK(file.busy && file.pending && file.command==1 && !file.done);
    CHECK(file.abort && file.state==3 && !rdx_factory_audio_ready(1));
    /* Late worker completion must not publish a free lease after timeout. */
    file_work(); rdx_factory_audio_service(1); rdx_factory_audio_service(1);
    CHECK(file.busy && file.state==3 && !handles);
    boot(); saved(); command("FILE_OPEN 7",reply); file_work(); rdx_factory_audio_service(1);
    fail_close=1; delay_work=scheduled_worker;
    CHECK(rdx_factory_audio_shutdown()<0 && file.busy && file.close_error);
    rdx_factory_audio_service(1); CHECK(file.busy && !file.pending);
    /* Time wrap and a stuck final close both retain the outstanding command. */
    boot(); saved(); command("FILE_OPEN 7",reply); file_work(); rdx_factory_audio_service(1);
    now=0xffffff00u; started=now;
    CHECK(rdx_factory_audio_shutdown()<0 && now-started==500);
    CHECK(file.busy && handles==1 && file.pending && file.command==3);
    /* Opening and final close share the same deadline (not 500 ms each). */
    boot(); saved(); command("FILE_OPEN 7",reply);
    delay_work=scheduled_worker; close_stalls=1;
    CHECK(rdx_factory_audio_shutdown()<0 && now==500 && file.command==3);
    close_stalls=0;
    return 0;
}
'''


def check_audio():
    import ctypes
    from llvmlite import binding as llvm
    crt = ctypes.CDLL('msvcrt.dll')
    for name in ('snprintf', 'strncmp', 'strcmp', 'strlen', 'strstr', 'strcpy', 'memcpy', 'memset'):
        export = '_snprintf' if name == 'snprintf' else name
        llvm.add_symbol(name, ctypes.cast(getattr(crt, export), ctypes.c_void_p).value)
    source = (ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_factory_audio.c').read_text(encoding='utf-8')
    source = re.sub(r'^#include .*\n', '', source, flags=re.M)
    path = ROOT / 'cache/factory-audio-tests/audio.c'
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(STUBS + source + TESTS, encoding='utf-8')
    run_c_checks(path, ['test_audio_delivery', 'test_audio_faults', 'test_file_lifetime', 'test_file_faults', 'test_shutdown_lifetime', 'test_shutdown_faults'], native=True)
    cdc = load_cdc_tool()
    AudioLink, Station = cdc.AudioLink, cdc.Station
    class RejectedLink:
        def request(self, command):
            return 'ERR BUSY'
    station = Station(None, {})
    station.audio = RejectedLink()
    assert station.query('FILE_OPEN 18') == 'ERR BUSY'
    assert station.report['device_error'] == dict(command='FILE_OPEN 18', response='ERR BUSY')
    import queue
    import threading
    import zlib
    def receiver():
        link = AudioLink.__new__(AudioLink)
        link.record_id = None; link.frames = bytearray(); link.frame_count = 0
        link.end_info = None; link.error = None; link.end = threading.Event()
        link.replies = queue.Queue(8); link.files = queue.Queue(1)
        return link
    def frame(text):
        return text + '*%08X' % zlib.crc32(text.encode())
    link = receiver()
    link.accept(frame('@A,7,0,40,' + '00'*40))
    link.accept(frame('@E,7,1,40,0,' + str(zlib.crc32(bytes(40)))))
    assert link.verify_stream()['bytes'] == 40
    for bad in [frame('@A,7,1,40,' + '00'*40), frame('@A,8,0,40,' + '00'*40),
                frame('@E,7,1,40,0,0'), '@A,7,0,40,' + '00'*40 + '*00000000']:
        try:
            link.accept(bad)
        except RuntimeError:
            pass
        else:
            raise AssertionError('invalid/late frame accepted')
    link = receiver()
    link.accept(frame('@E,7,0,0,0,0'))
    try:
        link.verify_stream()
    except RuntimeError:
        pass
    else:
        raise AssertionError('empty stream passed')
    print('CDC audio: CRC, queue overflow, tail ordering, session isolation, file lifetime and host validation passed.')


if __name__ == '__main__':
    check_audio()
