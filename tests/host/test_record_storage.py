"""Execute production recording aggregation with a mocked RDX storage boundary.

Uses JL clang IR on the host, like the factory USB harness. This does not
validate the binary RDX writer, physical storage, or Opus framing.
"""
import tempfile
from pathlib import Path

from test_factory_usb import ROOT, run_c_checks


def main():
    source = (ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_record.c').read_text(encoding='utf-8')
    begin = source.index('static int rdx_record_local_flush(')
    end = source.index('#if (RDX_AI_SEL_APP', begin)
    assert source.count('rdx_record_local_append(d, len, rp->scene);') == 2
    assert source.count('rdx_record_local_flush(rp->scene);') == 2
    stubs = r'''
typedef unsigned char u8;
typedef unsigned int u32;
#define AUDIO_SEND_BUF_SIZE 8000
#define r_printf(...) ((void)0)
void *memcpy(void *, const void *, unsigned int);
void *memset(void *, int, unsigned int);
static u8 au_buf[AUDIO_SEND_BUF_SIZE];
static u32 au_len;
static u8 saved[64000], input[48000];
static u32 saved_len, calls, stops, fail_call, bad, last_scene;
int rdx_uxfile_raw_write(u8 *data, u32 len, u8 scene) {
    ++calls; last_scene = scene;
    if (!len || len > 8000 || saved_len + len > sizeof(saved)) bad = 1;
    if (calls == fail_call) return -1;
    memcpy(saved + saved_len, data, len); saved_len += len;
    return 0;
}
void rdx_record_stop(void) {
    ++stops;
    if (au_len) bad = 1; /* cleanup must not see the failed block */
}
'''
    tests = r'''
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
void reset(void) {
    au_len = saved_len = calls = stops = fail_call = bad = last_scene = 0;
    for (u32 i = 0; i < sizeof(input); ++i) input[i] = (i * 37 + i / 251) & 255;
}
int test_storage(void) {
    const u32 sizes[] = {40, 80, 79, 81, 319, 8000, 8001, 24017};
    for (u32 t = 0; t < sizeof(sizes)/sizeof(sizes[0]); ++t) {
        reset();
        for (u32 pos = 0; pos < sizeof(input);) {
            u32 n = sizes[t];
            if (n > sizeof(input) - pos) n = sizeof(input) - pos;
            rdx_record_local_append(input + pos, n, 7); pos += n;
        }
        CHECK(rdx_record_local_flush(7) == 0);
        CHECK(!bad && !stops && !au_len && saved_len == sizeof(input));
        CHECK(last_scene == 7);
        for (u32 i = 0; i < saved_len; ++i) CHECK(saved[i] == input[i]);
    }
    reset();
    rdx_record_local_append(input, 7999, 2);
    rdx_record_local_append(input + 7999, 83, 2);
    CHECK(calls == 1 && saved_len == 8000 && au_len == 82);
    CHECK(rdx_record_local_flush(2) == 0 && saved_len == 8082);
    for (u32 i = 0; i < saved_len; ++i) CHECK(saved[i] == input[i]);
    CHECK(rdx_record_local_flush(2) == 0 && calls == 2);
    rdx_record_local_append(input, 0, 2); CHECK(calls == 2);
    reset(); fail_call = 2;
    rdx_record_local_append(input, 24017, 3);
    CHECK(calls == 2 && stops == 1 && !au_len && saved_len == 8000 && !bad);
    CHECK(rdx_record_local_flush(3) == 0 && calls == 2);
    reset(); fail_call = 1;
    rdx_record_local_append(input, 17, 4);
    CHECK(rdx_record_local_flush(4) == -1);
    CHECK(stops == 1 && !au_len && !saved_len && !bad);
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix='t2620-storage-') as work:
        path = Path(work) / 'record_storage.c'
        path.write_text(stubs + source[begin:end] + tests, encoding='utf-8')
        run_c_checks(path, ['test_storage'])
    print('Recording storage aggregation behavioral checks passed.')


if __name__ == '__main__':
    main()
