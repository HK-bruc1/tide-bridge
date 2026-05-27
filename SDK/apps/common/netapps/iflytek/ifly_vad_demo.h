#pragma once
#include "ifly_vad.h"

typedef struct {
    // vad
    ifly_vad_param vad_param;
    // other
    char local_text[MAX_VAD_LEN];		// 对话文本。近端
    int vad_timer;
} ifly_vad_struct;
