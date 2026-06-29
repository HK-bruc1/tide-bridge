#include "rdx_clock_service.h"
#include "system/includes.h"
#include "clock_manager/clock_manager.h"
#include "rdx_log.h"

/*
 * Clock lock service — Stage 5 extracted from rdx_app.c D-class candidates.
 * Manages a single clock lock flag and an optional auto-unlock timer.
 * The lock prevents JL SDK clock gating during critical sections.
 */

static bool g_clock_lock_flag  = FALSE;
static u16  g_clock_lock_timer = 0;

static void rdx_clock_service_unlock_timer_cb(void *priv)
{
	rdx_clock_service_unlock_with_timer((const char *)priv);
}

bool rdx_clock_service_is_locked(void)
{
	return g_clock_lock_flag;
}

void rdx_clock_service_unlock(const char *task_name)
{
	int ret;
	if (g_clock_lock_flag) {
		g_clock_lock_flag = FALSE;
		ret = clock_unlock((char *)task_name);
		log_info("====== %s, ret = %d \n", __func__, ret);
	} else {
		log_info("====== %s, rdx_clock_lock_flag = %d, no need to unlock! \n",
		         __func__, g_clock_lock_flag);
	}
}

void rdx_clock_service_lock(const char *task_name, int clk)
{
	int ret;
	if (g_clock_lock_flag == FALSE) {
		g_clock_lock_flag = TRUE;
		ret = clock_lock(task_name, clk);
		log_info("====== %s, ret = %d \n", __func__, ret);
	} else {
		log_info("====== %s, rdx_clock_lock_flag = %d, clk has been locked already! \n",
		         __func__, g_clock_lock_flag);
	}
}

void rdx_clock_service_unlock_with_timer(const char *task_name)
{
	int ret = 0;
	if (g_clock_lock_timer != 0) {
		ret = clock_unlock((char *)task_name);
		sys_timeout_del(g_clock_lock_timer);
		g_clock_lock_timer = 0;
	}
	log_info("====== %s, ret = %d \n", __func__, ret);
}

void rdx_clock_service_lock_with_timer(const char *task_name, int clk)
{
	int ret;
	if (g_clock_lock_timer) {
		sys_timer_re_run(g_clock_lock_timer);
		return;
	}
	ret = clock_lock(task_name, clk);
	g_clock_lock_timer = sys_timeout_add(NULL, rdx_clock_service_unlock_timer_cb, 5000);
	log_info("====== %s, ret = %d \r", __func__, ret);
}
