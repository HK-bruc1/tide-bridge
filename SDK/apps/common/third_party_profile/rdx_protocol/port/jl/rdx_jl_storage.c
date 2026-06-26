#include "port/jl/rdx_jl_storage.h"
#include "app_config.h"
#include "system/includes.h"
#include "syscfg_id.h"

rdx_err_t rdx_storage_read(rdx_vm_id_t id, u8 *buf, u16 len)
{
    int ret = syscfg_read((int)id, buf, len);
    if (ret < 0) {
        return RDX_ERR_IO;
    }
    if (ret == 0) {
        return RDX_ERR_NOENT;
    }
    return RDX_OK;
}

rdx_err_t rdx_storage_write(rdx_vm_id_t id, const u8 *buf, u16 len)
{
    int ret = syscfg_write((int)id, buf, len);
    return (ret >= 0) ? RDX_OK : RDX_ERR_IO;
}
