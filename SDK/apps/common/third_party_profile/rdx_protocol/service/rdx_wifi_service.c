#include "rdx_wifi_service.h"
#include "rdx_spi.h"
#include "system/includes.h"
#include "rdx_log.h"
#include "rdx_protocol.h"
#include "rdx_uxfile.h"
#include "rdx_jl_osal.h"
#include "rdx_ops.h"
#include "xxpUart.h"
#include "rdx_led_ctrl.h"
#include "rdx_default_hooks.h"

extern u8  xxp_rx_parse(u8 *data, unsigned short len);

extern ReqFileInfo *rdx_protocol_get_uploadfileInfo(void);
extern void rdx_protocol_file_sync_busy_timer_stop(void);
extern void rdx_protocol_prepared_data_clean(void);

static RdxWifiInfo g_wifi_info;
static const rdx_wifi_transport_ops_t *g_wifi_transport;

RdxWifiInfo *rdx_wifi_service_get_wifi_info(void)
{
    return &g_wifi_info;
}

void rdx_wifi_service_reset_state(void)
{
	memset(&g_wifi_info, 0, sizeof(g_wifi_info));
}

void rdx_wifi_service_set_state(u8 onoff, u8 conn_state)
{
	g_wifi_info.onoff = onoff;
	g_wifi_info.conn_state = conn_state;
}

static void wifi_rx_cb(const u8 *data, u32 len, void *ctx)
{
	(void)ctx;
	xxp_rx_parse((u8 *)data, (unsigned short)len);
}

static void wifi_tx_done_cb(void *ctx)
{
	ReqFileInfo *ru;

	(void)ctx;
	ru = rdx_protocol_get_uploadfileInfo();
	if (!ru) {
		return;
	}

	if (ru->file_send_busy == true) {
		ru->file_send_busy = false;
	}

	if (ru->send_stop == true) {
		rdx_protocol_file_sync_busy_timer_stop();
		rdx_uxfile_recordFileData_sendBuf_free();
		rdx_protocol_prepared_data_clean();
		if (g_wifi_transport && g_wifi_transport->control) {
			g_wifi_transport->control(RDX_WIFI_CTRL_DATA_TRANSFER_TIMER_STOP, NULL);
			g_wifi_transport->control(RDX_WIFI_CTRL_DATA_TRANSFER_TIMER_START, NULL);
		}
		ru->interrupt = false;
		return;
	}

	if (ru->interrupt == true) {
		ru->interrupt = false;
		if (ru->loop == true) {
			os_taskq_post_msg(RDX_PROTOCOL_SEND_TASK_NAME, 1, ru);
		}
	} else {
		if (ru->loop == true) {
			if (ru->total_pack > ru->pack_num) {
				ru->pack_num++;
				ru->ack = 0;
			} else {
				ru->pack_num = 0;
				ru->ack = 0;
			}
			rdx_os_time_dly(2);
			{
				int qret;
				qret = os_taskq_post_msg(RDX_PROTOCOL_SEND_TASK_NAME, 1, ru);
				if (qret != OS_NO_ERR) {
					rdx_os_time_dly(1);
					os_taskq_post_msg(RDX_PROTOCOL_SEND_TASK_NAME, 1, ru);
				}
			}
		}
	}
}

void rdx_wifi_service_init(void)
{
	rdx_spi_register_wifi_callbacks(wifi_rx_cb, wifi_tx_done_cb, NULL);
	g_wifi_transport = rdx_wifi_transport_ops_get();
	RDX_LOGI("wifi_service init done");
}

rdx_err_t rdx_wifi_power_on(void)
{
    b_printf("=== %s --> wifi open \r", __func__);
    if (g_wifi_info.onoff == TRANSFER_BY_WIFI_ON) {
        return RDX_OK;
    }
    if (g_wifi_transport && g_wifi_transport->open) {
        g_wifi_transport->open(NULL);
    }
    g_wifi_info.onoff = TRANSFER_BY_WIFI_ON;
    rdx_hook_led_set_scene(RDX_LED_SCENE_WIFI_START);
    return RDX_OK;
}

rdx_err_t rdx_wifi_power_off(void)
{
    b_printf("=== %s --> wifi close \r", __func__);
    if (g_wifi_transport && g_wifi_transport->control) {
        g_wifi_transport->control(RDX_WIFI_CTRL_POWERON_TIMER_CANCEL, NULL);
    }
    if (g_wifi_info.onoff == TRANSFER_BY_WIFI_OFF) {
        return RDX_OK;
    }
    if (g_wifi_transport && g_wifi_transport->close) {
        g_wifi_transport->close();
    }
    rdx_wifi_service_set_state(TRANSFER_BY_WIFI_OFF, TRANSFER_BY_WIFI_OFF);
    rdx_hook_led_restore_system_state();
    return RDX_OK;
}

void rdx_wifi_data_send(const u8 *data, u32 len)
{
	(void)data;
	(void)len;
}

u8 rdx_wifi_is_connected(void)
{
	return 0;
}

void rdx_wifi_service_on_rx(const u8 *data, u32 len)
{
	xxp_rx_parse((u8 *)data, (unsigned short)len);
}

void rdx_wifi_service_on_tx_done(void)
{
	wifi_tx_done_cb(NULL);
}

int rdx_wifi_service_is_send_stopped(void)
{
	ReqFileInfo *ru = rdx_protocol_get_uploadfileInfo();
	return (ru && ru->send_stop == true) ? 1 : 0;
}

int rdx_wifi_service_is_file_send_busy(void)
{
	ReqFileInfo *ru = rdx_protocol_get_uploadfileInfo();
	return (ru && ru->file_send_busy == true) ? 1 : 0;
}

void rdx_wifi_service_retry_on_stuck(void)
{
	ReqFileInfo *ru = rdx_protocol_get_uploadfileInfo();
	if (ru && ru->loop && !ru->send_stop && !ru->interrupt) {
		ru->ack = 1;
		os_taskq_post_msg(RDX_PROTOCOL_SEND_TASK_NAME, 1, ru);
	}
}
