#ifndef __RDX_COMMAND_DISPATCH_H__
#define __RDX_COMMAND_DISPATCH_H__

#include "rdx_protocol.h"

typedef void (*rdx_cmd_handler_t)(ProtocolEvents event, void *data, u32 len);

void rdx_cmd_dispatch_init(void);
void rdx_cmd_register(ProtocolEvents event, rdx_cmd_handler_t handler);
void rdx_cmd_dispatch(ProtocolEvents event, void *data, u32 len);

#endif
