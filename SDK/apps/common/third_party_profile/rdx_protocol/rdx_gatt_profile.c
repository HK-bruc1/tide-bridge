#ifdef SUPPORT_MS_EXTENSIONS
#pragma const_seg(".rdx_gatt_profile.text.const")
#pragma code_seg(".rdx_gatt_profile.text")
#endif

#include "app_config.h"
#include "rdx_hogp_profile.h"
#include "rdx_gatt_profile.h"

const u8 rdx_profile_data[] = {
#include "rdx_gatt_private_profile.inc"
#include "rdx_hid_profile.inc"
#include "rdx_dis_profile.inc"
    0x00, 0x00,
};

u16 rdx_gatt_profile_dispatch_read(
    const rdx_gatt_read_context_t *context,
    const rdx_gatt_profile_ops_t *ops)
{
    rdx_gatt_read_provider_t provider = NULL;

    if (!context || !ops) {
        return 0;
    }

    switch (context->att_handle) {
    case RDX_GATT_GAP_NAME_VALUE_HANDLE:
        provider = ops->read_gap_name;
        break;
    case RDX_GATT_READ_VALUE_HANDLE:
        provider = ops->read_private_value;
        break;
    case RDX_GATT_BATTERY_VALUE_HANDLE:
        provider = ops->read_battery;
        break;
    default:
        if (context->att_handle >= HID_SERVICE_START_HANDLE &&
            context->att_handle <= HID_SERVICE_END_HANDLE) {
            provider = ops->read_hid;
        }
        break;
    }

    return provider ? provider(context) : 0;
}

int rdx_gatt_profile_dispatch_write(
    const rdx_gatt_write_context_t *context,
    const rdx_gatt_profile_ops_t *ops)
{
    rdx_gatt_write_provider_t provider;

    if (!context || !ops) {
        return -1;
    }

    if (context->att_handle == RDX_GATT_BATTERY_CCC_HANDLE) {
        provider = ops->write_battery_ccc;
    } else if (context->att_handle >= HID_SERVICE_START_HANDLE &&
               context->att_handle <= HID_SERVICE_END_HANDLE) {
        provider = ops->write_hid;
    } else {
        switch (context->att_handle) {
        case RDX_GATT_COMMAND_VALUE_HANDLE:
        case RDX_GATT_NOTIFY_CCC_HANDLE:
        case RDX_GATT_OTA_COMMAND_VALUE_HANDLE:
        case RDX_GATT_OTA_NOTIFY_CCC_HANDLE:
            provider = ops->write_private;
            break;
        default:
            provider = ops->write_rejected;
            break;
        }
    }

    return provider ? provider(context) : -1;
}

u8 rdx_gatt_profile_is_rdx_capability_handle(u16 att_handle)
{
    switch (att_handle) {
    case RDX_GATT_COMMAND_VALUE_HANDLE:
    case RDX_GATT_NOTIFY_VALUE_HANDLE:
    case RDX_GATT_NOTIFY_CCC_HANDLE:
    case RDX_GATT_READ_VALUE_HANDLE:
    case RDX_GATT_OTA_COMMAND_VALUE_HANDLE:
    case RDX_GATT_OTA_NOTIFY_VALUE_HANDLE:
    case RDX_GATT_OTA_NOTIFY_CCC_HANDLE:
        return 1;
    default:
        return 0;
    }
}
