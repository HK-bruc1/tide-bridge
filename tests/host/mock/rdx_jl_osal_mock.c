#include "system/includes.h"
#include "rdx_event_bus.h"
#include "rdx_jl_osal.h"

/* Host mock for JL OS primitives used by rdx_event_bus.c */

static int g_critical_count = 0;

void CPU_CRITICAL_ENTER(void)
{
    g_critical_count++;
}

void CPU_CRITICAL_EXIT(void)
{
    if (g_critical_count > 0) {
        g_critical_count--;
    }
}

/*
 * Store-mode globals for async-pool-full testing.
 * When g_os_taskq_store_mode != 0, os_taskq_post_type stores the args
 * instead of executing the callback, so pool slots stay busy.
 */
int g_os_taskq_store_mode  = 0;
int g_os_taskq_store_count = 0;
int g_os_taskq_last_arg0   = 0;
int g_os_taskq_last_arg1   = 0;

int os_taskq_post_type(const char *name, int type, int argc, int *argv)
{
    (void)name;
    (void)type;
    (void)argc;

    if (g_os_taskq_store_mode) {
        if (argc >= 2 && argv) {
            g_os_taskq_last_arg0 = argv[0];
            g_os_taskq_last_arg1 = argv[1];
            g_os_taskq_store_count++;
        }
        return 0;
    }

    /* Synchronous mock: immediately invoke the callback with its private arg.
     * This makes async publish deterministic in host tests. */
    if (argc >= 2 && argv) {
        void (*cb)(void *) = (void (*)(void *))argv[0];
        void *priv = (void *)argv[1];
        if (cb) {
            cb(priv);
        }
    }
    return 0;
}

/* P2: OSAL wrappers — delegate to os_taskq_post_type mock */

rdx_err_t rdx_os_task_post_callback(const char *task_name,
                                    void (*callback)(void *),
                                    void *arg)
{
    int msg[2];
    msg[0] = (int)callback;
    msg[1] = (int)arg;
    return os_taskq_post_type(task_name, Q_CALLBACK, 2, msg) == 0
           ? RDX_OK : RDX_ERR_IO;
}

rdx_err_t rdx_os_task_post_msg_array(const char *task_name, u32 msg_type,
                                     u32 argc, int *argv)
{
    return os_taskq_post_type(task_name, (int)msg_type, (int)argc, argv) == 0
           ? RDX_OK : RDX_ERR_IO;
}

/* Timer stubs — not exercised by current test paths */

rdx_timer_t rdx_os_timer_add(void (*cb)(void *), void *priv, u32 timeout_ms)
{
    (void)cb; (void)priv; (void)timeout_ms;
    return 1;
}

void rdx_os_timer_del(rdx_timer_t id)
{
    (void)id;
}

void rdx_os_timer_re_run(rdx_timer_t id)
{
    (void)id;
}
