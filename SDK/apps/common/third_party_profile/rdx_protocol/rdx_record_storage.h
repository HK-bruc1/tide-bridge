#ifndef RDX_RECORD_STORAGE_H
#define RDX_RECORD_STORAGE_H
#include "typedef.h"

int rdx_record_storage_create(void);
void rdx_record_storage_destroy(void);
/* Serialized sink lifecycle: begin only after metadata initialization; end
 * must finish before saving/resetting the file, or releasing storage power. */
int rdx_record_storage_begin(u32 generation, u8 scene, u8 format);
int rdx_record_storage_push(const u8 *data, u32 len);
int rdx_record_storage_end(void);
#endif
