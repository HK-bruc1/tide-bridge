#ifndef RDX_RECORD_STORAGE_H
#define RDX_RECORD_STORAGE_H
#include "typedef.h"

int rdx_record_storage_create(void);
void rdx_record_storage_destroy(void);
int rdx_record_storage_check_start(void);
/* 0 admitted, -2 reserve reached, -1 storage unknown/faulted/busy. */
int rdx_record_storage_admit(void);
int rdx_record_storage_active(void);
/* Successful explicit format only, after the writer has drained. */
void rdx_record_storage_reset(void);
/* Serialized sink lifecycle: begin only after metadata initialization; end
 * must finish before saving/resetting the file, or releasing storage power. */
int rdx_record_storage_begin(u32 generation, u8 scene, u8 format);
/* 0 queued, 1 normal low-space stop (not accepted), -1 storage/queue failure. */
int rdx_record_storage_push(const u8 *data, u32 len);
int rdx_record_storage_end(void);
#endif
