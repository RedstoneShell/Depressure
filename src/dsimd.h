#ifndef DSIMD_H
#define DSIMD_H
#include "dtypes.h"
#include "dstat.h"
typedef struct dsimd_caps {
    /* CPUID.1 */
    u32 has_sse;
    u32 has_sse2;
    u32 has_sse3;
    u32 has_ssse3;
    u32 has_sse41;
    u32 has_sse42;
    u32 has_fma;
    u32 has_xsave;
    u32 has_osxsave;
    u32 has_avx;
    /* CPUID.7 */
    u32 has_bmi1;
    u32 has_bmi2;
    u32 has_avx2;
    u32 has_avx512f;
    u32 has_avx512dq;
    u32 has_avx512cd;
    u32 has_avx512bw;
    u32 has_avx512vl;
    u32 has_avx512vbmi;
    u32 has_avx512vnni;
    u32 has_avx512ifma;
    /* XCR0 */
    u32 xcr0_before;
    u32 xcr0_after;
    u32 avx2_enabled;
    u32 avx512_enabled;
    u32 avx512_verified;
} dsimd_caps_t;
typedef enum dsimd_type {
    DSIMD_TYPE_UNKNOWN = 0,
    DSIMD_TYPE_F32,        /* float 32-bit */
    DSIMD_TYPE_F64,        /* double 64-bit */
    DSIMD_TYPE_I8,
    DSIMD_TYPE_I16,
    DSIMD_TYPE_I32,
    DSIMD_TYPE_I64,
    DSIMD_TYPE_U8,
    DSIMD_TYPE_U16,
    DSIMD_TYPE_U32,
    DSIMD_TYPE_U64,
} dsimd_type_t;

typedef struct dsimd_buffer {
    void       *data;
    u64         bytes;
    u64         count;
    dsimd_type_t type;
    u32         alignment;
} dsimd_buffer_t;

typedef struct dsimd_v256 {
    u64 q[4];
} dsimd_v256_t;

typedef struct dsimd_v512 {
    u64 q[8];
} dsimd_v512_t;

typedef struct dsimd_api {
    u32 version;
    u32 _pad;
    dstatus_t (*get_caps)(dsimd_caps_t *out);
    dstatus_t (*get_xcr0)(u32 *out);
    dstatus_t (*test_avx2)(void);
    dstatus_t (*test_avx512)(void);
    dstatus_t (*type_size)(dsimd_type_t type, u64 *out_bytes);
    dstatus_t (*type_name)(dsimd_type_t type, const char **out_name);
    dstatus_t (*buffer_check)(const dsimd_buffer_t *buf);
    dstatus_t (*v256_add_u64)(const dsimd_v256_t *a,
                              const dsimd_v256_t *b,
                              dsimd_v256_t *out);
    dstatus_t (*v256_xor)(const dsimd_v256_t *a,
                          const dsimd_v256_t *b,
                          dsimd_v256_t *out);
    dstatus_t (*v256_zero)(dsimd_v256_t *out);
    dstatus_t (*v256_sum_u64)(const dsimd_v256_t *a, u64 *out_sum);
    dstatus_t (*v512_add_u64)(const dsimd_v512_t *a,
                              const dsimd_v512_t *b,
                              dsimd_v512_t *out);
    dstatus_t (*v512_xor)(const dsimd_v512_t *a,
                          const dsimd_v512_t *b,
                          dsimd_v512_t *out);
    dstatus_t (*v512_zero)(dsimd_v512_t *out);
    dstatus_t (*v512_sum_u64)(const dsimd_v512_t *a, u64 *out_sum);
} dsimd_api_t;
void dsimd_main(void *api, void *fb_info);
#endif /* DSIMD_H */