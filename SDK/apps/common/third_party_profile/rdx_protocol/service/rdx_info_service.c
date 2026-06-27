#include "rdx_info_service.h"
#include "rdx_command_dispatch.h"
#include "rdx_protocol.h"
#include "rdx_charge.h"
#include "system/includes.h"

extern u16 sys_get_auto_off_time(void);

static void rdx_info_handle_battery_query(ProtocolEvents event, void *data, u32 len);
static void rdx_info_handle_incharge_query(ProtocolEvents event, void *data, u32 len);
static void rdx_info_handle_version_query(ProtocolEvents event, void *data, u32 len);
static void rdx_info_handle_auth_sn(ProtocolEvents event, void *data, u32 len);
static void rdx_info_handle_offtime_query(ProtocolEvents event, void *data, u32 len);
static void rdx_info_handle_os_type(ProtocolEvents event, void *data, u32 len);

void rdx_info_service_init(void)
{
    rdx_cmd_register(PROTOCOL_EVENT_CMD_BATTERY_QUERY,   rdx_info_handle_battery_query);
    rdx_cmd_register(PROTOCOL_EVENT_CMD_INCHARGE_QUERY,  rdx_info_handle_incharge_query);
    rdx_cmd_register(PROTOCOL_EVENT_CMD_VERSION_QUERY,   rdx_info_handle_version_query);
    rdx_cmd_register(PROTOCOL_EVENT_CMD_AUTH_SN,         rdx_info_handle_auth_sn);
    rdx_cmd_register(PROTOCOL_EVENT_CMD_OFFTIME_QUERY,   rdx_info_handle_offtime_query);
    rdx_cmd_register(PROTOCOL_EVENT_CMD_OS_TYPE,         rdx_info_handle_os_type);
}

static void rdx_info_handle_battery_query(ProtocolEvents event, void *data, u32 len)
{
    (void)event; (void)data; (void)len;
    const RdxProtocolIndicateOps *ops = rdx_protocol_get_indicate_ops();
    if (!ops) return;
    DeviceBatInfo* pb = rdx_protocol_update_dev_battery_level();
    g_printf("[INFO CMD] battery (L=%d, R=%d, C=%d)\r",
             pb->tbat_percent_L, pb->tbat_percent_R, pb->tbat_percent_C);
    ops->battery_indicate(pb->tbat_percent_C, pb->tbat_percent_R, pb->tbat_percent_L);
}

static void rdx_info_handle_incharge_query(ProtocolEvents event, void *data, u32 len)
{
    (void)event; (void)data; (void)len;
    const RdxProtocolIndicateOps *ops = rdx_protocol_get_indicate_ops();
    if (!ops) return;
    DeviceBatInfo* pb = rdx_protocol_update_dev_battery_level();
    u8 charge_state = rdx_app_get_charge_state();
    g_printf("[INFO CMD] incharge state=%d (C=%d, R=%d, L=%d)\r",
             charge_state, pb->tbat_percent_C, pb->tbat_percent_R, pb->tbat_percent_L);
    ops->incharge_indicate(charge_state, pb->tbat_percent_C, pb->tbat_percent_R, pb->tbat_percent_L);
}

static void rdx_info_handle_version_query(ProtocolEvents event, void *data, u32 len)
{
    (void)event; (void)data; (void)len;
    const RdxProtocolIndicateOps *ops = rdx_protocol_get_indicate_ops();
    if (!ops) return;
    char* hv = rdx_protocol_get_hardware_version();
    char* sv = rdx_protocol_get_firmware_version();
    g_printf("[INFO CMD] version (fw=%s, hw=%s)\r", sv ? sv : "", hv ? hv : "");
    ops->version_indicate(hv, sv);
}

static void rdx_info_handle_auth_sn(ProtocolEvents event, void *data, u32 len)
{
    (void)event; (void)data; (void)len;
    const RdxProtocolIndicateOps *ops = rdx_protocol_get_indicate_ops();
    if (!ops) return;
    ops->auth_sn_indicate();
}

static void rdx_info_handle_offtime_query(ProtocolEvents event, void *data, u32 len)
{
    (void)event; (void)data; (void)len;
    const RdxProtocolIndicateOps *ops = rdx_protocol_get_indicate_ops();
    if (!ops) return;
    u32 sec = sys_get_auto_off_time();
    g_printf("[INFO CMD] offtime = %u\r", (unsigned)sec);
    ops->offtime_check_ack_indicate(0, sec);
}

static void rdx_info_handle_os_type(ProtocolEvents event, void *data, u32 len)
{
    (void)event;
    const RdxProtocolIndicateOps *ops = rdx_protocol_get_indicate_ops();
    if (!ops) return;
    if(!data || len < sizeof(ProtocolOsTypeParams)) return;
    ProtocolOsTypeParams* p = (ProtocolOsTypeParams*)data;
    g_printf("[INFO CMD] os_type = %d\r", p->os_type);
    ops->os_type_ack_indicate();
}
