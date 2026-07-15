#ifndef __SOURCE_DEV0_H__
#define __SOURCE_DEV0_H__

#include "typedef.h"
#include <stdbool.h>

u32 source_dev0_input_write(u8 *data, u16 len);
u32 source_dev0_get_free_space(void);
bool source_dev0_is_empty(void);
u32 source_dev0_get_consumed_bytes(void);
void source_dev0_reset_consumed_bytes(void);

#endif
