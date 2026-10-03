/* qemu `virt` machine: RAM at 0x40000000 (the DTB may be at its start), entry _start. */
OUTPUT_ARCH(arm)
ENTRY(_start)

SECTIONS
{
    . = 0x40010000;
    .text : { *(.text.start) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    /* Unwind tables: clang emits index entries even for C. */
    .ARM.extab : { *(.ARM.extab .ARM.extab.* .gnu.linkonce.armextab.*) }
    .ARM.exidx : {
        __exidx_start = .;
        *(.ARM.exidx .ARM.exidx.* .gnu.linkonce.armexidx.*)
        __exidx_end = .;
    }
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
