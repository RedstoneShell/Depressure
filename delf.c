#include "delf.h"

#define ELF_MAGIC_0   0x7F
#define ELF_MAGIC_1   'E'
#define ELF_MAGIC_2   'L'
#define ELF_MAGIC_3   'F'

#define ELFCLASS64    2
#define ELFDATA2LSB   1
#define ET_EXEC       2
#define ET_DYN        3
#define ELF_SHT_SYMTAB 2
#define ELF_SHN_UNDEF  0
#define ELF_SHN_ABS    0xFFF1
#define I32_MIN_VALUE  (-2147483647 - 1)
#define I32_MAX_VALUE  2147483647

#define RAM_LOW_LIMIT          0x100000ULL
#define RAM_HIGH_LIMIT         0x80000000ULL
#define KERNEL_RESERVED_START  0x1000000ULL
#define KERNEL_RESERVED_END    0x2000000ULL

int delf_check(const void *buf) {
    const u8 *p = (const u8 *)buf;

    return p &&
           p[0] == ELF_MAGIC_0 &&
           p[1] == ELF_MAGIC_1 &&
           p[2] == ELF_MAGIC_2 &&
           p[3] == ELF_MAGIC_3 &&
           p[4] == ELFCLASS64 &&
           p[5] == ELFDATA2LSB;
}

elf64_header_t *delf_header(void *buf) {
    return (elf64_header_t *)buf;
}

static int u64_add_ok(u64 a, u64 b, u64 *out) {
    if (a + b < a) return 0;
    *out = a + b;
    return 1;
}

static int u64_mul_ok(u64 a, u64 b, u64 *out) {
    if (a != 0 && b > U64_MAX / a) return 0;
    *out = a * b;
    return 1;
}

static int add_signed_ok(u64 value, i64 addend, u64 *out) {
    if (addend >= 0) return u64_add_ok(value, (u64)addend, out);

    u64 magnitude = (u64)(-(addend + 1)) + 1;
    if (value < magnitude) return 0;
    *out = value - magnitude;
    return 1;
}

static int i64_add_ok(i64 a, i64 b, i64 *out) {
    if ((b > 0 && a > I64_MAX - b) ||
        (b < 0 && a < (-I64_MAX - 1) - b)) return 0;
    *out = a + b;
    return 1;
}

static int i64_sub_ok(i64 a, i64 b, i64 *out) {
    if ((b < 0 && a > I64_MAX + b) ||
        (b > 0 && a < (-I64_MAX - 1) + b)) return 0;
    *out = a - b;
    return 1;
}

static int range_ok(u64 addr, u64 size) {
    u64 end;

    if (size == 0) return 1;
    if (addr < RAM_LOW_LIMIT ||
        !u64_add_ok(addr, size, &end) ||
        end > RAM_HIGH_LIMIT) return 0;

    return end <= KERNEL_RESERVED_START || addr >= KERNEL_RESERVED_END;
}

static int delf_valid_header(const elf64_header_t *eh, u64 size) {
    u64 table_size;
    u64 table_end;

    if (eh->e_ehsize != sizeof(elf64_header_t) ||
        eh->e_machine != 62 ||
        eh->e_version != 1 ||
        eh->e_phentsize != sizeof(elf64_phdr_t) ||
        eh->e_phnum == 0 ||
        eh->e_phnum > 64) return 0;

    if (!u64_mul_ok(eh->e_phnum, eh->e_phentsize, &table_size) ||
        !u64_add_ok(eh->e_phoff, table_size, &table_end) ||
        table_end > size) return 0;

    if (eh->e_shnum == 0) return 1;
    if (eh->e_shentsize != sizeof(elf64_shdr_t) ||
        eh->e_shnum > 256) return 0;

    if (!u64_mul_ok(eh->e_shnum, eh->e_shentsize, &table_size) ||
        !u64_add_ok(eh->e_shoff, table_size, &table_end) ||
        table_end > size) return 0;

    return 1;
}

static int delf_image_bounds(const elf64_header_t *eh, const u8 *base,
                             u64 size, u64 *min_vaddr, u64 *max_end) {
    const elf64_phdr_t *ph =
        (const elf64_phdr_t *)(base + eh->e_phoff);
    u64 min = U64_MAX;
    u64 max = 0;
    u32 load_count = 0;

    for (u16 i = 0; i < eh->e_phnum; i++) {
        u64 file_end;
        u64 mem_end;

        if (ph[i].p_type != ELF_PT_LOAD || ph[i].p_memsz == 0) continue;
        if (ph[i].p_filesz > ph[i].p_memsz ||
            !u64_add_ok(ph[i].p_offset, ph[i].p_filesz, &file_end) ||
            file_end > size ||
            !u64_add_ok(ph[i].p_vaddr, ph[i].p_memsz, &mem_end)) return 0;

        if (ph[i].p_align > 1 &&
            ((ph[i].p_align & (ph[i].p_align - 1)) != 0 ||
             (ph[i].p_vaddr % ph[i].p_align) !=
             (ph[i].p_offset % ph[i].p_align))) return 0;

        if (ph[i].p_vaddr < min) min = ph[i].p_vaddr;
        if (mem_end > max) max = mem_end;
        load_count++;
    }

    if (load_count == 0 || max <= min) return 0;
    *min_vaddr = min;
    *max_end = max;
    return 1;
}

u64 delf_image_span(void *buf, u64 size) {
    if (!buf || size < sizeof(elf64_header_t) || !delf_check(buf)) return 0;

    elf64_header_t *eh = (elf64_header_t *)buf;
    u64 min_vaddr;
    u64 max_end;

    if ((eh->e_type != ET_EXEC && eh->e_type != ET_DYN) ||
        !delf_valid_header(eh, size) ||
        !delf_image_bounds(eh, (const u8 *)buf, size,
                           &min_vaddr, &max_end)) return 0;

    return max_end - min_vaddr;
}

static int delf_virtual_range_loaded(const elf64_header_t *eh,
                                     const u8 *base, u64 vaddr, u64 width) {
    const elf64_phdr_t *ph =
        (const elf64_phdr_t *)(base + eh->e_phoff);
    u64 end;

    if (!u64_add_ok(vaddr, width, &end)) return 0;

    for (u16 i = 0; i < eh->e_phnum; i++) {
        u64 segment_end;

        if (ph[i].p_type != ELF_PT_LOAD || ph[i].p_memsz == 0 ||
            !u64_add_ok(ph[i].p_vaddr, ph[i].p_memsz, &segment_end)) continue;
        if (vaddr >= ph[i].p_vaddr && end <= segment_end) return 1;
    }

    return 0;
}

static int delf_symbol_value(const elf64_header_t *eh, const u8 *base,
                             u64 elf_size, u16 symtab_index, u32 symbol_index,
                             u64 actual_base, u64 min_vaddr, u64 *value) {
    const elf64_shdr_t *sections =
        (const elf64_shdr_t *)(base + eh->e_shoff);
    const elf64_shdr_t *symtab;
    const elf64_sym_t *symbols;
    u64 offset;
    u64 end;
    const elf64_sym_t *symbol;

    if (symbol_index == 0) {
        *value = 0;
        return 1;
    }

    if (symtab_index >= eh->e_shnum) return 0;
    symtab = &sections[symtab_index];
    if ((symtab->sh_type != ELF_SHT_SYMTAB &&
         symtab->sh_type != ELF_SHT_DYNSYM) ||
        symtab->sh_entsize != sizeof(elf64_sym_t) ||
        symtab->sh_size % sizeof(elf64_sym_t) != 0 ||
        symbol_index >= symtab->sh_size / sizeof(elf64_sym_t) ||
        !u64_mul_ok(symbol_index, sizeof(elf64_sym_t), &offset) ||
        !u64_add_ok(symtab->sh_offset, offset, &end) ||
        !u64_add_ok(end, sizeof(elf64_sym_t), &end) ||
        end > elf_size) return 0;

    u64 symtab_end;
    if (!u64_add_ok(symtab->sh_offset, symtab->sh_size, &symtab_end) ||
        end > symtab_end) return 0;

    symbols = (const elf64_sym_t *)(base + symtab->sh_offset);
    symbol = &symbols[symbol_index];

    if (symbol->st_shndx == ELF_SHN_UNDEF) {
        *value = 0;
        return 1;
    }

    if (symbol->st_shndx == ELF_SHN_ABS) {
        *value = symbol->st_value;
        return 1;
    }

    if (symbol->st_value < min_vaddr) {
        *value = 0;
        return 1;
    }

    return u64_add_ok(actual_base, symbol->st_value - min_vaddr, value);
}

static int delf_apply_relocations(const elf64_header_t *eh, const u8 *base,
                                  u64 elf_size, u64 actual_base,
                                  u64 min_vaddr) {
    const elf64_shdr_t *sections;
    u64 load_bias;

    if (eh->e_shoff == 0 || eh->e_shnum == 0) return 1;
    if (actual_base < min_vaddr) return 0;
    load_bias = actual_base - min_vaddr;
    sections = (const elf64_shdr_t *)(base + eh->e_shoff);

    for (u16 section_index = 0; section_index < eh->e_shnum;
         section_index++) {
        const elf64_shdr_t *rela_section = &sections[section_index];
        const elf64_rela_t *relocations;
        u64 section_end;
        u64 count;

        if (rela_section->sh_type != ELF_SHT_RELA ||
            rela_section->sh_size == 0) continue;
        if (rela_section->sh_entsize != sizeof(elf64_rela_t) ||
            rela_section->sh_size % sizeof(elf64_rela_t) != 0 ||
            !u64_add_ok(rela_section->sh_offset, rela_section->sh_size,
                        &section_end) ||
            section_end > elf_size) return 0;

        count = rela_section->sh_size / sizeof(elf64_rela_t);
        relocations =
            (const elf64_rela_t *)(base + rela_section->sh_offset);

        for (u64 i = 0; i < count; i++) {
            u32 type = (u32)relocations[i].r_info;
            u32 symbol_index = (u32)(relocations[i].r_info >> 32);
            u64 target_address;
            u64 symbol = 0;
            u64 value;
            u64 width;

            if (type == R_X86_64_NONE) continue;
            width = (type == R_X86_64_PC32 ||
                     type == R_X86_64_PLT32 ||
                     type == R_X86_64_32 ||
                     type == R_X86_64_32S) ? sizeof(u32) : sizeof(u64);

            if (relocations[i].r_offset < min_vaddr ||
                !delf_virtual_range_loaded(eh, base,
                                           relocations[i].r_offset, width) ||
                !u64_add_ok(actual_base,
                            relocations[i].r_offset - min_vaddr,
                            &target_address) ||
                !range_ok(target_address, width)) return 0;

            if (type != R_X86_64_RELATIVE &&
                !delf_symbol_value(eh, base, elf_size, rela_section->sh_link,
                                   symbol_index, actual_base, min_vaddr,
                                   &symbol)) return 0;

            switch (type) {
                case R_X86_64_RELATIVE:
                    if (!add_signed_ok(load_bias, relocations[i].r_addend,
                                       &value))
                        return 0;
                    *(u64 *)target_address = value;
                    break;

                case R_X86_64_64:
                    if (!add_signed_ok(symbol, relocations[i].r_addend,
                                       &value)) return 0;
                    *(u64 *)target_address = value;
                    break;

                case R_X86_64_GLOB_DAT:
                case R_X86_64_JUMP_SLOT:
                    *(u64 *)target_address = symbol;
                    break;

                case R_X86_64_PC32:
                case R_X86_64_PLT32: {
                    i64 result;
                    if (symbol > I64_MAX || target_address > I64_MAX ||
                        !i64_add_ok((i64)symbol, relocations[i].r_addend,
                                    &result) ||
                        !i64_sub_ok(result, (i64)target_address, &result) ||
                        result < I32_MIN_VALUE || result > I32_MAX_VALUE)
                        return 0;
                    *(i32 *)target_address = (i32)result;
                    break;
                }

                case R_X86_64_32:
                    if (!add_signed_ok(symbol, relocations[i].r_addend,
                                       &value) ||
                        value > U32_MAX) return 0;
                    *(u32 *)target_address = (u32)value;
                    break;

                case R_X86_64_32S: {
                    i64 result;
                    if (symbol > I64_MAX ||
                        !i64_add_ok((i64)symbol, relocations[i].r_addend,
                                    &result) ||
                        result < I32_MIN_VALUE || result > I32_MAX_VALUE)
                        return 0;
                    *(i32 *)target_address = (i32)result;
                    break;
                }

                default:
                    return 0;
            }
        }
    }

    return 1;
}

u64 delf_load_at(void *buf, u64 size, u64 load_base) {
    elf64_header_t *eh;
    const elf64_phdr_t *ph;
    u8 *base;
    u64 min_vaddr;
    u64 max_end;
    u64 actual_base;

    if (!buf || size < sizeof(elf64_header_t) || !delf_check(buf)) return 0;
    eh = (elf64_header_t *)buf;

    if ((eh->e_type != ET_EXEC && eh->e_type != ET_DYN) ||
        !delf_valid_header(eh, size) ||
        !delf_image_bounds(eh, (const u8 *)buf, size,
                           &min_vaddr, &max_end) ||
        eh->e_entry < min_vaddr || eh->e_entry >= max_end) return 0;

    base = (u8 *)buf;
    ph = (const elf64_phdr_t *)(base + eh->e_phoff);
    actual_base = load_base ? load_base : min_vaddr;

    for (u16 i = 0; i < eh->e_phnum; i++) {
        u64 offset;
        u64 dst_addr;

        if (ph[i].p_type != ELF_PT_LOAD || ph[i].p_memsz == 0) continue;
        if (ph[i].p_vaddr < min_vaddr) return 0;
        offset = ph[i].p_vaddr - min_vaddr;

        if (!u64_add_ok(actual_base, offset, &dst_addr) ||
            !range_ok(dst_addr, ph[i].p_memsz)) return 0;

        volatile u8 *dst = (volatile u8 *)dst_addr;
        const u8 *src = base + ph[i].p_offset;

        for (u64 j = 0; j < ph[i].p_filesz; j++) dst[j] = src[j];
        for (u64 j = ph[i].p_filesz; j < ph[i].p_memsz; j++) dst[j] = 0;
    }

    if (!delf_apply_relocations(eh, base, size, actual_base, min_vaddr))
        return 0;

    u64 entry;
    if (!u64_add_ok(actual_base, eh->e_entry - min_vaddr, &entry) ||
        !range_ok(entry, 1) ||
        !delf_virtual_range_loaded(eh, base, eh->e_entry, 1)) return 0;

    return entry;
}

u64 delf_load(void *buf, u64 size) {
    return delf_load_at(buf, size, 0);
}