"""Actual app_core CDC parser; invoked by test_factory_usb.py."""
import re
import ctypes
from llvmlite import binding as llvm
from host_c_test_lib import ROOT, run_c_checks

STUBS = r'''
int rdx_factory_audio_command(unsigned g, const char *s, char *o, unsigned c) { return 0; }
static int cleanup_pending, audio_services;
void rdx_factory_audio_service(unsigned g) { ++audio_services; }
int rdx_factory_audio_needs_service(void) { return cleanup_pending; }
void rdx_factory_audio_send(unsigned g) {}

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
#define NULL ((void *)0)
#define TCFG_T2620_FACTORY_USB_CDC_ENABLE 1
#define TCFG_T2620_FACTORY_USB_CDC_TEST_ENABLE 0
#define TCFG_T2620_FACTORY_USB_CDC_LOG_LEVEL 1
#define FACTORY_CDC_TX_MAX 258
#define FACTORY_CDC_OK 0
#define FACTORY_CDC_FULL -2
#define FACTORY_CDC_STALE -1
#define FIRMWARE_VERSION "1.0.7"
void *memcpy(void *, const void *, unsigned int);
void *memset(void *, int, unsigned int);
char *strcpy(char *, const char *);
int strcmp(const char *, const char *);
int strncmp(const char *, const char *, unsigned int);
char *strchr(const char *, int);
void rdx_dut_service_wake(void) {}
void rdx_dut_cdc_status(char *out, unsigned int capacity, int keys) { strcpy(out,"OK STATUS\r\n"); }
void rdx_dut_cdc_request(u32 gen, const char *name, u8 value, char *out, unsigned int capacity) {
    snprintf(out,capacity,"OK ACTION=%s;VALUE=%u;GEN=%u\r\n",name,value,gen);
}
unsigned int strlen(const char *);
int snprintf(char *, unsigned int, const char *, ...);
static u32 generation=1, now;
static int full, initialized=1, mac_error, aborted, irq, timer_fail;
static int timer_live, timer_adds, timer_dels, app_queued, app_posts, app_fail;
static void factory_event(void *priv);
#define Q_CALLBACK 1
int os_taskq_post_type(const char *task,int type,int count,int *message) {
    ++app_posts;
    if (app_fail || app_queued) return -1;
    app_queued=1; return 0;
}
static void pump(void) { if(app_queued) { app_queued=0; factory_event(NULL); } }
static char command_log[180];
static unsigned int command_logs;
static int log_in_irq;
int printf(const char *format, const char *text) {
    if (irq) log_in_irq=1;
    ++command_logs;
    return snprintf(command_log,sizeof(command_log),format,text);
}
static char wire[4096], name[40]="T2620";
static unsigned int wire_length;
static u8 mac[6]={1,2,3,4,5,6};
static struct { u8 AuthKey[25]; } identity;
static void *rdx_vm_get_auth_info(void);
/* Match the production expression with a typed result. */
#define rdx_vm_get_auth_info() (&identity)
u8 rdx_app_initialized(void) { return initialized; }
char *rdx_ble_server_get_local_name(void) { return name; }
int le_controller_get_mac(u8 *out) { memcpy(out,mac,6); return mac_error; }
void local_irq_disable(void) { ++irq; }
void local_irq_enable(void) { --irq; }
u32 factory_cdc_generation(void) { return generation; }
u32 sys_timer_get_ms(void) { return now; }
u16 sys_timer_add(void *p, void (*cb)(void *), int period) {
    ++timer_adds; if(timer_fail) return 0; timer_live=1; return 1;
}
void sys_timer_del(u16 id) { ++timer_dels; timer_live=0; }
static void (*state_handler)(void);
void factory_cdc_set_state_handler(void (*handler)(void)) { state_handler=handler; }
static int (*consumer)(u32, const u8 *, u32);
void factory_cdc_set_rx_handler(int (*cb)(u32, const u8 *, u32)) { consumer=cb; }
void factory_cdc_abort(u32 gen) { if (gen==generation) { generation=0; ++aborted; } }
int factory_cdc_send(u32 gen, const u8 *data, u32 length) {
    if (gen!=generation) return FACTORY_CDC_STALE;
    if (full) return FACTORY_CDC_FULL;
    memcpy(wire+wire_length,data,length); wire_length+=length; wire[wire_length]=0;
    return 0;
}
'''

TESTS = r'''
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static void boot(void) {
    memset(&factory_protocol,0,sizeof(factory_protocol));
    memset(&identity,0,sizeof(identity));
    factory_head=factory_count=0; generation=1; now=0;
    full=aborted=irq=timer_fail=mac_error=0; initialized=1;
    wire_length=0; wire[0]=0; strcpy(name,"T2620"); consumer=NULL;
    command_logs=0; command_log[0]=0; log_in_irq=0;
    factory_event_posted=factory_pending=factory_initialized=0;
    timer_live=timer_adds=timer_dels=app_queued=app_posts=app_fail=0;
    cleanup_pending=audio_services=0; state_handler=NULL;
    for (int i=0;i<6;++i) mac[i]=i+1;
    rdx_factory_cdc_init();
}
static int input(const char *text) {
    int length=strlen(text);
    while (length) {
        int count=length>64?64:length;
        if (factory_receive(generation,(const u8 *)text,count)) return -1;
        factory_service(NULL); text+=count; length-=count;
    }
    factory_service(NULL);
    return 0;
}
int test_frames(void) {
    boot(); CHECK(consumer!=NULL);
    CHECK(!input("pInG\r")); CHECK(!wire_length);
    CHECK(!input("\nPING\r\n")); CHECK(!strcmp(wire,"OK PONG\r\nOK PONG\r\n"));
    const char *bad[]={"\200DUT ft_format 1\r\nPING\r\n", "PING\nDUT ft_format 1\r\nPING\r\n",
        "PING\rDUT ft_format 1\r\nPING\r\n", "\r\nPING\r\n", " PING\r\nPING\r\n",
        "PING \r\nPING\r\n", "PING\r\r\nPING\r\n"};
    for (int i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        boot(); CHECK(!input(bad[i])); CHECK(!strcmp(wire,"ERR CMD\r\nOK PONG\r\n"));
    }
    char longline[180]; memset(longline,'A',128); strcpy(longline+128,"\r\nPING\r\n");
    boot(); CHECK(!input(longline)); CHECK(!strcmp(wire,"ERR CMD\r\nOK PONG\r\n"));
    memset(longline,'A',129); strcpy(longline+129,"DUT ft_format 1\r\nPING\r\n");
    boot(); CHECK(!input(longline)); CHECK(!strcmp(wire,"ERR ARG\r\nOK PONG\r\n"));
    boot(); CHECK(!input("GET_STATUS\r\n")); CHECK(!strcmp(wire,"OK STATUS\r\n"));
    return 0;
}
int test_sessions(void) {
    boot(); full=1; CHECK(!input("PING\r\nPING\r\n"));
    CHECK(factory_protocol.response_length && factory_count==1);
    for (int i=0;i<3;++i) CHECK(!factory_receive(1,(const u8 *)"PING\r\n",6));
    CHECK(factory_receive(1,(const u8 *)"PING\r\n",6));
    CHECK(factory_count==4 && !wire_length);
    full=0;
    for (int i=0;i<4;++i) factory_service(NULL);
    CHECK(!strcmp(wire,"OK PONG\r\nOK PONG\r\nOK PONG\r\nOK PONG\r\nOK PONG\r\n"));
    boot(); CHECK(!input("PI")); generation=2; CHECK(!input("NG\r\nPING\r\n"));
    CHECK(!strcmp(wire,"ERR CMD\r\nOK PONG\r\n"));
    boot(); full=1; CHECK(!input("PING\r\nPING\r\n")); generation=2; full=0;
    CHECK(!input("PING\r\n")); CHECK(!strcmp(wire,"OK PONG\r\n"));
    CHECK(factory_receive(1,(const u8 *)"PING\r\n",6)); CHECK(!irq);
    boot(); full=1; CHECK(!input("PING\r\n")); now=1999; factory_service(NULL); CHECK(!aborted);
    now=2000; factory_service(NULL); CHECK(aborted && !generation);
    generation=3; full=0; CHECK(!input("PING\r\n")); CHECK(!strcmp(wire,"OK PONG\r\n"));
    return 0;
}
int test_legacy_rejected(void) {
    const char *old[]={"DUT ft_dut 1\r\n", "dUt fT_sPeAkEr 0\r\n",
        "DUT ft_finalpack_end 1\r\n", "DUT FT_DUT\r\n", "DUT FT_DUT 01\r\n"};
    for (int index=0;index<sizeof(old)/sizeof(old[0]);++index) {
        boot(); CHECK(!input(old[index])); CHECK(!strcmp(wire,"ERR CMD\r\n"));
        CHECK(!input("PING\r\n")); CHECK(!strcmp(wire,"ERR CMD\r\nOK PONG\r\n"));
    }
    return 0;
}
int test_custom_framing(void) {
    boot(); CHECK(!input("*APP#custom#ft_speaker#0#\r")); CHECK(!command_logs && !wire_length);
    CHECK(!input("\n"));
    CHECK(!strcmp(command_log,"[CDC_RX] *APP#custom#ft_speaker#0#\n"));
    CHECK(!strcmp(wire,"OK ACTION=FT_SPEAKER;VALUE=0;GEN=1\r\n"));
    boot(); CHECK(!input("*APP#custom#ft_key_layout#\r\n"));
    CHECK(!strcmp(wire,"OK ACTION=FT_KEY_LAYOUT;VALUE=1;GEN=1\r\n"));
    boot(); CHECK(!input("*APP#custom#ft_finalpack_end#\r\n"));
    CHECK(!strcmp(wire,"OK ACTION=FT_FINALPACK_END;VALUE=1;GEN=1\r\n"));
    boot(); CHECK(!input("*APP#custom#ft_key_dut_enable#\r\n"));
    CHECK(!strcmp(wire,"OK ACTION=FT_KEY_DUT_ENABLE;VALUE=1;GEN=1\r\n"));
    boot(); CHECK(!input("*APP#custom#ft_key_dut_enable#1#\r\n"));
    CHECK(!strcmp(wire,"OK ACTION=FT_KEY_DUT_ENABLE;VALUE=1;GEN=1\r\n"));
    boot(); CHECK(!input("*app#CUSTOM#ft_rec_chat#1#\r\n*APP#custom#ft_rec_chat#0#\r\n"));
    CHECK(!strcmp(wire,"OK ACTION=FT_REC_CHAT;VALUE=1;GEN=1\r\nOK ACTION=FT_REC_CHAT;VALUE=0;GEN=1\r\n"));
    const char *bad[]={"*APP#custom#ft_dut#", "*APP#custom#ft_dut#1",
        "*APP#custom#ft_dut#01#", "*APP#custom#ft_dut#-1#", "*APP#custom#ft_dut#1##",
        "*APP#custom#ft_dut#1#extra", "*APP#custom##1#", "*APP#custom#ft_du t#1#",
        "*APP#custom#ft_key_layout#1#", "*APP#custom#ft_key_layout#0#",
        "*APP#custom#ft_key_layout##", "*APP#custom#ft_dut#1#*APP#custom#ft_dut#0#"};
    for (int index=0;index<sizeof(bad)/sizeof(bad[0]);++index) {
        boot(); CHECK(!input(bad[index])); CHECK(!input("\r\n"));
        CHECK(!strcmp(wire,"ERR ARG\r\n"));
    }
    boot(); CHECK(!input("*APP#custom#other#1#\r\n")); CHECK(!strcmp(wire,"ERR CMD\r\n"));
    boot(); CHECK(!input("*CDC#custom#ft_dut#1#\r\n")); CHECK(!strcmp(wire,"ERR CMD\r\n"));
    boot(); CHECK(!input("*APP#custom#ft_dut#")); generation=2;
    CHECK(!input("1#\r\nPING\r\n")); CHECK(!strcmp(wire,"ERR CMD\r\nOK PONG\r\n"));
    return 0;
}
int test_command_logging(void) {
    boot(); CHECK(!input("*APP#custom#fT_sPeAkEr#0#\r")); CHECK(!command_logs);
    CHECK(!input("\n")); CHECK(command_logs==1 && !log_in_irq);
    CHECK(!strcmp(command_log,"[CDC_RX] *APP#custom#fT_sPeAkEr#0#\n"));
    CHECK(!strcmp(wire,"OK ACTION=FT_SPEAKER;VALUE=0;GEN=1\r\n"));
    boot(); CHECK(!input("Future  Arg\r\n"));
    CHECK(!strcmp(command_log,"[CDC_RX] Future  Arg\n"));
    CHECK(!strcmp(wire,"ERR CMD\r\n"));
    boot(); CHECK(!input("GET_INFO\r\n"));
    CHECK(command_logs==1 && !strcmp(command_log,"[CDC_RX] GET_INFO\n"));
    boot(); CHECK(!input("bad\nsecret\r\n"));
    CHECK(!strcmp(command_log,"[CDC_RX] FRAME_ERROR=INVALID\n"));
    char oversized[132]; memset(oversized,'A',129); strcpy(oversized+129,"\r\n");
    boot(); CHECK(!input(oversized));
    CHECK(!strcmp(command_log,"[CDC_RX] FRAME_ERROR=OVERFLOW\n"));
    boot(); full=1; CHECK(!input("PING\r\n"));
    factory_service(NULL); factory_service(NULL); CHECK(command_logs==1);
    return 0;
}
int test_identity(void) {
    boot(); CHECK(!input("GET_INFO\r\n"));
    CHECK(!strcmp(wire,"OK PROTO=1;NAME=T2620;AUTH=000000000000000000000000;ADDR=060504030201;FW=1.0.7\r\n"));
    boot(); initialized=0; CHECK(!input("GET_INFO\r\n")); CHECK(!strcmp(wire,"ERR STATE\r\n"));
    boot(); identity.AuthKey[0]='A'; CHECK(!input("GET_INFO\r\n")); CHECK(!strcmp(wire,"ERR DATA\r\n"));
    boot(); memset(identity.AuthKey,'A',24); identity.AuthKey[24]='X';
    CHECK(!input("GET_INFO\r\n"));
    CHECK(!strcmp(wire,"OK PROTO=1;NAME=T2620;AUTH=AAAAAAAAAAAAAAAAAAAAAAAA;ADDR=060504030201;FW=1.0.7\r\n"));
    boot(); strcpy(name,"bad;name"); CHECK(!input("GET_INFO\r\n")); CHECK(!strcmp(wire,"ERR DATA\r\n"));
    return 0;
}
int test_idle_session_lifecycle(void) {
    boot(); generation=0; pump();
    CHECK(consumer && state_handler && !timer_live && !timer_adds);
    CHECK(!rdx_factory_cdc_needs_poll());
    int services=audio_services, posts=app_posts;
    for(int i=0;i<100;i++) rdx_factory_cdc_poll();
    CHECK(audio_services==services && app_posts==posts && !timer_adds);
    generation=2; state_handler(); pump(); CHECK(timer_live && timer_adds==1);
    CHECK(!input("PING\r\n") && !strcmp(wire,"OK PONG\r\n")); pump();
    full=1; CHECK(!input("PING\r\n")); CHECK(factory_protocol.response_length);
    generation=0; cleanup_pending=1; state_handler(); pump();
    CHECK(!timer_live && timer_dels==1 && !factory_protocol.response_length && !factory_count);
    services=audio_services; rdx_factory_cdc_poll(); CHECK(audio_services==services+1);
    cleanup_pending=0; services=audio_services; rdx_factory_cdc_poll();
    CHECK(audio_services==services && !timer_live);
    generation=3; full=0; state_handler(); pump(); CHECK(timer_live);
    CHECK(factory_receive(2,(const u8 *)"PING\r\n",6));
    CHECK(!input("PING\r\n") && !strcmp(wire,"OK PONG\r\nOK PONG\r\n"));
    return 0;
}
int test_notification_and_timer_failure_recovery(void) {
    boot(); generation=0; pump();
    app_fail=1; generation=2; state_handler(); CHECK(factory_pending && !app_queued);
    CHECK(rdx_factory_cdc_needs_poll());
    timer_fail=1; rdx_factory_cdc_poll(); CHECK(!timer_live && consumer);
    CHECK(rdx_factory_cdc_needs_poll());
    app_fail=0; CHECK(!factory_receive(2,(const u8 *)"PING\r\n",6)); pump();
    CHECK(!strcmp(wire,"OK PONG\r\n") && !timer_live);
    timer_fail=0; rdx_factory_cdc_poll(); CHECK(timer_live);
    CHECK(!rdx_factory_cdc_needs_poll());
    app_fail=1; generation=0; state_handler(); rdx_factory_cdc_poll();
    CHECK(!timer_live && !factory_count && !irq);
    app_fail=0; pump(); CHECK(!timer_live); /* 旧唤醒通知不能重新开启会话。 */
    return 0;
}
'''


def check_protocol():
    crt = ctypes.CDLL('msvcrt.dll')
    for name in ('snprintf', 'strncmp', 'strchr', 'strcmp', 'strlen', 'strcpy', 'memcpy', 'memset'):
        export = '_snprintf' if name == 'snprintf' else name
        llvm.add_symbol(name, ctypes.cast(getattr(crt, export), ctypes.c_void_p).value)
    source = (ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_factory_cdc.c').read_text(encoding='utf-8')
    source = re.sub(r'^#include .*$', '', source, flags=re.M)
    path = ROOT / 'cache/factory-usb-test/protocol.c'
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(STUBS + source + TESTS, encoding='utf-8')
    run_c_checks(path, ('test_frames', 'test_sessions', 'test_legacy_rejected', 'test_identity', 'test_command_logging', 'test_custom_framing', 'test_idle_session_lifecycle', 'test_notification_and_timer_failure_recovery'), native=True)
    print('PASS: CDC protocol framing, overflow isolation, mailbox/TX backpressure, session/timeout, bounded identity')


if __name__ == '__main__':
    check_protocol()
