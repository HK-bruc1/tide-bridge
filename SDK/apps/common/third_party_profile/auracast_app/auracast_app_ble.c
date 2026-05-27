#include "app_config.h"
#include "app_msg.h"
#include "bt_tws.h"
#include "app_main.h"
#include "btstack/avctp_user.h"
#include "multi_protocol_main.h"
#include "auracast_app_protocol.h"
#if TCFG_USER_TWS_ENABLE
#include "classic/tws_api.h"
#endif

#if (THIRD_PARTY_PROTOCOLS_SEL & AURACAST_APP_EN)

void *auracast_app_ble_hdl = NULL;
OS_SEM *auracast_app_exit_sem = NULL;
static u8 g_auracast_app_conn_num = 0;						// auracast连接数量，0是未连接

/*************************************************
                  BLE 相关内容
*************************************************/

const uint8_t auracast_app_ble_profile_data[] = {
    //////////////////////////////////////////////////////
    //
    // 0x0001 PRIMARY_SERVICE  1800
    //
    //////////////////////////////////////////////////////
    0x0a, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x28, 0x00, 0x18,

    /* CHARACTERISTIC,  2a00, READ | WRITE | DYNAMIC, */
    // 0x0002 CHARACTERISTIC 2a00 READ | WRITE | DYNAMIC
    0x0d, 0x00, 0x02, 0x00, 0x02, 0x00, 0x03, 0x28, 0x0a, 0x03, 0x00, 0x00, 0x2a,
    // 0x0003 VALUE 2a00 READ | WRITE | DYNAMIC
    0x08, 0x00, 0x0a, 0x01, 0x03, 0x00, 0x00, 0x2a,

    //////////////////////////////////////////////////////
    //
    // 0x0004 PRIMARY_SERVICE  ae00
    //
    //////////////////////////////////////////////////////
    0x0a, 0x00, 0x02, 0x00, 0x04, 0x00, 0x00, 0x28, 0x00, 0xae,

    /* CHARACTERISTIC,  ae03, WRITE_WITHOUT_RESPONSE | DYNAMIC, */
    // 0x0005 CHARACTERISTIC ae03 WRITE_WITHOUT_RESPONSE | DYNAMIC
    0x0d, 0x00, 0x02, 0x00, 0x05, 0x00, 0x03, 0x28, 0x04, 0x06, 0x00, 0x03, 0xae,
    // 0x0006 VALUE ae03 WRITE_WITHOUT_RESPONSE | DYNAMIC
    0x08, 0x00, 0x04, 0x01, 0x06, 0x00, 0x01, 0xae,

    /* CHARACTERISTIC,  ae04, NOTIFY, */
    // 0x0007 CHARACTERISTIC ae04 NOTIFY
    0x0d, 0x00, 0x02, 0x00, 0x07, 0x00, 0x03, 0x28, 0x10, 0x08, 0x00, 0x04, 0xae,
    // 0x0008 VALUE ae04 NOTIFY
    0x08, 0x00, 0x10, 0x00, 0x08, 0x00, 0x02, 0xae,
    // 0x0009 CLIENT_CHARACTERISTIC_CONFIGURATION
    0x0a, 0x00, 0x0a, 0x01, 0x09, 0x00, 0x02, 0x29, 0x00, 0x00,

    // END
    0x00, 0x00,
};

//
// characteristics <--> handles
//
static u16 auracast_adv_interval_min = 150;

void auracast_app_cbk_packet_handler(void *hdl, uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    u16 con_handle;
    // printf("cbk packet_type:0x%x, packet[0]:0x%x, packet[2]:0x%x", packet_type, packet[0], packet[2]);
    switch (packet_type) {
    case HCI_EVENT_PACKET:
        switch (hci_event_packet_get_type(packet)) {
        case ATT_EVENT_CAN_SEND_NOW:
            printf("ATT_EVENT_CAN_SEND_NOW");
            break;

        case HCI_EVENT_LE_META:
            switch (hci_event_le_meta_get_subevent_code(packet)) {
            case HCI_SUBEVENT_LE_CONNECTION_COMPLETE:
                con_handle = little_endian_read_16(packet, 4);
                printf("HCI_SUBEVENT_LE_CONNECTION_COMPLETE: %0x", con_handle);
                // reverse_bd_addr(&packet[8], addr);
                put_buf(&packet[8], 6);
                g_auracast_app_conn_num = 1;
                auracast_app_ble_adv_enable(0);
                break;
            default:
                break;
            }
            break;

        case HCI_EVENT_DISCONNECTION_COMPLETE:
            printf("HCI_EVENT_DISCONNECTION_COMPLETE: %0x", packet[5]);
            g_auracast_app_conn_num = 0;
            if (auracast_app_exit_sem != NULL) {
                os_sem_post(auracast_app_exit_sem);
            } else {
                auracast_app_ble_adv_enable(1);
            }
            break;
        default:
            break;
        }
        break;
    }
    return;
}

uint16_t auracast_app_att_read_callback(void *hdl, hci_con_handle_t connection_handle, uint16_t att_handle, uint16_t offset, uint8_t *buffer, uint16_t buffer_size)
{
    uint16_t  att_value_len = 0;
    uint16_t handle = att_handle;
    printf("<-------------read_callback, handle= 0x%04x,buffer= %08x", handle, (u32)buffer);
    switch (handle) {
    case ATT_CHARACTERISTIC_2a00_01_VALUE_HANDLE:
        const char *gap_name = bt_get_local_name();
        att_value_len = strlen(gap_name);
        if ((offset >= att_value_len) || (offset + buffer_size) > att_value_len) {
            break;
        }
        if (buffer) {
            memcpy(buffer, &gap_name[offset], buffer_size);
            att_value_len = buffer_size;
            printf("\n------read gap_name: %s", gap_name);
        }
        break;
    case ATT_CHARACTERISTIC_ae04_01_CLIENT_CONFIGURATION_HANDLE:
        if (buffer) {
            buffer[0] = att_get_ccc_config(handle);
            buffer[1] = 0;
        }
        att_value_len = 2;
        break;
    default:
        break;
    }
    printf("att_value_len= %d", att_value_len);
    return att_value_len;
    return 0;
}

int auracast_app_att_write_callback(void *hdl, hci_con_handle_t connection_handle, uint16_t att_handle, uint16_t transaction_mode, uint16_t offset, uint8_t *buffer, uint16_t buffer_size)
{
    int result = 0;
    u16 tmp16;
    u16 handle = att_handle;
    printf("<-------------write_callback, handle= 0x%04x,size = %d", handle, buffer_size);
    switch (handle) {
    case ATT_CHARACTERISTIC_2a00_01_VALUE_HANDLE:
        break;
    case ATT_CHARACTERISTIC_ae03_01_VALUE_HANDLE:
        printf("rx(%d):\n", buffer_size);
        put_buf(buffer, buffer_size);
        auracast_app_packet_receive(buffer, buffer_size);
        break;
    case ATT_CHARACTERISTIC_ae04_01_CLIENT_CONFIGURATION_HANDLE:
        printf("\nwrite ccc:%04x, %02x\n", handle, buffer[0]);
        att_set_ccc_config(handle, buffer[0]);
        break;
    default:
        break;
    }
    return 0;
}

static u8 auracast_fill_adv_data(u8 *adv_data)
{
    u8 offset = 0;
    const uint8_t *edr_addr = bt_get_mac_addr();
    adv_data[offset++] = 14;
    adv_data[offset++] = 0x16;
    adv_data[offset++] = 0x00;
    adv_data[offset++] = 0xAE;
    adv_data[offset++] = 0x01;
    adv_data[offset++] = 0x01;  // 0x03:dongle 0x02:soundbox 0x01:earphone
    adv_data[offset++] = 0x01;
    //memcpy(&adv_data[offset], edr_addr, 6);
    swapX(edr_addr, &adv_data[offset], 6);
    offset += 6;
    adv_data[offset++] = 0x00; //status
    adv_data[offset++] = 0x01; // 0:BLE 1:GATT over EDR

    if (offset > ADV_RSP_PACKET_MAX) {
        puts("***adv_data overflow!!!!!!\n");
        return 0;
    }
    return offset;
}

static u8 auracast_fill_rsp_data(u8 *rsp_data)
{
    u8 offset = 0;
    const char *name_p = bt_get_local_name();
    int name_len = strlen(name_p);
    offset += make_eir_packet_data(&rsp_data[offset], offset, HCI_EIR_DATATYPE_COMPLETE_LOCAL_NAME, (void *)name_p, name_len);
    if (offset > ADV_RSP_PACKET_MAX) {
        puts("***rsp_data overflow!!!!!!\n");
        return 0;
    }
    return offset;
}

int auracast_app_ble_adv_enable(u8 enable)
{
#if TCFG_USER_TWS_ENABLE
    if ((tws_api_get_role() == TWS_ROLE_SLAVE) && enable) {
        printf("tws slave cant open adv\n");
        return -1;
    }
#endif

    uint8_t adv_type = APP_ADV_SCAN_IND; // ADV_IND;
    uint8_t adv_channel = ADV_CHANNEL_ALL;
    uint8_t advData[ADV_RSP_PACKET_MAX] = {0};
    uint8_t rspData[ADV_RSP_PACKET_MAX] = {0};
    uint8_t len = 0;

    if (auracast_app_ble_hdl == NULL) {
        return -1;
    }

    if (enable == app_ble_adv_state_get(auracast_app_ble_hdl)) {
        return 0;
    }
    if (enable) {
        app_ble_set_adv_param(auracast_app_ble_hdl, auracast_adv_interval_min, adv_type, adv_channel);
        len = auracast_fill_adv_data(advData);
        if (len) {
            app_ble_adv_data_set(auracast_app_ble_hdl, advData, len);
        }
        len = auracast_fill_rsp_data(rspData);
        if (len) {
            app_ble_rsp_data_set(auracast_app_ble_hdl, rspData, len);
        }
    }
    printf("app_auracast_adv_en:%d\n", enable);
    app_ble_adv_enable(auracast_app_ble_hdl, enable);
    return 0;
}

int auracast_app_ble_send(u8 *data, u32 len)
{
    int ret = 0;
    int i;
    printf("auracast_app_ble_send len = %d", len);
    put_buf(data, len);

    if (0 == app_ble_get_hdl_con_handle(auracast_app_ble_hdl)) {
        printf("ble_send fail, conn handle null\n");
    }

    ret = app_ble_att_send_data(auracast_app_ble_hdl, ATT_CHARACTERISTIC_ae04_01_VALUE_HANDLE, data, len, ATT_OP_AUTO_READ_CCC);
    if (ret) {
        printf("send fail\n");
    }
    return ret;
}

int auracast_app_ble_disconnect(void)
{
    app_ble_disconnect(auracast_app_ble_hdl);
    return 0;
}

/*************************************************
                  BLE 相关内容 end
*************************************************/

void auracast_app_ble_init(void)
{
    printf("auracast_app_ble_init\n");
    const uint8_t *edr_addr = bt_get_mac_addr();

    if (auracast_app_ble_hdl != NULL) {
        printf("auracast_app_ble_hdl is not NULL\n");
        return;
    }

    printf("edr addr:");
    put_buf((uint8_t *)edr_addr, 6);

    // BLE init
    auracast_app_ble_hdl = app_ble_hdl_alloc();
    if (auracast_app_ble_hdl == NULL) {
        printf("auracast_app_ble_hdl alloc err !\n");
        return;
    }
    app_ble_set_mac_addr(auracast_app_ble_hdl, (void *)edr_addr);
    app_ble_profile_set(auracast_app_ble_hdl, auracast_app_ble_profile_data);
    app_ble_att_read_callback_register(auracast_app_ble_hdl, auracast_app_att_read_callback);
    app_ble_att_write_callback_register(auracast_app_ble_hdl, auracast_app_att_write_callback);
    app_ble_att_server_packet_handler_register(auracast_app_ble_hdl, auracast_app_cbk_packet_handler);
    app_ble_hci_event_callback_register(auracast_app_ble_hdl, auracast_app_cbk_packet_handler);
    app_ble_l2cap_packet_handler_register(auracast_app_ble_hdl, auracast_app_cbk_packet_handler);
    // BLE init end
}

void auracast_app_ble_exit(void)
{
    printf("auracast_app_ble_exit\n");

    if (auracast_app_ble_hdl == NULL) {
        printf("auracast_app_ble_hdl is NULL\n");
    }

    if (auracast_app_exit_sem != NULL) {
        printf("auracast_app_exit_sem != NULL\n");
    }

    // BLE exit
    if (app_ble_get_hdl_con_handle(auracast_app_ble_hdl)) {
        auracast_app_exit_sem = zalloc(sizeof(OS_SEM));
        os_sem_create(auracast_app_exit_sem, 0);
        app_ble_disconnect(auracast_app_ble_hdl);
        os_sem_pend(auracast_app_exit_sem, 0);
        os_sem_del(auracast_app_exit_sem, 0);
        free(auracast_app_exit_sem);
        auracast_app_exit_sem = NULL;
    } else {
        auracast_app_ble_adv_enable(0);
    }

    app_ble_hdl_free(auracast_app_ble_hdl);
    auracast_app_ble_hdl = NULL;
    printf("auracast_app_ble_exit ok\n");
}

#if TCFG_USER_TWS_ENABLE

static void auracast_app_conn_info_tws_sync(u8 app_conn_num)
{
    g_auracast_app_conn_num = app_conn_num;
    printf("tws_sync g_auracast_app_conn_num:%d\n", g_auracast_app_conn_num);
}

static void auracasat_app_conn_info_tws_sync_in_irq(void *_data, u16 len, bool rx)
{
    /* printf("%s, %s, %d, rx:%d\n", __FILE__, __FUNCTION__, __LINE__, rx); */
    if (rx && (tws_api_get_role() == TWS_ROLE_SLAVE)) {
        u8 *u8_data = (u8 *)_data;
        int argv[3];
        argv[0] = (int)auracast_app_conn_info_tws_sync;
        argv[1] = 1;
        argv[2] = (int) * u8_data;
        int ret = os_taskq_post_type("app_core", Q_CALLBACK, 3, argv);
        if (ret) {
            g_auracast_app_conn_num = (u8) * u8_data;
        }
    }
}

REGISTER_TWS_FUNC_STUB(tws_auracast_conn_info_sync) = {
    .func_id = 0x23456C5A,
    .func = auracasat_app_conn_info_tws_sync_in_irq,
};

static int app_le_auracast_tws_msg_handler(int *msg)
{
    struct tws_event *evt = (struct tws_event *)msg;
    u8 role = evt->args[0];
    u8 phone_link_connection = evt->args[1];
    u8 reason = evt->args[2];
    switch (evt->event) {
    case TWS_EVENT_CONNECTED:
        printf("app_le_auracast_tws_event_handler le_auracast role:%d, %d, %d\n", role, tws_api_get_role(), g_auracast_app_conn_num);
        if ((role != TWS_ROLE_SLAVE) && (!g_auracast_app_conn_num)) {
            // 已连接主机同步连接数量信息给从机
            tws_api_send_data_to_sibling((void *)&g_auracast_app_conn_num, sizeof(uint8_t), 0x23456C5A);
            auracast_app_ble_adv_enable(1);
        } else {
            auracast_app_ble_adv_enable(0);
        }
        break;
    case TWS_EVENT_PHONE_LINK_DETACH:
        break;
    case TWS_EVENT_CONNECTION_DETACH:
        printf("app_le_auracast_tws_msg_handler tws detach\n");
        if ((role != TWS_ROLE_SLAVE) && (!g_auracast_app_conn_num)) {
            auracast_app_ble_adv_enable(1);
        } else {
            auracast_app_ble_adv_enable(0);
        }
        break;
    case TWS_EVENT_ROLE_SWITCH:
        printf("le_auracast_tws_event_handler le_auracast role switch:%d, %d, %d\n", role, tws_api_get_role(), g_auracast_app_conn_num);
        if ((role != TWS_ROLE_SLAVE) && (!g_auracast_app_conn_num)) {
            auracast_app_ble_adv_enable(1);
        } else {
            auracast_app_ble_adv_enable(0);
        }
    default:
        break;
    }
    return 0;
}

APP_MSG_HANDLER(app_le_auracast_tws_msg_entry) = {
    .owner      = 0xff,
    .from       = MSG_FROM_TWS,
    .handler    = app_le_auracast_tws_msg_handler,
};

#endif // TCFG_USER_TWS_ENABLE

////////////////////////// gatt over edr ///////////////////////

#endif

