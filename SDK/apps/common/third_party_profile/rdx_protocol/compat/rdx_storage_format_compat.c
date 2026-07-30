#include "rdx_storage_format_compat.h"
#include "rdx_uxfile.h"

rdx_err_t rdx_storage_format_compat_for_dut(
	rdx_storage_legacy_format_cb_t cb)
{
	rdx_uxfile_device_sd_format(cb);
	return RDX_OK;
}

rdx_err_t rdx_storage_format_compat_for_unbind(
	rdx_storage_legacy_format_cb_t cb)
{
	return rdx_uxfile_sd_format(cb) == 0 ? RDX_OK : RDX_ERR_IO;
}
