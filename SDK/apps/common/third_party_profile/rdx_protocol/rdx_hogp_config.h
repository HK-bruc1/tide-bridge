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
* BLE Mode Defaults
******************************************************************************/
#ifndef RDX_BLE_DEFAULT_MODE_CONFIG
#define RDX_BLE_DEFAULT_MODE_CONFIG           0
#endif

#ifndef RDX_BLE_DEFAULT_MODE_HOGP
#define RDX_BLE_DEFAULT_MODE_HOGP             1
#endif

#ifndef RDX_BLE_DEFAULT_MODE
#define RDX_BLE_DEFAULT_MODE                  RDX_BLE_DEFAULT_MODE_CONFIG
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

/* Unified RDX + HOGP advertising is developed behind this gate. It must stay
 * off until the session authorization and lifecycle work is enabled with it. */
#ifndef TCFG_RDX_HOGP_UNIFIED_ENTRY_ENABLE
#define TCFG_RDX_HOGP_UNIFIED_ENTRY_ENABLE    0
#endif

#if TCFG_RDX_HOGP_UNIFIED_ENTRY_ENABLE && !TCFG_RDX_HOGP_ENABLE
#error "Unified RDX/HOGP entry requires TCFG_RDX_HOGP_ENABLE"
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
* Advertising
******************************************************************************/
#ifndef RDX_HOGP_APPEARANCE
#define RDX_HOGP_APPEARANCE                  0x03C1   /* Keyboard */
#endif

#ifndef RDX_HOGP_NAME_SOURCE
#define RDX_HOGP_NAME_SOURCE                 0   /* 0=server local name, 1=custom */
#endif

#ifndef RDX_HOGP_CUSTOM_NAME
#define RDX_HOGP_CUSTOM_NAME                 "VibeKeyboard"
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
