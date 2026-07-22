#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".rdx_ble_session.data.bss")
#pragma data_seg(".rdx_ble_session.data")
#pragma const_seg(".rdx_ble_session.text.const")
#pragma code_seg(".rdx_ble_session.text")
#endif

#include "rdx_ble_session.h"

static rdx_ble_link_state_t s_rdx_ble_links[RDX_BLE_LINK_MAX];
static rdx_ble_config_session_t s_rdx_config_session;
static u32 s_rdx_ble_transport_epoch = 1;
static u8 s_rdx_rdx_link_index = RDX_BLE_LINK_INVALID_INDEX;
static u8 s_rdx_hid_link_index = RDX_BLE_LINK_INVALID_INDEX;
static u8 s_rdx_compat_link_index = RDX_BLE_LINK_INVALID_INDEX;

static void rdx_ble_session_generation_advance(rdx_ble_link_state_t *link)
{
    link->slot_generation++;
    if (!link->slot_generation) {
        link->slot_generation = 1;
    }
}

static void rdx_ble_session_link_clear(rdx_ble_link_state_t *link)
{
    void *hdl = link->ble_hdl;
    u32 generation = link->slot_generation;

    memset(link, 0, sizeof(*link));
    link->ble_hdl = hdl;
    link->mtu_size = 20;
    link->slot_generation = generation;
}

void rdx_ble_session_transport_init(void *primary_hdl, void *secondary_hdl)
{
    memset(s_rdx_ble_links, 0, sizeof(s_rdx_ble_links));
    memset(&s_rdx_config_session, 0, sizeof(s_rdx_config_session));
    s_rdx_ble_links[0].ble_hdl = primary_hdl;
    s_rdx_ble_links[1].ble_hdl = secondary_hdl;
    s_rdx_ble_links[0].mtu_size = 20;
    s_rdx_ble_links[1].mtu_size = 20;
    s_rdx_ble_links[0].slot_generation = 1;
    s_rdx_ble_links[1].slot_generation = 1;
    s_rdx_rdx_link_index = RDX_BLE_LINK_INVALID_INDEX;
    s_rdx_hid_link_index = RDX_BLE_LINK_INVALID_INDEX;
    s_rdx_compat_link_index = RDX_BLE_LINK_INVALID_INDEX;
    s_rdx_ble_transport_epoch++;
    if (!s_rdx_ble_transport_epoch) {
        s_rdx_ble_transport_epoch = 1;
    }
}

void rdx_ble_session_transport_deinit(void)
{
    s_rdx_ble_transport_epoch++;
    if (!s_rdx_ble_transport_epoch) {
        s_rdx_ble_transport_epoch = 1;
    }
    memset(s_rdx_ble_links, 0, sizeof(s_rdx_ble_links));
    memset(&s_rdx_config_session, 0, sizeof(s_rdx_config_session));
    s_rdx_rdx_link_index = RDX_BLE_LINK_INVALID_INDEX;
    s_rdx_hid_link_index = RDX_BLE_LINK_INVALID_INDEX;
    s_rdx_compat_link_index = RDX_BLE_LINK_INVALID_INDEX;
}

rdx_ble_link_state_t *rdx_ble_session_find_by_hdl(void *hdl)
{
    u8 index;

    if (!hdl) {
        return NULL;
    }
    for (index = 0; index < RDX_BLE_LINK_MAX; index++) {
        if (s_rdx_ble_links[index].ble_hdl == hdl) {
            return &s_rdx_ble_links[index];
        }
    }
    return NULL;
}

rdx_ble_link_state_t *rdx_ble_session_find(void *hdl, u16 con_handle)
{
    rdx_ble_link_state_t *link = rdx_ble_session_find_by_hdl(hdl);

    if (!link || !link->connected || link->con_handle != con_handle) {
        return NULL;
    }
    return link;
}

rdx_ble_link_state_t *rdx_ble_session_find_by_con_handle(u16 con_handle)
{
    u8 index;

    if (!con_handle) {
        return NULL;
    }
    for (index = 0; index < RDX_BLE_LINK_MAX; index++) {
        if (s_rdx_ble_links[index].connected &&
            s_rdx_ble_links[index].con_handle == con_handle) {
            return &s_rdx_ble_links[index];
        }
    }
    return NULL;
}

rdx_ble_link_state_t *rdx_ble_session_link_accept(void *hdl, u16 con_handle)
{
    rdx_ble_link_state_t *link = rdx_ble_session_find_by_hdl(hdl);

    if (!link || !con_handle || link->connected ||
        rdx_ble_session_find_by_con_handle(con_handle)) {
        return NULL;
    }
    rdx_ble_session_generation_advance(link);
    link->con_handle = con_handle;
    link->mtu_size = 20;
    link->connected = 1;
    link->encrypted = 0;
    link->peer_addr_type = 0;
    memset(link->peer_addr, 0, sizeof(link->peer_addr));
    link->conn_param_index = 0;
    return link;
}

rdx_ble_link_state_t *rdx_ble_session_link_release(void *hdl, u16 con_handle)
{
    rdx_ble_link_state_t *link;

    link = hdl ? rdx_ble_session_find(hdl, con_handle) :
           rdx_ble_session_find_by_con_handle(con_handle);

    if (!link) {
        return NULL;
    }
    if (rdx_ble_session_link_index(link) == s_rdx_rdx_link_index) {
        s_rdx_rdx_link_index = RDX_BLE_LINK_INVALID_INDEX;
    }
    if (rdx_ble_session_link_index(link) == s_rdx_hid_link_index) {
        s_rdx_hid_link_index = RDX_BLE_LINK_INVALID_INDEX;
    }
    if (rdx_ble_session_link_index(link) == s_rdx_compat_link_index) {
        s_rdx_compat_link_index = RDX_BLE_LINK_INVALID_INDEX;
        memset(&s_rdx_config_session, 0, sizeof(s_rdx_config_session));
    }
    rdx_ble_session_generation_advance(link);
    rdx_ble_session_link_clear(link);
    return link;
}

u8 rdx_ble_session_active_count(void)
{
    u8 index;
    u8 count = 0;

    for (index = 0; index < RDX_BLE_LINK_MAX; index++) {
        if (s_rdx_ble_links[index].connected) {
            count++;
        }
    }
    return count;
}

u8 rdx_ble_session_link_index(const rdx_ble_link_state_t *link)
{
    u8 index;

    for (index = 0; index < RDX_BLE_LINK_MAX; index++) {
        if (&s_rdx_ble_links[index] == link) {
            return index;
        }
    }
    return RDX_BLE_LINK_INVALID_INDEX;
}

static rdx_ble_claim_result_t rdx_ble_session_claim(
    rdx_ble_link_state_t *link,
    u32 expected_slot_generation,
    rdx_ble_capability_t capability)
{
    u8 index = rdx_ble_session_link_index(link);
    u8 *owner_index;
    u8 other_owner_index;

    if (index >= RDX_BLE_LINK_MAX || !link->connected ||
        link->slot_generation != expected_slot_generation) {
        return RDX_BLE_CLAIM_STALE;
    }
    if (link->capability == capability) {
        return RDX_BLE_CLAIM_OK;
    }
    if (link->capability != RDX_BLE_CAPABILITY_NONE) {
        return RDX_BLE_CLAIM_CONFLICT;
    }

    if (capability == RDX_BLE_CAPABILITY_RDX) {
        owner_index = &s_rdx_rdx_link_index;
        other_owner_index = s_rdx_hid_link_index;
    } else if (capability == RDX_BLE_CAPABILITY_HID) {
        owner_index = &s_rdx_hid_link_index;
        other_owner_index = s_rdx_rdx_link_index;
    } else {
        return RDX_BLE_CLAIM_CONFLICT;
    }
    if (*owner_index < RDX_BLE_LINK_MAX) {
        return RDX_BLE_CLAIM_BUSY;
    }
    if (other_owner_index == index) {
        return RDX_BLE_CLAIM_CONFLICT;
    }

    link->capability = capability;
    *owner_index = index;
    return RDX_BLE_CLAIM_OK;
}

rdx_ble_claim_result_t rdx_ble_session_claim_rdx(
    rdx_ble_link_state_t *link,
    u32 expected_slot_generation)
{
    return rdx_ble_session_claim(link, expected_slot_generation,
                                 RDX_BLE_CAPABILITY_RDX);
}

rdx_ble_claim_result_t rdx_ble_session_claim_hid(
    rdx_ble_link_state_t *link,
    u32 expected_slot_generation)
{
    return rdx_ble_session_claim(link, expected_slot_generation,
                                 RDX_BLE_CAPABILITY_HID);
}

static rdx_ble_link_state_t *rdx_ble_session_owner_get(u8 owner_index,
                                                       u8 capability)
{
    rdx_ble_link_state_t *link;

    if (owner_index >= RDX_BLE_LINK_MAX) {
        return NULL;
    }
    link = &s_rdx_ble_links[owner_index];
    return (link->connected && link->capability == capability) ? link : NULL;
}

rdx_ble_link_state_t *rdx_ble_session_get_rdx_link(void)
{
    return rdx_ble_session_owner_get(s_rdx_rdx_link_index,
                                     RDX_BLE_CAPABILITY_RDX);
}

rdx_ble_link_state_t *rdx_ble_session_get_hid_link(void)
{
    return rdx_ble_session_owner_get(s_rdx_hid_link_index,
                                     RDX_BLE_CAPABILITY_HID);
}

u8 rdx_ble_session_link_is_rdx(const rdx_ble_link_state_t *link)
{
    return (link && link == rdx_ble_session_get_rdx_link()) ? 1 : 0;
}

u8 rdx_ble_session_link_is_hid(const rdx_ble_link_state_t *link)
{
    return (link && link == rdx_ble_session_get_hid_link()) ? 1 : 0;
}

void rdx_ble_session_link_set_hid_pairing_pending(
    rdx_ble_link_state_t *link,
    u8 pending)
{
    if (link && link->connected) {
        link->hid_pairing_pending = pending ? 1 : 0;
    }
}

u8 rdx_ble_session_link_is_hid_pairing_pending(
    const rdx_ble_link_state_t *link)
{
    return (link && link->connected && link->hid_pairing_pending) ? 1 : 0;
}

rdx_ble_async_token_t rdx_ble_session_token_capture(
    const rdx_ble_link_state_t *link)
{
    rdx_ble_async_token_t token = {
        .slot_index = RDX_BLE_LINK_INVALID_INDEX,
        .slot_generation = 0,
        .transport_epoch = s_rdx_ble_transport_epoch,
    };

    if (link) {
        token.slot_index = rdx_ble_session_link_index(link);
        token.slot_generation = link->slot_generation;
    }
    return token;
}

static rdx_ble_link_state_t *rdx_ble_session_token_slot_resolve(
    const rdx_ble_async_token_t *token)
{
    rdx_ble_link_state_t *link;

    if (!token || token->slot_index >= RDX_BLE_LINK_MAX ||
        token->transport_epoch != s_rdx_ble_transport_epoch) {
        return NULL;
    }
    link = &s_rdx_ble_links[token->slot_index];
    if (link->slot_generation != token->slot_generation) {
        return NULL;
    }
    return link;
}

rdx_ble_link_state_t *rdx_ble_session_idle_token_resolve(
    const rdx_ble_async_token_t *token)
{
    rdx_ble_link_state_t *link = rdx_ble_session_token_slot_resolve(token);

    return (link && !link->connected) ? link : NULL;
}

rdx_ble_link_state_t *rdx_ble_session_link_token_resolve(
    const rdx_ble_async_token_t *token)
{
    rdx_ble_link_state_t *link = rdx_ble_session_token_slot_resolve(token);

    return (link && link->connected) ? link : NULL;
}

void rdx_ble_session_link_set_mtu(rdx_ble_link_state_t *link, u16 mtu_size)
{
    if (link && link->connected) {
        link->mtu_size = mtu_size;
    }
}

void rdx_ble_session_link_set_encrypted(rdx_ble_link_state_t *link,
                                        u8 encrypted)
{
    if (link && link->connected) {
        link->encrypted = encrypted ? 1 : 0;
    }
}

void rdx_ble_session_link_set_peer(rdx_ble_link_state_t *link,
                                   u8 peer_addr_type,
                                   const u8 peer_addr[6])
{
    if (!link || !link->connected || !peer_addr) {
        return;
    }
    link->peer_addr_type = peer_addr_type;
    memcpy(link->peer_addr, peer_addr, sizeof(link->peer_addr));
}

void rdx_ble_session_link_set_conn_params(rdx_ble_link_state_t *link,
                                          u16 interval,
                                          u16 latency,
                                          u16 supervision_timeout)
{
    if (!link || !link->connected) {
        return;
    }
    link->conn_interval = interval;
    link->conn_latency = latency;
    link->supervision_timeout = supervision_timeout;
}

void rdx_ble_session_link_set_conn_param_index(rdx_ble_link_state_t *link,
                                                u8 conn_param_index)
{
    if (link && link->connected) {
        link->conn_param_index = conn_param_index;
    }
}

void rdx_ble_session_reset(void)
{
    rdx_ble_session_transport_deinit();
}

void rdx_ble_session_on_connected(u16 con_handle)
{
    rdx_ble_link_state_t *link;
    u8 index;

    link = rdx_ble_session_find_by_con_handle(con_handle);
    if (!link) {
        for (index = 0; index < RDX_BLE_LINK_MAX; index++) {
            if (!s_rdx_ble_links[index].connected) {
                link = &s_rdx_ble_links[index];
                rdx_ble_session_generation_advance(link);
                link->con_handle = con_handle;
                link->mtu_size = 20;
                link->connected = 1;
                break;
            }
        }
    }
    s_rdx_compat_link_index = rdx_ble_session_link_index(link);
    memset(&s_rdx_config_session, 0, sizeof(s_rdx_config_session));
}

void rdx_ble_session_on_disconnected(u16 con_handle)
{
    rdx_ble_link_state_t *link = rdx_ble_session_find_by_con_handle(con_handle);

    if (!link) {
        return;
    }
    rdx_ble_session_link_release(link->ble_hdl, con_handle);
}

u8 rdx_ble_session_is_current(u16 con_handle)
{
    const rdx_ble_link_state_t *link = rdx_ble_session_get_link_state();

    return (link && link->connected && link->con_handle == con_handle) ? 1 : 0;
}

void rdx_ble_session_set_mtu(u16 con_handle, u16 mtu_size)
{
    rdx_ble_session_link_set_mtu(rdx_ble_session_find_by_con_handle(con_handle),
                                 mtu_size);
}

void rdx_ble_session_set_encrypted(u16 con_handle, u8 encrypted)
{
    rdx_ble_session_link_set_encrypted(
        rdx_ble_session_find_by_con_handle(con_handle), encrypted);
}

u8 rdx_ble_session_activate_rdx(u16 con_handle)
{
    rdx_ble_link_state_t *link = rdx_ble_session_find_by_con_handle(con_handle);
    rdx_ble_claim_result_t claim_result;

    if (!link) {
        return 0;
    }
    claim_result = rdx_ble_session_claim_rdx(link, link->slot_generation);
    if (claim_result != RDX_BLE_CLAIM_OK) {
        return 0;
    }
    link->rdx_runtime_active = 1;
    s_rdx_compat_link_index = rdx_ble_session_link_index(link);
    s_rdx_config_session.active = 1;
    return 1;
}

u8 rdx_ble_session_is_rdx_active(u16 con_handle)
{
    rdx_ble_link_state_t *link =
        rdx_ble_session_find_by_con_handle(con_handle);

    return (rdx_ble_session_link_is_rdx(link) &&
            link->rdx_runtime_active) ? 1 : 0;
}

void rdx_ble_session_set_config_ccc(u16 con_handle, u8 configured)
{
    if (rdx_ble_session_is_current(con_handle)) {
        s_rdx_config_session.ccc_configured = configured ? 1 : 0;
    }
}

void rdx_ble_session_set_stream_tx_ready(u16 con_handle, u8 ready)
{
    if (rdx_ble_session_is_current(con_handle)) {
        s_rdx_config_session.stream_tx_ready = ready ? 1 : 0;
    }
}

const rdx_ble_link_state_t *rdx_ble_session_get_link_state(void)
{
    if (s_rdx_compat_link_index >= RDX_BLE_LINK_MAX) {
        return NULL;
    }
    return &s_rdx_ble_links[s_rdx_compat_link_index];
}

const rdx_ble_config_session_t *rdx_ble_session_get_config_state(void)
{
    return &s_rdx_config_session;
}
