#ifndef RDX_CDC_SOURCE_H
#define RDX_CDC_SOURCE_H
#include "app_config.h"
#include "typedef.h"
#if TCFG_T2620_CDC_FILE_ENABLE
int rdx_cdc_source_begin(u32 sn, const char *name);
int rdx_cdc_source_list_begin(void);
int rdx_cdc_source_busy(void);
void rdx_cdc_source_cancel(void);
void rdx_cdc_source_release(void);
void rdx_cdc_source_service(void);
int rdx_cdc_source_take(u8 *data, u32 *length, u32 *size);
int rdx_cdc_source_closed(void);
int rdx_cdc_source_shutdown(void);
#else
static inline int rdx_cdc_source_shutdown(void) { return 0; }
#endif
#endif
