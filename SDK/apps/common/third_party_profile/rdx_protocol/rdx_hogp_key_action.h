/*=====================================================================================
 HEADER NAME: rdx_hogp_key_action.h
 MODULE NAME: RDX HOGP key action executor public contract.

 GENERAL DESCRIPTION:
    Public types for the HOGP active-keymap executor. It intentionally does not include
    physical-key or configuration headers so that future BLE App/VM configuration layers
    can reuse the contract without depending on the legacy RDX key tables.

=======================================================================================*/

#ifndef _RDX_HOGP_KEY_ACTION_H_
#define _RDX_HOGP_KEY_ACTION_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "typedef.h"   /* for u8 */

/******************************************************************************
* Macro Define Section
******************************************************************************/
#define RDX_HOGP_KEY_ACTION_PHYSICAL_KEY_COUNT       5

/******************************************************************************
* Structure and Enum Section
******************************************************************************/
/* Single keyboard action: standard 8-byte Keyboard Report body (no Report ID). */
typedef struct {
    u8 modifiers;       /* HID keyboard modifier byte (bit0=LCtrl, bit1=LShift, ...) */
    u8 usages[6];       /* Up to 6 simultaneous HID usages */
} rdx_hogp_key_action_keyboard_t;

/* Runtime active keymap held privately by the executor. Not a Flash ABI. */
typedef struct {
    u8 version;         /* RAM structure sanity check only */
    u8 key_count;       /* Number of valid entries (<= RDX_HOGP_KEY_ACTION_PHYSICAL_KEY_COUNT) */
    rdx_hogp_key_action_keyboard_t keys[RDX_HOGP_KEY_ACTION_PHYSICAL_KEY_COUNT];
} rdx_hogp_key_action_keymap_t;

/******************************************************************************
* Function Section
******************************************************************************/
void rdx_hogp_key_action_init(void);
void rdx_hogp_key_action_reset(void);
void rdx_hogp_key_action_deinit(void);
int  rdx_hogp_key_action_keymap_apply(const rdx_hogp_key_action_keymap_t *keymap);
int  rdx_hogp_key_action_click(u8 key_id);

#ifdef __cplusplus
}
#endif

#endif /* _RDX_HOGP_KEY_ACTION_H_ */
