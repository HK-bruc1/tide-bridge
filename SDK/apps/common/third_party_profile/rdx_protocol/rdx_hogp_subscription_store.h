#ifndef _RDX_HOGP_SUBSCRIPTION_STORE_H_
#define _RDX_HOGP_SUBSCRIPTION_STORE_H_

#include "system/includes.h"

#define RDX_HOGP_SUBSCRIPTION_PEER_ADDR_LEN  6
#define RDX_HOGP_SUBSCRIPTION_KEYBOARD       0x01
#define RDX_HOGP_SUBSCRIPTION_CODEX          0x02
#define RDX_HOGP_SUBSCRIPTION_ALL            0x03

int rdx_hogp_subscription_store_init(void);
u8 rdx_hogp_subscription_store_get(
    const u8 peer_addr[RDX_HOGP_SUBSCRIPTION_PEER_ADDR_LEN]);
int rdx_hogp_subscription_store_update(
    const u8 peer_addr[RDX_HOGP_SUBSCRIPTION_PEER_ADDR_LEN],
    u8 subscription_bit, u8 enabled);
int rdx_hogp_subscription_store_reset(void);

#endif /* _RDX_HOGP_SUBSCRIPTION_STORE_H_ */
