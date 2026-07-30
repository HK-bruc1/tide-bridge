#include "rdx_file_transfer_cleanup_compat.h"
#include "system/includes.h"
#include "rdx_uxfile.h"

extern void rdx_protocol_uploadFileInfo_clean(void);
extern void rdx_protocol_file_sync_busy_timer_stop(void);
extern void rdx_protocol_send_buffer_reinit(void);

rdx_err_t rdx_file_transfer_compat_cleanup_record_disconnect(void)
{
	rdx_protocol_uploadFileInfo_clean();
	rdx_uxfile_recordFileData_sendBuf_free();
	rdx_protocol_file_sync_busy_timer_stop();
	rdx_protocol_send_buffer_reinit();
	return RDX_OK;
}

rdx_err_t rdx_file_transfer_compat_cleanup_ble_delayed(void)
{
	rdx_protocol_uploadFileInfo_clean();
	rdx_uxfile_recordFileData_sendBuf_free();
	rdx_uxfile_datFileInfo_sendBuf_free();
	rdx_protocol_file_sync_busy_timer_stop();
	rdx_protocol_send_buffer_reinit();
	return RDX_OK;
}
