#ifndef __RDX_JL_STORAGE_H__
#define __RDX_JL_STORAGE_H__

#include "typedef.h"
#include "rdx_err.h"

typedef enum {
    RDX_STORAGE_KEY_BOUND_STATUS = 0,
    RDX_STORAGE_KEY_CUSTOM_AUTH,
    RDX_STORAGE_KEY_RTC_INIT_VALUE,
    RDX_STORAGE_KEY_DUT_DISABLED,
    RDX_STORAGE_KEY_MIC_GAIN,
    RDX_STORAGE_KEY_REC_ERR_REBOOT,
    RDX_STORAGE_KEY_BLE_NAME,
    RDX_STORAGE_KEY_BLE_MAC,
} rdx_storage_key_t;

/* Fixed-size VM transfer contract: RDX_OK is returned only when the JL
 * syscfg API transfers exactly len bytes.  Zero-length reads map to NOENT;
 * partial reads/writes and negative returns map to RDX_ERR_IO. */
rdx_err_t rdx_storage_read(rdx_storage_key_t key, u8 *buf, u16 len);
rdx_err_t rdx_storage_write(rdx_storage_key_t key, const u8 *buf, u16 len);

/* Legacy variable-length VM blob contract.  Only BLE_NAME is supported;
 * positive reads up to capacity are successful and report actual_len. */
rdx_err_t rdx_storage_read_blob(rdx_storage_key_t key, u8 *buf,
                                u16 capacity, u16 *actual_len);
rdx_err_t rdx_storage_write_blob(rdx_storage_key_t key, const u8 *buf,
                                 u16 len);

/* Semantic accessors for JL-owned factory/system configuration. */
rdx_err_t rdx_storage_read_factory_bt_name(void *buf, u16 len);
rdx_err_t rdx_storage_write_factory_bt_name(const void *buf, u16 len);
rdx_err_t rdx_storage_read_factory_bt_mac(u8 mac[6]);

#endif
