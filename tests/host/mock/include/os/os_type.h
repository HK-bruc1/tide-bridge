#ifndef __OS_TYPE_H
#define __OS_TYPE_H

/* Host mock for SDK/interface/system/os/os_type.h */

#define OS_TICKS_PER_SEC          100

#define Q_MSG           1
#define Q_EVENT         2
#define Q_CALLBACK      3
#define Q_USER          4

#define OS_DEL_NO_PEND               0u
#define OS_DEL_ALWAYS                1u

typedef struct { int dummy; } OS_MUTEX;
typedef struct { int dummy; } OS_SEM;

#endif
