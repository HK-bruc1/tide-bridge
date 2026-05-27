#ifndef _ICSD_RT_ANC_LIB_H
#define _ICSD_RT_ANC_LIB_H

#include "icsd_common_v2.h"
#include "anc_DeAlg_v2.h"
#include "anc_sDe.h"
#include "icsd_anc_v2_config.h"
#include "math.h"
#include "asm/math_fast_function.h"
#include "../tool/anc_ext_tool.h"

#if 0
#define _rt_printf printf               //打开智能免摘库打印信息
#else
extern int rt_printf_off(const char *format, ...);
#define _rt_printf rt_printf_off
#endif
extern int (*rt_printf)(const char *format, ...);

#if 0
#define _hz_printf printf               //打开智能免摘库打印信息
#else
extern int rt_printf_off(const char *format, ...);
#define _hz_printf rt_printf_off
#endif
extern int (*hz_printf)(const char *format, ...);


#define RT_DMA_BELONG_TO_SZL    		1
#define RT_DMA_BELONG_TO_SZR    		2
#define RT_DMA_BELONG_TO_PZL    		3
#define RT_DMA_BELONG_TO_PZR    		4
#define RT_DMA_BELONG_TO_TONEL  		5
#define RT_DMA_BELONG_TO_BYPASSL 		6
#define RT_DMA_BELONG_TO_TONER  		7
#define RT_DMA_BELONG_TO_BYPASSR 	    8
#define RT_DMA_BELONG_TO_CMP 	   		9

#define RT_ANC_TEST_DATA			    0
#define RT_ANC_DSF8_DATA_DEBUG	        0
#define RT_DSF8_DATA_DEBUG_TYPE			RT_DMA_BELONG_TO_CMP

#define RT_ANC_ON						BIT(1)
#define RT_ANC_NEED_UPDATA              BIT(2)
#define RT_ANC_SUSPEND              	BIT(3)

#define RT_ANC_USER_TRAIN_DMA_LEN 		16384
#define RT_ANC_DMA_DOUBLE_LEN       	(512*4) //(512*8)
#define RT_ANC_DOUBLE_TRAIN_LEN         512
#define RT_ANC_DMA_DOUBLE_CNT       	32

#define RT_ANC_DMA_INTERVAL_MAX    		(1.8*RT_ANC_DMA_DOUBLE_LEN*1000/375) //us

#define RT_ANC_MAX_ORDER				10		//rt_anc最大支持滤波器个数

typedef struct {

} __rt_anc_config;

struct rt_anc_function {
    u8(*anc_dma_done_ppflag)();
    u16(*sys_timeout_add)(void *priv, void (*func)(void *priv), u32 msec);
    void (*icsd_adt_rtanc_suspend)();
    void (*sys_timeout_del)(u16 t);
    void (*anc_dma_on)(u8 out_sel, int *buf, int len);
    void (*anc_dma_on_double)(u8 out_sel, int *buf, int len);
    void (*anc_core_dma_ie)(u8 en);
    void (*anc_core_dma_stop)(void);
    void (*rt_anc_post_rttask_cmd)(u8 cmd);
    void (*rt_anc_post_anctask_cmd)(u8 cmd);
    void (*icsd_post_detask_msg)(u8 cmd);
    void (*rt_anc_dma_2ch_on)(u8 out_sel, int *buf, int len);
    void (*rt_anc_dma_4ch_on)(u8 out_sel_ch01, u8 out_sel_ch23, int *buf, int len);
    void (*rt_anc_task_create)();
    void (*rt_anc_task_kill)();
    void (*rt_anc_alg_output)(void *rt_param_l, void *rt_param_r);
    void (*icsd_rtanc_need_updata)();
    void (*icsd_rtanc_master_send)(u8 cmd);
    void (*icsd_rtanc_slave_send)(u8 cmd);
    int (*jiffies_usec2offset)(unsigned long begin, unsigned long end);
    unsigned long (*jiffies_usec)(void);
    int (*audio_anc_debug_send_data)(u8 *buf, int len);
    //tws
    int (*tws_api_get_role)(void);
    int (*tws_api_get_tws_state)();
    void (*rt_anc_config_init)(__rt_anc_config *_rt_anc_config);
    void (*rt_anc_param_updata_cmd)(void *param_l, void *param_r);

    void (*clock_refurbish)();

    void (*icsd_self_talk_output)(u8 flag);
    u8(*get_wind_lvl)();
    u8(*get_adjdcc_result)();
};
extern struct rt_anc_function *RT_ANC_FUNC;
enum {
    RT_ANC_PART2 = 0,
    RT_ANC_RTTASK_SUSPEND,
    RT_ANC_MASTER_UPDATA,
    RT_ANC_SLAVER_UPDATA,
    RT_ANC_WAIT_UPDATA,
    RT_ANC_UPDATA_END,
    RT_CMP_PART2,
};

enum {
    RT_ANC_CMD_FORCED_EXIT = 0,
    RT_ANC_CMD_PART1,
    RT_ANC_CMD_EXIT,
    RT_ANC_CMD_UPDATA,
    RTANC_CMD_UPDATA,
    RT_ANC_CMD_DMA_ON,
    RT_ANC_CMD_ANCTASK_SUSPEND,
};

typedef struct {
    u8  anc_mode;			//0 ANC, 1 通透(只会在DAC出声的时候更新)
    u8	ff_yorder;
    u8  fb_yorder;
    u8  cmp_yorder;
    float ffgain;
    float fbgain;
    float ff_fade_gain;
    float fb_fade_gain;
    float cmpgain;
    double ff_coeff[5 * RT_ANC_MAX_ORDER]; //max
    double fb_coeff[5 * RT_ANC_MAX_ORDER]; //max
    double cmp_coeff[5 * RT_ANC_MAX_ORDER]; //max
    void *param;
} __rt_anc_param;

struct rt_anc_infmt {
    void *alloc_ptr;     //外部申请的ram地址
    u16	tone_delay;      //提示音延长多长时间开始获取sz数据
    u8  type;            //RT_ANC_TWS / RT_ANC_HEADSET
    void *param;
    void *ext_cfg;
    __rt_anc_param *anc_param_l;
    __rt_anc_param *anc_param_r;
    u8  id;
    struct icsd_rtanc_tool_data *rtanc_tool;
};

struct rt_anc_libfmt {
    //输出参数
    int lib_alloc_size;  //算法ram需求大小
    u8  alogm;           //anc采样率
    //输入参数
    u8  type;            //RT_ANC_TWS / RT_ANC_HEADSET
};

struct rt_anc_tws_packet {
    s8  *data;
    u16 len;
};

enum {
    M_CMD_TEST = 0,
    S_CMD_TEST,
};

typedef struct {
    u8  cmd;
    u8  id;
    u8  data;
} __rtanc_sync_cmd;

extern const u8 rt_anc_dma_ch_num;
extern const u8 rt_cmp_en;
extern const u8 rt_anc_dac_en;
extern const u8 rt_anc_tool_data;
extern  u16	RT_ANC_STATE;
//RT_ANC调用
int os_taskq_post_msg(const char *name, int argc, ...);
//SDK 调用
void rt_anc_get_libfmt(struct rt_anc_libfmt *libfmt);
void rt_anc_run();
void rt_anc_dma_done();
void rt_anc_anctask_cmd_handle(void *param, u8 cmd);
void rt_anc_exit();
void rt_anc_master_packet(struct rt_anc_tws_packet *packet, u8 cmd);
void rt_anc_slave_packet(struct rt_anc_tws_packet *packet, u8 cmd);
void icsd_rtanc_m2s_cb(void *_data, u16 len, bool rx);
void icsd_rtanc_s2m_cb(void *_data, u16 len, bool rx);
void rt_anc_rttask_handler(int msg);
void rt_anc_suspend();
void rt_anc_resume();
//库调用
void rt_anc_config_init();
void rt_anc_dsf8_data_debug(u8 belong, s16 *dsf_out_ch0, s16 *dsf_out_ch1, s16 *dsf_out_ch2, s16 *dsf_out_ch3);


//=========RTANC============
struct __rtanc_bt_inf {
    u8 debug_function;
    u16 debug_id;

    u8 mode;   // norm/tidy
    u8 ff_dc_par_use;
    u8 wind_lvl;
    u8 trim_lock;
    u8 pass_idx;
    u8 ptr_cnt;
    u8 dov[3 * 16];
    u8 vod_cnt;
    u8 self_vod_cnt;
    u8 self_talk_flag;
    u8 spec_cnt;
    u8 ref_spec_cnt;
    u8 err_spec_cnt;
    u8 mse_fcnt_thr;
    u8 mse_ctl_fcnt;
    u8 hist_history[5];
    u8 hist_cur;
    u8 hist_last;
    u8 sz_diff_meet;
    u8 sz_hist_cur;
    u8 sz_hist_last;
    u8 sz_iir_cnt;

    float angle[3 * 16];
    float mse_180_500;
    float mse_500_1k;
    float mse_ctl1;
    float mse_ctl2;
    float mse_ctl3;
    float hist_err;
    float fitness;
    float tar_diff;
    float tar_hz_diff;
    float tar_diff_thr;
    float sz_stable_val1;
    float sz_stable_val2;
    float sz_iir_diff;
    float sz_hist_err;
    float sz_diff_cmp;

    struct __anc_ext_rtanc_adaptive_cfg rtanc_tool_cfg;
};

struct icsd_rtanc_infmt {
    void *alloc_ptr;     //外部申请的ram地址
    int lib_alloc_size;  //算法ram需求大小
    __rt_anc_param *anc_param_l;
    __rt_anc_param *anc_param_r;
    u8  ch_num;
    u8  ep_type;
    u8  rtanc_type;
    struct icsd_rtanc_tool_data *rtanc_tool;
    u8  TOOL_FUNCTION;
};

struct icsd_rtanc_libfmt {
    //输出参数
    int lib_alloc_size;  //算法ram需求大小
    u8  ch_num;
    u8  rtanc_type;
};

typedef struct {
    u8  dma_ch;
    s16 *inptr_h;
    s16 *inptr_l;
    float *out0_sum;
    float *out1_sum;
    float *out2_sum;
    int   *fft_ram;
    float *hpest_temp;
} __icsd_rtanc_part1_parm;

typedef struct {
    u8  dma_ch;
    float *out0_sum;
    float *out1_sum;
    float *out2_sum;
    float *sz_out0_sum;
    float *sz_out1_sum;
    float *sz_out2_sum;
    float *szpz_out;
} __icsd_rtanc_part2_parm;


typedef struct {
    u8     yorder;
    double *coeff;
    float  gain;
    float  fade_gain;
} __rtanc_filter;

struct rtanc_param {
    __rtanc_filter ffl_filter;
    __rtanc_filter fbl_filter;
    __rtanc_filter ffr_filter;
    __rtanc_filter fbr_filter;
};

void icsd_rtanc_get_libfmt(struct icsd_rtanc_libfmt *libfmt);
void icsd_rtanc_set_infmt(struct icsd_rtanc_infmt *fmt);
void icsd_alg_rtanc_run_part1(__icsd_rtanc_part1_parm *part1_parm);
u8   icsd_alg_rtanc_run_part2(__icsd_rtanc_part2_parm *part2_parm);
void icsd_alg_rtanc_part2_parm_init();
void rt_anc_time_out_del();
void rt_anc_set_init(struct rt_anc_infmt *fmt, struct __anc_ext_rtanc_adaptive_cfg *rtanc_tool_cfg);
void rt_anc_init(struct rt_anc_infmt *fmt, struct __anc_ext_rtanc_adaptive_cfg *rtanc_tool_cfg);
void rt_anc_param_updata_cmd(void *param_l, void *param_r);
void rt_anc_part1_reset();
void icsd_rtanc_alg_get_sz(float *sz_out, u8 ch);
void icsd_alg_rtanc_fadegain_update(struct rtanc_param *param);
void icsd_alg_rtanc_filter_update(struct rtanc_param *param);
void icsd_post_rtanctask_msg(u8 cmd);
void icsd_post_detask_msg(u8 cmd);
void rtanc_adjdcc_flag_set(u8 flag);
void rtanc_cal_and_update_filter_l_task();
u8 adjdcc_trigger_update(u8 env_level, float *table);
u8 rtanc_adjdcc_flag_get();

extern const u8 RTANC_ALG_DEBUG;
#endif
