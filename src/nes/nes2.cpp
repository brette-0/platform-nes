#include <platform-nes/header.hpp>

// Current build (see src/nes/mappers/vrc1.ld): mapper 75 (VRC1), 32 KiB
// PRG-ROM (2 x 16 KiB), 8 KiB CHR-ROM (1 x 8 KiB). No mirroring, battery,
// trainer, four-screen, submapper, PRG-RAM, PRG-NVRAM, CHR-RAM, or
// CHR-NVRAM configured; NTSC timing; no misc ROMs; no default expansion
// device.
NES2_HEADER(
    'N', 'E', 'S', 0x1a,
    0x02, 0x01,
    0xb0, 0x48, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
);
