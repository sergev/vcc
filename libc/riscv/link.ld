/* qemu `virt` machine, -bios none: RAM at 0x80000000, entry _start. */
OUTPUT_ARCH(riscv)
ENTRY(_start)

SECTIONS
{
    . = 0x80000000;
    .text : { *(.text.start) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.* .srodata .srodata.*) }
    .data : ALIGN(8) { *(.data .data.*) }
    .sdata : {
        __global_pointer$ = . + 0x800;
        *(.sdata .sdata.*)
    }
    .bss (NOLOAD) : ALIGN(8) {
        __bss_start = .;
        *(.sbss .sbss.* .bss .bss.* COMMON)
        . = ALIGN(8);
        __bss_end = .;
    }
    . = ALIGN(16);
    . += 0x10000;
    __stack_top = .;
}
