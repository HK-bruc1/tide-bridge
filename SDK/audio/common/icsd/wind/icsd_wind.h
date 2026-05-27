#ifndef _ICSD_WIND_H
#define _ICSD_WIND_H

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
#define _win_printf printf                  //打开智能免摘库打印信息
#else
#define _win_printf icsd_printf_off
#endif
extern int (*win_printf)(const char *format, ...);





//以下SDK的宏定义放到sdk外面
#define SDK_WIND_PHONE_TYPE  ICSD_WIND_TWS
#define SDK_WIND_MIC_TYPE    ICSD_WIND_LFF_TALK//ICSD_WIND_LFF_RFF

extern const u8 ICSD_WIND_PHONE_TYPE;
extern const u8 ICSD_WIND_MIC_TYPE;

struct icsd_win_libfmt {
    int lib_alloc_size;  //算法ram需求大小
};

struct icsd_win_infmt {
    void *alloc_ptr;     //外部申请的ram地址
    int lib_alloc_size;  //算法ram需求大小
    u8 TOOL_FUNCTION;
};

typedef struct {
    u8  f2wind;
    float f2pwr;
    float f2curin[48];
    int idx;
    u8  wind_id;
} __wind_part1_out;

typedef struct {
    u8  f2wind;
    float f2pwr;
    float f2in_cur3t[48];
    int idx;
    u8  wind_id;
} __wind_part1_out_rx;

typedef struct {
    s16 *data_1_ptr;
    s16 *data_2_ptr;
    s16 *data_3_ptr;
    u8 anc_mode;
    u8 wind_ft;
    __wind_part1_out *part1_out;
    __wind_part1_out_rx *part1_out_rx;
} __icsd_win_run_parm;

typedef struct {
    u8 wind_lvl;
    void *wind_alg_bt_inf;
    u16 wind_alg_bt_len;
} __icsd_win_output;

typedef struct {
    u8 wind_lvl_scale;
    u8 icsd_wind_num_thr1;
    u8 icsd_wind_num_thr2;
    float wind_iir_alpha;
    float corr_thr;
    float msc_lp_thr;
    float msc_mp_thr;
    float cpt_1p_thr;
    float ref_pwr_thr;
} __wind_config;

struct wind_function {
    void (*wind_config_init)(__wind_config *_wind_config);
    void (*HanningWin_pwr_float)(float *input, int *output, int len);
    void (*HanningWin_pwr_s1)(s16 *input, int *output, int len);
    void (*FFT_radix256)(int *in_cur, int *out);
    void (*FFT_radix128)(int *in_cur, int *out);
    void (*complex_mul)(int *input1, float *input2, float *out, int len);
    void (*complex_div)(float *input1, float *input2, float *out, int len);
    void (*pxydivpxxpyy)(float *pxy, float *pxx, float *pyy, float *out, int len);
    double (*log10_float)(double x);
    u8(*cal_score)(u8 valud, u8 *buf, u8 ind, u8 len);
    int (*icsd_adt_tws_msync)(u8 *data, s16 len);
    int (*icsd_adt_tws_ssync)(u8 *data, s16 len);
    void (*icsd_wind_run_part2_cmd)();
    void *(*icsd_adt_wind_part1_rx)();
};
extern struct wind_function *WIND_FUNC;

void icsd_wind_get_libfmt(struct icsd_win_libfmt *libfmt);
void icsd_wind_set_infmt(struct icsd_win_infmt *fmt);

float adt_pow10(float n);
float angle_float(float x, float y);

void icsd_wind_set_wind(float data);

void icsd_alg_wind_run(__icsd_win_run_parm *run_parm, __icsd_win_output *output);
void icsd_alg_wind_run_part1(__icsd_win_run_parm *run_parm, __icsd_win_output *output);
void icsd_alg_wind_run_part2(__icsd_win_run_parm *run_parm, __icsd_win_output *output);

void icsd_wind_master_tx_data_sucess(void *_data);
void icsd_wind_master_rx_data(void *_data);
void icsd_wind_slave_tx_data_sucess(void *_data);
void icsd_wind_slave_rx_data(void *_data);

int alg_wind_ssync(__wind_part1_out *_part1_out);
#endif
