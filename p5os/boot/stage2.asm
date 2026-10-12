; =============================================================================
; P5OS stage 2 boot loader
;
; Loaded by stage 1 at 0000:7E00, entered in real mode with DL = boot drive.
;
;   1. set VGA text mode 3 (80x25)
;   2. verify a 32-bit CPU (386+) and detect CPUID (Pentium has it)
;   3. detect memory: INT 15h E820 -> E801 -> AH=88h fallbacks, INT 12h
;   4. enable the A20 gate: BIOS (INT 15h 2401) -> 8042 KBC -> port 0x92
;   5. load the kernel image to 0x10000 (below 1 MB, BIOS reachable)
;   6. load a flat GDT, enter 32-bit protected mode
;   7. copy the kernel to 0x100000 and jump to it with
;      EAX = BOOTINFO_MAGIC, EBX = address of the boot info block
;
; Every failure prints a reason on screen and waits for a key to reboot.
;
; Low memory map used during boot:
;   0x00500-0x007FF  scratch (A20 test byte at 0x500)
;   0x00800-0x008FF  boot info block (read by kernel)
;   0x01000-0x013FF  E820 memory map buffer (max E820_MAX entries)
;   0x01400-0x07BFF  real/protected-mode stack (grows down from 0x7C00)
;   0x07C00-0x07DFF  stage 1
;   0x07E00-0x09DFF  stage 2 (this file, max 16 sectors)
;   0x10000-0x8FFFF  kernel image staging area (max 512 KB)
; =============================================================================

BITS 16
ORG 0x7E00

%ifndef KERNEL_SECTORS
%error "KERNEL_SECTORS must be defined (-DKERNEL_SECTORS=n)"
%endif
%ifndef STAGE2_SECTORS
%define STAGE2_SECTORS 16
%endif

KERNEL_LBA        equ 1 + STAGE2_SECTORS
KERNEL_TMP_SEG    equ 0x1000            ; staging buffer 0x10000
KERNEL_TMP_ADDR   equ 0x10000
KERNEL_MAX_SECT   equ 1024              ; 512 KB staging limit (0x10000-0x8FFFF)
KERNEL_LOAD_ADDR  equ 0x100000          ; final address (1 MB)

BOOTINFO_ADDR     equ 0x0800
BOOTINFO_MAGIC    equ 0x49423550        ; 'P5BI'
E820_BUF          equ 0x1000
E820_MAX          equ 32
E820_ENTRY_SIZE   equ 24
SMAP              equ 0x534D4150        ; 'SMAP'

A20_TEST_ADDR     equ 0x0500
READ_RETRIES      equ 3
KBC_TIMEOUT       equ 0xFFFF

CODE_SEL          equ 0x08
DATA_SEL          equ 0x10

; boot info block field offsets (must match include/bootinfo.h)
BI_MAGIC          equ 0
BI_BOOT_DRIVE     equ 4
BI_MEM_METHOD     equ 5
BI_CPUID          equ 6
BI_A20_METHOD     equ 7
BI_LOWMEM_KB      equ 8
BI_E820_COUNT     equ 10
BI_EXT_KB_LOW     equ 12
BI_EXT_KB_HIGH    equ 16
BI_E820_ADDR      equ 20
BI_KERNEL_SECT    equ 24
BI_KERNEL_LBA     equ 28
BI_DISK_SPT       equ 32
BI_DISK_HEADS     equ 34
BI_SIZE           equ 64

MEM_E820          equ 1
MEM_E801          equ 2
MEM_88            equ 3

start:
    jmp short stage2_main
    nop
    dd 0x32533550                       ; 'P5S2' signature checked by stage 1

stage2_main:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    cld
    mov [boot_drive], dl

    ; VGA text mode 3: 80x25, 16 colours.  Also clears the screen.
    mov ax, 0x0003
    int 0x10

    mov si, msg_banner
    call print

    ; clear boot info block
    mov di, BOOTINFO_ADDR
    mov cx, BI_SIZE / 2
    xor ax, ax
    rep stosw
    mov al, [boot_drive]
    mov [BOOTINFO_ADDR + BI_BOOT_DRIVE], al

    call check_cpu
    call detect_geometry
    call detect_memory
    call enable_a20
    call load_kernel

    mov si, msg_pmode
    call print

    ; ---- fill in remaining boot info ---------------------------------------
    mov dword [BOOTINFO_ADDR + BI_MAGIC], BOOTINFO_MAGIC
    mov dword [BOOTINFO_ADDR + BI_E820_ADDR], E820_BUF
    mov dword [BOOTINFO_ADDR + BI_KERNEL_SECT], KERNEL_SECTORS
    mov dword [BOOTINFO_ADDR + BI_KERNEL_LBA], KERNEL_LBA
    mov ax, [spt]
    mov [BOOTINFO_ADDR + BI_DISK_SPT], ax
    mov ax, [heads]
    mov [BOOTINFO_ADDR + BI_DISK_HEADS], ax

    ; ---- switch to protected mode -----------------------------------------
    cli
    mov al, 0xFF                        ; mask every IRQ on both 8259 PICs;
    out 0xA1, al                        ; the kernel reprograms them later
    out 0x21, al
    in al, 0x70                         ; disable NMI until the kernel has an IDT
    or al, 0x80
    out 0x70, al

    lgdt [gdt_desc]
    mov eax, cr0
    or eax, 1                           ; CR0.PE
    mov cr0, eax
    jmp CODE_SEL:pmode_entry            ; far jump loads CS, flushes prefetch

; =============================================================================
; check_cpu: require a 386 or later; record whether CPUID exists.
; =============================================================================
check_cpu:
    ; 8086/8088: FLAGS bits 12-15 always read as 1
    pushf
    pop ax
    mov cx, ax
    and ax, 0x0FFF
    push ax
    popf
    pushf
    pop ax
    and ax, 0xF000
    cmp ax, 0xF000
    je .no386
    ; 80286 in real mode: FLAGS bits 12-14 always read as 0
    mov ax, cx
    or ax, 0x7000
    push ax
    popf
    pushf
    pop ax
    push cx
    popf
    test ax, 0x7000
    jz .no386

    ; CPUID present if EFLAGS.ID (bit 21) can be toggled
    pushfd
    pop eax
    mov ecx, eax
    xor eax, 0x00200000
    push eax
    popfd
    pushfd
    pop eax
    push ecx
    popfd
    xor eax, ecx
    and eax, 0x00200000
    jz .no_cpuid
    mov byte [BOOTINFO_ADDR + BI_CPUID], 1
    mov si, msg_cpu_ok
    call print
    ret
.no_cpuid:
    mov si, msg_cpu_nocpuid             ; warn only: a 486 can still boot
    call print
    ret
.no386:
    mov si, msg_err_cpu
    jmp fatal

; =============================================================================
; detect_geometry: CHS geometry of the boot drive (fixed for floppies)
; =============================================================================
detect_geometry:
    mov dl, [boot_drive]
    test dl, 0x80
    jz .done
    mov ah, 0x08
    xor di, di
    push es
    int 0x13
    pop es
    jc .done
    and cx, 0x003F
    jz .done
    mov [spt], cx
    mov dl, dh
    xor dh, dh
    inc dx
    mov [heads], dx
.done:
    ret

; =============================================================================
; detect_memory
; =============================================================================
detect_memory:
    ; conventional memory size in KB (normally 639 or 640)
    int 0x12
    jc .no_int12
    mov [BOOTINFO_ADDR + BI_LOWMEM_KB], ax
    jmp .e820
.no_int12:
    mov word [BOOTINFO_ADDR + BI_LOWMEM_KB], 0

.e820:
    xor ebx, ebx
    mov di, E820_BUF
    xor bp, bp                          ; entry count
.e820_loop:
    mov eax, 0xE820
    mov ecx, E820_ENTRY_SIZE
    mov edx, SMAP
    mov dword [es:di + 20], 1           ; pre-set ACPI 3.0 "valid" bit
    int 0x15
    jc .e820_carry
    cmp eax, SMAP
    jne .e801
    jcxz .e820_next                     ; empty entry
    cmp cl, 20
    jbe .e820_keep
    test byte [es:di + 20], 1           ; ACPI 3.0: bit 0 clear = ignore
    jz .e820_next
.e820_keep:
    mov ecx, [es:di + 8]                ; skip zero-length ranges
    or ecx, [es:di + 12]
    jz .e820_next
    inc bp
    add di, E820_ENTRY_SIZE
    cmp bp, E820_MAX
    jae .e820_done
.e820_next:
    test ebx, ebx
    jnz .e820_loop
    jmp .e820_done
.e820_carry:                            ; CF also marks "end of list" on some BIOSes
    test bp, bp
    jz .e801
.e820_done:
    test bp, bp
    jz .e801
    mov [BOOTINFO_ADDR + BI_E820_COUNT], bp
    mov byte [BOOTINFO_ADDR + BI_MEM_METHOD], MEM_E820
    mov si, msg_mem_e820
    call print
    ret

.e801:
    xor cx, cx
    xor dx, dx
    mov ax, 0xE801
    int 0x15
    jc .m88
    cmp ah, 0x86                        ; unsupported function
    je .m88
    cmp ah, 0x80                        ; invalid command
    je .m88
    jcxz .e801_use_ax                   ; some BIOSes return in AX/BX only
    mov ax, cx
    mov bx, dx
.e801_use_ax:
    movzx eax, ax
    mov [BOOTINFO_ADDR + BI_EXT_KB_LOW], eax    ; KB between 1 MB and 16 MB
    movzx ebx, bx
    shl ebx, 6                                  ; 64 KB blocks -> KB
    mov [BOOTINFO_ADDR + BI_EXT_KB_HIGH], ebx
    mov byte [BOOTINFO_ADDR + BI_MEM_METHOD], MEM_E801
    mov si, msg_mem_e801
    call print
    ret

.m88:
    clc
    mov ah, 0x88
    int 0x15
    jc .no_mem
    test ax, ax
    jz .no_mem
    movzx eax, ax
    mov [BOOTINFO_ADDR + BI_EXT_KB_LOW], eax
    mov byte [BOOTINFO_ADDR + BI_MEM_METHOD], MEM_88
    mov si, msg_mem_88
    call print
    ret
.no_mem:
    mov si, msg_err_mem
    jmp fatal

; =============================================================================
; enable_a20: try each method in turn, verifying after each one
; =============================================================================
enable_a20:
    call check_a20
    mov bl, 1                           ; method 1: already enabled
    jnz .ok

    mov ax, 0x2401                      ; method 2: BIOS
    int 0x15
    call wait_a20
    mov bl, 2
    jnz .ok

    call a20_kbc                        ; method 3: 8042 output port
    call wait_a20
    mov bl, 3
    jnz .ok

    in al, 0x92                         ; method 4: "fast A20" (port 0x92)
    test al, 0x02
    jnz .fast_done
    or al, 0x02
    and al, 0xFE                        ; never set bit 0 (system reset!)
    out 0x92, al
.fast_done:
    call wait_a20
    mov bl, 4
    jnz .ok

    mov si, msg_err_a20
    jmp fatal
.ok:
    mov [BOOTINFO_ADDR + BI_A20_METHOD], bl
    mov si, msg_a20_ok
    call print
    ret

; check_a20: ZF=0 (NZ) if A20 is enabled, ZF=1 if memory wraps at 1 MB
check_a20:
    push ax
    push ds
    push es
    push si
    push di
    xor ax, ax
    mov ds, ax
    not ax
    mov es, ax                          ; ES = 0xFFFF
    mov di, A20_TEST_ADDR               ; DS:DI = 0x000500
    mov si, A20_TEST_ADDR + 0x10        ; ES:SI = 0x100500
    mov al, [ds:di]
    push ax
    mov al, [es:si]
    push ax
    mov byte [ds:di], 0x00
    mov byte [es:si], 0xFF
    mov al, [ds:di]                     ; 0xFF here means the write wrapped
    pop dx
    mov [es:si], dl
    pop dx
    mov [ds:di], dl
    cmp al, 0xFF                        ; ZF=1 -> wrapped -> A20 disabled
    pop di
    pop si
    pop es
    pop ds
    pop ax
    ret

; wait_a20: poll check_a20 for a while (KBC method can be slow)
wait_a20:
    push cx
    mov cx, 0x2000
.loop:
    call check_a20
    jnz .done
    loop .loop
    ; ZF still set here -> not enabled
.done:
    pop cx
    ret

a20_kbc:
    cli
    call kbc_wait_input
    mov al, 0xAD                        ; disable keyboard interface
    out 0x64, al
    call kbc_wait_input
    mov al, 0xD0                        ; read controller output port
    out 0x64, al
    call kbc_wait_output
    in al, 0x60
    push ax
    call kbc_wait_input
    mov al, 0xD1                        ; write controller output port
    out 0x64, al
    call kbc_wait_input
    pop ax
    or al, 0x02                         ; bit 1 = A20 gate
    out 0x60, al
    call kbc_wait_input
    mov al, 0xAE                        ; re-enable keyboard interface
    out 0x64, al
    call kbc_wait_input
    sti
    ret

kbc_wait_input:                         ; wait until input buffer empty
    push cx
    mov cx, KBC_TIMEOUT
.l: in al, 0x64
    test al, 0x02
    jz .d
    loop .l
.d: pop cx
    ret

kbc_wait_output:                        ; wait until output buffer full
    push cx
    mov cx, KBC_TIMEOUT
.l: in al, 0x64
    test al, 0x01
    jnz .d
    loop .l
.d: pop cx
    ret

; =============================================================================
; load_kernel: read KERNEL_SECTORS sectors from KERNEL_LBA to 0x10000.
; Reads up to the end of the current track per BIOS call (fast on floppies),
; never crossing a 64 KB physical boundary (ISA DMA limit).  If a multi-
; sector read keeps failing it falls back to single sectors.
; =============================================================================
load_kernel:
%if KERNEL_SECTORS > KERNEL_MAX_SECT
    %error "kernel image too large for the 512 KB staging area"
%endif
    ; the staging area must lie inside conventional memory reported by INT 12h
    movzx eax, word [BOOTINFO_ADDR + BI_LOWMEM_KB]
    test eax, eax
    jz .size_ok                         ; unknown: assume standard 640 KB
    shl eax, 10
    cmp eax, KERNEL_TMP_ADDR + KERNEL_SECTORS * 512
    jae .size_ok
    mov si, msg_err_lowmem
    jmp fatal
.size_ok:
    mov si, msg_loading
    call print

    mov dword [cur_lba], KERNEL_LBA
    mov word [remaining], KERNEL_SECTORS
    mov word [dest_seg], KERNEL_TMP_SEG

.chunk:
    cmp word [remaining], 0
    je .done

    ; LBA -> CHS
    mov eax, [cur_lba]
    xor edx, edx
    movzx ecx, word [spt]
    div ecx                             ; EAX = track, EDX = sector index
    mov [chs_sector], dl
    mov bx, [spt]
    sub bx, dx                          ; sectors left in this track
    xor edx, edx
    movzx ecx, word [heads]
    div ecx                             ; EAX = cylinder, EDX = head
    cmp eax, 1023
    ja .err_chs
    mov [chs_head], dl
    mov [chs_cyl], ax

    ; limit by remaining count
    cmp bx, [remaining]
    jbe .lim1
    mov bx, [remaining]
.lim1:
    ; limit by 64 KB DMA boundary: sectors until next 64 KB boundary
    mov ax, [dest_seg]
    and ax, 0x0FFF                      ; offset within 64 KB, in paragraphs
    mov cx, 0x1000
    sub cx, ax
    shr cx, 5                           ; paragraphs -> 512-byte sectors
    cmp bx, cx
    jbe .lim2
    mov bx, cx
.lim2:
    cmp byte [single_mode], 0
    je .read
    mov bx, 1
.read:
    mov [chunk_count], bl
    mov di, READ_RETRIES
.retry:
    mov ax, [dest_seg]
    mov es, ax
    xor bx, bx
    mov ax, [chs_cyl]
    mov ch, al
    mov cl, ah
    shl cl, 6
    or cl, [chs_sector]
    inc cl                              ; sectors are 1-based
    mov dh, [chs_head]
    mov dl, [boot_drive]
    mov ah, 0x02
    mov al, [chunk_count]
    int 0x13
    jnc .ok
    mov [last_status], ah
    xor ah, ah                          ; reset controller and retry
    mov dl, [boot_drive]
    int 0x13
    dec di
    jnz .retry
    ; persistent failure: drop to single-sector reads once
    cmp byte [chunk_count], 1
    je .err_read
    mov byte [single_mode], 1
    xor ax, ax
    mov es, ax
    jmp .chunk
.ok:
    xor ax, ax
    mov es, ax
    movzx eax, byte [chunk_count]
    add [cur_lba], eax
    sub [remaining], ax
    shl ax, 5                           ; sectors -> paragraphs
    add [dest_seg], ax
    mov al, '.'
    call putc
    jmp .chunk
.done:
    mov si, msg_crlf
    call print
    ret

.err_chs:
    mov si, msg_err_chs
    jmp fatal
.err_read:
    xor ax, ax
    mov es, ax
    mov si, msg_err_read
    call print
    mov ax, [cur_lba]
    call print_hex16
    mov si, msg_err_status
    call print
    mov al, [last_status]
    xor ah, ah
    call print_hex16
    mov si, msg_crlf
    jmp fatal

; =============================================================================
; text output helpers (BIOS teletype)
; =============================================================================
putc:
    push ax
    push bx
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    pop bx
    pop ax
    ret

print:
    push ax
.next:
    lodsb
    test al, al
    jz .out
    call putc
    jmp .next
.out:
    pop ax
    ret

print_hex16:                            ; AX -> 4 hex digits
    push ax
    push cx
    push dx
    mov dx, ax
    mov cx, 4
.l:
    rol dx, 4
    mov al, dl
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .p
    add al, 'A' - '0' - 10
.p:
    call putc
    loop .l
    pop dx
    pop cx
    pop ax
    ret

; fatal: print SI, wait for a key, reboot
fatal:
    xor ax, ax
    mov ds, ax
    mov es, ax
    call print
    mov si, msg_reboot
    call print
    xor ah, ah
    int 0x16
    int 0x19
.hang:
    hlt
    jmp .hang

; =============================================================================
; 32-bit protected mode entry
; =============================================================================
BITS 32
pmode_entry:
    mov ax, DATA_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x7C00

    ; move the kernel from the staging area to 1 MB (needs A20)
    mov esi, KERNEL_TMP_ADDR
    mov edi, KERNEL_LOAD_ADDR
    mov ecx, KERNEL_SECTORS * 512 / 4
    cld
    rep movsd

    mov eax, BOOTINFO_MAGIC
    mov ebx, BOOTINFO_ADDR
    mov edx, KERNEL_LOAD_ADDR
    jmp edx

; =============================================================================
; data
; =============================================================================
align 8
gdt:
    dq 0                                ; null descriptor
    ; 0x08: code, base 0, limit 4 GB, 32-bit, ring 0, execute/read
    dw 0xFFFF, 0x0000
    db 0x00, 0x9A, 0xCF, 0x00
    ; 0x10: data, base 0, limit 4 GB, 32-bit, ring 0, read/write
    dw 0xFFFF, 0x0000
    db 0x00, 0x92, 0xCF, 0x00
gdt_end:
gdt_desc:
    dw gdt_end - gdt - 1
    dd gdt

boot_drive   db 0
spt          dw 18
heads        dw 2
cur_lba      dd 0
remaining    dw 0
dest_seg     dw 0
chs_cyl      dw 0
chs_head     db 0
chs_sector   db 0
chunk_count  db 0
single_mode  db 0
last_status  db 0

msg_banner      db "P5OS stage2 boot loader", 13, 10, 0
msg_cpu_ok      db "CPU: 32-bit, CPUID present", 13, 10, 0
msg_cpu_nocpuid db "CPU: 32-bit, no CPUID (pre-Pentium?) - continuing", 13, 10, 0
msg_mem_e820    db "Memory: INT 15h E820 map", 13, 10, 0
msg_mem_e801    db "Memory: INT 15h E801", 13, 10, 0
msg_mem_88      db "Memory: INT 15h AH=88h", 13, 10, 0
msg_a20_ok      db "A20 gate enabled", 13, 10, 0
msg_loading     db "Loading kernel", 0
msg_pmode       db "Entering protected mode", 13, 10, 0
msg_crlf        db 13, 10, 0
msg_err_cpu     db "BOOT ERROR: a 32-bit (386 or later) CPU is required", 0
msg_err_mem     db "BOOT ERROR: cannot determine memory size", 0
msg_err_a20     db "BOOT ERROR: A20 gate could not be enabled", 0
msg_err_lowmem  db "BOOT ERROR: not enough conventional memory for kernel", 0
msg_err_chs     db 13, 10, "BOOT ERROR: kernel beyond CHS limit", 0
msg_err_read    db 13, 10, "BOOT ERROR: disk read failed at LBA 0x", 0
msg_err_status  db " status 0x", 0
msg_reboot      db 13, 10, "Press a key to reboot", 0

%if ($ - $$) > STAGE2_SECTORS * 512
    %error "stage2 exceeds its reserved sectors"
%endif
times STAGE2_SECTORS * 512 - ($ - $$) db 0
