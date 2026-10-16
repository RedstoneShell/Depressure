; SPDX-License-Identifier: GPL-3.0-or-later
; Copyright (c) 2026 RedstoneShell
;
; This file is part of the Depressure Operating System.

BITS 64
DEFAULT REL

section .text
global efi_main

print_hex:
    push rbp
    mov  rbp, rsp
    push rbx
    push r12
    push r13
    push r14
    push r15
    sub  rsp, 0x38

    mov  r12, rax

    lea  r14, [rel hex_buf]
    mov  word [r14], 0x0030
    mov  word [r14+2], 0x0078
    add  r14, 4

    mov  rax, r12
    mov  rcx, 16
.hex_iter:
    rol  rax, 4
    mov  rdx, rax
    and  rdx, 0x0F
    cmp  rdx, 10
    jl   .hex_digit
    add  rdx, 'A' - 10
    jmp  .hex_store
.hex_digit:
    add  rdx, '0'
.hex_store:
    mov  word [r14], dx
    add  r14, 2
    dec  rcx
    jnz  .hex_iter

    mov  word [r14], 0x000D
    mov  word [r14+2], 0x000A
    mov  word [r14+4], 0x0000

    mov  rcx, [rel g_con_out]
    test rcx, rcx
    jz   .hex_done
    lea  rdx, [rel hex_buf]
    mov  rax, [rcx + 8]
    call rax

.hex_done:
    add  rsp, 0x38
    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  rbx
    pop  rbp
    ret

print_mode_dump:
    push rbp
    mov  rbp, rsp
    push rbx
    push r12
    push r13
    push r14
    push r15
    sub  rsp, 0x38

    mov  r14, [rel gop_mode_ptr]
    test r14, r14
    jz   .done

    xor  r12, r12
.dump_loop:
    mov  rcx, [rel g_con_out]
    test rcx, rcx
    jz   .done
    lea  rdx, [rel log_dump_label]
    mov  rax, [rcx + 8]
    call rax

    mov  rax, [r14 + r12*8]
    call print_hex

    inc  r12
    cmp  r12, 8
    jl   .dump_loop

.done:
    add  rsp, 0x38
    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  rbx
    pop  rbp
    ret

efi_main:
    push rbp
    mov  rbp, rsp
    push rbx
    push r12
    push r13
    push r14
    push r15
    sub  rsp, 0x48

    mov  r12, rcx
    mov  [rel g_st], rdx
    mov  rbx, rdx

    mov  rax, [rbx + 0x40]
    mov  [rel g_con_out], rax
    test rax, rax
    jz   .done

    mov  rax, [rbx + 0x60]
    mov  [rel g_bs], rax

    mov  rcx, [rel g_con_out]
    lea  rdx, [rel log_entry]
    mov  rax, [rcx + 8]
    call rax

    mov  rcx, [rel g_con_out]
    test rcx, rcx
    jz   .done

    lea  rdx, [rel hello_msg]
    mov  rax, [rcx + 8]
    call rax

    mov  r13, [rel g_bs]
    test r13, r13
    jz   .no_bs

    mov  rcx, [rel g_con_out]
    lea  rdx, [rel log_bs_ptr]
    mov  rax, [rcx + 8]
    call rax
    mov  rax, r13
    call print_hex

    mov  rcx, [rel g_con_out]
    lea  rdx, [rel log_bs_sig]
    mov  rax, [rcx + 8]
    call rax
    mov  rax, [r13]
    call print_hex

    mov  rdi, 0x200100
    mov  rax, [rel g_st]
    mov  [rdi + 0x00], rax
    mov  rax, [rel g_bs]
    mov  [rdi + 0x08], rax
    mov  rax, [rel g_con_out]
    mov  [rdi + 0x10], rax
    mov  [rdi + 0x18], r12

    mov  rcx, [rel g_con_out]
    lea  rdx, [rel log_gop_call]
    mov  rax, [rcx + 8]
    call rax

    lea  rcx, [rel GopGuid]
    xor  rdx, rdx
    lea  r8,  [rel gop_ptr]
    mov  rax, [r13 + 0x140]
    call rax

    mov  r14, rax
    mov  rcx, [rel g_con_out]
    lea  rdx, [rel log_gop_status]
    mov  rax, [rcx + 8]
    call rax
    mov  rax, r14
    call print_hex
    test r14, r14
    jnz  .fail_gop

    mov  rcx, [rel g_con_out]
    lea  rdx, [rel log_gop_ptr]
    mov  rax, [rcx + 8]
    call rax
    mov  rax, [rel gop_ptr]
    call print_hex

    mov  rcx, [rel gop_ptr]
    mov  rcx, [rcx + 0x18]
    mov  [rel gop_mode_ptr], rcx

    mov  rcx, [rel g_con_out]
    lea  rdx, [rel log_mode_ptr]
    mov  rax, [rcx + 8]
    call rax
    mov  rax, [rel gop_mode_ptr]
    call print_hex

    mov  rcx, [rel g_con_out]
    lea  rdx, [rel log_mode_dump]
    mov  rax, [rcx + 8]
    call rax

    call print_mode_dump

    mov  rcx, [rel gop_mode_ptr]

    mov  eax, dword [rcx + 0x00]
    mov  [rel m_max], rax

    mov  eax, dword [rcx + 0x04]
    mov  [rel m_cur], rax

    mov  rax, [rcx + 0x08]
    mov  [rel gop_info_ptr], rax

    mov  rax, [rcx + 0x10]
    mov  [rel m_sizeofinfo], rax

    mov  rax, [rcx + 0x18]
    mov  [rel fb_base], rax

    mov  rax, [rcx + 0x20]
    mov  [rel fb_size], rax

    mov  rcx, [rel gop_info_ptr]
    test rcx, rcx
    jz   .skip_info

    mov  eax, dword [rcx + 0x00]
    mov  [rel i_version], rax
    mov  eax, dword [rcx + 0x04]
    mov  [rel fb_width], rax
    mov  eax, dword [rcx + 0x08]
    mov  [rel fb_height], rax
    mov  eax, dword [rcx + 0x0C]
    mov  [rel fb_format], rax
    mov  eax, dword [rcx + 0x20]
    mov  [rel fb_pitch], rax

    mov  rcx, [rel g_con_out]
    lea  rdx, [rel log_fb_w]
    mov  rax, [rcx + 8]
    call rax
    mov  rax, [rel fb_width]
    call print_hex

    mov  rcx, [rel g_con_out]
    lea  rdx, [rel log_fb_h]
    mov  rax, [rcx + 8]
    call rax
    mov  rax, [rel fb_height]
    call print_hex

    mov  rcx, [rel g_con_out]
    lea  rdx, [rel log_fb_fmt]
    mov  rax, [rcx + 8]
    call rax
    mov  rax, [rel fb_format]
    call print_hex

    mov  rcx, [rel g_con_out]
    lea  rdx, [rel log_fb_base]
    mov  rax, [rcx + 8]
    call rax
    mov  rax, [rel fb_base]
    call print_hex

.skip_info:

    mov  rdi, 0x200000

    mov  rax, [rel fb_base]
    mov  [rdi + 0x00], rax
    mov  rax, [rel fb_width]
    mov  [rdi + 0x08], rax
    mov  rax, [rel fb_height]
    mov  [rdi + 0x10], rax
    mov  rax, [rel fb_pitch]
    mov  [rdi + 0x18], rax
    mov  rax, [rel fb_format]
    mov  [rdi + 0x20], rax
    mov  rax, [rel fb_size]
    mov  [rdi + 0x28], rax

    mov  rcx, [rel g_con_out]
    lea  rdx, [rel log_calling_lp]
    mov  rax, [rcx + 8]
    call rax

    lea  rcx, [rel SimpleFsGuid]
    xor  rdx, rdx
    lea  r8,  [rel file_system]
    mov  r13, [rel g_bs]
    mov  rax, [r13 + 0x140]
    call rax

    mov  r14, rax
    mov  rcx, [rel g_con_out]
    lea  rdx, [rel log_lp_status]
    mov  rax, [rcx + 8]
    call rax
    mov  rax, r14
    call print_hex
    test r14, r14
    jnz  .fail_lp

    mov  rcx, [rel g_con_out]
    lea  rdx, [rel log_ov_call]
    mov  rax, [rcx + 8]
    call rax

    mov  rcx, [rel file_system]
    lea  rdx, [rel root_dir]
    mov  rax, [rcx + 8]
    call rax

    mov  r14, rax
    mov  rcx, [rel g_con_out]
    lea  rdx, [rel log_ov_status]
    mov  rax, [rcx + 8]
    call rax
    mov  rax, r14
    call print_hex
    test r14, r14
    jnz  .fail_ov

    mov  rcx, [rel g_con_out]
    lea  rdx, [rel log_open_call]
    mov  rax, [rcx + 8]
    call rax

    mov  rcx, [rel root_dir]
    lea  rdx, [rel file_handle]
    lea  r8,  [rel elf_path]
    mov  r9,  1
    mov  qword [rsp + 0x20], 0
    mov  rax, [rcx + 8]
    call rax

    mov  r14, rax
    mov  rcx, [rel g_con_out]
    lea  rdx, [rel log_open_status]
    mov  rax, [rcx + 8]
    call rax
    mov  rax, r14
    call print_hex
    test r14, r14
    jnz  .fail_open

    mov  rcx, [rel g_con_out]
    lea  rdx, [rel log_getinfo_call]
    mov  rax, [rcx + 8]
    call rax

    mov  qword [rel info_size], 4096
    mov  rcx, [rel file_handle]
    lea  rdx, [rel FileInfoGuid]
    lea  r8,  [rel info_size]
    lea  r9,  [rel info_buf]
    mov  rax, [rcx + 0x40]
    call rax

    mov  r14, rax
    mov  rcx, [rel g_con_out]
    lea  rdx, [rel log_getinfo_status]
    mov  rax, [rcx + 8]
    call rax
    mov  rax, r14
    call print_hex
    test r14, r14
    jnz  .fail_getinfo

    mov  rax, [rel info_buf + 0x08]
    mov  [rel read_size], rax

    mov  rcx, [rel g_con_out]
    lea  rdx, [rel log_read_size]
    mov  rax, [rcx + 8]
    call rax
    mov  rax, [rel read_size]
    call print_hex

    mov  rcx, [rel g_con_out]
    lea  rdx, [rel log_read_call]
    mov  rax, [rcx + 8]
    call rax

    mov  rcx, [rel file_handle]
    lea  rdx, [rel read_size]
    lea  r8,  [rel elf_buffer]
    mov  rax, [rcx + 0x20]
    call rax

    mov  r14, rax
    mov  rcx, [rel g_con_out]
    lea  rdx, [rel log_read_status]
    mov  rax, [rcx + 8]
    call rax
    mov  rax, r14
    call print_hex
    test r14, r14
    jnz  .fail_read

    mov  rcx, [rel g_con_out]
    lea  rdx, [rel log_elf_magic]
    mov  rax, [rcx + 8]
    call rax
    mov  eax, dword [rel elf_buffer]
    call print_hex

    mov  r14, [rel elf_buffer + 0x20]
    lea  rax, [rel elf_buffer]
    add  r14, rax

    movzx r15d, word [rel elf_buffer + 0x38]
    test  r15d, r15d
    jz   .no_segments

.segment_loop:
    cmp  dword [r14 + 0x00], 1
    jne  .next_segment

    mov  rsi, [r14 + 0x20]
    test rsi, rsi
    jz   .skip_copy

    lea  rsi, [rel elf_buffer]
    add  rsi, [r14 + 0x08]
    mov  rdi, [r14 + 0x10]
    mov  rcx, [r14 + 0x20]

    push rbx
    push r13
    push r14
    push r15
    cld
    rep movsb
    pop  r15
    pop  r14
    pop  r13
    pop  rbx

.skip_copy:
    mov  rax, [r14 + 0x28]
    sub  rax, [r14 + 0x20]
    test rax, rax
    jz   .skip_zero

    mov  rdi, [r14 + 0x10]
    add  rdi, [r14 + 0x20]
    mov  rcx, rax
    xor  eax, eax

    push rbx
    push r13
    push r14
    push r15
    cld
    rep stosb
    pop  r15
    pop  r14
    pop  r13
    pop  rbx

.skip_zero:
.next_segment:
    movzx eax, word [rel elf_buffer + 0x36]
    add  r14, rax
    dec  r15d
    jnz  .segment_loop

.no_segments:
    mov  rcx, [rel g_con_out]
    lea  rdx, [rel log_all_done]
    mov  rax, [rcx + 8]
    call rax

    mov  rcx, [rel g_con_out]
    lea  rdx, [rel log_jumping]
    mov  rax, [rcx + 8]
    call rax

    mov  rax, [rel elf_buffer + 0x18]
    jmp  rax

.fail_gop:
    mov  rcx, [rel g_con_out]
    lea  rdx, [rel log_fail_gop]
    mov  rax, [rcx + 8]
    call rax
    jmp  .done

.no_bs:
    mov  rcx, [rel g_con_out]
    lea  rdx, [rel log_no_bs]
    mov  rax, [rcx + 8]
    call rax
    jmp  .done

.fail_lp:
    mov  rcx, [rel g_con_out]
    lea  rdx, [rel log_fail_lp]
    mov  rax, [rcx + 8]
    call rax
    jmp  .done

.fail_ov:
    mov  rcx, [rel g_con_out]
    lea  rdx, [rel log_fail_ov]
    mov  rax, [rcx + 8]
    call rax
    jmp  .done

.fail_open:
    mov  rcx, [rel g_con_out]
    lea  rdx, [rel log_fail_open]
    mov  rax, [rcx + 8]
    call rax
    jmp  .done

.fail_getinfo:
    mov  rcx, [rel g_con_out]
    lea  rdx, [rel log_fail_getinfo]
    mov  rax, [rcx + 8]
    call rax
    jmp  .done

.fail_read:
    mov  rcx, [rel g_con_out]
    lea  rdx, [rel log_fail_read]
    mov  rax, [rcx + 8]
    call rax
    jmp  .done

.done:
    mov  rcx, [rel g_bs]
    test rcx, rcx
    jz   .exit
    mov  rax, [rcx + 0xF8]
    mov  rcx, 8000000
    call rax

.exit:
    xor  eax, eax
    add  rsp, 0x48
    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  rbx
    pop  rbp
    ret

section .data
align 8
g_st:            dq 0
g_bs:            dq 0
g_con_out:       dq 0
file_system:     dq 0
root_dir:        dq 0
file_handle:     dq 0
info_size:       dq 0
read_size:       dq 0
hex_buf:         times 48 dw 0

gop_ptr:         dq 0
gop_mode_ptr:    dq 0
gop_info_ptr:    dq 0
fb_base:         dq 0
fb_size:         dq 0
fb_width:        dq 0
fb_height:       dq 0
fb_pitch:        dq 0
fb_format:       dq 0
m_max:           dq 0
m_cur:           dq 0
m_sizeofinfo:    dq 0
i_version:       dq 0

align 4
SimpleFsGuid:
    dd 0x964E5B22
    dw 0x6459
    dw 0x11D2
    db 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B

align 4
FileInfoGuid:
    dd 0x09576E92
    dw 0x6D3F
    dw 0x11D2
    db 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B

align 4
GopGuid:
    dd 0x9042A9DE
    dw 0x23DC
    dw 0x4A38
    db 0x96, 0xFB, 0x7A, 0xDE, 0xD0, 0x80, 0x51, 0x6A

align 8
elf_path:
    dw '\', 'E', 'F', 'I', '\', 'B', 'O', 'O', 'T', '\'
    dw 'D', 'E', 'P', 'R', 'E', 'S', 'S', 'U', 'R', 'E', '.', 'E', 'L', 'F'
    dw 0x0000

hello_msg:
    dw 'B','o','o','t','i','n','g',' ','D','e','p','r','e','s','s','u'
    dw 'r','e',' ','(','B','u','i','l','d',' ','1',')','.','.','.'
    dw 0x000D, 0x000A, 0x0000

log_entry:          dw '[',']',' ','e','n','t','e','r',13,10,0
log_bs_ptr:         dw '[',']',' ','B','S',' ','p','t','r',':',' ',0
log_bs_sig:         dw '[',']',' ','B','S',' ','s','i','g',':',' ',0
log_calling_lp:     dw '[',']',' ','c','a','l','l','i','n','g',' ','L','P',13,10,0
log_gop_call:       dw '[',']',' ','G','O','P',' ','c','a','l','l',13,10,0
log_gop_status:     dw '[',']',' ','G','O','P',' ','s','t','a','t','u','s',':',' ',0
log_gop_ptr:        dw '[',']',' ','G','O','P',' ','p','t','r',':',' ',0
log_mode_ptr:       dw '[',']',' ','M','o','d','e',' ','p','t','r',':',' ',0
log_mode_dump:      dw '[',']',' ','M','o','d','e',' ','d','u','m','p',':',13,10,0
log_dump_label:     dw ' ',13,10,0
log_maxmode:        dw '[',']',' ','M','a','x','M','o','d','e',':',' ',0
log_mode:           dw '[',']',' ','M','o','d','e',':',' ',0
log_info_ptr:       dw '[',']',' ','I','n','f','o',' ','p','t','r',':',' ',0
log_sizeofinfo:     dw '[',']',' ','S','i','z','e','O','f','I','n','f','o',':',' ',0
log_iversion:       dw '[',']',' ','I','n','f','o','.','V','e','r','s','i','o','n',':',' ',0
log_fb_base:        dw '[',']',' ','f','b','_','b','a','s','e',':',' ',0
log_fb_size:        dw '[',']',' ','f','b','_','s','i','z','e',':',' ',0
log_fb_w:           dw '[',']',' ','f','b','_','w','i','d','t','h',':',' ',0
log_fb_h:           dw '[',']',' ','f','b','_','h','e','i','g','h','t',':',' ',0
log_fb_pitch:       dw '[',']',' ','f','b','_','p','i','t','c','h',':',' ',0
log_fb_fmt:         dw '[',']',' ','f','b','_','f','o','r','m','a','t',':',' ',0
log_fb_written:     dw '[',']',' ','f','b',' ','i','n','f','o',' ','w','r','i','t','t','e','n',13,10,0
log_ov_call:        dw '[',']',' ','c','a','l','l','i','n','g',' ','O','p','e','n','V','o','l','u','m','e',13,10,0
log_open_call:      dw '[',']',' ','c','a','l','l','i','n','g',' ','O','p','e','n',13,10,0
log_all_done:       dw '[',']',' ','A','L','L',' ','D','O','N','E',13,10,0
log_stall:          dw '[',']',' ','S','t','a','l','l',' ','2','s',13,10,0
log_ebs_call:       dw '[',']',' ','c','a','l','l','i','n','g',' ','E','x','i','t','B','o','o','t','S','e','r','v','i','c','e','s',13,10,0
log_jumping:        dw '[',']',' ','J','U','M','P','I','N','G',' ','T','O',' ','K','E','R','N','E','L',13,10,0
log_fail_gop:       dw '[','!',']',' ','F','A','I','L',' ','G','O','P',13,10,0
log_no_bs:          dw '[','!',']',' ','N','O',' ','B','S',13,10,0
log_fail_lp:        dw '[','!',']',' ','F','A','I','L',' ','L','P',13,10,0
log_fail_ov:        dw '[','!',']',' ','F','A','I','L',' ','O','V',13,10,0
log_fail_open:      dw '[','!',']',' ','F','A','I','L',' ','O','p','e','n',13,10,0
log_fail_getinfo:   dw '[','!',']',' ','F','A','I','L',' ','G','e','t','I','n','f','o',13,10,0
log_fail_read:      dw '[','!',']',' ','F','A','I','L',' ','R','e','a','d',13,10,0
log_lp_status:      dw '[',']',' ','L','P',' ','s','t',':',' ',0
log_ov_status:      dw '[',']',' ','O','V',' ','s','t',':',' ',0
log_open_status:    dw '[',']',' ','O','p','e','n',' ','s','t',':',' ',0
log_getinfo_call:   dw '[',']',' ','G','e','t','I','n','f','o',13,10,0
log_getinfo_status: dw '[',']',' ','G','I',' ','s','t',':',' ',0
log_read_size:      dw '[',']',' ','S','i','z','e',':',' ',0
log_read_call:      dw '[',']',' ','R','e','a','d',13,10,0
log_read_status:    dw '[',']',' ','R','d',' ','s','t',':',' ',0
log_elf_magic:      dw '[',']',' ','E','L','F',' ','m','a','g','i','c',':',' ',0

section .bss
align 16
info_buf:    resb 4096
elf_buffer:  resb 0x400000
mmap_buf:    resb 0x20000