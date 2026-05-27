#ifndef _ICSD_AVC_H
#define _ICSD_AVC_H

#include "generic/typedef.h"
#include "os/os_type.h"
#include "os/os_api.h"
#include "timer.h"
#include "math.h"
#include "icsd_common.h"
#include "icsd_common_v2.h"

#if 0
#define _avc_printf printf
#else
#define _avc_printf icsd_printf_off
#endif
extern int (*avc_printf)(const char *format, ...);

struct icsd_avc_libfmt {
    int lib_alloc_size;  //算法ram需求大小
    u8  type;
};

struct icsd_avc_infmt {
    void *alloc_ptr;     //外部申请的ram地址
    int lib_alloc_size;  //算法ram需求大小
    u8  type;
};

typedef struct {
    float alpha_db;
    float db_cali;
} __avc_config;

struct avc_function {
    void (*avc_config_init)(__avc_config *_avc_config);
    s16(*app_audio_get_volume)(u8 state);
};
extern struct avc_function *AVC_FUNC;

typedef struct {
    s16 *refmic;
    s16 *dac_data;
    u8  type;
} __icsd_avc_run_parm;

typedef struct {
    int ctl_lvl;
    float spldb_iir;
} __icsd_avc_output;


void icsd_avc_get_libfmt(struct icsd_avc_libfmt *libfmt);
void icsd_avc_set_infmt(struct icsd_avc_infmt *fmt);
void icsd_alg_avc_run(__icsd_avc_run_parm *run_parm, __icsd_avc_output *output);
void icsd_avc_ram_clean();
void icsd_avc_run(__icsd_avc_run_parm *_run_parm, __icsd_avc_output *_output);
void avc_config_update(__avc_config *_avc_config);

#endif
