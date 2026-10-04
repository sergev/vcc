/*
 * Bare-metal qemu, booted through the PVH note: loaded at 1 MiB, clear of the BIOS
 * area, in the 128 MiB of RAM qemu gives by default.  The small code model needs
 * everything below 2 GiB.
 */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

SECTIONS
{
    . = 0x100000;
    .note.Xen : { *(.note.Xen) }
    .text : { *(.text.start) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : ALIGN(4096) { *(.data .data.*) }
    .bss (NOLOAD) : ALIGN(16) {
        __bss_start = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(16);
        __bss_end = .;
    }
    . = ALIGN(16);
    __heap_start = .;
    . += 0x400000;
    __heap_end = .;
    . += 0x100000;
    __stack_top = .;
    /* No unwinding on bare metal; clang emits .eh_frame for C. */
    /DISCARD/ : { *(.eh_frame) *(.note.GNU-stack) *(.comment) }
}
