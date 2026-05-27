/*=====================================================================================
 HEADER NAME: rdx_wifi_event.h
 MODULE NAME: WiFi domain event bus (intra-domain only).

 GENERAL DESCRIPTION:
    Standalone event bus for the WiFi domain, decoupled from the protocol
    event bus (ProtocolEvents / RdxProtocolCallbacks).

    Header visibility:
      Intra-domain only. Do NOT include from UI layer, board layer or other
      SDK modules. Cross-module consumers should observe WiFi state via the
      existing rdx_app_* public APIs (e.g. rdx_app_get_wifi_info()) instead.
=======================================================================================*/
#ifndef __RDX_WIFI_EVENT_H__
#define __RDX_WIFI_EVENT_H__

/******************************************************************************
* Include files
******************************************************************************/
#include "system/includes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* WiFi event identifiers.
 * Power on/off is fast and atomic on this platform; no transitional
 * "request" variant is exposed - subscribers only see the final state flip. */
typedef enum {
    RDX_WIFI_EVENT_NONE = 0,

    /* App-issued WiFi control command: *APP#wifi#N#
     *   data = u8* (0 = close, 1 = open), len = 1.
     * The protocol layer keeps only format / precondition checks (OTA running,
     * recording, storage busy). All business scheduling (e.g. dispatching
     * rdx_app_wifi_handle on app_core) happens in the consumer. */
    RDX_WIFI_EVENT_CTRL,

    /* State flip events. data = NULL, len = 0.
     * Fired right after RdxWifiInfo.onoff is updated by the driver. */
    RDX_WIFI_EVENT_OPEN,
    RDX_WIFI_EVENT_CLOSE,
    RDX_WIFI_EVENT_AP_CONNECT_TIMEOUT,
    RDX_WIFI_EVENT_TCP_CONNECT_TIMEOUT,
    RDX_WIFI_EVENT_DATA_TRANSFER_TIMEOUT,

    RDX_WIFI_EVENT_MAX
} RdxWifiEvent;

typedef void (*rdx_wifi_event_fn)(RdxWifiEvent event, void *data, u32 len);

/* Register / unregister the single WiFi event consumer.
 * Intended to be called once from rdx_app_tasks_init(). */
void rdx_wifi_event_register(rdx_wifi_event_fn cb);
void rdx_wifi_event_unregister(void);

/* Cross-file dispatch entry for the protocol layer.
 * rdx_protocol_handle_wifi_ctrl() calls this after a successful parse. */
void rdx_wifi_event_post_ctrl(u8 cmd);

#ifdef __cplusplus
}
#endif

#endif /* __RDX_WIFI_EVENT_H__ */
