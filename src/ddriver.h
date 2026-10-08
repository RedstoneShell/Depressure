#ifndef DDRIVER_H
#define DDRIVER_H

#include "dtypes.h"

/* Magic "DDVR" */
#define DDRIVER_PATTERN    0x52564444u

/* Driver ABI ver */
#define DDRIVER_VERSION    0x00010000u   /* 1.0.0 */

/* Max drv name len*/
#define DDRIVER_NAME_MAX   32

/* Driver type */
#define DDRIVER_TYPE_NONE    0
#define DDRIVER_TYPE_VIDEO   1
#define DDRIVER_TYPE_DISK    2
#define DDRIVER_TYPE_NET     3
#define DDRIVER_TYPE_INPUT   4
#define DDRIVER_TYPE_TIMER   5
#define DDRIVER_TYPE_OTHER   255

typedef struct ddriver {
    u32 pattern;                        /* DDRIVER_PATTERN */
    u32 version;                        /* DDRIVER_VERSION */
    u64 next;                           /* Next at list, 0 = Endd */
    u64 type;                           /* DDRIVER_TYPE_* */
    char name[DDRIVER_NAME_MAX];        /* Driver name, ASCII, NUL-terminated */
    u64 entry;                          /* Entry */
    u64 api;                            /* API */
    u64 data;                           /* Private data */
} ddriver_t;

#endif /* DDRIVER_H */