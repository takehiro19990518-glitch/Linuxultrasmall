; Interrupt entry stubs.  Every vector pushes (error code, vector number) so
; that isr_common can build a uniform struct int_frame (include/arch.h).

BITS 32
section .text
extern interrupt_dispatch

%macro ISR_NOERR 1
isr%1:
    push dword 0                ; dummy error code
    push dword %1
    jmp isr_common
%endmacro

%macro ISR_ERR 1                ; CPU already pushed an error code
isr%1:
    push dword %1
    jmp isr_common
%endmacro

; CPU exceptions 0-31 (8, 10-14 and 17 push an error code)
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
%assign i 18
%rep 14
ISR_NOERR i
%assign i i+1
%endrep
; hardware IRQs 0-15 -> vectors 32-47
%assign i 32
%rep 16
ISR_NOERR i
%assign i i+1
%endrep

isr_common:
    pushad
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10                ; kernel data selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    cld
    push esp                    ; argument: struct int_frame *
    call interrupt_dispatch
    add esp, 4
    pop gs
    pop fs
    pop es
    pop ds
    popad
    add esp, 8                  ; drop vector number and error code
    iretd

section .rodata
global isr_stub_table
isr_stub_table:
%assign i 0
%rep 48
    dd isr %+ i
%assign i i+1
%endrep

section .note.GNU-stack noalloc noexec nowrite progbits
