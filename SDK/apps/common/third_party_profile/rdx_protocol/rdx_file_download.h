#ifndef RDX_FILE_DOWNLOAD_H
#define RDX_FILE_DOWNLOAD_H

#include "typedef.h"
#include "rdx_uxfile.h"

/* Fixed-archive hooks. Generic finish/cancel is never deletion authority. */
void rdx_file_download_abort(void);
u32 rdx_file_download_generation(void);
/* Per-block sync timeout used by the patched archive timer. */
u32 rdx_file_download_sync_timeout_ms(void);
void rdx_file_download_interrupt_notify(void);
u8 rdx_file_download_take_interrupt(void);
/* app_core: a local recording admission stops an active BLE opusfile transfer
 * and answers *DEV#filecmd#0#. Returns nonzero only if the stop was not queued. */
int rdx_file_download_preempt(const char *who);
void rdx_file_download_sent(u32 generation, const ReqFileInfo *request,
                            const uxfile_data_t *file, u32 offset,
                            u32 length, int read_result, int send_result);
u32 rdx_file_download_chunk(const ReqFileInfo *request,
                            const uxfile_data_t *file);
void rdx_file_download_delete_worker(void *context);

#endif
