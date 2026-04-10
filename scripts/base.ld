ENTRY(_start)

SECTIONS
{
    /* ========================= */
    /* CODE */
    /* ========================= */
    .text :
    {
        _text_start = .;
        *(.text*)
        *(.init)
        *(.fini)
        _text_end = .;
    }

    /* ========================= */
    /* READ-ONLY DATA */
    /* ========================= */
    .rodata :
    {
        _rodata_start = .;
        *(.rodata*)
        _rodata_end = .;
    }

    /* ========================= */
    /* INITIALIZED DATA */
    /* ========================= */
    .data :
    {
        _data_start = .;
        *(.data*)
        _data_end = .;
    }

    /* ========================= */
    /* ZEROED DATA */
    /* ========================= */
    .bss :
    {
        _bss_start = .;
        *(.bss*)
        *(COMMON)
        _bss_end = .;
    }

    /* ========================= */
    /* DEFAULT DISCARD */
    /* ========================= */
    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}