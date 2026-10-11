#ifndef DUMCAS_H
#define DUMCAS_H
#include "dkrnl.h"

#define UMCAS_RING_SIZE 256

typedef enum {
    UMCAS_OP_NONE = 0,
    UMCAS_OP_METRICS,
    UMCAS_OP_CUSTOM,
} umcas_opcode_t;

typedef struct umcas_task {
    u32 opcode;
    u32 core_id;
    u64 arg0;
    u64 arg1;
    u64 arg2;
    u64 arg3;
} umcas_task_t;

typedef struct umcas_ring {
    volatile u32 head;
    volatile u32 tail;
    u32 capacity;
    umcas_task_t *tasks;
} umcas_ring_t;

typedef struct dumcas_api {
    u32 version;
    u32 _pad;
    int  (*submit)(u32 opcode, u64 a0, u64 a1, u64 a2, u64 a3);
    void (*drain)(void);
    u64  (*get_submitted)(void);
    u64  (*get_executed)(void);
    u32  (*get_pending)(void);
} dumcas_api_t;

void dumcas_main(dkernel_api_t *api, void *fb_info);

#endif