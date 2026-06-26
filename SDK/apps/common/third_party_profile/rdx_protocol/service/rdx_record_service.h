#ifndef __RDX_RECORD_SERVICE_H__
#define __RDX_RECORD_SERVICE_H__

#include "typedef.h"

void rdx_record_service_init(void);
void rdx_record_service_exit(void);
void rdx_record_service_start(u8 mode);
void rdx_record_service_stop(void);
u8   rdx_record_service_get_state(void);
u8   rdx_record_service_is_active(void);

#endif
