#include "rdx_jl_storage.h"
#include "app_config.h"
#include "system/includes.h"
#include "syscfg_id.h"

rdx_err_t rdx_storage_read(rdx_vm_id_t id, u8 *buf, u16 len)
{
    int ret = syscfg_read((int)id, buf, len);
    if (ret == 0) {
        return RDX_ERR_NOENT;
    }
    return (ret == len) ? RDX_OK : RDX_ERR_IO;
}

rdx_err_t rdx_storage_write(rdx_vm_id_t id, const u8 *buf, u16 len)
{
    int ret = syscfg_write((int)id, buf, len);
    return (ret == len) ? RDX_OK : RDX_ERR_IO;
}

rdx_err_t rdx_storage_cfg_read_string(u16 id, void *buf, u16 len, u8 ver)
{
    int ret = syscfg_read_string(id, buf, len, ver);
    if (ret < 0) {
        return RDX_ERR_IO;
    }
    if (ret == 0) {
        return RDX_ERR_NOENT;
    }
    return RDX_OK;
}

rdx_err_t rdx_storage_cfg_write(u16 id, const void *buf, u16 len)
{
    int ret = syscfg_write(id, buf, len);
    return (ret >= 0) ? RDX_OK : RDX_ERR_IO;
}
