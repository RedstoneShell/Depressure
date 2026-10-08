BITS 64
DEFAULT REL

section .data
align 8
simple_fs_guid:
    dd 0x964E5B22
    dw 0x6459
    dw 0x11D2
    db 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B

align 8
file_info_guid:
    dd 0x09576E92
    dw 0x6D3F
    dw 0x11D2
    db 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B
align 8
block_io_guid:
    dd 0x964E5B21
    dw 0x6459
    dw 0x11d2
    db 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b

align 8
acpi_20_table_guid:
    db 0x71, 0xe8, 0x68, 0x88, 0xf1, 0xe4, 0xd3, 0x11
    db 0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81

align 8
acpi_10_table_guid:
    db 0x30, 0x2d, 0x9d, 0xeb, 0x88, 0x2d, 0xd3, 0x11
    db 0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d

section .bss
align 16
fs_ptr:      resq 1
root_ptr:    resq 1
file_ptr:    resq 1
dir_ptr:     resq 1
file_size:   resq 1
info_buf:    resb 1024
name_buf:    resb 512

section .text

global uefi_get_system_table
uefi_get_system_table:
    test rdi, rdi
    jz .gst_invalid

    mov rax, [abs 0x200100]
    test rax, rax
    jz .gst_not_found

    mov [rdi], rax
    xor eax, eax
    ret

.gst_invalid:
    mov eax, 2                 ; EFI_INVALID_PARAMETER
    ret

.gst_not_found:
    mov eax, 14                ; EFI_NOT_FOUND
    ret

global uefi_get_configuration_table
uefi_get_configuration_table:
    test rdi, rdi
    jz .gct_invalid
    test rsi, rsi
    jz .gct_invalid

    push rbx
    push r12
    push r13
    push r14
    push r15

    mov r13, rdi               ; requested GUID
    mov r12, rsi               ; output pointer
    mov qword [r12], 0

    mov rbx, [abs 0x200100]    ; EFI_SYSTEM_TABLE
    test rbx, rbx
    jz .gct_not_found

    mov r15, [rbx + 0x68]      ; NumberOfTableEntries
    mov r14, [rbx + 0x70]      ; ConfigurationTable
    test r14, r14
    jz .gct_not_found

    cmp r15, 256
    ja .gct_invalid

.gct_loop:
    test r15, r15
    jz .gct_not_found

    mov rax, [r14]
    cmp rax, [r13]
    jne .gct_next
    mov rax, [r14 + 8]
    cmp rax, [r13 + 8]
    jne .gct_next

    mov rax, [r14 + 16]
    test rax, rax
    jz .gct_not_found
    mov [r12], rax
    xor eax, eax
    jmp .gct_done

.gct_next:
    add r14, 24
    dec r15
    jmp .gct_loop

.gct_invalid:
    mov eax, 2                 ; EFI_INVALID_PARAMETER
    jmp .gct_done

.gct_not_found:
    mov eax, 14                ; EFI_NOT_FOUND

.gct_done:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    ret

global uefi_get_acpi_rsdp
uefi_get_acpi_rsdp:
    test rdi, rdi
    jz .garsdp_invalid

    push rbx
    push r12
    sub rsp, 8
    mov r12, rdi
    mov qword [r12], 0

    lea rdi, [rel acpi_20_table_guid]
    mov rsi, rsp
    call uefi_get_configuration_table
    test rax, rax
    jz .garsdp_done

    lea rdi, [rel acpi_10_table_guid]
    mov rsi, rsp
    call uefi_get_configuration_table
    test rax, rax
    jnz .garsdp_done

.garsdp_done:
    test rax, rax
    jnz .garsdp_restore
    mov rbx, [rsp]
    test rbx, rbx
    jz .garsdp_restore
    mov [r12], rbx

.garsdp_restore:
    add rsp, 8
    pop r12
    pop rbx
    ret

.garsdp_invalid:
    mov eax, 2                 ; EFI_INVALID_PARAMETER
    ret

global uefi_load_file
uefi_load_file:
    push rbp
    mov  rbp, rsp
    push rbx
    push r12
    push r13
    push r14
    push r15
    sub  rsp, 0x40

    mov  r12, rdi
    mov  r13, rsi
    mov  r14, rdx

    mov  rbx, [abs 0x200108]
    test rbx, rbx
    jz   .lf_fail

    ; LocateProtocol
    lea  rcx, [rel simple_fs_guid]
    xor  rdx, rdx
    lea  r8,  [rel fs_ptr]
    mov  rax, [rbx + 0x140]
    call rax
    test rax, rax
    jnz  .lf_fail

    ; OpenVolume
    mov  rcx, [rel fs_ptr]
    lea  rdx, [rel root_ptr]
    mov  rax, [rcx + 0x08]
    call rax
    test rax, rax
    jnz  .lf_fail

    ; Open
    mov  rcx, [rel root_ptr]
    lea  rdx, [rel file_ptr]
    mov  r8,  r12
    mov  r9,  1
    mov  qword [rsp + 0x20], 0
    mov  rax, [rcx + 0x08]
    call rax
    test rax, rax
    jnz  .lf_fail

    ; GetInfo
    mov  qword [rel file_size], 1024
    mov  rcx, [rel file_ptr]
    lea  rdx, [rel file_info_guid]
    lea  r8,  [rel file_size]
    lea  r9,  [rel info_buf]
    mov  rax, [rcx + 0x40]
    call rax
    test rax, rax
    jnz  .lf_fail_close

    mov  rax, [rel info_buf + 0x08]
    mov  [rel file_size], rax
    cmp  rax, r14
    ja   .lf_fail_close

    ; Read
    mov  rcx, [rel file_ptr]
    lea  rdx, [rel file_size]
    mov  r8,  r13
    mov  rax, [rcx + 0x20]
    call rax
    test rax, rax
    jnz  .lf_fail_close

    ; Close
    mov  rcx, [rel file_ptr]
    mov  rax, [rcx + 0x10]
    call rax

    mov  rax, [rel file_size]
    jmp  .lf_done

.lf_fail_close:
    mov  rcx, [rel file_ptr]
    test rcx, rcx
    jz   .lf_fail
    mov  rax, [rcx + 0x10]
    call rax

.lf_fail:
    xor  rax, rax

.lf_done:
    add  rsp, 0x40
    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  rbx
    pop  rbp
    ret

%define DIR_ENTRY_SIZE  0x200

global uefi_list_dir
uefi_list_dir:
    push rbp
    mov  rbp, rsp
    push rbx
    push r12
    push r13
    push r14
    push r15
    sub  rsp, 0x40

    mov  r12, rdi
    mov  r13, rsi
    mov  r14, rdx
    xor  r15, r15

    mov  rbx, [abs 0x200108]
    test rbx, rbx
    jz   .ld_fail

    ; LocateProtocol
    lea  rcx, [rel simple_fs_guid]
    xor  rdx, rdx
    lea  r8,  [rel fs_ptr]
    mov  rax, [rbx + 0x140]
    call rax
    test rax, rax
    jnz  .ld_fail

    ; OpenVolume
    mov  rcx, [rel fs_ptr]
    lea  rdx, [rel root_ptr]
    mov  rax, [rcx + 0x08]
    call rax
    test rax, rax
    jnz  .ld_fail

    ; Open dir
    mov  rcx, [rel root_ptr]
    lea  rdx, [rel dir_ptr]
    mov  r8,  r12
    mov  r9,  1
    mov  qword [rsp + 0x20], 0
    mov  rax, [rcx + 0x08]
    call rax
    test rax, rax
    jnz  .ld_fail

.ld_loop:
    cmp  r15, r14
    jae  .ld_close

    mov  qword [rel file_size], 1024
    mov  rcx, [rel dir_ptr]
    lea  rdx, [rel file_size]
    lea  r8,  [rel info_buf]
    mov  rax, [rcx + 0x20]
    call rax
    test rax, rax
    jnz  .ld_close

    mov  rax, [rel file_size]
    test rax, rax
    jz   .ld_close

    mov  rax, [rel info_buf + 0x48]
    test rax, 0x10
    jnz  .ld_loop

    ; rsi = info_buf + 0x50
    lea  rsi, [rel info_buf + 0x50]
    xor  rcx, rcx
.ld_len:
    mov  ax, [rsi + rcx*2]
    test ax, ax
    jz   .ld_len_done
    inc  rcx
    cmp  rcx, 200
    ja   .ld_loop
    jmp  .ld_len
.ld_len_done:
    cmp  rcx, 4
    jb   .ld_loop

    ; rdx = &name[rcx-4]
    lea  rdx, [rsi + rcx*2 - 8]
    mov  eax, [rdx]
    cmp  eax, 0x0065002E          ; '.', 'e'
    jne  .ld_loop
    mov  eax, [rdx + 4]
    cmp  eax, 0x0066006C          ; 'l', 'f'
    jne  .ld_loop

    ; rax = &entry
    mov  rax, r15
    shl  rax, 9
    add  rax, r13

    ; entry->file_size
    mov  rdx, [rel info_buf + 0x08]
    mov  [rax + 0x00], rdx

    ; entry->name_len
    mov  [rax + 0x08], rcx

    lea  rdi, [rax + 0x10]
    inc  rcx
    rep movsw

    inc  r15
    jmp  .ld_loop

.ld_close:
    mov  rcx, [rel dir_ptr]
    test rcx, rcx
    jz   .ld_done
    mov  rax, [rcx + 0x10]
    call rax

.ld_done:
    mov  rax, r15
    add  rsp, 0x40
    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  rbx
    pop  rbp
    ret

.ld_fail:
    xor  rax, rax
    jmp  .ld_done

global uefi_stall
uefi_stall:
    push rbp
    mov  rbp, rsp
    push rbx
    sub  rsp, 0x20

    mov  rbx, rdi

    ; BootServices 0x200108
    mov  rax, [abs 0x200108]
    test rax, rax
    jz   .st_done

    ; Stall(microseconds)
    mov  rcx, rbx              ; arg1 = microseconds
    mov  rax, [rax + 0xF8]     ; Stall
    call rax

.st_done:
    add  rsp, 0x20
    pop  rbx
    pop  rbp
    ret

global uefi_create_timer_event
uefi_create_timer_event:
    push rbp
    mov  rbp, rsp
    sub  rsp, 0x30

    ; CreateEvent(Type, NotifyTpl, NotifyFn, NotifyCtx, &Event)
    mov  rcx, 0x80000000        ; arg1 = EVT_TIMER (0x80000000)
    mov  rdx, 0x10              ; arg2 = TPL_CALLBACK
    xor  r8, r8                 ; arg3 = NotifyFunction = NULL
    xor  r9, r9                 ; arg4 = NotifyContext = NULL
    mov  [rsp + 0x20], rsi      ; arg5 = &out_event (shadow space + 5-й)
    mov  rax, [rdi + 0x50]      ; CreateEvent
    call rax

    add  rsp, 0x30
    pop  rbp
    ret

global uefi_set_timer_periodic
uefi_set_timer_periodic:
    push rbp
    mov  rbp, rsp
    sub  rsp, 0x20

    mov  r9, rdx                ; Save period
    mov  rcx, rsi               ; arg1 = event
    mov  rdx, 1                 ; arg2 = TimerPeriodic (1)
    mov  r8, r9                 ; arg3 = period (100ns units)
    mov  rax, [rdi + 0x58]      ; SetTimer
    call rax

    add  rsp, 0x20
    pop  rbp
    ret

global uefi_wait_for_event
uefi_wait_for_event:
    push rbp
    mov  rbp, rsp
    sub  rsp, 0x20

    mov  rcx, rsi               ; arg1 = NumberOfEvents
    mov  rax, [rdi + 0x60]      ; WaitForEvent
    call rax

    add  rsp, 0x20
    pop  rbp
    ret

global uefi_write_file
uefi_write_file:
    push rbp
    mov  rbp, rsp
    push rbx
    push r12
    push r13
    push r14
    push r15
    sub  rsp, 0x40

    mov  r12, rdi          ; path
    mov  r13, rsi          ; buf
    mov  r14, rdx          ; siz

    mov  rbx, [abs 0x200108]
    test rbx, rbx
    jz   .wf_fail

    ; LocateProtocol
    lea  rcx, [rel simple_fs_guid]
    xor  rdx, rdx
    lea  r8,  [rel fs_ptr]
    mov  rax, [rbx + 0x140]
    call rax
    test rax, rax
    jnz  .wf_fail

    ; OpenVolume
    mov  rcx, [rel fs_ptr]
    lea  rdx, [rel root_ptr]
    mov  rax, [rcx + 0x08]
    call rax
    test rax, rax
    jnz  .wf_fail

    ; Open with READ|WRITE|CREATE
    ; EFI_FILE_MODE_READ  = 0x01
    ; EFI_FILE_MODE_WRITE = 0x02
    ; EFI_FILE_MODE_CREATE = 0x8000000000000000
    mov  rcx, [rel root_ptr]
    lea  rdx, [rel file_ptr]
    mov  r8,  r12
    mov  r9,  0x8000000000000003   ; READ|WRITE|CREATE
    mov  qword [rsp + 0x20], 0
    mov  rax, [rcx + 0x08]
    call rax
    test rax, rax
    jnz  .wf_fail

    ; Write(file, &size, buf)
    mov  rcx, [rel file_ptr]
    lea  rdx, [rel file_size]
    mov  [rel file_size], r14
    mov  r8,  r13
    mov  rax, [rcx + 0x28]         ; Write offset = 0x28
    call rax
    test rax, rax
    jnz  .wf_fail_close

    ; Close
    mov  rcx, [rel file_ptr]
    mov  rax, [rcx + 0x10]
    call rax

    mov  rax, r14
    jmp  .wf_done

.wf_fail_close:
    mov  rcx, [rel file_ptr]
    test rcx, rcx
    jz   .wf_fail
    mov  rax, [rcx + 0x10]
    call rax

.wf_fail:
    xor  rax, rax

.wf_done:
    add  rsp, 0x40
    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  rbx
    pop  rbp
    ret

global uefi_get_total_ram
uefi_get_total_ram:
    push rbp
    mov  rbp, rsp
    push rbx
    push r12
    push r13
    push r14
    push r15
    sub  rsp, 0x60

    mov  rbx, rdi          ; rbx = BootServices
    test rbx, rbx
    jz   .ram_fail

    ; GetMemoryMap(&size, NULL, &map_key, &desc_size, &desc_ver)
    lea  r12, [rsp + 0x00]   ; &size
    mov  qword [r12], 0
    lea  r13, [rsp + 0x08]   ; &map_key
    lea  r14, [rsp + 0x10]   ; &desc_size
    lea  r15, [rsp + 0x18]   ; &desc_ver

    mov  rcx, r12            ; arg1 = &size
    xor  rdx, rdx            ; arg2 = NULL
    mov  r8,  r13            ; arg3 = &map_key
    mov  r9,  r14            ; arg4 = &desc_size
    mov  [rsp + 0x20], r15   ; arg5 = &desc_ver
    mov  rax, [rbx + 0x38]   ; GetMemoryMap
    call rax

    mov  r12, [rsp + 0x00]   ; size
    add  r12, 0x1000
    mov  r13, r12

    ; AllocatePool(EfiLoaderData, size, &buffer)
    mov  rcx, 2              ; EfiLoaderData = 2
    mov  rdx, r13            ; size
    lea  r8,  [rsp + 0x30]   ; &buffer
    mov  rax, [rbx + 0x40]   ; AllocatePool
    call rax

    test rax, rax
    jnz  .ram_fail

    mov  r14, [rsp + 0x30]   ; r14 = buffer

    lea  r12, [rsp + 0x00]   ; &size
    mov  [r12], r13          ; size
    lea  r13, [rsp + 0x08]   ; &map_key
    lea  r15, [rsp + 0x10]   ; &desc_size
    lea  rbx, [rsp + 0x18]   ; &desc_ver

    mov  rcx, r12            ; arg1 = &size
    mov  rdx, r14            ; arg2 = buffer
    mov  r8,  r13            ; arg3 = &map_key
    mov  r9,  r15            ; arg4 = &desc_size
    mov  [rsp + 0x20], rbx   ; arg5 = &desc_ver

    mov  rbx, [abs 0x200108]
    mov  rax, [rbx + 0x38]   ; GetMemoryMap
    call rax

    test rax, rax
    jnz  .ram_fail_free

    mov  r12, [rsp + 0x00]   ; size
    mov  r13, [rsp + 0x10]   ; desc_size
    mov  r14, [rsp + 0x30]   ; buffer
    xor  r15, r15            ; r15 = total_ram
    xor  rbx, rbx            ; rbx = offset

.ram_loop:
    cmp  rbx, r12
    jae  .ram_done

    lea  rax, [r14 + rbx]

    mov  ecx, [rax + 0x00]   ; Type

    ; Skip EfiReservedMemoryType (0) & EfiUnusableMemory (8)
    cmp  ecx, 0
    je   .ram_next
    cmp  ecx, 8
    je   .ram_next

    ; NumberOfPages @ offset 0x18
    mov  rdx, [rax + 0x18]
    shl  rdx, 12
    add  r15, rdx

.ram_next:
    add  rbx, r13
    jmp  .ram_loop

.ram_done:
    ; FreePool(buffer)
    mov  rcx, r14
    mov  rbx, [abs 0x200108]
    mov  rax, [rbx + 0x48]   ; FreePool
    call rax

    mov  rax, r15            ; ret total_ram
    jmp  .ram_exit

.ram_fail_free:
    mov  rcx, r14
    mov  rbx, [abs 0x200108]
    test rbx, rbx
    jz   .ram_fail
    mov  rax, [rbx + 0x48]
    call rax

.ram_fail:
    xor  rax, rax

.ram_exit:
    add  rsp, 0x60
    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  rbx
    pop  rbp
    ret

global uefi_locate_handle_blockio
uefi_locate_handle_blockio:
    push rbp
    mov  rbp, rsp
    sub  rsp, 0x30

    test rdi, rdi
    jz   .lh_invalid
    test rdx, rdx
    jz   .lh_invalid

    mov  rax, [rdi + 0xB0] ; LocateHandle
    test rax, rax
    jz   .lh_invalid
    mov  r9,  rdx          ; &size

    ; LocateHandle(ByProtocol, &BlockIoGuid, NULL, &size, buffer)
    mov  rcx, 2            ; ByProtocol = 2
    lea  rdx, [rel block_io_guid]
    xor  r8, r8            ; SearchKey = NULL
    mov  [rsp + 0x20], rsi ; buffer
    call rax

    leave
    ret

.lh_invalid:
    mov  rax, 0x8000000000000002 ; EFI_INVALID_PARAMETER
    leave
    ret

global uefi_handle_protocol_blockio
uefi_handle_protocol_blockio:
    push rbp
    mov  rbp, rsp
    sub  rsp, 0x20

    test rdi, rdi
    jz   .hp_invalid
    test rsi, rsi
    jz   .hp_invalid
    test rdx, rdx
    jz   .hp_invalid

    mov  rax, [rdi + 0x98] ; HandleProtocol
    test rax, rax
    jz   .hp_invalid

    mov  r8,  rdx          ; &Interface
    mov  rcx, rsi          ; Handle
    lea  rdx, [rel block_io_guid]
    call rax

    leave
    ret

.hp_invalid:
    mov  rax, 0x8000000000000002 ; EFI_INVALID_PARAMETER
    leave
    ret