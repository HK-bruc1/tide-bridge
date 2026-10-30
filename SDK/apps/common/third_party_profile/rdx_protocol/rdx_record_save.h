#ifndef RDX_RECORD_SAVE_H
#define RDX_RECORD_SAVE_H

#include "typedef.h"

#define RDX_RECORD_SAVE_CUSTOM_CMD "recsave"

void rdx_record_save_init(void);
/* 1 = save meeting files (default); 0 = online streaming without a file. */
u8 rdx_record_save_enabled(void);
void rdx_record_save_handle_custom(const char *value);
/* app_core only; restore the default ON during full factory reset. */
int rdx_record_save_factory_reset(void);

#endif
