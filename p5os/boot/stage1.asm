; =============================================================================
; P5OS stage 1 boot loader (MBR / floppy boot sector)
;
; Loaded by the BIOS at 0000:7C00 with DL = boot drive.
; Job: load stage 2 (STAGE2_SECTORS sectors starting at LBA 1) to 0000:7E00
; and jump to it.  Only 8086/186-level instructions are used here so that an
; unsupported CPU still gets as far as stage 2's CPU check and a readable
; error message.
;
; Disk access uses classic INT 13h AH=02h (CHS), one sector at a time, with
; retries and a controller reset between attempts.  CHS works on every BIOS
; (floppy and hard disk) and the sectors we need lie in the first cylinder(s).
;
; Layout of this 512-byte sector:
;   0x000-0x003  jmp + nop
;   0x003-0x03D  reserved, zero (some BIOSes poke at a BPB here on floppies)
;   0x03E-0x1BD  code + data (must fit in 446 bytes total)
;   0x1BE-0x1FD  partition table (filled in by tools/mkimage.py for HDD images)
;   0x1FE-0x1FF  0x55 0xAA boot signature
; =============================================================================

BITS 16
ORG 0x7C00

%ifndef STAGE2_SECTORS
%define STAGE2_SECTORS 16
%endif

STAGE2_ADDR     equ 0x7E00          ; stage 2 load address (segment 0)
STAGE2_LBA      equ 1
STAGE2_MAGIC    equ 0x32533550      ; 'P5S2' (little endian dword)
READ_RETRIES    equ 5
FLOPPY_SPT      equ 18              ; 1.44 MB floppy geometry
FLOPPY_HEADS    equ 2

start:
    jmp short real_start
    nop
    times 0x3E - ($ - $$) db 0      ; reserved BPB area

real_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00                  ; stack grows down below us
    sti
    jmp 0x0000:normalize            ; some BIOSes jump to 07C0:0000
normalize:
    cld
    mov [boot_drive], dl

    mov si, msg_loading
    call print

    ; ---- determine disk geometry -------------------------------------------
    ; Floppies: use fixed 1.44 MB geometry.  INT13/08 on a floppy reports the
    ; *drive* type, not the inserted media, so it can be wrong.
    test dl, 0x80
    jz .have_geometry               ; defaults already hold floppy values

    mov ah, 0x08                    ; get drive parameters
    xor di, di                      ; ES:DI = 0:0 works around BIOS bugs
    push es
    int 0x13
    pop es
    jc .have_geometry               ; keep defaults on failure (unlikely)
    and cx, 0x003F                  ; CL[5:0] = sectors per track
    jz .have_geometry
    mov [spt], cx
    mov dl, dh                      ; DH = max head index
    xor dh, dh
    inc dx
    mov [heads], dx
.have_geometry:

    ; ---- load stage 2 ------------------------------------------------------
    mov ax, STAGE2_LBA
    mov bx, STAGE2_ADDR
    mov cx, STAGE2_SECTORS
.load_loop:
    call read_sector
    inc ax
    add bx, 512
    loop .load_loop

    ; ---- verify and enter stage 2 -----------------------------------------
    cmp word [STAGE2_ADDR + 3], STAGE2_MAGIC & 0xFFFF
    jne bad_stage2
    cmp word [STAGE2_ADDR + 5], STAGE2_MAGIC >> 16
    jne bad_stage2

    mov dl, [boot_drive]
    jmp 0x0000:STAGE2_ADDR

; -----------------------------------------------------------------------------
; read_sector: AX = LBA, ES:BX = destination.  Preserves all registers.
; Halts with an error message after READ_RETRIES failures.
; -----------------------------------------------------------------------------
read_sector:
    push ax
    push bx
    push cx
    push dx
    push di
    mov di, READ_RETRIES
.retry:
    push ax
    xor dx, dx
    div word [spt]                  ; AX = LBA / spt, DX = LBA % spt
    mov cl, dl
    inc cl                          ; CHS sector numbers start at 1
    xor dx, dx
    div word [heads]                ; AX = cylinder, DX = head
    mov dh, dl                      ; DH = head
    mov ch, al                      ; CH = cylinder bits 7..0
    shl ah, 1                       ; cylinder bits 9..8 -> CL bits 7..6
    shl ah, 1
    shl ah, 1
    shl ah, 1
    shl ah, 1
    shl ah, 1
    or cl, ah
    mov dl, [boot_drive]
    mov ax, 0x0201                  ; AH=02 read, AL=1 sector
    int 0x13
    pop ax
    jnc .done
    push ax
    xor ah, ah                      ; reset disk system, then retry
    mov dl, [boot_drive]
    int 0x13
    pop ax
    dec di
    jnz .retry
    mov si, msg_disk_err
    jmp fatal
.done:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

bad_stage2:
    mov si, msg_bad_s2
    ; fall through

; fatal: print message at SI, wait for a key, reboot via INT 19h
fatal:
    call print
    mov si, msg_reboot
    call print
    xor ah, ah
    int 0x16
    int 0x19
.hang:
    hlt
    jmp .hang

; print: SI -> NUL-terminated string, BIOS teletype output
print:
    push ax
    push bx
.next:
    lodsb
    test al, al
    jz .out
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp .next
.out:
    pop bx
    pop ax
    ret

boot_drive  db 0
spt         dw FLOPPY_SPT
heads       dw FLOPPY_HEADS

msg_loading  db "P5OS stage1", 13, 10, 0
msg_disk_err db "Stage1: disk read error", 0
msg_bad_s2   db "Stage1: stage2 signature bad", 0
msg_reboot   db 13, 10, "Press a key to reboot", 0

    %if ($ - $$) > 446
        %error "stage1 code exceeds 446 bytes"
    %endif
    times 446 - ($ - $$) db 0
partition_table:
    times 64 db 0
    dw 0xAA55
