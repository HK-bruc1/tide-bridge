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
#include "rdx_uxfile.h"
#include "rdx_time_service.h"

/* BLE event business logic — Stage 4 cutover from rdx_ble_service.c */
extern void rdx_record_stream_interrupt(void);
extern void rdx_record_stream_resume_delayed(void);
extern void rdx_protocol_uploadFileInfo_clean(void);
extern void rdx_protocol_file_sync_busy_timer_stop(void);

/* symbols from librdxApp.a */
extern void rdx_protocol_record_trigger_indicate(RecordStatus *rp, u8 factor);
extern void rdx_protocol_record_state_indicate(void);
extern void rdx_util_str_hexstr2hexarray(u8 *str, u32 len, u8 *out);

/* symbols from rdx_app.c (thin wrappers, avoid circular dep) */
extern void rdx_app_switch_keep_timer_restart(void);
extern void rdx_app_switch_keep_timer_start(void);

/* Phase 2 local state moved from rdx_app.c */
static u16           g_upload_timer = 0;
static u8            g_record_mode  = RDX_RECORD_CHANNAL_SINGLE;

/*
 * 4-slot busy pool for protocol trigger indicate payload.
 * Slots are marked busy in alloc and released in the callback wrapper
 * (rpx_pool_cb) after rdx_protocol_record_trigger_indicate returns,
 * or on post failure.
 */
#define RP_POOL_SIZE 4
static RecordStatus  g_rp_pool[RP_POOL_SIZE];
static u8            g_rp_busy[RP_POOL_SIZE];

static RecordStatus *rp_pool_alloc(void)
{
	u8 i;
	CPU_CRITICAL_ENTER();
	for (i = 0; i < RP_POOL_SIZE; i++) {
		if (!g_rp_busy[i]) {
			g_rp_busy[i] = 1;
			CPU_CRITICAL_EXIT();
			return &g_rp_pool[i];
		}
	}
	CPU_CRITICAL_EXIT();
	return NULL;
}

static void rp_pool_release(RecordStatus *slot)
{
	u8 idx = (u8)(slot - g_rp_pool);
	CPU_CRITICAL_ENTER();
	g_rp_busy[idx] = 0;
	CPU_CRITICAL_EXIT();
}

static void rpx_pool_cb(void *p1, void *p2)
{
	RecordStatus *rp_slot = (RecordStatus *)p1;
	u8 factor = (u8)(u32)p2;
	rdx_protocol_record_trigger_indicate(rp_slot, factor);
	rp_pool_release(rp_slot);
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
        rdx_protocol_send_buffer_reinit();
        rdx_record_on_ble_conn_changed(1);
        rdx_record_stream_resume_delayed();
    } else if (event == RDX_EVENT_BLE_DISCONNECTED) {
        rdx_record_stream_interrupt();
        rdx_record_on_ble_conn_changed(0);
        rdx_record_process();
        rdx_protocol_uploadFileInfo_clean();
        rdx_uxfile_recordFileData_sendBuf_free();
        rdx_protocol_file_sync_busy_timer_stop();
        rdx_protocol_send_buffer_reinit();
    }
}

static void rdx_record_on_time_event(rdx_event_id_t event, void *payload, u32 len, void *user_ctx)
{
	(void)user_ctx;
	if (event != RDX_EVENT_TIME_SYNCED || !payload || len < sizeof(rdx_time_sync_event_t)) {
		return;
	}

	const rdx_time_sync_event_t *sync = (const rdx_time_sync_event_t *)payload;
	RecordStatus *rp = rdx_record_get_status();
	if (!rp || (rp->run != RECORD_STATE_START && rp->run != RECORD_STATE_RESUME)) {
		return;
	}

	uxfile_data_t *op = rdx_uxfile_get_operateFile_info();
	if (op && op->start_time > 0) {
		u32 corrected = (u32)((int)op->start_time + sync->delta);
		y_printf("[RTC_SYNC] Recording active, fix start_time: %u -> %u (delta=%d)\r",
		         op->start_time, corrected, sync->delta);
		op->start_time = corrected;
	}
}

/* ---- migrated handlers (Stage 4 from rdx_app.c) ---- */

static void rdx_cmd_handle_record_mode_query(ProtocolEvents event, void *data, u32 len)
{
	(void)event; (void)data; (void)len;
	const RdxProtocolIndicateOps *ops = rdx_protocol_get_indicate_ops();
	if (!ops) return;
	RecordStatus *rp_sw = rdx_record_get_status();
	u8 scene = (rp_sw->scene == RECORD_SCENE_CALL) ? 1 : 0;
	g_printf("[APP CMD] record_mode (scene=%d, run=%d)\r", scene, rp_sw->run);
	ops->record_mode_indicate(scene, rp_sw->run);
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
	memset(g_rp_busy, 0, sizeof(g_rp_busy));
	memset(g_rp_pool, 0, sizeof(g_rp_pool));
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
	RecordStatus *rp = rdx_record_get_status();

	(void)priv;
	rdx_record_service_upload_timer_stop();

	if (RECORD_STATE_START == rp->run || RECORD_STATE_RESUME == rp->run) {
		u16 con_hdl = rdx_ble_server_get_conn_handle();
		rp->run = RECORD_STATE_STOP;
		if (0xffff != con_hdl && 0 != con_hdl) {
			RecordStatus *rp_slot = rp_pool_alloc();
			if (rp_slot == NULL) {
				RDX_LOGW("record_svc rp pool full (upload)");
				return;
			}
			rp_slot->run    = rp->run;
			rp_slot->formate = rp->formate;
			rp_slot->scene  = rp->scene;
			if (rdx_os_task_post_callback2("app_core",
			     rpx_pool_cb, rp_slot, NULL) != RDX_OK) {
				rp_pool_release(rp_slot);
				RDX_LOGW("record_svc upload indicate post fail");
			}
		} else {
			rdx_os_task_post_callback("app_core",
			     (void (*)(void *))rdx_record_process, NULL);
		}
	}
}

/* ---- device record handle (was rdx_app_device_record_handle) ---- */

void rdx_record_service_device_record_handle(u8 scene)
{
	u8  formate = 0;
	u16 con_hdl = rdx_ble_server_get_conn_handle();
	RecordStatus *rp = rdx_record_get_status();

	if (scene == RECORD_SCENE_CHAT) {
		formate = RECORD_FORMATE_OPUS_16K_STERO;
	} else if (scene == RECORD_SCENE_CALL) {
		formate = RECORD_FORMATE_OPUS_16K_STERO;
	} else {
		return;
	}

	if (0xffff != con_hdl && 0 != con_hdl) {
		if (get_ota_status()) {
			return;
		}

		RecordStatus *rp_slot = rp_pool_alloc();
		if (rp_slot == NULL) {
			RDX_LOGW("record_svc rp pool full (dev_rec)");
			return;
		}
		memset(rp_slot, 0, sizeof(RecordStatus));
		if (rp->run == RECORD_STATE_STOP) {
			rp_slot->run    = RECORD_STATE_START;
			rp_slot->formate = formate;
			rp_slot->scene  = scene;
			rp_slot->mode   = rp->mode;

			rdx_record_service_upload_timer_start();
		} else {
			if (rp->orig_mode == RECORD_MODE_OFFLINE) {
				rp->run = RECORD_STATE_STOP;
				{
					rdx_os_task_post_callback("app_core",
					     (void (*)(void *))rdx_record_process, NULL);
				}
				rp_slot->run    = RECORD_STATE_STOP;
				rp_slot->formate = formate;
				rp_slot->scene  = scene;
				rp_slot->mode   = rp->mode;
			} else {
				rp_slot->run    = RECORD_STATE_STOP;
				rp_slot->formate = rp->formate;
				rp_slot->scene  = rp->scene;
				rp_slot->mode   = rp->mode;
			}
		}
		if (rdx_os_task_post_callback2("app_core",
		     rpx_pool_cb, rp_slot, NULL) != RDX_OK) {
			rp_pool_release(rp_slot);
			RDX_LOGW("record_svc trigger_indicate post fail");
		}
	} else {
		if (rp->run == RECORD_STATE_STOP) {
			rp->run    = RECORD_STATE_START;
			rp->formate = formate;
			rp->scene  = scene;
			{
				rdx_os_task_post_callback("app_core",
				     (void (*)(void *))rdx_record_process, NULL);
			}
		} else {
			rp->run = RECORD_STATE_STOP;
			{
				rdx_os_task_post_callback("app_core",
				     (void (*)(void *))rdx_record_process, NULL);
			}
		}
	}
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
	RecordStatus *rp = rdx_record_get_status();
	u16 con_hdl = rdx_ble_server_get_conn_handle();

	(void)show;
	if (rp->run == RECORD_STATE_STOP) {
		rp->scene      = RECORD_SCENE_CALL;
		g_record_mode  = RDX_RECORD_CHANNAL_DUAL;
		rp->orig_scene = rp->scene;
	}
	if (0xffff != con_hdl && 0 != con_hdl && rdx_protocol_get_indicate_ops()) {
		u8 scene = (rp->scene == RECORD_SCENE_CALL) ? 1 : 0;
		rdx_protocol_get_indicate_ops()->record_mode_indicate(scene, rp->run);
	}
}

/* ---- record switch ---- */

void rdx_record_service_switch(u8 orig_scene)
{
	u16 con_hdl = rdx_ble_server_get_conn_handle();

	if (rdx_dut_is_in_mode() || get_ota_status()) {
		return;
	}

	/* indicate current mode */
	{
		const RdxProtocolIndicateOps *ops = rdx_protocol_get_indicate_ops();
		if (ops) {
			RecordStatus *rp_cur = rdx_record_get_status();
			u8 scene = (rp_cur->scene == RECORD_SCENE_CALL) ? 1 : 0;
			ops->record_mode_indicate(scene, rp_cur->run);
		}
	}

	rdx_app_switch_keep_timer_restart();

	{
		RecordStatus *rp = rdx_record_get_status();
		if (rp->run != RECORD_STATE_STOP) {
			rp->noshow = 1;
			if (0xffff == con_hdl || 0 == con_hdl) {
				rp->run = RECORD_STATE_STOP;
				rdx_record_process();
			}

			rp->is_switch         = 1;
			rp->switch_orig_scene = orig_scene;

			{
				RecordStatus *rp_slot = rp_pool_alloc();
				if (rp_slot == NULL) {
					RDX_LOGW("record_svc rp pool full (switch)");
					return;
				}
				rp_slot->run    = RECORD_STATE_STOP;
				rp_slot->formate = rp->formate;
				rp_slot->scene  = orig_scene;

				if (rdx_os_task_post_callback2("app_core",
				     rpx_pool_cb, rp_slot, NULL) != RDX_OK) {
					rp_pool_release(rp_slot);
					RDX_LOGW("record_svc switch indicate post fail");
				}
			}
			{
				rdx_os_task_post_callback("app_core",
				     (void (*)(void *))rdx_app_switch_keep_timer_start, NULL);
			}
		}
	}
}

/* ---- BLE mode helpers ---- */

void rdx_record_service_set_mode_online(void)
{
	RecordStatus *rp = rdx_record_get_status();
	rp->mode = RECORD_MODE_ONLINE;
	if (rp->run == RECORD_STATE_STOP) {
		rp->orig_mode = RECORD_MODE_ONLINE;
	}
}

void rdx_record_service_set_mode_offline(void)
{
	RecordStatus *rp = rdx_record_get_status();
	if (rp->orig_mode != RECORD_MODE_OFFLINE) {
		rp->mode      = RECORD_MODE_OFFLINE;
		rp->orig_mode = RECORD_MODE_OFFLINE;
	}
}

bool rdx_record_service_is_running(void)
{
	RecordStatus *rp = rdx_record_get_status();
	return rp && (rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME);
}

bool rdx_record_service_can_auto_shutdown(void)
{
	RecordStatus *rp = rdx_record_get_status();
	return rp && rp->run == RECORD_STATE_STOP;
}

rdx_err_t rdx_record_service_handle_ble_disconnected(void)
{
	RecordStatus *rp = rdx_record_get_status();

	if (!rp) {
		return RDX_ERR_INVAL;
	}

#if RDX_RECORD_DISCONNECT_TO_OFFLINE
	rdx_record_service_set_mode_offline();
#else
	if (rp->orig_mode != RECORD_MODE_OFFLINE) {
		if (rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME) {
#if RDX_RECORD_DISCONNECT_RERUN
			rp->rerun = true;
#endif
			return rdx_record_service_stop_from_ble();
		}
	}
#endif

	return RDX_OK;
}

rdx_err_t rdx_record_service_sync_state_after_ble_write_ready(void)
{
	RecordStatus *rp = rdx_record_get_status();

	if (!rp) {
		return RDX_ERR_INVAL;
	}

	if (rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME) {
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

u8 rdx_record_service_is_active(void)
{
	return 0;
}

rdx_err_t rdx_record_service_stop_from_ble(void)
{
	RecordStatus *rp = rdx_record_get_status();
	if (rp && (rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME)) {
		rp->run = RECORD_STATE_STOP;
		rdx_record_process();
	}
	return RDX_OK;
}
