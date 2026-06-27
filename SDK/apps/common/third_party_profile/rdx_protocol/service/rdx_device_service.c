#include "rdx_device_service.h"
#include "rdx_event_bus.h"
#include "system/includes.h"
#include "rdx_log.h"
#include "rdx_err.h"
#include "rdx_record.h"
#include "rdx_vm.h"
#include "rdx_app.h"
#include "rdx_uxfile.h"
#include "rdx_wifi_service.h"
#include "xxpUart.h"
#include "gpio_config.h"
#include "rdx_jl_gpio.h"
#include "rdx_jl_osal.h"
#include "rdx_jl_storage.h"
#include "poweroff.h"
#include "btstack/avctp_user.h"

/* board config */
#include "board/t2616_cc/rdx_board_config.h"
#include "rdx_command_dispatch.h"
#include "rdx_protocol.h"

/* rdx_app.c symbols */
extern void      rdx_ble_server_app_disconnect(void);
extern void      rdx_ble_server_exit(void);
extern int       rdx_record_task_free(void);
extern void      rdx_uxfile_task_free(void);
extern void      rdx_app_wifi_handle(u8 cmd);
extern void      rdx_app_motor_run_once(void);
extern void      rdx_app_earphone_pack_readchardata(void);
extern void      xxp_uart_set_wifi_default_flag(bool f);
extern void      sd_set_power(u8 enable);
extern void      rdx_app_reset_delay_cb(void *priv);
extern void      rdx_ble_server_reset_local_name(void);
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
extern void rdx_record_err_reboot_flag_write_into_vm(u8 v);
extern void rdx_rtc_store_timestamp(void);
extern void rdx_cpu_reset(void);
extern void sys_set_auto_off_time(u16 t);
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
	rdx_gpio_set_highz(IO_PORTC_01);
	rdx_gpio_set_highz(IO_PORTC_02);
	rdx_gpio_set_highz(IO_PORTC_04);
	rdx_gpio_set_highz(IO_PORTC_05);
	rdx_gpio_set_highz(WIFI_POWER_PORT_IO);
	rdx_gpio_set_highz(VDD_POWER_PORT_IO);
	sys_enter_soft_poweroff(POWEROFF_NORMAL);
}

void rdx_device_service_soft_poweroff(void)
{
	RecordStatus *rp = rdx_record_get_status();

	if (rp->run != RECORD_STATE_STOP) {
		rp->run = RECORD_STATE_STOP;
		rdx_record_process();
	}

	{
		RdxWifiInfo *pw = rdx_app_get_wifi_info();
		if (pw && pw->onoff == TRANSFER_BY_WIFI_ON)
			rdx_app_wifi_handle(TRANSFER_BY_WIFI_OFF);
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

int rdx_device_service_pair(char *au_code, char *mac_str, char *label_sn)
{
	EarphoneInfo *p_epInfo = rdx_vm_get_ep_info();

	if (au_code == NULL || mac_str == NULL)
		return -1;
	if (memcmp(au_code, "0", RDX_BLE_DEVICE_AUTH_KEY_SIZE) == 0)
		return -1;
	if (memcmp(mac_str, "0", RDX_BLE_MAC_STRING_SIZE) == 0)
		return -1;
	if (memcmp(label_sn, "0", RDX_LABEL_SN_SIZE) == 0)
		return -1;

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
			return -1;
	}

	rdx_app_earphone_pack_readchardata();
	rdx_ble_server_reset_local_name();

	return 0;
}

/* ---- device unpair (charge case) ---- */

int rdx_device_service_unpair(void)
{
	EarphoneInfo *p_epInfo = rdx_vm_get_ep_info();

	memset(p_epInfo, 0, sizeof(EarphoneInfo));
	rdx_vm_write_ep_info_intoVM(p_epInfo);
	rdx_vm_read_ep_info_fromVM();
	rdx_app_earphone_pack_readchardata();

	return 0;
}

/* ---- unbound (moved from rdx_vm.c) ---- */

void rdx_device_service_unbound_cb(u8 result)
{
	if (result == MEM_FORMAT_RESULT_OK) {
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
			rdx_storage_cfg_read_string(CFG_BT_NAME, name, sizeof(name), 0);
			rdx_storage_cfg_write(CFG_BT_NAME, name, LOCAL_NAME_LEN);
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
	rdx_uxfile_sd_format(rdx_device_service_unbound_cb);
}

void rdx_device_service_choose_to_unbound_cb(u8 result)
{
	if (result == MEM_FORMAT_RESULT_OK) {
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
			rdx_storage_cfg_read_string(CFG_BT_NAME, name, sizeof(name), 0);
			rdx_storage_cfg_write(CFG_BT_NAME, name, LOCAL_NAME_LEN);
		}

		rdx_ble_server_reset_local_name();
		sys_set_auto_off_time(RDX_DEFAULT_SHUT_DOWN_TIME);
		rdx_record_mic_gain_set_default();
	}

	if (format_en == 1) {
		rdx_protocol_choose_to_unbound_ack_indicate(0, rdx_vm_get_bound_status());
		rdx_uxfile_sd_format(rdx_device_service_choose_to_unbound_cb);
	} else {
		rdx_vm_set_bound_status(0, 0);
		rdx_protocol_choose_to_unbound_ack_indicate(0, rdx_vm_get_bound_status());
		rdx_vm_set_unbounding(false);
		rdx_os_time_dly(100);
		rdx_cpu_reset();
	}
}

/* ---- factory reset / user para reset (moved from rdx_vm.c) ---- */

int rdx_device_service_factory_reset(void)
{
	RecordStatus *rp = rdx_record_get_status();

	if (get_ota_status())
		return RDX_ERR_BUSY;
	if (rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME)
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
		rdx_storage_cfg_read_string(CFG_BT_NAME, name, sizeof(name), 0);
		rdx_storage_cfg_write(CFG_BT_NAME, name, LOCAL_NAME_LEN);
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
	RecordStatus *rp = rdx_record_get_status();

	if (get_ota_status())
		return;
	if (rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME)
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
		rdx_storage_cfg_read_string(CFG_BT_NAME, name, sizeof(name), 0);
		rdx_storage_cfg_write(CFG_BT_NAME, name, LOCAL_NAME_LEN);
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
	RecordStatus* rp = rdx_record_get_status();
	if(get_ota_status() ||
	   rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME ||
	   rdx_wifi_service_is_file_send_busy()){
	    y_printf("[APP CMD] sys_reset rejected: busy\r");
	    ops->sys_set_default_ack_indicate(1);
	    return;
	}
	ops->sys_set_default_ack_indicate(0);
	int msg[2];
	msg[0] = (int)rdx_device_service_factory_reset;
	msg[1] = 0;
	if(os_taskq_post_type("app_core", Q_CALLBACK, 2, msg)){
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
	RecordStatus* rp = rdx_record_get_status();
	if(get_ota_status() ||
	   rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME ||
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

void rdx_device_service_init(void)
{
#if RDX_PRODUCT_IS_CHARGE_CASE
	rdx_cmd_register(PROTOCOL_EVENT_CMD_DEVICE_PAIR, rdx_cmd_handle_device_pair);
	rdx_cmd_register(PROTOCOL_EVENT_CMD_DEVICE_UNPAIR, rdx_cmd_handle_device_unpair);
#endif
	rdx_cmd_register(PROTOCOL_EVENT_CMD_SYS_RESET, rdx_cmd_handle_sys_reset);
	rdx_cmd_register(PROTOCOL_EVENT_CMD_BOUND, rdx_cmd_handle_bound);
	rdx_cmd_register(PROTOCOL_EVENT_CMD_UNBOUND, rdx_cmd_handle_unbound);
	RDX_LOGI("device_service init done");
}
