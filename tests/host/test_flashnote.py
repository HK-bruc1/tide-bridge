"""APP flashnote parser, owner isolation, status ordering and release barriers."""
import ctypes
import re
import tempfile
from pathlib import Path

from llvmlite import binding as llvm
from host_c_test_lib import ROOT, run_c_checks


BASE = ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol'


STUBS = r'''
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef long time_t;
typedef unsigned long long size_t;
typedef int bool;
#define true 1
#define false 0
#define NULL ((void *)0)
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
#define THIRD_PARTY_PROTOCOLS_SEL 1
#define RDX_EN 1
#define Q_CALLBACK 1
#define RDX_BLE_LINK_INVALID_INDEX 255
#define RDX_FLASHNOTE_CMD_STOP 0u
#define RDX_FLASHNOTE_CMD_START 1u
#define RDX_FLASHNOTE_STATE_END 0u
#define RDX_FLASHNOTE_STATE_RUNNING 1u
#define RDX_FLASHNOTE_STATE_FAIL_BUSY 2u
#define RDX_FLASHNOTE_STATE_FAIL_NOSPACE 3u
#define RECORD_STATE_START 0
#define RECORD_STATE_RESUME 2
#define RECORD_STATE_STOP 3
#define r_printf(...) ((void)0)

void *malloc(size_t);
void free(void *);
int memcmp(const void *, const void *, size_t);
int strcmp(const char *, const char *);
int snprintf(char *, size_t, const char *, ...);

typedef struct {
    u8 slot_index;
    u32 slot_generation;
    u32 transport_epoch;
} rdx_ble_async_token_t;

typedef struct {
    u8 run;
    u8 formate;
} RecordStatus;

static u32 slot_generation[2] = {1, 1};
static u32 slot_epoch[2] = {1, 1};
static u8 slot_live[2] = {1, 1};
static rdx_ble_async_token_t captured_token;
static u8 capture_enabled = 1;
static u8 binding_allowed = 1;
static u8 stream_ready = 1;
static u8 key_session_open;
static int record_start_ret;
static int record_start_calls;
static u32 record_start_generation = 100;
static u8 record_start_format = 1;
static u32 record_start_seen_generation;
static u8 record_start_seen_format;
static int release_wait;
static int release_calls;
static int release_complete_calls;
static RecordStatus record_status;
static u32 calculated_format_arg;
static u32 test_rtc_timestamp = 1793678305u;
static u8 fail_next_send;
static int fail_send_number;
static int send_attempts;
static int send_resolve_failures;
static int allocation_count;
static int free_count;
static void *last_request;
static char packets[12][96];
static u16 packet_lengths[12];
static int packet_count;

static int token_resolve(const rdx_ble_async_token_t *token, int ready)
{
    return token && token->slot_index < 2 && slot_live[token->slot_index] &&
           slot_generation[token->slot_index] == token->slot_generation &&
           slot_epoch[token->slot_index] == token->transport_epoch;
}

static int rdx_ble_session_rdx_token_resolve(
    const rdx_ble_async_token_t *token, int ready)
{
    (void)ready;
    return token_resolve(token, 1);
}

static int rdx_ble_session_rdx_token_capture(
    rdx_ble_async_token_t *token, int ready)
{
    (void)ready;
    if (!token || !capture_enabled || !token_resolve(&captured_token, 1)) {
        return 0;
    }
    *token = captured_token;
    return 1;
}

static u8 rdx_record_binding_allowed(void)
{
    return binding_allowed;
}

static int rdx_ble_server_is_stream_tx_ready(void)
{
    return stream_ready;
}

static u8 rdx_record_key_flashnote_session_open(void)
{
    return key_session_open;
}

static int rdx_record_flashnote_start(const rdx_ble_async_token_t *token,
                                      u32 *session_generation, u8 *format)
{
    if (!token_resolve(token, 1)) return -1;
    ++record_start_calls;
    if (record_start_ret) return record_start_ret;
    *session_generation = record_start_generation;
    *format = record_start_format;
    record_start_seen_generation = *session_generation;
    record_start_seen_format = *format;
    record_status.run = RECORD_STATE_START;
    record_status.formate = *format;
    return 0;
}

static u8 rdx_record_stream_only_release(void)
{
    ++release_calls;
    if (release_wait > 0) {
        --release_wait;
        return 0;
    }
    record_status.run = RECORD_STATE_STOP;
    return 1;
}

static void rdx_record_stream_only_release_complete(void)
{
    ++release_complete_calls;
}

static RecordStatus *rdx_record_get_status(void)
{
    return &record_status;
}

static u32 rdx_protocol_calc_opus_format(u8 format)
{
    calculated_format_arg = format;
    return format == 1 ? 11u : 12u;
}

static int rdx_ble_server_send_text_for_token(
    const char *packet, u32 length, const rdx_ble_async_token_t *token)
{
    ++send_attempts;
    if (!token_resolve(token, 1)) ++send_resolve_failures;
    if (fail_next_send || fail_send_number == send_attempts ||
        !token_resolve(token, 1) ||
        packet_count >= (int)(sizeof(packets) / sizeof(packets[0])) ||
        length >= sizeof(packets[0])) {
        if (fail_next_send) --fail_next_send;
        return -1;
    }
    memcpy(packets[packet_count], packet, length);
    packet_lengths[packet_count] = (u16)length;
    packets[packet_count][length] = 0;
    ++packet_count;
    return 0;
}

enum queued_kind {
    QUEUE_NONE,
    QUEUE_REQUEST,
    QUEUE_RELEASE,
    QUEUE_STOP,
    QUEUE_START_FAILED,
};

static struct {
    int kind;
    u32 argument;
} queued;
static int queue_fail;
static int queued_request_valid;
static int timer_live;
static int timer_id;
static int timer_fail;
static int timer_delay;
static void (*timer_callback)(void *);
static void *timer_argument;

static int ptr_low32(const void *pointer)
{
    return (int)(u32)(size_t)pointer;
}

static void flashnote_request_on_app_core(void *priv);
static void flashnote_release_on_app_core(void *priv);
static void flashnote_stop_on_app_core(void *priv);
static void flashnote_start_failed_on_app_core(void *priv);

static int os_taskq_post_type(const char *task, int type, int count, int *message)
{
    (void)task;
    if (queue_fail || type != Q_CALLBACK || count < 2 || count > 3 || queued.kind) {
        return -1;
    }
    if (message[0] == ptr_low32((const void *)flashnote_request_on_app_core)) {
        queued.kind = QUEUE_REQUEST;
        queued_request_valid = 1;
    } else if (message[0] == ptr_low32((const void *)flashnote_release_on_app_core)) {
        queued.kind = QUEUE_RELEASE;
    } else if (message[0] == ptr_low32((const void *)flashnote_stop_on_app_core)) {
        queued.kind = QUEUE_STOP;
        queued.argument = (u32)message[2];
    } else if (message[0] == ptr_low32((const void *)flashnote_start_failed_on_app_core)) {
        queued.kind = QUEUE_START_FAILED;
        queued.argument = (u32)message[2];
    } else {
        return -1;
    }
    return 0;
}

static int sys_timeout_add(void *priv, void (*callback)(void *), int delay)
{
    if (timer_fail || timer_live) return 0;
    timer_live = 1;
    timer_callback = callback;
    timer_argument = priv;
    timer_delay = delay;
    return ++timer_id;
}

static void sys_timeout_del(int timer)
{
    if (timer_live && timer == timer_id) {
        timer_live = 0;
        timer_callback = NULL;
        timer_argument = NULL;
    }
}

static void *test_malloc(size_t size)
{
    void *pointer = malloc(size);
    if (pointer) {
        ++allocation_count;
        last_request = pointer;
    }
    return pointer;
}

static void test_free(void *pointer)
{
    if (pointer) ++free_count;
    if (pointer == last_request) last_request = NULL;
    free(pointer);
}

#define malloc test_malloc
#define free test_free
'''


TESTS = r'''
static rdx_ble_async_token_t token(u8 slot, u32 generation, u32 epoch)
{
    rdx_ble_async_token_t value = {slot, generation, epoch};
    return value;
}

static void reset(void)
{
    memset(&flashnote, 0, sizeof(flashnote));
    memset(&key_flashnote, 0, sizeof(key_flashnote));
    flashnote.owner.slot_index = RDX_BLE_LINK_INVALID_INDEX;
    memset(&queued, 0, sizeof(queued));
    memset(packet_lengths, 0, sizeof(packet_lengths));
    packet_count = 0;
    queued_request_valid = 0;
    queue_fail = 0;
    release_wait = 0;
    release_calls = 0;
    release_complete_calls = 0;
    record_start_ret = 0;
    record_start_calls = 0;
    record_start_seen_generation = 0;
    record_start_seen_format = 0;
    record_start_generation = 100;
    record_start_format = 1;
    record_status.run = RECORD_STATE_STOP;
    record_status.formate = 2;
    calculated_format_arg = 0xff;
    test_rtc_timestamp = 1793678305u;
    fail_next_send = 0;
    fail_send_number = 0;
    send_attempts = 0;
    send_resolve_failures = 0;
    allocation_count = 0;
    free_count = 0;
    last_request = NULL;
    binding_allowed = 1;
    stream_ready = 1;
    key_session_open = 0;
    capture_enabled = 1;
    timer_live = 0;
    timer_fail = 0;
    timer_callback = NULL;
    timer_argument = NULL;
    for (int i = 0; i < 2; ++i) {
        slot_generation[i] = 1;
        slot_epoch[i] = 1;
        slot_live[i] = 1;
    }
    captured_token = token(0, 1, 1);
}

static void pump(void)
{
    int kind = queued.kind;
    u32 argument = queued.argument;
    void *request = last_request;
    int request_valid = queued_request_valid;
    memset(&queued, 0, sizeof(queued));
    queued_request_valid = 0;
    if (kind == QUEUE_REQUEST) {
        if (!request_valid) return;
        flashnote_request_on_app_core(request);
    } else if (kind == QUEUE_RELEASE) {
        flashnote_release_on_app_core(NULL);
    } else if (kind == QUEUE_STOP) {
        flashnote_stop_on_app_core((void *)(size_t)argument);
    } else if (kind == QUEUE_START_FAILED) {
        flashnote_start_failed_on_app_core((void *)(size_t)argument);
    }
}

static void fire_timer(void)
{
    void (*callback)(void *) = timer_callback;
    void *argument = timer_argument;
    if (!callback) return;
    timer_live = 0;
    timer_callback = NULL;
    timer_argument = NULL;
    callback(argument);
}

static int packet_is(int index, const char *expected)
{
    return index < packet_count &&
           packet_lengths[index] == strlen(expected) &&
           !memcmp(packets[index], expected, packet_lengths[index]);
}

static int begin_running(const rdx_ble_async_token_t *owner)
{
    if (rdx_flashnote_request(RDX_FLASHNOTE_CMD_START, owner)) return 0;
    pump();
    if (flashnote.state != 1 || record_start_calls != 1) return 0;
    if (rdx_flashnote_on_stream_started(flashnote.session_generation,
                                        record_start_seen_format, owner)) return 0;
    return flashnote.state == 2 &&
           packet_count == 1 &&
           packet_is(0, "*DEV#flashnote#1#0##11#");
}

int test_parser_exactness(void)
{
    u8 command = 0xee;
    char start[] = "*APP#flashnote#1#";
    char stop[] = "*APP#flashnote#0#";
    char terminated[] = "*APP#flashnote#1#\0";
    char bad_terminated[] = "*APP#flashnote#1#x";
    char bad_value[] = "*APP#flashnote#2#";
    char bad_prefix[] = "*APP#flashnote?1#";
    char extra[] = "*APP#flashnote#1#x";

    reset();
    CHECK(rdx_flashnote_protocol_parse((u8 *)start, 17, &command) == 1);
    CHECK(command == '1');
    CHECK(rdx_flashnote_protocol_parse((u8 *)stop, 17, &command) == 1);
    CHECK(command == '0');
    CHECK(rdx_flashnote_protocol_parse((u8 *)terminated, 18, &command) == 1);
    CHECK(command == '1');

    const char *bad[] = {bad_terminated, bad_value, bad_prefix, extra};
    const u16 lengths[] = {18, 17, 17, 18};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        command = 0xee;
        CHECK(!rdx_flashnote_protocol_parse((u8 *)bad[i], lengths[i], &command));
        CHECK(command == 0xee);
    }
    command = 0xee;
    CHECK(!rdx_flashnote_protocol_parse((u8 *)start, 16, &command));
    CHECK(!rdx_flashnote_protocol_parse((u8 *)start, 19, &command));
    CHECK(!rdx_flashnote_protocol_parse(NULL, 17, &command));
    CHECK(!rdx_flashnote_protocol_parse((u8 *)start, 17, NULL));
    CHECK(command == 0xee);
    return 0;
}

int test_request_running_and_duplicate_owner(void)
{
    rdx_ble_async_token_t first = token(0, 1, 1);
    rdx_ble_async_token_t second = token(1, 1, 1);
    reset();

    CHECK(begin_running(&first));
    CHECK(calculated_format_arg == record_start_seen_format);
    CHECK(record_status.run == RECORD_STATE_START);

    CHECK(!rdx_flashnote_request(RDX_FLASHNOTE_CMD_START, &first));
    pump();
    CHECK(packet_count == 2);
    CHECK(packet_is(1, "*DEV#flashnote#1#0##11#"));
    CHECK(record_start_calls == 1);

    CHECK(!rdx_flashnote_request(RDX_FLASHNOTE_CMD_START, &second));
    pump();
    CHECK(packet_count == 3 && packet_is(2, "*DEV#flashnote#2#"));
    CHECK(record_start_calls == 1);
    CHECK(allocation_count == free_count);
    return 0;
}

int test_busy_idle_end_and_stale_request(void)
{
    rdx_ble_async_token_t owner = token(0, 1, 1);
    reset();
    record_start_ret = -1;
    CHECK(!rdx_flashnote_request(RDX_FLASHNOTE_CMD_START, &owner));
    pump();
    CHECK(packet_count == 1 && packet_is(0, "*DEV#flashnote#2#"));
    CHECK(record_start_calls == 1);

    reset();
    CHECK(!rdx_flashnote_request(RDX_FLASHNOTE_CMD_STOP, &owner));
    pump();
    CHECK(packet_count == 1 && packet_is(0, "*DEV#flashnote#0#0##12#"));
    CHECK(calculated_format_arg == 2);

    reset();
    CHECK(!rdx_flashnote_request(RDX_FLASHNOTE_CMD_START, &owner));
    ++slot_generation[0];
    pump();
    CHECK(!packet_count && !record_start_calls);
    CHECK(allocation_count == free_count);

    reset();
    queue_fail = 1;
    CHECK(!rdx_flashnote_request(RDX_FLASHNOTE_CMD_START, &owner));
    CHECK(packet_count == 1 && packet_is(0, "*DEV#flashnote#2#"));
    CHECK(allocation_count == free_count);
    CHECK(rdx_flashnote_request(7, &owner) == -1);
    CHECK(rdx_flashnote_request(RDX_FLASHNOTE_CMD_START, NULL) == -1);
    return 0;
}

int test_stop_barrier_and_duplicate_stop(void)
{
    rdx_ble_async_token_t owner = token(0, 1, 1);
    reset();
    CHECK(begin_running(&owner));
    release_wait = 2;

    CHECK(!rdx_flashnote_request(RDX_FLASHNOTE_CMD_STOP, &owner));
    pump();
    CHECK(release_calls == 1 && packet_count == 1 && timer_live);
    CHECK(!rdx_flashnote_request(RDX_FLASHNOTE_CMD_STOP, &owner));
    pump();
    CHECK(release_calls == 2);
    CHECK(packet_count == 1);
    CHECK(timer_live);

    fire_timer();
    CHECK(queued.kind == QUEUE_STOP);
    pump();
    CHECK(release_calls == 3);
    CHECK(packet_count == 2);
    CHECK(release_complete_calls == 1);
    CHECK(packet_count == 2 &&
          packet_is(1, "*DEV#flashnote#0#0##11#"));
    CHECK(flashnote.state == 0 && !flashnote.owner_valid);
    CHECK(allocation_count == free_count);
    return 0;
}

int test_start_failure_paths(void)
{
    rdx_ble_async_token_t owner = token(0, 1, 1);
    reset();
    CHECK(!rdx_flashnote_request(RDX_FLASHNOTE_CMD_START, &owner));
    pump();
    rdx_flashnote_on_record_init_failed(-2);
    CHECK(queued.kind == QUEUE_START_FAILED);
    pump();
    CHECK(packet_count == 1);
    CHECK(packet_is(0, "*DEV#flashnote#2#"));
    CHECK(release_calls == 1 && release_complete_calls == 1);
    CHECK(flashnote.state == 0 && !flashnote.owner_valid);

    reset();
    CHECK(!rdx_flashnote_request(RDX_FLASHNOTE_CMD_START, &owner));
    pump();
    fail_next_send = 1;
    CHECK(rdx_flashnote_on_stream_started(flashnote.session_generation,
                                          record_start_seen_format,
                                          &owner) != 0);
    CHECK(queued.kind == QUEUE_START_FAILED);
    pump();
    CHECK(packet_count == 1 && packet_is(0, "*DEV#flashnote#2#"));
    CHECK(release_calls == 1 && release_complete_calls == 1);

    reset();
    CHECK(!rdx_flashnote_request(RDX_FLASHNOTE_CMD_START, &owner));
    pump();
    fail_next_send = 1;
    queue_fail = 1;
    CHECK(rdx_flashnote_on_stream_started(flashnote.session_generation,
                                          record_start_seen_format,
                                          &owner) != 0);
    CHECK(release_calls == 1 && release_complete_calls == 1);
    CHECK(flashnote.state == 0 && !flashnote.owner_valid);
    queue_fail = 0;

    reset();
    CHECK(!rdx_flashnote_request(RDX_FLASHNOTE_CMD_START, &owner));
    pump();
    fail_send_number = 1;
    CHECK(rdx_flashnote_on_stream_started(flashnote.session_generation,
                                          record_start_seen_format,
                                          &owner) != 0);
    CHECK(packet_count == 0);
    CHECK(queued.kind == QUEUE_START_FAILED);
    pump();
    CHECK(packet_is(0, "*DEV#flashnote#2#"));
    CHECK(release_calls == 1 && release_complete_calls == 1);

    CHECK(!rdx_flashnote_request(RDX_FLASHNOTE_CMD_START, &owner));
    pump();
    CHECK(record_start_calls == 2);
    return 0;
}

int test_cancellation_and_fast_reconnect(void)
{
    rdx_ble_async_token_t first = token(0, 1, 1);
    rdx_ble_async_token_t second = token(1, 1, 1);
    reset();
    CHECK(begin_running(&first));
    release_wait = 1;

    rdx_flashnote_cancel_for_token(&second);
    CHECK(flashnote.state == 2 && flashnote.owner_valid);
    rdx_flashnote_cancel_for_token(&first);
    CHECK(flashnote.state == 0 && !flashnote.owner_valid && flashnote.release_pending);
    CHECK(queued.kind == QUEUE_RELEASE);
    pump();
    CHECK(release_calls == 1 && timer_live && packet_count == 1);

    ++slot_generation[0];
    fire_timer();
    CHECK(queued.kind == QUEUE_RELEASE);
    pump();
    CHECK(packet_count == 1 &&
          packet_is(0, "*DEV#flashnote#1#0##11#"));
    CHECK(release_complete_calls == 1 && !flashnote.release_pending);

    CHECK(!rdx_flashnote_request(RDX_FLASHNOTE_CMD_START, &second));
    pump();
    CHECK(record_start_calls == 2 && flashnote.owner_valid);
    CHECK(flashnote.owner.slot_index == second.slot_index);

    rdx_flashnote_cancel_all();
    CHECK(flashnote.state == 0 && !flashnote.owner_valid);
    CHECK(allocation_count == free_count);
    return 0;
}

int test_key_flashnote_stream_state(void)
{
    rdx_ble_async_token_t owner = token(0, 1, 1);
    reset();

    CHECK(!rdx_flashnote_on_key_record_started(55, 1, &owner));
    CHECK(packet_count == 1 &&
          packet_is(0, "*DEV#flashnote#1#0##11#"));

    /* RESUME uses the same business generation and must not duplicate START. */
    CHECK(!rdx_flashnote_on_key_record_started(55, 1, &owner));
    CHECK(packet_count == 1);

    rdx_flashnote_on_key_record_stopped(54, 1, &owner);
    CHECK(packet_count == 1);
    rdx_flashnote_on_key_record_stopped(55, 1, &owner);
    CHECK(packet_count == 2 &&
          packet_is(1, "*DEV#flashnote#0#0##11#"));

    rdx_flashnote_on_key_record_stopped(55, 1, &owner);
    CHECK(packet_count == 2);

    /* A lost receiver clears the local key state without emitting END. */
    CHECK(!rdx_flashnote_on_key_record_started(56, 2, &owner));
    CHECK(packet_count == 3 &&
          packet_is(2, "*DEV#flashnote#1#0##12#"));
    rdx_flashnote_cancel_for_token(&owner);
    rdx_flashnote_on_key_record_stopped(56, 2, &owner);
    CHECK(packet_count == 3);
    return 0;
}

int test_key_local_identity_reconnect_and_end(void)
{
    rdx_ble_async_token_t connected = token(1, 1, 1);
    reset();
    captured_token = connected;
    record_status.run = RECORD_STATE_START;

    CHECK(!rdx_flashnote_on_key_local_record_started(
        77, 1, 31, "storage/sd0/C/1f4010.raw"));
    CHECK(key_flashnote.active && key_flashnote.local);
    CHECK(!key_flashnote.owner_valid && packet_count == 0);

    CHECK(!rdx_flashnote_on_key_local_record_started(
        77, 1, 31, "storage/sd0/C/1f4010.raw"));
    CHECK(rdx_flashnote_on_key_local_record_started(
        77, 1, 32, "storage/sd0/C/1f4011.raw"));

    CHECK(!rdx_flashnote_sync_active(&connected));
    CHECK(packet_count == 1);
    CHECK(packet_is(
        0,
        "*DEV#flashnote#1#31#storage/sd0/C/1f4010.raw#11#"));

    /* A transport loss only retires the APP presentation owner. The offline
     * file session remains active and is synchronized again on reconnect. */
    rdx_flashnote_cancel_all();
    CHECK(key_flashnote.active && key_flashnote.local);
    CHECK(!rdx_flashnote_sync_active(&connected));
    CHECK(packet_count == 2);
    CHECK(packet_is(
        1,
        "*DEV#flashnote#1#31#storage/sd0/C/1f4010.raw#11#"));

    rdx_flashnote_on_key_local_record_stopped(77, 1);
    CHECK(packet_count == 3);
    CHECK(packet_is(
        2,
        "*DEV#flashnote#0#31#storage/sd0/C/1f4010.raw#11#"));
    CHECK(!key_flashnote.active && !key_flashnote.local &&
          key_flashnote.sn == 0 && key_flashnote.filename[0] == '\0');

    rdx_flashnote_on_key_local_record_stopped(77, 1);
    CHECK(packet_count == 3);
    return 0;
}

int test_key_local_session_rejects_app_control(void)
{
    rdx_ble_async_token_t connected = token(1, 1, 1);
    reset();
    captured_token = connected;
    record_status.run = RECORD_STATE_START;
    CHECK(!rdx_flashnote_on_key_local_record_started(
        77, 1, 31, "storage/sd0/C/1f4010.raw"));
    CHECK(!rdx_flashnote_sync_active(&connected));

    CHECK(!rdx_flashnote_request(RDX_FLASHNOTE_CMD_STOP, &connected));
    pump();
    CHECK(packet_count == 2 && packet_is(1, "*DEV#flashnote#2#"));
    CHECK(record_status.run == RECORD_STATE_START && !release_calls);
    CHECK(key_flashnote.active && key_flashnote.local &&
          key_flashnote.session_generation == 77 && key_flashnote.sn == 31);
    CHECK(!strcmp(key_flashnote.filename, "storage/sd0/C/1f4010.raw"));

    CHECK(!rdx_flashnote_request(RDX_FLASHNOTE_CMD_START, &connected));
    pump();
    CHECK(packet_count == 3 && packet_is(2, "*DEV#flashnote#2#"));
    CHECK(!record_start_calls && record_status.run == RECORD_STATE_START);

    rdx_flashnote_cancel_all();
    CHECK(key_flashnote.active && key_flashnote.local);
    CHECK(!rdx_flashnote_sync_active(&connected));
    CHECK(!rdx_flashnote_request(RDX_FLASHNOTE_CMD_STOP, &connected));
    pump();
    CHECK(packet_count == 5 && packet_is(4, "*DEV#flashnote#2#"));
    CHECK(record_status.run == RECORD_STATE_START && !release_calls);

    rdx_flashnote_on_key_local_record_stopped(77, 1);
    CHECK(packet_count == 6 && packet_is(
        5, "*DEV#flashnote#0#31#storage/sd0/C/1f4010.raw#11#"));
    CHECK(!key_flashnote.active);
    record_status.run = RECORD_STATE_STOP;
    CHECK(!rdx_flashnote_request(RDX_FLASHNOTE_CMD_START, &connected));
    pump();
    CHECK(record_start_calls == 1 && flashnote.state == RDX_FLASHNOTE_STARTING);
    CHECK(allocation_count == free_count);
    return 0;
}

int test_key_online_and_pending_reject_app_control(void)
{
    rdx_ble_async_token_t owner = token(0, 1, 1);
    reset();
    record_status.run = RECORD_STATE_START;
    CHECK(!rdx_flashnote_on_key_record_started(55, 1, &owner));
    CHECK(!rdx_flashnote_request(RDX_FLASHNOTE_CMD_STOP, &owner));
    pump();
    CHECK(packet_count == 2 && packet_is(1, "*DEV#flashnote#2#"));
    CHECK(key_flashnote.active && record_status.run == RECORD_STATE_START);
    CHECK(!release_calls);
    rdx_flashnote_on_key_record_stopped(55, 1, &owner);
    CHECK(packet_count == 3 && packet_is(2, "*DEV#flashnote#0#0##11#"));

    /* The recorder owns START_PENDING and STOPPING before/after key state
     * indications, so the APP must not receive a false idle END. */
    for (int stage = 0; stage < 2; ++stage) {
        reset();
        key_session_open = 1;
        CHECK(!rdx_flashnote_request(RDX_FLASHNOTE_CMD_STOP, &owner));
        pump();
        CHECK(packet_count == 1 && packet_is(0, "*DEV#flashnote#2#"));
        CHECK(!release_calls && !record_start_calls);
        CHECK(!rdx_flashnote_request(RDX_FLASHNOTE_CMD_START, &owner));
        pump();
        CHECK(packet_count == 2 && packet_is(1, "*DEV#flashnote#2#"));
        CHECK(!release_calls && !record_start_calls);
        key_session_open = 0;
        CHECK(!rdx_flashnote_request(RDX_FLASHNOTE_CMD_STOP, &owner));
        pump();
        CHECK(packet_count == 3 && packet_is(2, "*DEV#flashnote#0#0##12#"));
    }
    return 0;
}
'''


def main():
    crt = ctypes.CDLL('msvcrt')
    llvm.add_symbol('snprintf', ctypes.cast(crt._snprintf, ctypes.c_void_p).value)
    source = (BASE / 'rdx_flashnote.c').read_text(encoding='utf-8')
    source = re.sub(r'^#include[^\n]*', '', source, flags=re.M)
    harness = STUBS + source + TESTS
    with tempfile.TemporaryDirectory(prefix='flashnote-') as temp:
        path = Path(temp) / 'flashnote.c'
        path.write_text(harness, encoding='utf-8')
        run_c_checks(path, re.findall(r'int (test_\w+)\(void\)', TESTS), native=True)
    print('APP flashnote: exact parser, stream-only RUNNING/END, owner isolation, '
          'BUSY/END, failure cleanup, STOP barrier and reconnect cancellation passed.')


if __name__ == '__main__':
    main()
