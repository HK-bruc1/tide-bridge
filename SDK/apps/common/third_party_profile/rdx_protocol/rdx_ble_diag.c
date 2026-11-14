#include "system/includes.h"
#include "app_config.h"
#include "rdx_ble_diag.h"

/* Referenced by the patched controller before it reads private LL fields. */
const int rdx_ble_diag_enabled = TCFG_T2620_BLE_DISCONNECT_DIAG_ENABLE;

#if TCFG_T2620_BLE_DISCONNECT_DIAG_ENABLE

#define DIAG_DEPTH 32
#define DIAG_LINKS 4
#define SUP_DEPTH 8

/* A validity mask makes t=0 and wrapped clocks distinguishable from missing data.
 * period_ms/due_est are requested timing, NOT a readback of the OS timer wheel. */
#define SUP_REGISTERED  0x001u
#define SUP_CONTEXT     0x002u
#define SUP_PERIOD      0x004u
#define SUP_MODIFY      0x008u
#define SUP_RESET       0x010u
#define SUP_RX          0x020u
#define SUP_ELIGIBLE    0x040u
#define SUP_EVENT       0x080u
#define SUP_REMOVED     0x100u
#define SUP_LIVE        0x200u

struct sup_state {
    u32 valid, timer_id, timer_gen, period_ms, basis_ms;
    u32 register_ms, context_ms, period_call_ms, reset_ms, reset_calls, reset_id;
    u32 modify_ms, modify_period_ms;
    int modify_result;
    u32 remove_ms, removed_id, mismatches;
    u32 rx_ms, eligible_ms, rx_calls, eligible_rx, rx_no_timer, rx_other;
    u32 event_ms, event_calls, rx_event, event_counter;
    u32 origin, origin_ms, origin_period_ms;
    u8 tracked, frozen;
};

struct sup_record {
    struct sup_state timer;
    u32 ms, connection, order, site, value, observed_timer;
    u32 state, timeout_10ms, event_counter, flags;
    u16 handle;
    u8 began;
};

/* ring entries retain a value copy; a reused handle cannot rewrite evidence. */
struct diag_record {
    u32 order, ms, connection, detail;
    u16 handle;
    u8 event, reason, began;
};

struct diag_link {
    u32 connection;
    u16 handle;
    u8 active, began;
    struct sup_state sup;
};

static struct {
    struct diag_record records[DIAG_DEPTH];
    struct diag_link links[DIAG_LINKS];
    struct sup_record sup_records[SUP_DEPTH];
    u32 order, connection, dropped, post_failed, untracked;
    u32 sup_dropped, sup_untracked;
    u8 head, count, queued, initialized;
    u8 sup_head, sup_count;
} diag;

/* spin_lock() is IRQ-only in this SDK; use the real cross-core primitive. */
static DEFINE_SPINLOCK(diag_lock);

static void diag_enter(void)
{
    spinlock_t *lock = &diag_lock;
    local_irq_disable();
    arch_spin_lock(lock);
}

static void diag_leave(void)
{
    spinlock_t *lock = &diag_lock;
    arch_spin_unlock(lock);
    local_irq_enable();
}

/* Defined only by the hash-pinned diagnostic archive: wrong linkage must fail. */
extern int rdx_ble_diag_patch_revision(void);
static void diag_flush(void *unused);

static struct diag_link *diag_link_get(u16 handle, u32 event)
{
    struct diag_link *free_link = NULL;
    struct diag_link *link = NULL;
    for (unsigned i = 0; i < DIAG_LINKS; ++i) {
        if (diag.links[i].active && diag.links[i].handle == handle) {
            link = &diag.links[i];
            break;
        }
        if (!diag.links[i].active && !free_link) free_link = &diag.links[i];
    }
    if (!link) {
        link = free_link;
        if (!link) {
            ++diag.untracked;
            return NULL;
        }
        memset(&link->sup, 0, sizeof(link->sup));
        link->handle = handle;
        link->connection = ++diag.connection;
        if (!link->connection) link->connection = ++diag.connection;
        link->active = 1;
        link->began = 0;
    } else if (event == RDX_BLE_DIAG_CONNECT && link->began) {
        /* An unobserved end must never merge two uses of the same handle. */
        memset(&link->sup, 0, sizeof(link->sup));
        link->connection = ++diag.connection;
        if (!link->connection) link->connection = ++diag.connection;
    }
    if (event == RDX_BLE_DIAG_CONNECT) link->began = 1;
    return link;
}

/* Hot probes never allocate a connection or enqueue work. All helpers below
 * run under diag_lock; no vendor pointers, timer operations or output inside. */
static struct diag_link *sup_link_find(u16 handle)
{
    for (unsigned i = 0; i < DIAG_LINKS; ++i) {
        if (diag.links[i].active && diag.links[i].handle == handle) {
            return &diag.links[i];
        }
    }
    return NULL;
}

static void sup_capture(struct diag_link *link, u32 site, u32 timer_id, u32 value,
                        u32 state, u32 timeout, u32 event_count, u32 flags, u32 now)
{
    struct sup_record *out;
    ++diag.order;
    if (diag.sup_count == SUP_DEPTH) {
        ++diag.sup_dropped;
        return;
    }
    out = &diag.sup_records[(diag.sup_head + diag.sup_count) % SUP_DEPTH];
    out->timer = link->sup;
    out->ms = now;
    out->connection = link->connection;
    out->order = diag.order;
    out->site = site;
    out->value = value;
    out->observed_timer = timer_id;
    out->state = state;
    out->timeout_10ms = timeout;
    out->event_counter = event_count;
    out->flags = flags;
    out->handle = link->handle;
    out->began = link->began;
    ++diag.sup_count;
}

void rdx_ble_diag_sup_state(u32 site, u32 handle, u32 timer_id, u32 value,
                            u32 state, u32 timeout_10ms, u32 event_count, u32 flags)
{
    struct diag_link *link;
    struct sup_state *s;
    u32 now;

    diag_enter();
    now = sys_timer_get_ms();
    if (site == RDX_BLE_SUP_ALLOC) {
        link = sup_link_find((u16)handle);
        if (link) link->active = 0;
        link = diag_link_get((u16)handle, 0);
    } else {
        link = sup_link_find((u16)handle);
    }
    if (!link) {
        ++diag.sup_untracked;
        diag_leave();
        return;
    }
    s = &link->sup;
    if (site == RDX_BLE_SUP_ALLOC) {
        s->tracked = 1;
    } else if (site == RDX_BLE_SUP_TIMEOUT_ENTER || site == RDX_BLE_SUP_COMMIT) {
        /* Freeze before CLOSE_HW/stop/state=8. Later REMOVE/FREE cannot destroy
         * the evidence; TIMEOUT_REASON retains the entry timer's identity. */
        sup_capture(link, site, timer_id, value, state, timeout_10ms,
                    event_count, flags, now);
        s->frozen = 1;
    } else if (site == RDX_BLE_SUP_TIMEOUT_REASON) {
        sup_capture(link, site, timer_id, value, state, timeout_10ms,
                    event_count, flags, now);
    } else if (site == RDX_BLE_SUP_FREE) {
        s->frozen = 1; /* Retain generation until the HCI end boundary. */
    } else if (!s->frozen) {
        switch (site) {
        case RDX_BLE_SUP_REGISTER:
            ++s->timer_gen;
            s->timer_id = timer_id;
            s->period_ms = value;
            s->register_ms = s->basis_ms = now;
            s->valid &= ~(SUP_CONTEXT | SUP_PERIOD | SUP_MODIFY | SUP_RESET |
                          SUP_REMOVED | SUP_LIVE);
            s->valid |= SUP_REGISTERED;
            if (timer_id) s->valid |= SUP_LIVE;
            break;
        case RDX_BLE_SUP_CONTEXT:
            if (s->timer_id != timer_id) ++s->mismatches;
            s->context_ms = now;
            s->valid |= SUP_CONTEXT;
            break;
        case RDX_BLE_SUP_PERIOD:
            if (s->timer_id != timer_id) ++s->mismatches;
            s->period_ms = value;
            s->period_call_ms = s->basis_ms = now;
            s->valid |= SUP_PERIOD;
            break;
        case RDX_BLE_SUP_REMOVE:
            if (s->timer_id != timer_id) ++s->mismatches;
            s->removed_id = timer_id;
            s->remove_ms = now;
            s->valid = (s->valid | SUP_REMOVED) & ~SUP_LIVE;
            break;
        case RDX_BLE_SUP_UPDATE_TEMP:
        case RDX_BLE_SUP_UPDATE_RESTORE:
        case RDX_BLE_SUP_SUBRATE:
            s->origin = site;
            s->origin_ms = now;
            s->origin_period_ms = value;
            break;
        default:
            break;
        }
    }
    diag_leave();
    /* Lifecycle hooks (or the existing application HCI callback) flush later.
     * In particular do not post app_core from timer/RX/event IRQ paths. */
}

void rdx_ble_diag_sup_activity(u32 kind, u32 handle, u32 sample, u32 reset_id)
{
    struct diag_link *link;
    struct sup_state *s;
    u32 now;
    diag_enter();
    link = sup_link_find((u16)handle);
    if (!link || !link->sup.tracked) {
        ++diag.sup_untracked;
        diag_leave();
        return;
    }
    s = &link->sup;
    if (s->frozen) {
        diag_leave();
        return;
    }
    now = sys_timer_get_ms();
    if (kind == RDX_BLE_SUP_RX) {
        u8 invalid = (sample >> 16) & 0xff;
        ++s->rx_calls;
        s->rx_ms = now;
        s->rx_event = sample & 0xffff;
        s->valid |= SUP_RX;
        if (invalid == 0 || invalid == 3) {
            ++s->eligible_rx;
            s->eligible_ms = now;
            s->valid |= SUP_ELIGIBLE;
            if (!reset_id) ++s->rx_no_timer;
        } else {
            ++s->rx_other;
        }
        if (reset_id) {
            if (reset_id != s->timer_id || !(s->valid & SUP_LIVE)) ++s->mismatches;
            ++s->reset_calls;
            s->reset_id = reset_id;
            s->reset_ms = s->basis_ms = now;
            s->valid |= SUP_RESET;
        }
    } else if (kind == RDX_BLE_SUP_EVENT) {
        ++s->event_calls;
        s->event_ms = now;
        s->event_counter = sample & 0xffff;
        s->valid |= SUP_EVENT;
    }
    diag_leave();
}

void rdx_ble_diag_sup_modify_result(u32 timer_id, u32 requested_ms, int result)
{
    if (!rdx_ble_diag_enabled) return;
    diag_enter();
    for (unsigned i = 0; i < DIAG_LINKS; ++i) {
        struct sup_state *s = &diag.links[i].sup;
        if (diag.links[i].active && !s->frozen && (s->valid & SUP_LIVE) &&
            s->timer_id == timer_id) {
            s->modify_ms = sys_timer_get_ms();
            s->modify_period_ms = requested_ms;
            s->modify_result = result;
            s->valid |= SUP_MODIFY;
            break;
        }
    }
    diag_leave();
}

void rdx_ble_diag_record(u32 event, u16 handle, u32 reason, u32 detail)
{
    struct diag_link *link;
    struct diag_record *record;

    diag_enter();
    link = diag_link_get(handle, event);
    ++diag.order;
    if (diag.count == DIAG_DEPTH) {
        /* Keep the earliest evidence, report lost newer records explicitly. */
        ++diag.dropped;
    } else {
        record = &diag.records[(diag.head + diag.count) % DIAG_DEPTH];
        record->order = diag.order;
        record->ms = sys_timer_get_ms();
        record->connection = link ? link->connection : 0;
        record->handle = handle;
        record->event = event;
        record->reason = reason;
        record->detail = detail;
        record->began = link ? link->began : 0;
        ++diag.count;
    }
    if (link && event == RDX_BLE_DIAG_DISCONNECTED) link->active = 0;
    diag_leave();

    /* Only lifecycle boundaries wake app_core; errors stay buffered until end. */
    if (event == RDX_BLE_DIAG_CONNECT || event == RDX_BLE_DIAG_DISCONNECTED) {
        rdx_ble_diag_flush_request();
    }
}

void rdx_ble_diag_flush_request(void)
{
    int msg[3];
    diag_enter();
    if (diag.queued || (!diag.count && !diag.dropped &&
                        !diag.post_failed && !diag.untracked && !diag.sup_count &&
                        !diag.sup_dropped && !diag.sup_untracked)) {
        diag_leave();
        return;
    }
    diag.queued = 1;
    diag_leave();
    msg[0] = (int)diag_flush;
    msg[1] = 1;
    msg[2] = 0;
    if (os_taskq_post_type("app_core", Q_CALLBACK, 3, msg)) {
        diag_enter();
        ++diag.post_failed;
        diag.queued = 0;
        diag_leave();
        /* Retain records; the app HCI callback/later boundary retries. */
    }
}

static const char *diag_name(u8 event)
{
    switch (event) {
    case RDX_BLE_DIAG_CONNECT: return "CTRL_CONNECT";
    case RDX_BLE_DIAG_MIC_CHECK: return "LOCAL_MIC_CHECK_FAIL";
    case RDX_BLE_DIAG_RX_ERROR: return "LOCAL_RX_ERROR";
    case RDX_BLE_DIAG_MIC_DISPATCH: return "MIC_ERROR_DISPATCH";
    case RDX_BLE_DIAG_ENC_ACL: return "LOCAL_ENC_STATE_ACL";
    case RDX_BLE_DIAG_ENC_CTRL: return "LOCAL_ENC_STATE_CTRL";
    case RDX_BLE_DIAG_REMOTE_TERM: return "REMOTE_TERMINATE";
    case RDX_BLE_DIAG_LOCAL_TERM: return "LOCAL_TERMINATE_REQUEST";
    case RDX_BLE_DIAG_TERM_TX: return "TERMINATE_TX_SUBMIT";
    case RDX_BLE_DIAG_COMMIT: return "DISCONNECT_COMMIT";
    case RDX_BLE_DIAG_DISCONNECTED: return "CTRL_DISCONNECTED";
    case RDX_BLE_DIAG_HOST_REQUEST: return "HOST_DISCONNECT_REQUEST";
    case RDX_BLE_DIAG_APP_REQUEST: return "APP_DISCONNECT_REQUEST";
    default: return "UNKNOWN";
    }
}

static void sup_print(const struct sup_record *r)
{
    const struct sup_state *s = &r->timer;
    const char *source = r->site == RDX_BLE_SUP_TIMEOUT_ENTER ? "TIMER_ENTER" :
                         r->site == RDX_BLE_SUP_TIMEOUT_REASON ? "TIMER_REASON" : "COMMIT";
    /* LOG_MAX_ARG_NUM is 14 and applies to every SDK printf. Split by meaning
     * so one snapshot stays readable and no line silently truncates. */
    printf("[BLE_SUP] n=%u t=%u conn=%u h=%04x src=%s began=%u tracked=%u\n",
           r->order, r->ms, r->connection, r->handle, source, r->began, s->tracked);
    printf("[BLE_SUP_RSN] n=%u reason=%02x chosen_wait=%u state=%u param_to10=%u "
           "evt=%u proc=%u wait=%u role=%u enc=%u observed_id=%u\n",
           r->order, r->value & 0xff, (r->value >> 8) & 1, r->state, r->timeout_10ms,
           r->event_counter, r->flags & 0xff, (r->flags >> 8) & 1,
           (r->flags >> 16) & 0xff, r->flags >> 24, r->observed_timer);
    printf("[BLE_SUP_TIMER] n=%u valid=%03x id=%u gen=%u req_ms=%u basis=%u "
           "due_est=%u reg=%u ctx=%u period=%u\n",
           r->order, s->valid, s->timer_id, s->timer_gen, s->period_ms,
           s->basis_ms, s->basis_ms + s->period_ms,
           s->register_ms, s->context_ms, s->period_call_ms);
    printf("[BLE_SUP_TIMER_MUT] n=%u reset=%u resets=%u reset_id=%u "
           "mod_t=%u mod_ms=%u mod_ret=%d remove=%u removed_id=%u mismatch=%u\n",
           r->order, s->reset_ms, s->reset_calls, s->reset_id,
           s->modify_ms, s->modify_period_ms, s->modify_result,
           s->remove_ms, s->removed_id, s->mismatches);
    printf("[BLE_SUP_PARAM] n=%u origin=%u origin_t=%u origin_ms=%u\n",
           r->order, s->origin, s->origin_ms, s->origin_period_ms);
    printf("[BLE_SUP_RX] n=%u rx_t=%u eligible_t=%u rx_calls=%u eligible=%u "
           "no_timer=%u other=%u rx_evt=%u event_t=%u event_calls=%u event_ctr=%u\n",
           r->order, s->rx_ms, s->eligible_ms, s->rx_calls, s->eligible_rx,
           s->rx_no_timer, s->rx_other, s->rx_event,
           s->event_ms, s->event_calls, s->event_counter);
}

static void diag_flush(void *unused)
{
    struct diag_record record;
    struct sup_record sup;
    u32 dropped, post_failed, untracked, sup_dropped, sup_untracked;
    unsigned budget = DIAG_DEPTH;
    (void)unused;

    diag_enter();
    dropped = diag.dropped;
    post_failed = diag.post_failed;
    untracked = diag.untracked;
    sup_dropped = diag.sup_dropped;
    sup_untracked = diag.sup_untracked;
    diag.dropped = diag.post_failed = diag.untracked = 0;
    diag.sup_dropped = diag.sup_untracked = 0;
    diag_leave();
    printf("[BLE_DIAG] flush dropped=%u post_failed=%u untracked=%u "
           "sup_dropped=%u sup_untracked=%u\n",
           dropped, post_failed, untracked, sup_dropped, sup_untracked);
    while (budget--) {
        diag_enter();
        if (!diag.count) {
            diag_leave();
            break;
        }
        record = diag.records[diag.head];
        diag.head = (diag.head + 1) % DIAG_DEPTH;
        --diag.count;
        diag_leave();
        printf("[BLE_DIAG] n=%u t=%u conn=%u h=%04x src=%s reason=%02x "
               "aux=%u enc=%u proc=%u role=%u began=%u\n",
               record.order, record.ms, record.connection, record.handle,
               diag_name(record.event), record.reason, record.detail & 0xff,
               (record.detail >> 8) & 0xff, (record.detail >> 16) & 0xff,
               record.detail >> 24, record.began);
    }
    budget = SUP_DEPTH;
    while (budget--) {
        diag_enter();
        if (!diag.sup_count) {
            diag_leave();
            break;
        }
        sup = diag.sup_records[diag.sup_head];
        diag.sup_head = (diag.sup_head + 1) % SUP_DEPTH;
        --diag.sup_count;
        diag_leave();
        sup_print(&sup);
    }
    diag_enter();
    diag.queued = 0;
    diag_leave();
    rdx_ble_diag_flush_request();
}

void rdx_ble_diag_init(void)
{
    diag_enter();
    if (diag.initialized) {
        diag_leave();
        return;
    }
    diag.initialized = 1;
    diag_leave();
    printf("[BLE_DIAG] enabled=1 rev=%d ctrl=6ecbbd876ea08e6d depth=%u "
           "sup_depth=%u build=%s %s\n", rdx_ble_diag_patch_revision(), DIAG_DEPTH,
           SUP_DEPTH, __DATE__, __TIME__);
    rdx_ble_diag_flush_request();
}

void rdx_ble_diag_app_request(u16 handle, u8 site)
{
    rdx_ble_diag_record(RDX_BLE_DIAG_APP_REQUEST, handle, 0, site);
}

#else

void rdx_ble_diag_record(u32 event, u16 handle, u32 reason, u32 detail) {}
void rdx_ble_diag_init(void) {}
void rdx_ble_diag_flush_request(void) {}
void rdx_ble_diag_app_request(u16 handle, u8 site) {}
void rdx_ble_diag_sup_state(u32 site, u32 handle, u32 timer_id, u32 value,
                            u32 state, u32 timeout_10ms, u32 event_count, u32 flags) {}
void rdx_ble_diag_sup_activity(u32 kind, u32 handle, u32 sample, u32 reset_id) {}
void rdx_ble_diag_sup_modify_result(u32 timer_id, u32 requested_ms, int result) {}

#endif
