#include "dreg.h"

void dreg_init(void) {
    dkernel_registry_t *reg = (dkernel_registry_t *)DREG_ADDR;

    reg->pattern      = DKES_PATTERN;
    reg->version      = DKES_VERSION;
    reg->first_driver = 0;
}

dkernel_registry_t *dreg_get(void) {
    return (dkernel_registry_t *)DREG_ADDR;
}

int dreg_register_driver(ddriver_t *drv) {
    if (!drv) return -1;
    if (drv->pattern != DDRIVER_PATTERN) return -1;

    dkernel_registry_t *reg = (dkernel_registry_t *)DREG_ADDR;

    drv->next = reg->first_driver;
    reg->first_driver = (u64)drv;

    return 0;
}

ddriver_t *dreg_first_driver(void) {
    dkernel_registry_t *reg = (dkernel_registry_t *)DREG_ADDR;
    if (reg->first_driver == 0) return (ddriver_t *)0;
    return (ddriver_t *)reg->first_driver;
}