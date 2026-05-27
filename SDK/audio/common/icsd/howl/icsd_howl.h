#ifndef _ICSD_HOWL_H
#define _ICSD_HOWL_H

#include "generic/typedef.h"
#include "os/os_type.h"
#include "os/os_api.h"
#include "timer.h"
#include "math.h"
#include "icsd_common.h"
#include "icsd_common_v2.h"

#if 0
#define _howl_printf printf
#else
#define _howl_printf icsd_printf_off
#endif
extern int (*howl_printf)(const char *format, ...);

struct icsd_howl_libfmt {
    int lib_alloc_size;  //算法ram需求大小
};

struct icsd_howl_infmt {
    void *alloc_ptr;     //外部申请的ram地址
    int lib_alloc_size;  //算法ram需求大小
};

typedef struct {
    float hd_scale;
    int   hd_sat_thr;
    float hd_det_thr;
    float hd_diff_pwr_thr;
    int   hd_maxind_thr1;
    int   hd_maxind_thr2;
    float param1;
    float param2;
    float param3;
    float param4;
    float param5;
    float param6;
} __howl_config;

struct howl_function {
    void (*howl_config_init)(__howl_config *_howl_config);
};
extern struct howl_function *HOWL_FUNC;

typedef struct {
    s16 *ref;
    s16 *anco;
} __icsd_howl_run_parm;

typedef struct {
    u8 howl_output;
} __icsd_howl_output;

void icsd_howl_get_libfmt(struct icsd_howl_libfmt *libfmt);
void icsd_howl_set_infmt(struct icsd_howl_infmt *fmt);
void icsd_alg_howl_run(__icsd_howl_run_parm *run_parm, __icsd_howl_output *output);



#endif
