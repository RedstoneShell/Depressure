#ifndef DSTORAGE_H
#define DSTORAGE_H

#include "dtypes.h"

void dstorage_detect(void *BootServices);
u64  dstorage_get_total(void);
u32  dstorage_get_count(void);
u64  dstorage_get_device_size(u32 index);

#endif