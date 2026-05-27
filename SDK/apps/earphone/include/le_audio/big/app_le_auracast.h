#ifndef _APP_LE_AURACAST_H
#define _APP_LE_AURACAST_H

#include "typedef.h"
#include "btstack/le/auracast_sink_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BROADCAST_STATUS_DEFAULT = 0,
    BROADCAST_STATUS_SCAN_START,
    BROADCAST_STATUS_SCAN_STOP,
    BROADCAST_STATUS_START,
    BROADCAST_STATUS_STOP,
} BROADCAST_STATUS;

/**
 * @brief 获取auracast状态
 */
BROADCAST_STATUS le_auracast_status_get();

/**
 * @brief 关闭auracast功能（音频、扫描）
 */
void le_auracast_stop(void);

/**
 * @brief 手机选中广播设备开始播歌
 *
 * @param param 要监听的广播设备
 */
int app_auracast_sink_big_sync_create(auracast_sink_source_info_t *param);

/**
 * @brief 关闭所有正在监听播歌的广播设备
 */
int app_auracast_sink_big_sync_terminate(void);

/**
 * @brief 手机通知设备开始搜索auracast广播
 */
int app_auracast_sink_scan_start(void);

/**
 * @brief 手机通知设备关闭搜索auracast广播
 */
int app_auracast_sink_scan_stop(void);

#ifdef __cplusplus
};
#endif

#endif

