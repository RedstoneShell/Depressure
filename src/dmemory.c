// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 RedstoneShell
//
// This file is part of the Depressure Operating System.

#include "dmemory.h"
#include "dkrnl.h"

typedef struct uart_api {
    u32 version;
    u32 _pad;
    void (*putc)(char c);
    void (*puts)(const char *str);
    void (*put_hex)(u64 value);
    int  (*has_data)(void);
    char (*getc)(void);
} uart_api_t;

static uart_api_t *g_uart = 0;

void dmemory_set_uart(void *uart) {
    g_uart = (uart_api_t *)uart;
}

static void hlog(const char *s) {
    if (g_uart && g_uart->puts) g_uart->puts(s);
}

static void hlog_dec(u64 v) {
    char buf[24];
    int i = 0;
    if (v == 0) { hlog("0"); return; }
    while (v > 0) { buf[i++] = '0' + (v % 10); v /= 10; }
    while (i > 0) { char c[2] = { buf[--i], 0 }; hlog(c); }
}

static void hlog_hex(u64 v) {
    if (g_uart && g_uart->put_hex) {
        g_uart->put_hex(v);
    } else {
        static const char hex[] = "0123456789ABCDEF";
        char buf[24];
        int i = 0;
        buf[i++] = '0'; buf[i++] = 'x';
        for (int k = 60; k >= 0; k -= 4) buf[i++] = hex[(v >> k) & 0xF];
        buf[i] = 0;
        hlog(buf);
    }
}

static dh_heap_header_t *g_hdr = 0;
static int g_initialized = 0;
static u64 g_heap_generation = 1; 
static volatile int g_lhgc_compacting = 0;

static inline void mem_zero(void *dst, u64 n) {
    u8 *d = (u8 *)dst;
    for (u64 i = 0; i < n; i++) d[i] = 0;
}

static inline void mem_copy(void *dst, const void *src, u64 n) {
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    for (u64 i = 0; i < n; i++) d[i] = s[i];
}

static inline void mem_move(void *dst, const void *src, u64 n) {
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    if (d == s) return;
    if (d < s) {
        for (u64 i = 0; i < n; i++) d[i] = s[i];
    } else {
        for (u64 i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
}

static int str_eq_n(const char *a, const char *b, u64 max) {
    for (u64 i = 0; i < max; i++) {
        if (a[i] != b[i]) return 0;
        if (a[i] == 0) return 1;
    }
    return 1;
}

static void str_copy_n(char *dst, const char *src, u64 max) {
    u64 i = 0;
    if (!src) { dst[0] = 0; return; }
    for (; i + 1 < max && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
}

static u64 fnv1a_64(const void *data, u64 n) {
    const u8 *p = (const u8 *)data;
    u64 h = 0xCBF29CE484222325ULL;
    for (u64 i = 0; i < n; i++) {
        h ^= p[i];
        h *= 0x100000001B3ULL;
    }
    return h;
}

static inline dh_heap_block_t *slot_ptr(u32 slot) {
    u8 *base = (u8 *)g_hdr + sizeof(dh_heap_header_t);
    return (dh_heap_block_t *)(base + (u64)slot * g_hdr->block_step);
}

static u32 find_free_slot(void) {
    for (u32 i = 0; i < g_hdr->total_slots; i++) {
        dh_heap_block_t *b = slot_ptr(i);
        if (b->flags == DHEAP_BLOCK_FREE) return i;
    }
    return (u32)-1;
}

static dh_heap_block_t *resolve_heap(D_HEAP_POINTER h, u32 *out_slot,
                                     dstatus_t *out_err) {
    if (out_err) *out_err = DEPRESSURE_STATUS_OK;
    if (!h || !g_hdr) {
        if (out_err) *out_err = DEPRESSURE_HEAP_UNDEFINED;
        return 0;
    }
    if (h->magic != 0xDEADBEEFu) {
        if (out_err) *out_err = DEPRESSURE_HEAP_UNDEFINED;
        return 0;
    }
    if (h->slot >= g_hdr->total_slots) {
        if (out_err) *out_err = DEPRESSURE_HEAP_UNDEFINED;
        return 0;
    }
    dh_heap_block_t *b = slot_ptr(h->slot);
    if (b->pattern != DHBL_PATTERN || b->flags != DHEAP_BLOCK_USED) {
        if (out_err) *out_err = DEPRESSURE_HEAP_DESTROYED_BY_LHGC;
        return 0;
    }
    if (h->generation != b->generation) {
        if (out_err) *out_err = DEPRESSURE_HEAP_DESTROYED_BY_LHGC;
        return 0;
    }
    if (out_slot) *out_slot = h->slot;
    return b;
}

dstatus_t dmemory_init(void) {
    if (g_initialized) return DEPRESSURE_ALREADY_INITIALIZED;
    g_hdr = (dh_heap_header_t *)DHMB_ADDR;
    mem_zero(g_hdr, sizeof(dh_heap_header_t));
    g_hdr->pattern     = DHMB_PATTERN;
    g_hdr->version     = DHMB_VERSION;
    g_hdr->size        = DHMB_SIZE;
    g_hdr->block_step  = (u32)sizeof(dh_heap_block_t);
    g_hdr->total_slots = (u32)((DHMB_SIZE - sizeof(dh_heap_header_t))
                               / g_hdr->block_step);
    g_hdr->blocks_count = 0;
    g_hdr->free_slots   = g_hdr->total_slots;
    g_hdr->scan_tick    = 0;
    g_hdr->gc_runs      = 0;
    g_hdr->gc_reclaimed = 0;
        for (u32 i = 0; i < g_hdr->total_slots; i++) {
        dh_heap_block_t *b = slot_ptr(i);
        b->pattern      = DHBL_PATTERN;
        b->flags        = DHEAP_BLOCK_FREE;
        b->source[0]    = 0;
        b->size         = 0;
        b->capacity     = DHEAP_BLOCK_DATA_SIZE;
        b->alloc_head   = 0;
        b->last_scan_tick = 0;
        b->last_hash    = 0;
        b->same_count   = 0;
        b->generation   = 0;
    }
    g_initialized = 1;
    return DEPRESSURE_STATUS_OK;
}

D_HEAP_POINTER DCreateHeap(const char *source) {
    if (!g_initialized) return 0;
    if (!source || !source[0]) return 0;
    if (g_lhgc_compacting) return 0;
    if (g_hdr->free_slots == 0) return 0;
    u32 slot = find_free_slot();
    if (slot == (u32)-1) return 0;
    dh_heap_block_t *b = slot_ptr(slot);
        b->pattern        = DHBL_PATTERN;
    b->flags          = DHEAP_BLOCK_USED;
    str_copy_n(b->source, source, DHEAP_SOURCE_MAX);
    b->size           = 0;
    b->capacity       = DHEAP_BLOCK_DATA_SIZE;
    b->alloc_head     = 0;
    b->last_scan_tick = 0;
    b->last_hash      = 0;
    b->same_count     = 0;
    b->generation     = g_heap_generation++;
    g_hdr->blocks_count++;
    g_hdr->free_slots--;
    static dh_heap_t g_handles[DHEAP_MAX_HEAPS];
    static u32 g_handle_next = 0;
    dh_heap_t *h = &g_handles[g_handle_next++ % DHEAP_MAX_HEAPS];
    h->slot       = slot;
    h->magic      = 0xDEADBEEFu;
    h->generation = b->generation;
    return h;
}

dstatus_t DWriteHeap(D_HEAP_POINTER heap, u64 offset,
                     const void *data, u64 size) {
    if (!heap || !data) return DEPRESSURE_NULL_DATA;
    if (!g_initialized) return DEPRESSURE_NOT_INITIALIZED;
    u32 slot;
    dstatus_t err;
    dh_heap_block_t *b = resolve_heap(heap, &slot, &err);
    if (!b) return err;
    if (offset > b->capacity || size > b->capacity - offset)
        return DEPRESSURE_HEAP_WITHOUT_FREE_SPACE;
    if (b->alloc_head == 0 && offset > 0) {
        mem_zero(b->data, offset);
    }
    mem_copy(b->data + offset, data, size);
    if (offset + size > b->alloc_head)
        b->alloc_head = offset + size;
    if (offset + size > b->size)
        b->size = offset + size;
    return DEPRESSURE_STATUS_OK;
}

dstatus_t DReadHeap(D_HEAP_POINTER heap, u64 offset,
                    void *out, u64 size) {
    if (!heap || !out) return DEPRESSURE_NULL_DATA;
    if (!g_initialized) return DEPRESSURE_NOT_INITIALIZED;
    u32 slot;
    dstatus_t err;
    dh_heap_block_t *b = resolve_heap(heap, &slot, &err);
    if (!b) return err;
    if (offset > b->alloc_head || size > b->alloc_head - offset)
        return DEPRESSURE_OUT_OF_RANGE;
    mem_copy(out, b->data + offset, size);
    return DEPRESSURE_STATUS_OK;
}

dstatus_t DCloseHeap(D_HEAP_POINTER heap) {
    if (!heap) return DEPRESSURE_NULL_DATA;
    if (!g_initialized) return DEPRESSURE_NOT_INITIALIZED;
    u32 slot;
    dstatus_t err;
    dh_heap_block_t *b = resolve_heap(heap, &slot, &err);
    if (!b) return err;
    b->flags          = DHEAP_BLOCK_FREE;
    b->source[0]      = 0;
    b->size           = 0;
    b->alloc_head     = 0;
    b->last_scan_tick = 0;
    b->last_hash      = 0;
    b->same_count     = 0;
    b->generation     = 0;
    heap->magic       = 0;
    heap->generation  = 0;
    if (g_hdr->blocks_count > 0) g_hdr->blocks_count--;
    g_hdr->free_slots++;
    return DEPRESSURE_STATUS_OK;
}

u64 dmemory_total(void) {
    return DHMB_SIZE;
}

u64 dmemory_used(void) {
    if (!g_hdr) return 0;
    u64 used = 0;
    for (u32 i = 0; i < g_hdr->total_slots; i++) {
        dh_heap_block_t *b = slot_ptr(i);
        if (b->flags == DHEAP_BLOCK_USED) used += b->alloc_head;
    }
    return used;
}

u32 dmemory_heap_count(void) {
    return g_hdr ? g_hdr->blocks_count : 0;
}

u32 dmemory_free_slots(void) {
    return g_hdr ? g_hdr->free_slots : 0;
}

static void put_hex(void (*log)(const char *), u64 v) {
    static const char hex[] = "0123456789ABCDEF";
    char buf[24];
    int i = 0;
    buf[i++] = '0'; buf[i++] = 'x';
    for (int k = 60; k >= 0; k -= 4) buf[i++] = hex[(v >> k) & 0xF];
    buf[i] = 0;
    log(buf);
}

static void put_dec(void (*log)(const char *), u64 v) {
    char buf[24];
    int i = 0;
    if (v == 0) { log("0"); return; }
    while (v > 0) { buf[i++] = '0' + (v % 10); v /= 10; }
    while (i > 0) { char c[2] = { buf[--i], 0 }; log(c); }
}

void dmemory_dump(void (*log)(const char *)) {
    if (!log) return;
    if (!g_hdr) { log("[heap] not initialized\n"); return; }

    log("[heap] === Heap Memory Block ===\n");
    log("[heap] pattern=");     put_hex(log, g_hdr->pattern);     log("\n");
    log("[heap] size=");        put_dec(log, g_hdr->size);        log("\n");
    log("[heap] block_step=");  put_dec(log, g_hdr->block_step);  log("\n");
    log("[heap] total_slots="); put_dec(log, g_hdr->total_slots); log("\n");
    log("[heap] free_slots=");  put_dec(log, g_hdr->free_slots);  log("\n");
    log("[heap] blocks_count=");put_dec(log, g_hdr->blocks_count);log("\n");
    log("[heap] gc_runs=");     put_dec(log, g_hdr->gc_runs);     log("\n");
    log("[heap] gc_reclaimed=");put_dec(log, g_hdr->gc_reclaimed);log("\n");

    for (u32 i = 0; i < g_hdr->total_slots; i++) {
        dh_heap_block_t *b = slot_ptr(i);
        if (b->flags != DHEAP_BLOCK_USED) continue;
        log("[heap] slot["); put_dec(log, i); log("] src=");
        log(b->source);
        log(" size="); put_dec(log, b->size);
        log(" head="); put_dec(log, b->alloc_head);
        log(" same="); put_dec(log, b->same_count);
        log("\n");
    }
    log("[heap] ===========================\n");
}

#define LHGC_SLOTS_PER_TICK   8u
#define LHGC_SCANS_TO_KILL    2u
static u32 g_lhgc_cursor  = 0;
static u32 g_lhgc_dead    = 0;
static int g_lhgc_phase   = 0;

static void lhgc_scan_chunk(void) {
    u32 end = g_lhgc_cursor + LHGC_SLOTS_PER_TICK;
    if (end > g_hdr->total_slots) end = g_hdr->total_slots;

    if (g_lhgc_cursor == 0) {
        hlog("[LHGC] scan start tick=");
        hlog_dec(g_hdr->scan_tick);
        hlog(" slots=");
        hlog_dec(g_hdr->total_slots);
        hlog(" used=");
        hlog_dec(g_hdr->blocks_count);
        hlog("\n");
    }

    for (u32 i = g_lhgc_cursor; i < end; i++) {
        dh_heap_block_t *b = slot_ptr(i);
        if (b->flags != DHEAP_BLOCK_USED) continue;

        u64 h = (b->alloc_head == 0)
              ? 0
              : fnv1a_64(b->data, b->alloc_head);

        if (h == b->last_hash && b->alloc_head > 0) {
            b->same_count++;
        } else {
            b->same_count = 0;
        }
        b->last_hash      = h;
        b->last_scan_tick = g_hdr->scan_tick;

        if (b->same_count >= LHGC_SCANS_TO_KILL) {
            b->flags = DHEAP_BLOCK_DEAD;
            g_lhgc_dead++;

            hlog("[LHGC] LEAK slot=");
            hlog_dec(i);
            hlog(" src=");
            hlog(b->source);
            hlog(" head=");
            hlog_dec(b->alloc_head);
            hlog(" hash=");
            hlog_hex(h);
            hlog(" same=");
            hlog_dec(b->same_count);
            hlog("\n");
        }
    }

    g_lhgc_cursor = end;
    dkrnl_task_yield();
}

static void lhgc_compact(void) {
    u32 write = 0;
    for (u32 read = 0; read < g_hdr->total_slots; read++) {
        dh_heap_block_t *src = slot_ptr(read);
        if (src->flags == DHEAP_BLOCK_DEAD) continue;
        if (src->flags == DHEAP_BLOCK_FREE) continue;
        if (write != read) {
            dh_heap_block_t *dst = slot_ptr(write);
            mem_move(dst, src, g_hdr->block_step);
        }
        write++;
    }
    for (u32 i = write; i < g_hdr->total_slots; i++) {
        dh_heap_block_t *b = slot_ptr(i);
        b->pattern = DHBL_PATTERN;
        b->flags   = DHEAP_BLOCK_FREE;
        b->size    = 0;
        b->alloc_head = 0;
        b->source[0] = 0;
        b->last_hash = 0;
        b->same_count = 0;
    }
    g_hdr->blocks_count = write;
    g_hdr->free_slots   = g_hdr->total_slots - write;
}

void lhgc_task(void) {
    if (!g_initialized || !g_hdr) return;
    g_hdr->scan_tick++;
    g_hdr->gc_runs++;
    if (g_lhgc_phase == 0) {
        lhgc_scan_chunk();
        if (g_lhgc_cursor >= g_hdr->total_slots) {
            hlog("[LHGC] scan done, dead=");
            hlog_dec(g_lhgc_dead);
            hlog(" -> compact phase\n");
            g_lhgc_phase = 1;
            g_lhgc_cursor = 0;
            g_lhgc_compacting = 1;
        }
    } else {
        static u32 compact_cursor = 0;
        static u32 compact_write  = 0;
        u32 end = compact_cursor + LHGC_SLOTS_PER_TICK;
        if (end > g_hdr->total_slots) end = g_hdr->total_slots;
        for (u32 read = compact_cursor; read < end; read++) {
            dh_heap_block_t *src = slot_ptr(read);
            if (src->flags == DHEAP_BLOCK_DEAD) continue;
            if (src->flags == DHEAP_BLOCK_FREE) continue;
            if (compact_write != read) {
                dh_heap_block_t *dst = slot_ptr(compact_write);
                mem_move(dst, src, g_hdr->block_step);
            }
            compact_write++;
        }
        compact_cursor = end;
        if (compact_cursor >= g_hdr->total_slots) {
            for (u32 i = compact_write; i < g_hdr->total_slots; i++) {
                dh_heap_block_t *b = slot_ptr(i);
                b->pattern      = DHBL_PATTERN;
                b->flags        = DHEAP_BLOCK_FREE;
                b->size         = 0;
                b->alloc_head   = 0;
                b->source[0]    = 0;
                b->last_hash    = 0;
                b->same_count   = 0;
                b->generation   = 0;
            }
            g_hdr->blocks_count = compact_write;
            g_hdr->free_slots   = g_hdr->total_slots - compact_write;
            g_hdr->gc_reclaimed += g_lhgc_dead;
            hlog("[LHGC] compact done: alive=");
            hlog_dec(compact_write);
            hlog(" reclaimed_total=");
            hlog_dec(g_hdr->gc_reclaimed);
            hlog(" free_slots=");
            hlog_dec(g_hdr->free_slots);
            hlog("\n");
            g_lhgc_dead       = 0;
            g_lhgc_cursor     = 0;
            compact_cursor    = 0;
            compact_write     = 0;
            g_lhgc_phase      = 0;
            g_lhgc_compacting = 0;
        }
        dkrnl_task_yield();
    }
}