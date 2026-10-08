#ifndef DDEF_H
#define DDEF_H
#include "dtypes.h"
#define doffsetof(type, member)  ((usize)&(((type *)0)->member))
#define dcontainer_of(ptr, type, member) \
    ((type *)((uptr)(ptr) - doffsetof(type, member)))
#define dcountof(arr)  (sizeof(arr) / sizeof((arr)[0]))
#endif /* DDEF_H */