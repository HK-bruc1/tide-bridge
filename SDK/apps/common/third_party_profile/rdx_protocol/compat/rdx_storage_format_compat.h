#ifndef __RDX_STORAGE_FORMAT_COMPAT_H__
#define __RDX_STORAGE_FORMAT_COMPAT_H__

#include "typedef.h"
#include "rdx_err.h"

typedef void (*rdx_storage_legacy_format_cb_t)(u8 legacy_result);

rdx_err_t rdx_storage_format_compat_for_dut(
	rdx_storage_legacy_format_cb_t cb);
rdx_err_t rdx_storage_format_compat_for_unbind(
	rdx_storage_legacy_format_cb_t cb);

#endif
