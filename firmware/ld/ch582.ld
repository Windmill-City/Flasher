/*
 * CH582F memory layout.
 *
 * 448 KiB code flash at 0x00000000 and 32 KiB SRAM at 0x20000000.
 * The vector table lives in .highcode so it can be copied to RAM.
 */

ENTRY(_start)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 448K
    RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 32K
}

SECTIONS
{
    .start :
    {
        KEEP(*(.start))
        *(.vector_handler)
        . = ALIGN(4);
    } > FLASH AT> FLASH

    .highcode :
    {
        . = ALIGN(4);
        PROVIDE(_highcode_vma_start = .);
        . = ALIGN(1024);
        *(.vector)
        *(.highcode)
        *(.highcode.*)
        . = ALIGN(4);
        PROVIDE(_highcode_vma_end = .);
    } > RAM AT> FLASH

    PROVIDE(_highcode_lma = LOADADDR(.highcode));

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH AT> FLASH

    PROVIDE(_data_lma = LOADADDR(.data));

    .data :
    {
        . = ALIGN(4);
        PROVIDE(_data_vma = .);
        *(.data .data.*)
        . = ALIGN(8);
        PROVIDE(__global_pointer$ = . + 0x800);
        *(.sdata .sdata.*)
        . = ALIGN(8);
        *(.srodata.cst16)
        *(.srodata.cst8)
        *(.srodata.cst4)
        *(.srodata.cst2)
        *(.srodata .srodata.*)
        . = ALIGN(4);
        PROVIDE(_edata = .);
    } > RAM AT> FLASH

    .bss :
    {
        . = ALIGN(4);
        PROVIDE(_sbss = .);
        *(.sbss*)
        *(.bss*)
        *(COMMON*)
        . = ALIGN(4);
        PROVIDE(_ebss = .);
    } > RAM AT> FLASH

    .stack ORIGIN(RAM) + LENGTH(RAM) :
    {
        . = ALIGN(4);
        PROVIDE(_eusrstack = .);
    } > RAM
}
