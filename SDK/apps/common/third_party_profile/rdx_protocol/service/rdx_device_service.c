#include "rdx_device_service.h"
#include "rdx_event_bus.h"
#include "system/includes.h"
#include "rdx_log.h"
#include "rdx_err.h"
#include "rdx_record.h"
#include "rdx_vm.h"
#include "rdx_app.h"
#include "xxpUart.h"
#include "gpio_config.h"
#include "poweroff.h"

/* board config */
#include "board/t2616_cc/rdx_board_config.h"

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
	gpio_set_mode(IO_PORT_SPILT(IO_PORTC_01), PORT_HIGHZ);
	gpio_set_mode(IO_PORT_SPILT(IO_PORTC_02), PORT_HIGHZ);
	gpio_set_mode(IO_PORT_SPILT(IO_PORTC_04), PORT_HIGHZ);
	gpio_set_mode(IO_PORT_SPILT(IO_PORTC_05), PORT_HIGHZ);
	gpio_set_mode(IO_PORT_SPILT(WIFI_POWER_PORT_IO), PORT_HIGHZ);
	gpio_set_mode(IO_PORT_SPILT(VDD_POWER_PORT_IO), PORT_HIGHZ);
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
	os_time_dly(50);
	sys_timeout_add(NULL, rdx_device_service_poweroff_cb, 500);
}

/* ---- reboot ---- */

void rdx_device_service_reboot(void)
{
	sys_timeout_add((void *)1, rdx_app_reset_delay_cb, 1000);
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

/* ---- factory reset shell (VM extraction next round) ---- */

int rdx_device_service_factory_reset(void)
{
	return RDX_ERR_NOTSUP;
}

void rdx_device_service_init(void)
{
	RDX_LOGI("device_service init done");
}
