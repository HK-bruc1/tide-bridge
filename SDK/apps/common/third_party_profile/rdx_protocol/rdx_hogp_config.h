/*=====================================================================================
 HEADER NAME: rdx_hogp_config.h
 MODULE NAME: RDX BLE HID-over-GATT keyboard compile-time configuration.

 GENERAL DESCRIPTION:
    Centralized tunables for the HOGP keyboard module. Every macro uses an
    #ifndef guard so project-level overrides (t2620_project_config.h) take
    precedence. This file is included by rdx_hogp_keyboard.c, rdx_hogp_keyboard.h,
    rdx_hogp_profile.c, and rdx_ble_server.c. This ensures all four files see the
    same fallback defaults.

=======================================================================================*/

#ifndef _RDX_HOGP_CONFIG_H_
#define _RDX_HOGP_CONFIG_H_

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
* IMPORTANT: include app_config.h (which pulls in t2620_project_config.h)
* BEFORE including this header, otherwise this fallback will lock the macro
* to 0 and the project override will be ignored.
*
* Override in t2620_project_config.h or board-specific config:
*   #define TCFG_RDX_HOGP_ENABLE 1

******************************************************************************/
#ifndef TCFG_RDX_HOGP_ENABLE
#define TCFG_RDX_HOGP_ENABLE                  0
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
* Debug / development hooks
******************************************************************************/
#ifndef RDX_BLE_DEBUG_MODE_SWITCH_KEY
#define RDX_BLE_DEBUG_MODE_SWITCH_KEY        1   /* 1 = NUM0 short/long click toggles HOGP mode */
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

#ifdef __cplusplus
}
#endif

#endif /* _RDX_HOGP_CONFIG_H_ */
