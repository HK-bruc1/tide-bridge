#ifndef LIB_MEDIA_CONFIG_H
#define LIB_MEDIA_CONFIG_H


extern const int CONFIG_DAC_CACHE_MSEC;
extern const int CONFIG_JLSTREAM_MULTI_THREAD_ENABLE;
extern const int CONFIG_MULTI_THREAD_SELF_ADAPTION_ENABLE;
extern const int config_media_24bit_enable;
extern const int CONFIG_SEAMLESS_RECORDER_ENABLE;
extern const int config_ch_adapter_32bit_enable;
extern const int config_mixer_32bit_enable;
extern const int config_jlstream_fade_32bit_enable;
extern const int config_audio_eq_xfade_enable;
extern const int config_out_dev_limiter_enable;
extern const int config_peak_rms_32bit_enable;
extern const int config_audio_vocal_track_synthesis_32bit_enable;

extern const int CONFIG_MAX_ENC_DATA_CACHE_SIZE;
extern const int CONFIG_LOG_OUTPUT_ENABLE;
extern const int config_audio_cfg_online_enable;
extern const int config_audio_dac_dma_buf_realloc_enable;
extern const int config_audio_dac_delay_off_ms;
extern const int CONFIG_MEDIA_MEM_DEBUG;


/*
 *******************************************************************
 *						DAC Configs
 *******************************************************************
 */
extern const int config_audio_dac_output_channel;
extern const int config_audio_dac_output_mode;
extern const int config_audio_dac_channel_left_enable;
extern const int config_audio_dac_channel_right_enable;
extern const int config_audio_dac_power_on_mode;
extern const int config_audio_dac_power_off_lite;
extern const int config_audio_dac_mix_enable;
extern const int config_audio_dac_noisefloor_optimize_enable;
extern const char config_audio_dac_trim_enable;
extern const int config_audio_dac_mute_timeout;
extern const int config_audio_dac_pa_mode;
extern const int config_audio_dac_power_mode;

/*
 *******************************************************************
 *						ADC Configs
 *******************************************************************
 */
extern const int config_audio_adc0_enable;
extern const int config_audio_adc1_enable;
extern const int config_audio_adc2_enable;
extern const int config_audio_adc3_enable;
extern const int config_audio_adc4_enable;
extern const int config_audio_adc5_enable;
extern const int config_audio_adc6_enable;
extern const int config_audio_adc7_enable;
extern const int config_audio_adc0_input_mode;
extern const int config_audio_adc1_input_mode;
extern const int config_audio_adc2_input_mode;
extern const int config_audio_adc3_input_mode;
extern const int config_audio_adc4_input_mode;
extern const int config_audio_adc5_input_mode;
extern const int config_audio_adc6_input_mode;
extern const int config_audio_adc7_input_mode;
extern const u8 const_mic_capless_open_delay_debug;
extern const u8 const_mic_capless_trim_delay_debug;
extern const u8 const_adc_async_en;						//是否支持多个ADC异步打开

/*
 *******************************************************************
 *						EQ Configs
 *******************************************************************
 */
extern const int config_audio_eq_hp_enable;		//High Pass
extern const int config_audio_eq_lp_enable;		//Low Pass
extern const int config_audio_eq_bp_enable;		//Band Pass(Peaking)
extern const int config_audio_eq_hs_enable;		//High Shelf
extern const int config_audio_eq_ls_enable;		//Low Shelf
extern const int config_audio_eq_hs_q_enable;	//High Shelf Q
extern const int config_audio_eq_ls_q_enable;	//Low Shelf Q
extern const int config_audio_eq_hp_adv_enable;	//High Pass Advance
extern const int config_audio_eq_lp_adv_enable;	//Low Pass Advance

/*
 *******************************************************************
 *						SRC Configs
 *******************************************************************
 */
extern const int SRC_HW_VERSION_SEL;//0:srv_v2 1:src_v3

/*
 *******************************************************************
 *						Effect Configs
 *******************************************************************
 */
extern const int config_audio_gain_enable;
extern const int config_audio_split_gain_enable;
extern const int config_audio_stereomix_enable;
extern const int voicechanger_effect_v_config;
extern const int mb_limiter_3band_run_en;
extern const int limiter_run_mode;
extern const int drc_advance_run_mode;
extern const int drc_run_mode;
extern const int  stereo_phaser_run_mode;
extern const int  stereo_flanger_run_mode;
extern const int  stereo_chorus_run_mode;
extern const int dynamic_eq_run_mode;
extern const  int drc_detect_run_mode;

/*
 *******************************************************************
 *						Audio Codec Configs
 *******************************************************************
 */
extern const u8 config_flac_id3_enable;
extern const u8 config_ape_id3_enable;
extern const u8 config_m4a_id3_enable;
extern const u8 config_wav_id3_enable;
extern const u8 config_wma_id3_enable;










#endif
