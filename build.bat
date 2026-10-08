@echo off
cls
set PATH=%CD%\elf_compile\bin;%CD%\mingw64\bin;%PATH%

set KFLAGS=-nostdlib -ffreestanding -fno-builtin -fno-stack-protector ^
 -fno-pic -fno-pie -fno-asynchronous-unwind-tables ^
 -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -mno-80387 ^
 -mcmodel=small -m64 -O0 -Wall -Wextra -std=gnu11 ^
 -fno-strict-aliasing -Isrc

set DFLAGS=-nostdlib -ffreestanding -fno-builtin -fno-stack-protector ^
 -fPIC -fno-asynchronous-unwind-tables ^
 -mno-red-zone ^
 -mcmodel=small -m64 -O2 -Wall -Wextra -std=gnu11 ^
 -fno-strict-aliasing -Isrc

echo [1/5] COMPILING ASM...

nasm -f win64 src/BOOTX64.asm -o BOOTX64.obj
if %errorlevel% neq 0 goto :err

nasm -f elf64 src/KENTRY.asm -o KENTRY.o
if %errorlevel% neq 0 goto :err

nasm -f elf64 src/interrupts.asm -o interrupts.o
if %errorlevel% neq 0 goto :err

nasm -f elf64 src/duefi.asm -o duefi.o
if %errorlevel% neq 0 goto :err

nasm -f elf64 src/dws_entry.asm -o dws_entry.o
if %errorlevel% neq 0 goto :err

nasm -f elf64 src/uart_entry.asm -o uart_entry.o
if %errorlevel% neq 0 goto :err

nasm -f elf64 src/DevCon_entry.asm -o DevCon_entry.o
if %errorlevel% neq 0 goto :err

nasm -f elf64 src/dkbd_entry.asm -o dkbd_entry.o
if %errorlevel% neq 0 goto :err

nasm -f elf64 src/DepMgr_entry.asm -o DepMgr_entry.o
if %errorlevel% neq 0 goto :err

nasm -f elf64 src/dmouse_entry.asm -o dmouse_entry.o
if %errorlevel% neq 0 goto :err

nasm -f elf64 src/dumcas_entry.asm -o dumcas_entry.o
if %errorlevel% neq 0 goto :err

nasm -f elf64 src/dsmp.asm -o dsmp.o
if %errorlevel% neq 0 goto :err

nasm -f elf64 src/dap_worker.asm -o dap_worker.o
if %errorlevel% neq 0 goto :err

nasm -f elf64 src/de1000_entry.asm -o de1000_entry.o
if %errorlevel% neq 0 goto :err

nasm -f elf64 src/dsimd_entry.asm -o dsimd_entry.o
if %errorlevel% neq 0 goto :err

nasm -f elf64 src/pci_entry.asm -o pci_entry.o
if %errorlevel% neq 0 goto :err

nasm -f elf64 src/acpi_entry.asm -o acpi_entry.o
if %errorlevel% neq 0 goto :err

echo [2/5] COMPILING C...

x86_64-elf-gcc -c src/kmain.c -o kmain.o %KFLAGS%
if %errorlevel% neq 0 goto :err

x86_64-elf-gcc -c src/interrupts.c -o interrupts_c.o %KFLAGS%
if %errorlevel% neq 0 goto :err

x86_64-elf-gcc -c src/dreg.c -o dreg.o %KFLAGS%
if %errorlevel% neq 0 goto :err

x86_64-elf-gcc -c src/delf.c -o delf.o %KFLAGS%
if %errorlevel% neq 0 goto :err

x86_64-elf-gcc -c src/dbmp.c -o dbmp.o %KFLAGS%
if %errorlevel% neq 0 goto :err

x86_64-elf-gcc -c src/dws.c -o dws.o %DFLAGS% -fPIC
if %errorlevel% neq 0 goto :err

x86_64-elf-gcc -c src/uart.c -o uart.o %DFLAGS% -fPIC
if %errorlevel% neq 0 goto :err

x86_64-elf-gcc -c src/dboot.c -o dboot.o %DFLAGS% -fPIC
if %errorlevel% neq 0 goto :err

x86_64-elf-gcc -c src/DevCon.c -o DevCon.o %DFLAGS% -fPIC
if %errorlevel% neq 0 goto :err

x86_64-elf-gcc -c src/dkbd.c -o dkbd.o %DFLAGS% -fPIC
if %errorlevel% neq 0 goto :err

x86_64-elf-gcc -c src/DepMgr.c -o DepMgr.o %DFLAGS% -fPIC
if %errorlevel% neq 0 goto :err

x86_64-elf-gcc -c src/dmouse.c -o dmouse.o %DFLAGS% -fPIC
if %errorlevel% neq 0 goto :err

x86_64-elf-gcc -c src/dumcas.c -o dumcas.o %DFLAGS% -fPIC
if %errorlevel% neq 0 goto :err

x86_64-elf-gcc -c src/dsmp.c -o dsmp_1.o %KFLAGS%
if %errorlevel% neq 0 goto :err

x86_64-elf-gcc -c src/dstorage.c -o dstorage.o %KFLAGS%
if %errorlevel% neq 0 goto :err

x86_64-elf-gcc -c src/dhpet.c -o dhpet.o %KFLAGS%
if %errorlevel% neq 0 goto :err

x86_64-elf-gcc -c src/dap.c -o dap_c.o %KFLAGS%
if %errorlevel% neq 0 goto :err

x86_64-elf-gcc -c src/dap_worker.c -o dap_worker_c.o %KFLAGS%
if %errorlevel% neq 0 goto :err

x86_64-elf-gcc -c src/de1000.c -o de1000.o %DFLAGS% -fPIC
if %errorlevel% neq 0 goto :err

x86_64-elf-gcc -c src/dsimd.c -o dsimd.o %DFLAGS% -fPIC
if %errorlevel% neq 0 goto :err

x86_64-elf-gcc -c src/pci.c -o pci.o %DFLAGS% -fPIC
if %errorlevel% neq 0 goto :err

x86_64-elf-gcc -c src/acpi.c -o acpi.o %DFLAGS% -fPIC
if %errorlevel% neq 0 goto :err

echo [4/5] LINKING...

x86_64-elf-ld -m elf_x86_64 -T linker.ld -o DEPRESSURE.ELF ^
    KENTRY.o interrupts.o kmain.o interrupts_c.o dreg.o duefi.o delf.o dbmp.o dboot.o dsmp.o dsmp_1.o dstorage.o dhpet.o ^
    dap_c.o dap_worker.o dap_worker_c.o
if %errorlevel% neq 0 goto :err

x86_64-elf-ld -pie -z max-page-size=0x1000 -m elf_x86_64 -T dws.ld -o dws.elf dws_entry.o dws.o
if %errorlevel% neq 0 goto :err

.\mingw64\bin\ld.exe -m i386pep --subsystem 10 -e efi_main -o BOOTX64.EFI BOOTX64.obj
if %errorlevel% neq 0 goto :err

x86_64-elf-ld -pie -z max-page-size=0x1000 -m elf_x86_64 -T uart.ld -o uart.elf uart_entry.o uart.o
if %errorlevel% neq 0 goto :err

x86_64-elf-ld -pie -z max-page-size=0x1000 -m elf_x86_64 -T DevCon.ld -o DevCon.elf DevCon_entry.o DevCon.o duefi.o
if %errorlevel% neq 0 goto :err

x86_64-elf-ld -pie -z max-page-size=0x1000 -m elf_x86_64 -T dkbd.ld -o dkbd.elf ^
    dkbd_entry.o dkbd.o
if %errorlevel% neq 0 goto :err

x86_64-elf-ld -pie -z max-page-size=0x1000 -m elf_x86_64 -T DepMgr.ld -o DepMgr.elf ^
    DepMgr_entry.o DepMgr.o
if %errorlevel% neq 0 goto :err

x86_64-elf-ld -pie -z max-page-size=0x1000 -m elf_x86_64 -T dmouse.ld -o dmouse.elf ^
    dmouse_entry.o dmouse.o
if %errorlevel% neq 0 goto :err

x86_64-elf-ld -pie -z max-page-size=0x1000 -m elf_x86_64 -T de1000.ld -o de1000.elf ^
    de1000_entry.o de1000.o duefi.o
if %errorlevel% neq 0 goto :err

x86_64-elf-ld -pie -z max-page-size=0x1000 -m elf_x86_64 -T umcas.ld -o umcas.elf ^
    dumcas_entry.o dumcas.o
if %errorlevel% neq 0 goto :err

x86_64-elf-ld -pie -z max-page-size=0x1000 -m elf_x86_64 -T dsimd.ld -o dsimd.elf ^
    dsimd_entry.o dsimd.o
if %errorlevel% neq 0 goto :err

x86_64-elf-ld -pie -z max-page-size=0x1000 -m elf_x86_64 -T pci.ld -o pci.elf ^
    pci_entry.o pci.o
if %errorlevel% neq 0 goto :err

x86_64-elf-ld -pie -z max-page-size=0x1000 -m elf_x86_64 -T acpi.ld -o acpi.elf ^
    acpi_entry.o acpi.o duefi.o
if %errorlevel% neq 0 goto :err

echo [5/5] IMAGE + QEMU...

if not exist image\EFI\BOOT mkdir image\EFI\BOOT
if not exist image\Depressure\BOOT mkdir image\Depressure\BOOT

copy /Y BOOTX64.EFI image\EFI\BOOT\BOOTX64.EFI > nul
copy /Y DEPRESSURE.ELF image\EFI\BOOT\DEPRESSURE.ELF > nul
copy /Y dws.elf image\Depressure\BOOT\dws.elf > nul
copy /Y uart.elf image\Depressure\BOOT\uart.elf > nul
copy /Y DevCon.elf image\Depressure\DevCon.elf > nul
copy /Y dkbd.elf image\Depressure\BOOT\dkbd.elf > nul
copy /Y DepMgr.elf image\Depressure\DepMgr.elf > nul
copy /Y dmouse.elf image\Depressure\BOOT\dmouse.elf > nul
copy /Y umcas.elf image\Depressure\BOOT\umcas.elf > nul
copy /Y de1000.elf image\Depressure\BOOT\de1000.elf > nul
copy /Y dsimd.elf image\Depressure\BOOT\dsimd.elf > nul
copy /Y pci.elf image\Depressure\BOOT\pci.elf > nul
copy /Y acpi.elf image\Depressure\BOOT\acpi.elf > nul

echo.
qemu-system-x86_64 -bios OVMF.fd ^
 -smp 6 ^
 -drive file=fat:rw:image,format=raw ^
 -serial stdio ^
 -no-reboot ^
 -d int -D qemu.log ^
 -netdev user,id=hostnet0 -device e1000,netdev=hostnet0 ^
 -m 2G
goto :eof

:err
echo BUILD FAILED
pause
exit /b %errorlevel%