#ifndef __CPU_DAC_H__
#define __CPU_DAC_H__


#include "generic/typedef.h"
#include "generic/atomic.h"
#include "os/os_api.h"
#include "audio_src.h"
#include "media/audio_cfifo.h"
#include "system/spinlock.h"
#include "audio_def.h"
#include "audio_general.h"

/***************************************************************************
  							Audio DAC Features
Notes:以下为芯片规格定义，不可修改，仅供引用
***************************************************************************/
#define AUDIO_DAC_CHANNEL_NUM				2	//DAC通道数
#define AUDIO_ADDA_IRQ_MULTIPLEX_ENABLE			//DAC和ADC中断入口复用使能


#define DACVDD_LDO_1_20V        0
#define DACVDD_LDO_1_25V        1
#define DACVDD_LDO_1_30V        2
#define DACVDD_LDO_1_35V        3

#define AUDIO_DAC_SYNC_IDLE             0
#define AUDIO_DAC_SYNC_START            1
#define AUDIO_DAC_SYNC_NO_DATA          2
#define AUDIO_DAC_SYNC_ALIGN_COMPLETE   3

#define AUDIO_SRC_SYNC_ENABLE   1
#define SYNC_LOCATION_FLOAT      1
#if SYNC_LOCATION_FLOAT
#define PCM_PHASE_BIT           0
#else
#define PCM_PHASE_BIT           8
#endif

#define DA_LEFT        0
#define DA_RIGHT       1

/************************************
 *              DAC模式
 *************************************/
#define DAC_MODE_L_DIFF          (0)  // 低压差分模式   , 适用于低功率差分耳机  , 输出幅度 0~2Vpp
#define DAC_MODE_L_SINGLE        (1)  // 低压单端模式   , 主要用于左右差分模式, 差分输出幅度 0~2Vpp
#define DAC_MODE_H1_DIFF         (2)  // 高压1档差分模式, 适用于高功率差分耳机  , 输出幅度 0~3Vpp
#define DAC_MODE_H1_SINGLE       (3)  // 高压1档单端模式, 适用于高功率单端PA音箱, 输出幅度 0~1.5Vpp
#define DAC_MODE_H2_DIFF         (4)  // 高压2档差分模式, 适用于高功率差分PA音箱, 输出幅度 0~5Vpp
#define DAC_MODE_H2_SINGLE       (5)  // 高压2档单端模式, 适用于高功率单端PA音箱, 输出幅度 0~2.5Vpp


#define DACR32_DEFAULT		8192

#define DAC_ANALOG_OPEN_PREPARE         (1)
#define DAC_ANALOG_OPEN_FINISH          (2)
#define DAC_ANALOG_CLOSE_PREPARE        (3)
#define DAC_ANALOG_CLOSE_FINISH         (4)
//void audio_dac_power_state(u8 state)
//在应用层重定义 audio_dac_power_state 函数可以获取dac模拟开关的状态

struct audio_dac_hdl;
struct dac_platform_data {
    u32 digital_gain_limit;
    u32 max_sample_rate;    	// 支持的最大采样率
    s16 *dig_vol_tab;
    u16 dma_buf_time_ms;        // DAC dma buf 大小
    u16 max_dig_vol;
    u8 ldo_id;
    u8 ldo_volt;                // 电压 0:1.2V    1:1.25V    2:1.3V    3:1.35V
    u8 pa_isel;                 // 电流 范围：0 ~ 6
    u8 pa_mute_port;
    u8 pa_mute_value;
    u8 vcmo_en;
    u8 keep_vcmo;
    u8 lpf_isel;
    u8 sys_vol_type;            // 系统音量类型， 0:默认调数字音量  1:默认调模拟音量
    u8 max_ana_vol;
    u8 vcm_cap_en;              // 配1代表走外部通路,vcm上有电容时,可以提升电路抑制电源噪声能力，提高ADC的性能，配0相当于vcm上无电容，抑制电源噪声能力下降,ADC性能下降
    u8 power_on_mode;
    u8 bit_width;               // DAC输出位宽
    u8 l_ana_gain;              // 左声道模拟增益
    u8 r_ana_gain;              // 右声道模拟增益
    u8 power_boost;             // 输出功率增强
    u8 clk_sel;                 // 时钟源选择：单端数字时钟/差分晶振时钟
    u8 pa_en_slow;              // PA上电速度控制 0：快上电 1：缓上电 (power_on_mode == 1时有效)
};


struct audio_dac_trim {
    s16 left;
    s16 right;
    s16 vcomo;
};

struct trim_init_param_t {
    u8 fast_trim;
};

// *INDENT-OFF*
struct audio_dac_sync {
    u8 channel;
    u8 start;
    u8 fast_align;
    int fast_points;
    u32 input_num;
    int phase_sub;
    int in_rate;
    int out_rate;
#if AUDIO_SRC_SYNC_ENABLE
    struct audio_src_sync_handle *src_sync;
    void *buf;
    int buf_len;
    void *filt_buf;
    int filt_len;
#else
    struct audio_src_base_handle *src_base;
#endif
#if SYNC_LOCATION_FLOAT
    float pcm_position;
#else
    u32 pcm_position;
#endif
    void *priv;
    int (*handler)(void *priv, u8 state);
    void *correct_priv;
    void (*correct_cabllback)(void *priv, int diff);
};

struct audio_dac_fade {
    u8 enable;
    volatile u8 ref_L_gain;
    volatile u8 ref_R_gain;
    int timer;
};


struct audio_dac_hdl {
    u8 analog_inited;
    volatile u8 state;
    u8 ng_threshold;
    u8 gain;
    u8 channel;
	u8 dvol_mute;             //DAC数字音量是否mute
    u8 power_on;
    u8 need_close;
    u8 protect_fadein;
    u8 vol_set_en;
    u8 dec_channel_num;
    u8 anc_dac_open;
    u8 dac_read_flag;	//AEC可读取DAC参考数据的标志
    u16 d_volume[2];
    u16 start_ms;
    u16 delay_ms;
    u16 start_points;
    u16 delay_points;
    u16 prepare_points;//未开始让DAC真正跑之前写入的PCM点数
    u16 irq_points;
    s16 protect_time;
    s16 protect_pns;
    s16 fadein_frames;
    s16 fade_vol;
    u16 unread_samples;             /*未读样点个数*/
    s16 *output_buf;
    u32 sample_rate;
    u32 digital_gain_limit;
    u32 output_buf_len;

    OS_SEM *sem;
    OS_MUTEX mutex;
    spinlock_t lock;
    const struct dac_platform_data *pd;
	struct audio_dac_noisegate ng;
    void (*fade_handler)(u8 left_gain, u8 right_gain);
};

#endif

