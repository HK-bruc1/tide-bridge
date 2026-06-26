#ifndef __RDX_STORAGE_SERVICE_H__
#define __RDX_STORAGE_SERVICE_H__

#include "typedef.h"

void rdx_storage_service_init(void);
int  rdx_storage_format_request(void);
int  rdx_storage_sync_start(void);
int  rdx_storage_sync_stop(void);
u8   rdx_storage_is_syncing(void);

#endif
