#ifndef __RDX_OPS_H__
#define __RDX_OPS_H__

#include "typedef.h"
#include "rdx_err.h"

/*
 * 小 vtable 合约定义。
 *
 * 阶段 3 落地 P0 级 vtable（rdx_time_ops_t / rdx_lifecycle_ops_t）；
 * P1/P2 级在本阶段只定义结构体，供后续阶段填空。
 */

/* --- P0: 时间操作 vtable ------------------------------------------------ */
typedef struct {
    rdx_err_t (*set_time)(u32 timestamp);
    u32       (*get_time)(void);
    int       (*is_leap_year)(int year);
    int       (*days_in_month)(int year, int month);
    u8        is_hw_rtc;  /* 1 = hardware RTC, 0 = software */
} rdx_time_ops_t;

/* --- P0: 芯片生命周期 vtable -------------------------------------------- */
typedef struct {
    void (*early_init)(void);
    int  (*pre_sleep)(void);
    void (*post_wakeup)(void);
    void (*pre_poweroff)(void);
} rdx_lifecycle_ops_t;

/* --- P1: WiFi transport vtable — Stage 4 落地实现 ------------------------ */
typedef struct {
    int  (*open)(void *cfg);
    int  (*close)(void);
    int  (*control)(u32 cmd, void *arg);
    int  (*tx)(const u8 *data, u32 len);
    int  (*rx_done)(void);
} rdx_wifi_transport_ops_t;

/* WiFi transport control commands (cmd argument to ops->control) */
#define RDX_WIFI_CTRL_POWERON_TIMER_CANCEL       1
#define RDX_WIFI_CTRL_DATA_TRANSFER_TIMER_STOP   2
#define RDX_WIFI_CTRL_DATA_TRANSFER_TIMER_START  3

/* --- P1/P2: BLE transport vtable — 先定义，有真实调用点再落地 ---------- */
typedef struct {
    int  (*send)(const u8 *data, u32 len);
    u16  (*get_mtu)(void);
    int  (*is_connected)(void);
} rdx_ble_transport_ops_t;

/* --- 注册与校验 API ----------------------------------------------------- */

/* 获取当前板型的 ops 实例 */
const rdx_time_ops_t           *rdx_time_ops_get(void);
const rdx_lifecycle_ops_t      *rdx_lifecycle_ops_get(void);
const rdx_wifi_transport_ops_t *rdx_wifi_transport_ops_get(void);

/* 启动校验：关键函数指针为 NULL 时打印诊断并返回错误 */
rdx_err_t rdx_time_ops_validate(const rdx_time_ops_t *ops);
rdx_err_t rdx_lifecycle_ops_validate(const rdx_lifecycle_ops_t *ops);
rdx_err_t rdx_wifi_transport_ops_validate(const rdx_wifi_transport_ops_t *ops);

#endif
