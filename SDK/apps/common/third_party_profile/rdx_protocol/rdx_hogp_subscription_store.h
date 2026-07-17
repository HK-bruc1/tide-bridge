#ifndef _RDX_HOGP_SUBSCRIPTION_STORE_H_
#define _RDX_HOGP_SUBSCRIPTION_STORE_H_

#include "system/includes.h"

#define RDX_HOGP_SUBSCRIPTION_PEER_ADDR_LEN  6

int rdx_hogp_subscription_store_contains(
    const u8 peer_addr[RDX_HOGP_SUBSCRIPTION_PEER_ADDR_LEN]);
int rdx_hogp_subscription_store_set(
    const u8 peer_addr[RDX_HOGP_SUBSCRIPTION_PEER_ADDR_LEN],
    u8 enabled);
int rdx_hogp_subscription_store_reset(void);

#endif /* _RDX_HOGP_SUBSCRIPTION_STORE_H_ */
