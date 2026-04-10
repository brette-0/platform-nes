SECTIONS
{
    .chr_rom :
    {
        /* force origin */
        . = 0x0000;

        __chr_rom_start = .;

        KEEP(*(.chr_rom*))

        . = __chr_rom_start + 0x2000;

        __chr_rom_end = .;

        FILL(0x00);
    }

    ASSERT((__chr_rom_end - __chr_rom_start) == 0x2000,
           "CHR ROM must be exactly 8KB");
}