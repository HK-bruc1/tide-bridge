#ifndef MIC_SELECT_H
#define MIC_SELECT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MS_MAX_MICS 4
#define MS_PACKET_SAMPLES_16K 320
typedef enum {
    MS_CHANNEL_STATE_LOW = 0,
    MS_CHANNEL_STATE_NORMAL = 1,
    MS_CHANNEL_STATE_CLIPPING = 2
} ms_channel_state_t;

typedef struct {
    int sample_rate;
    int num_mics;
    int analysis_samples;
    float rms_min;
    int clip_threshold;
    float clip_rate_max;
    float energy_advantage_db;
    int confirm_frames;
    int hold_time_ms;
    float crossfade_ms;
    int cold_start_frames;
} mic_select_config_t;

/* 初始化默认参数：
 * - sample_rate: 16000
 *   采样率，单位 Hz；当前按 16kHz / 20ms 一包的语音链路做优化。
 * - num_mics: 4
 *   麦克风数量；当前支持 2 路或 4 路，默认按 4 路配置。
 * - analysis_samples: 320
 *   分析帧长度，单位为“每通道样本数”；固定与 20ms 输入包长一致。
 * - rms_min: 100
 *   通道最低有效 RMS 能量阈值；低于该值时判定为 LOW（静音/断线/过弱）。
 * - clip_threshold: 32000
 *   单样本削波判定阈值；绝对值大于等于该值的样本计入削波统计。
 * - clip_rate_max: 0.05
 *   最大允许削波比例；分析帧内削波样本占比超过该值时判定为 CLIPPING。
 * - energy_advantage_db: 3.0
 *   择优切换的能量优势门限，单位 dB；候选通道需明显强于当前通道才允许切换。
 * - confirm_frames: 8
 *   连续确认帧数；候选通道需连续满足条件达到该帧数后才真正触发切换。
 * - hold_time_ms: 1500
 *   最小驻留时间，单位 ms；一次切换完成后，在该时间内禁止再次切换。
 * - crossfade_ms: 600
 *   交叉淡化时长，单位 ms；切换时旧通道与新通道线性平滑过渡的持续时间。
 * - cold_start_frames: 16
 *   冷启动保护帧数；启动初期仅允许保护性切换，抑制基于能量优势的择优切换。
 * 说明：
 * - 算法内部不使用 malloc/calloc/free 等堆分配，状态由内部固定静态单例持有；未 destroy 前再次 create 会失败。
 * - 默认一帧即一包：16kHz 下每次调用建议输入 320 样本，输入输出样本数保持一致。
 * - 若调用方自行填写配置，非法参数不会被静默修正；sl_mic_select_create() 会直接失败并返回 -1。
 */
void sl_mic_select_config_init(mic_select_config_t* config);
int sl_mic_select_create(const mic_select_config_t* config);
void sl_mic_select_destroy(void);
void sl_mic_select_reset(void);

/* 流式处理接口。
 * 参数说明：
 * - ch1/ch2/ch3/ch4: 每路输入音频，均为 int16 单通道 PCM。
 *   当 num_mics=2 时只使用 ch1/ch2；当 num_mics=4 时使用全部 4 路。
 * - count: 本次每个通道输入的样本数。
 *   优化版本要求与 analysis_samples 保持一致；16kHz 场景下即 320（20ms 一包）。
 * - output: 输出单路最优音频，长度至少为 count。
 * - selected_channels: 可选输出，长度至少为 count。
 *   若非 NULL，则 selected_channels[i] 输出第 i 个样本对应的当前选中通道序号（1-based）。
 *   在 0.6 秒交叉淡化期间，这里输出“目标通道”的序号，便于实时观察切换结果。
 */
int sl_mic_select_process(const int16_t* ch1,
                          const int16_t* ch2,
                          const int16_t* ch3,
                          const int16_t* ch4,
                          uint32_t count,
                          int16_t* output,
                          int* selected_channels);

/* 返回当前选中通道序号（1-based）：2 路默认 1，4 路默认 3。 */
int sl_mic_select_get_active_channel(void);
void sl_mic_select_get_channel_rms(float rms[MS_MAX_MICS]);
void sl_mic_select_get_channel_state(int states[MS_MAX_MICS]);

#ifdef __cplusplus
}
#endif

#endif /* MIC_SELECT_H */
