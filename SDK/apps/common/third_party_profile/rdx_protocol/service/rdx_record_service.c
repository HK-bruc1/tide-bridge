#include <stdbool.h>
#include "rdx_record_service.h"
#include "rdx_event_bus.h"
#include "system/includes.h"
#include "rdx_log.h"
#include "rdx_record.h"
#include "rdx_protocol.h"
#include "rdx_app_config.h"
#include "rdx_ble_server.h"
#include "rdx_dut.h"
#include "rdx_command_dispatch.h"
#include "rdx_jl_osal.h"
#include "rdx_time_service.h"
#include "rdx_storage_service.h"
#include "rdx_file_transfer_service.h"
#include "../internal/rdx_record_domain.h"
#include "../compat/rdx_record_protocol_adapter.h"

/* BLE event business logic — Stage 4 cutover from rdx_ble_service.c */
extern void rdx_record_stream_interrupt(void);
extern void rdx_record_stream_resume_delayed(void);

/* symbols from librdxApp.a */
extern void rdx_protocol_record_state_indicate(void);
extern void rdx_util_str_hexstr2hexarray(u8 *str, u32 len, u8 *out);

/* symbols from rdx_app.c (thin wrappers, avoid circular dep) */
extern void rdx_app_switch_keep_timer_restart(void);
extern void rdx_app_switch_keep_timer_start(void);

/* Phase 2 local state moved from rdx_app.c */
static u16           g_upload_timer = 0;
static u8            g_record_mode  = RDX_RECORD_CHANNAL_SINGLE;

static rdx_record_trigger_payload_t rdx_record_service_trigger_payload(
	rdx_record_trigger_payload_kind_t kind,
	const rdx_record_domain_state_t *state)
{
	rdx_record_trigger_payload_t payload;

	payload.kind = kind;
	payload.run = state->run;
	payload.format = state->format;
	payload.scene = state->scene;
	payload.mode = state->mode;
	payload.factor = 0;
	return payload;
}

static void rdx_cmd_handle_mic_gain_query(ProtocolEvents event, void *data, u32 len)
{
	(void)event; (void)data; (void)len;
	const RdxProtocolIndicateOps *ops = rdx_protocol_get_indicate_ops();
	if (!ops) return;
	if(!data || len < sizeof(ProtocolMicGainQueryParams)) return;
	ProtocolMicGainQueryParams* p = (ProtocolMicGainQueryParams*)data;
	int g1 = 0, g2 = 0;
	int ret = rdx_record_mic_gain_query(p->mode, &g1, &g2);
	ops->mic_gain_check_ack_indicate((u8)(ret ? 1 : 0), p->mode, g1, g2);

}
static void rdx_cmd_handle_mic_gain_set(ProtocolEvents event, void *data, u32 len)
{
	(void)event; (void)data; (void)len;
	const RdxProtocolIndicateOps *ops = rdx_protocol_get_indicate_ops();
	if (!ops) return;
	if(!data || len < sizeof(ProtocolMicGainSetParams)) return;
	ProtocolMicGainSetParams* p = (ProtocolMicGainSetParams*)data;
	int g1 = p->mic1_gain, g2 = p->mic2_gain;
	int ret = rdx_record_mic_gain_set(p->mode, &g1, &g2);
	ops->mic_gain_set_ack_indicate((u8)(ret ? 1 : 0), p->mode, g1, g2);

}

static void rdx_cmd_handle_record(ProtocolEvents event, void *data, u32 len)
{
	(void)event; (void)data; (void)len;
	const RdxProtocolIndicateOps *ops = rdx_protocol_get_indicate_ops();
	if (!ops) return;
	if(!data || len < sizeof(Record_info)) return;
	rdx_record_cmd_handle((Record_info*)data);

}

#if TDX_HAS_RECMARK_ABILITY
static void rdx_cmd_handle_recmark(ProtocolEvents event, void *data, u32 len)
{
	(void)event; (void)data; (void)len;
	const RdxProtocolIndicateOps *ops = rdx_protocol_get_indicate_ops();
	if (!ops) return;
	if(!data || len < 1) return;
	u8 src = *(u8*)data;
	rdx_record_add_mark(src);

}

#endif

/*
 * BLE event callback — Stage 4 business cutover.
 * All BLE-triggered record/protocol actions that were previously
 * direct-called from rdx_ble_service.c now execute here.
 */
static void rdx_record_on_ble_event(rdx_event_id_t event, void *payload, u32 len, void *user_ctx)
{
    (void)payload; (void)len; (void)user_ctx;
    if (event == RDX_EVENT_BLE_CONNECTED) {
        rdx_file_transfer_on_ble_connected();
        rdx_record_on_ble_conn_changed(1);
        rdx_record_stream_resume_delayed();
    } else if (event == RDX_EVENT_BLE_DISCONNECTED) {
        rdx_record_stream_interrupt();
        rdx_record_on_ble_conn_changed(0);
        (void)rdx_file_transfer_cleanup_record_disconnect();
    }
}

static void rdx_record_on_time_event(rdx_event_id_t event, void *payload, u32 len, void *user_ctx)
{
	(void)user_ctx;
	if (event != RDX_EVENT_TIME_SYNCED || !payload || len < sizeof(rdx_time_sync_event_t)) {
		return;
	}

	const rdx_time_sync_event_t *sync = (const rdx_time_sync_event_t *)payload;
	if (!rdx_record_service_is_running()) {
		return;
	}
	(void)rdx_storage_service_adjust_active_record_time(sync->delta);
}

/* ---- migrated handlers (Stage 4 from rdx_app.c) ---- */

static void rdx_cmd_handle_record_mode_query(ProtocolEvents event, void *data, u32 len)
{
	(void)event; (void)data; (void)len;
	const RdxProtocolIndicateOps *ops = rdx_protocol_get_indicate_ops();
	rdx_record_domain_state_t state;
	u8 scene;

	if (!ops) return;
	if (rdx_record_domain_get_state(&state) != RDX_OK) return;
	scene = (state.scene == RECORD_SCENE_CALL) ? 1 : 0;
	g_printf("[APP CMD] record_mode (scene=%d, run=%d)\r", scene, state.run);
	ops->record_mode_indicate(scene, state.run);
}

static void rdx_cmd_handle_audio_stream(ProtocolEvents event, void *data, u32 len)
{
	(void)event; (void)data; (void)len;
	const RdxProtocolIndicateOps *ops = rdx_protocol_get_indicate_ops();
	if (!ops) return;
	if (!data || len < sizeof(ProtocolAudioStreamParams)) return;
	ops->audio_stream_play((const ProtocolAudioStreamParams *)data);
}

#if TDX_HAS_FLASHNOTE_ABILITY
static void rdx_cmd_handle_flashnote(ProtocolEvents event, void *data, u32 len)
{
	(void)event; (void)data; (void)len;
	const RdxProtocolIndicateOps *ops = rdx_protocol_get_indicate_ops();
	if (!ops) return;
	if (!data || len < 1) return;
	u8 fn_cmd = *(u8 *)data;
	r_printf("[APP CMD] flashnote cmd=%u (no app impl, swallowed)\r", fn_cmd);
}
#endif

void rdx_record_service_init(void)
{
	rdx_cmd_register(PROTOCOL_EVENT_CMD_MIC_GAIN_QUERY, rdx_cmd_handle_mic_gain_query);
	rdx_cmd_register(PROTOCOL_EVENT_CMD_MIC_GAIN_SET, rdx_cmd_handle_mic_gain_set);
	rdx_cmd_register(PROTOCOL_EVENT_CMD_RECORD, rdx_cmd_handle_record);
#if TDX_HAS_RECMARK_ABILITY
	rdx_cmd_register(PROTOCOL_EVENT_CMD_RECMARK, rdx_cmd_handle_recmark);
#endif
#if TDX_HAS_FLASHNOTE_ABILITY
	rdx_cmd_register(PROTOCOL_EVENT_CMD_FLASHNOTE, rdx_cmd_handle_flashnote);
#endif
	rdx_cmd_register(PROTOCOL_EVENT_CMD_RECORD_MODE_QUERY, rdx_cmd_handle_record_mode_query);
	rdx_cmd_register(PROTOCOL_EVENT_CMD_AUDIO_STREAM, rdx_cmd_handle_audio_stream);
	/* Stage 4: BLE event business subscription */
	rdx_event_subscribe(RDX_EVENT_BLE_CONNECTED,    rdx_record_on_ble_event, NULL);
	rdx_event_subscribe(RDX_EVENT_BLE_DISCONNECTED, rdx_record_on_ble_event, NULL);
	rdx_event_subscribe(RDX_EVENT_TIME_SYNCED,      rdx_record_on_time_event, NULL);

	g_upload_timer = 0;
	g_record_mode  = RDX_RECORD_CHANNAL_SINGLE;
	rdx_record_protocol_adapter_init();
	RDX_LOGI("record_service init done");
}

void rdx_record_service_exit(void)
{
	rdx_record_service_upload_timer_stop();
}

/* ---- upload timer ---- */

void rdx_record_service_upload_timer_stop(void)
{
	if (g_upload_timer) {
		rdx_os_timer_del(g_upload_timer);
		g_upload_timer = 0;
	}
}

void rdx_record_service_upload_timer_start(void)
{
	if (g_upload_timer == 0) {
		g_upload_timer = rdx_os_timer_add(rdx_record_service_upload_timer_cb, NULL, 3000);
	}
}

void rdx_record_service_upload_timer_cb(void *priv)
{
	rdx_record_domain_state_t trigger;
	rdx_record_trigger_payload_t payload;
	rdx_err_t ret;
	u16 con_hdl;

	(void)priv;
	rdx_record_service_upload_timer_stop();

	if (rdx_record_domain_prepare_upload_fallback(
		RDX_RECORD_STOP_UPLOAD_FALLBACK,
		rdx_ble_server_get_conn_handle,
		&con_hdl,
		&trigger) != RDX_OK) {
		return;
	}
	if (0xffff != con_hdl && 0 != con_hdl) {
		payload = rdx_record_service_trigger_payload(
			RDX_RECORD_TRIGGER_PAYLOAD_UPLOAD, &trigger);
		ret = rdx_record_protocol_post_trigger(&payload);
		if (ret == RDX_ERR_NOMEM) {
			RDX_LOGW("record_svc rp pool full (upload)");
		} else if (ret != RDX_OK) {
			RDX_LOGW("record_svc upload indicate post fail");
		}
	} else {
		(void)rdx_record_domain_post_process();
	}
}

/* ---- device record handle (was rdx_app_device_record_handle) ---- */

static rdx_err_t rdx_record_service_device_toggle_core(u8 scene)
{
	rdx_record_scene_t typed_scene;
	u16 con_hdl = rdx_ble_server_get_conn_handle();
	rdx_record_protocol_reservation_t reservation;
	rdx_record_domain_toggle_result_t result;
	rdx_record_trigger_payload_t payload;
	rdx_err_t ret;

	if (scene == RECORD_SCENE_CHAT) {
		typed_scene = RDX_RECORD_SCENE_CHAT;
	} else if (scene == RECORD_SCENE_CALL) {
		typed_scene = RDX_RECORD_SCENE_CALL;
	} else {
		return RDX_ERR_INVAL;
	}
	if (0xffff != con_hdl && 0 != con_hdl) {
		if (get_ota_status()) {
			return RDX_ERR_BUSY;
		}

		ret = rdx_record_protocol_reserve(
			RDX_RECORD_TRIGGER_PAYLOAD_DEVICE, &reservation);
		if (ret != RDX_OK) {
			RDX_LOGW("record_svc rp pool full (dev_rec)");
			return ret;
		}
		ret = rdx_record_domain_prepare_connected_toggle(typed_scene, &result);
		if (ret != RDX_OK) {
			rdx_record_protocol_cancel_reserved(&reservation);
			return ret;
		}
		payload = rdx_record_service_trigger_payload(
			RDX_RECORD_TRIGGER_PAYLOAD_DEVICE, &result.trigger);
		ret = rdx_record_protocol_fill_reserved(&reservation, &payload);
		if (ret != RDX_OK) {
			rdx_record_protocol_cancel_reserved(&reservation);
			return ret;
		}
		if (result.start_upload_timer) {
			rdx_record_service_upload_timer_start();
		}
		ret = rdx_record_protocol_post_reserved(&reservation);
		if (ret != RDX_OK) {
			RDX_LOGW("record_svc trigger_indicate post fail");
			return ret;
		}
		return result.process_post_result;
	}

	return rdx_record_domain_toggle_post(typed_scene);
}

void rdx_record_service_device_record_handle(u8 scene)
{
	(void)rdx_record_service_device_toggle_core(scene);
}

rdx_err_t rdx_record_service_device_toggle(rdx_record_scene_t scene)
{
	u16 con_hdl = rdx_ble_server_get_conn_handle();
	u8 legacy_scene;

	switch (scene) {
	case RDX_RECORD_SCENE_CHAT:
		legacy_scene = RECORD_SCENE_CHAT;
		break;
	case RDX_RECORD_SCENE_CALL:
		legacy_scene = RECORD_SCENE_CALL;
		break;
	default:
		return RDX_ERR_INVAL;
	}
	if (0xffff != con_hdl && 0 != con_hdl && get_ota_status()) {
		return RDX_ERR_BUSY;
	}
	return rdx_record_service_device_toggle_core(legacy_scene);
}

/* ---- record mode ---- */

u8 rdx_record_service_get_mode(void)
{
	return g_record_mode;
}

void rdx_record_service_set_mode(u8 d)
{
	g_record_mode = d;
}

void rdx_record_service_mode_active_check(bool show)
{
	rdx_record_domain_state_t state;
	bool mode_changed;
	u16 con_hdl = rdx_ble_server_get_conn_handle();

	(void)show;
	if (rdx_record_domain_mode_active_check(&mode_changed, &state) != RDX_OK) {
		return;
	}
	if (mode_changed) {
		g_record_mode  = RDX_RECORD_CHANNAL_DUAL;
	}
	if (0xffff != con_hdl && 0 != con_hdl && rdx_protocol_get_indicate_ops()) {
		u8 scene = (state.scene == RECORD_SCENE_CALL) ? 1 : 0;
		rdx_protocol_get_indicate_ops()->record_mode_indicate(scene, state.run);
	}
}

/* ---- record switch ---- */

static rdx_err_t rdx_record_service_switch_core(u8 original_scene)
{
	u16 con_hdl = rdx_ble_server_get_conn_handle();
	bool trigger_required;
	rdx_record_domain_state_t current;
	rdx_record_domain_state_t trigger;
	rdx_record_trigger_payload_t payload;
	rdx_err_t trigger_ret = RDX_OK;
	rdx_err_t timer_ret;
	const RdxProtocolIndicateOps *ops;

	if (rdx_dut_is_in_mode() || get_ota_status()) {
		return RDX_ERR_BUSY;
	}

	ops = rdx_protocol_get_indicate_ops();
	if (ops && rdx_record_domain_get_state(&current) == RDX_OK) {
		u8 scene = (current.scene == RECORD_SCENE_CALL) ? 1 : 0;
		ops->record_mode_indicate(scene, current.run);
	}

	rdx_app_switch_keep_timer_restart();
	if (rdx_record_domain_prepare_switch_compat(
		original_scene,
		0xffff != con_hdl && 0 != con_hdl,
		&trigger_required,
		&trigger) != RDX_OK) {
		return RDX_ERR_INVAL;
	}
	if (!trigger_required) {
		return RDX_OK;
	}

	payload = rdx_record_service_trigger_payload(
		RDX_RECORD_TRIGGER_PAYLOAD_SWITCH, &trigger);
	trigger_ret = rdx_record_protocol_post_trigger(&payload);
	if (trigger_ret == RDX_ERR_NOMEM) {
		RDX_LOGW("record_svc rp pool full (switch)");
		return trigger_ret;
	}
	if (trigger_ret != RDX_OK) {
		RDX_LOGW("record_svc switch indicate post fail");
	}
	timer_ret = rdx_os_task_post_callback(
		"app_core",
		(void (*)(void *))rdx_app_switch_keep_timer_start,
		NULL);
	return trigger_ret != RDX_OK ? trigger_ret : timer_ret;
}

void rdx_record_service_switch(u8 orig_scene)
{
	(void)rdx_record_service_switch_core(orig_scene);
}

rdx_err_t rdx_record_service_switch_scene(rdx_record_scene_t original_scene)
{
	u8 legacy_scene;

	switch (original_scene) {
	case RDX_RECORD_SCENE_CHAT:
		legacy_scene = RECORD_SCENE_CHAT;
		break;
	case RDX_RECORD_SCENE_CALL:
		legacy_scene = RECORD_SCENE_CALL;
		break;
	default:
		return RDX_ERR_INVAL;
	}
	if (rdx_dut_is_in_mode() || get_ota_status()) {
		return RDX_ERR_BUSY;
	}
	return rdx_record_service_switch_core(legacy_scene);
}

/* ---- BLE mode helpers ---- */

void rdx_record_service_set_mode_online(void)
{
	(void)rdx_record_service_set_path(RDX_RECORD_PATH_ONLINE);
}

void rdx_record_service_set_mode_offline(void)
{
	(void)rdx_record_service_set_path(RDX_RECORD_PATH_OFFLINE);
}

rdx_err_t rdx_record_service_handle_ble_disconnected(void)
{
	return rdx_record_domain_handle_ble_disconnected(
		RDX_RECORD_DISCONNECT_TO_OFFLINE != 0,
		RDX_RECORD_DISCONNECT_RERUN != 0);
}

rdx_err_t rdx_record_service_sync_state_after_ble_write_ready(void)
{
	bool running;
	rdx_err_t ret = rdx_record_domain_get_running(&running);

	if (ret != RDX_OK) {
		return ret;
	}

	if (running) {
		y_printf("====== %s --> sync record state to app \r", __func__);
		rdx_protocol_record_state_indicate();
	} else {
		r_printf("====== %s --> record not running, skip sync \r", __func__);
	}

	rdx_record_service_mode_active_check(false);
	return RDX_OK;
}

void rdx_record_service_start(u8 mode)
{
	(void)mode;
}

void rdx_record_service_stop(void)
{
}

u8 rdx_record_service_get_state(void)
{
	return 0;
}

rdx_err_t rdx_record_service_stop_from_ble(void)
{
	rdx_err_t ret = rdx_record_domain_stop_running_now(
		RDX_RECORD_STOP_BLE_DISCONNECT);

	return ret == RDX_ERR_INVAL ? RDX_OK : ret;
}
