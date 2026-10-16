// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 RedstoneShell
//
// This file is part of the Depressure Operating System.

#include "dtypes.h"
#include "dfont8x8.h"
#include "dkrnl.h"
#include "pci.h"
#include "acpi.h"
#include "dbmp.h"
#include "dpng.h"
#include "hpet.h"

typedef struct uart_api {
    u32 version; u32 _pad;
    void (*putc)(char c);
    void (*puts)(const char *str);
    void (*put_hex)(u64 value);
    int  (*has_data)(void);
    char (*getc)(void);
} uart_api_t;

typedef struct dkbd_api {
    u32 version; u32 _pad;
    int  (*has_key)(void);
    char (*get_key)(void);
    u8   (*get_scancode)(void);
    int  (*has_arrow)(void);
    int  (*get_arrow)(void);
} dkbd_api_t;

typedef struct dmouse_api {
    u32 version; u32 _pad;
    int  (*has_event)(void);
    void (*get_state)(int *x, int *y, int *buttons);
    int  (*get_x)(void);
    int  (*get_y)(void);
    int  (*get_buttons)(void);
    void (*set_position)(int x, int y);
} dmouse_api_t;

typedef struct dws_api {
    u32 version; u32 _pad;
    void (*put_pixel)(u64 x, u64 y, u32 rgb);
    void (*fill)(u32 rgb);
    void (*fill_rect)(u64 x, u64 y, u64 w, u64 h, u32 rgb);
    u32  (*convert_color)(u32 rgb);
    void (*blit_scaled)(u64 x, u64 y, u64 dst_w, u64 dst_h,
                        const u32 *src, u64 src_w, u64 src_h);
    void (*draw_char)(u64 x, u64 y, char ch, u32 fg, u32 bg);
    void (*draw_text)(u64 x, u64 y, const char *str, u32 fg, u32 bg);
} dws_api_t;

static uart_api_t    *g_uart  = 0;
static dws_api_t     *g_dws   = 0;
static dkbd_api_t    *g_kbd   = 0;
static dkernel_api_t *g_api   = 0;
static dmouse_api_t  *g_mouse = 0;
static acpi_api_t    *g_acpi  = 0;
static pci_api_t     *g_pci   = 0;

static u64 g_screen_w = 800;
static u64 g_screen_h = 600;

#define COL_BG      0x00990000
#define COL_FG      0x00FFFFFF
#define COL_ACCENT  0x00FFCC00
#define COL_DARK    0x00660000
#define COL_CORE_BG 0x00CC0000
#define COL_CORE_ON 0x0000FF00
#define COL_CORE_OFF 0x00880000
#define COL_BTN     0x00330000
#define COL_BTN_ON  0x0000AA00

#define CHAR_W 16
#define CHAR_H 16

#define SCREEN_MAIN       0
#define SCREEN_HWMAP      1
#define SCREEN_PCI        2
#define SCREEN_MANAGEMENT 3
#define SCREEN_TIMER      4
#define SCREEN_UEFI       5
#define SCREEN_UEFI2      6

#define PCI_PAGE_SIZE 10
#define POWER_ACTION_NONE     0
#define POWER_ACTION_SHUTDOWN 1
#define POWER_ACTION_REBOOT   2
#define CORES_PER_ROW 5

#define BACKBUF_MAX_W 1024
#define BACKBUF_MAX_H 768
#define BACKBUF_ADDR  0x02000000ULL
#define BACKBUF_SIZE  ((u64)BACKBUF_MAX_W * BACKBUF_MAX_H * sizeof(u32))

#define DTIMER_SOURCE_PIT    0u
#define DTIMER_SOURCE_HPET   1u
#define DTIMER_SOURCE_LAPIC  2u
#define DTIMER_SOURCE_COUNT  3u

extern int acpi_start_cooling(void);

static int g_screen = SCREEN_MAIN;
static int g_dirty = 1;
static u32 g_pci_page = 0;

static u64 g_management_x = 0, g_management_y = 0;
static u64 g_management_w = 0, g_management_h = 0;
static u64 g_shutdown_x = 0, g_shutdown_y = 0;
static u64 g_reboot_x = 0,   g_reboot_y = 0;
static u64 g_confirm_x = 0,  g_confirm_y = 0;
static u64 g_cancel_x = 0,   g_cancel_y = 0;
static int g_power_confirmation = POWER_ACTION_NONE;
static dstatus_t g_power_action_status = DEPRESSURE_STATUS_OK;

static u64 g_icon_x = 0, g_icon_y = 0, g_icon_w = 0, g_icon_h = 0;
static u64 g_timer_icon_x = 0, g_timer_icon_y = 0;
static u64 g_timer_icon_w = 0, g_timer_icon_h = 0;

static u64 g_timer_btn_x[DTIMER_SOURCE_COUNT];
static u64 g_timer_btn_y = 0;
static u64 g_timer_btn_w = 200;
static u64 g_timer_btn_h = 56;

static int g_last_buttons = 0;
static int g_mouse_x = 400, g_mouse_y = 300;
static int g_prev_mouse_x = -1, g_prev_mouse_y = -1;

static u32 g_ncpu = 1;
static u32 g_ncpu_cpuid = 1;
static char g_cpu_vendor[13] = {0};
static char g_cpu_name[64] = {0};
static u32 g_core_load[64] = {0};
static u32 g_prev_core_load[64] = {0};

typedef struct cpu_info {
    u32 max_leaf;
    u32 max_ext_leaf;
    u32 family;
    u32 model;
    u32 stepping;
    u32 brand_idx;
    u32 clflush;
    u32 logical_cpu;
    u32 phys_bits;
    u32 virt_bits;

    u32 fpu;
    u32 vme;
    u32 de;
    u32 pse;
    u32 tsc;
    u32 msr;
    u32 pae;
    u32 mce;
    u32 cx8;
    u32 apic;
    u32 sep;
    u32 mtrr;
    u32 pge;
    u32 mca;
    u32 cmov;
    u32 pat;
    u32 pse36;
    u32 clflush_bit;
    u32 mmx;
    u32 fxsr;
    u32 sse;
    u32 sse2;
    u32 htt;
    u32 sse3;
    u32 pclmulqdq;
    u32 dtes64;
    u32 monitor;
    u32 ds_cpl;
    u32 vmx;
    u32 smx;
    u32 est;
    u32 tm2;
    u32 ssse3;
    u32 cnxt_id;
    u32 sdbg;
    u32 fma;
    u32 cx16;
    u32 xtpr;
    u32 pdcm;
    u32 pcid;
    u32 dca;
    u32 sse41;
    u32 sse42;
    u32 x2apic;
    u32 movbe;
    u32 popcnt;
    u32 tsc_deadline;
    u32 aesni;
    u32 xsave;
    u32 osxsave;
    u32 avx;
    u32 f16c;
    u32 rdrand;
    u32 hypervisor;

    u32 fsgsbase;
    u32 bmi1;
    u32 hle;
    u32 avx2;
    u32 smep;
    u32 bmi2;
    u32 erms;
    u32 invpcid;
    u32 rtm;
    u32 mpx;
    u32 avx512f;
    u32 avx512dq;
    u32 rdseed;
    u32 adx;
    u32 smap;
    u32 avx512ifma;
    u32 pcommit;
    u32 clflushopt;
    u32 clwb;
    u32 intel_pt;
    u32 avx512pf;
    u32 avx512er;
    u32 avx512cd;
    u32 sha;
    u32 avx512bw;
    u32 avx512vl;

    u32 prefetchwt1;
    u32 avx512vbmi;
    u32 umip;
    u32 pku;
    u32 ospke;
    u32 avx512_vbmi2;
    u32 cet_ss;
    u32 gfni;
    u32 vaes;
    u32 vpclmulqdq;
    u32 avx512_vnni;
    u32 avx512_bitalg;
    u32 tme;
    u32 avx512_vpopcntdq;

    u32 la57;
    u32 rdpid;
    u32 bus_lock_detect;
    u32 cldemote;
    u32 movdiri;
    u32 movdir64b;
    u32 pconfig;
    u32 pku_la57;

    u32 xcr0_supported;
    u32 xcr0_current;

    /* MMU (Control Registers) */
    u64 cr0;
    u64 cr2;
    u64 cr3;
    u64 cr4;
    u32 mmu_paging;
    u32 mmu_pae;
    u32 mmu_pse;
    u32 mmu_pge;
    u32 mmu_smep;
    u32 mmu_smap;
    u32 mmu_pcid;
    u32 mmu_la57;
    u32 mmu_sce;
    u32 mmu_nxe;
    u32 mmu_wp;
    u32 mmu_pe;
    u32 mmu_pg;
    u32 mmu_cd;
    u32 mmu_nw;
    u32 mmu_am;
    u32 mmu_vme;
    u32 mmu_pvi;
    u32 mmu_tsd;
    u32 mmu_de;
    u32 mmu_pse36;
    u32 mmu_pae_enabled;
    u32 mmu_pge_enabled;
    u32 mmu_osfxsr;
    u32 mmu_osxmmexcpt;
    u32 mmu_vmx_enabled;
    u32 mmu_smep_enabled;
    u32 mmu_smap_enabled;
    u32 mmu_pke;

    /* MCU (Memory Controller Unit) - PCI 0:0:0 */
    int mcu_found;
    u16 mcu_vendor_id;
    u16 mcu_device_id;
    u8  mcu_revision;
    u8  mcu_class;
    u8  mcu_subclass;
    u8  mcu_prog_if;
    u32 mcu_bar0;
    u32 mcu_bar1;
    u32 mcu_bar2;
    u32 mcu_bar3;
    u32 mcu_bar4;
    u32 mcu_bar5;
    u16 mcu_command;
    u16 mcu_status;
    u32 mcu_smram;        /* SMRAM control (offset 0x9E) */
    u32 mcu_tolud;        /* Top of Low Usable DRAM (offset 0xA0) */
    u32 mcu_tseg;         /* TSEG (offset 0xB8) */
    u32 mcu_remapbase;    /* Remap base (offset 0x90) */
    u32 mcu_remaplimit;   /* Remap limit (offset 0x98) */
    u32 mcu_memtype;      /* DRAM type register */
    u32 mcu_dram_freq;    /* Estimated DRAM freq (from ratio) */

    int vendor_intel;
    int vendor_amd;
} cpu_info_t;

static cpu_info_t g_cpu;

typedef struct pcu_info {
    int valid;

    /* CST */
    int cst_ok;
    u64 cst_raw;
    u32 cstate_limit;
    u32 cstate_cfg_lock;
    u32 cstate_io_mwait;
    u32 cstate_c1_auto;
    u32 cstate_c3_auto;

    /* Perf */
    int perf_status_ok;
    u64 perf_status_raw;
    u32 perf_status_ratio;
    u32 perf_status_vid;

    int perf_ctl_ok;
    u64 perf_ctl_raw;
    u32 perf_ctl_ratio;
    u32 perf_ctl_est;

    /* Package power */
    int pkg_pl_ok;
    u64 pkg_pl_raw;
    u32 pl1_watts;
    u32 pl1_enabled;
    u32 pl1_clamp;
    u32 pl1_lock;
    u32 pl1_time_ms;

    u32 pl2_watts;
    u32 pl2_enabled;
    u32 pl2_time_ms;

    /* Turbo Activation */
    int turbo_act_ok;
    u64 turbo_act_raw;
    u32 turbo_act_ratio;
    u32 turbo_act_locked;

    /* MBI (Mailbox) */
    int mbi_ok;
    u32 mbi_bus;
    u32 mbi_dev;
    u32 mbi_func;
    u32 mbi_mcr_off;
    u32 mbi_mdr_off;
    u32 mbi_mcr_val;
    u32 mbi_mdr_val;

    /* Thermal / PROCHOT (MSR 0x19C) */
    int     therm_ok;
    u64     therm_raw;
    u32     therm_digital_readout;
    u32     therm_prochot_log;
    u32     therm_prochot_now;
    u32     therm_power_limit_log;
    u32     therm_power_limit_now;
    u32     therm_thermal_status_log;
    u32     therm_thermal_status_now;
    u32     therm_crit_temp;
    u32     therm_temp_c;

    /* EC (Embedded Controller) */
    int     ec_ok;
    u8      ec_reg_00;
    u8      ec_reg_10;
    u8      ec_reg_1e;
    u8      ec_reg_1f;
    u8      ec_reg_20;
    u8      ec_reg_21;
    u16     ec_fan1_raw;
    u32     ec_fan1_rpm;
} pcu_info_t;

static pcu_info_t g_pcu;

static u32 *g_backbuf = (u32 *)BACKBUF_ADDR;
static u32  g_backbuf_w = 0;
static u32  g_backbuf_h = 0;
static int  g_drawn = 0;

static const char *ts_name(u32 s) {
    if (g_api && g_api->timer_source_name) return g_api->timer_source_name(s);
    switch (s) {
        case DTIMER_SOURCE_PIT:   return "PIT";
        case DTIMER_SOURCE_HPET:  return "HPET";
        case DTIMER_SOURCE_LAPIC: return "LAPIC";
        default:                  return "?";
    }
}

static void cpuid_ex(u32 leaf, u32 sub, u32 *eax, u32 *ebx, u32 *ecx, u32 *edx) {
    __asm__ volatile ("cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(sub));
}

static int msr_read(u32 msr, u64 *out) {
    u32 lo, hi;
    if (!g_cpu.msr) return 0;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    *out = ((u64)hi << 32) | lo;
    return 1;
}

static inline u8 ec_inb(u16 port) {
    u8 v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void ec_outb(u16 port, u8 v) {
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(port));
}

static int ec_wait_ibf_clear(void) {
    for (int i = 0; i < 10000; i++) {
        if ((ec_inb(0x66) & 0x02) == 0) return 1;
    }
    return 0;
}

static int ec_wait_obf_set(void) {
    for (int i = 0; i < 10000; i++) {
        if ((ec_inb(0x66) & 0x01) != 0) return 1;
    }
    return 0;
}

static int ec_read(u8 addr, u8 *out) {
    if (!out) return 0;
    if (!ec_wait_ibf_clear()) return 0;
    ec_outb(0x66, 0x80);
    if (!ec_wait_ibf_clear()) return 0;
    ec_outb(0x62, addr);
    if (!ec_wait_obf_set()) return 0;
    *out = ec_inb(0x62);
    return 1;
}

static void ec_probe(void) {
    u8 v;

    if (!ec_read(0x00, &v)) {
        g_pcu.ec_ok = 0;
        return;
    }
    g_pcu.ec_ok = 1;

    ec_read(0x00, &g_pcu.ec_reg_00);
    ec_read(0x10, &g_pcu.ec_reg_10);
    ec_read(0x1E, &g_pcu.ec_reg_1e);
    ec_read(0x1F, &g_pcu.ec_reg_1f);
    ec_read(0x20, &g_pcu.ec_reg_20);
    ec_read(0x21, &g_pcu.ec_reg_21);

    g_pcu.ec_fan1_raw = ((u16)g_pcu.ec_reg_1f << 8) | g_pcu.ec_reg_1e;

    /* IT8572E: RPM = 60 * 1000000 / (12800 * TACH * P), P = 2 */
    if (g_pcu.ec_fan1_raw > 0) {
        u32 rpm = (u32)(60ULL * 1000000ULL /
                        (12800ULL * (u32)g_pcu.ec_fan1_raw * 2ULL));
        g_pcu.ec_fan1_rpm = rpm;
    } else {
        g_pcu.ec_fan1_rpm = 0;
    }
}

static void pcu_detect(void) {
    if (!g_cpu.vendor_intel) return;

    u64 v;

    if (msr_read(0xE2, &v)) {
        g_pcu.cst_ok       = 1;
        g_pcu.cst_raw      = v;
        g_pcu.cstate_limit   = (u32)(v & 0x7);
        g_pcu.cstate_cfg_lock = (u32)((v >> 15) & 1);
        g_pcu.cstate_io_mwait = (u32)((v >>  1) & 1);
        g_pcu.cstate_c1_auto  = (u32)((v >>  3) & 1);
        g_pcu.cstate_c3_auto  = (u32)((v >>  4) & 1);
    }

    if (msr_read(0x198, &v)) {
        g_pcu.perf_status_ok = 1;
        g_pcu.perf_status_raw = v;
        g_pcu.perf_status_ratio = (u32)((v >> 8) & 0xFF);
        g_pcu.perf_status_vid   = (u32)((v >> 32) & 0xFFFF);
    }

    if (msr_read(0x199, &v)) {
        g_pcu.perf_ctl_ok = 1;
        g_pcu.perf_ctl_raw = v;
        g_pcu.perf_ctl_ratio = (u32)((v >> 8) & 0xFF);
        g_pcu.perf_ctl_est   = (u32)((v >> 16) & 1);
    }

    if (msr_read(0x19C, &v)) {
        g_pcu.therm_ok = 1;
        g_pcu.therm_raw = v;
        g_pcu.therm_digital_readout = (u32)(v & 1);
        g_pcu.therm_thermal_status_log = (u32)((v >> 1) & 1);
        g_pcu.therm_prochot_log = (u32)((v >> 2) & 1);
        g_pcu.therm_prochot_now = (u32)((v >> 3) & 1);
        g_pcu.therm_thermal_status_now = (u32)((v >> 4) & 1);
        g_pcu.therm_power_limit_log = (u32)((v >> 5) & 1);
        g_pcu.therm_power_limit_now = (u32)((v >> 10) & 1);
        u32 dr = (u32)((v >> 16) & 0x7F);
        if (dr <= 105) {
            g_pcu.therm_temp_c = 105 - dr;
        } else {
            g_pcu.therm_temp_c = 0;
        }
        g_pcu.therm_crit_temp = (u32)((v >> 27) & 0xF);
    }

    g_pcu.valid = (g_pcu.cst_ok || g_pcu.perf_status_ok ||
                   g_pcu.perf_ctl_ok || g_pcu.pkg_pl_ok ||
                   g_pcu.turbo_act_ok);
}

static inline u64 read_cr0(void) {
    u64 v; __asm__ volatile ("mov %%cr0, %0" : "=r"(v)); return v;
}
static inline u64 read_cr2(void) {
    u64 v; __asm__ volatile ("mov %%cr2, %0" : "=r"(v)); return v;
}
static inline u64 read_cr3(void) {
    u64 v; __asm__ volatile ("mov %%cr3, %0" : "=r"(v)); return v;
}
static inline u64 read_cr4(void) {
    u64 v; __asm__ volatile ("mov %%cr4, %0" : "=r"(v)); return v;
}

static void mmu_detect(void) {
    g_cpu.cr0 = read_cr0();
    g_cpu.cr2 = read_cr2();
    g_cpu.cr3 = read_cr3();
    g_cpu.cr4 = read_cr4();
    /* CR0 */
    g_cpu.mmu_pe  = (u32)(g_cpu.cr0 & 1);          /* PE: Protection Enable */
    g_cpu.mmu_wp  = (u32)((g_cpu.cr0 >> 16) & 1);  /* WP: Write Protect */
    g_cpu.mmu_am  = (u32)((g_cpu.cr0 >> 18) & 1);  /* AM: Alignment Mask */
    g_cpu.mmu_nw  = (u32)((g_cpu.cr0 >> 29) & 1);  /* NW: Not Write-through */
    g_cpu.mmu_cd  = (u32)((g_cpu.cr0 >> 30) & 1);  /* CD: Cache Disable */
    g_cpu.mmu_pg  = (u32)((g_cpu.cr0 >> 31) & 1);  /* PG: Paging */
    /* CR4 */
    g_cpu.mmu_vme        = (u32)(g_cpu.cr4 & 1);           /* VME */
    g_cpu.mmu_pvi        = (u32)((g_cpu.cr4 >> 1) & 1);    /* PVI */
    g_cpu.mmu_tsd        = (u32)((g_cpu.cr4 >> 2) & 1);    /* TSD */
    g_cpu.mmu_de         = (u32)((g_cpu.cr4 >> 3) & 1);    /* DE */
    g_cpu.mmu_pse        = (u32)((g_cpu.cr4 >> 4) & 1);    /* PSE */
    g_cpu.mmu_pae        = (u32)((g_cpu.cr4 >> 5) & 1);    /* PAE */
    g_cpu.mmu_pge        = (u32)((g_cpu.cr4 >> 7) & 1);    /* PGE */
    g_cpu.mmu_pse36      = (u32)((g_cpu.cr4 >> 7) & 1);    /* PSE36 (same bit?) */
    g_cpu.mmu_osfxsr     = (u32)((g_cpu.cr4 >> 9) & 1);    /* OSFXSR */
    g_cpu.mmu_osxmmexcpt = (u32)((g_cpu.cr4 >> 10) & 1);   /* OSXMMEXCPT */
    g_cpu.mmu_vmx_enabled = (u32)((g_cpu.cr4 >> 13) & 1);  /* VMXE */
    g_cpu.mmu_smep_enabled = (u32)((g_cpu.cr4 >> 20) & 1); /* SMEP */
    g_cpu.mmu_smap_enabled = (u32)((g_cpu.cr4 >> 21) & 1); /* SMAP */
    g_cpu.mmu_pke        = (u32)((g_cpu.cr4 >> 22) & 1);   /* PKE */
    g_cpu.mmu_pcid       = (u32)((g_cpu.cr4 >> 17) & 1);   /* PCIDE */
    g_cpu.mmu_la57       = (u32)((g_cpu.cr4 >> 12) & 1);   /* LA57 */
    g_cpu.mmu_sce        = (u32)((g_cpu.cr4 >> 8) & 1);    /* PCE */
}

static u32 mcu_pci_read32(u8 bus, u8 slot, u8 func, u8 off) {
    u32 addr = 0x80000000u
             | ((u32)bus << 16)
             | ((u32)slot << 11)
             | ((u32)func << 8)
             | (off & 0xFC);
    __asm__ volatile ("outl %0, %1" : : "a"(addr), "Nd"((u16)0xCF8));
    u32 v;
    __asm__ volatile ("inl %1, %0" : "=a"(v) : "Nd"((u16)0xCFC));
    return v;
}

static void mcu_detect(void) {
    /* Bus 0, Dev 0, Func 0 = Host Bridge / Memory Controller */
    u32 id = mcu_pci_read32(0, 0, 0, 0x00);
    if (id == 0xFFFFFFFF || id == 0) {
        g_cpu.mcu_found = 0;
        return;
    }
    g_cpu.mcu_found = 1;
    g_cpu.mcu_vendor_id = (u16)(id & 0xFFFF);
    g_cpu.mcu_device_id = (u16)((id >> 16) & 0xFFFF);
    u32 class_rev = mcu_pci_read32(0, 0, 0, 0x08);
    g_cpu.mcu_revision = (u8)(class_rev & 0xFF);
    g_cpu.mcu_prog_if  = (u8)((class_rev >> 8) & 0xFF);
    g_cpu.mcu_subclass = (u8)((class_rev >> 16) & 0xFF);
    g_cpu.mcu_class    = (u8)((class_rev >> 24) & 0xFF);
    u32 cmd_sts = mcu_pci_read32(0, 0, 0, 0x04);
    g_cpu.mcu_command = (u16)(cmd_sts & 0xFFFF);
    g_cpu.mcu_status  = (u16)((cmd_sts >> 16) & 0xFFFF);
    /* BAR0..BAR5 */
    g_cpu.mcu_bar0 = mcu_pci_read32(0, 0, 0, 0x10);
    g_cpu.mcu_bar1 = mcu_pci_read32(0, 0, 0, 0x14);
    g_cpu.mcu_bar2 = mcu_pci_read32(0, 0, 0, 0x18);
    g_cpu.mcu_bar3 = mcu_pci_read32(0, 0, 0, 0x1C);
    g_cpu.mcu_bar4 = mcu_pci_read32(0, 0, 0, 0x20);
    g_cpu.mcu_bar5 = mcu_pci_read32(0, 0, 0, 0x24);
    g_cpu.mcu_remapbase  = mcu_pci_read32(0, 0, 0, 0x90);
    g_cpu.mcu_remaplimit = mcu_pci_read32(0, 0, 0, 0x98);
    g_cpu.mcu_smram      = mcu_pci_read32(0, 0, 0, 0x9E) & 0xFF;
    g_cpu.mcu_tolud      = mcu_pci_read32(0, 0, 0, 0xA0);
    g_cpu.mcu_tseg       = mcu_pci_read32(0, 0, 0, 0xB8);
    g_cpu.mcu_memtype = mcu_pci_read32(0, 0, 0, 0x40);
    u32 ddr_ratio_reg = mcu_pci_read32(0, 0, 0, 0x44);
    u32 ratio = (ddr_ratio_reg >> 8) & 0xFF;
    if (ratio > 0 && ratio < 0x40) {
        g_cpu.mcu_dram_freq = ratio * 100;
    } else {
        g_cpu.mcu_dram_freq = 1333;
    }
}

static void cpu_detect(void) {
    u32 eax, ebx, ecx, edx;

    cpuid_ex(0, 0, &eax, &ebx, &ecx, &edx);
    g_cpu.max_leaf = eax;
    *(u32 *)&g_cpu_vendor[0] = ebx;
    *(u32 *)&g_cpu_vendor[4] = edx;
    *(u32 *)&g_cpu_vendor[8] = ecx;
    g_cpu_vendor[12] = 0;

    g_cpu.vendor_intel = (ebx == 0x756E6547 && edx == 0x49656E69 && ecx == 0x6C65746E);
    g_cpu.vendor_amd   = (ebx == 0x68747541 && edx == 0x69746E65 && ecx == 0x444D4163);

    cpuid_ex(0x80000000, 0, &eax, &ebx, &ecx, &edx);
    g_cpu.max_ext_leaf = eax;

    cpuid_ex(1, 0, &eax, &ebx, &ecx, &edx);
    g_cpu.stepping  = eax & 0xF;
    g_cpu.model     = (eax >> 4) & 0xF;
    g_cpu.family    = (eax >> 8) & 0xF;
    u32 ext_model   = (eax >> 16) & 0xF;
    u32 ext_family  = (eax >> 20) & 0xFF;
    if (g_cpu.family == 0xF) g_cpu.family += ext_family;
    if (g_cpu.family == 0x6 || g_cpu.family == 0xF) g_cpu.model += (ext_model << 4);
    g_cpu.brand_idx = ebx & 0xFF;
    g_cpu.clflush   = (ebx >> 8) & 0xFF;
    g_cpu.logical_cpu = (ebx >> 16) & 0xFF;
    g_cpu.phys_bits = (eax >> 24) & 0xFF;
    g_cpu.virt_bits = (eax >> 16) & 0xFF;

    g_cpu.fpu     = (edx >>  0) & 1;
    g_cpu.vme     = (edx >>  1) & 1;
    g_cpu.de      = (edx >>  2) & 1;
    g_cpu.pse     = (edx >>  3) & 1;
    g_cpu.tsc     = (edx >>  4) & 1;
    g_cpu.msr     = (edx >>  5) & 1;
    g_cpu.pae     = (edx >>  6) & 1;
    g_cpu.mce     = (edx >>  7) & 1;
    g_cpu.cx8     = (edx >>  8) & 1;
    g_cpu.apic    = (edx >>  9) & 1;
    g_cpu.sep     = (edx >> 11) & 1;
    g_cpu.mtrr    = (edx >> 12) & 1;
    g_cpu.pge     = (edx >> 13) & 1;
    g_cpu.mca     = (edx >> 14) & 1;
    g_cpu.cmov    = (edx >> 15) & 1;
    g_cpu.pat     = (edx >> 16) & 1;
    g_cpu.pse36   = (edx >> 17) & 1;
    g_cpu.clflush_bit = (edx >> 19) & 1;
    g_cpu.mmx     = (edx >> 23) & 1;
    g_cpu.fxsr    = (edx >> 24) & 1;
    g_cpu.sse     = (edx >> 25) & 1;
    g_cpu.sse2    = (edx >> 26) & 1;
    g_cpu.htt     = (edx >> 28) & 1;
    g_cpu.tm2     = (edx >> 29) & 1;

    g_cpu.sse3       = (ecx >>  0) & 1;
    g_cpu.pclmulqdq  = (ecx >>  1) & 1;
    g_cpu.dtes64     = (ecx >>  2) & 1;
    g_cpu.monitor    = (ecx >>  3) & 1;
    g_cpu.ds_cpl     = (ecx >>  4) & 1;
    g_cpu.vmx        = (ecx >>  5) & 1;
    g_cpu.smx        = (ecx >>  6) & 1;
    g_cpu.est        = (ecx >>  7) & 1;
    g_cpu.ssse3      = (ecx >>  9) & 1;
    g_cpu.cnxt_id    = (ecx >> 10) & 1;
    g_cpu.sdbg       = (ecx >> 11) & 1;
    g_cpu.fma        = (ecx >> 12) & 1;
    g_cpu.cx16       = (ecx >> 13) & 1;
    g_cpu.xtpr       = (ecx >> 14) & 1;
    g_cpu.pdcm       = (ecx >> 15) & 1;
    g_cpu.pcid       = (ecx >> 17) & 1;
    g_cpu.dca        = (ecx >> 18) & 1;
    g_cpu.sse41      = (ecx >> 19) & 1;
    g_cpu.sse42      = (ecx >> 20) & 1;
    g_cpu.x2apic     = (ecx >> 21) & 1;
    g_cpu.movbe      = (ecx >> 22) & 1;
    g_cpu.popcnt     = (ecx >> 23) & 1;
    g_cpu.tsc_deadline = (ecx >> 24) & 1;
    g_cpu.aesni      = (ecx >> 25) & 1;
    g_cpu.xsave      = (ecx >> 26) & 1;
    g_cpu.osxsave    = (ecx >> 27) & 1;
    g_cpu.avx        = (ecx >> 28) & 1;
    g_cpu.f16c       = (ecx >> 29) & 1;
    g_cpu.rdrand     = (ecx >> 30) & 1;
    g_cpu.hypervisor = (ecx >> 31) & 1;

    if (g_cpu.max_leaf >= 7) {
        cpuid_ex(7, 0, &eax, &ebx, &ecx, &edx);

        g_cpu.fsgsbase   = (ebx >>  0) & 1;
        g_cpu.bmi1       = (ebx >>  3) & 1;
        g_cpu.hle        = (ebx >>  4) & 1;
        g_cpu.avx2       = (ebx >>  5) & 1;
        g_cpu.smep       = (ebx >>  7) & 1;
        g_cpu.bmi2       = (ebx >>  8) & 1;
        g_cpu.erms       = (ebx >>  9) & 1;
        g_cpu.invpcid    = (ebx >> 10) & 1;
        g_cpu.rtm        = (ebx >> 11) & 1;
        g_cpu.mpx        = (ebx >> 14) & 1;
        g_cpu.avx512f    = (ebx >> 16) & 1;
        g_cpu.avx512dq   = (ebx >> 17) & 1;
        g_cpu.rdseed     = (ebx >> 18) & 1;
        g_cpu.adx        = (ebx >> 19) & 1;
        g_cpu.smap       = (ebx >> 20) & 1;
        g_cpu.avx512ifma = (ebx >> 21) & 1;
        g_cpu.pcommit    = (ebx >> 22) & 1;
        g_cpu.clflushopt = (ebx >> 23) & 1;
        g_cpu.clwb       = (ebx >> 24) & 1;
        g_cpu.intel_pt   = (ebx >> 25) & 1;
        g_cpu.avx512pf   = (ebx >> 26) & 1;
        g_cpu.avx512er   = (ebx >> 27) & 1;
        g_cpu.avx512cd   = (ebx >> 28) & 1;
        g_cpu.sha        = (ebx >> 29) & 1;
        g_cpu.avx512bw   = (ebx >> 30) & 1;
        g_cpu.avx512vl   = (ebx >> 31) & 1;

        g_cpu.prefetchwt1  = (ecx >>  0) & 1;
        g_cpu.avx512vbmi   = (ecx >>  1) & 1;
        g_cpu.umip         = (ecx >>  2) & 1;
        g_cpu.pku          = (ecx >>  3) & 1;
        g_cpu.ospke        = (ecx >>  4) & 1;
        g_cpu.avx512_vbmi2 = (ecx >>  6) & 1;
        g_cpu.cet_ss       = (ecx >>  7) & 1;
        g_cpu.gfni         = (ecx >>  8) & 1;
        g_cpu.vaes         = (ecx >>  9) & 1;
        g_cpu.vpclmulqdq   = (ecx >> 10) & 1;
        g_cpu.avx512_vnni  = (ecx >> 11) & 1;
        g_cpu.avx512_bitalg = (ecx >> 12) & 1;
        g_cpu.tme          = (ecx >> 13) & 1;
        g_cpu.avx512_vpopcntdq = (ecx >> 14) & 1;
        g_cpu.la57         = (ecx >> 16) & 1;
        g_cpu.rdpid        = (ecx >> 22) & 1;
        g_cpu.bus_lock_detect = (ecx >> 24) & 1;
        g_cpu.cldemote     = (ecx >> 25) & 1;
        g_cpu.movdiri      = (ecx >> 27) & 1;
        g_cpu.movdir64b    = (ecx >> 28) & 1;
        g_cpu.pconfig      = (ecx >> 29) & 1;
        g_cpu.pku_la57     = (ecx >> 30) & 1;
    }

    if (g_cpu.xsave) {
        u32 a, b, c, d;
        cpuid_ex(0x0D, 0, &a, &b, &c, &d);
        g_cpu.xcr0_supported = a;
        if (g_cpu.osxsave) {
            u32 lo, hi;
            __asm__ volatile ("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
            g_cpu.xcr0_current = lo;
        }
    }

    int i = 0;
    if (g_cpu.brand_idx != 0 && g_cpu.max_ext_leaf >= 0x80000004) {
        u32 buf[12];
        for (u32 leaf = 0; leaf < 3; leaf++) {
            cpuid_ex(0x80000002 + leaf, 0, &buf[leaf*4+0], &buf[leaf*4+1],
                     &buf[leaf*4+2], &buf[leaf*4+3]);
        }
        const char *brand = (const char *)buf;
        while (i < 48 && brand[i] != 0 && brand[i] != ' ') { g_cpu_name[i] = brand[i]; i++; }
        while (i > 0 && g_cpu_name[i-1] == ' ') i--;
        g_cpu_name[i++] = ' ';
        g_cpu_name[i++] = 'F';
        g_cpu_name[i++] = '0' + (g_cpu.family % 10);
        g_cpu_name[i++] = 'M';
        g_cpu_name[i++] = '0' + ((g_cpu.model / 10) % 10);
        g_cpu_name[i++] = '0' + (g_cpu.model % 10);
        g_cpu_name[i] = 0;
    } else {
        const char *v = g_cpu_vendor;
        while (v[i] && i < 48) { g_cpu_name[i] = v[i]; i++; }
        g_cpu_name[i++] = ' ';
        g_cpu_name[i++] = 'F';
        g_cpu_name[i++] = '0' + (g_cpu.family % 10);
        g_cpu_name[i++] = 'M';
        g_cpu_name[i++] = '0' + ((g_cpu.model / 10) % 10);
        g_cpu_name[i++] = '0' + (g_cpu.model % 10);
        g_cpu_name[i] = 0;
    }

    if (g_cpu.logical_cpu > 0) g_ncpu_cpuid = g_cpu.logical_cpu;

    if (g_cpu.max_ext_leaf >= 0x80000008) {
        cpuid_ex(0x80000008, 0, &eax, &ebx, &ecx, &edx);
        g_cpu.phys_bits = eax & 0xFF;
        g_cpu.virt_bits = (eax >> 8) & 0xFF;
    }

    mmu_detect();
    mcu_detect();
    pcu_detect();
    ec_probe();
}

static void bb_clear(u32 rgb) {
    u32 n = g_backbuf_w * g_backbuf_h;
    for (u32 i = 0; i < n; i++) g_backbuf[i] = rgb;
}

static void bb_pixel(u64 x, u64 y, u32 rgb) {
    if (x >= g_backbuf_w || y >= g_backbuf_h) return;
    g_backbuf[y * g_backbuf_w + x] = rgb;
}

static void bb_fill_rect(u64 x, u64 y, u64 w, u64 h, u32 rgb) {
    if (x >= g_backbuf_w || y >= g_backbuf_h) return;
    if (x + w > g_backbuf_w) w = g_backbuf_w - x;
    if (y + h > g_backbuf_h) h = g_backbuf_h - y;
    for (u64 dy = 0; dy < h; dy++) {
        u32 *row = &g_backbuf[(y + dy) * g_backbuf_w + x];
        for (u64 dx = 0; dx < w; dx++) row[dx] = rgb;
    }
}

static void bb_char(u64 x, u64 y, char ch, u32 fg, u32 bg, const u8 *font) {
    if (ch < 32 || ch > 126) ch = '?';
    const u8 *g = font + (u8)(ch - 32) * 8;
    for (u64 row = 0; row < 8; row++) {
        u8 bits = g[row];
        for (u64 col = 0; col < 8; col++) {
            u32 c = (bits & (1 << col)) ? fg : bg;
            bb_fill_rect(x + col, y + row, 1, 1, c);
        }
    }
}

static void bb_puts_small(u64 x, u64 y, const char *s, u32 fg, u32 bg, const u8 *font) {
    u64 cx = x;
    while (*s) {
        if (*s == '\n') { cx = x; y += 8; }
        else { bb_char(cx, y, *s, fg, bg, font); cx += 8; }
        s++;
    }
}

static void bb_char2(u64 x, u64 y, char ch, u32 fg, u32 bg, const u8 *font) {
    if (ch < 32 || ch > 126) ch = '?';
    const u8 *g = font + (u8)(ch - 32) * 8;
    for (u64 row = 0; row < 8; row++) {
        u8 bits = g[row];
        for (u64 col = 0; col < 8; col++) {
            u32 c = (bits & (1 << col)) ? fg : bg;
            bb_fill_rect(x + col*2, y + row*2, 2, 2, c);
        }
    }
}

static void bb_puts(u64 x, u64 y, const char *s, u32 fg, u32 bg, const u8 *font) {
    u64 cx = x;
    while (*s) {
        if (*s == '\n') { cx = x; y += CHAR_H; }
        else { bb_char2(cx, y, *s, fg, bg, font); cx += CHAR_W; }
        s++;
    }
}

static u64 bb_u64(u64 x, u64 y, u64 v, u32 fg, u32 bg, const u8 *font) {
    char b[24]; int i = 0;
    if (v == 0) b[i++] = '0';
    else while (v > 0) { b[i++] = '0' + (v % 10); v /= 10; }
    b[i] = 0;
    for (int k = 0; k < i/2; k++) { char t = b[k]; b[k] = b[i-1-k]; b[i-1-k] = t; }
    bb_puts(x, y, b, fg, bg, font);
    return x + (u64)i * CHAR_W;
}

static u64 bb_hex(u64 x, u64 y, u64 v, int digits, u32 fg, u32 bg, const u8 *font) {
    static const char h[] = "0123456789ABCDEF";
    char b[20]; int i = 0;
    b[i++] = '0'; b[i++] = 'x';
    for (int k = (digits - 1) * 4; k >= 0; k -= 4) b[i++] = h[(v >> k) & 0xF];
    b[i] = 0;
    bb_puts(x, y, b, fg, bg, font);
    return x + (u64)i * CHAR_W;
}

static u64 bb_hex16(u64 x, u64 y, u16 v, u32 fg, u32 bg, const u8 *font) {
    static const char h[] = "0123456789ABCDEF";
    char b[5];
    for (int i = 0; i < 4; i++) b[i] = h[(v >> (12 - i*4)) & 0xF];
    b[4] = 0;
    bb_puts(x, y, b, fg, bg, font);
    return x + 4 * CHAR_W;
}

static void bb_button(u64 x, u64 y, u64 w, u64 h, const char *lbl,
                      u32 border, u32 fill, const u8 *font) {
    bb_fill_rect(x, y, w, h, fill);
    bb_fill_rect(x, y, w, 2, border);
    bb_fill_rect(x, y + h - 2, w, 2, border);
    bb_fill_rect(x, y, 2, h, border);
    bb_fill_rect(x + w - 2, y, 2, h, border);
    u64 ll = 0; while (lbl[ll]) ll++;
    bb_puts(x + (w - ll*CHAR_W)/2, y + (h - CHAR_H)/2, lbl, COL_FG, fill, font);
}

static void badge(u64 x, u64 y, const char *name, int on, const u8 *font) {
    u32 fg = on ? COL_FG : 0x00888888;
    u32 bg = on ? 0x00006600 : 0x00330000;
    u64 n = 0; while (name[n]) n++;
    u64 w = n * 8 + 6;
    bb_fill_rect(x, y, w, 10, bg);
    bb_puts_small(x + 3, y + 1, name, fg, bg, font);
}

static void badge_hex(u64 x, u64 y, const char *name, u64 val, int on, const u8 *font) {
    u32 fg = on ? COL_FG : 0x00888888;
    u32 bg = on ? 0x00006600 : 0x00330000;
    static const char h[] = "0123456789ABCDEF";
    char buf[32];
    u64 n = 0;
    while (name[n]) { buf[n] = name[n]; n++; }
    buf[n++] = ':';
    for (int k = 28; k >= 0; k -= 4) buf[n++] = h[(val >> k) & 0xF];
    buf[n] = 0;
    u64 w = n * 8 + 6;
    bb_fill_rect(x, y, w, 10, bg);
    bb_puts_small(x + 3, y + 1, buf, fg, bg, font);
}

typedef struct uefi_info {
    u64 system_table;
    u64 boot_services;
    u64 con_out;
    u64 image_handle;
    u64 runtime_services;
    u64 config_tables;

    u32 st_hdr_size;
    u32 st_hdr_rev;
    u32 st_fw_vendor;
    u32 st_fw_rev;
    u32 cfg_count;

    u32 bs_hdr_size;
    u32 bs_hdr_rev;

    u32 rt_hdr_size;
    u32 rt_hdr_rev;

    int valid;
} uefi_info_t;

static uefi_info_t g_uefi;

static void uefi_probe(void) {
    volatile u64 *p = (volatile u64 *)0x200100;
    g_uefi.system_table  = p[0];
    g_uefi.boot_services = p[1];
    g_uefi.con_out       = p[2];
    g_uefi.image_handle  = p[3];

    if (!g_uefi.system_table) { g_uefi.valid = 0; return; }

    u64 st = g_uefi.system_table;

    g_uefi.st_hdr_size = *(volatile u32 *)(st + 0x0C);
    g_uefi.st_hdr_rev  = *(volatile u32 *)(st + 0x08);
    g_uefi.st_fw_vendor = *(volatile u32 *)(st + 0x18);
    g_uefi.st_fw_rev    = *(volatile u32 *)(st + 0x20);
    g_uefi.runtime_services = *(volatile u64 *)(st + 0x58);
    g_uefi.config_tables    = *(volatile u64 *)(st + 0x70);

    u64 num_cfg = *(volatile u64 *)(st + 0x68);
    if (num_cfg > 256) num_cfg = 256;
    g_uefi.cfg_count = (u32)num_cfg;

    if (g_uefi.runtime_services) {
        g_uefi.rt_hdr_size = *(volatile u32 *)(g_uefi.runtime_services + 0x0C);
        g_uefi.rt_hdr_rev  = *(volatile u32 *)(g_uefi.runtime_services + 0x08);
    }
    if (g_uefi.boot_services) {
        g_uefi.bs_hdr_size = *(volatile u32 *)(g_uefi.boot_services + 0x0C);
        g_uefi.bs_hdr_rev  = *(volatile u32 *)(g_uefi.boot_services + 0x08);
    }

    g_uefi.valid = 1;
}

static void draw_timer_screen(void) {
    const u8 *font = (const u8 *)font8x8;
    bb_clear(COL_BG);
    bb_puts(40, 20, "=== TIMER SOURCE ===", COL_ACCENT, COL_BG, font);

    u32 cur = (g_api && g_api->timer_get_source) ? g_api->timer_get_source() : 0;
    u32 hz  = (g_api && g_api->timer_get_hz)     ? g_api->timer_get_hz()     : 1000;

    bb_puts(40, 56, "Current:", COL_FG, COL_BG, font);
    bb_puts(40 + 10*CHAR_W, 56, ts_name(cur), COL_CORE_ON, COL_BG, font);
    bb_puts(40, 76, "Frequency:", COL_FG, COL_BG, font);
    u64 hx = bb_u64(40 + 12*CHAR_W, 76, hz, COL_FG, COL_BG, font);
    bb_puts(hx, 76, " Hz", COL_FG, COL_BG, font);
    bb_puts(40, 110, "Select timer source:", COL_ACCENT, COL_BG, font);

    g_timer_btn_y = 140;
    u64 tw = DTIMER_SOURCE_COUNT * g_timer_btn_w + (DTIMER_SOURCE_COUNT - 1) * 24;
    u64 sx = (g_screen_w - tw) / 2;
    for (u32 i = 0; i < DTIMER_SOURCE_COUNT; i++) {
        g_timer_btn_x[i] = sx + i * (g_timer_btn_w + 24);
        bb_button(g_timer_btn_x[i], g_timer_btn_y, g_timer_btn_w, g_timer_btn_h,
                  ts_name(i), COL_ACCENT, (i == cur) ? COL_BTN_ON : COL_BTN, font);
    }

    u64 y = g_timer_btn_y + g_timer_btn_h + 30;
    bb_puts(40, y, "PIT   - 8253/8254 programmable interval timer, IRQ0", COL_FG, COL_BG, font); y += CHAR_H + 4;
    bb_puts(40, y, "HPET  - High Precision Event Timer, comparator 0 -> IOAPIC GSI2", COL_FG, COL_BG, font); y += CHAR_H + 4;
    bb_puts(40, y, "LAPIC - Local APIC timer, per-CPU, LVT timer", COL_FG, COL_BG, font); y += CHAR_H + 4;
    y += 10;

    u32 av = (g_api && g_api->timer_get_ioapic_available) ? g_api->timer_get_ioapic_available() : 0;
    bb_puts(40, y, "IOAPIC:", COL_FG, COL_BG, font);
    bb_puts(40 + 8*CHAR_W, y, av ? "AVAILABLE" : "NOT AVAILABLE",
            av ? COL_CORE_ON : COL_ACCENT, COL_BG, font);
    y += CHAR_H;

    u32 lid = (g_api && g_api->timer_get_lapic_id) ? g_api->timer_get_lapic_id() : 0;
    bb_puts(40, y, "LAPIC ID:", COL_FG, COL_BG, font);
    bb_u64(40 + 10*CHAR_W, y, lid, COL_FG, COL_BG, font);
    y += CHAR_H;

    if (g_acpi && g_acpi->get_info) {
        acpi_system_info_t info;
        if (DSTATUS_IS_OK(g_acpi->get_info(&info))) {
            bb_puts(40, y, "IOAPIC count:", COL_FG, COL_BG, font);
            bb_u64(40 + 14*CHAR_W, y, info.ioapic_count, COL_FG, COL_BG, font);
            y += CHAR_H;
            for (u32 i = 0; i < info.ioapic_count && i < ACPI_MAX_IOAPICS; i++) {
                bb_puts(80, y, "ioapic[", COL_FG, COL_BG, font);
                bb_u64(80 + 7*CHAR_W, y, i, COL_FG, COL_BG, font);
                bb_puts(80 + 8*CHAR_W, y, "]", COL_FG, COL_BG, font);
                bb_puts(80 + 10*CHAR_W, y, "addr=", COL_FG, COL_BG, font);
                bb_hex(80 + 15*CHAR_W, y, (u32)info.ioapic_address[i], 8, COL_FG, COL_BG, font);
                bb_puts(80 + 26*CHAR_W, y, "gsi=", COL_FG, COL_BG, font);
                bb_u64(80 + 30*CHAR_W, y, info.ioapic_gsi_base[i], COL_FG, COL_BG, font);
                y += CHAR_H;
            }
        }
    }

    if (g_api && g_api->hpet_info) {
        const volatile hpet_system_info_t *hp = g_api->hpet_info;
        bb_puts(40, y, "HPET:", COL_FG, COL_BG, font);
        bb_puts(40 + 6*CHAR_W, y, hp->enabled ? "ENABLED" : "DISABLED",
                hp->enabled ? COL_CORE_ON : COL_ACCENT, COL_BG, font);
    }

    bb_puts(40, g_screen_h - CHAR_H*2,
            "[1] PIT  [2] HPET  [3] LAPIC  [Esc] Back", COL_FG, COL_BG, font);
}

static const char *pci_class_label(u8 c) {
    switch (c) {
        case 0x01: return "STORAGE";
        case 0x02: return "NETWORK";
        case 0x03: return "DISPLAY";
        case 0x04: return "MEDIA";
        case 0x06: return "BRIDGE";
        case 0x07: return "SERIAL";
        case 0x08: return "SYSTEM";
        case 0x09: return "INPUT";
        case 0x0C: return "BUS";
        default:   return "OTHER";
    }
}

static void draw_pci_screen(void) {
    const u8 *font = (const u8 *)font8x8;
    u32 count = 0, trunc = 0;
    bb_clear(COL_BG);
    bb_puts(40, 20, "=== PCI DEVICES ===", COL_ACCENT, COL_BG, font);

    if (!g_pci || !g_pci->get_count || !g_pci->get_device) {
        bb_puts(40, 64, "PCI driver/API is not available", COL_FG, COL_BG, font);
        bb_puts(40, g_screen_h - CHAR_H*2, "[H] Hardware Map  [Esc] Main", COL_FG, COL_BG, font);
        return;
    }
    if (DSTATUS_IS_ERR(g_pci->get_count(&count))) {
        bb_puts(40, 64, "PCI device query failed", COL_FG, COL_BG, font);
        return;
    }
    if (g_pci->get_scan_truncated &&
        DSTATUS_IS_ERR(g_pci->get_scan_truncated(&trunc))) trunc = 1;

    bb_puts(40, 44, "Found:", COL_FG, COL_BG, font);
    bb_u64(40 + 7*CHAR_W, 44, count, COL_FG, COL_BG, font);
    if (trunc)
        bb_puts(40 + 14*CHAR_W, 44, "SCAN LIMIT REACHED", COL_ACCENT, COL_BG, font);

    u32 pages = (count + PCI_PAGE_SIZE - 1) / PCI_PAGE_SIZE;
    if (pages == 0) pages = 1;
    if (g_pci_page >= pages) g_pci_page = pages - 1;

    bb_puts(40, 64, "BDF      Vendor:Device  Class", COL_ACCENT, COL_BG, font);

    u32 first = g_pci_page * PCI_PAGE_SIZE;
    u32 limit = first + PCI_PAGE_SIZE;
    if (limit > count) limit = count;
    for (u32 i = first; i < limit; i++) {
        const pci_device_t *d = 0;
        u64 y = 88 + (i - first) * 44;
        if (DSTATUS_IS_ERR(g_pci->get_device(i, &d)) || !d) continue;
        bb_puts(40, y, "B", COL_FG, COL_BG, font);
        bb_u64(40 + CHAR_W, y, d->bus, COL_FG, COL_BG, font);
        bb_puts(40 + 4*CHAR_W, y, "S", COL_FG, COL_BG, font);
        bb_u64(40 + 5*CHAR_W, y, d->slot, COL_FG, COL_BG, font);
        bb_puts(40 + 8*CHAR_W, y, "F", COL_FG, COL_BG, font);
        bb_u64(40 + 9*CHAR_W, y, d->func, COL_FG, COL_BG, font);
        bb_hex16(40 + 12*CHAR_W, y, d->vendor_id, COL_FG, COL_BG, font);
        bb_puts(40 + 16*CHAR_W, y, ":", COL_FG, COL_BG, font);
        bb_hex16(40 + 17*CHAR_W, y, d->device_id, COL_FG, COL_BG, font);
        bb_puts(40 + 22*CHAR_W, y, pci_class_label(d->class_code), COL_FG, COL_BG, font);
        bb_puts(56, y + 14, (d->command & 1) ? "IO:ON" : "IO:OFF", COL_FG, COL_BG, font);
        bb_puts(56 + 10*CHAR_W, y + 14, (d->command & 2) ? "MEM:ON" : "MEM:OFF", COL_FG, COL_BG, font);
        bb_puts(56 + 22*CHAR_W, y + 14, (d->command & 4) ? "MASTER:ON" : "MASTER:OFF", COL_FG, COL_BG, font);
    }
    bb_puts(40, g_screen_h - CHAR_H*2,
            "[N] Next  [B] Previous  [H] Hardware  [Esc] Main", COL_FG, COL_BG, font);
}

static const char *hpet_stat(dstatus_t s) {
    switch (s) {
        case DEPRESSURE_STATUS_OK:            return "ENABLED";
        case DEPRESSURE_HPET_NOT_FOUND:       return "NOT FOUND";
        case DEPRESSURE_HPET_NOT_SUPPORTED:   return "NOT SUPPORTED";
        case DEPRESSURE_HPET_INVALID_TABLE:   return "INVALID TABLE";
        case DEPRESSURE_HPET_INVALID_ADDRESS: return "INVALID ADDRESS";
        case DEPRESSURE_HPET_INVALID_PERIOD:  return "INVALID PERIOD";
        case DEPRESSURE_HPET_ENABLE_FAILED:   return "ENABLE FAILED";
        case DEPRESSURE_HPET_NOT_INITIALIZED: return "NOT INITIALIZED";
        case DEPRESSURE_NOT_FOUND:            return "ACPI N/A";
        case DEPRESSURE_CHECKSUM_ERROR:       return "ACPI CHECKSUM";
        case DEPRESSURE_CORRUPTED_DATA:       return "ACPI CORRUPT";
        default:                              return "INIT ERROR";
    }
}

static const char *fadt_stat(dstatus_t s) {
    switch (s) {
        case DEPRESSURE_STATUS_OK:                return "READY";
        case DEPRESSURE_ACPI_FADT_NOT_FOUND:      return "NOT FOUND";
        case DEPRESSURE_ACPI_FADT_INVALID:        return "INVALID";
        case DEPRESSURE_ACPI_S5_NOT_FOUND:        return "S5 NOT FOUND";
        case DEPRESSURE_ACPI_POWER_UNAVAILABLE:   return "PWR UNAVAIL";
        case DEPRESSURE_ACPI_RESET_UNAVAILABLE:   return "RST UNAVAIL";
        default:                                  return "ERROR";
    }
}

static void draw_mgmt_screen(void) {
    const u8 *font = (const u8 *)font8x8;
    const volatile hpet_system_info_t *hp = g_api ? g_api->hpet_info : 0;
    acpi_system_info_t ai;
    int have_ai = 0;
    u64 y = 84;

    bb_clear(COL_BG);
    bb_puts(40, 20, "=== MANAGEMENT ===", COL_ACCENT, COL_BG, font);
    bb_puts(40, 56, "HPET STATUS", COL_ACCENT, COL_BG, font);

    if (g_acpi && g_acpi->version == ACPI_API_VERSION &&
        g_acpi->get_info && DSTATUS_IS_OK(g_acpi->get_info(&ai))) have_ai = 1;

    if (!hp) {
        bb_puts(56, y, "HPET state unavailable", COL_ACCENT, COL_BG, font);
    } else {
        bb_puts(56, y, "State:", COL_FG, COL_BG, font);
        bb_puts(56 + 8*CHAR_W, y, hpet_stat(hp->status),
                hp->enabled ? COL_CORE_ON : COL_ACCENT, COL_BG, font);
        bb_puts(360, y, "MMIO:", COL_FG, COL_BG, font);
        bb_hex(360 + 7*CHAR_W, y, hp->mmio_base, 16, COL_FG, COL_BG, font);
        y += 20;
        bb_puts(56, y, "Frequency:", COL_FG, COL_BG, font);
        bb_u64(56 + 12*CHAR_W, y, hp->frequency_hz, COL_FG, COL_BG, font);
        bb_puts(56 + 23*CHAR_W, y, "Hz", COL_FG, COL_BG, font);
        bb_puts(360, y, "Counter:", COL_FG, COL_BG, font);
        bb_hex(360 + 10*CHAR_W, y, hp->counter_value, 16, COL_FG, COL_BG, font);
        y += 20;
        bb_puts(56, y, "Timers:", COL_FG, COL_BG, font);
        bb_u64(56 + 9*CHAR_W, y, hp->timer_count, COL_FG, COL_BG, font);
        bb_puts(240, y, "Vendor:", COL_FG, COL_BG, font);
        bb_hex16(240 + 8*CHAR_W, y, (u16)hp->vendor_id, COL_FG, COL_BG, font);
        bb_puts(360, y, "Period fs:", COL_FG, COL_BG, font);
        bb_u64(360 + 12*CHAR_W, y, hp->period_femtoseconds, COL_FG, COL_BG, font);
    }

    y += 24;
    bb_puts(40, y, "FACP / LEGACY HARDWARE", COL_ACCENT, COL_BG, font);
    y += 22;
    if (!have_ai) {
        bb_puts(56, y, "ACPI driver data unavailable", COL_ACCENT, COL_BG, font);
    } else {
        bb_puts(56, y, "FADT:", COL_FG, COL_BG, font);
        bb_puts(56 + 7*CHAR_W, y, fadt_stat(ai.fadt_status),
                DSTATUS_IS_OK(ai.fadt_status) ? COL_CORE_ON : COL_ACCENT, COL_BG, font);
        bb_puts(360, y, "SCI:", COL_FG, COL_BG, font);
        bb_u64(360 + 5*CHAR_W, y, ai.sci_interrupt, COL_FG, COL_BG, font);
        bb_puts(480, y, "SCI_EN:", COL_FG, COL_BG, font);
        bb_puts(480 + 9*CHAR_W, y, ai.acpi_enabled ? "ON" : "OFF",
                ai.acpi_enabled ? COL_CORE_ON : COL_ACCENT, COL_BG, font);
        y += 20;
        bb_puts(56, y, "PM1 CNT A:", COL_FG, COL_BG, font);
        bb_hex16(56 + 12*CHAR_W, y, (u16)ai.pm1a_control_port, COL_FG, COL_BG, font);
        bb_puts(280, y, "B:", COL_FG, COL_BG, font);
        bb_hex16(280 + 3*CHAR_W, y, (u16)ai.pm1b_control_port, COL_FG, COL_BG, font);
        bb_puts(440, y, "len:", COL_FG, COL_BG, font);
        bb_u64(440 + 5*CHAR_W, y, ai.pm1_control_length, COL_FG, COL_BG, font);
        y += 20;
        bb_puts(56, y, "CNT VAL A:", COL_FG, COL_BG, font);
        bb_hex16(56 + 12*CHAR_W, y, ai.pm1a_control_value, COL_FG, COL_BG, font);
        bb_puts(280, y, "B:", COL_FG, COL_BG, font);
        bb_hex16(280 + 3*CHAR_W, y, ai.pm1b_control_value, COL_FG, COL_BG, font);
        bb_puts(440, y, "SLP_EN:", COL_FG, COL_BG, font);
        bb_puts(440 + 9*CHAR_W, y, (ai.pm1a_control_value & 0x2000) ? "ON" : "OFF", COL_FG, COL_BG, font);
        y += 20;
        bb_puts(56, y, "PM1 EVT A:", COL_FG, COL_BG, font);
        bb_hex16(56 + 12*CHAR_W, y, (u16)ai.pm1a_event_port, COL_FG, COL_BG, font);
        bb_puts(280, y, "B:", COL_FG, COL_BG, font);
        bb_hex16(280 + 3*CHAR_W, y, (u16)ai.pm1b_event_port, COL_FG, COL_BG, font);
        bb_puts(440, y, "len:", COL_FG, COL_BG, font);
        bb_u64(440 + 5*CHAR_W, y, ai.pm1_event_length, COL_FG, COL_BG, font);
        y += 20;
        bb_puts(56, y, "EVT STAT A:", COL_FG, COL_BG, font);
        bb_hex16(56 + 12*CHAR_W, y, ai.pm1a_event_status, COL_FG, COL_BG, font);
        bb_puts(280, y, "B:", COL_FG, COL_BG, font);
        bb_hex16(280 + 3*CHAR_W, y, ai.pm1b_event_status, COL_FG, COL_BG, font);
        bb_puts(440, y, "EVT EN A:", COL_FG, COL_BG, font);
        bb_hex16(440 + 11*CHAR_W, y, ai.pm1a_event_enable, COL_FG, COL_BG, font);
        y += 20;
        bb_puts(56, y, "EVT EN B:", COL_FG, COL_BG, font);
        bb_hex16(56 + 11*CHAR_W, y, ai.pm1b_event_enable, COL_FG, COL_BG, font);
        y += 20;
        bb_puts(56, y, "PM2:", COL_FG, COL_BG, font);
        bb_hex16(56 + 6*CHAR_W, y, (u16)ai.pm2_control_port, COL_FG, COL_BG, font);
        bb_puts(200, y, "PM TMR:", COL_FG, COL_BG, font);
        bb_hex16(200 + 9*CHAR_W, y, (u16)ai.pm_timer_port, COL_FG, COL_BG, font);
        bb_puts(440, y, "GPE0:", COL_FG, COL_BG, font);
        bb_hex16(440 + 7*CHAR_W, y, (u16)ai.gpe0_port, COL_FG, COL_BG, font);
        y += 20;
        bb_puts(56, y, "PM2 VALUE:", COL_FG, COL_BG, font);
        bb_hex16(56 + 12*CHAR_W, y, ai.pm2_control_value, COL_FG, COL_BG, font);
        bb_puts(240, y, "PM TMR NOW:", COL_FG, COL_BG, font);
        bb_hex(240 + 13*CHAR_W, y, ai.fadt_pm_timer_value, 8, COL_FG, COL_BG, font);
        y += 20;
        bb_puts(56, y, "S5:", COL_FG, COL_BG, font);
        bb_puts(56 + 5*CHAR_W, y, ai.s5_available ? "AVAILABLE" : "UNAVAILABLE",
                ai.s5_available ? COL_CORE_ON : COL_ACCENT, COL_BG, font);
        bb_puts(240, y, "Sleep types A/B:", COL_FG, COL_BG, font);
        bb_u64(240 + 17*CHAR_W, y, ai.s5_type_a, COL_FG, COL_BG, font);
        bb_puts(240 + 20*CHAR_W, y, "/", COL_FG, COL_BG, font);
        bb_u64(240 + 22*CHAR_W, y, ai.s5_type_b, COL_FG, COL_BG, font);
    }

    y += 24;
    bb_puts(40, y, "UEFI INFO", COL_ACCENT, COL_BG, font);
    y += 22;

    bb_puts(56, y, "Firmware:", COL_FG, COL_BG, font);
    bb_puts(56 + 10*CHAR_W, y, "UEFI 2.x", COL_FG, COL_BG, font);
    y += 18;

    bb_puts(56, y, "Boot mode:", COL_FG, COL_BG, font);
    bb_puts(56 + 11*CHAR_W, y, "GOP (Graphics Output Protocol)", COL_FG, COL_BG, font);
    y += 18;

    if (have_ai && ai.bgrt_available) {
        bb_puts(56, y, "BGRT:", COL_FG, COL_BG, font);
        bb_puts(56 + 6*CHAR_W, y, ai.bgrt_status ? "AVAILABLE" : "NOT SHOWN",
                ai.bgrt_status ? COL_CORE_ON : COL_ACCENT, COL_BG, font);
        y += 18;

        bb_puts(56, y, "Image:", COL_FG, COL_BG, font);
        bb_puts(56 + 7*CHAR_W, y, ai.bgrt_image_type == 0 ? "BMP" : "PNG",
                COL_FG, COL_BG, font);
        bb_puts(56 + 12*CHAR_W, y, "at", COL_FG, COL_BG, font);
        bb_hex(56 + 15*CHAR_W, y, ai.bgrt_image_address, 16, COL_FG, COL_BG, font);
        y += 18;

        bb_puts(56, y, "Size:", COL_FG, COL_BG, font);
        bb_u64(56 + 6*CHAR_W, y, ai.bgrt_image_width, COL_FG, COL_BG, font);
        bb_puts(56 + 12*CHAR_W, y, "x", COL_FG, COL_BG, font);
        bb_u64(56 + 13*CHAR_W, y, ai.bgrt_image_height, COL_FG, COL_BG, font);
        y += 18;
    } else {
        bb_puts(56, y, "BGRT: NOT AVAILABLE", COL_ACCENT, COL_BG, font);
        y += 18;
    }

    g_shutdown_x = 80; g_reboot_x = 420;
    g_shutdown_y = g_screen_h - 100;
    g_reboot_y = g_shutdown_y;
    bb_button(g_shutdown_x, g_shutdown_y, 260, 40, "Shutdown", COL_ACCENT, COL_BTN, font);
    bb_button(g_reboot_x, g_reboot_y, 260, 40, "Reboot", COL_ACCENT, COL_BTN, font);

    if (g_power_confirmation != POWER_ACTION_NONE) {
        const char *prompt = (g_power_confirmation == POWER_ACTION_SHUTDOWN) ?
            "Confirm shutdown? Y/N" : "Confirm reboot? Y/N";
        u64 my = g_shutdown_y - 82;
        bb_fill_rect(120, my, 560, 72, COL_DARK);
        bb_puts(144, my + 6, prompt, COL_ACCENT, COL_DARK, font);
        g_confirm_x = 144; g_confirm_y = my + 30;
        g_cancel_x = 424; g_cancel_y = g_confirm_y;
        bb_button(g_confirm_x, g_confirm_y, 200, 40, "Confirm", COL_ACCENT, COL_BTN, font);
        bb_button(g_cancel_x, g_cancel_y, 200, 40, "Cancel", COL_ACCENT, COL_BTN, font);
    } else if (DSTATUS_IS_ERR(g_power_action_status)) {
        bb_puts(40, g_shutdown_y - 24,
                g_power_action_status == DEPRESSURE_TIMEOUT ?
                    "Power command issued" :
                g_power_action_status == DEPRESSURE_ACPI_RESET_UNAVAILABLE ?
                    "ACPI reset unavailable" :
                    "ACPI power unavailable",
                COL_ACCENT, COL_BG, font);
    }

    bb_puts(40, g_screen_h - CHAR_H*2,
            "[S] Shutdown  [R] Reboot  [B] UEFI Info  [Esc] Main Menu",
            COL_FG, COL_BG, font);
}

static void draw_uefi_screen(void) {
    const u8 *font = (const u8 *)font8x8;
    acpi_system_info_t ai;
    int have_ai = 0;

    bb_clear(COL_BG);
    bb_puts(40, 16, "=== UEFI / BIOS INFO (1/2) ===", COL_ACCENT, COL_BG, font);

    if (g_acpi && g_acpi->version == ACPI_API_VERSION &&
        g_acpi->get_info && DSTATUS_IS_OK(g_acpi->get_info(&ai))) have_ai = 1;

    if (!g_uefi.valid) {
        bb_puts(40, 60, "UEFI System Table not available", COL_ACCENT, COL_BG, font);
        bb_puts(40, g_screen_h - CHAR_H*2, "[Esc] Back to Management", COL_FG, COL_BG, font);
        return;
    }

    u64 lx = 40;
    u64 y = 44;
    bb_puts(lx, y, "SYSTEM TABLE", COL_ACCENT, COL_BG, font); y += 18;
    bb_puts(lx, y, "Addr:", COL_FG, COL_BG, font);
    bb_hex(lx + 6*CHAR_W, y, g_uefi.system_table, 10, COL_FG, COL_BG, font); y += 14;
    bb_puts(lx, y, "Sign:", COL_FG, COL_BG, font);
    {
        u64 sig = *(volatile u64 *)(g_uefi.system_table + 0x00);
        char s[9];
        for (int i = 0; i < 8; i++) s[i] = (char)((sig >> (i * 8)) & 0xFF);
        s[8] = 0;
        bb_puts(lx + 6*CHAR_W, y, s, COL_FG, COL_BG, font);
    } y += 14;
    bb_puts(lx, y, "Rev:", COL_FG, COL_BG, font);
    bb_hex(lx + 5*CHAR_W, y, g_uefi.st_hdr_rev, 8, COL_FG, COL_BG, font);
    bb_puts(lx + 15*CHAR_W, y, "HdrSz:", COL_FG, COL_BG, font);
    bb_u64(lx + 21*CHAR_W, y, g_uefi.st_hdr_size, COL_FG, COL_BG, font); y += 14;
    bb_puts(lx, y, "FW Rev:", COL_FG, COL_BG, font);
    bb_hex(lx + 8*CHAR_W, y, g_uefi.st_fw_rev, 8, COL_FG, COL_BG, font); y += 14;
    bb_puts(lx, y, "FW Vend:", COL_FG, COL_BG, font);
    bb_hex(lx + 9*CHAR_W, y, g_uefi.st_fw_vendor, 8, COL_FG, COL_BG, font); y += 18;

    bb_puts(lx, y, "ACPI", COL_ACCENT, COL_BG, font); y += 16;
    if (have_ai) {
        bb_puts(lx, y, "RSDP:", COL_FG, COL_BG, font);
        bb_hex(lx + 6*CHAR_W, y, ai.rsdp_address, 10, COL_FG, COL_BG, font); y += 14;
        bb_puts(lx, y, "Root:", COL_FG, COL_BG, font);
        bb_hex(lx + 6*CHAR_W, y, ai.root_address, 10, COL_FG, COL_BG, font);
        bb_puts(lx + 18*CHAR_W, y, ai.root_is_xsdt ? "XSDT" : "RSDT",
                COL_CORE_ON, COL_BG, font); y += 14;
        bb_puts(lx, y, "Tables:", COL_FG, COL_BG, font);
        bb_u64(lx + 8*CHAR_W, y, ai.table_count, COL_FG, COL_BG, font); y += 14;
        bb_puts(lx, y, "MADT:", COL_FG, COL_BG, font);
        bb_hex(lx + 6*CHAR_W, y, ai.madt_address, 10, COL_FG, COL_BG, font); y += 14;
        bb_puts(lx, y, "FADT:", COL_FG, COL_BG, font);
        bb_hex(lx + 6*CHAR_W, y, ai.fadt_address, 10, COL_FG, COL_BG, font); y += 14;
    } else {
        bb_puts(lx, y, "ACPI info unavailable", COL_ACCENT, COL_BG, font); y += 14;
    }

    u64 rx = 440;
    u64 ry = 44;
    bb_puts(rx, ry, "SERVICES", COL_ACCENT, COL_BG, font); ry += 18;
    bb_puts(rx, ry, "BootSvc:", COL_FG, COL_BG, font);
    bb_hex(rx + 9*CHAR_W, ry, g_uefi.boot_services, 10, COL_FG, COL_BG, font); ry += 14;
    bb_puts(rx, ry, "RunSvc:", COL_FG, COL_BG, font);
    bb_hex(rx + 8*CHAR_W, ry, g_uefi.runtime_services, 10, COL_FG, COL_BG, font); ry += 14;
    bb_puts(rx, ry, "ConOut:", COL_FG, COL_BG, font);
    bb_hex(rx + 8*CHAR_W, ry, g_uefi.con_out, 10, COL_FG, COL_BG, font); ry += 14;
    bb_puts(rx, ry, "ImgHnd:", COL_FG, COL_BG, font);
    bb_hex(rx + 8*CHAR_W, ry, g_uefi.image_handle, 10, COL_FG, COL_BG, font); ry += 14;
    bb_puts(rx, ry, "CfgTab:", COL_FG, COL_BG, font);
    bb_hex(rx + 8*CHAR_W, ry, g_uefi.config_tables, 10, COL_FG, COL_BG, font); ry += 14;
    bb_puts(rx, ry, "CfgCnt:", COL_FG, COL_BG, font);
    bb_u64(rx + 8*CHAR_W, ry, g_uefi.cfg_count, COL_FG, COL_BG, font); ry += 14;
    bb_puts(rx, ry, "BS Rev:", COL_FG, COL_BG, font);
    bb_hex(rx + 8*CHAR_W, ry, g_uefi.bs_hdr_rev, 8, COL_FG, COL_BG, font);
    bb_puts(rx + 16*CHAR_W, ry, "Sz:", COL_FG, COL_BG, font);
    bb_u64(rx + 19*CHAR_W, ry, g_uefi.bs_hdr_size, COL_FG, COL_BG, font); ry += 14;
    bb_puts(rx, ry, "RT Rev:", COL_FG, COL_BG, font);
    bb_hex(rx + 8*CHAR_W, ry, g_uefi.rt_hdr_rev, 8, COL_FG, COL_BG, font);
    bb_puts(rx + 16*CHAR_W, ry, "Sz:", COL_FG, COL_BG, font);
    bb_u64(rx + 19*CHAR_W, ry, g_uefi.rt_hdr_size, COL_FG, COL_BG, font); ry += 18;

    bb_puts(rx, ry, "BGRT", COL_ACCENT, COL_BG, font); ry += 16;
    if (have_ai && ai.bgrt_available) {
        bb_puts(rx, ry, "St:", COL_FG, COL_BG, font);
        bb_puts(rx + 4*CHAR_W, ry, ai.bgrt_status ? "OK" : "NO",
                ai.bgrt_status ? COL_CORE_ON : COL_ACCENT, COL_BG, font);
        bb_puts(rx + 8*CHAR_W, ry, "Type:", COL_FG, COL_BG, font);
        bb_puts(rx + 14*CHAR_W, ry, ai.bgrt_image_type == 0 ? "BMP" : "PNG",
                COL_FG, COL_BG, font); ry += 14;
        bb_puts(rx, ry, "Size:", COL_FG, COL_BG, font);
        bb_u64(rx + 6*CHAR_W, ry, ai.bgrt_image_width, COL_FG, COL_BG, font);
        bb_puts(rx + 11*CHAR_W, ry, "x", COL_FG, COL_BG, font);
        bb_u64(rx + 12*CHAR_W, ry, ai.bgrt_image_height, COL_FG, COL_BG, font); ry += 14;
        bb_puts(rx, ry, "Addr:", COL_FG, COL_BG, font);
        bb_hex(rx + 6*CHAR_W, ry, ai.bgrt_image_address, 10, COL_FG, COL_BG, font); ry += 14;
    } else {
        bb_puts(rx, ry, "BGRT unavailable", COL_ACCENT, COL_BG, font); ry += 14;
    }

    u64 cy = 340;
    bb_puts(40, cy, "CONFIG TABLES (first 6)", COL_ACCENT, COL_BG, font); cy += 18;
    bb_puts(56, cy, "#", COL_ACCENT, COL_BG, font);
    bb_puts(56 + 4*CHAR_W, cy, "GUID (low 64)", COL_ACCENT, COL_BG, font);
    bb_puts(56 + 22*CHAR_W, cy, "Address", COL_ACCENT, COL_BG, font);
    cy += 14;

    u32 show = g_uefi.cfg_count > 6 ? 6 : g_uefi.cfg_count;
    for (u32 i = 0; i < show; i++) {
        u64 entry = g_uefi.config_tables + i * 24;
        u64 addr = *(volatile u64 *)(entry + 16);
        u32 guid0 = *(volatile u32 *)(entry);
        u32 guid1 = *(volatile u32 *)(entry + 4);
        u64 guid_lo = ((u64)guid1 << 32) | guid0;

        bb_puts(56, cy, "[", COL_FG, COL_BG, font);
        bb_u64(56 + CHAR_W, cy, i, COL_FG, COL_BG, font);
        bb_puts(56 + 3*CHAR_W, cy, "]", COL_FG, COL_BG, font);
        bb_hex(56 + 5*CHAR_W, cy, guid_lo, 16, COL_FG, COL_BG, font);
        bb_hex(56 + 23*CHAR_W, cy, addr, 10, COL_FG, COL_BG, font);
        cy += 14;
    }

    if (g_uefi.cfg_count > show) {
        bb_puts(56, cy, "... ", COL_FG, COL_BG, font);
        bb_u64(56 + 4*CHAR_W, cy, g_uefi.cfg_count - show, COL_FG, COL_BG, font);
        bb_puts(56 + 6*CHAR_W, cy, " more on page 2", COL_ACCENT, COL_BG, font);
    }

    bb_puts(40, g_screen_h - CHAR_H*2,
            "[N] Next page (rest + logo)   [Esc] Back to Management",
            COL_FG, COL_BG, font);
}

static void draw_uefi_screen2(void) {
    const u8 *font = (const u8 *)font8x8;
    acpi_system_info_t ai;
    int have_ai = 0;

    bb_clear(COL_BG);
    bb_puts(40, 16, "=== UEFI / BIOS INFO (2/2) ===", COL_ACCENT, COL_BG, font);

    if (g_acpi && g_acpi->version == ACPI_API_VERSION &&
        g_acpi->get_info && DSTATUS_IS_OK(g_acpi->get_info(&ai))) have_ai = 1;

    u64 cx = 40;
    u64 cy = 44;
    bb_puts(cx, cy, "CONFIG TABLES (rest)", COL_ACCENT, COL_BG, font); cy += 18;
    bb_puts(cx, cy, "#", COL_ACCENT, COL_BG, font);
    bb_puts(cx + 4*CHAR_W, cy, "GUID (low 64)", COL_ACCENT, COL_BG, font);
    bb_puts(cx + 22*CHAR_W, cy, "Address", COL_ACCENT, COL_BG, font);
    cy += 14;

    u32 start = 6;
    u32 shown = 0;
    for (u32 i = start; i < g_uefi.cfg_count && shown < 20; i++) {
        u64 entry = g_uefi.config_tables + i * 24;
        u64 addr = *(volatile u64 *)(entry + 16);
        u32 guid0 = *(volatile u32 *)(entry);
        u32 guid1 = *(volatile u32 *)(entry + 4);
        u64 guid_lo = ((u64)guid1 << 32) | guid0;

        bb_puts(cx, cy, "[", COL_FG, COL_BG, font);
        bb_u64(cx + CHAR_W, cy, i, COL_FG, COL_BG, font);
        if (i < 10) bb_puts(cx + 2*CHAR_W, cy, "]", COL_FG, COL_BG, font);
        else        bb_puts(cx + 3*CHAR_W, cy, "]", COL_FG, COL_BG, font);
        bb_hex(cx + 5*CHAR_W, cy, guid_lo, 16, COL_FG, COL_BG, font);
        bb_hex(cx + 23*CHAR_W, cy, addr, 10, COL_FG, COL_BG, font);
        cy += 14;
        shown++;
    }

    if (g_uefi.cfg_count <= start) {
        bb_puts(cx, cy, "No additional entries", COL_FG, COL_BG, font);
    }

    u64 lx = 440;
    u64 ly = 44;
    bb_puts(lx, ly, "BGRT LOGO", COL_ACCENT, COL_BG, font); ly += 20;

    if (!have_ai || !ai.bgrt_available || !ai.bgrt_image_address ||
        !ai.bgrt_image_width || !ai.bgrt_image_height) {
        bb_puts(lx, ly, "BGRT not available", COL_ACCENT, COL_BG, font);
        bb_puts(40, g_screen_h - CHAR_H*2,
                "[B] Previous page   [Esc] Back to Management",
                COL_FG, COL_BG, font);
        return;
    }

    bb_puts(lx, ly, "Type:", COL_FG, COL_BG, font);
    bb_puts(lx + 6*CHAR_W, ly, ai.bgrt_image_type == 0 ? "BMP" : "PNG",
            COL_FG, COL_BG, font); ly += 14;
    bb_puts(lx, ly, "Size:", COL_FG, COL_BG, font);
    bb_u64(lx + 6*CHAR_W, ly, ai.bgrt_image_width, COL_FG, COL_BG, font);
    bb_puts(lx + 11*CHAR_W, ly, "x", COL_FG, COL_BG, font);
    bb_u64(lx + 12*CHAR_W, ly, ai.bgrt_image_height, COL_FG, COL_BG, font); ly += 14;

    const u8 *img = (const u8 *)ai.bgrt_image_address;
    u64 logo_y = ly + 6;

    if (ai.bgrt_image_type == 0) {
        dbmp_info_t bmp;
        if (dbmp_check(img, 54) && dbmp_parse(img, 16 * 1024 * 1024, &bmp) == 0) {
            u64 bw = bmp.width, bh = bmp.height;
            u64 max_w = 260, max_h = 200;
            if (bw > max_w) { bh = bh * max_w / bw; bw = max_w; }
            if (bh > max_h) { bw = bw * max_h / bh; bh = max_h; }
            if (bw == 0) bw = 1;
            if (bh == 0) bh = 1;

            for (u64 yy = 0; yy < bh; yy++) {
                for (u64 xx = 0; xx < bw; xx++) {
                    u64 sx = xx * bmp.width / bw;
                    u64 sy = yy * bmp.height / bh;
                    bb_fill_rect(lx + xx, logo_y + yy, 1, 1, dbmp_get_pixel(&bmp, sx, sy));
                }
            }

            bb_fill_rect(lx - 2, logo_y - 2, bw + 4, 2, COL_ACCENT);
            bb_fill_rect(lx - 2, logo_y + bh, bw + 4, 2, COL_ACCENT);
            bb_fill_rect(lx - 2, logo_y - 2, 2, bh + 4, COL_ACCENT);
            bb_fill_rect(lx + bw, logo_y - 2, 2, bh + 4, COL_ACCENT);
            bb_puts(lx, logo_y + bh + 8, "Rendered BMP", COL_CORE_ON, COL_BG, font);
        } else {
            bb_puts(lx, logo_y, "Failed to parse BMP", COL_ACCENT, COL_BG, font);
        }
    } else if (ai.bgrt_image_type == 1) {
        dpng_info_t png;
        if (dpng_parse(img, 16 * 1024 * 1024, &png) == 0) {
            u64 bw = png.width, bh = png.height;
            u64 max_w = 260, max_h = 200;
            if (bw > max_w) { bh = bh * max_w / bw; bw = max_w; }
            if (bh > max_h) { bw = bw * max_h / bh; bh = max_h; }
            if (bw == 0) bw = 1;
            if (bh == 0) bh = 1;

            for (u64 yy = 0; yy < bh; yy++) {
                for (u64 xx = 0; xx < bw; xx++) {
                    u64 sx = xx * png.width / bw;
                    u64 sy = yy * png.height / bh;
                    bb_fill_rect(lx + xx, logo_y + yy, 1, 1, dpng_get_pixel(&png, sx, sy));
                }
            }

            bb_fill_rect(lx - 2, logo_y - 2, bw + 4, 2, COL_ACCENT);
            bb_fill_rect(lx - 2, logo_y + bh, bw + 4, 2, COL_ACCENT);
            bb_fill_rect(lx - 2, logo_y - 2, 2, bh + 4, COL_ACCENT);
            bb_fill_rect(lx + bw, logo_y - 2, 2, bh + 4, COL_ACCENT);
            bb_puts(lx, logo_y + bh + 8, "Rendered PNG", COL_CORE_ON, COL_BG, font);
        } else {
            bb_puts(lx, logo_y, "Failed to parse PNG", COL_ACCENT, COL_BG, font);
        }
    }

    bb_puts(40, g_screen_h - CHAR_H*2,
            "[B] Previous page   [Esc] Back to Management",
            COL_FG, COL_BG, font);
}

static void draw_gear(u64 cx, u64 cy, u64 sz) {
    u64 ro = sz/2, ri = ro*6/10, rh = ro*3/10;
    static const i8 dx[8] = {0,1,1,1,0,-1,-1,-1};
    static const i8 dy[8] = {-1,-1,0,1,1,1,0,-1};
    for (int i = 0; i < 8; i++) {
        i64 tx = (i64)cx + dx[i] * (i64)(ri + (ro - ri)/2);
        i64 ty = (i64)cy + dy[i] * (i64)(ri + (ro - ri)/2);
        u64 tw = sz/5, th = sz/5;
        bb_fill_rect((u64)(tx - (i64)tw/2), (u64)(ty - (i64)th/2), tw, th, COL_ACCENT);
    }
    for (i64 d = -(i64)ro; d <= (i64)ro; d++) {
        for (i64 e = -(i64)ro; e <= (i64)ro; e++) {
            i64 d2 = d*d + e*e;
            if (d2 <= (i64)(ro*ro) && d2 >= (i64)(ri*ri))
                bb_pixel((u64)((i64)cx+e), (u64)((i64)cy+d), COL_ACCENT);
        }
    }
    for (i64 d = -(i64)rh; d <= (i64)rh; d++)
        for (i64 e = -(i64)rh; e <= (i64)rh; e++)
            if (d*d + e*e <= (i64)(rh*rh))
                bb_pixel((u64)((i64)cx+e), (u64)((i64)cy+d), COL_BG);
}

static void draw_clock(u64 cx, u64 cy, u64 sz) {
    u64 ro = sz/2, ri = ro-4, rh = 3;
    for (i64 d = -(i64)ro; d <= (i64)ro; d++)
        for (i64 e = -(i64)ro; e <= (i64)ro; e++) {
            i64 d2 = d*d + e*e;
            if (d2 <= (i64)(ro*ro) && d2 >= (i64)(ri*ri))
                bb_pixel((u64)((i64)cx+e), (u64)((i64)cy+d), COL_ACCENT);
        }
    for (i64 t = 0; t < (i64)ri-6; t++) {
        bb_pixel((u64)cx, (u64)((i64)cy - t), COL_ACCENT);
        bb_pixel((u64)((i64)cx + t), (u64)cy, COL_ACCENT);
    }
    for (i64 d = -(i64)rh; d <= (i64)rh; d++)
        for (i64 e = -(i64)rh; e <= (i64)rh; e++)
            if (d*d + e*e <= (i64)(rh*rh))
                bb_pixel((u64)((i64)cx+e), (u64)((i64)cy+d), COL_FG);
}

static void draw_main_screen(void) {
    bb_clear(COL_BG);
    const u8 *font = (const u8 *)font8x8;

    const char *title = "Depressure Manager";
    u64 tl = 0; while (title[tl]) tl++;
    bb_puts((g_screen_w - tl*CHAR_W)/2, 40, title, COL_ACCENT, COL_BG, font);

    u64 isz = 140, sp = 60;
    u64 tw = isz*2 + sp;
    u64 sx = (g_screen_w - tw)/2;
    u64 iy = (g_screen_h - isz)/2 - 60;

    g_icon_w = isz; g_icon_h = isz;
    g_icon_x = sx;  g_icon_y = iy;
    draw_gear(g_icon_x + isz/2, g_icon_y + isz/2, isz);

    g_timer_icon_w = isz; g_timer_icon_h = isz;
    g_timer_icon_x = sx + isz + sp; g_timer_icon_y = iy;
    draw_clock(g_timer_icon_x + isz/2, g_timer_icon_y + isz/2, isz);

    const char *l1 = "Hardware"; u64 l1l = 0; while (l1[l1l]) l1l++;
    bb_puts(g_icon_x + (isz - l1l*CHAR_W)/2, g_icon_y + isz + 10, l1, COL_FG, COL_BG, font);
    const char *l2 = "Timer"; u64 l2l = 0; while (l2[l2l]) l2l++;
    bb_puts(g_timer_icon_x + (isz - l2l*CHAR_W)/2, g_timer_icon_y + isz + 10, l2, COL_FG, COL_BG, font);

    g_management_w = 240; g_management_h = 44;
    g_management_x = (g_screen_w - g_management_w)/2;
    g_management_y = g_icon_y + isz + 50;
    bb_button(g_management_x, g_management_y, g_management_w, g_management_h,
              "Management", COL_ACCENT, COL_BTN, font);

    const char *ft = "Gear: Hardware | Clock: Timer | M: Management";
    u64 fl = 0; while (ft[fl]) fl++;
    bb_puts((g_screen_w - fl*CHAR_W)/2, g_screen_h - CHAR_H*2, ft, COL_FG, COL_BG, font);
}

static void draw_core_box(u64 x, u64 y, u64 w, u64 h, u32 idx, u32 load) {
    const u8 *font = (const u8 *)font8x8;
    bb_fill_rect(x, y, w, h, COL_CORE_BG);
    bb_fill_rect(x, y, w, 2, COL_ACCENT);
    bb_fill_rect(x, y + h - 2, w, 2, COL_ACCENT);
    bb_fill_rect(x, y, 2, h, COL_ACCENT);
    bb_fill_rect(x + w - 2, y, 2, h, COL_ACCENT);

    char lbl[8] = {'C','O','R','E',' ',  '0' + ((idx/10)%10), '0' + (idx%10), 0};
    bb_puts(x + 8, y + 6, lbl, COL_FG, COL_CORE_BG, font);
    bb_puts(x + 8, y + 26, "ONLINE", COL_CORE_ON, COL_CORE_BG, font);
    char ld[16] = {'L',':'};
    int li = 2; u32 v = load;
    if (v == 0) ld[li++] = '0';
    else { char t[8]; int ti = 0;
        while (v > 0) { t[ti++] = '0' + (v%10); v /= 10; }
        for (int k = ti-1; k >= 0; k--) ld[li++] = t[k]; }
    ld[li++] = '%'; ld[li] = 0;
    bb_puts(x + 8, y + 44, ld, COL_FG, COL_CORE_BG, font);

    u64 bw = w - 16;
    u64 fill = (bw * (load > 100 ? 100 : load))/100;
    bb_fill_rect(x + 8, y + h - 10, bw, 4, COL_CORE_OFF);
    bb_fill_rect(x + 8, y + h - 10, fill, 4, COL_CORE_ON);
}

static void draw_hwmap_screen(void) {
    bb_clear(COL_BG);
    const u8 *font = (const u8 *)font8x8;
    bb_puts(40, 16, "=== HARDWARE MAP ===", COL_ACCENT, COL_BG, font);

    char line[80];
    int ci = 0;
    const char *cp = "CPU: ";
    while (*cp) line[ci++] = *cp++;
    cp = g_cpu_name;
    while (*cp && ci < 60) line[ci++] = *cp++;
    line[ci] = 0;
    bb_puts(40, 36, line, COL_ACCENT, COL_BG, font);

    ci = 0;
    const char *vp = "VENDOR: ";
    while (*vp) line[ci++] = *vp++;
    cp = g_cpu_vendor;
    while (*cp) line[ci++] = *cp++;
    line[ci] = 0;
    bb_puts(40, 52, line, COL_FG, COL_BG, font);

    bb_puts(560, 52, "CORES:", COL_FG, COL_BG, font);
    bb_u64(560 + 7*CHAR_W, 52, g_ncpu, COL_FG, COL_BG, font);

    u64 y = 72;

    bb_puts(40, y, "FPU/MMX/SSE:", COL_ACCENT, COL_BG, font);
    u64 bx = 40 + 13*CHAR_W;
    badge(bx, y + 3, "FPU",  g_cpu.fpu,  font);  bx += 4*8 + 12;
    badge(bx, y + 3, "MMX",  g_cpu.mmx,  font);  bx += 4*8 + 12;
    badge(bx, y + 3, "FXSR", g_cpu.fxsr, font);  bx += 5*8 + 12;
    badge(bx, y + 3, "SSE",  g_cpu.sse,  font);  bx += 4*8 + 12;
    badge(bx, y + 3, "SSE2", g_cpu.sse2, font);  bx += 5*8 + 12;
    badge(bx, y + 3, "SSE3", g_cpu.sse3, font);  bx += 5*8 + 12;
    badge(bx, y + 3, "SSSE3", g_cpu.ssse3, font);
    y += 16;

    bx = 40 + 13*CHAR_W;
    badge(bx, y + 3, "SSE4.1", g_cpu.sse41, font); bx += 7*8 + 12;
    badge(bx, y + 3, "SSE4.2", g_cpu.sse42, font); bx += 7*8 + 12;
    badge(bx, y + 3, "FMA",    g_cpu.fma,   font); bx += 4*8 + 12;
    badge(bx, y + 3, "AVX",    g_cpu.avx,   font); bx += 4*8 + 12;
    badge(bx, y + 3, "AVX2",   g_cpu.avx2,  font); bx += 5*8 + 12;
    badge(bx, y + 3, "F16C",   g_cpu.f16c,  font);
    y += 16;

    bb_puts(40, y, "AVX-512:", COL_ACCENT, COL_BG, font);
    bx = 40 + 13*CHAR_W;
    badge(bx, y + 3, "F",    g_cpu.avx512f,    font); bx += 2*8 + 12;
    badge(bx, y + 3, "DQ",   g_cpu.avx512dq,   font); bx += 3*8 + 12;
    badge(bx, y + 3, "CD",   g_cpu.avx512cd,   font); bx += 3*8 + 12;
    badge(bx, y + 3, "BW",   g_cpu.avx512bw,   font); bx += 3*8 + 12;
    badge(bx, y + 3, "VL",   g_cpu.avx512vl,   font); bx += 3*8 + 12;
    badge(bx, y + 3, "IFMA", g_cpu.avx512ifma, font); bx += 5*8 + 12;
    badge(bx, y + 3, "VBMI", g_cpu.avx512vbmi, font); bx += 5*8 + 12;
    badge(bx, y + 3, "VNNI", g_cpu.avx512_vnni, font);
    y += 16;

    bb_puts(40, y, "XSAVE:", COL_ACCENT, COL_BG, font);
    bb_puts(40 + 13*CHAR_W, y, g_cpu.xsave ? "YES" : "NO", COL_FG, COL_BG, font);
    bb_puts(40 + 18*CHAR_W, y, "OSXSAVE:", COL_ACCENT, COL_BG, font);
    bb_puts(40 + 27*CHAR_W, y, g_cpu.osxsave ? "YES" : "NO", COL_FG, COL_BG, font);
    y += 16;

    if (g_cpu.xsave) {
        bb_puts(40, y, "XCR0 cur:", COL_ACCENT, COL_BG, font);
        bb_hex(40 + 11*CHAR_W, y, g_cpu.xcr0_current, 8, COL_FG, COL_BG, font);
        bb_puts(40 + 23*CHAR_W, y, "sup:", COL_ACCENT, COL_BG, font);
        bb_hex(40 + 28*CHAR_W, y, g_cpu.xcr0_supported, 8, COL_FG, COL_BG, font);
        y += 16;
    }

    bb_puts(40, y, "VIRT:", COL_ACCENT, COL_BG, font);
    bx = 40 + 13*CHAR_W;
    badge(bx, y + 3, "VMX",     g_cpu.vmx,     font); bx += 4*8 + 12;
    badge(bx, y + 3, "SMX",     g_cpu.smx,     font); bx += 4*8 + 12;
    badge(bx, y + 3, "AESNI",   g_cpu.aesni,   font); bx += 6*8 + 12;
    badge(bx, y + 3, "PCLMUL",  g_cpu.pclmulqdq, font); bx += 7*8 + 12;
    badge(bx, y + 3, "RDRAND",  g_cpu.rdrand,  font); bx += 7*8 + 12;
    badge(bx, y + 3, "RDSEED",  g_cpu.rdseed,  font); bx += 7*8 + 12;
    badge(bx, y + 3, "SHA",     g_cpu.sha,     font);
    y += 16;

    bb_puts(40, y, "MISC:", COL_ACCENT, COL_BG, font);
    bx = 40 + 13*CHAR_W;
    badge(bx, y + 3, "TSC",     g_cpu.tsc,     font); bx += 4*8 + 12;
    badge(bx, y + 3, "MSR",     g_cpu.msr,     font); bx += 4*8 + 12;
    badge(bx, y + 3, "PAE",     g_cpu.pae,     font); bx += 4*8 + 12;
    badge(bx, y + 3, "APIC",    g_cpu.apic,    font); bx += 5*8 + 12;
    badge(bx, y + 3, "X2APIC",  g_cpu.x2apic,  font); bx += 7*8 + 12;
    badge(bx, y + 3, "POPCNT",  g_cpu.popcnt,  font); bx += 7*8 + 12;
    badge(bx, y + 3, "MOVBE",   g_cpu.movbe,   font); bx += 6*8 + 12;
    badge(bx, y + 3, "BMI1",    g_cpu.bmi1,    font); bx += 5*8 + 12;
    badge(bx, y + 3, "BMI2",    g_cpu.bmi2,    font);
    y += 16;

    bx = 40 + 13*CHAR_W;
    badge(bx, y + 3, "SMEP",    g_cpu.smep,    font); bx += 5*8 + 12;
    badge(bx, y + 3, "SMAP",    g_cpu.smap,    font); bx += 5*8 + 12;
    badge(bx, y + 3, "FSGSBASE",g_cpu.fsgsbase,font); bx += 9*8 + 12;
    badge(bx, y + 3, "INVPCID", g_cpu.invpcid, font); bx += 8*8 + 12;
    badge(bx, y + 3, "ERMS",    g_cpu.erms,    font); bx += 5*8 + 12;
    badge(bx, y + 3, "ADX",     g_cpu.adx,     font);
    y += 16;

    bb_puts(40, y, "PREFETCH:", COL_ACCENT, COL_BG, font);
    bx = 40 + 13*CHAR_W;
    badge(bx, y + 3, "L2",   g_cpu.prefetchwt1, font); bx += 3*8 + 12;
    badge(bx, y + 3, "DCU",  g_cpu.clflushopt,  font); bx += 4*8 + 12;
    badge(bx, y + 3, "CLWB", g_cpu.clwb,        font);
    y += 16;

    bb_puts(40, y, "PCU:", COL_ACCENT, COL_BG, font);
    if (!g_pcu.valid) {
        badge(40 + 13*CHAR_W, y + 3, "N/A", 0, font);
        y += 16;
    } else {
        if (g_pcu.cst_ok) {
            u64 bxx = 40 + 13*CHAR_W;
            badge(bxx, y + 3, "CST", 1, font); bxx += 4*8 + 8;

            char buf[8];
            buf[0] = 'L'; buf[1] = ':';
            buf[2] = '0' + (g_pcu.cstate_limit % 10);
            buf[3] = 0;
            badge(bxx, y + 3, buf, 1, font); bxx += 3*8 + 8;

            badge(bxx, y + 3, "LOCK", g_pcu.cstate_cfg_lock, font);
        }
        if (g_pcu.perf_status_ok || g_pcu.perf_ctl_ok) {
            bb_puts(40 + 28*CHAR_W, y, "cur:", COL_FG, COL_BG, font);
            bb_u64(40 + 32*CHAR_W, y, g_pcu.perf_status_ratio, COL_FG, COL_BG, font);
        }
        y += 14;

        if (g_pcu.perf_status_ok || g_pcu.perf_ctl_ok) {
            bb_puts(40 + 13*CHAR_W, y, "tgt:", COL_FG, COL_BG, font);
            bb_u64(40 + 17*CHAR_W, y, g_pcu.perf_ctl_ratio, COL_FG, COL_BG, font);
            bb_puts(40 + 21*CHAR_W, y, "VID:", COL_FG, COL_BG, font);
            bb_hex(40 + 25*CHAR_W, y, g_pcu.perf_status_vid, 4, COL_FG, COL_BG, font);
            badge(40 + 31*CHAR_W, y + 3, "EST", g_pcu.perf_ctl_est, font);
        }
        y += 14;

        if (g_pcu.therm_ok) {
            u64 bxx = 40 + 13*CHAR_W;
            badge(bxx, y + 3, "THERM", g_pcu.therm_digital_readout, font);
            bxx += 6*8 + 8;

            badge(bxx, y + 3, "PROCHOT", g_pcu.therm_prochot_now, font);
            bxx += 8*8 + 8;

            badge(bxx, y + 3, "PLIMIT", g_pcu.therm_power_limit_now, font);
        }
        y += 14;

        if (g_pcu.therm_ok) {
            bb_puts(40 + 13*CHAR_W, y, "T:", COL_FG, COL_BG, font);
            bb_u64(40 + 15*CHAR_W, y, g_pcu.therm_temp_c, COL_FG, COL_BG, font);
            bb_puts(40 + 18*CHAR_W, y, "C", COL_FG, COL_BG, font);
            y += 14;

            bb_puts(40 + 13*CHAR_W, y, "logs:", COL_FG, COL_BG, font);
            bb_puts(40 + 18*CHAR_W, y, "TH", COL_FG, COL_BG, font);
            bb_u64(40 + 20*CHAR_W, y, g_pcu.therm_thermal_status_log, COL_FG, COL_BG, font);
            bb_puts(40 + 22*CHAR_W, y, "PH", COL_FG, COL_BG, font);
            bb_u64(40 + 24*CHAR_W, y, g_pcu.therm_prochot_log, COL_FG, COL_BG, font);
            bb_puts(40 + 26*CHAR_W, y, "PL", COL_FG, COL_BG, font);
            bb_u64(40 + 28*CHAR_W, y, g_pcu.therm_power_limit_log, COL_FG, COL_BG, font);
            y += 14;
        }
    }
    y += 4;

    bb_puts(40, y, "EC:", COL_ACCENT, COL_BG, font);
    if (!g_pcu.ec_ok) {
        badge(40 + 13*CHAR_W, y + 3, "N/A", 0, font);
        y += 16;
    } else {
        bb_puts(40 + 6*CHAR_W, y, "00:", COL_FG, COL_BG, font);
        bb_hex(40 + 9*CHAR_W, y, g_pcu.ec_reg_00, 2, COL_FG, COL_BG, font);
        bb_puts(40 + 14*CHAR_W, y, "10:", COL_FG, COL_BG, font);
        bb_hex(40 + 17*CHAR_W, y, g_pcu.ec_reg_10, 2, COL_FG, COL_BG, font);
        bb_puts(40 + 22*CHAR_W, y, "1E:", COL_FG, COL_BG, font);
        bb_hex(40 + 25*CHAR_W, y, g_pcu.ec_reg_1e, 2, COL_FG, COL_BG, font);
        bb_puts(40 + 30*CHAR_W, y, "1F:", COL_FG, COL_BG, font);
        bb_hex(40 + 33*CHAR_W, y, g_pcu.ec_reg_1f, 2, COL_FG, COL_BG, font);
        y += 14;

        bb_puts(40 + 6*CHAR_W, y, "TACH:", COL_FG, COL_BG, font);
        bb_hex(40 + 11*CHAR_W, y, g_pcu.ec_fan1_raw, 4, COL_FG, COL_BG, font);
        bb_puts(40 + 18*CHAR_W, y, "FAN1:", COL_FG, COL_BG, font);
        if (g_pcu.ec_fan1_rpm > 0) {
            bb_u64(40 + 23*CHAR_W, y, g_pcu.ec_fan1_rpm, COL_CORE_ON, COL_BG, font);
            bb_puts(40 + 28*CHAR_W, y, "RPM", COL_CORE_ON, COL_BG, font);
        } else {
            bb_puts(40 + 23*CHAR_W, y, "STOP", COL_ACCENT, COL_BG, font);
        }
        y += 14;

        bb_puts(40 + 6*CHAR_W, y, "20:", COL_FG, COL_BG, font);
        bb_hex(40 + 9*CHAR_W, y, g_pcu.ec_reg_20, 2, COL_FG, COL_BG, font);
        bb_puts(40 + 14*CHAR_W, y, "21:", COL_FG, COL_BG, font);
        bb_hex(40 + 17*CHAR_W, y, g_pcu.ec_reg_21, 2, COL_FG, COL_BG, font);
        y += 14;
    }
    y += 4;

    bb_puts(40, y, "MMU:", COL_ACCENT, COL_BG, font);
    {
        u64 bxx = 40 + 6*CHAR_W;
        badge(bxx, y + 3, "PE",  g_cpu.mmu_pe,  font); bxx += 3*8 + 8;
        badge(bxx, y + 3, "PG",  g_cpu.mmu_pg,  font); bxx += 3*8 + 8;
        badge(bxx, y + 3, "PAE", g_cpu.mmu_pae, font); bxx += 4*8 + 8;
        badge(bxx, y + 3, "PSE", g_cpu.mmu_pse, font); bxx += 4*8 + 8;
        badge(bxx, y + 3, "PGE", g_cpu.mmu_pge, font); bxx += 4*8 + 8;
        badge(bxx, y + 3, "WP",  g_cpu.mmu_wp,  font); bxx += 3*8 + 8;
        badge(bxx, y + 3, "CD",  g_cpu.mmu_cd,  font); bxx += 3*8 + 8;
        badge(bxx, y + 3, "NW",  g_cpu.mmu_nw,  font);
    }
    y += 14;

    {
        u64 bxx = 40 + 6*CHAR_W;
        badge(bxx, y + 3, "SMEP", g_cpu.mmu_smep_enabled, font); bxx += 5*8 + 8;
        badge(bxx, y + 3, "SMAP", g_cpu.mmu_smap_enabled, font); bxx += 5*8 + 8;
        badge(bxx, y + 3, "PCID", g_cpu.mmu_pcid, font); bxx += 5*8 + 8;
        badge(bxx, y + 3, "LA57", g_cpu.mmu_la57, font); bxx += 5*8 + 8;
        badge(bxx, y + 3, "VMXE", g_cpu.mmu_vmx_enabled, font); bxx += 5*8 + 8;
        badge(bxx, y + 3, "PKE",  g_cpu.mmu_pke, font);
    }
    y += 14;

    bb_puts(40 + 6*CHAR_W, y, "CR0:", COL_FG, COL_BG, font);
    bb_hex(40 + 11*CHAR_W, y, g_cpu.cr0, 16, COL_FG, COL_BG, font);
    y += 14;
    bb_puts(40 + 6*CHAR_W, y, "CR3:", COL_FG, COL_BG, font);
    bb_hex(40 + 11*CHAR_W, y, g_cpu.cr3, 16, COL_FG, COL_BG, font);
    y += 14;
    bb_puts(40 + 6*CHAR_W, y, "CR4:", COL_FG, COL_BG, font);
    bb_hex(40 + 11*CHAR_W, y, g_cpu.cr4, 16, COL_FG, COL_BG, font);
    y += 14;
    bb_puts(40 + 6*CHAR_W, y, "CR2:", COL_FG, COL_BG, font);
    bb_hex(40 + 11*CHAR_W, y, g_cpu.cr2, 16, COL_FG, COL_BG, font);
    y += 16;

    bb_puts(40, y, "MCU:", COL_ACCENT, COL_BG, font);
    if (!g_cpu.mcu_found) {
        badge(40 + 6*CHAR_W, y + 3, "N/A", 0, font);
        y += 16;
    } else {
        bb_puts(40 + 6*CHAR_W, y, "B0:D0:F0", COL_FG, COL_BG, font);
        bb_puts(40 + 16*CHAR_W, y, "VID:", COL_FG, COL_BG, font);
        bb_hex16(40 + 20*CHAR_W, y, g_cpu.mcu_vendor_id, COL_FG, COL_BG, font);
        bb_puts(40 + 25*CHAR_W, y, "DID:", COL_FG, COL_BG, font);
        bb_hex16(40 + 29*CHAR_W, y, g_cpu.mcu_device_id, COL_FG, COL_BG, font);
        y += 14;

        bb_puts(40 + 6*CHAR_W, y, "Class:", COL_FG, COL_BG, font);
        bb_hex(40 + 13*CHAR_W, y, g_cpu.mcu_class, 2, COL_FG, COL_BG, font);
        bb_puts(40 + 16*CHAR_W, y, ":", COL_FG, COL_BG, font);
        bb_hex(40 + 17*CHAR_W, y, g_cpu.mcu_subclass, 2, COL_FG, COL_BG, font);
        bb_puts(40 + 20*CHAR_W, y, "Rev:", COL_FG, COL_BG, font);
        bb_hex(40 + 24*CHAR_W, y, g_cpu.mcu_revision, 2, COL_FG, COL_BG, font);
        bb_puts(40 + 28*CHAR_W, y, "Cmd:", COL_FG, COL_BG, font);
        bb_hex(40 + 32*CHAR_W, y, g_cpu.mcu_command, 4, COL_FG, COL_BG, font);
        y += 14;

        bb_puts(40 + 6*CHAR_W, y, "BAR0:", COL_FG, COL_BG, font);
        bb_hex(40 + 12*CHAR_W, y, g_cpu.mcu_bar0, 8, COL_FG, COL_BG, font);
        bb_puts(40 + 22*CHAR_W, y, "BAR1:", COL_FG, COL_BG, font);
        bb_hex(40 + 27*CHAR_W, y, g_cpu.mcu_bar1, 8, COL_FG, COL_BG, font);
        y += 14;

        bb_puts(40 + 6*CHAR_W, y, "TOLUD:", COL_FG, COL_BG, font);
        bb_hex(40 + 13*CHAR_W, y, g_cpu.mcu_tolud, 8, COL_FG, COL_BG, font);
        bb_puts(40 + 23*CHAR_W, y, "TSEG:", COL_FG, COL_BG, font);
        bb_hex(40 + 28*CHAR_W, y, g_cpu.mcu_tseg, 8, COL_FG, COL_BG, font);
        y += 14;

        bb_puts(40 + 6*CHAR_W, y, "RemapBase:", COL_FG, COL_BG, font);
        bb_hex(40 + 17*CHAR_W, y, g_cpu.mcu_remapbase, 8, COL_FG, COL_BG, font);
        y += 14;
        bb_puts(40 + 6*CHAR_W, y, "RemapLimit:", COL_FG, COL_BG, font);
        bb_hex(40 + 18*CHAR_W, y, g_cpu.mcu_remaplimit, 8, COL_FG, COL_BG, font);
        y += 14;

        bb_puts(40 + 6*CHAR_W, y, "SMRAM:", COL_FG, COL_BG, font);
        bb_hex(40 + 13*CHAR_W, y, g_cpu.mcu_smram, 2, COL_FG, COL_BG, font);
        bb_puts(40 + 17*CHAR_W, y, "MemType:", COL_FG, COL_BG, font);
        bb_hex(40 + 26*CHAR_W, y, g_cpu.mcu_memtype, 8, COL_FG, COL_BG, font);
        y += 14;

        bb_puts(40 + 6*CHAR_W, y, "DRAM Freq:", COL_FG, COL_BG, font);
        bb_u64(40 + 17*CHAR_W, y, g_cpu.mcu_dram_freq, COL_FG, COL_BG, font);
        bb_puts(40 + 21*CHAR_W, y, "MHz (est)", COL_FG, COL_BG, font);
        y += 14;
    }
    y += 4;

    bb_puts(40, y, "CAPS:", COL_ACCENT, COL_BG, font);
    bb_puts(40 + 13*CHAR_W, y, "FAM:", COL_FG, COL_BG, font);
    bb_u64(40 + 17*CHAR_W, y, g_cpu.family, COL_FG, COL_BG, font);
    bb_puts(40 + 20*CHAR_W, y, "MOD:", COL_FG, COL_BG, font);
    bb_u64(40 + 24*CHAR_W, y, g_cpu.model, COL_FG, COL_BG, font);
    bb_puts(40 + 28*CHAR_W, y, "STP:", COL_FG, COL_BG, font);
    bb_u64(40 + 32*CHAR_W, y, g_cpu.stepping, COL_FG, COL_BG, font);
    y += 16;

    bb_puts(40, y, "Phys:", COL_FG, COL_BG, font);
    bb_u64(40 + 5*CHAR_W, y, g_cpu.phys_bits, COL_FG, COL_BG, font);
    bb_puts(40 + 9*CHAR_W, y, "Virt:", COL_FG, COL_BG, font);
    bb_u64(40 + 14*CHAR_W, y, g_cpu.virt_bits, COL_FG, COL_BG, font);
    bb_puts(40 + 18*CHAR_W, y, "CLFL:", COL_FG, COL_BG, font);
    bb_u64(40 + 23*CHAR_W, y, g_cpu.clflush, COL_FG, COL_BG, font);
    y += 18;

    bb_puts(40, y, "CORES", COL_ACCENT, COL_BG, font);
    y += 14;

    u64 cy0 = y + 4, cw = 136, ch = 70, cg = 10, rh = ch + cg;
    u32 drawn = 0;
    for (u32 i = 0; i < g_ncpu && i < 64; i++) {
        u32 row = i / CORES_PER_ROW, col = i % CORES_PER_ROW;
        u64 cx = 40 + col*(cw + cg);
        u64 cy = cy0 + row*rh;
        if (cy + ch > g_screen_h - 100) break;
        draw_core_box(cx, cy, cw, ch, i, g_core_load[i]);
        drawn++;
    }

    u64 iy = cy0 + ((drawn + CORES_PER_ROW - 1)/CORES_PER_ROW)*rh + 6;
    if (iy > g_screen_h - 80) iy = g_screen_h - 80;

    bb_puts(40, iy, "RAM:", COL_ACCENT, COL_BG, font);
    u64 tram = (g_api && g_api->get_total_ram) ? g_api->get_total_ram() : 0;
    u64 tmb = tram/(1024*1024), tgb = tmb/1024;
    if (tgb > 0) {
        bb_u64(40 + 5*CHAR_W, iy, tgb, COL_FG, COL_BG, font);
        bb_puts(40 + 9*CHAR_W, iy, "GB", COL_FG, COL_BG, font);
    } else {
        bb_u64(40 + 5*CHAR_W, iy, tmb, COL_FG, COL_BG, font);
        bb_puts(40 + 9*CHAR_W, iy, "MB", COL_FG, COL_BG, font);
    }

    bb_puts(300, iy, "STORAGE:", COL_ACCENT, COL_BG, font);
    u64 sb = 0; u32 sc = 0;
    if (g_api) {
        if (g_api->get_storage_bytes) sb = g_api->get_storage_bytes();
        if (g_api->get_storage_count) sc = g_api->get_storage_count();
    }
    bb_u64(300 + 9*CHAR_W, iy, sc, COL_FG, COL_BG, font);
    bb_puts(300 + 12*CHAR_W, iy, "dev", COL_FG, COL_BG, font);
    u64 gb = sb/(1024ULL*1024*1024), mb = sb/(1024ULL*1024);
    if (gb > 0) {
        bb_u64(300 + 16*CHAR_W, iy, gb, COL_FG, COL_BG, font);
        bb_puts(300 + 19*CHAR_W, iy, "GB", COL_FG, COL_BG, font);
    } else {
        bb_u64(300 + 16*CHAR_W, iy, mb, COL_FG, COL_BG, font);
        bb_puts(300 + 20*CHAR_W, iy, "MB", COL_FG, COL_BG, font);
    }

    bb_puts(40, g_screen_h - CHAR_H*2,
            "[P] PCI Devices  [T] Timer  [Esc] Back", COL_FG, COL_BG, font);
}

static void draw_cursor(int x, int y) {
    if (x < 0 || y < 0) return;
    bb_fill_rect((u64)x - 1, (u64)y - 8, 3, 17, COL_FG);
    bb_fill_rect((u64)x - 8, (u64)y - 1, 17, 3, COL_FG);
}

static void blit(void) {
    if (!g_dws || !g_dws->blit_scaled) return;
    g_dws->blit_scaled(0, 0, g_screen_w, g_screen_h,
                       g_backbuf, g_backbuf_w, g_backbuf_h);
}

static void redraw_all(void) {
    if      (g_screen == SCREEN_MAIN)       draw_main_screen();
    else if (g_screen == SCREEN_HWMAP)      draw_hwmap_screen();
    else if (g_screen == SCREEN_PCI)        draw_pci_screen();
    else if (g_screen == SCREEN_MANAGEMENT) draw_mgmt_screen();
    else if (g_screen == SCREEN_TIMER)      draw_timer_screen();
    else if (g_screen == SCREEN_UEFI)       draw_uefi_screen();
    else if (g_screen == SCREEN_UEFI2)      draw_uefi_screen2();
    draw_cursor(g_mouse_x, g_mouse_y);
    blit();
    g_dirty = 0;
    g_drawn = 1;
    g_prev_mouse_x = g_mouse_x;
    g_prev_mouse_y = g_mouse_y;
}

static void confirm_power(void) {
    if (g_power_confirmation == POWER_ACTION_SHUTDOWN)
        g_power_action_status = (g_acpi && g_acpi->shutdown)
            ? g_acpi->shutdown() : DEPRESSURE_ACPI_POWER_UNAVAILABLE;
    else if (g_power_confirmation == POWER_ACTION_REBOOT)
        g_power_action_status = (g_acpi && g_acpi->reboot)
            ? g_acpi->reboot() : DEPRESSURE_ACPI_RESET_UNAVAILABLE;
    g_power_confirmation = POWER_ACTION_NONE;
    g_dirty = 1;
}

static void timer_select(u32 src) {
    if (!g_api || !g_api->timer_set_source) return;
    dstatus_t s = g_api->timer_set_source(src);
    if (g_uart) {
        g_uart->puts("[DepMgr] timer -> ");
        g_uart->puts(ts_name(src));
        g_uart->puts(" st=");
        g_uart->put_hex(s);
        g_uart->puts("\r\n");
    }
    g_dirty = 1;
}

static void handle_click(int x, int y) {
    if (g_screen == SCREEN_MAIN) {
        if ((u64)x >= g_management_x && (u64)x < g_management_x + g_management_w &&
            (u64)y >= g_management_y && (u64)y < g_management_y + g_management_h) {
            g_screen = SCREEN_MANAGEMENT; g_dirty = 1;
        } else if ((u64)x >= g_icon_x && (u64)x < g_icon_x + g_icon_w &&
                   (u64)y >= g_icon_y && (u64)y < g_icon_y + g_icon_h) {
            g_screen = SCREEN_HWMAP; g_dirty = 1;
        } else if ((u64)x >= g_timer_icon_x && (u64)x < g_timer_icon_x + g_timer_icon_w &&
                   (u64)y >= g_timer_icon_y && (u64)y < g_timer_icon_y + g_timer_icon_h) {
            g_screen = SCREEN_TIMER; g_dirty = 1;
        }
    } else if (g_screen == SCREEN_TIMER) {
        for (u32 i = 0; i < DTIMER_SOURCE_COUNT; i++)
            if ((u64)x >= g_timer_btn_x[i] && (u64)x < g_timer_btn_x[i] + g_timer_btn_w &&
                (u64)y >= g_timer_btn_y && (u64)y < g_timer_btn_y + g_timer_btn_h) {
                timer_select(i); return;
            }
    } else if (g_screen == SCREEN_MANAGEMENT) {
        if (g_power_confirmation != POWER_ACTION_NONE) {
            if ((u64)x >= g_confirm_x && (u64)x < g_confirm_x + 200 &&
                (u64)y >= g_confirm_y && (u64)y < g_confirm_y + 40) confirm_power();
            else if ((u64)x >= g_cancel_x && (u64)x < g_cancel_x + 200 &&
                     (u64)y >= g_cancel_y && (u64)y < g_cancel_y + 40) {
                g_power_confirmation = POWER_ACTION_NONE; g_dirty = 1;
            }
        } else if ((u64)x >= g_shutdown_x && (u64)x < g_shutdown_x + 260 &&
                   (u64)y >= g_shutdown_y && (u64)y < g_shutdown_y + 40) {
            g_power_confirmation = POWER_ACTION_SHUTDOWN;
            g_power_action_status = DEPRESSURE_STATUS_OK; g_dirty = 1;
        } else if ((u64)x >= g_reboot_x && (u64)x < g_reboot_x + 260 &&
                   (u64)y >= g_reboot_y && (u64)y < g_reboot_y + 40) {
            g_power_confirmation = POWER_ACTION_REBOOT;
            g_power_action_status = DEPRESSURE_STATUS_OK; g_dirty = 1;
        }
    }
}

static void handle_key(char c) {
    if (c == 27) {
        if (g_screen == SCREEN_UEFI2) {
            g_screen = SCREEN_UEFI; g_dirty = 1;
        } else if (g_screen == SCREEN_UEFI) {
            g_screen = SCREEN_MANAGEMENT; g_dirty = 1;
        } else if (g_screen == SCREEN_MANAGEMENT && g_power_confirmation != POWER_ACTION_NONE) {
            g_power_confirmation = POWER_ACTION_NONE; g_dirty = 1;
        } else if (g_screen != SCREEN_MAIN) {
            g_screen = SCREEN_MAIN; g_dirty = 1;
        }
    } else if (g_screen == SCREEN_HWMAP && (c == 'f' || c == 'F')) {
        int r = acpi_start_cooling();
        if (g_uart) {
            g_uart->puts("[DepMgr] acpi_start_cooling = ");
            g_uart->put_hex((u64)r);
            g_uart->puts("\r\n");
        }
        g_dirty = 1;
    } else if (g_screen == SCREEN_UEFI && (c == 'n' || c == 'N')) {
        g_screen = SCREEN_UEFI2; g_dirty = 1;
    } else if (g_screen == SCREEN_UEFI2 && (c == 'b' || c == 'B')) {
        g_screen = SCREEN_UEFI; g_dirty = 1;
    } else if (g_screen == SCREEN_MAIN && (c == 'm' || c == 'M')) {
        g_screen = SCREEN_MANAGEMENT; g_dirty = 1;
    } else if (g_screen == SCREEN_MAIN && (c == 't' || c == 'T')) {
        g_screen = SCREEN_TIMER; g_dirty = 1;
    } else if (g_screen == SCREEN_MANAGEMENT && g_power_confirmation != POWER_ACTION_NONE &&
               (c == 'y' || c == 'Y')) {
        confirm_power();
    } else if (g_screen == SCREEN_MANAGEMENT && g_power_confirmation != POWER_ACTION_NONE &&
               (c == 'n' || c == 'N')) {
        g_power_confirmation = POWER_ACTION_NONE; g_dirty = 1;
    } else if (g_screen == SCREEN_MANAGEMENT && g_power_confirmation == POWER_ACTION_NONE &&
               (c == 's' || c == 'S')) {
        g_power_confirmation = POWER_ACTION_SHUTDOWN; g_dirty = 1;
    } else if (g_screen == SCREEN_MANAGEMENT && g_power_confirmation == POWER_ACTION_NONE &&
               (c == 'r' || c == 'R')) {
        g_power_confirmation = POWER_ACTION_REBOOT; g_dirty = 1;
    } else if (g_screen == SCREEN_MANAGEMENT && g_power_confirmation == POWER_ACTION_NONE &&
               (c == 'b' || c == 'B')) {
        g_screen = SCREEN_UEFI; g_dirty = 1;
    } else if (g_screen == SCREEN_HWMAP && (c == 'p' || c == 'P')) {
        g_pci_page = 0; g_screen = SCREEN_PCI; g_dirty = 1;
    } else if (g_screen == SCREEN_HWMAP && (c == 't' || c == 'T')) {
        g_screen = SCREEN_TIMER; g_dirty = 1;
    } else if (g_screen == SCREEN_TIMER) {
        if (c == '1') timer_select(DTIMER_SOURCE_PIT);
        else if (c == '2') timer_select(DTIMER_SOURCE_HPET);
        else if (c == '3') timer_select(DTIMER_SOURCE_LAPIC);
    } else if (g_screen == SCREEN_PCI) {
        u32 count = 0, pages = 1;
        if (g_pci && g_pci->get_count && DSTATUS_IS_OK(g_pci->get_count(&count))) {
            pages = (count + PCI_PAGE_SIZE - 1)/PCI_PAGE_SIZE;
            if (pages == 0) pages = 1;
        }
        if (c == 'n' || c == 'N') { if (g_pci_page + 1 < pages) g_pci_page++; g_dirty = 1; }
        else if (c == 'b' || c == 'B') { if (g_pci_page > 0) g_pci_page--; g_dirty = 1; }
        else if (c == 'h' || c == 'H') { g_screen = SCREEN_HWMAP; g_dirty = 1; }
    }
}

void depmgr_poll(void) {
    int mx = g_mouse_x, my = g_mouse_y, clicked = 0;
    int dx = 0, dy = 0;
    int new_buttons = g_last_buttons;

    if (g_mouse && g_mouse->has_event && g_mouse->get_state) {
        int dxm = 0, dym = 0, b = 0;
        g_mouse->get_state(&dxm, &dym, &b);
        dx += dxm; dy += dym;
        new_buttons = b;
        clicked = (b & 1) && !(g_last_buttons & 1);
    }

    char key = 0;
    if (g_kbd && g_kbd->has_key && g_kbd->has_key())
        key = g_kbd->get_key();

    if (g_kbd && g_kbd->has_arrow && g_kbd->get_arrow) {
        while (g_kbd->has_arrow()) {
            int a = g_kbd->get_arrow();
            int step = 8;
            switch (a) {
                case 0: dy -= step; break;
                case 1: dy += step; break;
                case 2: dx -= step; break;
                case 3: dx += step; break;
            }
        }
    }

    g_mouse_x += dx;
    g_mouse_y += dy;
    if (g_mouse_x < 0) g_mouse_x = 0;
    if (g_mouse_y < 0) g_mouse_y = 0;
    if (g_mouse_x >= (int)g_screen_w) g_mouse_x = (int)g_screen_w - 1;
    if (g_mouse_y >= (int)g_screen_h) g_mouse_y = (int)g_screen_h - 1;
    if (dx != 0 || dy != 0) g_dirty = 1;

    g_last_buttons = new_buttons;
    mx = g_mouse_x;
    my = g_mouse_y;

    if (mx != g_prev_mouse_x || my != g_prev_mouse_y) g_dirty = 1;

    if (g_api && g_api->smp_get_load) {
        u32 limit = g_ncpu < 64 ? g_ncpu : 64;
        for (u32 i = 0; i < limit; i++) {
            u32 v = g_api->smp_get_load(i);
            if (v != g_prev_core_load[i]) {
                g_prev_core_load[i] = v;
                g_core_load[i] = v;
                g_dirty = 1;
            }
        }
    }

    if (key == '\n' || key == '\r' || key == ' ') {
        clicked = 1;
        mx = g_mouse_x;
        my = g_mouse_y;
    } else if (key) {
        handle_key(key);
    }

    if (g_dirty) redraw_all();
    if (clicked) handle_click(mx, my);
}

void *depmgr_main(dkernel_api_t *api, void *fb_info,
                  uart_api_t *uart, dws_api_t *dws, dkbd_api_t *kbd,
                  dmouse_api_t *mouse, int boot_mode, u32 ncpu) {
    (void)boot_mode;
    g_uart = uart; g_dws = dws; g_kbd = kbd; g_api = api; g_mouse = mouse;

    if (g_api && g_api->DFindAndGetDriver) {
        ddriver_t *pd = g_api->DFindAndGetDriver("pci");
        ddriver_t *ad = g_api->DFindAndGetDriver("acpi");
        if (pd && pd->api) g_pci = (pci_api_t *)pd->api;
        if (ad && ad->api) g_acpi = (acpi_api_t *)ad->api;
    }
    if (ncpu > 0) g_ncpu = ncpu;

    if (fb_info) {
        typedef struct { u64 base, width, height, pitch, format, size; } fb_info_t;
        fb_info_t *fb = (fb_info_t *)fb_info;
        g_screen_w = fb->width;
        g_screen_h = fb->height;
    }
    if (g_screen_w > BACKBUF_MAX_W) g_screen_w = BACKBUF_MAX_W;
    if (g_screen_h > BACKBUF_MAX_H) g_screen_h = BACKBUF_MAX_H;
    if (g_screen_w == 0) g_screen_w = 800;
    if (g_screen_h == 0) g_screen_h = 600;

    g_backbuf_w = (u32)g_screen_w;
    g_backbuf_h = (u32)g_screen_h;
    g_backbuf = (u32 *)BACKBUF_ADDR;

    if (!g_dws) {
        if (g_uart) g_uart->puts("[DepMgr] No DWS!\r\n");
        return 0;
    }
    if (g_uart) g_uart->puts("[DepMgr] Starting GUI...\r\n");

    cpu_detect();
    uefi_probe();

    if (g_api && g_api->smp_get_ncpu) {
        u32 n = g_api->smp_get_ncpu();
        if (n > 0) g_ncpu = n;
    }
    if (g_api && g_api->smp_get_load) {
        u32 limit = g_ncpu < 64 ? g_ncpu : 64;
        for (u32 i = 0; i < limit; i++) {
            g_core_load[i] = g_api->smp_get_load(i);
            g_prev_core_load[i] = g_core_load[i];
        }
    }

    redraw_all();
    if (api && api->task_add) api->task_add("DepMgr", depmgr_poll, 1);
    if (g_uart) g_uart->puts("[DepMgr] GUI ready.\r\n");
    return (void *)depmgr_poll;
}