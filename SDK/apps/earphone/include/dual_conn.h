#ifndef DUAL_CONN_H
#define DUAL_CONN_H

/**
 * @brief 设置inquiry scan和page scan
 *
 * @param scan_enable inquiry_scan_en
 * @param conn_enable page_scan_en
 */
void write_scan_conn_enable(bool scan_enable, bool conn_enable);

/**
 * @brief 根据手机连接状态和连接记录设置可发现可连接
 */
void dual_conn_state_handler();

/**
 * @brief 清除回连配对列表的设备信息
 */
void clr_device_in_page_list();

#endif
