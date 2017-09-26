/*
    GNU linker script for MPY external modules
*/

SECTIONS
{
    .all :
    {
        . = ALIGN(4);
        KEEP(*(.mpy_header))
        KEEP(*(.mpy_fun_table))
        KEEP(*(.mpy_qstr_table))
        KEEP(*(.mpy_text))
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    }
}
