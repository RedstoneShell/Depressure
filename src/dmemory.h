// SPDX-License-Identifier: LicenseRef-Depressure-BSL-1.1
// Copyright (c) 2026 RedstoneShell
//
// Change Date: October 11, 2158
// Change License: GPL-3.0-or-later
// Additional Use Grant: Compilation of third-party software using these headers is permitted without restrictions.

#ifndef DMEMORY_H
#define DMEMORY_H

#include "dtypes.h"
#include "dstat.h"

#define DHMB_ADDR        0x05000000ULL
#define DHMB_SIZE        (16ULL * 1024ULL * 1024ULL)  /* 16 MB */

#define DHMB_PATTERN     0x424D4844u   /* "DHMB" */
#define DHBL_PATTERN     0x4C424844u   /* "DHBL" */
#define DHMB_VERSION     0x00000001u

#define DHEAP_BLOCK_DATA_SIZE  (256ULL * 1024ULL)   /* 256 KB payload */
#define DHEAP_MAX_HEAPS        64u
#define DHEAP_SOURCE_MAX       32u
#define DHEAP_BLOCK_FREE       0u
#define DHEAP_BLOCK_USED       1u
#define DHEAP_BLOCK_DEAD       2u

typedef struct dh_heap_block {
    u32  pattern;                       /* DHBL_PATTERN */
    u32  flags;                         /* DHEAP_BLOCK_* */
    char source[DHEAP_SOURCE_MAX];      /* task_name of creator */
    u64  size;                          /* real used bytes */
    u64  capacity;                      /* DHEAP_BLOCK_DATA_SIZE */
    u64  alloc_head;                    /* bump pointer in data[] */
    u64  last_scan_tick;                /* LHGC: where scan */
    u64  last_hash;                     /* LHGC: hash of last scan */
    u64  generation;                    /* NEW: Heap ID */
    u32  same_count;                    /* LHGC: Count of same scans */
    u32  _pad;
    u8   data[DHEAP_BLOCK_DATA_SIZE];   /* payload, zero-filled at creation */
} dh_heap_block_t;

typedef struct dh_heap_header {
    u32  pattern;                       /* DHMB_PATTERN */
    u32  version;                       /* DHMB_VERSION */
    u64  size;                          /* Std region size */
    u32  blocks_count;                  /* Used blocks */
    u32  block_step;                    /* = sizeof(dh_heap_block_t) */
    u32  total_slots;                   /* Total slots in region */
    u32  free_slots;                    /* Free slots */
    u64  scan_tick;                     /* LHGC Last scan */
    u64  gc_runs;                       /* LHGC scan count */
    u64  gc_reclaimed;                  /* Count of killed blocks by LHGC */
    u8   _reserved[64];
} dh_heap_header_t;

typedef struct dh_heap {
    u32  slot;                          /* Slot idx in region*/
    u32  magic;                         /* 0xDEADBEEF - for validation */
    u64  generation;                    /* ++ at every slot reusage */
} dh_heap_t;

typedef dh_heap_t *D_HEAP_POINTER;

dstatus_t dmemory_init(void);
D_HEAP_POINTER DCreateHeap(const char *source);
dstatus_t DWriteHeap(D_HEAP_POINTER heap, u64 offset,
                     const void *data, u64 size);
dstatus_t DReadHeap(D_HEAP_POINTER heap, u64 offset,
                    void *out, u64 size);
dstatus_t DCloseHeap(D_HEAP_POINTER heap);
u64 dmemory_total(void);
u64 dmemory_used(void);
u32 dmemory_heap_count(void);
u32 dmemory_free_slots(void);
void dmemory_dump(void (*log)(const char *));
void dmemory_set_uart(void *uart);
void lhgc_task(void);

#endif /* DMEMORY_H */