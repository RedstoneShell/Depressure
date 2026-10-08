#ifndef PCI_H
#define PCI_H

#include "dtypes.h"
#include "dstat.h"

#define PCI_MAX_DEVICES   256
#define PCI_NAME_MAX      32
#define PCI_API_VERSION   0x00010001

typedef struct pci_device {
    u8  bus;
    u8  slot;
    u8  func;
    u16 vendor_id;
    u16 device_id;
    u8  revision;
    u8  prog_if;
    u8  subclass;
    u8  class_code;
    u8  header_type;
    u8  irq_line;
    u8  irq_pin;
    u32 bar[6];
    u32 bar_size[6];
    u16 command;
    u16 status;
} pci_device_t;

typedef struct pci_api {
    u32 version;
    u32 _pad;
    dstatus_t (*get_count)(u32 *out_count);
    dstatus_t (*get_device)(u32 index, const pci_device_t **out);
    dstatus_t (*find_by_class)(u8 class_code, u8 subclass, u32 *out_index);
    dstatus_t (*find_by_id)(u16 vendor_id, u16 device_id, u32 *out_index);
    dstatus_t (*read_config32)(u8 bus, u8 slot, u8 func, u8 off, u32 *out);
    dstatus_t (*write_config32)(u8 bus, u8 slot, u8 func, u8 off, u32 value);
    dstatus_t (*get_scan_truncated)(u32 *out_truncated);
} pci_api_t;

void pci_main(void *api, void *fb_info);

#endif /* PCI_H */