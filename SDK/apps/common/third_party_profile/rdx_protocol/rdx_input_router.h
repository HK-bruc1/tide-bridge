/* RDX physical-input router and canonical typed action map. */

#ifndef _RDX_INPUT_ROUTER_H_
#define _RDX_INPUT_ROUTER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "typedef.h"

#define RDX_INPUT_ROUTER_PHYSICAL_KEY_COUNT       5
#define RDX_INPUT_ACTION_MAP_VERSION               2
#define RDX_INPUT_ACTION_DATA_LEN                  7

#define RDX_INPUT_ACTION_NONE                      0x00
#define RDX_INPUT_ACTION_KEYBOARD                  0x01
#define RDX_INPUT_ACTION_CODEX_AGENT               0x02

#define RDX_INPUT_ROUTER_OK                        0
#define RDX_INPUT_ROUTER_NOT_SENT                  1
#define RDX_INPUT_ROUTER_INVALID                  -1
#define RDX_INPUT_ROUTER_TEST_MODE                -2

/* Fixed 8-byte canonical entry. This is a RAM contract, not a VM ABI yet. */
typedef struct {
    u8 kind;
    u8 data[RDX_INPUT_ACTION_DATA_LEN];
} rdx_input_action_entry_t;

typedef struct {
    u8 version;
    u8 key_count;
    rdx_input_action_entry_t entries[RDX_INPUT_ROUTER_PHYSICAL_KEY_COUNT];
} rdx_input_action_map_t;

void rdx_input_router_init(void);
void rdx_input_router_reset(void);
void rdx_input_router_deinit(void);
void rdx_input_router_keyboard_ready_drop_cleanup(void);
int rdx_input_router_action_map_apply(const rdx_input_action_map_t *action_map);
int rdx_input_router_click(u8 physical_key_id);
u8 rdx_input_router_test_mode_active(void);

#ifdef __cplusplus
}
#endif

#endif /* _RDX_INPUT_ROUTER_H_ */
