/* P5OS kernel link script: flat binary loaded at 1 MB by stage 2. */
OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(_start)

SECTIONS
{
    . = 0x100000;
    __kernel_start = .;

    .text : {
        *(.text.entry)          /* _start must be the first byte */
        *(.text .text.*)
    }
    .rodata : ALIGN(16) { *(.rodata .rodata.*) }
    .data   : ALIGN(16) { *(.data .data.*) }
    . = ALIGN(16);
    __image_end = .;            /* end of bytes present in kernel.bin */

    .bss (NOLOAD) : ALIGN(16) {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    }
    . = ALIGN(4096);
    __kernel_end = .;           /* first page after the kernel */

    /DISCARD/ : { *(.comment) *(.note .note.*) *(.eh_frame*) }
}
