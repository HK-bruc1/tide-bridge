/*=====================================================================================
 HEADER NAME: rdx_hogp_config.h
 MODULE NAME: RDX BLE HID-over-GATT keyboard compile-time configuration.

 GENERAL DESCRIPTION:
    Centralized tunables for the HOGP keyboard module. This header includes
    app_config.h before applying fallback defaults, so project-level overrides
    from t2620_project_config.h are visible regardless of include order.

=======================================================================================*/

#ifndef _RDX_HOGP_CONFIG_H_
#define _RDX_HOGP_CONFIG_H_

#include "app_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
* Master Enable
*
* TCFG_RDX_HOGP_ENABLE is the project-level kill switch. Define it in
* t2620_project_config.h. Defaults to 0 (disabled) if not defined, so
* non-HOGP products never pay code size.
*
* Override in t2620_project_config.h or board-specific config:
*   #define TCFG_RDX_HOGP_ENABLE 1

******************************************************************************/
#ifndef TCFG_RDX_HOGP_ENABLE
#define TCFG_RDX_HOGP_ENABLE                  0
#endif

#ifndef TCFG_RDX_HOGP_DUAL_LINK_ENABLE
#define TCFG_RDX_HOGP_DUAL_LINK_ENABLE        0
#endif

/******************************************************************************
* Key Action
******************************************************************************/
#ifndef TCFG_RDX_HOGP_KEY_UP_DELAY_MS
#define TCFG_RDX_HOGP_KEY_UP_DELAY_MS         20
#endif

/******************************************************************************
* Security
******************************************************************************/
#ifndef RDX_HOGP_ENCRYPTION_REQUIRED
#define RDX_HOGP_ENCRYPTION_REQUIRED          1
#endif

#ifndef RDX_HOGP_PAIRING_MODE
#define RDX_HOGP_PAIRING_MODE                 0   /* 0=Just Works, 1/2 reserved */
#endif

#if RDX_HOGP_PAIRING_MODE != 0
#error "RDX_HOGP_PAIRING_MODE: only mode 0 (Just Works) is implemented in Phase 3"
#endif

/******************************************************************************
* Logging
******************************************************************************/
#ifndef RDX_HOGP_LOG_ENABLE
#define RDX_HOGP_LOG_ENABLE                   1   /* 0 = strip all HOGP logs */
#endif

#ifndef RDX_HOGP_VERBOSE_LOG
#define RDX_HOGP_VERBOSE_LOG                  0   /* 0 = only state changes and errors; 1 = per-packet read/write */
#endif

#ifndef RDX_HOGPKM_TRACE_ENABLE
#define RDX_HOGPKM_TRACE_ENABLE               0   /* 1 = verbose APP keymap transaction trace */
#endif

#ifdef __cplusplus
}
#endif

#endif /* _RDX_HOGP_CONFIG_H_ */
