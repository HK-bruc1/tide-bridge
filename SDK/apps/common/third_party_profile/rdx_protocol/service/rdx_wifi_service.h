#ifndef __RDX_WIFI_SERVICE_H__
#define __RDX_WIFI_SERVICE_H__

#include "typedef.h"
#include "rdx_err.h"

#define TRANSFER_BY_WIFI_OFF                        (0)
#define TRANSFER_BY_WIFI_ON                         (1)

typedef struct {
    u8 onoff;
    u8 conn_state;
} RdxWifiInfo;

void      rdx_wifi_service_init(void);
rdx_err_t rdx_wifi_power_on(void);
rdx_err_t rdx_wifi_power_off(void);
void rdx_wifi_data_send(const u8 *data, u32 len);
u8   rdx_wifi_is_connected(void);

RdxWifiInfo *rdx_wifi_service_get_wifi_info(void);
void rdx_wifi_service_reset_state(void);
void rdx_wifi_service_set_state(u8 onoff, u8 conn_state);

/*
 * SPI transport callback handlers.
 * These are registered to rdx_spi via rdx_spi_register_wifi_callbacks()
 * and replace the old direct protocol/uxfile calls in rdx_spi.c.
 */
void rdx_wifi_service_on_rx(const u8 *data, u32 len);
void rdx_wifi_service_on_tx_done(void);

/*
 * Transport state queries — wrap ReqFileInfo* access so rdx_spi.c
 * doesn't reach into protocol state directly.
 */
int  rdx_wifi_service_is_send_stopped(void);
int  rdx_wifi_service_is_file_send_busy(void);
void rdx_wifi_service_retry_on_stuck(void);

#endif
