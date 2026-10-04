/*
 * The classic MSP430 under mspsim, whose 64 KB are all RAM: the peripherals below 0x200,
 * then 15.5 KB of "RAM" for .data, .bss, the heap and the stack, and 48 KB of "ROM" for
 * code, .rodata and the load image of .data, then the vectors at 0xffe0.  The split
 * mirrors a flash device, so crt0 copies .data as it would have to there.  The heap
 * follows .bss and grows up; the stack starts at the top of RAM and grows down.
 *
 * Link with ld.lld -n, or it loads the ELF headers at address 0, over the peripherals.
 */
OUTPUT_FORMAT(elf32-msp430)
OUTPUT_ARCH(msp430)
ENTRY(_start)

MEMORY
{
    ram (rwx)    : ORIGIN = 0x0200, LENGTH = 0x3e00
    rom (rx)     : ORIGIN = 0x4000, LENGTH = 0xbfe0
    vectors (rx) : ORIGIN = 0xffe0, LENGTH = 0x20
}

SECTIONS
{
    .text : {
        *(.text .text.*)
    } > rom

    .rodata : {
        *(.rodata .rodata.*)
    } > rom

    .data : {
        __data_start = .;
        *(.data .data.*)
        __data_end = .;
    } > ram AT > rom
    __data_load_start = LOADADDR(.data);

    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss .bss.* COMMON)
        __bss_end = .;
    } > ram

    .noinit (NOLOAD) : {
        *(.noinit .noinit.*)
    } > ram

    __heap_start = ALIGN(2);
    __stack_top = ORIGIN(ram) + LENGTH(ram);

    .vectors : {
        KEEP(*(.vectors))
    } > vectors

    /* No unwinding on bare metal. */
    /DISCARD/ : { *(.eh_frame) *(.note.GNU-stack) *(.comment) }
}

/* Leave the stack at least 2 KB: a program too big for RAM fails to link. */
ASSERT(__heap_start + 2048 <= __stack_top, "too little RAM left for the stack")
