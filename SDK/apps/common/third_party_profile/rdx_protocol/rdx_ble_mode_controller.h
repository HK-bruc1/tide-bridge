/*=====================================================================================
 HEADER NAME: rdx_ble_mode_controller.h
 MODULE NAME: RDX BLE mode controller.

 GENERAL DESCRIPTION:
    Lightweight state manager for BLE mode requests, advertised identity, and
    connection ownership. It does not perform BLE operations (disconnect,
    advertising, suppression checks); those remain in rdx_ble_server.c.

=======================================================================================*/

#ifndef _RDX_BLE_MODE_CONTROLLER_H_
#define _RDX_BLE_MODE_CONTROLLER_H_

#include "system/includes.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
* Types
******************************************************************************/
typedef enum {
    RDX_BLE_MODE_CONFIG = 0,
    RDX_BLE_MODE_HOGP,
} rdx_ble_mode_t;

typedef enum {
    RDX_BLE_OWNER_NONE = 0,
    RDX_BLE_OWNER_CONFIG,
    RDX_BLE_OWNER_HOGP,
} rdx_ble_connection_owner_t;

/******************************************************************************
* Lifecycle
******************************************************************************/
void rdx_ble_mode_controller_init(void);
void rdx_ble_mode_controller_reset(void);

/******************************************************************************
* Mode requests
******************************************************************************/
int  rdx_ble_mode_request_set(rdx_ble_mode_t mode);

/******************************************************************************
* Mode queries
******************************************************************************/
u8             rdx_ble_mode_is_hogp_requested(void);
rdx_ble_mode_t rdx_ble_mode_get_requested(void);
rdx_ble_mode_t rdx_ble_mode_get_advertised(void);
void           rdx_ble_mode_set_advertised(rdx_ble_mode_t mode);
u8             rdx_ble_mode_switch_pending(void);
void           rdx_ble_mode_clear_pending(void);

/******************************************************************************
* Connection ownership
******************************************************************************/
rdx_ble_connection_owner_t rdx_ble_connection_owner_get(void);
void                       rdx_ble_connection_owner_set(rdx_ble_connection_owner_t owner);
u8                         rdx_ble_connection_owner_is_hogp(void);

/******************************************************************************
* Effective default respecting HOGP master switch
******************************************************************************/
rdx_ble_mode_t rdx_ble_mode_effective_default(void);

#ifdef __cplusplus
}
#endif

#endif /* _RDX_BLE_MODE_CONTROLLER_H_ */
