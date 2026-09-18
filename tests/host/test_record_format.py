"""Real format/recovery C and cJSON against an in-memory JL filesystem.

Checks persistence semantics and interruption replay, not physical SD durability.
"""
import re

from host_c_test_lib import ROOT, function, run_c_checks


STUBS = r'''
typedef unsigned char u8;
typedef unsigned int u32;
#define RECORD_FORMATE_OPUS_16K_MONO 1
#define RECORD_FORMATE_OPUS_16K_STERO 2
typedef struct { u32 sn; char filename[64]; u8 record_scene; u32 start_time, frame_size; } uxfile_data_t;
typedef struct { char name[80]; u8 data[16000]; u32 size, pos; int present, opened; } mock_file;
static mock_file files[32];
static int fail_write, fail_flush, fail_close, fail_truncate, fail_read, handles;
static const char *fail_path;
static int writes;
#define FILE mock_file
#define fopen mock_open
#define fclose mock_close
#define fread mock_read
#define fwrite mock_write
#define ftell mock_tell
#define printf(...) ((void)0)
static mock_file *lookup(const char *name) {
    for (u32 i=0; i<32; ++i) if (files[i].present && !strcmp(files[i].name,name)) return &files[i];
    return NULL;
}
FILE *mock_open(const char *name, const char *mode) {
    FILE *f=lookup(name);
    if (!f && mode[0]=='w') for (u32 i=0; i<32; ++i) if (!files[i].present) {
        f=&files[i]; memset(f,0,sizeof(*f)); strcpy(f->name,name); f->present=1; break;
    }
    if (f) { if(f->opened) return NULL; f->pos=0; f->opened=1; ++handles; }
    return f;
}
int mock_close(FILE *f) { if(!f || !f->opened) return -1; f->opened=0; --handles; return fail_close ? -1 : 0; }
int mock_read(void *buf,u32 size,u32 count,FILE *f) {
    u32 n=size*count;
    if (fail_read) return 0;
    if(n>f->size-f->pos) n=f->size-f->pos;
    memcpy(buf,f->data+f->pos,n); f->pos+=n; return n;
}
int mock_write(void *buf,u32 size,u32 count,FILE *f) {
    u32 n=size*count; ++writes;
    if(fail_write && (!fail_path || !strcmp(f->name,fail_path))) n=n? n-1 : 0;
    if(f->pos+n>sizeof(f->data)) return -1;
    memcpy(f->data+f->pos,buf,n); f->pos+=n;
    if(f->pos>f->size) f->size=f->pos;
    return n;
}
int fget_name(FILE *f,u8 *buf,int len) { const char *p=strrchr(f->name,'/'); if(!p) p=f->name; else ++p; u32 n=strlen(p); if(n>(u32)len) n=len; memcpy(buf,p,n); return n; }
int flen(FILE *f) { return f->size; }
int mock_tell(FILE *f) { return f->pos; }
int ftruncate(FILE *f,u32 n) { if(fail_truncate || n>sizeof(f->data)) return -1; f->size=n; return 0; }
int f_flush_wbuf(const char *p) { return fail_flush || strcmp(p,"storage/sd0/C/") ? -1 : 0; }
int fdelete_by_name(const char *p) { FILE *f=lookup(p); if(!f) return -1; f->present=0; return 0; }
struct vfscan { u32 file_number; FILE *items[32]; };
#define FSEL_BY_NUMBER 5
static struct vfscan scan_data;
struct vfscan *fscan(const char *root,const char *args,u8 depth) {
    memset(&scan_data,0,sizeof(scan_data));
    for(u32 i=0;i<32;++i) if(files[i].present && strstr(files[i].name,".mta"))
        scan_data.items[scan_data.file_number++]=&files[i];
    return &scan_data;
}
FILE *fselect(struct vfscan *s,int mode,int n) { return mock_open(s->items[n-1]->name,"r"); }
void fscan_release(struct vfscan *s) {}
void wdt_clear(void) {}
void rdx_app_emmc_poweron(u8 check) {}
void sd_set_power_user(u8 en) {}
int dev_manager_list_check_by_logo(const char *s) { return 1; }
int dev_manager_add(const char *s) { return 0; }
void *zalloc(u32 n) { void *p=malloc(n); if(p) memset(p,0,n); return p; }
u32 rdx_protocol_calc_opus_format(u8 format) { return format==1 ? 0x40a80400u : 0x41280800u; }
'''

TESTS = r'''
#define CHECK(x) do { if(!(x)) return __LINE__; } while(0)
static uxfile_data_t current;
static u8 audio[4000];
static int recover(void) { rf_boot_started=0; return rdx_record_format_boot(); }
static void reset(void) {
    memset(files,0,sizeof(files)); memset(&rf_current,0,sizeof(rf_current));
    rf_boot_started=0; rf_error=fail_write=fail_flush=fail_close=fail_truncate=fail_read=handles=writes=0; fail_path=NULL;
    memset(&current,0,sizeof(current)); current.sn=7; current.start_time=100;
    current.frame_size=40; strcpy(current.filename,"abc123.raw");
    for(u32 i=0;i<sizeof(audio);++i) audio[i]=(i*19+3)&255;
}
static int seed_mono(void) {
    if(rdx_record_format_begin(&current,1) || rdx_record_format_frame(&current,1,audio,40)) return -1;
    return rf_write(RF_ROOT "abc123.raw",audio,sizeof(audio));
}
static cJSON *read_dat(void) {
    FILE *f=lookup(RF_DAT); if(!f || f->size>=sizeof(f->data)) return NULL;
    f->data[f->size]=0; return cJSON_Parse((char *)f->data);
}
static u32 num(cJSON *obj,const char *key) { cJSON *n=cJSON_GetObjectItem(obj,key); return n ? n->valuedouble : 0xffffffffu; }
int test_format_persistence(void) {
    reset(); CHECK(seed_mono()==0); CHECK(handles==0);
    int before=writes; CHECK(rdx_record_format_frame(&current,1,audio+40,40)==0);
    CHECK(writes==before); /* resume/next frame does not overwrite identity */
    CHECK(rdx_record_format_frame(&current,2,audio,80)<0); CHECK(rf_error<0);
    reset(); CHECK(rdx_record_format_begin(&current,1)==0);
    CHECK(rdx_record_format_frame(&current,1,audio,39)<0 && !lookup(RF_ROOT "abc123.mta"));
    reset(); CHECK(rdx_record_format_begin(&current,1)==0); fail_write=1;
    CHECK(rdx_record_format_frame(&current,1,audio,40)<0 && handles==0);
    reset(); CHECK(rdx_record_format_begin(&current,1)==0); fail_flush=1;
    CHECK(rdx_record_format_frame(&current,1,audio,40)<0 && handles==0);
    reset(); CHECK(rdx_record_format_begin(&current,1)==0); fail_close=1;
    CHECK(rdx_record_format_frame(&current,1,audio,40)<0 && handles==0);
    reset(); CHECK(rdx_record_format_begin(&current,1)==0); fail_read=1;
    CHECK(rdx_record_format_frame(&current,1,audio,40)<0 && handles==0);
    reset(); strcpy(current.filename,"../abc.raw"); CHECK(rdx_record_format_begin(&current,1)<0);
    reset(); current.frame_size=80; current.record_scene=1;
    CHECK(rdx_record_format_begin(&current,2)==0);
    CHECK(rdx_record_format_frame(&current,2,audio,80)==0);
    CHECK(rf_write(RF_ROOT "abc123.raw",audio,sizeof(audio))==0);
    CHECK(recover()==0);
    cJSON *root=read_dat(); cJSON *obj=cJSON_GetArrayItem(root,0);
    CHECK(num(obj,"frame_size")==80 && num(obj,"opus")==0x41280800u && num(obj,"scene")==1);
    cJSON_Delete(root);
    return 0;
}
int test_format_recovery(void) {
    reset(); CHECK(seed_mono()==0); CHECK(recover()==0 && handles==0);
    cJSON *root=read_dat(); CHECK(root && cJSON_GetArraySize(root)==1);
    cJSON *obj=cJSON_GetArrayItem(root,0);
    CHECK(num(obj,"sn")==7 && num(obj,"frame_size")==40 && num(obj,"opus")==0x40a80400u);
    CHECK(num(obj,"size")==4000 && num(obj,"start_time")==100 && num(obj,"end_time")==102);
    u32 crc=0; crc=rdx_util_crc32(audio,4000,&crc);
    CHECK(strtoul(cJSON_GetObjectItem(obj,"crc")->valuestring,NULL,10)==crc);
    cJSON_Delete(root);
    CHECK(!lookup(RF_COMMIT));
    int unchanged=writes; CHECK(rdx_record_format_boot()==0 && writes==unchanged);
    CHECK(recover()==0 && handles==0 && writes==unchanged);
    /* A healthy index boots even when all writes would fail. */
    fail_write=1;
    CHECK(recover()==0 && handles==0 && writes==unchanged);
    fail_write=0;
    root=read_dat(); CHECK(cJSON_GetArraySize(root)==1); cJSON_Delete(root);
    /* A legacy stereo entry and marks survive missing-mono-entry recovery. */
    const char *old="[{\"sn\":3,\"name\":\"def123.raw\",\"frame_size\":80,\"marks\":[12,45]}]";
    CHECK(rf_write(RF_DAT,old,strlen(old))==0);
    CHECK(recover()==0 && handles==0);
    root=read_dat(); CHECK(cJSON_GetArraySize(root)==2);
    obj=cJSON_GetArrayItem(root,0); CHECK(num(obj,"frame_size")==80);
    CHECK(cJSON_GetArraySize(cJSON_GetObjectItem(obj,"marks"))==2); cJSON_Delete(root);
    /* Already indexed mono with a bad legacy profile is corrected in place. */
    old="[{\"sn\":7,\"name\":\"abc123.raw\",\"frame_size\":80,\"opus\":0,\"marks\":[77]}]";
    CHECK(rf_write(RF_DAT,old,strlen(old))==0); CHECK(recover()==0);
    root=read_dat(); obj=cJSON_GetArrayItem(root,0);
    CHECK(num(obj,"frame_size")==40 && num(obj,"opus")==0x40a80400u);
    CHECK(cJSON_GetArraySize(cJSON_GetObjectItem(obj,"marks"))==1); cJSON_Delete(root);
    unchanged=writes;
    CHECK(recover()==0 && writes==unchanged && handles==0);
    /* Missing profile fields still require repair; preserve unknown fields. */
    old="[{\"sn\":7,\"name\":\"abc123.raw\",\"vendor\":{\"revision\":2}}]";
    CHECK(rf_write(RF_DAT,old,strlen(old))==0); unchanged=writes;
    CHECK(recover()==0 && writes>unchanged && handles==0);
    root=read_dat(); obj=cJSON_GetArrayItem(root,0);
    CHECK(num(obj,"frame_size")==40 && num(obj,"opus")==0x40a80400u);
    CHECK(num(cJSON_GetObjectItem(obj,"vendor"),"revision")==2); cJSON_Delete(root);
    /* Preserve original DAT bytes, including whitespace, on a healthy boot. */
    old="[ {\"sn\":7, \"name\":\"abc123.raw\", \"frame_size\":40, \"opus\":1084752896} ]";
    CHECK(rf_write(RF_DAT,old,strlen(old))==0); unchanged=writes;
    CHECK(recover()==0 && writes==unchanged && handles==0);
    CHECK(lookup(RF_DAT)->size==strlen(old) && !memcmp(lookup(RF_DAT)->data,old,strlen(old)));
    return 0;
}
int test_format_fail_closed(void) {
    reset(); CHECK(seed_mono()==0);
    lookup(RF_ROOT "abc123.mta")->data[0]^=1;
    CHECK(recover()<0 && !lookup(RF_DAT) && handles==0);
    reset(); CHECK(seed_mono()==0); lookup(RF_ROOT "abc123.raw")->data[0]^=1;
    CHECK(recover()<0 && !lookup(RF_DAT) && handles==0);
    reset(); CHECK(seed_mono()==0); lookup(RF_ROOT "abc123.raw")->size=3999;
    CHECK(recover()<0 && !lookup(RF_DAT) && handles==0);
    reset(); CHECK(seed_mono()==0); fdelete_by_name(RF_ROOT "abc123.raw");
    CHECK(recover()==0 && !lookup(RF_DAT)); /* no resurrection */
    lookup(RF_ROOT "abc123.mta")->size=3; /* cut before RAW creation */
    CHECK(recover()==0 && handles==0);
    reset(); CHECK(seed_mono()==0);
    const char *bad="[{\"sn\":7,\"name\":\"def123.raw\"}]";
    CHECK(rf_write(RF_DAT,bad,strlen(bad))==0);
    int unchanged=writes;
    CHECK(recover()<0 && handles==0 && writes==unchanged);
    CHECK(lookup(RF_DAT)->size==strlen(bad) && !memcmp(lookup(RF_DAT)->data,bad,strlen(bad)));
    reset(); CHECK(seed_mono()==0); bad="[{\"sn\":3";
    CHECK(rf_write(RF_DAT,bad,strlen(bad))==0);
    CHECK(recover()<0 && handles==0);
    CHECK(lookup(RF_DAT)->size==strlen(bad));
    return 0;
}
int test_format_replay(void) {
    reset(); CHECK(seed_mono()==0); fail_write=1; fail_path=RF_DAT;
    CHECK(recover()<0 && lookup(RF_COMMIT) && handles==0);
    fail_write=rf_error=0; fail_path=NULL;
    CHECK(recover()==0 && !lookup(RF_COMMIT) && handles==0);
    cJSON *root=read_dat(); CHECK(root && cJSON_GetArraySize(root)==1); cJSON_Delete(root);
    reset(); CHECK(seed_mono()==0); fail_write=1; fail_path=RF_DAT;
    CHECK(recover()<0 && lookup(RF_COMMIT));
    fail_write=rf_error=0; fail_path=NULL; lookup(RF_TMP)->data[1]^=1;
    CHECK(recover()<0 && lookup(RF_COMMIT) && handles==0);
    return 0;
}
'''


def main():
    directory = ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol'
    source = (directory / 'rdx_record_format.c').read_text(encoding='utf-8')
    source = re.sub(r'^#include.*\n', '', source, flags=re.M)
    util = (directory / 'rdx_util.c').read_text(encoding='utf-8-sig')
    crc = function(util, 'u32 rdx_util_crc32(')
    audit = ROOT / 'cache/record-format-tests'
    audit.mkdir(parents=True, exist_ok=True)
    path = audit / 'format.c'
    json_dir = ROOT / 'SDK/apps/common/cJSON'
    header = re.sub(r'^#include.*\n', '', (json_dir / 'cJSON.h').read_text(), flags=re.M)
    json_source = re.sub(r'^#include.*\n', '', (json_dir / 'cJSON.c').read_text(), flags=re.M)
    libc = r"""
typedef unsigned long long size_t;
typedef long long ptrdiff_t;
#define NULL ((void *)0)
#define INT_MAX 2147483647
#define INT_MIN (-INT_MAX-1)
#define DBL_EPSILON 2.2204460492503131e-16
void *malloc(size_t);
void *realloc(void *,size_t);
void free(void *);
void *memcpy(void *,const void *,size_t);
void *memset(void *,int,size_t);
int memcmp(const void *,const void *,size_t);
size_t strlen(const char *);
char *strcpy(char *,const char *);
char *strstr(const char *,const char *);
char *strrchr(const char *,int);
int strcmp(const char *,const char *);
int tolower(int);
int sprintf(char *,const char *,...);
int sscanf(const char *,const char *,...);
double strtod(const char *,char **);
unsigned long strtoul(const char *,char **,int);
double fabs(double);
"""
    path.write_text(libc + header + json_source + STUBS + crc + source + TESTS, encoding='utf-8')
    run_c_checks(path, ['test_format_persistence', 'test_format_recovery',
                        'test_format_fail_closed', 'test_format_replay'], native=True)
    print('Recording format persistence/recovery/replay checks passed (mock filesystem).')


if __name__ == '__main__':
    main()
