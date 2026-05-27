#ifndef _ICSD_ADJDCC_H
#define _ICSD_ADJDCC_H

/*
算法库要求：
1.头文件只能包含基础头文件，不能包含其他库或功能性头文件
2.库内所有打印可以通过该头文件进行开关,库里面不允许直接使用printf
3.算法不允许内部申请资源，所有资源由外部申请分配给算法使用由外部并释放,debug空间除外
4.上层通过icsd_demo_get_libfmt函数获取算法资源需求
5.上层通过icsd_demo_set_infmt 函数进行算法配置,算法获取到的配置信息需自行保存，外部空间会释放
  算法在该函数内进行相关初始化
6.上层通过icsd_demo_run(__icsd_demo_run_parm *run_parm,__icsd_demo_output *output) 函数启动算法
  上层通过run_parm将算法需要的参数传给run函数
  算法直接将输出结果写道output指针
7.算法内所有阈值等参数需要放在__demo_config,通过demo_config_init()函数初始化
8.算法内需要调用的外部函数必须通过函数指针调用，通过demo_function_init()函数初始化
9.多个库必须都要看到的定义放在common
*/
#include "generic/typedef.h"
#include "os/os_type.h"
#include "os/os_api.h"
#include "timer.h"
#include "math.h"
#include "icsd_common.h"
#include "icsd_common_v2.h"

#if 0
#define _dcc_printf printf                  //打开自适应DCC算法库信息
#else
#define _dcc_printf icsd_printf_off
#endif
extern int (*dcc_printf)(const char *format, ...);

struct icsd_adjdcc_libfmt {
    int lib_alloc_size;  //算法ram需求大小
};

struct icsd_adjdcc_infmt {
    void *alloc_ptr;     //外部申请的ram地址
    int lib_alloc_size;  //算法ram需求大小
    u8 TOOL_FUNCTION;
};

typedef struct {
    s16 *refmic;
    s16 *errmic;
    u16 len;
} __icsd_adjdcc_run_parm;

typedef struct {
    u8 result;
    u8 de_task;
} __icsd_adjdcc_output;

typedef struct {
    u8 idx;
    u8 ff_dc_par;
    u8 release_cnt;
    u8 steps;
    u8 wind_lvl_thr;

    u16 refmic_max_thr;
    u16 refmic_mp_thr;

    float iir_coef;
    float *thr_list_up;
    float *thr_list_down;

    float *err_overload_list;
    float *param_table;

} __adjdcc_config;

struct adjdcc_function {
    void (*adjdcc_config_init)(__adjdcc_config *_adjdcc_config);
    void (*ff_adjdcc_par_set)(u8 dc_par);
    void (*rtanc_adjdcc_flag_set)(u8 flag);
    u8(*adjdcc_trigger_update)(u8 env_level, float *table);
    u8(*rtanc_adjdcc_flag_get)();
    u8(*get_wind_lvl)();
};
extern struct adjdcc_function *ADJDCC_FUNC;

void icsd_adjdcc_get_libfmt(struct icsd_adjdcc_libfmt *libfmt);
void icsd_adjdcc_set_infmt(struct icsd_adjdcc_infmt *fmt);
void icsd_alg_adjdcc_run(__icsd_adjdcc_run_parm *run_parm, __icsd_adjdcc_output *output);

#endif
