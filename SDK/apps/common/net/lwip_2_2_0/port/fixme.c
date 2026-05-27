
#include "system/includes.h"
#include "app_config.h"

#define CPU_RAND()	(JL_RAND->R64L)
unsigned int random32(int type)
{
    return CPU_RAND();
}


u32 OSGetTime(void)
{
    return jiffies;
}

