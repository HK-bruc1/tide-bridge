#include "app_config.h"
#include "app_msg.h"
#include "bt_tws.h"
#include "app_main.h"
#include "btstack/avctp_user.h"
#include "multi_protocol_main.h"
#include "auracast_app_protocol.h"

#if (THIRD_PARTY_PROTOCOLS_SEL & AURACAST_APP_EN)

static u8 auracast_app_cur_sn = 0;

int auracast_app_get_device_info_deal(u8 opcode, u8 sn, u8 *payload, u32 payload_len)
{
    printf("auracast_app_get_device_info_deal %d\n", payload_len);
    put_buf(payload, payload_len);
    u8 tbuf[64];
    u16 tlen = 0;
    while (payload_len--) {
        switch (*payload) {
        case 0x01:                      // 基础信息(0x01)
            tbuf[tlen++] = *payload;
            tbuf[tlen++] = 0;           // info len

            tbuf[tlen++] = 2;
            tbuf[tlen++] = 0x01;        // 协议版本号(0x01)
            tbuf[tlen++] = 0x01;

            tbuf[tlen++] = 5;
            tbuf[tlen++] = 0x02;        // 版本信息(0x02)
            tbuf[tlen++] = 0x00;
            tbuf[tlen++] = 0x00;
            tbuf[tlen++] = 0x00;
            tbuf[tlen++] = 0x00;

            tbuf[tlen++] = 9;
            tbuf[tlen++] = 0x03;        // 产品信息(0x03)
            tbuf[tlen++] = 0x01;
            tbuf[tlen++] = 0x01;
            tbuf[tlen++] = 0x01;
            tbuf[tlen++] = 0x02;
            tbuf[tlen++] = 0x03;
            tbuf[tlen++] = 0x04;
            tbuf[tlen++] = 0x05;
            tbuf[tlen++] = 0x06;

            tbuf[tlen++] = 3;
            tbuf[tlen++] = 0x04;        // 音量信息(0x04)
            tbuf[tlen++] = 10;
            tbuf[tlen++] = 1;

            tbuf[tlen++] = 5;
            tbuf[tlen++] = 0x05;        // 支持工作模式(0x05)
            tbuf[tlen++] = 0;
            tbuf[tlen++] = 0;
            tbuf[tlen++] = 0;
            tbuf[tlen++] = 0;
            break;
        case 0x02:                      // 蓝牙信息(0x02)
            tbuf[tlen++] = *payload;
            tbuf[tlen++] = 0;           // info len

            tbuf[tlen++] = 9;
            tbuf[tlen++] = 0x01;
            tbuf[tlen++] = BIT(3) | BIT(0);
            swapX(bt_get_mac_addr(), &tbuf[tlen], 6);
            tlen += 6;
            tbuf[tlen++] = 0x00;

            tbuf[tlen++] = 9;
            tbuf[tlen++] = 0x02;
            tbuf[tlen++] = BIT(0);
            swapX(bt_get_mac_addr(), &tbuf[tlen], 6);
            tlen += 6;
            tbuf[tlen++] = 0x01;
            break;
        case 0x03:                      // 功能配置信息(0x03)
            tbuf[tlen++] = *payload;
            tbuf[tlen++] = 0;           // info len

            tbuf[tlen++] = 2;
            tbuf[tlen++] = 0x01;
            tbuf[tlen++] = BIT(1) | BIT(0);
            break;
        default:
            tbuf[tlen++] = *payload;
            tbuf[tlen++] = 0;           // info len
            break;
        }
        payload++;
    }
    tbuf[1] = tlen - 2;         // info len
    return auracast_app_packet_response(0x00, opcode, sn, tbuf, tlen);
}

int auracast_app_packet_receive(u8 *data, u32 len)
{
    u8 state;
    u8 opcode;
    u8 sn;
    u8 *payload;
    u16 packet_len;
    u32 payload_len;
    u16 crc16;
    u16 temp_crc16;

    packet_len = (data[1] << 8) | data[0];
    crc16 = ((data[len - 1]) << 8) | data[len - 2];
    temp_crc16 = CRC16(data, packet_len);
    printf("auracast_app_packet_receive crc 0x%x->0x%x\n", crc16, temp_crc16);
    opcode = data[3];
    sn = data[4];
    payload = &data[5];
    payload_len = packet_len - 5;
    printf("auracast_app_packet_receive op:0x%x sn:0x%x plen:%d\n", opcode, sn, payload_len);

    switch (opcode) {
    case AURACAST_APP_OPCODE_RECV_GET_DEVICE_INFO:
        auracast_app_get_device_info_deal(opcode, sn, payload, payload_len);
        break;
    case AURACAST_APP_OPCODE_RECV_LOGIN_AUTHENTICATION:
        auracast_app_login_authentication_deal(opcode, sn, payload, payload_len);
        break;
    case AURACAST_APP_OPCODE_RECV_LOGIN_PASSWORD_SET:
        auracast_app_login_password_set_deal(opcode, sn, payload, payload_len);
        break;
    case AURACAST_APP_OPCODE_RECV_BROADCAST_SETTING:
        auracast_app_broadcast_setting_deal(opcode, sn, payload, payload_len);
        break;
    case AURACAST_APP_OPCODE_RECV_DEVICE_RESET:
        auracast_app_broadcast_reset_deal(opcode, sn, payload, payload_len);
        break;
    case AURACAST_APP_OPCODE_RECV_SCAN_CONTROL:
        auracast_app_recv_scan_control_deal(opcode, sn, payload, payload_len);
        break;
    case AURACAST_APP_OPCODE_RECV_DEVICE_STATUS:
        auracast_app_recv_device_status_deal(opcode, sn, payload, payload_len);
        break;
    case AURACAST_APP_OPCODE_RECV_LISTENING_CONTROL:
        auracast_app_recv_listening_control_deal(opcode, sn, payload, payload_len);
        break;
    }

    return 0;
}

int auracast_app_packet_response(u8 state, u8 opcode, u8 sn, u8 *payload, u32 payload_len)
{
    u32 packet_len = payload_len + 8;
    u16 crc16;
    if ((payload_len != 0) && (payload == NULL)) {
        return -1;
    }
    u8 *packet = zalloc(packet_len);
    packet[0] = (packet_len - 2) & 0xFF;
    packet[1] = ((packet_len - 2) >> 8) & 0xFF;
    packet[2] = 0;
    packet[3] = opcode;
    packet[4] = sn;
    auracast_app_cur_sn = sn;
    packet[5] = state;
    if (payload_len) {
        memcpy(&packet[6], payload, payload_len);
    }
    crc16 = CRC16(packet, packet_len - 2);
    packet[packet_len - 2] = crc16 & 0xFF;
    packet[packet_len - 1] = (crc16 >> 8) & 0xFF;
    auracast_app_ble_send(packet, packet_len);
    auracast_app_gatt_over_edr_send(packet, packet_len);
    free(packet);
    return 0;
}

int auracast_app_packet_cmd(u8 opcode, u8 flag, u8 *payload, u32 payload_len)
{
    u32 packet_len = payload_len + 7;
    u16 crc16;
    if ((payload_len != 0) && (payload == NULL)) {
        return -1;
    }
    u8 *packet = zalloc(packet_len);
    packet[0] = (packet_len - 2) & 0xFF;
    packet[1] = ((packet_len - 2) >> 8) & 0xFF;
    packet[2] = flag;
    packet[3] = opcode;
    packet[4] = ++auracast_app_cur_sn;
    if (payload_len) {
        memcpy(&packet[5], payload, payload_len);
    }
    crc16 = CRC16(packet, packet_len - 2);
    packet[packet_len - 2] = crc16 & 0xFF;
    packet[packet_len - 1] = (crc16 >> 8) & 0xFF;
    auracast_app_ble_send(packet, packet_len);
    auracast_app_gatt_over_edr_send(packet, packet_len);
    free(packet);
    return 0;
}

extern void ll_set_param_aclMaxPduCToP(uint8_t aclMaxRxPdu);
void auracast_app_all_init(void)
{
    printf("auracast_app_all_init\n");

    ll_set_param_aclMaxPduCToP(255);
    auracast_app_source_api_init();

    auracast_app_ble_init();
    auracast_app_ble_adv_enable(1);

    auracast_app_gatt_over_edr_init();
}

void auracast_app_all_exit(void)
{
    printf("auracast_app_all_exit\n");
    auracast_app_ble_exit();
    auracast_app_source_api_uninit();
}

#endif



