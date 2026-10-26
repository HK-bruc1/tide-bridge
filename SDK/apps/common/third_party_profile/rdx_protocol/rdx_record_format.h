#ifndef RDX_RECORD_FORMAT_H
#define RDX_RECORD_FORMAT_H

#include "typedef.h"
#include "rdx_uxfile.h"

/* 启动时由 UXFILE 工作任务执行索引恢复，完成后再交接 DAT 缓存。 */
int rdx_record_format_boot(void);
int rdx_record_format_status(void);
/* Audio budget after reserving two index copies and metadata/close headroom.
 * JL reports free space in KiB; an unknown query is never unlimited space. */
int rdx_record_format_space(u32 *audio_bytes);
u32 rdx_record_format_capacity(void);
int rdx_record_format_erase_enter(void);
void rdx_record_format_erased(void);
void rdx_record_format_erase_leave(int result);
/* 文件工作任务负责恢复与缓存交接：1 准备中，0 就绪，-1 失败。 */
int rdx_record_format_service_status(void);
/* Read-only safety query excluding only CDC's read lease, not storage faults. */
int rdx_record_format_stream_status(void);
int rdx_record_format_io_enter(void);
/* 包含真实 IO 与短暂解析占用；仅真实 IO 更新空闲活动并恢复 PA4。 */
u32 rdx_record_format_io_users(void);
void rdx_record_format_command_enter(void);
void rdx_record_format_command_leave(void);
int rdx_record_format_flush_enter(void);
void rdx_record_format_io_leave(void);
int rdx_record_format_download_acquire(void);
void rdx_record_format_download_release(void);
int rdx_record_format_download_busy(void);
int rdx_record_format_download_conflict(const char *operation);
int rdx_record_format_protocol_busy(void);
int rdx_record_format_download_open(u32 sn, const char *name, void **handle, u32 *size);
int rdx_record_format_download_list_open(void **handle, u32 *size);
void rdx_record_format_service_ready(int result);
/* 参数或身份拒绝与存储失败分开，拒绝请求不应关闭健康的文件服务。 */
#define RDX_RECORD_DELETE_REJECTED (-2)
int rdx_record_format_delete(u32 sn, const char *name);
/* Archive DAT mutex held. Reserve/persist a normal SN before publishing it;
 * recovery and legacy high IDs stay readable but never advance this counter. */
int rdx_record_format_reserve_sn(u32 *sn);
/* Closed, current-session RAW only; durable paired RAW/MTA discard. */
int rdx_record_format_discard_short(const uxfile_data_t *file);
uxfile_datfile_info_t *rdx_record_format_list(uxfile_datfile_info_t *info);
/* Sticky storage failure, also used by the asynchronous recording writer. */
void rdx_record_format_storage_fault(void);
/* Called after new-file generation, never on resume. */
int rdx_record_format_begin(const uxfile_data_t *file, u8 format);
/* 存储任务排空后调用：1 已取消身份匹配且从未写入的会话，0 需正常保存，
 * -1 存储任务仍活动或已有故障。不会删除文件或清除 IO 故障。 */
int rdx_record_format_cancel_empty(const uxfile_data_t *file);
/* Pin the first encoded frame before any RAW bytes are written. */
int rdx_record_format_frame(const uxfile_data_t *file, u8 format,
                            const u8 *data, u32 len);
/* Factory reader: called only by CDC file worker while holding file-service
 * admission. Validates MTA identity and first frame, returns a private FILE*. */
void *rdx_record_format_factory_open(u32 sn, const char *name, u32 start, u32 *size, u32 *crc);
#endif
