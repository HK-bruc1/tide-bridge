#ifndef RDX_CACHE_FLUSH_H
#define RDX_CACHE_FLUSH_H

#include "generic/typedef.h"

/* 生命周期由 app_core 的 UXFILE 初始化/退出入口管理。 */
void rdx_cache_flush_start(void);
void rdx_cache_flush_stop(void);
/* 库内每次修改 dirty 后发布通知；不在调用者上下文保存或恢复 PA4。 */
void rdx_cache_flush_changed(void);
u8 rdx_cache_flush_needs_service(void);
void rdx_cache_flush_service(void);

/* 哈希固定的库适配：0 干净，1 可保存，2 暂忙，负数关闭或故障。 */
int rdx_uxfile_cache_flush_state(void);
int rdx_uxfile_cache_flush_once(void);

#endif
