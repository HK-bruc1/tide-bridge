/*=====================================================================================
 HEADER NAME: rdx_wifi_event.c
 MODULE NAME: WiFi domain event bus implementation.

 GENERAL DESCRIPTION:
    Single-subscriber WiFi event bus. See rdx_wifi_event.h for the visibility
    contract and the producer / consumer list.

    Threading model:
      - rdx_wifi_event_register() / _unregister() are expected to be called
        exactly once during rdx_app_tasks_init(), before any event source
        becomes active. No internal locking is provided.
      - Dispatches may originate from sys timer task or app_core task; the
        consumer is responsible for re-posting heavy work (BLE / record / UI)
        to app_core, in the same fashion as PROTOCOL_EVENT_CMD_SYS_RESET.
=======================================================================================*/

/******************************************************************************
* Include files
******************************************************************************/
#include "app_config.h"
#include "system/includes.h"
#include "rdx_commonDef.h"
#include "rdx_wifi_event.h"

#if (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN)

static rdx_wifi_event_fn s_wifi_event_cb = NULL;

void rdx_wifi_event_register(rdx_wifi_event_fn cb)
{
    s_wifi_event_cb = cb;
}

void rdx_wifi_event_unregister(void)
{
    s_wifi_event_cb = NULL;
}

/* Intra-domain dispatch helper. Exported (non-static) so xxpUart.c can
 * extern-declare and call it without going through public wrappers, but
 * intentionally NOT declared in the public header to discourage external use.
 *
 * Callers outside the WiFi/protocol domain must go through the public
 * rdx_wifi_event_post_* wrappers instead. */
void _rdx_wifi_event_dispatch(RdxWifiEvent event, void *data, u32 len)
{
    if (s_wifi_event_cb) {
        s_wifi_event_cb(event, data, len);
    }
}

void rdx_wifi_event_post_ctrl(u8 cmd)
{
    _rdx_wifi_event_dispatch(RDX_WIFI_EVENT_CTRL, &cmd, 1);
}

#endif /* (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN) */
