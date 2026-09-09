#ifndef RDX_SESSION_CONTROL_H
#define RDX_SESSION_CONTROL_H
#include "rdx_ble_session.h"
#define RDX_SESSION_CUSTOM_CMD "rdxclose"
enum rdx_detach_cause {
    RDX_DETACH_PHYSICAL,
    RDX_DETACH_LOGICAL_COMMAND,
};
void rdx_session_control_attach(const rdx_ble_link_state_t *link);
void rdx_session_control_reset(void);
void rdx_session_control_handle_custom(const char *value);
void rdx_ble_server_session_detach(const rdx_ble_async_token_t *token,
                                 enum rdx_detach_cause cause);
#endif
