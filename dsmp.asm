BITS 64
DEFAULT REL

section .data
align 8
mp_services_guid:
    dd 0x3fdda605
    dw 0xa76e
    dw 0x4f46
    db 0xad, 0x29, 0x12, 0xf4, 0x53, 0x1b, 0x3d, 0x08

section .bss
align 16
mp_protocol_ptr:    resq 1

section .text

; =====================================================
; uefi_locate_mp_services
; System V: RDI = BootServices
; Повертає: RAX = MP protocol pointer (або 0)
; =====================================================
global uefi_locate_mp_services
uefi_locate_mp_services:
    push rbp
    mov  rbp, rsp
    push rbx
    push r12
    sub  rsp, 0x28              ; 32 (shadow) + 8 (вирівнювання)

    test rdi, rdi
    jz   .loc_fail

    mov  rbx, rdi               ; rbx = BootServices

    ; LocateProtocol(&guid, NULL, &out)
    lea  rcx, [rel mp_services_guid]
    xor  rdx, rdx
    lea  r8,  [rel mp_protocol_ptr]
    mov  rax, [rbx + 0x140]     ; LocateProtocol
    call rax

    test rax, rax
    jnz  .loc_fail

    mov  rax, [rel mp_protocol_ptr]
    jmp  .loc_done

.loc_fail:
    xor  eax, eax

.loc_done:
    add  rsp, 0x28
    pop  r12
    pop  rbx
    pop  rbp
    ret

; =====================================================
; uefi_mp_get_num_processors
; System V: RDI = mp, RSI = &num_cpus, RDX = &num_enabled
; Повертає: RAX = status
; =====================================================
global uefi_mp_get_num_processors
uefi_mp_get_num_processors:
    push rbp
    mov  rbp, rsp
    push rbx
    sub  rsp, 0x28

    mov  rbx, rdi               ; rbx = mp

    ; UEFI: RCX=This, RDX=&num_cpus, R8=&num_enabled
    mov  rcx, rbx
    mov  r8,  rdx
    mov  rdx, rsi

    mov  rax, [rbx + 0x00]      ; GetNumberOfProcessors
    call rax

    add  rsp, 0x28
    pop  rbx
    pop  rbp
    ret

; =====================================================
; uefi_mp_startup_all_aps
; System V:
;   RDI = mp
;   RSI = Procedure
;   RDX = ProcedureArgument
;   RCX = SingleThread
;   R8  = Timeout
;   R9  = WaitEvent
; Повертає: RAX = status
; =====================================================
global uefi_mp_startup_all_aps
uefi_mp_startup_all_aps:
    push rbp
    mov  rbp, rsp
    push rbx
    sub  rsp, 0x38              ; 0x20 (shadow) + 0x10 (args) + 8 (вирівнювання)

    mov  rbx, rdi               ; rbx = mp

    ; --- UEFI x64 ABI ---
    ; RCX = This           = mp
    ; RDX = Procedure      = Procedure
    ; R8  = SingleThread   = SingleThread
    ; R9  = WaitEvent      = WaitEvent
    ; [rsp+0x20] = TimeoutInMicroseconds
    ; [rsp+0x28] = FailedCpuList

    mov  rcx, rbx               ; This
    mov  r10, rsi               ; зберегти Procedure
    mov  rdx, r10               ; Procedure

    ; R8 (UEFI) = SingleThread (System V arg4)
    ; У System V arg4 = RCX → треба зберегти перед перезаписом RCX
    mov  r11, rcx               ; r11 = System V arg4 (SingleThread)
    mov  r8,  r11               ; UEFI arg3 = SingleThread

    ; R9 = WaitEvent (System V arg6) — вже в R9
    ; Нічого не робимо

    ; 5-й аргумент UEFI = Timeout (System V arg5 = R8)
    ; Але R8 вже перезаписано! Треба було зберегти заздалегідь.
    ; ВИПРАВЛЕННЯ: зберегти R8 (Timeout) до перезапису
    ; (це вже пізно — переписуємо логіку: збережемо спочатку)

    ; ---- ПРАВИЛЬНА ЛОГІКА ----
    ; System V вхід:
    ;   RDI=mp, RSI=Proc, RDX=ProcArg, RCX=Single, R8=Timeout, R9=WaitEvent
    ; UEFI вихід:
    ;   RCX=mp, RDX=Proc, R8=Single, R9=WaitEvent, [rsp+20]=Timeout, [rsp+28]=NULL

    ; Зберігаємо у shadow space та на стеку
    mov  [rsp + 0x20], r8       ; Timeout (System V arg5)
    mov  qword [rsp + 0x28], 0  ; FailedCpuList = NULL

    ; Регістри для UEFI
    mov  rcx, rdi               ; This = mp
    mov  rdx, rsi               ; Procedure
    mov  r8,  r11               ; SingleThread (збережено вище)
    ; r9 = WaitEvent — вже правильний

    ; Виклик
    mov  rax, [rcx + 0x10]      ; mp->StartupAllAPs
    call rax

    add  rsp, 0x38
    pop  rbx
    pop  rbp
    ret

; =====================================================
; uefi_mp_startup_this_ap
; System V:
;   RDI = mp
;   RSI = Procedure
;   RDX = ProcedureArgument
;   RCX = ProcessorNumber
;   R8  = WaitEvent
;   R9  = Timeout
; Повертає: RAX = status
; =====================================================
global uefi_mp_startup_this_ap
uefi_mp_startup_this_ap:
    push rbp
    mov  rbp, rsp
    push rbx
    sub  rsp, 0x38

    mov  rbx, rdi               ; rbx = mp

    ; System V: RDI=mp, RSI=Proc, RDX=ProcArg, RCX=ProcNum, R8=WaitEvent, R9=Timeout
    ; UEFI:      RCX=mp, RDX=Proc, R8=ProcNum, R9=WaitEvent,
    ;            [rsp+20]=Timeout, [rsp+28]=ProcArg

    ; Стекові аргументи UEFI
    mov  [rsp + 0x20], r9       ; Timeout
    mov  [rsp + 0x28], rdx      ; ProcedureArgument

    ; Регістрові аргументи UEFI
    mov  r9,  r8                ; WaitEvent
    mov  r8,  rcx               ; ProcessorNumber
    mov  rcx, rbx               ; This = mp
    mov  rdx, rsi               ; Procedure

    mov  rax, [rcx + 0x18]      ; mp->StartupThisAP
    call rax

    add  rsp, 0x38
    pop  rbx
    pop  rbp
    ret

; =====================================================
; uefi_mp_whoami
; System V: RDI = mp, RSI = &cpu_num
; =====================================================
global uefi_mp_whoami
uefi_mp_whoami:
    push rbp
    mov  rbp, rsp
    push rbx
    sub  rsp, 0x28

    mov  rbx, rdi

    mov  rcx, rbx               ; This
    mov  rdx, rsi               ; &cpu_num
    mov  rax, [rbx + 0x30]      ; WhoAmI
    call rax

    add  rsp, 0x28
    pop  rbx
    pop  rbp
    ret

; =====================================================
; uefi_create_event
; System V: RDI = BootServices, RSI = &out_event
; Створює EVT_NOTIFY_SIGNAL подію (Type = 0)
; Повертає: RAX = status
; =====================================================
global uefi_create_event
uefi_create_event:
    push rbp
    mov  rbp, rsp
    push rbx
    sub  rsp, 0x38

    mov  rbx, rdi               ; rbx = BootServices

    ; CreateEvent(Type, NotifyTpl, NotifyFn, NotifyCtx, &Event)
    mov  rcx, 0                 ; Type = 0 (EVT_NOTIFY_SIGNAL)
    mov  rdx, 0x10              ; TPL_CALLBACK
    xor  r8, r8                 ; NotifyFn = NULL
    xor  r9, r9                 ; NotifyCtx = NULL
    mov  [rsp + 0x20], rsi      ; &Event (5-й аргумент)

    mov  rax, [rbx + 0x50]      ; BootServices->CreateEvent
    call rax

    add  rsp, 0x38
    pop  rbx
    pop  rbp
    ret