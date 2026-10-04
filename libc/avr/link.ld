/*
 * The ATmega1280 on bare-metal qemu (-M arduino-mega -bios): 128 KB of flash at 0, and
 * 8 KB of SRAM at 0x200..0x21ff, which the AVR toolchain addresses at 0x800000 + the
 * data address.  .rodata is copied to SRAM with .data, as with avr-gcc and clang: an
 * ordinary pointer reaches only SRAM.  The heap follows .bss and grows up; the stack
 * starts at the top of SRAM and grows down.
 */
OUTPUT_FORMAT(elf32-avr)
OUTPUT_ARCH(avr:51)
ENTRY(_start)

MEMORY
{
    flash (rx)  : ORIGIN = 0, LENGTH = 128K
    sram (rw!x) : ORIGIN = 0x800200, LENGTH = 8K
}

SECTIONS
{
    .text : {
        KEEP(*(.vectors))
        *(.text .text.*)
        *(.progmem .progmem.*)
    } > flash

    .data : {
        __data_start = .;
        *(.data .data.*)
        *(.rodata .rodata.*)
        __data_end = .;
    } > sram AT > flash
    __data_load_start = LOADADDR(.data);

    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss .bss.* COMMON)
        __bss_end = .;
    } > sram

    .noinit (NOLOAD) : {
        *(.noinit .noinit.*)
    } > sram

    __heap_start = .;
    __stack_top = ORIGIN(sram) + LENGTH(sram) - 1;

    /* No unwinding on bare metal. */
    /DISCARD/ : { *(.eh_frame) *(.note.GNU-stack) *(.comment) }
}

/* Leave the stack at least 1 KB: a program too big for SRAM fails to link. */
ASSERT(__heap_start + 1024 <= __stack_top, "too little SRAM left for the stack")
