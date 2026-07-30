#ifndef __RDX_STORAGE_SERVICE_H__
#define __RDX_STORAGE_SERVICE_H__

#include "typedef.h"
#include "rdx_err.h"

void      rdx_storage_service_init(void);
rdx_err_t rdx_storage_service_runtime_init(void);

/* format orchestration (moved from rdx_app.c) */
rdx_err_t rdx_storage_service_format_for_app(void);
void rdx_storage_service_format_handle(void);
void rdx_storage_service_format_cb(u8 result);

/* sync / file management shells */
rdx_err_t rdx_storage_format_request(void);
rdx_err_t rdx_storage_sync_start(void);
rdx_err_t rdx_storage_sync_stop(void);
u8        rdx_storage_is_syncing(void);
u8        rdx_storage_is_formatting(void);

/* Legacy BLE cleanup facades; retained for compatibility. */
rdx_err_t rdx_storage_service_cleanup_ble_immediate(void);
rdx_err_t rdx_storage_service_cleanup_ble_buffers(void);
rdx_err_t rdx_storage_service_adjust_active_record_time(int delta_seconds);

/* board power control — Stage 5 D-class */
rdx_err_t rdx_storage_service_sdmmc_set_power(u8 enable);

#endif
