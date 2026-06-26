#ifndef __RDX_LOG_H__
#define __RDX_LOG_H__

#include "app_config.h"
#include "utils/debug.h"

#define RDX_LOG_TAG "RDX"

#define RDX_LOG_LEVEL_NONE  0
#define RDX_LOG_LEVEL_ERR   1
#define RDX_LOG_LEVEL_WARN  2
#define RDX_LOG_LEVEL_INFO  3
#define RDX_LOG_LEVEL_DBG   4

#ifndef RDX_LOG_LEVEL
#define RDX_LOG_LEVEL RDX_LOG_LEVEL_INFO
#endif

#if (RDX_LOG_LEVEL >= RDX_LOG_LEVEL_ERR)
#define RDX_LOGE(fmt, ...)  log_error("[RDX_E] " fmt "\r", ##__VA_ARGS__)
#else
#define RDX_LOGE(fmt, ...)  ((void)0)
#endif

#if (RDX_LOG_LEVEL >= RDX_LOG_LEVEL_WARN)
#define RDX_LOGW(fmt, ...)  log_warn("[RDX_W] " fmt "\r", ##__VA_ARGS__)
#else
#define RDX_LOGW(fmt, ...)  ((void)0)
#endif

#if (RDX_LOG_LEVEL >= RDX_LOG_LEVEL_INFO)
#define RDX_LOGI(fmt, ...)  log_info("[RDX_I] " fmt "\r", ##__VA_ARGS__)
#else
#define RDX_LOGI(fmt, ...)  ((void)0)
#endif

#if (RDX_LOG_LEVEL >= RDX_LOG_LEVEL_DBG)
#define RDX_LOGD(fmt, ...)  log_debug("[RDX_D] " fmt "\r", ##__VA_ARGS__)
#else
#define RDX_LOGD(fmt, ...)  ((void)0)
#endif

#endif
