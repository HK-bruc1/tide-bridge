#ifndef __RDX_JL_GPIO_H__
#define __RDX_JL_GPIO_H__

#include "typedef.h"
#include "gpio_config.h"

/*
 * 阶段 1 只定义薄层内联函数接口，实现直接映射到 JL gpio_set_mode()。
 * 如果业务代码中大量出现 gpio_set_mode(IO_PORT_SPILT(...)) 模式，
 * 可先在阶段 1 用这些 wrapper 替换；否则可延后到阶段 2。
 */

static inline void rdx_gpio_set_output_high(u32 io)
{
    gpio_set_mode(IO_PORT_SPILT(io), PORT_OUTPUT_HIGH);
}

static inline void rdx_gpio_set_output_low(u32 io)
{
    gpio_set_mode(IO_PORT_SPILT(io), PORT_OUTPUT_LOW);
}

static inline void rdx_gpio_set_highz(u32 io)
{
    gpio_set_mode(IO_PORT_SPILT(io), PORT_HIGHZ);
}

#endif
