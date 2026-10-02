/* qemu `virt` machine: RAM at 0x40000000 (the DTB may be at its start), entry _start. */
OUTPUT_ARCH(aarch64)
ENTRY(_start)

SECTIONS
{
    . = 0x40080000;
    .text : { *(.text.start) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : ALIGN(16) { *(.data .data.*) }
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
    . += 0x10000;
    __stack_top = .;
}
