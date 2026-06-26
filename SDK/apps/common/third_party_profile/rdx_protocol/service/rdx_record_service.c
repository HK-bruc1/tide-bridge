#include <stdbool.h>
#include "rdx_record_service.h"
#include "rdx_event_bus.h"
#include "system/includes.h"
#include "rdx_log.h"
#include "rdx_record.h"
#include "rdx_protocol.h"
#include "rdx_ble_server.h"
#include "rdx_dut.h"

/* symbols from librdxApp.a */
extern void rdx_protocol_record_trigger_indicate(RecordStatus *rp, u8 factor);
extern void rdx_util_str_hexstr2hexarray(u8 *str, u32 len, u8 *out);

/* symbols from rdx_app.c (thin wrappers, avoid circular dep) */
extern void rdx_app_switch_keep_timer_restart(void);
extern void rdx_app_switch_keep_timer_start(void);

/* Phase 2 local state moved from rdx_app.c */
static u16           g_upload_timer = 0;
static u8            g_record_mode  = RDX_RECORD_CHANNAL_SINGLE;

/*
 * 4-slot pool for protocol trigger indicate payload.
 * All 3 async paths post &slot to app_core via os_taskq_post_type;
 * the short critical section protects the slot index across contexts.
 */
#define RP_POOL_SIZE 4
static RecordStatus  g_rp_pool[RP_POOL_SIZE];
static u8            g_rp_idx;

static RecordStatus *rp_pool_alloc(void)
{
	RecordStatus *slot;
	CPU_CRITICAL_ENTER();
	slot = &g_rp_pool[g_rp_idx];
	g_rp_idx = (g_rp_idx + 1) % RP_POOL_SIZE;
	CPU_CRITICAL_EXIT();
	return slot;
}

void rdx_record_service_init(void)
{
	g_upload_timer = 0;
	g_record_mode  = RDX_RECORD_CHANNAL_SINGLE;
	g_rp_idx       = 0;
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
		sys_timeout_del(g_upload_timer);
		g_upload_timer = 0;
	}
}

void rdx_record_service_upload_timer_start(void)
{
	if (g_upload_timer == 0) {
		g_upload_timer = sys_timeout_add(NULL,
			rdx_record_service_upload_timer_cb, 3000);
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
			rp_slot->run    = rp->run;
			rp_slot->formate = rp->formate;
			rp_slot->scene  = rp->scene;
			{
				int msg[4];
				msg[0] = (int)rdx_protocol_record_trigger_indicate;
				msg[1] = 2;
				msg[2] = (int)rp_slot;
				msg[3] = 0;
				if (os_taskq_post_type("app_core", Q_CALLBACK, 4, msg))
					RDX_LOGW("record_svc upload indicate post fail");
			}
		} else {
			int arg[2];
			arg[0] = (int)rdx_record_process;
			arg[1] = 0;
			os_taskq_post_type("app_core", Q_CALLBACK, 2, arg);
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
					int msg_stop[2];
					msg_stop[0] = (int)rdx_record_process;
					msg_stop[1] = 0;
					os_taskq_post_type("app_core", Q_CALLBACK, 2, msg_stop);
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
		{
			int msg[4];
			msg[0] = (int)rdx_protocol_record_trigger_indicate;
			msg[1] = 2;
			msg[2] = (int)rp_slot;
			msg[3] = 0;
			if (os_taskq_post_type("app_core", Q_CALLBACK, 4, msg))
				RDX_LOGW("record_svc trigger_indicate post fail");
		}
	} else {
		if (rp->run == RECORD_STATE_STOP) {
			rp->run    = RECORD_STATE_START;
			rp->formate = formate;
			rp->scene  = scene;
			{
				int msg[2];
				msg[0] = (int)rdx_record_process;
				msg[1] = 0;
				os_taskq_post_type("app_core", Q_CALLBACK, 2, msg);
			}
		} else {
			rp->run = RECORD_STATE_STOP;
			{
				int msg[2];
				msg[0] = (int)rdx_record_process;
				msg[1] = 0;
				os_taskq_post_type("app_core", Q_CALLBACK, 2, msg);
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
				rp_slot->run    = RECORD_STATE_STOP;
				rp_slot->formate = rp->formate;
				rp_slot->scene  = orig_scene;

				int msg[4];
				msg[0] = (int)rdx_protocol_record_trigger_indicate;
				msg[1] = 2;
				msg[2] = (int)rp_slot;
				msg[3] = 0;
				if (os_taskq_post_type("app_core", Q_CALLBACK, 4, msg))
					RDX_LOGW("record_svc switch indicate post fail");
			}
			{
				int msg1[2];
				msg1[0] = (int)rdx_app_switch_keep_timer_start;
				msg1[1] = 0;
				os_taskq_post_type("app_core", Q_CALLBACK, 2, msg1);
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
