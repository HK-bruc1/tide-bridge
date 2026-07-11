/*=====================================================================================
 HEADER NAME: rdx_ble_mode_controller.c
 MODULE NAME: RDX BLE mode controller.

 GENERAL DESCRIPTION:
    Lightweight state manager for BLE mode requests, advertised identity, and
    connection ownership. It does not perform BLE operations (disconnect,
    advertising, suppression checks); those remain in rdx_ble_server.c.

=======================================================================================*/

#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".rdx_ble_mode_controller.data.bss")
#pragma data_seg(".rdx_ble_mode_controller.data")
#pragma const_seg(".rdx_ble_mode_controller.text.const")
#pragma code_seg(".rdx_ble_mode_controller.text")
#endif

/******************************************************************************
* Include files
******************************************************************************/
#include "app_config.h"
#include "rdx_hogp_config.h"
#include "rdx_ble_mode_controller.h"

/******************************************************************************
* Local variables
******************************************************************************/
static struct {
    rdx_ble_mode_t requested_mode;
    rdx_ble_mode_t advertised_mode;
    rdx_ble_connection_owner_t connection_owner;
    u8 switch_pending;
} s_ble_mode = {
    .requested_mode = RDX_BLE_MODE_CONFIG,
    .advertised_mode = RDX_BLE_MODE_CONFIG,
    .connection_owner = RDX_BLE_OWNER_NONE,
    .switch_pending = 0,
};

/******************************************************************************
* Static helpers
******************************************************************************/
static const char *rdx_ble_mode_name(rdx_ble_mode_t mode)
{
    switch (mode) {
    case RDX_BLE_MODE_CONFIG: return "CONFIG";
    case RDX_BLE_MODE_HOGP:   return "HOGP";
    default:                  return "UNKNOWN";
    }
}

/******************************************************************************
* Effective default
******************************************************************************/
rdx_ble_mode_t rdx_ble_mode_effective_default(void)
{
#if TCFG_RDX_HOGP_ENABLE
    return (rdx_ble_mode_t)RDX_BLE_DEFAULT_MODE;
#else
    /* HOGP compiled out: always fall back to RDX Config regardless of project default. */
    return RDX_BLE_MODE_CONFIG;
#endif
}

/******************************************************************************
* Lifecycle
******************************************************************************/
void rdx_ble_mode_controller_init(void)
{
    rdx_ble_mode_t default_mode = rdx_ble_mode_effective_default();
    s_ble_mode.requested_mode = default_mode;
    s_ble_mode.advertised_mode = default_mode;
    s_ble_mode.connection_owner = RDX_BLE_OWNER_NONE;
    s_ble_mode.switch_pending = 0;
}

void rdx_ble_mode_controller_reset(void)
{
    rdx_ble_mode_controller_init();
}

/******************************************************************************
* Mode requests
******************************************************************************/
int rdx_ble_mode_request_set(rdx_ble_mode_t mode)
{
    if (mode != RDX_BLE_MODE_CONFIG && mode != RDX_BLE_MODE_HOGP) {
        y_printf("[BLE_MODE] invalid mode request %d\n", mode);
        return -1;
    }

    if (s_ble_mode.requested_mode == mode && !s_ble_mode.switch_pending) {
        y_printf("[BLE_MODE] mode %s already requested, ignore\n", rdx_ble_mode_name(mode));
        return 0;
    }

    s_ble_mode.requested_mode = mode;
    s_ble_mode.switch_pending = 1;
    return 0;
}

/******************************************************************************
* Mode queries
******************************************************************************/
u8 rdx_ble_mode_is_hogp_requested(void)
{
#if TCFG_RDX_HOGP_ENABLE
    return (s_ble_mode.requested_mode == RDX_BLE_MODE_HOGP) ? 1 : 0;
#else
    return 0;
#endif
}

rdx_ble_mode_t rdx_ble_mode_get_requested(void)
{
    return s_ble_mode.requested_mode;
}

rdx_ble_mode_t rdx_ble_mode_get_advertised(void)
{
    return s_ble_mode.advertised_mode;
}

void rdx_ble_mode_set_advertised(rdx_ble_mode_t mode)
{
    s_ble_mode.advertised_mode = mode;
}

u8 rdx_ble_mode_switch_pending(void)
{
    return s_ble_mode.switch_pending;
}

void rdx_ble_mode_clear_pending(void)
{
    s_ble_mode.switch_pending = 0;
}

/******************************************************************************
* Connection ownership
******************************************************************************/
rdx_ble_connection_owner_t rdx_ble_connection_owner_get(void)
{
    return s_ble_mode.connection_owner;
}

void rdx_ble_connection_owner_set(rdx_ble_connection_owner_t owner)
{
    s_ble_mode.connection_owner = owner;
}

u8 rdx_ble_connection_owner_is_hogp(void)
{
    return (s_ble_mode.connection_owner == RDX_BLE_OWNER_HOGP) ? 1 : 0;
}
