/* One existing custom command releases RDX without disconnecting HID.
 * The receive FIFO barrier prevents attach from replacing the ingress epoch
 * until the old parser work has drained. No on-wire session protocol. */
#include "app_config.h"
#include "system/includes.h"
#include "rdx_app_config.h"
#include "rdx_session_control.h"
#if (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN)
static rdx_ble_async_token_t ingress;
static u8 ingress_valid;

void rdx_session_control_reset(void)
{
    local_irq_disable();
    ingress_valid = 0;
    local_irq_enable();
}
void rdx_session_control_attach(const rdx_ble_link_state_t *link)
{
    local_irq_disable();
    ingress = rdx_ble_session_token_capture(link);
    ingress_valid = 1;
    local_irq_enable();
}
static void execute(rdx_ble_async_token_t *token)
{
    /* btstack serializes logical and physical detach. Duplicate or stale
     * callbacks cannot affect a new owner, including reuse of the same ACL. */
    if (rdx_ble_session_rdx_token_resolve(token, 1)) {
        rdx_ble_server_session_detach(token, RDX_DETACH_LOGICAL_COMMAND);
    }
    free(token);
}
void rdx_session_control_handle_custom(const char *value)
{
    rdx_ble_async_token_t *token;
    int msg[3];
    if (!value || strcmp(value, "1")) return;
    token = malloc(sizeof(*token));
    if (!token) {
        printf("[RDX_CLOSE] request allocation failed\n");
        return;
    }
    local_irq_disable();
    if (!ingress_valid) {
        local_irq_enable();
        free(token);
        return;
    }
    *token = ingress;
    local_irq_enable();
    msg[0] = (int)execute; msg[1] = 1; msg[2] = (int)token;
    if (os_taskq_post_type("btstack", Q_CALLBACK, 3, msg)) {
        free(token);
        printf("[RDX_CLOSE] request queue failed\n");
    }
}
#endif
