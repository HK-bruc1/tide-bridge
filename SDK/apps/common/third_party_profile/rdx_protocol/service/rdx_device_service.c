#include "rdx_device_service.h"
#include "rdx_event_bus.h"
#include "system/includes.h"
#include "rdx_log.h"
#include "rdx_err.h"
#include "rdx_record_service.h"
#include "rdx_storage_service.h"
#include "rdx_vm.h"
#include "rdx_app.h"
#include "rdx_wifi_service.h"
#include "xxpUart.h"
#include "rdx_jl_osal.h"
#include "rdx_jl_storage.h"
#include "poweroff.h"
#include "btstack/avctp_user.h"
#include "app_main.h"

/* board config */
#include "rdx_board_config.h"
#include "rdx_board_hal.h"
#include "rdx_command_dispatch.h"
#include "rdx_protocol.h"
#include "rdx_ble_server.h"
#include "rdx_default_hooks.h"
#include "../compat/rdx_storage_format_compat.h"

/* rdx_app.c symbols */
extern void      rdx_ble_server_app_disconnect(void);
extern void      rdx_ble_server_exit(void);
extern int       rdx_record_task_free(void);
extern void      rdx_uxfile_task_free(void);
extern void      rdx_app_motor_run_once(void);
extern void      rdx_app_earphone_pack_readchardata(void);
extern void      xxp_uart_set_wifi_default_flag(bool f);
extern void      sd_set_power(u8 enable);
extern void      rdx_app_reset_delay_cb(void *priv);
extern void      oled_task_free(void);

/* librdxApp.a symbols */
extern void rdx_util_str_hexstr2hexarray(u8 *str, u32 len, u8 *out);
extern void rdx_util_reverse_byte(u8 *p, int len);
extern void rdx_protocol_bound_result_indicate(u8 result);
extern void rdx_protocol_choose_to_unbound_ack_indicate(u8 result, u8 state);
/* ReqFileInfo queries now go through rdx_wifi_service_is_file_send_busy() */

/* rdx_app.c / JL SDK symbols needed by VM business functions */
extern void rdx_app_time_to_reset(void);
extern void rdx_app_reset_AI_mode_info(void);
extern void rdx_record_mic_gain_set_default(void);
extern void rdx_rtc_store_timestamp(void);
extern void rdx_cpu_reset(void);
extern void sys_set_auto_off_time(u16 t);
extern u16  sys_get_auto_off_time(void);
extern u8   get_ota_status(void);
extern void bt_tws_remove_pairs(void);
extern int  tws_api_get_role(void);

/* ---- poweroff ---- */

void rdx_device_service_poweroff_cb(void *priv)
{
	(void)priv;
	rdx_record_task_free();
	rdx_uxfile_task_free();
#if (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_OLED) || (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_BOTH_OLED_EMMC)
	oled_task_free();
#endif
	sd_set_power(0);
	rdx_board_shutdown_io_state();
	rdx_board_wifi_power_off();
	rdx_board_vdd_power_off_highz();
	sys_enter_soft_poweroff(POWEROFF_NORMAL);
}

void rdx_device_service_soft_poweroff(void)
{
	(void)rdx_record_service_stop_now(RDX_RECORD_STOP_POWEROFF);

	{
		rdx_wifi_power_off();
	}

	rdx_ble_server_app_disconnect();
	rdx_ble_server_exit();

#if (RDX_SUPPORT_MOTOR == 1)
	rdx_app_motor_run_once();
#endif

	xxp_uart_set_wifi_default_flag(false);
	rdx_os_time_dly(50);
	rdx_os_timer_add(rdx_device_service_poweroff_cb, NULL, 500);
}

/* ---- reboot ---- */

void rdx_device_service_reboot(void)
{
	rdx_os_timer_add(rdx_app_reset_delay_cb, (void *)1, 1000);
}

/* ---- device pair (charge case) ---- */

#if RDX_PRODUCT_IS_CHARGE_CASE

rdx_err_t rdx_device_service_pair(char *au_code, char *mac_str, char *label_sn)
{
	EarphoneInfo *p_epInfo = rdx_vm_get_ep_info();

	if (au_code == NULL || mac_str == NULL)
		return RDX_ERR_INVAL;
	if (memcmp(au_code, "0", RDX_BLE_DEVICE_AUTH_KEY_SIZE) == 0)
		return RDX_ERR_INVAL;
	if (memcmp(mac_str, "0", RDX_BLE_MAC_STRING_SIZE) == 0)
		return RDX_ERR_INVAL;
	if (memcmp(label_sn, "0", RDX_LABEL_SN_SIZE) == 0)
		return RDX_ERR_INVAL;

	{
		rdx_auth_info_t *p_authInfo = rdx_vm_get_auth_info();
		memcpy(p_authInfo->AuthKey, au_code, RDX_BLE_DEVICE_AUTH_KEY_SIZE);
		memcpy(p_authInfo->label_sn, label_sn, RDX_LABEL_SN_SIZE);
	}

	{
		EarphoneInfo ep_info;
		memset(&ep_info, 0, sizeof(EarphoneInfo));
		strncpy(ep_info.ep_mac_str, mac_str, RDX_BLE_MAC_STRING_SIZE);
		rdx_util_str_hexstr2hexarray((u8 *)ep_info.ep_mac_str,
			strlen(ep_info.ep_mac_str), ep_info.ep_mac);
		rdx_util_reverse_byte(ep_info.ep_mac, 6);
		memcpy(p_epInfo, &ep_info, sizeof(EarphoneInfo));
		if (!rdx_vm_write_ep_info_intoVM(&ep_info))
			return RDX_ERR_IO;
	}

	rdx_app_earphone_pack_readchardata();
	rdx_ble_server_reset_local_name();

	return RDX_OK;
}

/* ---- device unpair (charge case) ---- */

rdx_err_t rdx_device_service_unpair(void)
{
	EarphoneInfo *p_epInfo = rdx_vm_get_ep_info();

	memset(p_epInfo, 0, sizeof(EarphoneInfo));
	rdx_vm_write_ep_info_intoVM(p_epInfo);
	rdx_vm_read_ep_info_fromVM();
	rdx_app_earphone_pack_readchardata();

	return RDX_OK;
}

#endif

/* ---- unbound (moved from rdx_vm.c) ---- */

void rdx_device_service_unbound_cb(u8 result)
{
	if (rdx_storage_format_compat_result_is_ok(result)) {
#if TCFG_USER_TWS_ENABLE
		bt_tws_remove_pairs();
#endif
#if (RDX_AI_TRANSLATE_SUPPORT == 1)
		rdx_app_reset_AI_mode_info();
#endif
#if (TCFG_USER_TWS_ENABLE && TCFG_APP_BT_EN)
		if (tws_api_get_role() == TWS_ROLE_MASTER)
			rdx_ble_server_app_disconnect();
#endif
		bt_cmd_prepare(USER_CTRL_DEL_ALL_REMOTE_INFO, 0, NULL);

		{
			u8 name[LOCAL_NAME_LEN];
			memset(name, 0x00, sizeof(name));
			rdx_storage_read_factory_bt_name(name, sizeof(name));
			rdx_storage_write_factory_bt_name(name, LOCAL_NAME_LEN);
		}

		rdx_ble_server_reset_local_name();
		sys_set_auto_off_time(RDX_DEFAULT_SHUT_DOWN_TIME);
		rdx_record_mic_gain_set_default();
		rdx_vm_set_bound_status(0, 0);
		rdx_protocol_bound_result_indicate(0);

		rdx_vm_set_unbounding(false);
		rdx_os_time_dly(100);
		rdx_cpu_reset();
	} else {
		rdx_protocol_bound_result_indicate(1);
		rdx_vm_set_unbounding(false);
	}
}

void rdx_device_service_unbound_handle(void)
{
	rdx_vm_set_unbounding(true);
	(void)rdx_storage_format_compat_for_unbind(rdx_device_service_unbound_cb);
}

void rdx_device_service_choose_to_unbound_cb(u8 result)
{
	if (rdx_storage_format_compat_result_is_ok(result)) {
		rdx_vm_set_bound_status(0, 0);
		rdx_protocol_choose_to_unbound_ack_indicate(0, rdx_vm_get_bound_status());
		rdx_vm_set_unbounding(false);
		rdx_os_time_dly(50);
		rdx_cpu_reset();
	} else {
		rdx_protocol_choose_to_unbound_ack_indicate(1, rdx_vm_get_bound_status());
		rdx_vm_set_unbounding(false);
	}
}

void rdx_device_service_choose_to_unbound_handle(int usr_para, int format_en)
{
	rdx_vm_set_unbounding(true);

	if (usr_para == 1) {
		bt_cmd_prepare(USER_CTRL_DEL_ALL_REMOTE_INFO, 0, NULL);

		{
			u8 name[LOCAL_NAME_LEN];
			memset(name, 0x00, sizeof(name));
			rdx_storage_read_factory_bt_name(name, sizeof(name));
			rdx_storage_write_factory_bt_name(name, LOCAL_NAME_LEN);
		}

		rdx_ble_server_reset_local_name();
		sys_set_auto_off_time(RDX_DEFAULT_SHUT_DOWN_TIME);
		rdx_record_mic_gain_set_default();
	}

	if (format_en == 1) {
		rdx_protocol_choose_to_unbound_ack_indicate(0, rdx_vm_get_bound_status());
		(void)rdx_storage_format_compat_for_unbind(
			rdx_device_service_choose_to_unbound_cb);
	} else {
		rdx_vm_set_bound_status(0, 0);
		rdx_protocol_choose_to_unbound_ack_indicate(0, rdx_vm_get_bound_status());
		rdx_vm_set_unbounding(false);
		rdx_os_time_dly(100);
		rdx_cpu_reset();
	}
}

/* ---- factory reset / user para reset (moved from rdx_vm.c) ---- */

rdx_err_t rdx_device_service_factory_reset(void)
{
	if (get_ota_status())
		return RDX_ERR_BUSY;
	if (rdx_record_service_is_running())
		return RDX_ERR_BUSY;
	if (rdx_wifi_service_is_file_send_busy())
		return RDX_ERR_BUSY;

#if TCFG_USER_TWS_ENABLE
	bt_tws_remove_pairs();
#endif
#if (RDX_AI_TRANSLATE_SUPPORT == 1)
	rdx_app_reset_AI_mode_info();
#endif
#if (TCFG_USER_TWS_ENABLE && TCFG_APP_BT_EN)
	if (tws_api_get_role() == TWS_ROLE_MASTER)
		rdx_ble_server_app_disconnect();
#endif
	bt_cmd_prepare(USER_CTRL_DEL_ALL_REMOTE_INFO, 0, NULL);

	{
		u8 name[LOCAL_NAME_LEN];
		memset(name, 0x00, sizeof(name));
		rdx_storage_read_factory_bt_name(name, sizeof(name));
		rdx_storage_write_factory_bt_name(name, LOCAL_NAME_LEN);
	}

	rdx_ble_server_reset_local_name();
	rdx_record_err_reboot_flag_write_into_vm(0);
	sys_set_auto_off_time(RDX_DEFAULT_SHUT_DOWN_TIME);
	rdx_record_mic_gain_set_default();

#if (RDX_RTC_PATH_SEL == RDX_RTC_PATH_SOFTWARE)
	rdx_rtc_store_timestamp();
#endif

	rdx_device_service_reboot();
	return RDX_OK;
}

void rdx_device_service_user_para_reset(void)
{
	if (get_ota_status())
		return;
	if (rdx_record_service_is_running())
		return;
	if (rdx_wifi_service_is_file_send_busy())
		return;

#if TCFG_USER_TWS_ENABLE
	bt_tws_remove_pairs();
#endif
#if (RDX_AI_TRANSLATE_SUPPORT == 1)
	rdx_app_reset_AI_mode_info();
#endif
#if (TCFG_USER_TWS_ENABLE && TCFG_APP_BT_EN)
	if (tws_api_get_role() == TWS_ROLE_MASTER)
		rdx_ble_server_app_disconnect();
#endif
	bt_cmd_prepare(USER_CTRL_DEL_ALL_REMOTE_INFO, 0, NULL);

	{
		u8 name[LOCAL_NAME_LEN];
		memset(name, 0x00, sizeof(name));
		rdx_storage_read_factory_bt_name(name, sizeof(name));
		rdx_storage_write_factory_bt_name(name, LOCAL_NAME_LEN);
	}

	rdx_ble_server_reset_local_name();
	rdx_record_err_reboot_flag_write_into_vm(0);
	sys_set_auto_off_time(RDX_DEFAULT_SHUT_DOWN_TIME);
	rdx_record_mic_gain_set_default();

#if (RDX_RTC_PATH_SEL == RDX_RTC_PATH_SOFTWARE)
	rdx_rtc_store_timestamp();
#endif
}

static void rdx_cmd_handle_sys_reset(ProtocolEvents event, void *data, u32 len)
{
	(void)event; (void)data; (void)len;
	const RdxProtocolIndicateOps *ops = rdx_protocol_get_indicate_ops();
	if (!ops) return;
	if(get_ota_status() ||
	   rdx_record_service_is_running() ||
	   rdx_wifi_service_is_file_send_busy()){
	    y_printf("[APP CMD] sys_reset rejected: busy\r");
	    ops->sys_set_default_ack_indicate(1);
	    return;
	}
	ops->sys_set_default_ack_indicate(0);
	if (rdx_os_task_post_callback("app_core",
	     (void (*)(void *))rdx_device_service_factory_reset, NULL) != RDX_OK) {
	    log_info("[APP CMD] sys_reset taskq post err\r");
	}

}

static void rdx_cmd_handle_bound(ProtocolEvents event, void *data, u32 len)
{
	(void)event; (void)data; (void)len;
	const RdxProtocolIndicateOps *ops = rdx_protocol_get_indicate_ops();
	if (!ops) return;
	if(!data || len < sizeof(ProtocolBoundParams)) return;
	ProtocolBoundParams* p = (ProtocolBoundParams*)data;
	g_printf("[APP CMD] bound cmd=%d\r", p->cmd);
	if(p->cmd == 1){
	    rdx_vm_set_bound_status(1, 1);
	    ops->bound_result_ack_indicate(0);
	}else{
	    ops->bound_result_ack_indicate(0);
	    rdx_device_service_unbound_handle();
	}

}

static void rdx_cmd_handle_unbound(ProtocolEvents event, void *data, u32 len)
{
	(void)event; (void)data; (void)len;
	const RdxProtocolIndicateOps *ops = rdx_protocol_get_indicate_ops();
	if (!ops) return;
	if(!data || len < sizeof(ProtocolUnboundParams)) return;
	ProtocolUnboundParams* p = (ProtocolUnboundParams*)data;
	if(get_ota_status() ||
	   rdx_record_service_is_running() ||
	   rdx_wifi_service_is_file_send_busy()){
	    y_printf("[APP CMD] unbound rejected: busy\r");
	    ops->unbound_ack_indicate(1, rdx_vm_get_bound_status());
	    return;
	}
	g_printf("[APP CMD] unbound user=%d format=%d\r", p->user_para, p->format_en);
	rdx_device_service_choose_to_unbound_handle(p->user_para, p->format_en);

}

#if RDX_PRODUCT_IS_CHARGE_CASE
static void rdx_cmd_handle_device_pair(ProtocolEvents event, void *data, u32 len)
{
	(void)event; (void)data; (void)len;
	const RdxProtocolIndicateOps *ops = rdx_protocol_get_indicate_ops();
	if (!ops) return;
	if(!data || len < sizeof(ProtocolDevicePairParams)) return;
	ProtocolDevicePairParams* p = (ProtocolDevicePairParams*)data;
	g_printf("[APP CMD] device pair auth=%s ep_mac=%s case_mac=%s sn=%s\r",
	         p->auth_code, p->ep_mac, p->case_mac, p->label_sn);
	int r = rdx_device_service_pair(p->auth_code, p->ep_mac, p->label_sn);
	ops->device_pair_ack_indicate((u8)((r < 0) ? 1 : 0));

}

static void rdx_cmd_handle_device_unpair(ProtocolEvents event, void *data, u32 len)
{
	(void)event; (void)data; (void)len;
	const RdxProtocolIndicateOps *ops = rdx_protocol_get_indicate_ops();
	if (!ops) return;
	g_printf("[APP CMD] device unpair\r");
	int r = rdx_device_service_unpair();
	ops->device_unpair_ack_indicate((u8)((r < 0) ? 1 : 0));

}

#endif

/* ---- OFFTIME_SET handler (Stage 4 from rdx_app.c) ---- */

static void rdx_cmd_handle_offtime_set(ProtocolEvents event, void *data, u32 len)
{
	(void)event; (void)data; (void)len;
	const RdxProtocolIndicateOps *ops = rdx_protocol_get_indicate_ops();
	if (!ops) return;
	if (!data || len < sizeof(ProtocolOfftimeParams)) return;
	ProtocolOfftimeParams *p = (ProtocolOfftimeParams *)data;
	u32 sec = p->offtime;
	if (p->has_value) {
		if (sec >= 1) {
			sys_set_auto_off_time((u16)sec);
		} else {
			ops->offtime_set_ack_indicate(1, (u16)sec);
			return;
		}
	} else {
		sec = sys_get_auto_off_time();
	}
	ops->offtime_set_ack_indicate(0, (u16)sec);
}

/* ---- name query/set handlers (Stage 4 from rdx_app.c) ---- */

static void rdx_cmd_handle_bt_name_query(ProtocolEvents event, void *data, u32 len)
{
	(void)event; (void)data; (void)len;
	const RdxProtocolIndicateOps *ops = rdx_protocol_get_indicate_ops();
	if (!ops) return;
	char bt_name[64];
	int ret = rdx_ble_server_bt_name_set_handle(0, NULL, bt_name, sizeof(bt_name));
	g_printf("[APP CMD] bt name = %s\r", bt_name);
	ops->bt_name_check_ack_indicate((u8)(ret ? 1 : 0), bt_name);
}

static void rdx_cmd_handle_ble_name_query(ProtocolEvents event, void *data, u32 len)
{
	(void)event; (void)data; (void)len;
	const RdxProtocolIndicateOps *ops = rdx_protocol_get_indicate_ops();
	if (!ops) return;
	char ble_name[64];
	int ret = rdx_ble_server_ble_name_set_handle(0, NULL, ble_name, sizeof(ble_name));
	g_printf("[APP CMD] ble name = %s\r", ble_name);
	ops->ble_name_check_ack_indicate((u8)(ret ? 1 : 0), ble_name);
}

static void rdx_cmd_handle_bt_name_set(ProtocolEvents event, void *data, u32 len)
{
	(void)event; (void)data; (void)len;
	const RdxProtocolIndicateOps *ops = rdx_protocol_get_indicate_ops();
	if (!ops) return;
	if (!data || len < sizeof(ProtocolNameParams)) return;
	ProtocolNameParams *p = (ProtocolNameParams *)data;
	char bt_name[64];
	int ret = rdx_ble_server_bt_name_set_handle(p->has_value, p->name,
	                                             bt_name, sizeof(bt_name));
	ops->bt_name_set_ack_indicate((u8)(ret ? 1 : 0), bt_name);
}

static void rdx_cmd_handle_ble_name_set(ProtocolEvents event, void *data, u32 len)
{
	(void)event; (void)data; (void)len;
	const RdxProtocolIndicateOps *ops = rdx_protocol_get_indicate_ops();
	if (!ops) return;
	if (!data || len < sizeof(ProtocolNameParams)) return;
	ProtocolNameParams *p = (ProtocolNameParams *)data;
	char ble_name[64];
	int ret = rdx_ble_server_ble_name_set_handle(p->has_value, p->name,
	                                              ble_name, sizeof(ble_name));
	ops->ble_name_set_ack_indicate((u8)(ret ? 1 : 0), ble_name);
}

void rdx_device_service_init(void)
{
#if RDX_PRODUCT_IS_CHARGE_CASE
	rdx_cmd_register(PROTOCOL_EVENT_CMD_DEVICE_PAIR, rdx_cmd_handle_device_pair);
	rdx_cmd_register(PROTOCOL_EVENT_CMD_DEVICE_UNPAIR, rdx_cmd_handle_device_unpair);
#endif
	rdx_cmd_register(PROTOCOL_EVENT_CMD_SYS_RESET, rdx_cmd_handle_sys_reset);
	rdx_cmd_register(PROTOCOL_EVENT_CMD_BOUND, rdx_cmd_handle_bound);
	rdx_cmd_register(PROTOCOL_EVENT_CMD_UNBOUND, rdx_cmd_handle_unbound);
	rdx_cmd_register(PROTOCOL_EVENT_CMD_OFFTIME_SET, rdx_cmd_handle_offtime_set);
	rdx_cmd_register(PROTOCOL_EVENT_CMD_BT_NAME_QUERY, rdx_cmd_handle_bt_name_query);
	rdx_cmd_register(PROTOCOL_EVENT_CMD_BLE_NAME_QUERY, rdx_cmd_handle_ble_name_query);
	rdx_cmd_register(PROTOCOL_EVENT_CMD_BT_NAME_SET, rdx_cmd_handle_bt_name_set);
	rdx_cmd_register(PROTOCOL_EVENT_CMD_BLE_NAME_SET, rdx_cmd_handle_ble_name_set);
	RDX_LOGI("device_service init done");
}

/* ============================================================================
 * eMMC power state machine — migrated from rdx_app.c (Stage 3)
 * ============================================================================
 * Public API (called via rdx_app_emmc_* wrappers in rdx_app.c for backward
 * compatibility; Stage 4 should switch callers to these names directly):
 *   rdx_device_service_do_emmc_reset()
 *   rdx_device_service_emmc_poweron()
 *   rdx_device_service_emmc_poweroff()
 *   rdx_device_service_emmc_poweroff_check()
 *   rdx_device_service_emmc_poweroff_check_timer_stop()
 */

#define EMMC_LDO_POWER_OFF_CHECK_TIMEOUT    (10 * 1000)

static u16  g_emmc_poweroff_check_timer = 0;
static bool g_emmc_poweroff_flag = FALSE;

/* File-transfer compatibility queries remain P12 ownership work. */
extern u8   rdx_is_file_transfer_active(void);
extern u8   rdx_is_file_sync_busy(void);

static void rdx_device_service_emmc_poweroff_check_timer_cb(void *priv);
static void rdx_device_service_emmc_poweroff_check_timer_start(void);

/*
 * Legacy disabled: returns immediately. The original implementation in rdx_app.c
 * was disabled (return-as-first-statement) and we preserve that behavior here.
 * Re-enable when the eMMC auto-poweroff policy is defined for Stage 4.
 */
void rdx_device_service_emmc_poweroff_check(void)
{
    return;
}

void rdx_device_service_do_emmc_reset(void)
{
    rdx_board_vdd_power_low();
    rdx_os_time_dly(50);
    rdx_board_vdd_power_on();
}

void rdx_device_service_emmc_poweron(u8 check_en)
{
    y_printf("=====> %s --> emmc_poweroff_flag = %d \n", __func__, g_emmc_poweroff_flag);
    if (g_emmc_poweroff_flag == TRUE) {
        rdx_board_vdd_power_on();
        sd_set_power(1);
#if (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_OLED) || (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_BOTH_OLED_EMMC)
        OLED_Init();
#endif
        if (check_en) {
            rdx_device_service_emmc_poweroff_check_timer_start();
        }
        g_emmc_poweroff_flag = false;
    }
}

void rdx_device_service_emmc_poweroff(void)
{
    y_printf("=====> %s --> emmc_poweroff_flag = %d \r", __func__, g_emmc_poweroff_flag);
    if (g_emmc_poweroff_flag == false) {
        rdx_device_service_emmc_poweroff_check_timer_stop();
        sd_set_power(0);
        rdx_board_sd_nand_poweroff_io_state();
        rdx_board_vdd_power_low();
        rdx_board_vdd_power_off_highz();
        g_emmc_poweroff_flag = true;
    }
}

void rdx_device_service_emmc_poweroff_check_timer_stop(void)
{
    if (g_emmc_poweroff_check_timer) {
        rdx_os_timer_del(g_emmc_poweroff_check_timer);
        g_emmc_poweroff_check_timer = 0;
    }
}

static void rdx_device_service_emmc_poweroff_check_timer_cb(void *priv)
{
    bool offline_active = rdx_record_service_is_offline_active();
    (void)priv;
    y_printf("=====> %s --> offline_active = %d \r", __func__, offline_active);
    if (offline_active) {
        y_printf("emmc poweroff timer cb --> emmc is busy, do not power off \r");
        EXCEPTION_THROW();
    }
    if (rdx_storage_is_dat_sync_in_progress()) {
        y_printf("emmc poweroff timer cb --> DAT sync in progress, do not power off \r");
        EXCEPTION_THROW();
    }
    if (rdx_storage_is_file_info_loading()) {
        y_printf("emmc poweroff timer cb --> datFileInfo loading, do not power off \r");
        EXCEPTION_THROW();
    }
    if (rdx_is_file_transfer_active()) {
        y_printf("emmc poweroff timer cb --> file transfer active, do not power off \r");
        EXCEPTION_THROW();
    }
    if (rdx_is_file_sync_busy()) {
        y_printf("emmc poweroff timer cb --> file sync busy, do not power off \r");
        EXCEPTION_THROW();
    }
    if (rdx_storage_is_scan_active()) {
        y_printf("emmc poweroff timer cb --> async scan active, do not power off \r");
        EXCEPTION_THROW();
    }
    if (rdx_storage_is_format_operation_active()) {
        y_printf("emmc poweroff timer cb --> SD formatting, do not power off \r");
        EXCEPTION_THROW();
    }
    if (rdx_hook_motor_is_running()) {
        y_printf("emmc poweroff timer cb --> motor is working, do not power off \r");
        EXCEPTION_THROW();
    }
    y_printf("emmc poweroff timer cb --> emmc power off \r");
    rdx_device_service_emmc_poweroff();
    return;

EXCEPTION_POINTER()
    rdx_device_service_emmc_poweroff_check_timer_stop();
    rdx_device_service_emmc_poweroff_check_timer_start();
}

static void rdx_device_service_emmc_poweroff_check_timer_start(void)
{
    bool record_running = rdx_record_service_is_running();
    if (true == app_in_mode(APP_MODE_PC)) {
        r_printf("=====> %s --> APP_MODE_PC, do not start poweroff timer\r", __func__);
        return;
    }
    if (record_running) {
        return;
    }
    if (rdx_wifi_service_get_wifi_info()->onoff == TRANSFER_BY_WIFI_ON) {
        return;
    }
    if (g_emmc_poweroff_check_timer == 0) {
        g_emmc_poweroff_check_timer = rdx_os_timer_add(
            rdx_device_service_emmc_poweroff_check_timer_cb, NULL,
            EMMC_LDO_POWER_OFF_CHECK_TIMEOUT);
    }
}
