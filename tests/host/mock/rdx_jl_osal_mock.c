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

/* Synchronous mock: immediately invoke the callback with its private arg.
 * This makes async publish deterministic in host tests. */
int os_taskq_post_type(const char *name, int type, int argc, int *argv)
{
    (void)name;
    (void)type;
    (void)argc;

    if (argc >= 2 && argv) {
        void (*cb)(void *) = (void (*)(void *))argv[0];
        void *priv = (void *)argv[1];
        if (cb) {
            cb(priv);
        }
    }
    return 0;
}
