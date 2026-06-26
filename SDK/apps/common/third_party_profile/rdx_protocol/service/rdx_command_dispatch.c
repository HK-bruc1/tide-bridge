#include "rdx_command_dispatch.h"
#include "system/includes.h"
#include "rdx_log.h"

static rdx_cmd_handler_t g_cmd_table[PROTOCOL_EVENT_CMD_TYPE_MAX];

void rdx_cmd_dispatch_init(void)
{
	u32 i;
	for (i = 0; i < PROTOCOL_EVENT_CMD_TYPE_MAX; i++) {
		g_cmd_table[i] = NULL;
	}
}

void rdx_cmd_register(ProtocolEvents event, rdx_cmd_handler_t handler)
{
	if (event >= PROTOCOL_EVENT_CMD_TYPE_MAX) {
		RDX_LOGW("cmd_dispatch register invalid event=%d", event);
		return;
	}
	g_cmd_table[event] = handler;
}

void rdx_cmd_dispatch(ProtocolEvents event, void *data, u32 len)
{
	if (event >= PROTOCOL_EVENT_CMD_TYPE_MAX) {
		RDX_LOGW("cmd_dispatch invalid event=%d", event);
		return;
	}

	if (g_cmd_table[event]) {
		g_cmd_table[event](event, data, len);
	} else {
		RDX_LOGW("cmd_dispatch unregistered event=%d", event);
	}
}
