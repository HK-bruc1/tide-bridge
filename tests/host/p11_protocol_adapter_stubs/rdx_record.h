#ifndef __P11_TEST_RDX_RECORD_H__
#define __P11_TEST_RDX_RECORD_H__

#include "typedef.h"

typedef struct {
    u8 run;
    u8 formate;
    u8 scene;
    u8 orig_scene;
    bool is_switch;
    u8 switch_orig_scene;
    u8 noshow;
    u8 process_state;
    u8 mode;
    u8 orig_mode;
    bool key_trigger;
    void (*ui_notify)(void);
    u32 begin_time;
    bool rerun;
} RecordStatus;

#endif
