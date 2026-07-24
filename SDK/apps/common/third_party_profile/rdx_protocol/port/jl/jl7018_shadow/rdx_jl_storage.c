#include "rdx_jl_storage.h"
#include "app_config.h"
#include "system/includes.h"
#include "syscfg_id.h"

/* jl7018_shadow: use a helper trampoline to simulate indirect VM access */
static int shadow_syscfg_read(int id, u8 *buf, u16 len)
{
    return syscfg_read(id, buf, len);
}

static int shadow_syscfg_write(int id, const void *buf, u16 len)
{
    return syscfg_write(id, buf, len);
}

static int shadow_syscfg_read_string(int id, void *buf, u16 len, u8 ver)
{
    return syscfg_read_string(id, buf, len, ver);
}

static int rdx_storage_key_to_syscfg_id(rdx_storage_key_t key)
{
    switch (key) {
    case RDX_STORAGE_KEY_BOUND_STATUS:
        return VM_RDX_NOTTA_BOUND_STATUS;
    case RDX_STORAGE_KEY_CUSTOM_AUTH:
        return VM_RDX_CUSTOM_AUTH;
    case RDX_STORAGE_KEY_RTC_INIT_VALUE:
        return VM_RDX_RTC_INIT_VALUE;
    case RDX_STORAGE_KEY_DUT_DISABLED:
        return VM_RDX_KEY_DUT_DISABLED;
    case RDX_STORAGE_KEY_MIC_GAIN:
        return VM_RDX_MIC_GAIN;
    case RDX_STORAGE_KEY_REC_ERR_REBOOT:
        return VM_RDX_REC_ERR_REBOOT;
    case RDX_STORAGE_KEY_BLE_NAME:
        return VM_RDX_BLE_NAME;
    case RDX_STORAGE_KEY_BLE_MAC:
        return VM_RDX_BLE_MAC;
    default:
        return -1;
    }
}

rdx_err_t rdx_storage_read(rdx_storage_key_t key, u8 *buf, u16 len)
{
    int id;
    int ret;

    if (!buf || len == 0) {
        return RDX_ERR_INVAL;
    }
    id = rdx_storage_key_to_syscfg_id(key);
    if (id < 0) {
        return RDX_ERR_NOTSUP;
    }

    ret = shadow_syscfg_read(id, buf, len);
    if (ret == 0) {
        return RDX_ERR_NOENT;
    }
    return (ret == len) ? RDX_OK : RDX_ERR_IO;
}

rdx_err_t rdx_storage_write(rdx_storage_key_t key, const u8 *buf, u16 len)
{
    int id;
    int ret;

    if (!buf || len == 0) {
        return RDX_ERR_INVAL;
    }
    id = rdx_storage_key_to_syscfg_id(key);
    if (id < 0) {
        return RDX_ERR_NOTSUP;
    }

    ret = shadow_syscfg_write(id, buf, len);
    return (ret == len) ? RDX_OK : RDX_ERR_IO;
}

rdx_err_t rdx_storage_read_blob(rdx_storage_key_t key, u8 *buf,
                                u16 capacity, u16 *actual_len)
{
    int id;
    int ret;

    if (!buf || !actual_len || capacity == 0) {
        return RDX_ERR_INVAL;
    }
    *actual_len = 0;
    if (key != RDX_STORAGE_KEY_BLE_NAME) {
        return RDX_ERR_NOTSUP;
    }
    id = rdx_storage_key_to_syscfg_id(key);
    if (id < 0) {
        return RDX_ERR_NOTSUP;
    }

    ret = shadow_syscfg_read(id, buf, capacity);
    if (ret == 0) {
        return RDX_ERR_NOENT;
    }
    if (ret < 0 || ret > capacity) {
        return RDX_ERR_IO;
    }
    *actual_len = (u16)ret;
    return RDX_OK;
}

rdx_err_t rdx_storage_write_blob(rdx_storage_key_t key, const u8 *buf,
                                 u16 len)
{
    int id;
    int ret;

    if (!buf || len == 0) {
        return RDX_ERR_INVAL;
    }
    if (key != RDX_STORAGE_KEY_BLE_NAME) {
        return RDX_ERR_NOTSUP;
    }
    id = rdx_storage_key_to_syscfg_id(key);
    if (id < 0) {
        return RDX_ERR_NOTSUP;
    }

    ret = shadow_syscfg_write(id, buf, len);
    return (ret == len) ? RDX_OK : RDX_ERR_IO;
}

rdx_err_t rdx_storage_read_factory_bt_name(void *buf, u16 len)
{
    int ret;

    if (!buf || len == 0) {
        return RDX_ERR_INVAL;
    }
    ret = shadow_syscfg_read_string(CFG_BT_NAME, buf, len, 0);
    if (ret < 0) {
        return RDX_ERR_IO;
    }
    if (ret == 0) {
        return RDX_ERR_NOENT;
    }
    return RDX_OK;
}

rdx_err_t rdx_storage_write_factory_bt_name(const void *buf, u16 len)
{
    int ret;

    if (!buf || len == 0) {
        return RDX_ERR_INVAL;
    }
    ret = shadow_syscfg_write(CFG_BT_NAME, buf, len);
    return (ret >= 0) ? RDX_OK : RDX_ERR_IO;
}

rdx_err_t rdx_storage_read_factory_bt_mac(u8 mac[6])
{
    int ret;

    if (!mac) {
        return RDX_ERR_INVAL;
    }
    ret = shadow_syscfg_read(CFG_BT_MAC_ADDR, mac, 6);
    if (ret == 0) {
        return RDX_ERR_NOENT;
    }
    return (ret == 6) ? RDX_OK : RDX_ERR_IO;
}
