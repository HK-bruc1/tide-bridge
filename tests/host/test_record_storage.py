"""Run production async recorder with deterministic worker/I/O scheduling.
Hardware latency and library behavior still require on-device acceptance.
"""
import re
import tempfile
from pathlib import Path
from host_c_test_lib import ROOT, run_c_checks


def main():
    base = ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol'
    source = (base / 'rdx_record_storage.c').read_text(encoding='utf-8')
    source = re.sub(r'^#include .*\n', '', source, flags=re.M)
    record = (base / 'rdx_record.c').read_text(encoding='utf-8')
    assert record.count('rdx_record_storage_push(d, len)') == 2
    assert record.count('rdx_record_storage_end();') == 2
    assert 'rdx_uxfile_raw_write(' not in record
    assert 'rdx_record_format_frame(' not in record
    stubs = r'''
typedef unsigned char u8;
typedef unsigned int u32;
typedef int OS_SEM;
#define NULL ((void*)0)
#define THIRD_PARTY_PROTOCOLS_SEL 1
#define RDX_EN 1
#define RECORD_FORMATE_OPUS_16K_MONO 1
#define RECORD_FORMATE_OPUS_16K_STERO 2
#define printf(...) ((void)0)
#define OS_ENTER_CRITICAL() (++locked)
#define OS_EXIT_CRITICAL() (--locked)
static int locked, bad, faults, status, meta_fail, fail_write, writes;
static u32 meta_calls, saved_len, ticks, stall_frames, seq, expected, frame_len;
static int inside_push, inject, meta_inject;
static int push(void);
static u8 input[80];
void *memcpy(void*, const void*, unsigned int);
void *memset(void*, int, unsigned int);
int os_sem_create(OS_SEM *s, int v) {*s=v; return 0;}
int os_sem_del(OS_SEM *s, int v) {return 0;}
int os_sem_set(OS_SEM *s, unsigned short v) {*s=v; return 0;}
int os_sem_post(OS_SEM *s) {++*s; return 0;}
int os_sem_pend(OS_SEM *, int);
int os_task_create(void (*f)(void*), void *p, int a, int b, int c, const char *n) {return 0;}
void os_task_del(const char *n) {}
u32 sys_timer_get_ms(void) {return ticks;}
void translation_ear_recoder_storage_fault(void) {++faults;}
int rdx_record_format_status(void) {return status;}
void rdx_record_format_storage_fault(void) {status=-1;}
void *rdx_uxfile_get_operateFile_info(void) {return (void*)1;}
int rdx_record_format_frame(void *file, u8 format, const u8 *data, u32 len) {
    if (locked || inside_push || len != frame_len) bad=1;
    ++meta_calls;
    if (meta_inject) {
        meta_inject=0;
        for (u32 i=0;i<stall_frames;++i) {ticks+=20; push();}
        /* The first frame must remain owned by the worker during MTA I/O. */
        for (u32 i=0;i<len;++i) if (data[i]!=(i&255)) bad=1;
    }
    if (meta_fail) return -1;
    return 0;
}
int rdx_uxfile_raw_write(u8*, u32, u8);
'''
    tests = r'''
#define CHECK(x) do {if (!(x)) return __LINE__;} while(0)
static void step(void) {
    if (rs.wake) {--rs.wake; rs_service();}
}
int os_sem_pend(OS_SEM *s, int timeout) {
    if (locked || inside_push) bad=1;
    for (int i=0; !*s && i<1000; ++i) step();
    if (!*s) {bad=1; return -1;}
    --*s; return 0;
}
static int push(void) {
    for (u32 i=0;i<frame_len;++i) input[i]=(seq*37+i)&255;
    inside_push=1;
    int ret=rdx_record_storage_push(input,frame_len);
    inside_push=0;
    if (!ret) ++seq;
    /* Producer owns the original frame immediately after return. */
    memset(input,0xff,sizeof(input));
    return ret;
}
int rdx_uxfile_raw_write(u8 *data,u32 len,u8 scene) {
    if (locked || inside_push || !meta_calls || scene!=7 || !len || len>8000) bad=1;
    ++writes;
    if (inject) {
        inject=0;
        for (u32 i=0;i<stall_frames;++i) {ticks+=20; push();}
    }
    if (fail_write==writes) return -1;
    for (u32 i=0;i<len;++i) {
        u32 frame=expected/frame_len, pos=expected%frame_len;
        if (data[i]!=((frame*37+pos)&255)) bad=1;
        ++expected;
    }
    saved_len+=len;
    return writes%2 ? 0 : 7; /* nonnegative library status, not byte count */
}
static int reset(u8 format) {
    memset(&rs,0,sizeof(rs));
    locked=bad=faults=status=meta_fail=fail_write=writes=0;
    meta_calls=saved_len=ticks=stall_frames=seq=expected=inside_push=inject=meta_inject=0;
    frame_len=format==1 ? 40 : 80;
    if (rdx_record_storage_create()) return -1;
    return rdx_record_storage_begin(42,7,format);
}
int test_storage(void) {
    for (u8 format=1;format<=2;++format) {
        CHECK(!reset(format));
        CHECK(rdx_record_storage_begin(43,7,format)<0);
        /* Two hours plus a non-block-aligned tail, and many ring wraps. */
        for (u32 i=0;i<360013;++i) {CHECK(!push()); step();}
        CHECK(!rdx_record_storage_end());
        CHECK(saved_len==seq*frame_len && rs.accepted==rs.persisted);
        CHECK(!bad && !faults && !rs.active && !rs.count);
        u32 before=saved_len;
        CHECK(!rdx_record_storage_end() && saved_len==before);
        /* Resume same archive with a fresh sink generation. */
        CHECK(!rdx_record_storage_begin(43,7,format));
        CHECK(!push()); CHECK(!rdx_record_storage_end());
        CHECK(saved_len==before+frame_len && !bad);
        rdx_record_storage_destroy(); CHECK(!rs.created);
    }
    /* Block the writer for 220ms and 2s while audio keeps enqueueing. */
    const u32 stalls[]={11,100};
    for (u32 t=0;t<2;++t) {
        CHECK(!reset(1));
        stall_frames=stalls[t]; inject=1;
        for (u32 i=0;i<200;++i) {CHECK(!push()); step();}
        CHECK(!rdx_record_storage_end());
        CHECK(saved_len==seq*frame_len && !bad && !faults);
        CHECK(rs.max_write_ms==stall_frames*20);
    }
    CHECK(!reset(1));
    stall_frames=100; meta_inject=1;
    CHECK(!push()); step();
    CHECK(!rdx_record_storage_end() && !bad && !faults);
    CHECK(saved_len==seq*frame_len && rs.max_meta_ms==2000);
    /* Saturation cannot overwrite the in-flight slot; drain accepted prefix. */
    CHECK(!reset(2));
    for (u32 i=0;i<RS_FRAMES;++i) CHECK(!push());
    CHECK(push()<0 && faults && !writes);
    CHECK(rdx_record_storage_end()<0 && status<0);
    CHECK(saved_len==RS_FRAMES*80 && !bad);
    CHECK(rdx_record_storage_begin(43,7,2)<0);
    /* Overflow while raw_write owns the aggregation block. */
    CHECK(!reset(1)); stall_frames=RS_FRAMES+4; inject=1;
    for (u32 i=0;i<200;++i) {CHECK(!push()); step();}
    CHECK(rdx_record_storage_end()<0 && status<0);
    CHECK(saved_len==seq*frame_len && !bad);
    /* RAW failure must never retry or append later bytes after the gap. */
    CHECK(!reset(1)); fail_write=1;
    for (u32 i=0;i<200;++i) {CHECK(!push()); step();}
    CHECK(rdx_record_storage_end()<0 && status<0 && faults);
    CHECK(writes==1 && !saved_len && !bad);
    CHECK(push()<0 && rdx_record_storage_begin(44,7,1)<0);
    CHECK(!reset(2)); fail_write=1;
    CHECK(!push()); CHECK(rdx_record_storage_end()<0);
    CHECK(writes==1 && !saved_len && status<0 && !bad);
    /* Metadata failure prevents any RAW output, including tail. */
    CHECK(!reset(1)); meta_fail=1;
    CHECK(!push()); CHECK(rdx_record_storage_end()<0);
    CHECK(!writes && !saved_len && status<0 && !bad);
    CHECK(!reset(1));
    CHECK(rdx_record_storage_push(input,79)<0);
    CHECK(rdx_record_storage_end()<0 && !writes && status<0);
    CHECK(!reset(1));
    CHECK(!rdx_record_storage_end() && !writes && !status && !bad);
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix='t2620-storage-') as work:
        path = Path(work) / 'record_storage.c'
        path.write_text(stubs + source + tests, encoding='utf-8')
        run_c_checks(path, ['test_storage'])
    print('Recording async storage/lifecycle behavioral checks passed.')


if __name__ == '__main__':
    main()
