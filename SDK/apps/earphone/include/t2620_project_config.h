#ifndef T2620_PROJECT_CONFIG_H
#define T2620_PROJECT_CONFIG_H

/*
 * T2620 project-level overrides.
 *
 * Keep these definitions outside sdk_config.h/c because those files are owned
 * by the JL visual configuration tool.
 */

#ifndef TCFG_DIP_SWITCH_POWER_ENABLE
#define TCFG_DIP_SWITCH_POWER_ENABLE              1
#endif

#ifndef TCFG_DIP_SWITCH_POWER_IO
#define TCFG_DIP_SWITCH_POWER_IO                  IO_PORTB_01
#endif

#if TCFG_DIP_SWITCH_POWER_ENABLE
/*
 * The current generated ADKEY and LP_TOUCH defaults use PB1. PB1 is reserved
 * for the DIP power switch on T2620, so keep those modules disabled here even
 * if the visual tool regenerates sdk_config.h.
 */
#undef TCFG_ADKEY_ENABLE
#define TCFG_ADKEY_ENABLE                         0

#undef TCFG_LP_TOUCH_KEY_ENABLE
#define TCFG_LP_TOUCH_KEY_ENABLE                  0
#endif

#ifndef TCFG_RDX_HOGP_ENABLE
#define TCFG_RDX_HOGP_ENABLE                      1
#endif

#endif /* T2620_PROJECT_CONFIG_H */
