/*
    GNU linker script for MPY external modules
*/

SECTIONS
{
    .all :
    {
        . = ALIGN(4);
        KEEP(*(.mpyheader))
        QUAD(_qstr_end - _qstr_start)
        _qstr_start = .;
        KEEP(*(.qstr))
        _qstr_end = .;
        . = ALIGN(8);
        KEEP(*(.mpytext))
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    }
}
