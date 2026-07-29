#ifndef __P11_STORAGE_DOMAIN_RDX_UXFILE_H__
#define __P11_STORAGE_DOMAIN_RDX_UXFILE_H__

#include "typedef.h"

typedef struct {
    u32 start_time;
} uxfile_data_t;

uxfile_data_t *rdx_uxfile_get_operateFile_info(void);

#endif
