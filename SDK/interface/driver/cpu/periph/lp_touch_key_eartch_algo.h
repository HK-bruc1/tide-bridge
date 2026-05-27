#ifndef  __LP_TOUCH_KEY_EARTCH_ALGO_H__
#define  __LP_TOUCH_KEY_EARTCH_ALGO_H__


#include "typedef.h"


void lp_touch_key_eartch_algorithm_init(u32 ch_idx, u32 temp_th, u32 edge_th, u32 win_data_len);

u32 lp_touch_key_eartch_algorithm_analyze(u32 ch_idx, u16 res);

void lp_touch_key_eartch_algorithm_set_win_data_len(u32 ch_idx, u32 data_len);

u32 lp_touch_key_eartch_algorithm_get_vaild_avg(u32 ch_idx);

u32 lp_touch_key_eartch_algorithm_is_invalid(u32 ch_idx);

u32 lp_touch_key_eartch_algorithm_get_ear_state(u32 ch_idx);

void lp_touch_key_eartch_algorithm_set_ear_state(u32 ch_idx, u32 ear_state);

u32 lp_touch_key_eartch_algorithm_get_inear_up_th(u32 ch_idx);

void lp_touch_key_eartch_algorithm_set_inear_up_th(u32 ch_idx, u32 up_th);



#endif

