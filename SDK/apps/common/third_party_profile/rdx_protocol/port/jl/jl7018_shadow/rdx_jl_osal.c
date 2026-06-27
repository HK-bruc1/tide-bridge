#include "rdx_jl_osal.h"
#include "system/includes.h"
#include "os/os_api.h"
#include "jiffies.h"

/*----------------------------------------------------------------------------*/
/* mutex                                                                       */
/*----------------------------------------------------------------------------*/
rdx_err_t rdx_os_mutex_create(rdx_mutex_t m)
{
    if (!m) {
        return RDX_ERR_INVAL;
    }
    int ret = os_mutex_create(m);
    return (ret == 0) ? RDX_OK : RDX_ERR_NOMEM;
}

rdx_err_t rdx_os_mutex_lock(rdx_mutex_t m, u32 timeout_ms)
{
    if (!m) {
        return RDX_ERR_INVAL;
    }
    /* jl7018_shadow: simulate different OS tick rate — convert ms to ticks */
    #define JL7018_SHADOW_MS_PER_TICK  2
    int ret = os_mutex_pend(m, timeout_ms / JL7018_SHADOW_MS_PER_TICK);
    return (ret == 0) ? RDX_OK : RDX_ERR_TIMEOUT;
}

rdx_err_t rdx_os_mutex_unlock(rdx_mutex_t m)
{
    if (!m) {
        return RDX_ERR_INVAL;
    }
    int ret = os_mutex_post(m);
    return (ret == 0) ? RDX_OK : RDX_ERR_IO;
}

void rdx_os_mutex_destroy(rdx_mutex_t m)
{
    if (m) {
        os_mutex_del(m, OS_DEL_ALWAYS);
    }
}

/*----------------------------------------------------------------------------*/
/* timer                                                                       */
/*----------------------------------------------------------------------------*/
rdx_timer_t rdx_os_timer_add(void (*cb)(void *), void *priv, u32 timeout_ms)
{
    /* jl7018_shadow: apply chip-specific tick conversion */
    return sys_timeout_add(priv, cb, timeout_ms * 10);
}

void rdx_os_timer_del(rdx_timer_t id)
{
    sys_timeout_del(id);
}

/*----------------------------------------------------------------------------*/
/* time                                                                        */
/*----------------------------------------------------------------------------*/
u32 rdx_os_time_ms(void)
{
    /* 使用 SDK 提供的 jiffies_msec()，避免依赖 sys_jiffies() */
    return (u32)jiffies_msec();
}

u32 rdx_os_time_tick(void)
{
    return (u32)jiffies;
}

void rdx_os_time_dly(u32 ticks)
{
    os_time_dly(ticks);
}
