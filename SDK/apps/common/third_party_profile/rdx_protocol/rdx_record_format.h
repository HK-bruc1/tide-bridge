#ifndef RDX_RECORD_FORMAT_H
#define RDX_RECORD_FORMAT_H

#include "typedef.h"
#include "rdx_uxfile.h"

/* 启动时由 UXFILE 工作任务执行索引恢复，完成后再交接 DAT 缓存。 */
int rdx_record_format_boot(void);
int rdx_record_format_status(void);
/* 文件工作任务负责恢复与缓存交接：1 准备中，0 就绪，-1 失败。 */
int rdx_record_format_service_status(void);
void rdx_record_format_service_ready(int result);
/* 参数或身份拒绝与存储失败分开，拒绝请求不应关闭健康的文件服务。 */
#define RDX_RECORD_DELETE_REJECTED (-2)
int rdx_record_format_delete(u32 sn, const char *name);
uxfile_datfile_info_t *rdx_record_format_list(uxfile_datfile_info_t *info);
/* Sticky storage failure, also used by the asynchronous recording writer. */
void rdx_record_format_storage_fault(void);
/* Called after new-file generation, never on resume. */
int rdx_record_format_begin(const uxfile_data_t *file, u8 format);
/* Pin the first encoded frame before any RAW bytes are written. */
int rdx_record_format_frame(const uxfile_data_t *file, u8 format,
                            const u8 *data, u32 len);
#endif
