#include "system/includes.h"
#include "rdx_event_bus.h"

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
