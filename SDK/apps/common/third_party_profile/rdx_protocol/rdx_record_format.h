#ifndef RDX_RECORD_FORMAT_H
#define RDX_RECORD_FORMAT_H

#include "typedef.h"
#include "rdx_uxfile.h"

/* Boot-only reconciliation, before the archive creates its task/timers/cache. */
int rdx_record_format_boot(void);
int rdx_record_format_status(void);
/* Called after new-file generation, never on resume. */
int rdx_record_format_begin(const uxfile_data_t *file, u8 format);
/* Pin the first encoded frame before any RAW bytes are written. */
int rdx_record_format_frame(const uxfile_data_t *file, u8 format,
                            const u8 *data, u32 len);
#endif
