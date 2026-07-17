#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".rdx_ble_session.data.bss")
#pragma data_seg(".rdx_ble_session.data")
#pragma const_seg(".rdx_ble_session.text.const")
#pragma code_seg(".rdx_ble_session.text")
#endif

#include "rdx_ble_session.h"

static rdx_ble_link_state_t s_rdx_ble_link;
static rdx_ble_config_session_t s_rdx_config_session;

void rdx_ble_session_reset(void)
{
    memset(&s_rdx_ble_link, 0, sizeof(s_rdx_ble_link));
    memset(&s_rdx_config_session, 0, sizeof(s_rdx_config_session));
    s_rdx_config_session.auth_state = RDX_SESSION_UNAUTHORIZED;
}

void rdx_ble_session_on_connected(u16 con_handle)
{
    rdx_ble_session_reset();
    s_rdx_ble_link.con_handle = con_handle;
    s_rdx_ble_link.connected = 1;
}

void rdx_ble_session_on_disconnected(u16 con_handle)
{
    if (!s_rdx_ble_link.connected ||
        s_rdx_ble_link.con_handle != con_handle) {
        return;
    }
    rdx_ble_session_reset();
}

u8 rdx_ble_session_is_current(u16 con_handle)
{
    return (s_rdx_ble_link.connected &&
            s_rdx_ble_link.con_handle == con_handle) ? 1 : 0;
}

void rdx_ble_session_set_mtu(u16 con_handle, u16 mtu_size)
{
    if (rdx_ble_session_is_current(con_handle)) {
        s_rdx_ble_link.mtu_size = mtu_size;
    }
}

void rdx_ble_session_set_encrypted(u16 con_handle, u8 encrypted)
{
    if (rdx_ble_session_is_current(con_handle)) {
        s_rdx_ble_link.encrypted = encrypted ? 1 : 0;
    }
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
    return &s_rdx_ble_link;
}

const rdx_ble_config_session_t *rdx_ble_session_get_config_state(void)
{
    return &s_rdx_config_session;
}

u8 rdx_protocol_session_mark_identified(u16 con_handle)
{
    if (!rdx_ble_session_is_current(con_handle)) {
        return 0;
    }
    if (s_rdx_config_session.auth_state == RDX_SESSION_UNAUTHORIZED) {
        s_rdx_config_session.auth_state = RDX_SESSION_IDENTIFIED;
    }
    return 1;
}

u8 rdx_protocol_session_authorize(u16 con_handle)
{
    if (!rdx_ble_session_is_current(con_handle) ||
        s_rdx_config_session.auth_state < RDX_SESSION_IDENTIFIED) {
        return 0;
    }
    s_rdx_config_session.auth_state = RDX_SESSION_AUTHORIZED;
    return 1;
}

void rdx_protocol_session_revoke(u16 con_handle)
{
    if (!rdx_ble_session_is_current(con_handle)) {
        return;
    }
    s_rdx_config_session.auth_state = RDX_SESSION_UNAUTHORIZED;
}

u8 rdx_protocol_session_is_authorized(u16 con_handle)
{
    return (rdx_ble_session_is_current(con_handle) &&
            s_rdx_config_session.auth_state == RDX_SESSION_AUTHORIZED) ? 1 : 0;
}
