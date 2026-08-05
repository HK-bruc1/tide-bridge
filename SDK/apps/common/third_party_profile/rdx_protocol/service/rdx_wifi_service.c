#include "rdx_wifi_service.h"
#include "rdx_file_transfer_service.h"
#include "rdx_spi.h"
#include "system/includes.h"
#include "rdx_log.h"
#include "rdx_ops.h"
#include "xxpUart.h"
#include "rdx_led_ctrl.h"
#include "rdx_default_hooks.h"

extern u8  xxp_rx_parse(u8 *data, unsigned short len);

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

static void wifi_file_transfer_timer_control(
    rdx_file_transfer_timer_action_t action,
    const void *ctx)
{
    const rdx_wifi_transport_ops_t *transport =
        (const rdx_wifi_transport_ops_t *)ctx;
    u32 command = action == RDX_FILE_TRANSFER_TIMER_STOP
                  ? RDX_WIFI_CTRL_DATA_TRANSFER_TIMER_STOP
                  : RDX_WIFI_CTRL_DATA_TRANSFER_TIMER_START;

    transport->control(command, NULL);
}

static void wifi_tx_done_cb(void *ctx)
{
	(void)ctx;
	rdx_file_transfer_on_tx_done(
		g_wifi_transport && g_wifi_transport->control
		? wifi_file_transfer_timer_control : NULL,
		g_wifi_transport);
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
    /* xxpUart flips onoff after its delayed hardware power-on path completes. */
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
	int stopped = 0;

	return rdx_file_transfer_get_stopped(&stopped) == RDX_OK
	       ? stopped : 0;
}

int rdx_wifi_service_is_file_send_busy(void)
{
    rdx_file_transfer_state_t state = RDX_FILE_TRANSFER_STATE_UNAVAILABLE;

    return rdx_file_transfer_get_state(&state) == RDX_OK &&
           state == RDX_FILE_TRANSFER_STATE_BUSY;
}

void rdx_wifi_service_retry_on_stuck(void)
{
	rdx_file_transfer_retry_on_stuck();
}
