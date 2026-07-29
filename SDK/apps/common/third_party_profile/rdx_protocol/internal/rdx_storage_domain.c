#include "rdx_storage_domain.h"
#include "system/includes.h"
#include "rdx_uxfile.h"

rdx_err_t rdx_storage_domain_adjust_active_record_time(int delta_seconds)
{
	uxfile_data_t *op = rdx_uxfile_get_operateFile_info();

	if (op && op->start_time > 0) {
		u32 corrected = (u32)((int)op->start_time + delta_seconds);
		y_printf("[RTC_SYNC] Recording active, fix start_time: %u -> %u (delta=%d)\r",
		         op->start_time, corrected, delta_seconds);
		op->start_time = corrected;
	}
	return RDX_OK;
}
