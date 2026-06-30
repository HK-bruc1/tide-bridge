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
    return sys_timeout_add(priv, cb, timeout_ms);
}

void rdx_os_timer_del(rdx_timer_t id)
{
    sys_timeout_del(id);
}

void rdx_os_timer_re_run(rdx_timer_t id)
{
    sys_timer_re_run(id);
}

/*----------------------------------------------------------------------------*/
/* task post                                                                   */
/*----------------------------------------------------------------------------*/
rdx_err_t rdx_os_task_post_callback0(const char *task_name,
                                     void (*callback)(void))
{
    int msg[3];
    msg[0] = (int)callback;
    msg[1] = 0;
    msg[2] = 0;
    int ret = os_taskq_post_type(task_name, Q_CALLBACK, 3, msg);
    return (ret == 0) ? RDX_OK : RDX_ERR_IO;
}

rdx_err_t rdx_os_task_post_callback1(const char *task_name,
                                     void (*callback)(void *),
                                     void *arg)
{
    int msg[3];
    msg[0] = (int)callback;
    msg[1] = 1;
    msg[2] = (int)arg;
    int ret = os_taskq_post_type(task_name, Q_CALLBACK, 3, msg);
    return (ret == 0) ? RDX_OK : RDX_ERR_IO;
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
    int ret = os_taskq_post_type(task_name, Q_CALLBACK, 4, msg);
    return (ret == 0) ? RDX_OK : RDX_ERR_IO;
}

rdx_err_t rdx_os_task_post_callback(const char *task_name,
                                    void (*callback)(void *),
                                    void *arg)
{
    return rdx_os_task_post_callback1(task_name, callback, arg);
}

rdx_err_t rdx_os_task_post_msg(const char *task_name, u32 msg, u32 arg)
{
    int ret = os_taskq_post_type(task_name, Q_MSG, 2, (int[]){ (int)msg, (int)arg });
    return (ret == 0) ? RDX_OK : RDX_ERR_IO;
}

rdx_err_t rdx_os_task_post_msg_array(const char *task_name, u32 msg_type,
                                     u32 argc, int *argv)
{
    int ret = os_taskq_post_type(task_name, msg_type, argc, argv);
    return (ret == 0) ? RDX_OK : RDX_ERR_IO;
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
