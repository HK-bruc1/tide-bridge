#ifndef _RDX_CODEX_MICRO_H_
#define _RDX_CODEX_MICRO_H_

#include "system/includes.h"
#include "btstack/btstack_typedef.h"

void rdx_codex_micro_init(void);
void rdx_codex_micro_deinit(void);
void rdx_codex_micro_runtime_reset(void);
void rdx_codex_micro_ready_drop_cleanup(void);
u16 rdx_codex_micro_att_read(hci_con_handle_t connection_handle,
                             u16 att_handle, u16 offset,
                             u8 *buffer, u16 buffer_size);
int rdx_codex_micro_output_write(hci_con_handle_t connection_handle,
                                 u16 offset, const u8 *buffer,
                                 u16 buffer_size);
void rdx_codex_micro_on_can_send_now(void);
int rdx_codex_micro_fast_key_click(void);
void rdx_codex_micro_fast_key_release_all(void);

#endif
