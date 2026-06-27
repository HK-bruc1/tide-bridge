#ifndef __RDX_EVENT_BUS_H__
#define __RDX_EVENT_BUS_H__

#include "typedef.h"

typedef enum {
	RDX_EVENT_RECORD_START        = 0x01,
	RDX_EVENT_RECORD_STOP         = 0x02,
	RDX_EVENT_RECORD_DATA_READY   = 0x03,
	RDX_EVENT_WIFI_CONNECTED      = 0x10,
	RDX_EVENT_WIFI_DISCONNECTED   = 0x11,
	RDX_EVENT_WIFI_DATA_SENT      = 0x12,
	RDX_EVENT_STORAGE_FORMAT_DONE = 0x20,
	RDX_EVENT_STORAGE_SYNC_DONE   = 0x21,
	RDX_EVENT_DEVICE_PAIRED       = 0x30,
	RDX_EVENT_DEVICE_UNPAIRED     = 0x31,
	RDX_EVENT_DEVICE_POWEROFF     = 0x32,
	RDX_EVENT_BLE_CONNECTED       = 0x40,
	RDX_EVENT_BLE_DISCONNECTED    = 0x41,
	RDX_EVENT_IDLE_ENTER          = 0x50,
	RDX_EVENT_IDLE_EXIT           = 0x51,
	RDX_EVENT_TIME_SYNCED         = 0x52,
	RDX_EVENT_MAX                 = 0x60,
} rdx_event_id_t;

typedef void (*rdx_event_callback_t)(rdx_event_id_t event, void *payload, u32 len, void *user_ctx);

#define RDX_EVENT_MAX_SUBSCRIBERS  8

void rdx_event_bus_init(void);
int  rdx_event_subscribe(rdx_event_id_t event, rdx_event_callback_t callback, void *user_ctx);
int  rdx_event_unsubscribe(rdx_event_id_t event, rdx_event_callback_t callback, void *user_ctx);
void rdx_event_publish(rdx_event_id_t event, void *payload, u32 len);
int  rdx_event_publish_async(rdx_event_id_t event, void *payload, u32 len);

#endif
