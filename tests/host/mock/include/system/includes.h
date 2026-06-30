#ifndef SYSTEM_INCLUDES_H
#define SYSTEM_INCLUDES_H

/* Host mock replacement for SDK/system/includes.h */
/* Provides only what RDX service/core code needs to compile on host. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "typedef.h"
#include "os/os_type.h"

/* JL OS primitives mocked for host tests */

void CPU_CRITICAL_ENTER(void);
void CPU_CRITICAL_EXIT(void);
int os_taskq_post_type(const char *name, int type, int argc, int *argv);

#endif
