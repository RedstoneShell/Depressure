#include "dtypes.h"
#include "dumcas.h"
#include "dsmp.h"
#include "dap.h"
#include "duefi.h"

typedef struct {
    u64 signature;
    u8  checksum;
    u8  oem_id[6];
    u8  revision;
    u32 rsdt_addr;
    u32 length;
    u64 xsdt_addr;
    u8  ext_checksum;
    u8  reserved[3];
} acpi_rsdp_t;

typedef struct {
    u64 signature;
    u32 length;
    u8  revision;
    u8  checksum;
    u8  oem_id[6];
    u8  oem_table_id[8];
    u32 oem_revision;
    u32 creator_id;
    u32 creator_revision;
} acpi_madt_t;

typedef struct {
    u8  type;
    u8  length;
    u8  acpi_processor_id;
    u8  apic_id;
    u32 flags;
} madt_entry_t;

static EFI_MP_SERVICES_PROTOCOL *g_mp = 0;
static volatile u64 g_workers_started = 0;
static volatile u64 g_tasks_done = 0;
u32 g_ncpu = 1;

static core_metrics_t g_core_metrics[64];

static int (*g_umcas_submit)(u32, u64, u64, u64, u64) = 0;

static volatile u64 *g_dbg = (volatile u64 *)0x301000;

static acpi_madt_t *g_madt = 0;
static u32 g_lapic_ids[64];
static u32 g_lapic_count = 0;
static u32 g_bsp_lapic_id = 0;

#define LAPIC_BASE  0xFEE00000ULL

extern u64 ap_started_count;

u64 dsmp_rdtsc(void) {
    u32 low, high;
    __asm__ volatile ("rdtsc" : "=a"(low), "=d"(high));
    return ((u64)high << 32) | low;
}

static void lapic_write(u32 reg, u32 value) {
    *(volatile u32 *)(LAPIC_BASE + reg) = value;
}

static u32 lapic_read(u32 reg) {
    return *(volatile u32 *)(LAPIC_BASE + reg);
}

u32 dsmp_get_lapic_id(void) {
    return lapic_read(0x20) >> 24;
}

u32 dsmp_lapic_to_core(u32 lapic_id) {
    for (u32 i = 0; i < g_lapic_count && i < 64; i++) {
        if (g_lapic_ids[i] == lapic_id) return i;
    }
    return 0;
}

static void acpi_find_madt(EFI_BOOT_SERVICES *bs) {
    (void)bs;

    u64 *sys_table = (u64 *)0x200100;
    u64 st = sys_table[0];

    if (!st) {
        g_dbg[100] = 0xFFFFFFFFFFFFFFFFULL;
        return;
    }

    u64 num_entries = *(u64 *)(st + 0x68);
    u64 cfg_table   = *(u64 *)(st + 0x70);

    g_dbg[100] = num_entries;

    if (!cfg_table || num_entries == 0 || num_entries > 256) {
        g_dbg[101] = 0xEEEEEEEEEEEEEEEEULL;
        return;
    }

    static const u8 acpi_20_guid[16] = {
        0x71, 0xe8, 0x68, 0x88, 0xf1, 0xe4, 0xd3, 0x11,
        0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81
    };

    for (u64 i = 0; i < num_entries; i++) {
        u8 *entry = (u8 *)(cfg_table + i * 24);
        u8 *guid = entry;
        u64 table = *(u64 *)(entry + 16);

        int match = 1;
        for (int k = 0; k < 16; k++) {
            if (guid[k] != acpi_20_guid[k]) { match = 0; break; }
        }

        if (match && table) {
            acpi_rsdp_t *rsdp = (acpi_rsdp_t *)table;
            g_dbg[102] = (u64)rsdp;

            if (rsdp->revision >= 2 && rsdp->xsdt_addr) {
                u8 *xsdt = (u8 *)rsdp->xsdt_addr;
                u32 xsdt_len = *(u32 *)(xsdt + 4);
                u32 count = (xsdt_len - 36) / 8;
                g_dbg[103] = count;

                for (u32 j = 0; j < count && j < 32; j++) {
                    u64 madt_addr = *(u64 *)(xsdt + 36 + j * 8);
                    acpi_madt_t *madt = (acpi_madt_t *)madt_addr;

                    if ((madt->signature & 0xFFFFFFFFULL) == 0x43495041ULL) {
                        g_madt = madt;
                        g_dbg[104] = madt_addr;
                        break;
                    }
                }
            }

            if (g_madt) break;
        }
    }

    if (!g_madt) {
        g_dbg[105] = 0xDDDDDDDDDDDDDDDDULL;
    }
}

static void acpi_parse_madt_entries(void) {
    if (!g_madt) return;

    u8 *base = (u8 *)g_madt;
    u32 length = g_madt->length;
    u32 offset = 44;

    g_lapic_count = 0;

    while (offset < length) {
        madt_entry_t *e = (madt_entry_t *)(base + offset);
        if (e->length == 0) break;

        if (e->type == 0) {
            u32 apic_id = e->apic_id;

            if (g_lapic_count < 64) {
                g_lapic_ids[g_lapic_count++] = apic_id;
            }

            if (g_lapic_count == 1) {
                g_bsp_lapic_id = apic_id;
            }
        }

        offset += e->length;
    }

    g_dbg[106] = g_lapic_count;
    if (g_lapic_count > 0) {
        g_dbg[107] = g_lapic_ids[0];
        if (g_lapic_count > 1) g_dbg[108] = g_lapic_ids[1];
        if (g_lapic_count > 2) g_dbg[109] = g_lapic_ids[2];
        if (g_lapic_count > 3) g_dbg[110] = g_lapic_ids[3];
    }

    if (g_lapic_count > 0) {
        g_ncpu = g_lapic_count;
    }
}

static void lapic_init(void) {
    lapic_write(0xF0, 0x1FF);
    lapic_write(0x80, 0);
    lapic_write(0xB0, 0);

    g_dbg[111] = lapic_read(0x20);
    g_dbg[112] = lapic_read(0xF0);
}

void dsmp_set_umcas_submit(int (*fn)(u32, u64, u64, u64, u64)) {
    g_umcas_submit = fn;
}

void dsmp_add_metrics(u32 core_id, u64 active, u64 total) {
    if (core_id >= 64) return;
    __atomic_add_fetch(&g_core_metrics[core_id].active_cycles, active, __ATOMIC_RELAXED);
    __atomic_add_fetch(&g_core_metrics[core_id].total_cycles,  total,  __ATOMIC_RELAXED);
}

void dsmp_update_loads(void) {
    for (u32 i = 0; i < g_ncpu && i < 64; i++) {
        if (i == 0) {
            u64 active = __atomic_load_n(&g_core_metrics[i].active_cycles, __ATOMIC_RELAXED);
            u64 total  = __atomic_load_n(&g_core_metrics[i].total_cycles,  __ATOMIC_RELAXED);
            u32 pct = 0;
            if (total > 0) {
                pct = (u32)((active * 100) / total);
                if (pct > 100) pct = 100;
            }
            g_core_metrics[i].load_percent = pct;
            __atomic_store_n(&g_core_metrics[i].active_cycles, 0, __ATOMIC_RELAXED);
            __atomic_store_n(&g_core_metrics[i].total_cycles,  0, __ATOMIC_RELAXED);
        } else {
            u64 idle = 0, active = 0, tasks = 0;
            d_ap_get_stats(i, &idle, &active, &tasks);
            u64 total = idle + active;
            u32 pct = 0;
            if (total > 0) {
                pct = (u32)((active * 100) / total);
                if (pct > 100) pct = 100;
            }
            g_core_metrics[i].load_percent = pct;
        }
    }
}

u32 dsmp_get_ncpu(void) {
    return g_ncpu;
}

u32 dsmp_get_load(u32 core_id) {
    if (core_id >= 64) return 0;
    return g_core_metrics[core_id].load_percent;
}

u32 dsmp_get_lapic_count(void) {
    return g_lapic_count;
}

u32 dsmp_get_bsp_lapic_id(void) {
    return g_bsp_lapic_id;
}

int dsmp_init(void *BootServices) {
    EFI_BOOT_SERVICES *bs = (EFI_BOOT_SERVICES *)BootServices;

    acpi_find_madt(bs);
    acpi_parse_madt_entries();
    lapic_init();

    d_ap_init_shared();
    d_ap_set_ncpu(g_lapic_count);
    for (u32 i = 0; i < g_lapic_count && i < D_AP_MAX_CORES; i++) {
        d_ap_set_core_lapic(i, g_lapic_ids[i]);
    }

    g_mp = uefi_locate_mp_services(BootServices);
    if (g_mp) {
        u64 num_cpus = 0, num_enabled = 0;
        u64 status = uefi_mp_get_num_processors(g_mp, &num_cpus, &num_enabled);
        if (status == 0) {
            g_dbg[50] = num_enabled;
        } else {
            g_dbg[50] = 0xEEEEEEEEEEEEEEEEULL;
        }
    } else {
        g_dbg[50] = 0xFFFFFFFFFFFFFFFFULL;
    }

    for (u32 i = 0; i < 64; i++) {
        g_core_metrics[i].total_cycles  = 0;
        g_core_metrics[i].active_cycles = 0;
        g_core_metrics[i].load_percent  = 0;
    }

    return (int)g_ncpu;
}

int dsmp_start_workers(void *bs) {
    (void)bs;

    if (!g_mp) {
        g_dbg[51] = 0xFFFFFFFFFFFFFFFFULL;
        return -1;
    }

    ap_started_count = 0;

    u64 num_cpus = 0, num_enabled = 0;
    uefi_mp_get_num_processors(g_mp, &num_cpus, &num_enabled);
    g_dbg[54] = num_enabled;

    u32 started = 0;
    for (u64 i = 1; i < num_enabled && i < D_AP_MAX_CORES; i++) {
        u64 s = uefi_mp_startup_this_ap(g_mp,
                                         (EFI_AP_PROCEDURE)ap_worker_asm,
                                         0, i, 0, 0);
        g_dbg[55 + i] = s;
        if (s == 0) started++;
    }

    g_dbg[60] = started;
    g_workers_started = started;
    return (started > 0) ? 0 : -1;
}

void dsmp_poll_workers(void) {
    if (!g_mp) return;

    for (u64 i = 1; i < g_ncpu && i < D_AP_MAX_CORES; i++) {
        uefi_mp_startup_this_ap(g_mp,
                                 (EFI_AP_PROCEDURE)ap_worker_asm,
                                 0, i, 0, 0);
    }
}

u64 dsmp_get_workers_started(void) {
    return g_workers_started;
}

u64 dsmp_get_tasks_done(void) {
    return g_tasks_done;
}