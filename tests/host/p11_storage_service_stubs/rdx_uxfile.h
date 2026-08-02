#ifndef __P11_STORAGE_SERVICE_RDX_UXFILE_H__
#define __P11_STORAGE_SERVICE_RDX_UXFILE_H__

#include "system/includes.h"

#define MEM_FORMAT_RESULT_OK   0x01
#define MEM_FORMAT_RESULT_FAIL 0x10

typedef void (*uxfile_format_cb)(u8 result);

void rdx_uxfile_init(void);
void rdx_uxfile_device_sd_mem_check(void);
void rdx_uxfile_device_sd_format(uxfile_format_cb callback);
int rdx_uxfile_recordFile_delete_handle(int file_num, char *file_name);
int rdx_uxfile_sd_format(uxfile_format_cb callback);
bool rdx_uxfile_sd_format_status_check(void);
u8 rdx_uxfile_is_sync_in_progress(void);
u8 rdx_uxfile_is_datFileInfo_loading(void);
u8 rdx_uxfile_is_scan_active(void);
u8 rdx_uxfile_is_formatting(void);

#endif
