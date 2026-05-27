#include "app_config.h"
#include "app_msg.h"
#include "bt_tws.h"
#include "app_main.h"
#include "btstack/avctp_user.h"
#include "multi_protocol_main.h"
#include "auracast_app_protocol.h"

#if (TCFG_LE_AUDIO_APP_CONFIG & LE_AUDIO_AURACAST_SINK_EN)
#include "app_le_auracast.h"
#include "le/auracast_sink_api.h"
#endif
#if TCFG_USER_TWS_ENABLE
#include "classic/tws_api.h"
#endif

#if (THIRD_PARTY_PROTOCOLS_SEL & AURACAST_APP_EN)

int auracast_app_notify_source_list(struct auracst_source_item_t *src)
{
    printf("auracast_app_notify_source_list\n");

    u8 tbuf[64];
    u16 tlen = 0;

    u8 name_len = strlen(src->broadcast_name);
    if (0 != name_len) {
        tbuf[tlen++] = name_len + 1;
        tbuf[tlen++] = 0x01;    // Broadcast Name（0x01）
        memcpy(&tbuf[tlen], src->broadcast_name, name_len);
        tlen += name_len;
    }

    tbuf[tlen++] = 4;
    tbuf[tlen++] = 0x02;    // Broadcast ID（0x02）
    tbuf[tlen++] = src->broadcast_id[0];
    tbuf[tlen++] = src->broadcast_id[1];
    tbuf[tlen++] = src->broadcast_id[2];

    tbuf[tlen++] = 2;
    tbuf[tlen++] = 0x03;    // Broadcast features（0x03）
    tbuf[tlen++] = src->broadcast_features;

    tbuf[tlen++] = 7;
    tbuf[tlen++] = 0x04;    // Broadcast addr（0x04）
    swapX(src->adv_address, &tbuf[tlen], 6);
    tlen += 6;

    return auracast_app_packet_cmd(AURACAST_APP_OPCODE_NOTIFY_SOURCE_LIST, 0x01, tbuf, tlen);
}

struct auracst_source_item_t cur_listening_src = {0};
static auracast_sink_source_info_t temp_info = {0};

static u8 fill_listening_status_payload(struct auracst_source_item_t *src, u8 *tbuf)
{
    u8 tlen = 0;
    u8 name_len = strlen(src->broadcast_name);
    if (0 != name_len) {
        tbuf[tlen++] = name_len + 1;
        tbuf[tlen++] = 0x01;    // Broadcast Name（0x01）
        memcpy(&tbuf[tlen], src->broadcast_name, name_len);
        tlen += name_len;
    }

    tbuf[tlen++] = 4;
    tbuf[tlen++] = 0x02;    // Broadcast ID（0x02）
    tbuf[tlen++] = src->broadcast_id[0];
    tbuf[tlen++] = src->broadcast_id[1];
    tbuf[tlen++] = src->broadcast_id[2];

    tbuf[tlen++] = 2;
    tbuf[tlen++] = 0x03;    // Broadcast features（0x03）
    tbuf[tlen++] = src->broadcast_features;

    tbuf[tlen++] = 7;
    tbuf[tlen++] = 0x04;    // Broadcast addr（0x04）
    swapX(src->adv_address, &tbuf[tlen], 6);
    tlen += 6;

    tbuf[tlen++] = 3;
    tbuf[tlen++] = 0x05;    // Broadcast State(0x05)
    tbuf[tlen++] = src->listening_state;
    tbuf[tlen++] = src->listening_state_error;
    return tlen;
}

int auracast_app_notify_listening_status(u8 status, u8 error)
{
#if TCFG_USER_TWS_ENABLE
    if (tws_api_get_role() == TWS_ROLE_SLAVE) {
        printf("auracast_app_notify_listening_status tws slave cant send\n");
        return -1;
    }
#endif

    printf("auracast_app_notify_source_status %d %d\n", status, error);
    struct auracst_source_item_t *src = &cur_listening_src;
    u8 tbuf[64];
    u8 tlen = 0;
    src->listening_state = status;
    src->listening_state_error = error;
    tlen += fill_listening_status_payload(src, &tbuf[tlen]);
    return auracast_app_packet_cmd(AURACAST_APP_OPCODE_NOTIFY_LISTENING_STATUS, 0x01, tbuf, tlen);
}

int auracast_app_recv_device_status_deal(u8 opcode, u8 sn, u8 *payload, u32 payload_len)
{
    return 0;
}

u8 auracast_app_get_scan_status(void)
{
    auracast_sink_scan_state_t scan_state = auracast_sink_scan_state_get();
    if (scan_state) {
        return 1;
    } else {
        return 0;
    }
}

u8 auracast_app_get_listening_status(struct auracst_source_item_t *src)
{
    u8 linstening_status = 0;
    u8 name_len;
    auracast_sink_source_info_t *src_info;
    auracast_sink_big_state_t big_state = auracast_sink_big_state_get();
    if (big_state == SINK_BIG_STATE_IDLE) {
        linstening_status = 0;
    } else if (big_state == SINK_BIG_STATE_BIG_SYNC) {
        linstening_status = 2;
    } else {
        linstening_status = 1;
    }

    if (linstening_status && (NULL != src)) {
        src_info = auracast_sink_listening_source_info_get();

        name_len = strlen((const char *)src_info->broadcast_name);
        if (0 != name_len) {
            memcpy(src->broadcast_name, src_info->broadcast_name, name_len);
        }
        memcpy(src->adv_address, src_info->source_mac_addr, 6);
        src->broadcast_features = src_info->feature;
        src->broadcast_id[0] = src_info->broadcast_id & 0xFF;
        src->broadcast_id[1] = (src_info->broadcast_id >> 8) & 0xFF;
        src->broadcast_id[2] = (src_info->broadcast_id >> 16) & 0xFF;
    }
    return linstening_status;
}

static int auracast_app_response_listening_status(u8 opcode, u8 sn, u8 action)
{
    printf("auracast_app_response_listening_status\n");

    u8 tbuf[64];
    u8 tlen = 0;
    u8 payload_len = 0;

    u8 linstening_status = auracast_app_get_listening_status(&cur_listening_src);

    tbuf[tlen++] = action;
    if (linstening_status == 0) {
        tbuf[tlen++] = 0;   // len
    } else {
        tbuf[tlen++] = 0;
        cur_listening_src.listening_state = linstening_status;
        cur_listening_src.listening_state_error = 0;
        payload_len = fill_listening_status_payload(&cur_listening_src, &tbuf[tlen]);
        tbuf[tlen - 1] = payload_len;
        tlen += payload_len;
    }

    return auracast_app_packet_response(0x00, opcode, sn, tbuf, tlen);
}

int auracast_app_recv_scan_control_deal(u8 opcode, u8 sn, u8 *payload, u32 payload_len)
{
    int wlen = 0;
    u8 action = payload[0];
    printf("auracast_app_recv_scan_control_deal %d\n", payload_len);
    put_buf(payload, payload_len);

    switch (action) {
    case 0x00:      // 停止搜索
        printf("stop scan");
        app_auracast_sink_scan_stop();
        return auracast_app_packet_response(0x00, opcode, sn, NULL, 0);
        break;
    case 0x01:      // 开始搜索
        printf("start scan");
        app_auracast_sink_scan_start();
        return auracast_app_packet_response(0x00, opcode, sn, NULL, 0);
        break;
    }
    return 0;
}

static int auracast_app_source_control_add(u8 opcode, u8 sn, u8 action, u8 *payload, u32 payload_len)
{
    printf("auracast_app_source_control_add");

    u8 tbuf[2];
    u8 status = 0;
    u8 length = 0;
    u8 type = 0;
    int ret = 0;
    u8 name_len;
    struct auracst_source_item_t src = {0};
    u8 *payload_end = payload + payload_len;
    while (payload < payload_end) {
        length = *payload++;
        type = *payload++;
        payload_len -= 2;
        switch (type) {
        case 0x01: //Broadcast Name（0x01）
            memcpy(src.broadcast_name, payload, length - 1);
            payload += length - 1;
            printf("name:%s\n", src.broadcast_name);
            break;
        case 0x02: //Broadcast ID（0x02）
            src.broadcast_id[0] = *payload++;
            src.broadcast_id[1] = *payload++;
            src.broadcast_id[2] = *payload++;
            printf("id: 0x %x %x %x\n", src.broadcast_id[2], src.broadcast_id[1], src.broadcast_id[0]);
            break;
        case 0x03: //Broadcast Features（0x03）
            src.broadcast_features = *payload++;
            printf("features: 0x %x\n", src.broadcast_features);
            break;
        case 0x04: //Broadcast Address(0x04)
            swapX(payload, src.adv_address, 6);
            payload += 6;
            printf("addr:");
            put_buf(src.adv_address, 6);
            break;
        default:
            payload += length - 1;
            break;
        }
    }

    memcpy(temp_info.source_mac_addr, src.adv_address, 6);
    temp_info.broadcast_id = src.broadcast_id[0] + (src.broadcast_id[1] << 8) + (src.broadcast_id[2] << 16);
    temp_info.feature = src.broadcast_features;
    name_len = strlen((const char *)src.broadcast_name);
    if (0 != name_len) {
        memcpy(temp_info.broadcast_name, src.broadcast_name, name_len);
    }

    if (auracast_app_get_scan_status()) {
        app_auracast_sink_scan_stop();
    }

    if (auracast_app_get_listening_status(NULL) != 0) {
        auracast_app_notify_listening_status(0, 0);
        app_auracast_sink_big_sync_terminate();
    }

    ret = app_auracast_sink_big_sync_create(&temp_info);
    if (ret != 0) {
        tbuf[0] = action;
        tbuf[1] = 0x01;
        ret = auracast_app_packet_response(status, opcode, sn, tbuf, 2);
        return ret;
    }

    tbuf[0] = action;
    tbuf[1] = 0;
    ret = auracast_app_packet_response(status, opcode, sn, tbuf, 2);

    memcpy(&cur_listening_src, &src, sizeof(struct auracst_source_item_t));
    auracast_app_notify_listening_status(1, 0);
    return ret;
}

static int auracast_app_source_control_remove(u8 opcode, u8 sn, u8 action)
{
    printf("auracast_app_source_control_remove");
    u8 tbuf[2];
    int ret;
    tbuf[0] = action;
    tbuf[1] = 0;
    ret = auracast_app_packet_response(0x00, opcode, sn, tbuf, 2);

    app_auracast_sink_big_sync_terminate();
    auracast_app_notify_listening_status(0, 0);
    return ret;
}

int auracast_app_recv_listening_control_deal(u8 opcode, u8 sn, u8 *payload, u32 payload_len)
{
    int wlen = 0;
    u8 action = payload[0];
    u8 param_len = payload[1];
    printf("auracast_app_recv_listening_control_deal %d\n", payload_len);
    put_buf(payload, payload_len);

    switch (action) {
    case 0x01:      // 添加音源
        auracast_app_source_control_add(opcode, sn, action, payload + 2, param_len);
        break;
    case 0x02:      // 删除音源
        auracast_app_source_control_remove(opcode, sn, action);
        break;
    case 0x03:      // 获取同步中的音源信息
        auracast_app_response_listening_status(opcode, sn, action);
        break;
    }
    return 0;
}

#endif
