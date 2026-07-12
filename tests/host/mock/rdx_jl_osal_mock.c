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
    (void)argc;

    if (type == Q_CALLBACK) {
        /* JL Q_CALLBACK convention: argv[0]=cb, argv[1]=arg_count, argv[2..]=args */
        if (g_os_taskq_store_mode) {
            if (argc >= 2 && argv) {
                g_os_taskq_last_arg0 = argv[0];
                g_os_taskq_last_arg1 = argv[1]; /* arg_count */
                g_os_taskq_store_count++;
            }
            return 0;
        }
        if (argc >= 2 && argv && argv[0]) {
            int n = argv[1];
            if (n == 0) {
                ((void (*)(void))argv[0])();
            } else if (n == 1) {
                ((void (*)(void *))argv[0])((void *)argv[2]);
            } else if (n == 2) {
                ((void (*)(void *, void *))argv[0])((void *)argv[2], (void *)argv[3]);
            }
        }
        return 0;
    }

    /* Q_MSG etc. — backwards compatible */
    if (g_os_taskq_store_mode) {
        if (argc >= 2 && argv) {
            g_os_taskq_last_arg0 = argv[0];
            g_os_taskq_last_arg1 = argv[1];
            g_os_taskq_store_count++;
        }
        return 0;
    }

    return 0;
}

/* P2: OSAL Q_CALLBACK wrappers — delegate to os_taskq_post_type mock */

rdx_err_t rdx_os_task_post_callback0(const char *task_name,
                                     void (*callback)(void))
{
    int msg[3];
    msg[0] = (int)callback;
    msg[1] = 0;
    msg[2] = 0;
    return os_taskq_post_type(task_name, Q_CALLBACK, 3, msg) == 0
           ? RDX_OK : RDX_ERR_IO;
}

rdx_err_t rdx_os_task_post_callback1(const char *task_name,
                                     void (*callback)(void *),
                                     void *arg)
{
    int msg[3];
    msg[0] = (int)callback;
    msg[1] = 1;
    msg[2] = (int)arg;
    return os_taskq_post_type(task_name, Q_CALLBACK, 3, msg) == 0
           ? RDX_OK : RDX_ERR_IO;
}

rdx_err_t rdx_os_task_post_callback2(const char *task_name,
                                     void (*callback)(void *, void *),
                                     void *arg1, void *arg2)
{
    int msg[4];
    msg[0] = (int)callback;
    msg[1] = 2;
    msg[2] = (int)arg1;
    msg[3] = (int)arg2;
    return os_taskq_post_type(task_name, Q_CALLBACK, 4, msg) == 0
           ? RDX_OK : RDX_ERR_IO;
}

rdx_err_t rdx_os_task_post_callback(const char *task_name,
                                    void (*callback)(void *),
                                    void *arg)
{
    return rdx_os_task_post_callback1(task_name, callback, arg);
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

rdx_timer_t rdx_os_timer_periodic_add(void (*cb)(void *), void *priv, u32 period_ms)
{
    (void)cb; (void)priv; (void)period_ms;
    return 2;
}

void rdx_os_timer_periodic_del(rdx_timer_t id)
{
    (void)id;
}

rdx_timer_t rdx_os_timer_add_to_task(const char *task_name,
                                     void (*cb)(void *),
                                     void *priv, u32 timeout_ms)
{
    (void)task_name; (void)cb; (void)priv; (void)timeout_ms;
    return 3;
}
