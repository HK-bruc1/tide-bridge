#ifndef  __EFUSE_H__
#define  __EFUSE_H__


u32 efuse_get_chip_id();
u32 efuse_get_vbat_trim_4p20();
u32 efuse_get_vbat_trim_4p35();
u32 efuse_get_gpadc_vbg_trim();
u32 get_chip_version();

u32 efuse_get_btvbg_xosc_trim();
u32 efuse_get_btvbg_syspll_trim();
void   efuse_init();

u32 efuse_get_wvdd_level_trim();
u32 get_chip_id();

u32 efuse_get_charge_cur_trim(void);
u32 efuse_get_charge_full_trim(void);

u32 efuse_get_audio_vbg_trim();
u32 efuse_get_lrc_freq_trim();
u32 efuse_get_vbat_trim();
u32 get_btvbg_trx_trim();
u32 get_btvbg_btadc_trim();
u32 get_btvbg_btpll_trim();

#endif  /*EFUSE_H*/
