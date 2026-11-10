#ifndef _RDX_HOGP_SUBSCRIPTION_STORE_H_
#define _RDX_HOGP_SUBSCRIPTION_STORE_H_

#include "system/includes.h"

#define RDX_HOGP_SUBSCRIPTION_PEER_ADDR_LEN  6

/* 订阅记录与 BLE SM 绑定同生命周期：只由 CCC 写入保存或删除，
 * 恢复出厂/解绑不清理（PC 回连时不会重写 Input CCC）。 */
int rdx_hogp_subscription_store_contains(
    const u8 peer_addr[RDX_HOGP_SUBSCRIPTION_PEER_ADDR_LEN]);
int rdx_hogp_subscription_store_set(
    const u8 peer_addr[RDX_HOGP_SUBSCRIPTION_PEER_ADDR_LEN],
    u8 enabled);

#endif /* _RDX_HOGP_SUBSCRIPTION_STORE_H_ */
