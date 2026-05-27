#include "audio_dac.h"
#include "media_config.h"
#include "power/power_manage.h"


static u8 audio_dac_idle_query()
{
    return audio_dac_is_idle();
}

static enum LOW_POWER_LEVEL audio_dac_level_query(void)
{
    /*根据dac的状态选择sleep等级*/
    if (config_audio_dac_power_off_lite) {
        /*anc打开，进入轻量级低功耗*/
        return LOW_POWER_MODE_LIGHT_SLEEP;
    } else {
        /*进入最优低功耗*/
        return LOW_POWER_MODE_SLEEP;
    }
}

REGISTER_LP_TARGET(audio_dac_lp_target) = {
    .name    = "audio_dac",
    .level   = audio_dac_level_query,
    .is_idle = audio_dac_idle_query,
};
