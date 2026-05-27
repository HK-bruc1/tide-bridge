#ifndef TWS_DUAL_CONN_H
#define TWS_DUAL_CONN_H

/**
 * @brief 设置inquiry scan和page scan
 *
 * @param scan_enable inquiry_scan_en
 * @param conn_enable page_scan_en
 */
void write_scan_conn_enable(bool scan_enable, bool conn_enable);

/**
 * @brief 根据tws、手机连接状态和连接记录设置可发现可连接
 */
void tws_dual_conn_state_handler();

/**
 * @brief tws不允许进入sniff
 */
void tws_sniff_controle_check_disable(void);

/**
 * @brief 同步可发现可连接及配对信息到对耳
 */
void send_page_device_addr_2_sibling();

/**
 * @brief 清除回连配对列表的设备信息
 */
void clr_device_in_page_list();

#endif
