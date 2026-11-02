#ifndef RDX_FILE_DOWNLOAD_H
#define RDX_FILE_DOWNLOAD_H

#include "typedef.h"
#include "rdx_uxfile.h"

/* Fixed-archive hooks. Generic finish/cancel is never deletion authority. */
void rdx_file_download_abort(void);
u32 rdx_file_download_generation(void);
void rdx_file_download_sent(u32 generation, const ReqFileInfo *request,
                            const uxfile_data_t *file, u32 offset,
                            u32 length, int read_result, int send_result);
u32 rdx_file_download_chunk(const ReqFileInfo *request,
                            const uxfile_data_t *file);
void rdx_file_download_delete_worker(void *context);

#endif
