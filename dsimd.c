#include "dtypes.h"
#include "ddriver.h"
#include "dkrnl.h"
#include "dsimd.h"
#include "dstat.h"

#define CPUID_1_EDX_SSE         (1u << 25)
#define CPUID_1_EDX_SSE2        (1u << 26)

#define CPUID_1_ECX_SSE3        (1u << 0)
#define CPUID_1_ECX_SSSE3       (1u << 9)
#define CPUID_1_ECX_FMA         (1u << 12)
#define CPUID_1_ECX_SSE41       (1u << 19)
#define CPUID_1_ECX_SSE42       (1u << 20)
#define CPUID_1_ECX_XSAVE       (1u << 26)
#define CPUID_1_ECX_OSXSAVE     (1u << 27)
#define CPUID_1_ECX_AVX         (1u << 28)

#define CPUID_7_EBX_BMI1        (1u << 3)
#define CPUID_7_EBX_AVX2        (1u << 5)
#define CPUID_7_EBX_BMI2        (1u << 8)
#define CPUID_7_EBX_AVX512F     (1u << 16)
#define CPUID_7_EBX_AVX512DQ    (1u << 17)
#define CPUID_7_EBX_AVX512IFMA  (1u << 21)
#define CPUID_7_EBX_AVX512CD    (1u << 28)
#define CPUID_7_EBX_AVX512BW    (1u << 30)
#define CPUID_7_EBX_AVX512VL    (1u << 31)

#define CPUID_7_ECX_AVX512VBMI  (1u << 1)
#define CPUID_7_ECX_AVX512VNNI  (1u << 11)

#define XCR0_X87        (1ull << 0)
#define XCR0_SSE        (1ull << 1)
#define XCR0_AVX        (1ull << 2)
#define XCR0_OPMASK     (1ull << 5)
#define XCR0_ZMM_HI256  (1ull << 6)
#define XCR0_HI16_ZMM   (1ull << 7)

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
static void uart_put_hex32(u32 v) {
    static const char hex[] = "0123456789ABCDEF";
    uart_puts("0x");
    for (int i = 28; i >= 0; i -= 4) uart_putc(hex[(v >> i) & 0xF]);
}
static void uart_put_hex64(u64 v) {
    static const char hex[] = "0123456789ABCDEF";
    uart_puts("0x");
    for (int i = 60; i >= 0; i -= 4) uart_putc(hex[(v >> i) & 0xF]);
}

static inline void cpuid(u32 leaf, u32 sub, u32 *a, u32 *b, u32 *c, u32 *d) {
    __asm__ volatile ("cpuid"
        : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
        : "a"(leaf), "c"(sub));
}
static inline u64 read_cr4(void) {
    u64 v;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(v));
    return v;
}
static inline void write_cr4(u64 v) {
    __asm__ volatile ("mov %0, %%cr4" :: "r"(v));
}
static inline u64 xgetbv(u32 reg) {
    u32 lo, hi;
    __asm__ volatile ("xgetbv" : "=a"(lo), "=d"(hi) : "c"(reg));
    return ((u64)hi << 32) | lo;
}
static inline void xsetbv(u32 reg, u64 val) {
    u32 lo = (u32)val;
    u32 hi = (u32)(val >> 32);
    __asm__ volatile ("xsetbv" :: "a"(lo), "d"(hi), "c"(reg));
}

static dsimd_caps_t g_caps_data;
static u32 g_caps_bits = 0;

static void dsimd_enable(void) {
    u32 eax, ebx, ecx, edx;
    u32 max_leaf;

    uart_puts("[dsimd] init start\n");

    cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    max_leaf = eax;

    if (max_leaf < 1) return;

    cpuid(1, 0, &eax, &ebx, &ecx, &edx);

    g_caps_data.has_sse   = (edx & CPUID_1_EDX_SSE)   ? 1 : 0;
    g_caps_data.has_sse2  = (edx & CPUID_1_EDX_SSE2)  ? 1 : 0;
    g_caps_data.has_sse3  = (ecx & CPUID_1_ECX_SSE3)  ? 1 : 0;
    g_caps_data.has_ssse3 = (ecx & CPUID_1_ECX_SSSE3) ? 1 : 0;
    g_caps_data.has_sse41 = (ecx & CPUID_1_ECX_SSE41) ? 1 : 0;
    g_caps_data.has_sse42 = (ecx & CPUID_1_ECX_SSE42) ? 1 : 0;
    g_caps_data.has_fma   = (ecx & CPUID_1_ECX_FMA)   ? 1 : 0;
    g_caps_data.has_xsave = (ecx & CPUID_1_ECX_XSAVE) ? 1 : 0;
    g_caps_data.has_avx   = (ecx & CPUID_1_ECX_AVX)   ? 1 : 0;

    if (!g_caps_data.has_xsave || !g_caps_data.has_avx) {
        uart_puts("[dsimd] no XSAVE/AVX\n");
        return;
    }

    u64 cr4 = read_cr4();
    cr4 |= (1ull << 18);
    cr4 |= (1ull << 10);
    write_cr4(cr4);

    cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    g_caps_data.has_osxsave = (ecx & CPUID_1_ECX_OSXSAVE) ? 1 : 0;

    if (!g_caps_data.has_osxsave) return;

    g_caps_data.xcr0_before = (u32)xgetbv(0);

    if (max_leaf >= 7) {
        cpuid(7, 0, &eax, &ebx, &ecx, &edx);

        g_caps_data.has_bmi1       = (ebx & CPUID_7_EBX_BMI1)       ? 1 : 0;
        g_caps_data.has_bmi2       = (ebx & CPUID_7_EBX_BMI2)       ? 1 : 0;
        g_caps_data.has_avx2       = (ebx & CPUID_7_EBX_AVX2)       ? 1 : 0;
        g_caps_data.has_avx512f    = (ebx & CPUID_7_EBX_AVX512F)    ? 1 : 0;
        g_caps_data.has_avx512dq   = (ebx & CPUID_7_EBX_AVX512DQ)   ? 1 : 0;
        g_caps_data.has_avx512ifma = (ebx & CPUID_7_EBX_AVX512IFMA) ? 1 : 0;
        g_caps_data.has_avx512cd   = (ebx & CPUID_7_EBX_AVX512CD)   ? 1 : 0;
        g_caps_data.has_avx512bw   = (ebx & CPUID_7_EBX_AVX512BW)   ? 1 : 0;
        g_caps_data.has_avx512vl   = (ebx & CPUID_7_EBX_AVX512VL)   ? 1 : 0;
        g_caps_data.has_avx512vbmi = (ecx & CPUID_7_ECX_AVX512VBMI) ? 1 : 0;
        g_caps_data.has_avx512vnni = (ecx & CPUID_7_ECX_AVX512VNNI) ? 1 : 0;
    }

    u64 xcr0 = XCR0_X87 | XCR0_SSE;

    if (g_caps_data.has_avx2) {
        xcr0 |= XCR0_AVX;
        g_caps_bits |= 1;
    }

    if (g_caps_data.has_avx512f) {
        u32 ca, cb, cc, cd;
        cpuid(0x0D, 0, &ca, &cb, &cc, &cd);
        u64 supported = ((u64)cd << 32) | ca;

        u64 want = XCR0_OPMASK | XCR0_ZMM_HI256 | XCR0_HI16_ZMM;

        if ((supported & want) == want) {
            xcr0 |= want;
            g_caps_bits |= 2;
            if (g_caps_data.has_avx512dq)   g_caps_bits |= 4;
            if (g_caps_data.has_avx512bw)   g_caps_bits |= 8;
            if (g_caps_data.has_avx512vl)   g_caps_bits |= 16;
            if (g_caps_data.has_avx512cd)   g_caps_bits |= 32;
            if (g_caps_data.has_avx512ifma) g_caps_bits |= 64;
            if (g_caps_data.has_avx512vbmi) g_caps_bits |= 128;
            if (g_caps_data.has_avx512vnni) g_caps_bits |= 256;
        }
    }

    xsetbv(0, xcr0);

    g_caps_data.xcr0_after = (u32)xgetbv(0);
    g_caps_data.avx2_enabled =
        (g_caps_data.xcr0_after & XCR0_AVX) ? 1 : 0;
    g_caps_data.avx512_enabled =
        ((g_caps_data.xcr0_after &
          (XCR0_OPMASK | XCR0_ZMM_HI256 | XCR0_HI16_ZMM)) ==
         (XCR0_OPMASK | XCR0_ZMM_HI256 | XCR0_HI16_ZMM)) ? 1 : 0;

    uart_puts("[dsimd] init done\n");
}

static dstatus_t api_get_caps(dsimd_caps_t *out) {
    if (!out) return DEPRESSURE_NULL_DATA;
    *out = g_caps_data;
    return DEPRESSURE_STATUS_OK;
}

static dstatus_t api_get_xcr0(u32 *out) {
    if (!out) return DEPRESSURE_NULL_DATA;
    if (!g_caps_data.has_osxsave) return DEPRESSURE_NOT_SUPPORTED;
    *out = (u32)xgetbv(0);
    return DEPRESSURE_STATUS_OK;
}

static dstatus_t api_test_avx2(void) {
    if (!g_caps_data.avx2_enabled) return DEPRESSURE_NOT_SUPPORTED;
    __asm__ volatile (
        ".byte 0xC5, 0xFD, 0xEF, 0xC0\n"
        ".byte 0xC5, 0xF8, 0x77\n"
    );
    return DEPRESSURE_STATUS_OK;
}

static dstatus_t api_test_avx512(void) {
    if (!g_caps_data.avx512_enabled) return DEPRESSURE_NOT_SUPPORTED;
    __asm__ volatile (
        ".byte 0x62, 0xF1, 0x7D, 0x48, 0xEF, 0xC0\n"
        ".byte 0xC5, 0xF8, 0x77\n"
    );
    g_caps_data.avx512_verified = 1;
    return DEPRESSURE_STATUS_OK;
}

static dstatus_t api_type_size(dsimd_type_t type, u64 *out_bytes) {
    if (!out_bytes) return DEPRESSURE_NULL_DATA;
    switch (type) {
        case DSIMD_TYPE_I8:  case DSIMD_TYPE_U8:  *out_bytes = 1; return DEPRESSURE_STATUS_OK;
        case DSIMD_TYPE_I16: case DSIMD_TYPE_U16: *out_bytes = 2; return DEPRESSURE_STATUS_OK;
        case DSIMD_TYPE_F32: case DSIMD_TYPE_I32: case DSIMD_TYPE_U32: *out_bytes = 4; return DEPRESSURE_STATUS_OK;
        case DSIMD_TYPE_F64: case DSIMD_TYPE_I64: case DSIMD_TYPE_U64: *out_bytes = 8; return DEPRESSURE_STATUS_OK;
        default: return DEPRESSURE_INCOMPATIBLE_TYPE;
    }
}

static dstatus_t api_type_name(dsimd_type_t type, const char **out_name) {
    if (!out_name) return DEPRESSURE_NULL_DATA;
    switch (type) {
        case DSIMD_TYPE_F32: *out_name = "f32"; return DEPRESSURE_STATUS_OK;
        case DSIMD_TYPE_F64: *out_name = "f64"; return DEPRESSURE_STATUS_OK;
        case DSIMD_TYPE_I8:  *out_name = "i8";  return DEPRESSURE_STATUS_OK;
        case DSIMD_TYPE_I16: *out_name = "i16"; return DEPRESSURE_STATUS_OK;
        case DSIMD_TYPE_I32: *out_name = "i32"; return DEPRESSURE_STATUS_OK;
        case DSIMD_TYPE_I64: *out_name = "i64"; return DEPRESSURE_STATUS_OK;
        case DSIMD_TYPE_U8:  *out_name = "u8";  return DEPRESSURE_STATUS_OK;
        case DSIMD_TYPE_U16: *out_name = "u16"; return DEPRESSURE_STATUS_OK;
        case DSIMD_TYPE_U32: *out_name = "u32"; return DEPRESSURE_STATUS_OK;
        case DSIMD_TYPE_U64: *out_name = "u64"; return DEPRESSURE_STATUS_OK;
        default: *out_name = "unknown"; return DEPRESSURE_INCOMPATIBLE_TYPE;
    }
}

static dstatus_t api_buffer_check(const dsimd_buffer_t *buf) {
    if (!buf) return DEPRESSURE_NULL_DATA;
    if (!buf->data) return DEPRESSURE_INVALID_POINTER;

    u64 elem = 0;
    dstatus_t s = api_type_size(buf->type, &elem);
    if (DSTATUS_IS_ERR(s)) return s;

    if (buf->count == 0 || elem == 0) return DEPRESSURE_TOO_LONG_DATA;
    if (buf->count > (U64_MAX / elem)) return DEPRESSURE_TOO_LONG_DATA;
    if (buf->bytes != buf->count * elem) return DEPRESSURE_TOO_LONG_DATA;

    if (buf->alignment != 0 && buf->alignment != 16 &&
        buf->alignment != 32 && buf->alignment != 64)
        return DEPRESSURE_ALIGNMENT_ERROR;

    return DEPRESSURE_STATUS_OK;
}

static dstatus_t api_v256_add_u64(const dsimd_v256_t *a,
                                  const dsimd_v256_t *b,
                                  dsimd_v256_t *out) {
    if (!a || !b || !out) return DEPRESSURE_NULL_DATA;
    if (!g_caps_data.avx2_enabled) return DEPRESSURE_NOT_SUPPORTED;

    __asm__ volatile (
        "vmovdqu (%1), %%ymm1\n"
        "vpaddq  (%2), %%ymm1, %%ymm0\n"
        "vmovdqu %%ymm0, (%0)\n"
        "vzeroupper\n"
        :
        : "r"(out), "r"(a), "r"(b)
        : "ymm0", "ymm1", "memory"
    );

    return DEPRESSURE_STATUS_OK;
}

static dstatus_t api_v256_xor(const dsimd_v256_t *a,
                              const dsimd_v256_t *b,
                              dsimd_v256_t *out) {
    if (!a || !b || !out) return DEPRESSURE_NULL_DATA;
    if (!g_caps_data.avx2_enabled) return DEPRESSURE_NOT_SUPPORTED;

    __asm__ volatile (
        "vmovdqu (%1), %%ymm1\n"
        "vpxor   (%2), %%ymm1, %%ymm0\n"
        "vmovdqu %%ymm0, (%0)\n"
        "vzeroupper\n"
        :
        : "r"(out), "r"(a), "r"(b)
        : "ymm0", "ymm1", "memory"
    );

    return DEPRESSURE_STATUS_OK;
}

static dstatus_t api_v256_zero(dsimd_v256_t *out) {
    if (!out) return DEPRESSURE_NULL_DATA;
    if (!g_caps_data.avx2_enabled) return DEPRESSURE_NOT_SUPPORTED;

    __asm__ volatile (
        "vpxor %%ymm0, %%ymm0, %%ymm0\n"
        "vmovdqu %%ymm0, (%0)\n"
        "vzeroupper\n"
        :
        : "r"(out)
        : "ymm0", "memory"
    );

    return DEPRESSURE_STATUS_OK;
}

static dstatus_t api_v256_sum_u64(const dsimd_v256_t *a, u64 *out_sum) {
    if (!a || !out_sum) return DEPRESSURE_NULL_DATA;
    if (!g_caps_data.avx2_enabled) return DEPRESSURE_NOT_SUPPORTED;

    u64 tmp[4];
    __asm__ volatile (
        "vmovdqu (%1), %%ymm0\n"
        "vextracti128 $1, %%ymm0, %%xmm1\n"
        "vpaddq %%xmm1, %%xmm0, %%xmm0\n"
        "vpshufd $0x4E, %%xmm0, %%xmm1\n"
        "vpaddq %%xmm1, %%xmm0, %%xmm0\n"
        "vmovq %%xmm0, (%0)\n"
        "vzeroupper\n"
        :
        : "r"(tmp), "r"(a)
        : "ymm0", "ymm1", "memory"
    );
    *out_sum = tmp[0];
    return DEPRESSURE_STATUS_OK;
}

static dstatus_t api_v512_add_u64(const dsimd_v512_t *a,
                                  const dsimd_v512_t *b,
                                  dsimd_v512_t *out) {
    if (!a || !b || !out) return DEPRESSURE_NULL_DATA;
    if (!g_caps_data.avx512_enabled) return DEPRESSURE_NOT_SUPPORTED;

    __asm__ volatile (
        "vmovdqu64 (%1), %%zmm1\n"
        "vpaddq    (%2), %%zmm1, %%zmm0\n"
        "vmovdqu64 %%zmm0, (%0)\n"
        "vzeroupper\n"
        :
        : "r"(out), "r"(a), "r"(b)
        : "zmm0", "zmm1", "memory"
    );

    return DEPRESSURE_STATUS_OK;
}

static dstatus_t api_v512_xor(const dsimd_v512_t *a,
                              const dsimd_v512_t *b,
                              dsimd_v512_t *out) {
    if (!a || !b || !out) return DEPRESSURE_NULL_DATA;
    if (!g_caps_data.avx512_enabled) return DEPRESSURE_NOT_SUPPORTED;

    __asm__ volatile (
        "vmovdqu64 (%1), %%zmm1\n"
        "vpxord    (%2), %%zmm1, %%zmm0\n"
        "vmovdqu64 %%zmm0, (%0)\n"
        "vzeroupper\n"
        :
        : "r"(out), "r"(a), "r"(b)
        : "zmm0", "zmm1", "memory"
    );

    return DEPRESSURE_STATUS_OK;
}

static dstatus_t api_v512_zero(dsimd_v512_t *out) {
    if (!out) return DEPRESSURE_NULL_DATA;
    if (!g_caps_data.avx512_enabled) return DEPRESSURE_NOT_SUPPORTED;

    __asm__ volatile (
        "vpxord %%zmm0, %%zmm0, %%zmm0\n"
        "vmovdqu64 %%zmm0, (%0)\n"
        "vzeroupper\n"
        :
        : "r"(out)
        : "zmm0", "memory"
    );

    return DEPRESSURE_STATUS_OK;
}

static dstatus_t api_v512_sum_u64(const dsimd_v512_t *a, u64 *out_sum) {
    if (!a || !out_sum) return DEPRESSURE_NULL_DATA;
    if (!g_caps_data.avx512_enabled) return DEPRESSURE_NOT_SUPPORTED;

    u64 tmp[8];
    __asm__ volatile (
        "vmovdqu64 (%1), %%zmm0\n"
        "vextracti64x4 $1, %%zmm0, %%ymm1\n"
        "vpaddq %%ymm1, %%ymm0, %%ymm0\n"
        "vextracti128 $1, %%ymm0, %%xmm1\n"
        "vpaddq %%xmm1, %%xmm0, %%xmm0\n"
        "vpshufd $0x4E, %%xmm0, %%xmm1\n"
        "vpaddq %%xmm1, %%xmm0, %%xmm0\n"
        "vmovq %%xmm0, (%0)\n"
        "vzeroupper\n"
        :
        : "r"(tmp), "r"(a)
        : "zmm0", "zmm1", "ymm0", "xmm0", "xmm1", "memory"
    );
    *out_sum = tmp[0];
    return DEPRESSURE_STATUS_OK;
}

static dsimd_api_t g_simd_api;

static ddriver_t simd_driver = {
    .pattern = DDRIVER_PATTERN,
    .version = DDRIVER_VERSION,
    .next    = 0,
    .type    = DDRIVER_TYPE_OTHER,
    .name    = "dsimd",
    .entry   = 0,
    .api     = 0,
    .data    = 0,
};

static void dsimd_test_api(void) {
    uart_puts("[dsimd] === API test ===\n");

    {
        dsimd_caps_t caps;
        dstatus_t s;

        s = g_simd_api.get_caps(&caps);
        uart_puts("[test] get_caps(&caps)     = ");
        uart_put_hex64(s);
        uart_puts(" (expect 0x0)\n");

        s = g_simd_api.get_caps(0);
        uart_puts("[test] get_caps(0)         = ");
        uart_put_hex64(s);
        uart_puts(" (expect 0x3)\n");
    }

    {
        u32 xcr0 = 0;
        dstatus_t s;

        s = g_simd_api.get_xcr0(&xcr0);
        uart_puts("[test] get_xcr0(&xcr0)     = ");
        uart_put_hex64(s);
        uart_puts(" xcr0=");
        uart_put_hex32(xcr0);
        uart_puts("\n");

        s = g_simd_api.get_xcr0(0);
        uart_puts("[test] get_xcr0(0)         = ");
        uart_put_hex64(s);
        uart_puts(" (expect 0x3)\n");
    }

    {
        u64 bytes;
        dstatus_t s;

        s = g_simd_api.type_size(DSIMD_TYPE_U64, &bytes);
        uart_puts("[test] type_size(u64)      = ");
        uart_put_hex64(s);
        uart_puts(" bytes=");
        uart_put_hex64(bytes);
        uart_puts("\n");

        s = g_simd_api.type_size(DSIMD_TYPE_UNKNOWN, &bytes);
        uart_puts("[test] type_size(UNKNOWN)  = ");
        uart_put_hex64(s);
        uart_puts(" (expect 0x2)\n");

        s = g_simd_api.type_size(DSIMD_TYPE_U64, 0);
        uart_puts("[test] type_size(NULL)     = ");
        uart_put_hex64(s);
        uart_puts(" (expect 0x3)\n");
    }

    {
        const char *name = 0;
        dstatus_t s;

        s = g_simd_api.type_name(DSIMD_TYPE_U32, &name);
        uart_puts("[test] type_name(u32)      = ");
        uart_put_hex64(s);
        uart_puts(" name=");
        if (name) uart_puts(name);
        uart_puts("\n");

        s = g_simd_api.type_name(DSIMD_TYPE_UNKNOWN, &name);
        uart_puts("[test] type_name(UNKNOWN)  = ");
        uart_put_hex64(s);
        uart_puts(" (expect 0x2)\n");

        s = g_simd_api.type_name(DSIMD_TYPE_U32, 0);
        uart_puts("[test] type_name(NULL)     = ");
        uart_put_hex64(s);
        uart_puts(" (expect 0x3)\n");
    }

    {
        u64 arr[16];
        dsimd_buffer_t buf;
        dstatus_t s;

        buf.data = arr;
        buf.bytes = 16 * 8;
        buf.count = 16;
        buf.type = DSIMD_TYPE_U64;
        buf.alignment = 64;
        s = g_simd_api.buffer_check(&buf);
        uart_puts("[test] buffer_check(OK)    = ");
        uart_put_hex64(s);
        uart_puts(" (expect 0x0)\n");

        s = g_simd_api.buffer_check(0);
        uart_puts("[test] buffer_check(NULL)  = ");
        uart_put_hex64(s);
        uart_puts(" (expect 0x3)\n");

        buf.data = 0;
        s = g_simd_api.buffer_check(&buf);
        uart_puts("[test] buffer_check(data=0)= ");
        uart_put_hex64(s);
        uart_puts(" (expect 0x4)\n");

        buf.data = arr;
        buf.type = DSIMD_TYPE_UNKNOWN;
        s = g_simd_api.buffer_check(&buf);
        uart_puts("[test] buffer_check(type?) = ");
        uart_put_hex64(s);
        uart_puts(" (expect 0x2)\n");

        buf.type = DSIMD_TYPE_U64;
        buf.count = 0;
        s = g_simd_api.buffer_check(&buf);
        uart_puts("[test] buffer_check(cnt=0) = ");
        uart_put_hex64(s);
        uart_puts(" (expect 0x1)\n");

        buf.count = 16;
        buf.bytes = 42;
        s = g_simd_api.buffer_check(&buf);
        uart_puts("[test] buffer_check(bytes?) = ");
        uart_put_hex64(s);
        uart_puts(" (expect 0x1)\n");

        buf.bytes = 128;
        buf.alignment = 7;
        s = g_simd_api.buffer_check(&buf);
        uart_puts("[test] buffer_check(align?)= ");
        uart_put_hex64(s);
        uart_puts(" (expect 0x16)\n");
    }

    {
        dsimd_v256_t a = {{1, 2, 3, 4}};
        dsimd_v256_t b = {{10, 20, 30, 40}};
        dsimd_v256_t c = {{0, 0, 0, 0}};
        u64 sum = 0;
        dstatus_t s;

        s = g_simd_api.v256_zero(&c);
        uart_puts("[test] v256_zero           = ");
        uart_put_hex64(s);
        uart_puts("\n");

        s = g_simd_api.v256_add_u64(&a, &b, &c);
        uart_puts("[test] v256_add_u64        = ");
        uart_put_hex64(s);
        uart_puts(" c0=");
        uart_put_hex64(c.q[0]);
        uart_puts(" c3=");
        uart_put_hex64(c.q[3]);
        uart_puts("\n");

        s = g_simd_api.v256_xor(&a, &b, &c);
        uart_puts("[test] v256_xor            = ");
        uart_put_hex64(s);
        uart_puts("\n");

        s = g_simd_api.v256_sum_u64(&a, &sum);
        uart_puts("[test] v256_sum_u64        = ");
        uart_put_hex64(s);
        uart_puts(" sum=");
        uart_put_hex64(sum);
        uart_puts(" (expect 0xA)\n");

        s = g_simd_api.v256_add_u64(0, &b, &c);
        uart_puts("[test] v256_add(0)         = ");
        uart_put_hex64(s);
        uart_puts(" (expect 0x3)\n");

        s = g_simd_api.v256_add_u64(&a, &b, 0);
        uart_puts("[test] v256_add(out=0)     = ");
        uart_put_hex64(s);
        uart_puts(" (expect 0x3)\n");
    }

    {
        dsimd_v512_t a = {{1, 2, 3, 4, 5, 6, 7, 8}};
        dsimd_v512_t b = {{10, 20, 30, 40, 50, 60, 70, 80}};
        dsimd_v512_t c;
        u64 sum = 0;
        dstatus_t s;

        s = g_simd_api.v512_zero(&c);
        uart_puts("[test] v512_zero           = ");
        uart_put_hex64(s);
        uart_puts("\n");

        s = g_simd_api.v512_add_u64(&a, &b, &c);
        uart_puts("[test] v512_add_u64        = ");
        uart_put_hex64(s);
        uart_puts(" c0=");
        uart_put_hex64(c.q[0]);
        uart_puts(" c7=");
        uart_put_hex64(c.q[7]);
        uart_puts("\n");

        s = g_simd_api.v512_xor(&a, &b, &c);
        uart_puts("[test] v512_xor            = ");
        uart_put_hex64(s);
        uart_puts("\n");

        s = g_simd_api.v512_sum_u64(&a, &sum);
        uart_puts("[test] v512_sum_u64        = ");
        uart_put_hex64(s);
        uart_puts(" sum=");
        uart_put_hex64(sum);
        uart_puts(" (expect 0x24)\n");

        s = g_simd_api.v512_add_u64(0, &b, &c);
        uart_puts("[test] v512_add(0)         = ");
        uart_put_hex64(s);
        uart_puts(" (expect 0x3)\n");

        s = g_simd_api.v512_add_u64(&a, 0, &c);
        uart_puts("[test] v512_add(b=0)       = ");
        uart_put_hex64(s);
        uart_puts(" (expect 0x3)\n");
    }

    {
        dstatus_t s;
        s = g_simd_api.test_avx2();
        uart_puts("[test] test_avx2           = ");
        uart_put_hex64(s);
        uart_puts(s == DEPRESSURE_NOT_SUPPORTED ? " (NOT_SUPPORTED)\n" : " (OK)\n");

        s = g_simd_api.test_avx512();
        uart_puts("[test] test_avx512         = ");
        uart_put_hex64(s);
        uart_puts(s == DEPRESSURE_NOT_SUPPORTED ? " (NOT_SUPPORTED)\n" : " (OK)\n");
    }

    uart_puts("[dsimd] === API test done ===\n");
}

void dsimd_main(void *api, void *fb_info) {
    (void)fb_info;

    dsimd_enable();

    g_simd_api.version      = 0x00010000;
    g_simd_api._pad         = 0;
    g_simd_api.get_caps     = api_get_caps;
    g_simd_api.get_xcr0     = api_get_xcr0;
    g_simd_api.test_avx2    = api_test_avx2;
    g_simd_api.test_avx512  = api_test_avx512;
    g_simd_api.type_size    = api_type_size;
    g_simd_api.type_name    = api_type_name;
    g_simd_api.buffer_check = api_buffer_check;

    g_simd_api.v256_add_u64 = api_v256_add_u64;
    g_simd_api.v256_xor     = api_v256_xor;
    g_simd_api.v256_zero    = api_v256_zero;
    g_simd_api.v256_sum_u64 = api_v256_sum_u64;

    g_simd_api.v512_add_u64 = api_v512_add_u64;
    g_simd_api.v512_xor     = api_v512_xor;
    g_simd_api.v512_zero    = api_v512_zero;
    g_simd_api.v512_sum_u64 = api_v512_sum_u64;

    if (api && ((dkernel_api_t *)api)->register_driver) {
        simd_driver.api = (u64)&g_simd_api;
        ((dkernel_api_t *)api)->register_driver(&simd_driver);
        uart_puts("[dsimd] registered as 'dsimd'\n");
    }

    dsimd_test_api();
}