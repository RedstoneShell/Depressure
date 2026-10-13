// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 RedstoneShell
//
// This file is part of the Depressure Operating System.

#include "dtypes.h"
#include "ddriver.h"
#include "dkrnl.h"
#include "dstat.h"
#include "pci.h"

#define PCI_CONFIG_ADDR  0xCF8
#define PCI_CONFIG_DATA  0xCFC

static inline void outl(u16 p, u32 v) {
    __asm__ volatile ("outl %0, %1" : : "a"(v), "Nd"(p));
}
static inline u32 inl(u16 p) {
    u32 r;
    __asm__ volatile ("inl %1, %0" : "=a"(r) : "Nd"(p));
    return r;
}

static u32 pci_read32_raw(u8 bus, u8 slot, u8 func, u8 off) {
    u32 a = 0x80000000u
          | ((u32)bus  << 16)
          | ((u32)slot << 11)
          | ((u32)func << 8)
          | (off & 0xFC);
    outl(PCI_CONFIG_ADDR, a);
    return inl(PCI_CONFIG_DATA);
}
static void pci_write32_raw(u8 bus, u8 slot, u8 func, u8 off, u32 v) {
    u32 a = 0x80000000u
          | ((u32)bus  << 16)
          | ((u32)slot << 11)
          | ((u32)func << 8)
          | (off & 0xFC);
    outl(PCI_CONFIG_ADDR, a);
    outl(PCI_CONFIG_DATA, v);
}
static u16 pci_read16_raw(u8 bus, u8 slot, u8 func, u8 off) {
    u32 v = pci_read32_raw(bus, slot, func, off & 0xFC);
    return (u16)((v >> ((off & 2) * 8)) & 0xFFFF);
}
static u8 pci_read8_raw(u8 bus, u8 slot, u8 func, u8 off) {
    u32 v = pci_read32_raw(bus, slot, func, off & 0xFC);
    return (u8)((v >> ((off & 3) * 8)) & 0xFF);
}

static inline void uart_outb(u16 port, u8 v) {
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(port));
}
static inline u8 uart_inb(u16 port) {
    u8 r;
    __asm__ volatile ("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}
static void uart_putc(char c) {
    while ((uart_inb(0x3FD) & 0x20) == 0) {}
    uart_outb(0x3F8, (u8)c);
}
static void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s++);
    }
}
static void uart_put_hex8(u8 v) {
    static const char hex[] = "0123456789ABCDEF";
    uart_putc(hex[(v >> 4) & 0xF]);
    uart_putc(hex[v & 0xF]);
}
static void uart_put_hex16(u16 v) {
    uart_put_hex8((u8)(v >> 8));
    uart_put_hex8((u8)(v & 0xFF));
}
static void uart_put_hex32(u32 v) {
    uart_put_hex16((u16)(v >> 16));
    uart_put_hex16((u16)(v & 0xFFFF));
}
static void uart_put_dec(u64 v) {
    char buf[24];
    int i = 0;
    if (v == 0) { uart_putc('0'); return; }
    while (v > 0) { buf[i++] = '0' + (v % 10); v /= 10; }
    while (i > 0) uart_putc(buf[--i]);
}

static pci_device_t g_devices[PCI_MAX_DEVICES];
static u32          g_device_count = 0;
static u32          g_scan_truncated = 0;

static const char *pci_class_name(u8 cls) {
    switch (cls) {
        case 0x00: return "Unclassified";
        case 0x01: return "Mass Storage";
        case 0x02: return "Network";
        case 0x03: return "Display";
        case 0x04: return "Multimedia";
        case 0x05: return "Memory";
        case 0x06: return "Bridge";
        case 0x07: return "Communication";
        case 0x08: return "System Peripheral";
        case 0x09: return "Input Device";
        case 0x0A: return "Docking Station";
        case 0x0B: return "Processor";
        case 0x0C: return "Serial Bus";
        case 0x0D: return "Wireless";
        case 0x0E: return "Intelligent I/O";
        case 0x0F: return "Satellite";
        case 0x10: return "Encryption";
        case 0x11: return "Signal Processing";
        case 0x12: return "Processing Accelerator";
        case 0x13: return "Non-Essential Instrumentation";
        case 0x40: return "Coprocessor";
        case 0xFF: return "Unassigned";
        default:   return "?";
    }
}

static const char *pci_subclass_name(u8 cls, u8 sub) {
    switch (cls) {
        case 0x00:
            switch (sub) {
                case 0x00: return "Non-VGA";
                case 0x01: return "VGA-Compatible";
                default:   return "?";
            }
        case 0x01:
            switch (sub) {
                case 0x00: return "SCSI";
                case 0x01: return "IDE";
                case 0x02: return "Floppy";
                case 0x03: return "IPI";
                case 0x04: return "RAID";
                case 0x05: return "ATA";
                case 0x06: return "SATA";
                case 0x07: return "SAS";
                case 0x08: return "NVM";
                case 0x80: return "Other";
                default:   return "?";
            }
        case 0x02:
            switch (sub) {
                case 0x00: return "Ethernet";
                case 0x01: return "Token Ring";
                case 0x02: return "FDDI";
                case 0x03: return "ATM";
                case 0x04: return "ISDN";
                case 0x05: return "WorldFip";
                case 0x06: return "PICMG 2.14";
                case 0x07: return "Infiniband";
                case 0x08: return "Fabric";
                case 0x80: return "Other";
                default:   return "?";
            }
        case 0x03:
            switch (sub) {
                case 0x00: return "VGA";
                case 0x01: return "XGA";
                case 0x02: return "3D";
                case 0x80: return "Other";
                default:   return "?";
            }
        case 0x06:
            switch (sub) {
                case 0x00: return "Host";
                case 0x01: return "ISA";
                case 0x02: return "EISA";
                case 0x03: return "MCA";
                case 0x04: return "PCI-PCI";
                case 0x05: return "PCMCIA";
                case 0x06: return "NuBus";
                case 0x07: return "CardBus";
                case 0x08: return "RACEway";
                case 0x09: return "PCI-PCI Semi-Transparent";
                case 0x0A: return "InfiniBand-PCI";
                case 0x80: return "Other";
                default:   return "?";
            }
        case 0x09:
            switch (sub) {
                case 0x00: return "Keyboard";
                case 0x01: return "Digitizer";
                case 0x02: return "Mouse";
                case 0x03: return "Scanner";
                case 0x04: return "Gameport";
                case 0x80: return "Other";
                default:   return "?";
            }
        case 0x0C:
            switch (sub) {
                case 0x00: return "FireWire";
                case 0x01: return "ACCESS.bus";
                case 0x02: return "SSA";
                case 0x03: return "USB";
                case 0x04: return "Fibre Channel";
                case 0x05: return "SMBus";
                case 0x06: return "InfiniBand";
                case 0x07: return "IPMI";
                case 0x08: return "SERCOS";
                case 0x09: return "CANbus";
                default:   return "?";
            }
        default:
            return "?";
    }
}

static const char *pci_vendor_name(u16 vendor) {
    switch (vendor) {
        case 0x8086: return "Intel";
        case 0x1234: return "QEMU";
        case 0x1AF4: return "Red Hat (virtio)";
        case 0x1022: return "AMD";
        case 0x10DE: return "NVIDIA";
        case 0x1002: return "ATI";
        case 0x5333: return "S3 Graphics";
        case 0x80EE: return "VirtualBox";
        case 0x15AD: return "VMware";
        case 0x1B36: return "Red Hat";
        default:     return "?";
    }
}

static const char *pci_device_name(u16 vendor, u16 device) {
    if (vendor == 0x8086) {
        switch (device) {
            case 0x1237: return "440FX Host Bridge";
            case 0x7000: return "PIIX3 ISA Bridge";
            case 0x7010: return "PIIX3 IDE Controller";
            case 0x7020: return "PIIX3 USB Controller";
            case 0x7110: return "PIIX4 ISA Bridge";
            case 0x7111: return "PIIX4 IDE";
            case 0x7113: return "PIIX4 ACPI";
            case 0x100E: return "82540EM Gigabit Ethernet";
            case 0x100F: return "82545EM Gigabit Ethernet";
            case 0x10D3: return "82574L Gigabit Ethernet";
            case 0x29C0: return "82G33/G31/P35 Host Bridge";
            case 0x2918: return "ICH9 LPC Bridge";
            case 0x2922: return "ICH9 SATA Controller";
            case 0x2930: return "ICH9 SMBus";
            case 0x2934: return "ICH9 USB UHCI";
            case 0x2935: return "ICH9 USB UHCI";
            case 0x2936: return "ICH9 USB EHCI";
            case 0x293A: return "ICH9 USB UHCI";
            case 0x293C: return "ICH9 USB EHCI";
            default:     return "Intel device";
        }
    }
    if (vendor == 0x1234) {
        switch (device) {
            case 0x1111: return "QEMU Standard VGA";
            default:     return "QEMU device";
        }
    }
    if (vendor == 0x1AF4) {
        switch (device) {
            case 0x1000: return "Virtio Network";
            case 0x1001: return "Virtio Block";
            case 0x1002: return "Virtio Balloon";
            case 0x1003: return "Virtio Console";
            case 0x1004: return "Virtio SCSI";
            case 0x1005: return "Virtio Entropy";
            case 0x1041: return "Virtio Network (modern)";
            case 0x1042: return "Virtio Block (modern)";
            default:     return "Virtio device";
        }
    }
    return "?";
}

static u32 pci_bar_size(u8 bus, u8 slot, u8 func, u8 bar_index) {
    u8 off = 0x10 + bar_index * 4;
    u32 orig = pci_read32_raw(bus, slot, func, off);

    if (orig & 0x1) return 0;
    if ((orig & 0x6) == 0x4) return 0;

    pci_write32_raw(bus, slot, func, off, 0xFFFFFFFF);
    u32 mask = pci_read32_raw(bus, slot, func, off);
    pci_write32_raw(bus, slot, func, off, orig);

    if (mask == 0 || mask == 0xFFFFFFFF) return 0;
    mask &= 0xFFFFFFF0u;
    if (mask == 0) return 0;

    return (~mask) + 1;
}

static void pci_dump_device(const pci_device_t *d) {
    uart_puts("  device  : ");
    uart_put_hex16(d->vendor_id); uart_putc(':');
    uart_put_hex16(d->device_id);
    uart_puts("  ");
    uart_puts(pci_vendor_name(d->vendor_id));
    uart_puts(" ");
    uart_puts(pci_device_name(d->vendor_id, d->device_id));
    uart_puts("\n");

    uart_puts("  class   : ");
    uart_puts(pci_class_name(d->class_code));
    uart_puts(" / ");
    uart_puts(pci_subclass_name(d->class_code, d->subclass));
    uart_puts("  (");
    uart_put_hex8(d->class_code); uart_putc(':');
    uart_put_hex8(d->subclass); uart_putc(':');
    uart_put_hex8(d->prog_if);
    uart_puts(")\n");

    uart_puts("  revision: 0x");
    uart_put_hex8(d->revision);
    uart_puts("\n");

    uart_puts("  location: bus=");
    uart_put_dec(d->bus);
    uart_puts(" slot=");
    uart_put_dec(d->slot);
    uart_puts(" func=");
    uart_put_dec(d->func);
    uart_puts("\n");

    if (d->irq_line != 0xFF) {
        uart_puts("  IRQ     : line=");
        uart_put_dec(d->irq_line);
        uart_puts(" pin=");
        uart_put_dec(d->irq_pin);
        uart_puts("\n");
    }

    uart_puts("  hdr_type: 0x");
    uart_put_hex8(d->header_type);
    uart_puts("\n");

    if ((d->header_type & 0x7F) == 0x00) {
        for (int i = 0; i < 6; i++) {
            if (d->bar[i] == 0 && d->bar_size[i] == 0) continue;

            uart_puts("  BAR");
            uart_put_dec(i);
            uart_puts("    : ");

            if (d->bar[i] & 0x1) {
                uart_puts("I/O  base=");
                uart_put_hex32(d->bar[i] & 0xFFFFFFFC);
            } else {
                uart_puts("MMIO base=");
                uart_put_hex32(d->bar[i] & 0xFFFFFFF0);
            }

            if (d->bar_size[i]) {
                uart_puts(" size=");
                uart_put_dec(d->bar_size[i]);
            }
            uart_puts("\n");
        }
    }
}

static dstatus_t pci_scan_device(u8 bus, u8 slot, u8 func) {
    if (g_device_count >= PCI_MAX_DEVICES)
        return DEPRESSURE_OUT_OF_MEMORY;

    u16 vendor = pci_read16_raw(bus, slot, func, 0x00);
    if (vendor == 0xFFFF || vendor == 0x0000)
        return DEPRESSURE_NOT_FOUND;

    u16 device = pci_read16_raw(bus, slot, func, 0x02);

    pci_device_t *d = &g_devices[g_device_count];

    d->bus         = bus;
    d->slot        = slot;
    d->func        = func;
    d->vendor_id   = vendor;
    d->device_id   = device;
    d->revision    = pci_read8_raw(bus, slot, func, 0x08);
    d->prog_if     = pci_read8_raw(bus, slot, func, 0x09);
    d->subclass    = pci_read8_raw(bus, slot, func, 0x0A);
    d->class_code  = pci_read8_raw(bus, slot, func, 0x0B);
    d->header_type = pci_read8_raw(bus, slot, func, 0x0E);
    d->irq_line    = pci_read8_raw(bus, slot, func, 0x3C);
    d->irq_pin     = pci_read8_raw(bus, slot, func, 0x3D);
    d->command     = pci_read16_raw(bus, slot, func, 0x04);
    d->status      = pci_read16_raw(bus, slot, func, 0x06);

    if ((d->header_type & 0x7F) == 0x00) {
        for (int i = 0; i < 6; i++) {
            d->bar[i]      = pci_read32_raw(bus, slot, func, 0x10 + i * 4);
            d->bar_size[i] = pci_bar_size(bus, slot, func, i);
        }
    } else {
        for (int i = 0; i < 6; i++) {
            d->bar[i]      = 0;
            d->bar_size[i] = 0;
        }
    }

    uart_puts("[");
    uart_put_dec(bus); uart_putc(':');
    uart_put_dec(slot); uart_putc('.');
    uart_put_dec(func);
    uart_puts("]\n");

    pci_dump_device(d);
    uart_puts("\n");

    g_device_count++;
    return DEPRESSURE_STATUS_OK;
}

static void pci_scan_bus(u8 bus) {
    for (u8 slot = 0; slot < 32; slot++) {
        u16 vendor = pci_read16_raw(bus, slot, 0, 0x00);
        if (vendor == 0xFFFF || vendor == 0x0000) continue;

        if (pci_scan_device(bus, slot, 0) == DEPRESSURE_OUT_OF_MEMORY) {
            g_scan_truncated = 1;
            return;
        }

        u8 header = pci_read8_raw(bus, slot, 0, 0x0E);
        if (header & 0x80) {
            for (u8 func = 1; func < 8; func++) {
                u16 v = pci_read16_raw(bus, slot, func, 0x00);
                if (v != 0xFFFF && v != 0x0000) {
                    if (pci_scan_device(bus, slot, func) ==
                        DEPRESSURE_OUT_OF_MEMORY) {
                        g_scan_truncated = 1;
                        return;
                    }
                }
            }
        }
    }
}

static dstatus_t api_get_count(u32 *out) {
    if (!out) return DEPRESSURE_NULL_DATA;
    *out = g_device_count;
    return DEPRESSURE_STATUS_OK;
}

static dstatus_t api_get_device(u32 index, const pci_device_t **out) {
    if (!out) return DEPRESSURE_NULL_DATA;
    if (index >= g_device_count) return DEPRESSURE_OUT_OF_RANGE;
    g_devices[index].command =
        pci_read16_raw(g_devices[index].bus, g_devices[index].slot,
                       g_devices[index].func, 0x04);
    g_devices[index].status =
        pci_read16_raw(g_devices[index].bus, g_devices[index].slot,
                       g_devices[index].func, 0x06);
    *out = &g_devices[index];
    return DEPRESSURE_STATUS_OK;
}

static dstatus_t api_find_by_class(u8 class_code, u8 subclass, u32 *out) {
    if (!out) return DEPRESSURE_NULL_DATA;
    for (u32 i = 0; i < g_device_count; i++) {
        if (g_devices[i].class_code == class_code &&
            g_devices[i].subclass == subclass) {
            *out = i;
            return DEPRESSURE_STATUS_OK;
        }
    }
    return DEPRESSURE_NOT_FOUND;
}

static dstatus_t api_find_by_id(u16 vendor_id, u16 device_id, u32 *out) {
    if (!out) return DEPRESSURE_NULL_DATA;
    for (u32 i = 0; i < g_device_count; i++) {
        if (g_devices[i].vendor_id == vendor_id &&
            g_devices[i].device_id == device_id) {
            *out = i;
            return DEPRESSURE_STATUS_OK;
        }
    }
    return DEPRESSURE_NOT_FOUND;
}

static dstatus_t api_read_config32(u8 bus, u8 slot, u8 func, u8 off, u32 *out) {
    if (!out) return DEPRESSURE_NULL_DATA;
    if (off & 0x3) return DEPRESSURE_ALIGNMENT_ERROR;
    *out = pci_read32_raw(bus, slot, func, off);
    return DEPRESSURE_STATUS_OK;
}

static dstatus_t api_write_config32(u8 bus, u8 slot, u8 func, u8 off, u32 value) {
    if (off & 0x3) return DEPRESSURE_ALIGNMENT_ERROR;
    pci_write32_raw(bus, slot, func, off, value);
    return DEPRESSURE_STATUS_OK;
}

static dstatus_t api_get_scan_truncated(u32 *out) {
    if (!out) return DEPRESSURE_NULL_DATA;
    *out = g_scan_truncated;
    return DEPRESSURE_STATUS_OK;
}

static pci_api_t g_pci_api;

static ddriver_t pci_driver = {
    .pattern = DDRIVER_PATTERN,
    .version = DDRIVER_VERSION,
    .next    = 0,
    .type    = DDRIVER_TYPE_OTHER,
    .name    = "pci",
    .entry   = 0,
    .api     = 0,
    .data    = 0,
};

void pci_main(void *api, void *fb_info) {
    (void)fb_info;

    uart_puts("[pci] === PCI scan ===\n");

    for (u32 i = 0; i < PCI_MAX_DEVICES; i++) {
        g_devices[i].vendor_id = 0;
    }
    g_device_count = 0;
    g_scan_truncated = 0;

    for (u32 bus = 0; bus < 256 && !g_scan_truncated; bus++) {
        pci_scan_bus((u8)bus);
    }

    uart_puts("[pci] devices found: ");
    uart_put_dec(g_device_count);
    uart_puts("\n");

    g_pci_api.version          = PCI_API_VERSION;
    g_pci_api._pad             = 0;
    g_pci_api.get_count        = api_get_count;
    g_pci_api.get_device       = api_get_device;
    g_pci_api.find_by_class    = api_find_by_class;
    g_pci_api.find_by_id       = api_find_by_id;
    g_pci_api.read_config32    = api_read_config32;
    g_pci_api.write_config32   = api_write_config32;
    g_pci_api.get_scan_truncated = api_get_scan_truncated;

    if (api && ((dkernel_api_t *)api)->register_driver) {
        pci_driver.api = (u64)&g_pci_api;
        ((dkernel_api_t *)api)->register_driver(&pci_driver);
        uart_puts("[pci] registered as 'pci'\n");
    }
}