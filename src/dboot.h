#ifndef DBOOT_H
#define DBOOT_H

#include "dtypes.h"

typedef struct dboot_config {
    int  boot_mode;       /* 0=DevCon, 1=GUI */
    int  log_level;       /* 0=off, 1=info, 2=debug */
    char init[64];
} dboot_config_t;
int dboot_parse(const char *buf, u64 size, dboot_config_t *out);
int dboot_load(dboot_config_t *out);

#endif /* DBOOT_H */