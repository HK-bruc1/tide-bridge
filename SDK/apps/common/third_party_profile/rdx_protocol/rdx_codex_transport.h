#ifndef _RDX_CODEX_TRANSPORT_H_
#define _RDX_CODEX_TRANSPORT_H_

#include "system/includes.h"
#include "btstack/btstack_typedef.h"

void rdx_codex_transport_init(void);
void rdx_codex_transport_deinit(void);
void rdx_codex_transport_runtime_reset(void);
u16 rdx_codex_transport_att_read(hci_con_handle_t connection_handle,
                                 u16 att_handle, u16 offset,
                                 u8 *buffer, u16 buffer_size);
int rdx_codex_transport_output_write(hci_con_handle_t connection_handle,
                                     u16 offset, const u8 *buffer,
                                     u16 buffer_size);
int rdx_codex_transport_send_json(const char *json);
void rdx_codex_transport_on_can_send_now(void);

#endif
