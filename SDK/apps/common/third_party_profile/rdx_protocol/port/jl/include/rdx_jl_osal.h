#ifndef __RDX_JL_OSAL_H__
#define __RDX_JL_OSAL_H__

#include "typedef.h"
#include "os/os_type.h"   /* OS_MUTEX / OS_SEM 真实类型，工程通用写法 */
#include "rdx_err.h"

/*
 * 阶段 1 的 public handle 直接基于 JL OS 类型，
 * 但只在 port/jl 内部包含 os_type.h，业务 core/service public header 禁止 include 本头。
 * 阶段 2/3 再考虑是否需要不透明包装。
 */
typedef OS_MUTEX *rdx_mutex_t;
typedef OS_SEM   *rdx_sem_t;
typedef int       rdx_timer_t;     /* JL sys_timeout 返回 int id */
typedef int       rdx_task_t;

/* 任务入口签名 */
typedef void (*rdx_task_entry_t)(void *arg);

/* mutex */
rdx_err_t rdx_os_mutex_create(rdx_mutex_t m);  /* m 指向已分配的 OS_MUTEX */
rdx_err_t rdx_os_mutex_lock(rdx_mutex_t m, u32 timeout_ms);
rdx_err_t rdx_os_mutex_unlock(rdx_mutex_t m);
void      rdx_os_mutex_destroy(rdx_mutex_t m);

/* sem — 阶段 1 可选实现，头文件先定义，阶段 2 补全 */
rdx_err_t rdx_os_sem_create(rdx_sem_t s, u32 initial);
rdx_err_t rdx_os_sem_wait(rdx_sem_t s, u32 timeout_ms);
rdx_err_t rdx_os_sem_post(rdx_sem_t s);
void      rdx_os_sem_destroy(rdx_sem_t s);

/* timer：timeout_ms = 0 表示立即触发 */
rdx_timer_t rdx_os_timer_add(void (*cb)(void *), void *priv, u32 timeout_ms);
void        rdx_os_timer_del(rdx_timer_t id);
void        rdx_os_timer_re_run(rdx_timer_t id);

/* task — 阶段 1 可选实现 */
rdx_err_t rdx_os_task_create(const char *name, rdx_task_entry_t entry, void *arg,
                             u32 stack_depth, u32 prio, rdx_task_t *task);
rdx_err_t rdx_os_task_post_msg(const char *task_name, u32 msg, u32 arg);

/*
 * Q_CALLBACK family — all delegates to os_taskq_post_type with JL convention:
 *   msg[0]=callback, msg[1]=arg_count, msg[2..]=args
 * Business code must use these instead of hand-rolling Q_CALLBACK msg[] arrays.
 */
rdx_err_t rdx_os_task_post_callback0(const char *task_name,
                                     void (*callback)(void));
rdx_err_t rdx_os_task_post_callback1(const char *task_name,
                                     void (*callback)(void *),
                                     void *arg);
rdx_err_t rdx_os_task_post_callback2(const char *task_name,
                                     void (*callback)(void *, void *),
                                     void *arg1, void *arg2);

/* Backwards compatibility — delegates to callback1 */
rdx_err_t rdx_os_task_post_callback(const char *task_name,
                                    void (*callback)(void *),
                                    void *arg);

/* task message array — P2: 封装任意 argc 的投递模式 */
rdx_err_t rdx_os_task_post_msg_array(const char *task_name, u32 msg_type,
                                     u32 argc, int *argv);

/* time */
u32  rdx_os_time_ms(void);
u32  rdx_os_time_tick(void);
void rdx_os_time_dly(u32 ticks);

#endif
