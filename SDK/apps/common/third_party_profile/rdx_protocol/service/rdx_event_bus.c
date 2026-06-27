#include "rdx_event_bus.h"
#include "rdx_err.h"
#include "system/includes.h"
#include "rdx_log.h"

typedef struct {
	rdx_event_callback_t callback;
	void *user_ctx;
	u8   active;
} rdx_event_subscriber_t;

static rdx_event_subscriber_t g_subscribers[RDX_EVENT_MAX][RDX_EVENT_MAX_SUBSCRIBERS];

#define RDX_EVENT_ASYNC_POOL_SIZE  4
#define RDX_EVENT_ASYNC_PAYLOAD_MAX  16

struct rdx_event_async_msg {
	rdx_event_id_t event;
	void *payload;
	u32   len;
	u8    busy;
	u8    payload_buf[RDX_EVENT_ASYNC_PAYLOAD_MAX];
};

static struct rdx_event_async_msg g_async_pool[RDX_EVENT_ASYNC_POOL_SIZE];

void rdx_event_bus_init(void)
{
	u32 i, j;
	for (i = 0; i < RDX_EVENT_MAX; i++) {
		for (j = 0; j < RDX_EVENT_MAX_SUBSCRIBERS; j++) {
			g_subscribers[i][j].active = 0;
		}
	}

	for (i = 0; i < RDX_EVENT_ASYNC_POOL_SIZE; i++) {
		g_async_pool[i].busy = 0;
	}
}

int rdx_event_subscribe(rdx_event_id_t event, rdx_event_callback_t callback, void *user_ctx)
{
	u32 i;

	if (event >= RDX_EVENT_MAX || callback == NULL) {
		return RDX_ERR_INVAL;
	}

	for (i = 0; i < RDX_EVENT_MAX_SUBSCRIBERS; i++) {
		if (g_subscribers[event][i].active &&
		    g_subscribers[event][i].callback == callback) {
			return RDX_OK;
		}
	}

	for (i = 0; i < RDX_EVENT_MAX_SUBSCRIBERS; i++) {
		if (!g_subscribers[event][i].active) {
			g_subscribers[event][i].callback = callback;
			g_subscribers[event][i].user_ctx = user_ctx;
			g_subscribers[event][i].active = 1;
			return RDX_OK;
		}
	}

	RDX_LOGW("event_bus subscribe full event=%d", event);
	return RDX_ERR_NOMEM;
}

int rdx_event_unsubscribe(rdx_event_id_t event, rdx_event_callback_t callback, void *user_ctx)
{
	u32 i;

	if (event >= RDX_EVENT_MAX) {
		return RDX_ERR_INVAL;
	}

	if (callback == NULL) {
		for (i = 0; i < RDX_EVENT_MAX_SUBSCRIBERS; i++) {
			g_subscribers[event][i].active = 0;
		}
		return RDX_OK;
	}

	for (i = 0; i < RDX_EVENT_MAX_SUBSCRIBERS; i++) {
		if (g_subscribers[event][i].active &&
		    g_subscribers[event][i].callback == callback &&
		    g_subscribers[event][i].user_ctx == user_ctx) {
			g_subscribers[event][i].active = 0;
			return RDX_OK;
		}
	}

	return RDX_ERR_NOENT;
}

void rdx_event_publish(rdx_event_id_t event, void *payload, u32 len)
{
	u32 i;

	if (event >= RDX_EVENT_MAX) {
		return;
	}

	for (i = 0; i < RDX_EVENT_MAX_SUBSCRIBERS; i++) {
		if (g_subscribers[event][i].active) {
			g_subscribers[event][i].callback(event, payload, len,
			                                 g_subscribers[event][i].user_ctx);
		}
	}
}

static void rdx_event_publish_async_cb(void *priv)
{
	struct rdx_event_async_msg *msg = (struct rdx_event_async_msg *)priv;
	if (msg->busy) {
		rdx_event_publish(msg->event, msg->payload, msg->len);
		CPU_CRITICAL_ENTER();
		msg->busy = 0;
		CPU_CRITICAL_EXIT();
	}
}

int rdx_event_publish_async(rdx_event_id_t event, void *payload, u32 len)
{
	int arg[2];
	int slot;
	u32 i;

	if (event >= RDX_EVENT_MAX) {
		return RDX_ERR_INVAL;
	}

	CPU_CRITICAL_ENTER();
	slot = -1;
	for (i = 0; i < RDX_EVENT_ASYNC_POOL_SIZE; i++) {
		if (!g_async_pool[i].busy) {
			g_async_pool[i].event   = event;
			g_async_pool[i].len     = len;
			if (payload && len > 0) {
				if (len > RDX_EVENT_ASYNC_PAYLOAD_MAX) {
					CPU_CRITICAL_EXIT();
					return RDX_ERR_INVAL;
				}
				memcpy(g_async_pool[i].payload_buf, payload, len);
				g_async_pool[i].payload = g_async_pool[i].payload_buf;
			} else {
				g_async_pool[i].payload = NULL;
			}
			g_async_pool[i].busy    = 1;
			slot = (int)i;
			break;
		}
	}
	CPU_CRITICAL_EXIT();

	if (slot < 0) {
		RDX_LOGW("event_bus async pool full event=%d", event);
		return RDX_ERR_NOMEM;
	}

	arg[0] = (int)rdx_event_publish_async_cb;
	arg[1] = (int)&g_async_pool[slot];

	if (os_taskq_post_type("app_core", Q_CALLBACK, 2, arg) != 0) {
		CPU_CRITICAL_ENTER();
		g_async_pool[slot].busy = 0;
		CPU_CRITICAL_EXIT();
		return RDX_ERR_BUSY;
	}
	return RDX_OK;
}
