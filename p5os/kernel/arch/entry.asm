; Kernel entry point.  Stage 2 jumps here (physical 0x100000) in 32-bit
; protected mode with flat segments, interrupts disabled,
; EAX = BOOTINFO_MAGIC and EBX = pointer to the boot info block.

BITS 32
section .text.entry
global _start
extern kmain
extern __bss_start
extern __bss_end

_start:
    cli
    mov esi, eax                ; magic (EBX is not touched by the clear)

    ; zero .bss first: the kernel stack lives there
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    cld
    rep stosb

    mov esp, kernel_stack_top
    xor ebp, ebp                ; EBP=0 terminates stack back-traces
    push ebx                    ; arg2: bootinfo pointer
    push esi                    ; arg1: magic
    call kmain

.hang:                          ; kmain must never return
    cli
    hlt
    jmp .hang

; Kernel stack (16 KB) in .bss, page aligned, preceded by a guard page.
; Once paging is on (kernel/mm/paging.c) the guard page is left unmapped, so
; a stack overflow faults (#PF -> #DF task gate) instead of silently
; corrupting other kernel data.
KERNEL_STACK_SIZE equ 16384
PAGE_SIZE         equ 4096
section .bss nobits alloc noexec write align=4096
global kernel_stack_guard
global kernel_stack_bottom
global kernel_stack_top
kernel_stack_guard:
    resb PAGE_SIZE
kernel_stack_bottom:
    resb KERNEL_STACK_SIZE
kernel_stack_top:

section .note.GNU-stack noalloc noexec nowrite progbits
