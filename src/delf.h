// SPDX-License-Identifier: LicenseRef-Depressure-BSL-1.1
// Copyright (c) 2026 RedstoneShell
//
// Change Date: October 11, 2158
// Change License: GPL-3.0-or-later
// Additional Use Grant: Compilation of third-party software using these headers is permitted without restrictions.

#ifndef DELF_H
#define DELF_H

#include "dtypes.h"

/* ELF64 header */
typedef struct {
    u8  e_ident[16];
    u16 e_type;
    u16 e_machine;
    u32 e_version;
    u64 e_entry;
    u64 e_phoff;
    u64 e_shoff;
    u32 e_flags;
    u16 e_ehsize;
    u16 e_phentsize;
    u16 e_phnum;
    u16 e_shentsize;
    u16 e_shnum;
    u16 e_shstrndx;
} elf64_header_t;

/* ELF64 program header */
typedef struct {
    u32 p_type;
    u32 p_flags;
    u64 p_offset;
    u64 p_vaddr;
    u64 p_paddr;
    u64 p_filesz;
    u64 p_memsz;
    u64 p_align;
} elf64_phdr_t;

/* ELF64 section header */
typedef struct {
    u32 sh_name;
    u32 sh_type;
    u64 sh_flags;
    u64 sh_addr;
    u64 sh_offset;
    u64 sh_size;
    u32 sh_link;
    u32 sh_info;
    u64 sh_addralign;
    u64 sh_entsize;
} elf64_shdr_t;

/* ELF64 symbol table entry */
typedef struct {
    u32 st_name;
    u8  st_info;
    u8  st_other;
    u16 st_shndx;
    u64 st_value;
    u64 st_size;
} elf64_sym_t;

/* ELF64 relocation entry */
typedef struct {
    u64 r_offset;
    u64 r_info;
    i64 r_addend;
} elf64_rela_t;

/* Section types */
#define ELF_SHT_NULL      0
#define ELF_SHT_PROGBITS  1
#define ELF_SHT_SYMTAB    2
#define ELF_SHT_STRTAB    3
#define ELF_SHT_RELA      4
#define ELF_SHT_NOBITS    8
#define ELF_SHT_DYNSYM    11

/* Program header types */
#define ELF_PT_LOAD    1
#define ELF_PT_DYNAMIC 2

/* x86_64 relocation types */
#define R_X86_64_NONE        0
#define R_X86_64_64          1
#define R_X86_64_PC32        2
#define R_X86_64_PLT32       4
#define R_X86_64_GLOB_DAT    6
#define R_X86_64_JUMP_SLOT   7
#define R_X86_64_RELATIVE    8
#define R_X86_64_32          10
#define R_X86_64_32S         11

int delf_check(const void *buf);

elf64_header_t *delf_header(void *buf);

u64 delf_image_span(void *buf, u64 size);

u64 delf_load_at(void *buf, u64 size, u64 load_base);

u64 delf_load(void *buf, u64 size);

#endif /* DELF_H */