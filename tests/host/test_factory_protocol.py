"""Actual app_core CDC parser; invoked by test_factory_usb.py."""
import re
import ctypes
from llvmlite import binding as llvm
from host_c_test_lib import ROOT, run_c_checks

STUBS = r'''
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
#define NULL ((void *)0)
#define TCFG_T2620_FACTORY_USB_CDC_ENABLE 1
#define TCFG_T2620_FACTORY_USB_CDC_TEST_ENABLE 0
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
void rdx_dut_cdc_status(char *out, unsigned int capacity, int keys) { strcpy(out,"OK STATUS\r\n"); }
void rdx_dut_cdc_request(u32 gen, const char *name, u8 value, char *out, unsigned int capacity) {
    snprintf(out,capacity,"OK ACTION=%s;VALUE=%u;GEN=%u\r\n",name,value,gen);
}
unsigned int strlen(const char *);
int snprintf(char *, unsigned int, const char *, ...);
static u32 generation=1, now;
static int full, initialized=1, mac_error, aborted, irq, timer_fail;
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
u16 sys_timer_add(void *p, void (*cb)(void *), int period) { return !timer_fail; }
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
int test_action_framing(void) {
    boot(); CHECK(!input("dUt fT_sPeAkEr 1\r\n"));
    CHECK(!strcmp(wire,"OK ACTION=FT_SPEAKER;VALUE=1;GEN=1\r\n"));
    const char *bad[]={"DUT FT_DUT\r\n", "DUT FT_DUT 01\r\n", "DUT FT_DUT -1\r\n",
        "DUT FT_DUT 1 \r\n"};
    for(int i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        boot(); CHECK(!input(bad[i])); CHECK(!strcmp(wire,"ERR ARG\r\n"));
    }
    boot(); CHECK(!input("DUT FT_DUT ")); generation=2;
    CHECK(!input("1\r\n")); CHECK(!strcmp(wire,"ERR CMD\r\n"));
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
    run_c_checks(path, ('test_frames', 'test_sessions', 'test_action_framing', 'test_identity'), native=True)
    print('PASS: CDC protocol framing, overflow isolation, mailbox/TX backpressure, session/timeout, bounded identity')


if __name__ == '__main__':
    check_protocol()
