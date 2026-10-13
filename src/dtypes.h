// SPDX-License-Identifier: LicenseRef-Depressure-BSL-1.1
// Copyright (c) 2026 RedstoneShell
//
// Change Date: October 11, 2158
// Change License: GPL-3.0-or-later
// Additional Use Grant: Compilation of third-party software using these headers is permitted without restrictions.

#ifndef DTYPES_H
#define DTYPES_H
typedef unsigned char       u8;
typedef unsigned short      u16;
typedef unsigned int        u32;
typedef unsigned long long  u64;
typedef signed char         i8;
typedef signed short        i16;
typedef signed int          i32;
typedef signed long long    i64;
typedef unsigned long long  usize;
typedef signed long long    isize;
typedef unsigned long long  uptr;
typedef void                dvoid;
#define U8_MAX   ((u8)0xFF)
#define U16_MAX  ((u16)0xFFFF)
#define U32_MAX  ((u32)0xFFFFFFFF)
#define U64_MAX  ((u64)0xFFFFFFFFFFFFFFFFULL)
#define I8_MAX   ((i8)0x7F)
#define I16_MAX  ((i16)0x7FFF)
#define I32_MAX  ((i32)0x7FFFFFFF)
#define I64_MAX  ((i64)0x7FFFFFFFFFFFFFFFLL)
#define NULL   ((void *)0)
#define TRUE   1
#define FALSE  0

#endif /* DTYPES_H */