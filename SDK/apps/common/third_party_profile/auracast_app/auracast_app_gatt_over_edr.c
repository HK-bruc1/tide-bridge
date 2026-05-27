#include "app_config.h"
#include "app_msg.h"
#include "bt_tws.h"
#include "app_main.h"
#include "btstack/avctp_user.h"
#include "multi_protocol_main.h"
#include "auracast_app_protocol.h"

#if (THIRD_PARTY_PROTOCOLS_SEL & AURACAST_APP_EN)



void *auracast_app_gatt_hdl = NULL;

#define AURACAST_EDR_ATT_HDL_UUID \
    (((u8)('A' + 'U' + 'R') << (3 * 8)) | \
     ((u8)('C' + 'A' + 'S') << (2 * 8)) | \
     ((u8)('E' + 'D' + 'R') << (1 * 8)) | \
     ((u8)('A' + 'T' + 'T') << (0 * 8)))

static void app_ble_att_over_edr_init(void *hdl, u8 addr_type, u32 uuid)
{
    app_ble_adv_address_type_set(hdl, 0);
    app_ble_att_connect_type_set(hdl, addr_type);
    app_ble_hdl_uuid_set(hdl, uuid);
    app_ble_profile_set(hdl, auracast_app_ble_profile_data);
    app_ble_att_read_callback_register(hdl, auracast_app_att_read_callback);
    app_ble_att_write_callback_register(hdl, auracast_app_att_write_callback);
    app_ble_hci_event_callback_register(hdl, auracast_app_cbk_packet_handler);
}

void auracast_app_gatt_over_edr_init(void)
{
    if (auracast_app_gatt_hdl == NULL) {
        auracast_app_gatt_hdl = app_ble_hdl_alloc();
        if (auracast_app_gatt_hdl == NULL) {
            ASSERT(0, "auracast_app_gatt_hdl alloc err !!\n");
            return;
        }
    }

    u8 addr_type = 0xFB;
    app_ble_att_over_edr_init(auracast_app_gatt_hdl, addr_type, AURACAST_EDR_ATT_HDL_UUID);
}

void auracast_app_gatt_over_edr_exit(void)
{

}

int auracast_app_gatt_over_edr_send(u8 *data, u32 len)
{
    int ret = 0;
    int i;
    printf("auracast_app_gatt_over_edr_send len = %d", len);
    put_buf(data, len);

    if (0 == app_ble_get_hdl_con_handle(auracast_app_gatt_hdl)) {
        printf("gatt over edr send fail, conn handle null\n");
    }

    ret = app_ble_att_send_data(auracast_app_gatt_hdl, ATT_CHARACTERISTIC_ae04_01_VALUE_HANDLE, data, len, ATT_OP_AUTO_READ_CCC);
    if (ret) {
        printf("send fail\n");
    }
    return 0;
}


#endif
