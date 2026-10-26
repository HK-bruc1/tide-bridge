"""Real format/recovery C and cJSON against an in-memory JL filesystem.

Checks persistence semantics and interruption replay, not physical SD durability.
"""
import re

from host_c_test_lib import ROOT, function, run_c_checks


STUBS = r'''


typedef unsigned char u8;
typedef unsigned int u32;
#define TCFG_T2620_FACTORY_USB_CDC_ENABLE 1
#define TCFG_T2620_FACTORY_USB_CDC_TEST_ENABLE 0
#define MIN(a,b) ((a)<(b)?(a):(b))
#define SEEK_SET 0
static int factory_busy;
static int power_fail, power_users, power_enters, idle_hints;
int rdx_adv_policy_business_enter(void) { ++power_enters; if(power_fail) return -1; ++power_users; return 0; }
void rdx_adv_policy_business_exit(void) { --power_users; }
void rdx_peripheral_power_vdd_idle_hint(void) { ++idle_hints; }
void rdx_peripheral_power_vdd_connected_changed_notify(void) {}
static int format_busy, writer_active, writer_resets, record_recovered;
int rdx_app_format_is_busy(void) { return format_busy; }
int rdx_record_storage_active(void) { return writer_active; }
void rdx_record_storage_reset(void) { ++writer_resets; }
void rdx_record_storage_recovered(void) { ++record_recovered; }
void rdx_record_format_service_ready(int result);
struct vfs_partition { u32 total_size; };
static struct vfs_partition partition={200000};
static u32 free_kb;
static int space_unknown, unmounted;
struct vfs_partition *fget_partition(const char *p) { return unmounted ? NULL : &partition; }
int fget_free_space(const char *p, u32 *space) { if(!space_unknown) *space=free_kb; return 17; }
void local_irq_disable(void) {}
void local_irq_enable(void) {}
static int snapshot_result, snapshot_calls;
int rdx_uxfile_download_snapshot(void) { ++snapshot_calls; return snapshot_result; }
int rdx_factory_audio_file_busy(void) { return factory_busy; }
u32 rdx_factory_audio_crc(u32 crc, const u8 *data, u32 length);
typedef struct { u8 *data; u32 data_len,left_len,sent_size,divpack,cur_pack_num,orig_pack_num; int loading; } uxfile_datfile_info_t;
#define RECORD_FORMATE_OPUS_16K_MONO 1
#define RECORD_FORMATE_OPUS_16K_STERO 2
#define RDX_RECORD_DELETE_REJECTED (-2)
#define VM_RDX_RECORD_FILE_NUM 147
typedef struct { u32 sn; char filename[64]; u8 record_scene; u32 start_time, frame_size; } uxfile_data_t;
typedef struct { char name[80]; u8 data[65536]; u32 size, pos; int present, opened; } mock_file;
static mock_file files[640];
static int fail_delete;
static const char *fail_delete_path;
static int fail_write, fail_flush, fail_close, fail_truncate, fail_read, fail_seek, handles;
static int fail_flush_call, flush_calls;
static u32 read_bytes;
static const char *fail_path;
static int writes;
static int fail_alloc;
static u32 vm_sn;
static int vm_present, vm_write_fail, vm_writes;
static int vm_read_fail, vm_readback_fail;
int syscfg_read(unsigned short id, void *data, unsigned short size) {
    if(id!=147 || size!=sizeof(vm_sn)) return -1;
    if(vm_read_fail) return vm_read_fail==1 ? VM_READ_DATA_ERR : vm_read_fail==2 ? size-1 : 0;
    if(!vm_present) return VM_INDEX_ERR;
    if(vm_readback_fail && vm_writes) {
        if(vm_readback_fail==1) return VM_READ_DATA_ERR;
        u32 different=vm_sn+1; memcpy(data,&different,sizeof(different)); return sizeof(different);
    }
    memcpy(data,&vm_sn,sizeof(vm_sn)); return sizeof(vm_sn);
}
int syscfg_write(unsigned short id, void *data, unsigned short size) {
    if(id!=147 || size!=sizeof(vm_sn)) return -1;
    ++vm_writes;
    if(vm_write_fail) return vm_write_fail==1 ? -1 : size-1;
    memcpy(&vm_sn,data,sizeof(vm_sn)); vm_present=1; return sizeof(vm_sn);
}
static void *format_malloc(size_t n) { return fail_alloc ? NULL : malloc(n); }
/* JL 的 realloc(NULL, n) 返回 NULL，首次分配必须显式 malloc。 */
static void *format_realloc(void *p,size_t n) { return fail_alloc || !p ? NULL : realloc(p,n); }
#define malloc format_malloc
#define realloc format_realloc
#define FILE mock_file
#define fopen mock_open
#define fclose mock_close
#define fread mock_read
#define fwrite mock_write
#define ftell mock_tell
#define printf(...) ((void)0)
static mock_file *lookup(const char *name) {
    for (u32 i=0; i<640; ++i) if (files[i].present && !strcmp(files[i].name,name)) return &files[i];
    return NULL;
}
FILE *mock_open(const char *name, const char *mode) {
    FILE *f=lookup(name);
    if (!f && mode[0]=='w') for (u32 i=0; i<640; ++i) if (!files[i].present) {
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
    memcpy(buf,f->data+f->pos,n); f->pos+=n; read_bytes+=n; return n;
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
int fseek(FILE *f,u32 pos,int mode) { if(fail_seek) return -1; f->pos=pos; return 0; }
int mock_tell(FILE *f) { return f->pos; }
int ftruncate(FILE *f,u32 n) { if(fail_truncate || n>sizeof(f->data)) return -1; f->size=n; return 0; }
int f_flush_wbuf(const char *p) { ++flush_calls; return fail_flush || (fail_flush_call && flush_calls==fail_flush_call) || strcmp(p,"storage/sd0/C/") ? -1 : 0; }
int fdelete_by_name(const char *p) { if(fail_delete || (fail_delete_path && !strcmp(p,fail_delete_path))) return -1; FILE *f=lookup(p); if(!f) return -1; f->present=0; return 0; }
struct vfscan { u32 file_number; FILE *items[640]; };
#define FSEL_BY_NUMBER 5
static struct vfscan scan_data;
struct vfscan *fscan(const char *root,const char *args,u8 depth) {
    memset(&scan_data,0,sizeof(scan_data));
    for(u32 i=0;i<640;++i) if(files[i].present && strstr(files[i].name, strstr(args,"MTA") ? ".mta" : ".raw"))
        scan_data.items[scan_data.file_number++]=&files[i];
    return &scan_data;
}
FILE *fselect(struct vfscan *s,int mode,int n) { return mock_open(s->items[n-1]->name,"r"); }
void fscan_release(struct vfscan *s) {}
void wdt_clear(void) {}
void os_time_dly(int n) {}
int frename(FILE *f,const char *name) { strcpy(f->name,"storage/sd0/C/"); strcat(f->name,name); return 0; }
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
    rf_serial_ready=rf_serial_max=0;
    format_busy=writer_active=writer_resets=record_recovered=rf_erasing=0;
    free_kb=100000; space_unknown=unmounted=0; partition.total_size=200000;
    rf_download=0; rf_io_users=rf_command_users=0; factory_busy=0;
    power_fail=power_users=power_enters=idle_hints=0;
    snapshot_result=snapshot_calls=0;
    vm_sn=0; vm_present=vm_write_fail=vm_writes=0;
    vm_read_fail=vm_readback_fail=0;
    fail_delete_path=NULL; fail_flush_call=flush_calls=0;
    fail_alloc=0;
    fail_seek=0; read_bytes=0;
    memset(files,0,sizeof(files)); memset(&rf_current,0,sizeof(rf_current));
    rf_deleted_name[0]=0; fail_delete=0; rf_boot_started=0; rf_error=fail_write=fail_flush=fail_close=fail_truncate=fail_read=handles=writes=0; fail_path=NULL;
    memset(&current,0,sizeof(current)); current.sn=7; current.start_time=100;
    current.frame_size=40; strcpy(current.filename,"abc123.raw");
    for(u32 i=0;i<sizeof(audio);++i) audio[i]=(i*19+3)&255;
}
int test_space_reserve(void) {
    reset(); CHECK(!rf_write(RF_DAT,"[]",2));
    u32 budget=123;
    free_kb=1024; CHECK(!rdx_record_format_space(&budget) && !budget);
    free_kb=1025; CHECK(!rdx_record_format_space(&budget) && budget==1024);
    free_kb=0; CHECK(!rdx_record_format_space(&budget) && !budget);
    space_unknown=1; CHECK(rdx_record_format_space(&budget)<0 && !budget);
    space_unknown=0; unmounted=1; CHECK(rdx_record_format_space(&budget)<0);
    CHECK(!rdx_record_format_capacity());
    unmounted=0; CHECK(rdx_record_format_capacity()==partition.total_size);
    free_kb=partition.total_size+1;
    CHECK(rdx_record_format_space(&budget)<0);
    free_kb=200000; lookup(RF_DAT)->size=2000000;
    CHECK(!rdx_record_format_space(&budget));
    CHECK(budget==200000u*1024-(2*(2000000+RF_OBJECT_SIZE)+65536));
    /* Large volumes/indexes cannot wrap into a false unlimited budget. */
    partition.total_size=free_kb=5000000; lookup(RF_DAT)->size=0xfffffff0u;
    CHECK(!rdx_record_format_space(&budget) && !budget);
    lookup(RF_DAT)->size=2;
    CHECK(!rdx_record_format_space(&budget) && budget==0xffffffffu);
    fail_close=1; CHECK(rdx_record_format_space(&budget)<0 && !budget);
    CHECK(!handles); return 0;
}
int test_explicit_format_recovery(void) {
    reset(); rf_service=-1; rf_error=-1; rf_boot_started=1;
    strcpy(rf_deleted_name,"abc123.raw"); rf_current.sn=7;
    CHECK(!rdx_record_format_erase_enter());
    CHECK(rdx_record_format_service_status()==1);
    CHECK(rdx_record_format_io_enter()!=0 && rdx_record_format_download_acquire()<0);
    CHECK(rdx_record_format_erase_enter()<0);
    rdx_record_format_erased();
    CHECK(!rf_boot_started && !rf_error && !rf_current.sn && !rf_deleted_name[0]);
    CHECK(!rdx_record_format_boot());
    CHECK(lookup(RF_DAT) && lookup(RF_DAT)->size==2 && !memcmp(lookup(RF_DAT)->data,"[]",2));
    rdx_record_format_service_ready(0); /* Cache ready must not release erase owner. */
    CHECK(rdx_record_format_service_status()==1);
    CHECK(rdx_record_format_download_acquire()<0 && !rf_download);
    rdx_record_format_erase_leave(0);
    CHECK(!rdx_record_format_service_status() && writer_resets==1 && record_recovered==1);
    CHECK(!rdx_record_format_erase_enter());
    rdx_record_format_erase_leave(-1);
    CHECK(rf_service==-1 && rf_error==-1 && writer_resets==1);
    CHECK(!rdx_record_format_erase_enter()); /* Failed erase remains retryable. */
    rdx_record_format_erase_leave(-1);
    for(int which=0;which<6;++which) {
        reset(); rf_service=-1;
        switch(which) {
        case 0: format_busy=1; break;
        case 1: writer_active=1; break;
        case 2: rf_io_users=1; break;
        case 3: rf_download=1; break;
        case 4: factory_busy=1; break;
        case 5: rf_service=1; break;
        }
        CHECK(rdx_record_format_erase_enter()<0 && !rf_erasing && !writer_resets);
    }
    return 0;
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
int test_factory_file_identity(void) {
    reset(); CHECK(seed_mono()==0); rf_service=0; factory_busy=1;
    u32 size=0,crc=0;
    FILE *f=rdx_record_format_factory_open(7,"abc123.raw",100,&size,&crc);
    CHECK(f && handles==1 && f->pos==0 && size==4000);
    CHECK(crc==rdx_factory_audio_crc(0,audio,4000)); fclose(f);
    CHECK(rdx_record_format_service_status()==1);
    CHECK(!rdx_record_format_factory_open(8,"abc123.raw",100,&size,&crc));
    CHECK(!rdx_record_format_factory_open(7,"abc123.raw",101,&size,&crc));
    CHECK(!rdx_record_format_factory_open(7,"../abc.raw",100,&size,&crc));
    lookup(RF_ROOT "abc123.raw")->data[0]^=1;
    CHECK(!rdx_record_format_factory_open(7,"abc123.raw",100,&size,&crc));
    CHECK(handles==0); factory_busy=0;
    CHECK(!rdx_record_format_factory_open(7,"abc123.raw",100,&size,&crc));
    return 0;
}
int test_download_admission(void) {
    reset(); rf_service=0;
    CHECK(!rdx_record_format_io_enter() && rf_io_users==1);
    CHECK(rdx_record_format_download_acquire()<0 && !rf_download);
    CHECK(!rdx_record_format_io_enter() && rf_io_users==2);
    rdx_record_format_io_leave(); rdx_record_format_io_leave();
    CHECK(!rdx_record_format_download_acquire() && rf_download);
    CHECK(rdx_record_format_service_status()==1 && rdx_record_format_io_enter()!=0);
    CHECK(rf_io_users==0 && rdx_record_format_download_acquire()<0);
    rdx_record_format_service_ready(-1);
    rdx_record_format_download_release();
    CHECK(rdx_record_format_service_status()==-1 && rdx_record_format_download_acquire()<0);
    rdx_record_format_service_ready(0);
    factory_busy=1; CHECK(rdx_record_format_download_acquire()<0);
    factory_busy=0; CHECK(!rdx_record_format_download_acquire());
    rdx_record_format_download_release(); CHECK(!rdx_record_format_service_status());
    rdx_record_format_command_enter();
    CHECK(rf_command_users==1 && rdx_record_format_download_acquire()<0);
    rdx_record_format_command_leave();
    CHECK(!rdx_record_format_download_acquire());
    rdx_record_format_command_enter();
    CHECK(rdx_record_format_protocol_busy() && rf_command_users==1);
    CHECK(rdx_record_format_io_enter()!=0);
    rdx_record_format_download_release();
    CHECK(!rdx_record_format_protocol_busy() && rdx_record_format_download_acquire()<0);
    rdx_record_format_command_leave();
    CHECK(!rdx_record_format_download_acquire());
    rdx_record_format_download_release();
    return 0;
}
int test_stream_only_storage_query(void) {
    reset(); rf_service=0;
    CHECK(!rdx_record_format_stream_status());
    CHECK(!rdx_record_format_download_acquire());
    int enters=power_enters, hints=idle_hints;
    CHECK(rdx_record_format_service_status() && !rdx_record_format_stream_status());
    rf_service=1; CHECK(rdx_record_format_stream_status());
    rf_service=-1; CHECK(rdx_record_format_stream_status()<0);
    rf_service=0; rf_erasing=1; CHECK(rdx_record_format_stream_status());
    rf_erasing=0; factory_busy=1; CHECK(rdx_record_format_stream_status());
    factory_busy=0; rdx_record_format_storage_fault();
    CHECK(rdx_record_format_stream_status()<0);
    CHECK(rf_download && !rf_io_users && !rf_command_users);
    CHECK(power_enters==enters && idle_hints==hints);
    rf_error=0; CHECK(!rdx_record_format_stream_status());
    CHECK(rdx_record_format_io_enter()!=0 && rdx_record_format_protocol_busy());
    rdx_record_format_download_release();
    CHECK(!rdx_record_format_service_status() && !rdx_record_format_stream_status());
    return 0;
}
int test_power_admission(void) {
    reset(); rf_service=0;
    rdx_record_format_command_enter();
    CHECK(!rdx_record_format_flush_enter());
    CHECK(rdx_record_format_io_users()==2 && power_enters==0);
    rdx_record_format_command_leave(); rdx_record_format_command_leave();
    CHECK(!rdx_record_format_io_users() && idle_hints==1 && !power_users);
    power_fail=1;
    CHECK(rdx_record_format_io_enter()<0);
    CHECK(!rdx_record_format_io_users() && !power_users);
    power_fail=0;
    CHECK(!rdx_record_format_io_enter());
    CHECK(rf_io_users==1 && power_users==1);
    rdx_record_format_io_leave(); rdx_record_format_io_leave();
    CHECK(!power_users && !rdx_record_format_io_users());
    rf_service=-1;
    CHECK(rdx_record_format_flush_enter()<0 && rdx_record_format_io_enter()<0);
    CHECK(!rdx_record_format_io_users());
    return 0;
}
int test_verified_raw_reader(void) {
    reset(); CHECK(seed_mono()==0);
    rf_meta meta={0};
    CHECK(rf_read_meta("abc123.raw",&meta)==0);
    CHECK(meta.sn==7 && meta.start==100 && meta.format==1);
    FILE *handle=NULL; u32 size=0;
    FILE *raw=lookup(RF_ROOT "abc123.raw");
    raw->size=48000;
    u32 before=read_bytes; int before_writes=writes;
    CHECK(rf_open_verified_raw(&meta,16777216,&handle,&size)==0);
    CHECK(handle && size==48000 && handle->pos==0 && handles==1);
    CHECK(read_bytes-before==40 && writes==before_writes);
    CHECK(fclose(handle)==0 && handles==0);
    rf_service=0; factory_busy=1; u32 crc=0;
    CHECK(!rdx_record_format_factory_open(7,"abc123.raw",100,&size,&crc));
    CHECK(handles==0);
    CHECK(rf_open_verified_raw(&meta,40000,&handle,&size)==-1);
    CHECK(!handle && !size && handles==0);
    raw->size=4001;
    CHECK(rf_open_verified_raw(&meta,16777216,&handle,&size)==-1 && handles==0);
    raw->size=0;
    CHECK(rf_open_verified_raw(&meta,16777216,&handle,&size)==-1 && handles==0);
    raw->size=4000; raw->data[0]^=1;
    CHECK(rf_open_verified_raw(&meta,16777216,&handle,&size)==-1 && handles==0);
    raw->data[0]^=1;
    fail_seek=1;
    CHECK(rf_open_verified_raw(&meta,16777216,&handle,&size)==-1 && handles==0);
    fail_seek=0; fail_read=1;
    CHECK(rf_open_verified_raw(&meta,16777216,&handle,&size)==-1 && handles==0);
    fail_close=1;
    CHECK(rf_open_verified_raw(&meta,16777216,&handle,&size)==-2);
    CHECK(!handle && !size && handles==0);
    fail_read=0;
    CHECK(rf_read_meta("abc123.raw",&meta)==-2 && handles==0);
    fail_close=0;
    CHECK(rf_read_meta(NULL,&meta)==-1);
    CHECK(rf_read_meta("../abc.raw",&meta)==-1);
    CHECK(rf_read_meta("abc123.raw",NULL)==-1);
    CHECK(rf_read_meta("abc123.raw",&meta)==0);
    meta.format=257;
    CHECK(rf_open_verified_raw(&meta,16777216,&handle,&size)==-1 && handles==0);
    reset(); current.frame_size=80;
    CHECK(rdx_record_format_begin(&current,2)==0);
    CHECK(rdx_record_format_frame(&current,2,audio,80)==0);
    CHECK(rf_write(RF_ROOT "abc123.raw",audio,sizeof(audio))==0);
    CHECK(rf_read_meta("abc123.raw",&meta)==0 && meta.format==2);
    before=read_bytes;
    CHECK(rf_open_verified_raw(&meta,16777216,&handle,&size)==0);
    CHECK(size==4000 && handle->pos==0 && read_bytes-before==80);
    CHECK(fclose(handle)==0);
    rf_service=0; factory_busy=1;
    CHECK(!rdx_record_format_factory_open(7,"abc123.raw",100,&size,&crc));
    CHECK(handles==0); factory_busy=0;
    return 0;
}
static int seed_index(const char *json) {
    FILE *existing=lookup(RF_DAT);
    if(existing) existing->present=0;
    return rf_write(RF_DAT,json,strlen(json));
}
int test_indexed_raw_reader(void) {
    reset(); CHECK(seed_mono()==0);
    const char *valid="[{\"name\":\"abc123.raw\",\"sn\":8,\"size\":4000,\"start_time\":100,\"end_time\":102,\"frame_size\":40,\"opus\":1084752896,\"marks\":[{\"v\":\"}[]\"}]}]";
    CHECK(seed_index(valid)==0);
    rf_meta meta={0}; CHECK(rf_read_meta("abc123.raw",&meta)==0 && meta.sn==7);
    memset(&rf_reader,0x5a,sizeof(rf_reader));
    memset(rf_json,0x6b,sizeof(rf_json));
    rf_reader_state previous=rf_reader;
    u32 before=read_bytes; int before_writes=writes;
    FILE *handle=NULL; u32 size=0;
    CHECK(rf_open_indexed_raw(8,"abc123.raw",16777216,&handle,&size)==0);
    CHECK(size==4000 && handle && handle->pos==0 && handles==1);
    CHECK(read_bytes-before==sizeof(rf_meta)+strlen(valid)+40);
    CHECK(writes==before_writes && !memcmp(&previous,&rf_reader,sizeof(previous)));
    CHECK((u8)rf_json[0]==0x6b && (u8)rf_json[RF_OBJECT_SIZE-1]==0x6b);
    CHECK(fclose(handle)==0);
    void *public_handle=NULL;
    rf_service=0;
    CHECK(rdx_record_format_download_open(8,"abc123.raw",&public_handle,&size)<0 && !snapshot_calls);
    CHECK(!rdx_record_format_download_acquire());
    CHECK(!rdx_record_format_download_open(8,"abc123.raw",&public_handle,&size));
    CHECK(size==4000 && public_handle && snapshot_calls==1 && handles==1);
    CHECK(!fclose(public_handle));
    CHECK(!rdx_record_format_download_list_open(&public_handle,&size));
    CHECK(size==strlen(valid) && public_handle && handles==1);
    CHECK(!fclose(public_handle));
    snapshot_result=-2;
    CHECK(rdx_record_format_download_list_open(&public_handle,&size)==-2);
    CHECK(!public_handle && !size && !handles);
    CHECK(rdx_record_format_download_open(8,"abc123.raw",&public_handle,&size)==-2);
    CHECK(!public_handle && !size && !handles);
    rdx_record_format_download_release();
    CHECK(rdx_record_format_download_list_open(&public_handle,&size)<0);
    snapshot_result=0;
    CHECK(rf_open_indexed_raw(7,"abc123.raw",16777216,&handle,&size)==-1);
    CHECK(!handle && !size && handles==0);
    CHECK(rf_open_indexed_raw(8,"abc123.raw",3999,&handle,&size)==-1);
    lookup(RF_ROOT "abc123.raw")->size=4040;
    CHECK(rf_open_indexed_raw(8,"abc123.raw",16777216,&handle,&size)==-1);
    CHECK(!handle && !size && handles==0);
    lookup(RF_ROOT "abc123.raw")->size=4000;
    CHECK(seed_index("[]")==0);
    CHECK(rf_open_indexed_raw(8,"abc123.raw",16777216,&handle,&size)==-3);
    const char *invalid[]={
        "[{\"name\":\"abc123.raw\",\"sn\":8}]",
        "[{\"name\":\"abc123.raw\",\"sn\":8,\"sn\":9}]",
        "[{\"name\":\"abc123.raw\",\"name\":\"def123.raw\",\"sn\":8}]",
        "[{\"name\":\"abc123.raw\",\"sn\":8.5}]",
        "[{\"name\":\"def123.raw\",\"sn\":8}]",
        "[{\"name\":\"abc123.raw\",\"sn\":8,\"size\":4000,\"start_time\":101,\"end_time\":102,\"frame_size\":40,\"opus\":1084752896}]",
        "[{\"name\":\"abc123.raw\",\"sn\":8,\"size\":4000,\"start_time\":100,\"end_time\":99,\"frame_size\":40,\"opus\":1084752896}]",
        "[{\"name\":\"abc123.raw\",\"sn\":8,\"size\":4000,\"start_time\":100,\"end_time\":102,\"frame_size\":80,\"opus\":1084752896}]",
        "[{\"name\":\"abc123.raw\",\"sn\":8,\"size\":4000,\"start_time\":100,\"end_time\":102,\"frame_size\":40,\"opus\":0}]",
        "[{\"name\":\"abc123.raw\",\"sn\":8,\"size\":4001,\"start_time\":100,\"end_time\":102,\"frame_size\":40,\"opus\":1084752896}]",
        "[{\"name\":\"abc123.raw\",\"sn\":8,\"size\":4000,\"start_time\":100,\"end_time\":102,\"frame_size\":40,\"opus\":1084752896},]",
        "[{\"name\":\"abc123.raw\",\"sn\":8,\"size\":4000,\"start_time\":100,\"end_time\":102,\"frame_size\":40,\"opus\":1084752896}]junk",
        "[{\"name\":\"abc123.raw\",\"sn\":8,\"size\":4000,\"start_time\":100,\"end_time\":102,\"frame_size\":40,\"opus\":1084752896},{\"name\":\"def123.raw\",\"sn\":8}]",
        "[{\"name\":\"abc123.raw\",\"sn\":8,\"size\":4000,\"start_time\":100,\"end_time\":102,\"frame_size\":40,\"opus\":1084752896},{\"name\":\"abc123.raw\",\"sn\":9}]"
    };
    for(u32 index=0;index<sizeof(invalid)/sizeof(invalid[0]);++index) {
        CHECK(seed_index(invalid[index])==0);
        CHECK(rf_open_indexed_raw(8,"abc123.raw",16777216,&handle,&size)==-1);
        CHECK(!handle && !size && handles==0);
    }
    CHECK(seed_index(valid)==0);
    fail_read=1; CHECK(rf_read_index_identity(8,&meta,&size)==-1); fail_read=0;
    fail_close=1; CHECK(rf_read_index_identity(8,&meta,&size)==-2); fail_close=0;
    CHECK(!size && handles==0);
    CHECK(rf_open_indexed_raw(8,"../abc.raw",16777216,&handle,&size)==-1);
    CHECK(rf_open_indexed_raw(8,"abc123.raw",16777216,NULL,&size)==-1);
    meta.format=2; CHECK(rf_read_index_identity(8,&meta,&size)==-1);
    return 0;
}
int test_large_download_open(void) {
    u32 lengths[]={16777240u,143340000u,2147483680u,4294967280u};
    for(unsigned index=0;index<sizeof(lengths)/sizeof(lengths[0]);++index) {
        reset(); CHECK(seed_mono()==0); rf_service=0;
        char json[240];
        sprintf(json,"[{\"name\":\"abc123.raw\",\"sn\":8,\"size\":%u,\"start_time\":100,\"end_time\":102,\"frame_size\":40,\"opus\":1084752896}]",lengths[index]);
        CHECK(seed_index(json)==0);
        /* 稀疏文件模拟：打开时只读取元数据、索引和首帧，不扫描整文件。 */
        lookup(RF_ROOT "abc123.raw")->size=lengths[index];
        CHECK(!rdx_record_format_download_acquire());
        void *handle=NULL; u32 size=0, before=read_bytes; int before_writes=writes;
        CHECK(!rdx_record_format_download_open(8,"abc123.raw",&handle,&size));
        CHECK(handle && size==lengths[index] && handles==1 && ((FILE *)handle)->pos==0);
        CHECK(read_bytes-before==sizeof(rf_meta)+strlen(json)+40 && writes==before_writes);
        CHECK(!fclose(handle));
        rdx_record_format_download_release();
        factory_busy=1; u32 crc=0;
        CHECK(!rdx_record_format_factory_open(7,"abc123.raw",100,&size,&crc));
        CHECK(!handles);
    }
    return 0;
}
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
    /* 文件错误应体现在条目中，不得升级为整机准入错误。 */
    reset(); CHECK(seed_mono()==0);
    lookup(RF_ROOT "abc123.mta")->data[0]^=1;
    CHECK(recover()==0 && lookup(RF_DAT) && handles==0);
    cJSON *root=read_dat(); CHECK(cJSON_GetArraySize(root)==1);
    CHECK(num(cJSON_GetArrayItem(root,0),"frame_size")==1); cJSON_Delete(root);
    reset(); CHECK(seed_mono()==0); lookup(RF_ROOT "abc123.raw")->data[0]^=1;
    CHECK(recover()==0 && lookup(RF_DAT) && handles==0);
    reset(); CHECK(seed_mono()==0); lookup(RF_ROOT "abc123.raw")->size=3999;
    CHECK(recover()==0 && lookup(RF_DAT) && handles==0);
    CHECK(lookup(RF_ROOT "abc123.raw")->size==3999); /* 不得静默截断 */
    reset(); CHECK(seed_mono()==0); fdelete_by_name(RF_ROOT "abc123.raw");
    CHECK(recover()==0 && lookup(RF_DAT)); /* 孤立 MTA 仍可管理 */
    root=read_dat(); CHECK(cJSON_GetArraySize(root)==1);
    CHECK(!strcmp(cJSON_GetObjectItem(cJSON_GetArrayItem(root,0),"name")->valuestring,"abc123.mta")); cJSON_Delete(root);
    reset(); CHECK(seed_mono()==0);
    const char *bad="[{\"sn\":3";
    CHECK(rf_write(RF_DAT,bad,strlen(bad))==0);
    CHECK(recover()==0 && handles==0);
    CHECK(lookup(RF_ROOT "rf000000.bak")->size==strlen(bad));
    root=read_dat(); CHECK(root && cJSON_GetArraySize(root)==1); cJSON_Delete(root);
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
    CHECK(recover()==0 && !lookup(RF_COMMIT) && handles==0);
    CHECK(lookup(RF_ROOT "rf000000.bak"));
    return 0;
}
int test_allocation_failure(void) {
    reset(); CHECK(seed_mono()==0); CHECK(recover()==0);
    FILE *dat=lookup(RF_DAT); CHECK(dat);
    u32 length=dat->size, hash=rf_hash(dat->data,length);
    fail_alloc=1;
    CHECK(recover()<0);
    CHECK(dat->size==length && rf_hash(dat->data,length)==hash);
    CHECK(lookup(RF_ROOT "abc123.raw") && lookup(RF_ROOT "abc123.mta"));
    uxfile_datfile_info_t info={0};
    CHECK(rdx_record_format_list(&info)==&info && !info.data && !info.data_len);
    CHECK(handles==0);
    fail_alloc=0; CHECK(recover()==0);
    CHECK(rdx_record_format_list(&info)==&info && info.data_len==length);
    CHECK(info.divpack==1);
    free(info.data); CHECK(handles==0);
    return 0;
}
int test_delete_durable_identity(void) {
    reset(); CHECK(seed_mono()==0); CHECK(recover()==0);
    CHECK(rdx_record_format_delete(9,"abc123.raw")==RDX_RECORD_DELETE_REJECTED);
    CHECK(lookup(RF_ROOT "abc123.raw") && lookup(RF_ROOT "abc123.mta"));
    CHECK(rdx_record_format_delete(7,"../abc.raw")==RDX_RECORD_DELETE_REJECTED);
    fail_delete=1;
    CHECK(rdx_record_format_delete(7,"abc123.raw")<0);
    CHECK(lookup(RF_DELETE));
    fail_delete=0;
    CHECK(rdx_record_format_delete(7,"abc123.raw")==0);
    CHECK(!lookup(RF_ROOT "abc123.raw") && !lookup(RF_ROOT "abc123.mta"));
    CHECK(!lookup(RF_DELETE) && !lookup(RF_COMMIT));
    CHECK(rdx_record_format_delete(7,"abc123.raw")==0);
    CHECK(recover()==0 && handles==0);
    cJSON *root=read_dat(); CHECK(root && cJSON_GetArraySize(root)==0); cJSON_Delete(root);
    return 0;
}
int test_delete_interrupted_index(void) {
    reset(); CHECK(seed_mono()==0);
    CHECK(rf_write(RF_ROOT "def123.raw",audio,40)==0);
    CHECK(recover()==0);
    fail_write=1; fail_path=RF_DAT;
    CHECK(rdx_record_format_delete(7,"abc123.raw")<0);
    CHECK(lookup(RF_DELETE) && lookup(RF_COMMIT));
    fail_write=0; fail_path=NULL; rf_error=0;
    CHECK(recover()==0 && handles==0);
    CHECK(!lookup(RF_ROOT "abc123.raw") && lookup(RF_ROOT "def123.raw"));
    CHECK(!lookup(RF_DELETE) && !lookup(RF_COMMIT));
    cJSON *root=read_dat(); CHECK(root && cJSON_GetArraySize(root)==1); cJSON_Delete(root);
    return 0;
}
int test_more_than_300_and_duplicate_sn(void) {
    reset();
    for(int i=0;i<301;++i) {
        char name[40]; sprintf(name,RF_ROOT "%06x.raw",i+1);
        CHECK(rf_write(name,audio,40)==0);
    }
    CHECK(recover()==0 && handles==0);
    cJSON *root=read_dat(); CHECK(root && cJSON_GetArraySize(root)==301); cJSON_Delete(root);
    uxfile_datfile_info_t info={0};
    CHECK(rdx_record_format_list(&info)==&info && info.data_len>0);
    root=cJSON_Parse((char*)info.data); CHECK(root && cJSON_GetArraySize(root)==301);
    CHECK(info.divpack==(info.data_len+459)/460);
    /* 按协议使用的总包数读取，确保大目录可以完整传输。 */
    u32 received=0;
    for (u32 pack=1; pack<=info.divpack; ++pack) {
        u32 offset=(pack-1)*460;
        CHECK(offset<info.data_len);
        u32 bytes=info.data_len-offset;
        if(bytes>460) bytes=460;
        CHECK(!memcmp(info.data+offset,lookup(RF_DAT)->data+offset,bytes));
        received+=bytes;
    }
    CHECK(received==info.data_len);
    cJSON_Delete(root); free(info.data);
    CHECK(rdx_record_format_delete(0x10000000u+301,"00012d.raw")==0);
    CHECK(recover()==0);
    root=read_dat(); CHECK(root && cJSON_GetArraySize(root)==300); cJSON_Delete(root);
    reset(); CHECK(seed_mono()==0);
    strcpy(current.filename,"def123.raw");
    CHECK(rdx_record_format_begin(&current,1)==0);
    CHECK(rdx_record_format_frame(&current,1,audio,40)==0);
    CHECK(rf_write(RF_ROOT "def123.raw",audio,40)==0);
    CHECK(recover()==0);
    root=read_dat(); CHECK(root && cJSON_GetArraySize(root)==2);
    CHECK(num(cJSON_GetArrayItem(root,0),"sn")!=num(cJSON_GetArrayItem(root,1),"sn"));
    cJSON_Delete(root);
    return 0;
}

'''


TESTS += r'''
int test_dat_mta_collision(void) {
    reset(); CHECK(seed_mono()==0);
    current.sn=8; strcpy(current.filename,"def123.raw");
    CHECK(rdx_record_format_begin(&current,1)==0);
    CHECK(rdx_record_format_frame(&current,1,audio,40)==0);
    CHECK(rf_write(RF_ROOT "def123.raw",audio,sizeof(audio))==0);
    const char *dat="[{\"sn\":8,\"name\":\"abc123.raw\",\"frame_size\":40,\"opus\":1084752896}]";
    CHECK(rf_write(RF_DAT,dat,strlen(dat))==0 && recover()==0);
    cJSON *root=read_dat(); CHECK(cJSON_GetArraySize(root)==2);
    int duplicate=num(cJSON_GetArrayItem(root,0),"sn")==num(cJSON_GetArrayItem(root,1),"sn");
    CHECK(num(cJSON_GetArrayItem(root,0),"sn")==8);
    cJSON_Delete(root); CHECK(!duplicate);
    CHECK(recover()==0);
    root=read_dat(); CHECK(cJSON_GetArraySize(root)==2);
    CHECK(num(cJSON_GetArrayItem(root,0),"sn")==8);
    CHECK(num(cJSON_GetArrayItem(root,1),"sn")!=8);
    cJSON_Delete(root);
    rf_meta meta={0};
    CHECK(rf_read_meta("abc123.raw",&meta)==0 && meta.sn==7);
    CHECK(rf_read_meta("def123.raw",&meta)==0 && meta.sn==8);
    u32 next=0; CHECK(rdx_record_format_reserve_sn(&next)==0 && next==9);
    CHECK(handles==0);
    return 0;
}
static int seed_named(u32 sn, const char *name) {
    current.sn=sn; strcpy(current.filename,name);
    if(rdx_record_format_begin(&current,1) || rdx_record_format_frame(&current,1,audio,40)) return -1;
    char path[40]; rf_path(path,name,0);
    return rf_write(path,audio,sizeof(audio));
}
static u32 named_sn(cJSON *root, const char *name) {
    for(int i=0;i<cJSON_GetArraySize(root);++i) {
        cJSON *obj=cJSON_GetArrayItem(root,i);
        cJSON *n=cJSON_GetObjectItem(obj,"name");
        if(cJSON_IsString(n) && !strcmp(n->valuestring,name)) return num(obj,"sn");
    }
    return 0;
}
int test_sn_reservation_migration(void) {
    reset(); CHECK(seed_named(44,"4f24b8.raw")==0);
    CHECK(seed_named(316899292u,"20436e.raw")==0);
    current.sn=23; strcpy(current.filename,"e37fdb.raw");
    CHECK(rdx_record_format_begin(&current,1)==0);
    CHECK(rdx_record_format_frame(&current,1,audio,40)==0);
    const char *dat="[{\"sn\":316899292,\"name\":\"20436e.raw\",\"frame_size\":40,\"opus\":1084752896},"
                    "{\"sn\":44,\"name\":\"4f24b8.raw\",\"frame_size\":40,\"opus\":1084752896}]";
    CHECK(rf_write(RF_DAT,(void *)dat,strlen(dat))==0);
    vm_present=1; vm_sn=316899293u;
    CHECK(recover()==0 && handles==0);
    cJSON *root=read_dat(); CHECK(root && cJSON_GetArraySize(root)==3);
    CHECK(named_sn(root,"20436e.raw")==316899292u);
    CHECK(named_sn(root,"4f24b8.raw")==44);
    u32 orphan=named_sn(root,"e37fdb.mta");
    CHECK(orphan==316899291u); cJSON_Delete(root);
    u32 sn=0; CHECK(rdx_record_format_reserve_sn(&sn)==0 && sn==45);
    CHECK(vm_present && vm_sn==45 && vm_writes==1);
    CHECK(rdx_record_format_reserve_sn(&sn)==0 && sn==46 && vm_sn==46);
    CHECK(recover()==0);
    CHECK(rdx_record_format_reserve_sn(&sn)==0 && sn==47);
    root=read_dat(); CHECK(named_sn(root,"20436e.raw")==316899292u);
    CHECK(named_sn(root,"e37fdb.mta")==orphan); cJSON_Delete(root);
    CHECK(rdx_record_format_delete(45,"20436e.raw")==RDX_RECORD_DELETE_REJECTED);
    CHECK(rdx_record_format_delete(316899292u,"20436e.raw")==0);
    CHECK(!lookup(RF_ROOT "20436e.raw") && lookup(RF_ROOT "4f24b8.raw"));
    CHECK(rdx_record_format_delete(orphan,"e37fdb.mta")==0 && handles==0);
    return 0;
}
int test_sn_reservation_full_index_and_empty(void) {
    reset(); CHECK(rf_write(RF_DAT,"[]",2)==0);
    CHECK(recover()==0);
    u32 sn=999; CHECK(rdx_record_format_reserve_sn(&sn)==0 && sn==1 && vm_sn==1);
    CHECK(recover()==0);
    CHECK(rdx_record_format_reserve_sn(&sn)==0 && sn==2);
    reset(); CHECK(seed_named(44,"4f24b8.raw")==0);
    vm_present=1; vm_sn=70; CHECK(recover()==0);
    CHECK(rdx_record_format_reserve_sn(&sn)==0 && sn==71 && vm_sn==71);
    reset();
    char dat[30000]; u32 length=0;
    length+=sprintf(dat+length,"[");
    for(int i=0;i<301;++i) {
        length+=sprintf(dat+length,"%s{\"sn\":%u,\"name\":\"%06x.raw\"}",
                        i ? "," : "",i==300 ? 900u : (u32)i+1,i+1);
    }
    length+=sprintf(dat+length,"]");
    CHECK(rf_write(RF_DAT,dat,length)==0 && recover()==0);
    CHECK(rdx_record_format_reserve_sn(&sn)==0 && sn==901 && vm_sn==901);
    CHECK(handles==0); return 0;
}
int test_sn_reservation_failures_and_limit(void) {
    for(int failure=1;failure<=2;++failure) {
        reset(); CHECK(seed_named(44,"4f24b8.raw")==0);
        vm_present=1; vm_sn=44; CHECK(recover()==0);
        vm_write_fail=failure; u32 sn=99;
        CHECK(rdx_record_format_reserve_sn(&sn)<0 && !sn && vm_sn==44);
        vm_write_fail=0; CHECK(recover()==0);
        CHECK(rdx_record_format_reserve_sn(&sn)==0 && sn==45 && vm_sn==45);
        /* Reservation itself is durable, even if no RAW is ever created. */
        CHECK(recover()==0);
        CHECK(rdx_record_format_reserve_sn(&sn)==0 && sn==46);
    }
    for(int failure=1;failure<=3;++failure) {
        reset(); CHECK(seed_named(44,"4f24b8.raw")==0);
        vm_present=1; vm_sn=44; CHECK(recover()==0);
        vm_read_fail=failure; u32 sn=99;
        CHECK(rdx_record_format_reserve_sn(&sn)<0 && !sn && !vm_writes);
        vm_read_fail=0;
        CHECK(rdx_record_format_reserve_sn(&sn)==0 && sn==45);
    }
    for(int failure=1;failure<=2;++failure) {
        reset(); CHECK(seed_named(44,"4f24b8.raw")==0);
        vm_present=1; vm_sn=44; CHECK(recover()==0);
        vm_readback_fail=failure; u32 sn=99;
        CHECK(rdx_record_format_reserve_sn(&sn)<0 && !sn && vm_sn==45);
        vm_readback_fail=0;
        CHECK(recover()==0);
        CHECK(rdx_record_format_reserve_sn(&sn)==0 && sn==46);
    }
    reset(); CHECK(seed_named(44,"4f24b8.raw")==0);
    vm_present=1; vm_sn=44; CHECK(recover()==0);
    vm_write_fail=1; u32 skipped=99;
    CHECK(rdx_record_format_reserve_sn(&skipped)<0 && !skipped);
    vm_write_fail=0;
    CHECK(rdx_record_format_reserve_sn(&skipped)==0 && skipped==46);
    reset(); vm_present=1; vm_sn=0x0ffffffeu;
    CHECK(recover()==0); u32 sn=0;
    CHECK(rdx_record_format_reserve_sn(&sn)==0 && sn==0x0fffffffu);
    int before=vm_writes; sn=99;
    CHECK(rdx_record_format_reserve_sn(&sn)<0 && !sn && vm_writes==before);
    CHECK(recover()==0); sn=99;
    CHECK(rdx_record_format_reserve_sn(&sn)<0 && !sn && vm_sn==0x0fffffffu);
    CHECK(handles==0); return 0;
}
static int seed_short(u32 bytes) {
    vm_present=1; vm_sn=6;
    if(recover() || rdx_record_format_reserve_sn(&current.sn) || seed_mono()) return -1;
    lookup(RF_ROOT "abc123.raw")->size=bytes;
    return 0;
}
int test_empty_cancel(void) {
    for(u8 format=1;format<=2;++format) {
        reset(); vm_present=1; vm_sn=6;
        CHECK(recover()==0 && rdx_record_format_reserve_sn(&current.sn)==0);
        CHECK(current.sn==7);
        current.frame_size=format==1 ? 40 : 80;
        CHECK(rdx_record_format_begin(&current,format)==0);
        int before=writes, flushed=flush_calls;
        CHECK(rdx_record_format_cancel_empty(&current)==1);
        CHECK(!rf_current.sn && !rf_error && !handles);
        CHECK(writes==before && flush_calls==flushed && vm_sn==7);
        CHECK(!lookup(RF_ROOT "abc123.raw") && !lookup(RF_ROOT "abc123.mta"));
        CHECK(!lookup(RF_DISCARD));
        CHECK(rdx_record_format_cancel_empty(&current)==0);
        CHECK(recover()==0);
        cJSON *root=read_dat(); CHECK(root && !cJSON_GetArraySize(root)); cJSON_Delete(root);
        CHECK(rdx_record_format_reserve_sn(&current.sn)==0 && current.sn==8);
        CHECK(rdx_record_format_begin(&current,format)==0);
        CHECK(rdx_record_format_frame(&current,format,audio,current.frame_size)==0);
        /* 只要已处理过一个可存储帧，RAW 缺失就不能按空会话取消；
         * 暂停前已有数据、恢复段为零帧的会话同样不能取消。 */
        CHECK(rdx_record_format_cancel_empty(&current)==0 && rf_current.ready);
        CHECK(!lookup(RF_ROOT "abc123.raw") && lookup(RF_ROOT "abc123.mta"));
        CHECK(rf_write(RF_ROOT "abc123.raw",audio,4000)==0);
        CHECK(rdx_record_format_cancel_empty(&current)==0);
        CHECK(lookup(RF_ROOT "abc123.raw") && lookup(RF_ROOT "abc123.mta"));
    }
    for(int wrong=0;wrong<5;++wrong) {
        reset(); CHECK(rdx_record_format_begin(&current,1)==0);
        uxfile_data_t other=current;
        if(wrong==0) ++other.sn;
        if(wrong==1) strcpy(other.filename,"def123.raw");
        if(wrong==2) ++other.start_time;
        if(wrong==3) ++other.record_scene;
        if(wrong==4) other.frame_size=80;
        CHECK(rdx_record_format_cancel_empty(&other)==0 && rf_current.sn==7);
        CHECK(rdx_record_format_cancel_empty(NULL)==0 && rf_current.sn==7);
        CHECK(rdx_record_format_cancel_empty(&current)==1);
    }
    reset(); CHECK(rdx_record_format_begin(&current,1)==0);
    writer_active=1;
    CHECK(rdx_record_format_cancel_empty(&current)<0 && rf_current.sn==7);
    writer_active=0; rdx_record_format_storage_fault();
    CHECK(rdx_record_format_cancel_empty(&current)<0 && rf_error<0 && rf_current.sn==7);
    reset(); CHECK(rdx_record_format_begin(&current,1)==0); fail_write=1;
    CHECK(rdx_record_format_frame(&current,1,audio,40)<0);
    CHECK(rdx_record_format_cancel_empty(&current)<0 && rf_error<0 && rf_current.ready);
    CHECK(!handles); return 0;
}
int test_short_discard_admission(void) {
    reset(); CHECK(seed_short(3600)==0);
    CHECK(rdx_record_format_discard_short(&current)==0);
    CHECK(!lookup(RF_ROOT "abc123.raw") && !lookup(RF_ROOT "abc123.mta"));
    CHECK(!lookup(RF_DISCARD) && handles==0);
    CHECK(recover()==0);
    cJSON *root=read_dat(); CHECK(root && !cJSON_GetArraySize(root)); cJSON_Delete(root);
    u32 sn=0; CHECK(rdx_record_format_reserve_sn(&sn)==0 && sn==8);
    reset(); CHECK(seed_short(4000)==0);
    CHECK(rdx_record_format_discard_short(&current)<0);
    CHECK(lookup(RF_ROOT "abc123.raw") && lookup(RF_ROOT "abc123.mta"));
    reset(); CHECK(seed_short(0x80000000u)==0);
    CHECK(rdx_record_format_discard_short(&current)<0);
    CHECK(lookup(RF_ROOT "abc123.raw") && lookup(RF_ROOT "abc123.mta"));
    CHECK(!lookup(RF_DISCARD) && !handles);
    for(int wrong=0;wrong<3;++wrong) {
        reset(); CHECK(seed_short(3600)==0);
        uxfile_data_t other=current;
        if(wrong==0) ++other.sn;
        if(wrong==1) strcpy(other.filename,"def123.raw");
        if(wrong==2) lookup(RF_ROOT "abc123.mta")->data[0]^=1;
        CHECK(rdx_record_format_discard_short(&other)<0);
        CHECK(lookup(RF_ROOT "abc123.raw") && lookup(RF_ROOT "abc123.mta"));
        CHECK(!lookup(RF_DISCARD) && handles==0);
    }
    return 0;
}
int test_short_discard_replay(void) {
    for(int stage=0;stage<4;++stage) {
        reset(); CHECK(seed_short(3600)==0);
        if(stage==0) fail_delete_path=RF_ROOT "abc123.raw";
        if(stage==1) fail_delete_path=RF_ROOT "abc123.mta";
        if(stage==2) fail_delete_path=RF_DISCARD;
        if(stage==3) fail_flush_call=flush_calls+2;
        CHECK(rdx_record_format_discard_short(&current)<0 && handles==0);
        CHECK(lookup(RF_DISCARD));
        u32 sn=99; CHECK(rdx_record_format_reserve_sn(&sn)<0 && !sn);
        fail_delete_path=NULL; fail_flush_call=0;
        CHECK(recover()==0 && handles==0);
        CHECK(!lookup(RF_ROOT "abc123.raw") && !lookup(RF_ROOT "abc123.mta"));
        CHECK(!lookup(RF_DISCARD));
        cJSON *root=read_dat(); CHECK(root && !cJSON_GetArraySize(root)); cJSON_Delete(root);
        CHECK(rdx_record_format_reserve_sn(&sn)==0 && sn==8);
    }
    /* An uncommitted/torn intent must never authorize data deletion. */
    reset(); CHECK(seed_short(3600)==0);
    fail_write=1; fail_path=RF_DISCARD;
    CHECK(rdx_record_format_discard_short(&current)<0 && handles==0);
    CHECK(lookup(RF_ROOT "abc123.raw") && lookup(RF_ROOT "abc123.mta"));
    fail_write=0; fail_path=NULL;
    CHECK(recover()==0);
    CHECK(lookup(RF_ROOT "abc123.raw") && lookup(RF_ROOT "abc123.mta"));
    cJSON *root=read_dat(); CHECK(root && named_sn(root,"abc123.raw")==7); cJSON_Delete(root);
    return 0;
}
int test_sn_explicit_format_reset(void) {
    reset(); vm_present=1; vm_sn=70; CHECK(recover()==0);
    CHECK(rdx_record_format_erase_enter()==0);
    rdx_record_format_erased(); CHECK(!rf_error && vm_sn==0);
    CHECK(rdx_record_format_boot()==0);
    rdx_record_format_erase_leave(0);
    u32 sn=0; CHECK(rdx_record_format_reserve_sn(&sn)==0 && sn==1);
    for(int failure=1;failure<=4;++failure) {
        reset(); vm_present=1; vm_sn=70; CHECK(recover()==0);
        CHECK(rdx_record_format_erase_enter()==0);
        if(failure<=2) vm_write_fail=failure;
        else vm_readback_fail=failure-2;
        rdx_record_format_erased();
        CHECK(rf_error<0 && rf_boot_started && rdx_record_format_boot()<0);
        sn=99; CHECK(rdx_record_format_reserve_sn(&sn)<0 && !sn);
        rdx_record_format_erase_leave(-1);
        CHECK(rdx_record_format_service_status()<0 && !record_recovered);
        vm_write_fail=vm_readback_fail=0;
        CHECK(rdx_record_format_erase_enter()==0);
        rdx_record_format_erased(); CHECK(vm_sn==0 && !rf_error);
        CHECK(rdx_record_format_boot()==0);
        rdx_record_format_erase_leave(0);
        CHECK(rdx_record_format_service_status()==0);
        CHECK(rdx_record_format_reserve_sn(&sn)==0 && sn==1);
    }
    CHECK(handles==0); return 0;
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
char *strcat(char *,const char *);
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
    factory_crc = function((ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_factory_audio.c').read_text(encoding='utf-8'), 'rdx_factory_audio_crc')
    vm_header = (ROOT / 'SDK/interface/utils/device/vm.h').read_text(encoding='utf-8')
    vm_enum = re.search(r'typedef enum _vm_err\s*\{.*?\}\s*VM_ERR;', vm_header, re.S)[0]
    path.write_text(libc + header + json_source + vm_enum + STUBS + crc + factory_crc + source + TESTS, encoding='utf-8')
    run_c_checks(path, ['test_space_reserve', 'test_explicit_format_recovery',
                        'test_download_admission', 'test_stream_only_storage_query', 'test_power_admission', 'test_factory_file_identity', 'test_verified_raw_reader', 'test_indexed_raw_reader',
                        'test_large_download_open',
                        'test_format_persistence', 'test_format_recovery',
                        'test_format_fail_closed', 'test_format_replay',
                        'test_allocation_failure', 'test_delete_durable_identity', 'test_delete_interrupted_index',
                        'test_more_than_300_and_duplicate_sn', 'test_dat_mta_collision',
                        'test_sn_reservation_migration', 'test_sn_reservation_full_index_and_empty',
                        'test_sn_reservation_failures_and_limit', 'test_empty_cancel', 'test_short_discard_admission',
                        'test_short_discard_replay', 'test_sn_explicit_format_reset'], native=True)
    print('Recording format persistence/recovery/replay checks passed (mock filesystem).')


if __name__ == '__main__':
    main()
    from file_delete_checks import run_delete_checks
    run_delete_checks()
    from recovery_regression_checks import run_recovery_checks
    run_recovery_checks()
