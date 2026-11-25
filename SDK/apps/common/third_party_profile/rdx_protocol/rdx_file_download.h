#ifndef RDX_FILE_DOWNLOAD_H
#define RDX_FILE_DOWNLOAD_H

#include "typedef.h"
#include "rdx_uxfile.h"
#include "rdx_ble_session.h"

/* Fixed-archive hooks. Generic finish/cancel is never deletion authority. */
void rdx_file_download_abort(void);
u32 rdx_file_download_generation(void);
void rdx_file_download_interrupt_notify(void);
u8 rdx_file_download_take_interrupt(void);
/* app_core: a local recording admission stops an active BLE opusfile transfer
 * and answers *DEV#filecmd#0# after the frame in flight and before any later
 * RDX packet (recording status). Returns nonzero only if the stop was not
 * queued. */
int rdx_file_download_preempt(const char *who);
void rdx_file_download_stop_for_recording(void);
/* Patched rdx_protocol_download_preempt prologue/epilogue (protocol_send_task). */
void rdx_file_download_preempt_wait(void);
void rdx_file_download_preempt_done(u32 stopped);
/* rdx_ble_server_send_internal: negative refuses the packet until the owed
 * stop has reached ATT and deferred sends have drained; positive takes a
 * direct (token-bound) send over. Download frames stay refused until cleanup. */
int rdx_file_download_tx_gate(const u8 *data, u32 len,
                              const rdx_ble_async_token_t *token);
/* ATT_EVENT_CAN_SEND_NOW on the RDX link. */
void rdx_file_download_tx_ready(void);
/* Nonzero while a preemption stop or deferred send is outstanding. */
u8 rdx_file_download_tx_held(void);
void rdx_file_download_sent(u32 generation, const ReqFileInfo *request,
                            const uxfile_data_t *file, u32 offset,
                            u32 length, int read_result, int send_result);
u32 rdx_file_download_chunk(const ReqFileInfo *request,
                            const uxfile_data_t *file);
void rdx_file_download_delete_worker(void *context);

#endif
