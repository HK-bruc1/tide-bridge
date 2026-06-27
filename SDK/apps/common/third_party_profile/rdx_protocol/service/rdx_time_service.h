#ifndef __RDX_TIME_SERVICE_H__
#define __RDX_TIME_SERVICE_H__

#include "typedef.h"

typedef struct {
	u32 timestamp;
	int delta;
} rdx_time_sync_event_t;

void rdx_time_service_init(void);

#endif
