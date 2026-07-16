/*=====================================================================================
 HEADER NAME: rdx_hogp_keymap_config.h
 MODULE NAME: RDX HOGP APP keymap configuration protocol.
=======================================================================================*/

#ifndef _RDX_HOGP_KEYMAP_CONFIG_H_
#define _RDX_HOGP_KEYMAP_CONFIG_H_

#include "system/includes.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RDX_HOGP_KEYMAP_CUSTOM_CMD          "hogpkm"

void rdx_hogp_keymap_config_init(void);
void rdx_hogp_keymap_config_handle_custom(const char *value);
void rdx_hogp_keymap_config_on_disconnect(void);

#ifdef __cplusplus
}
#endif

#endif /* _RDX_HOGP_KEYMAP_CONFIG_H_ */
