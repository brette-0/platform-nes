/**
 * @file nes2.hpp
 * @brief Builds the 16-byte NES2.0 ROM header (https://www.nesdev.org/wiki/NES_2.0)
 *        as a compile-time value, then places it at the front of the ROM file.
 */
#pragma once

#include <intsh>
using namespace br0::intsh;

#ifdef TARGET_NES

// Alternative nametable wiring (NES2.0 flags 6, bit 3 -- the "four-screen"
// bit): set by the board's own CMake config (ALTERNATIVE_NAMETABLE in
// local.cmake), not by a Header with* call -- it's a fact about which
// board is being linked, same as MAPPER/SUBMAPPER below. Only its
// non-zero-ness matters here; the value itself is board glue code's own
// business, not something the header format records.
#ifndef ALTERNATIVE_NAMETABLE
#define ALTERNATIVE_NAMETABLE 0
#endif

/**
 * @brief Places a fully-built ::nes2::Header at the front of the ROM file.
 *
 * Call exactly once, in exactly one TU, with a `nes2::Header` value built by
 * chaining `.withX(...)` calls off `nes2::Header()`. The object's own storage
 * -- guaranteed exactly 16 bytes by the static_assert below -- lands directly
 * in the `.nes2_header` section, which the mapper's linker script (e.g.
 * vrc1.ld) places via its `nes2_header` MEMORY region ahead of
 * PRG-ROM/CHR-ROM, replacing the old `INCLUDE ines-header.ld` mechanism.
 */
#define NES2_PLACE_HEADER(headerExpr)                                        \
    static_assert(sizeof(::nes2::Header) == 16,                              \
        "nes2::Header must stay exactly 16 bytes -- it is placed verbatim "  \
        "as the ROM's NES2.0 header");                                      \
    extern "C" [[gnu::section(".nes2_header"), gnu::used, gnu::retain]]      \
    constexpr ::nes2::Header _nes2_header = (headerExpr)

/** @brief Hard-wired nametable mirroring (NES2.0 flags 6, bit 0). */
enum class Mirroring : u8 {
    Horizontal = 0,
    /// Alias for Horizontal (same bit): the mapper drives nametables at
    /// runtime (MMC1, MMC3, ...), so this bit is a don't-care default, not
    /// a claim about physical wiring. Spelled out at the call site for
    /// readability only.
    Mapper     = 0,
    Vertical   = 1,
};

/** @brief CPU/PPU timing region (NES2.0 byte 12, bits 0-1). */
enum class Timing : u8 {
    NTSC  = 0,
    PAL   = 1,
    MULTI = 2,
    DENDY = 3,
};

namespace nes2 {
    namespace detail {
        /**
         * @brief Splits @p bytes into a power-of-two exponent and odd remainder.
         * @param bytes Value to factor.
         * @param e     Set to the number of trailing zero bits of @p bytes.
         * @return The odd part of @p bytes (bytes >> e).
         */
        constexpr unsigned long long odd_part(unsigned long long bytes, unsigned &e) {
            e = 0;
            while (bytes != 0 && (bytes & 1) == 0) { bytes >>= 1; ++e; }
            return bytes;
        }

        /**
         * @brief True if @p bytes is exactly representable as NES2.0's
         *        exponent-multiplier form, 2^E * (2M + 1) with M in 0..3.
         *
         * Every power-of-two byte count qualifies (odd part 1, M 0) -- which is
         * every PRG-ROM/CHR-ROM size this project produces. @p bytes == 0 is
         * also representable (as the plain-notation zero; the exponent form
         * itself can't spell zero).
         */
        constexpr bool rom_size_representable(unsigned long long bytes) {
            if (bytes == 0) return true;
            unsigned e;
            return odd_part(bytes, e) <= 7;
        }

        /** @brief NES2.0 exponent-multiplier byte for a PRG-ROM/CHR-ROM size (header byte 4 / 5). */
        constexpr u8 rom_size_byte(unsigned long long bytes) {
            if (bytes == 0) return 0;
            unsigned e;
            const auto odd = odd_part(bytes, e);
            return static_cast<u8>((e << 2) | ((odd - 1) / 2));
        }

        /**
         * @brief MSB nibble for header byte 9: 0xF selects the exponent-multiplier
         *        form (byte 4/5 holds E/M instead of a size nibble); 0x0 is the
         *        plain-notation zero used when the ROM is absent.
         */
        constexpr u8 rom_msb_nibble(unsigned long long bytes) {
            return bytes == 0 ? 0x0 : 0xF;
        }

        /**
         * @brief True if @p bytes is 0 (absent) or exactly 64 << n for some n in 1..15
         *        -- the shift-count form shared by PRG-RAM, PRG-NVRAM/EEPROM,
         *        CHR-RAM and CHR-NVRAM (header bytes 10-11).
         */
        constexpr bool ram_size_representable(unsigned long long bytes) {
            if (bytes == 0) return true;
            unsigned long long v = 128;
            unsigned n = 1;
            while (v < bytes && n < 15) { v <<= 1; ++n; }
            return v == bytes;
        }

        /** @brief Shift count for a PRG-RAM/PRG-NVRAM/CHR-RAM/CHR-NVRAM size (header bytes 10-11). */
        constexpr u8 ram_shift(unsigned long long bytes) {
            if (bytes == 0) return 0;
            unsigned long long v = 128;
            unsigned n = 1;
            while (v < bytes) { v <<= 1; ++n; }
            return static_cast<u8>(n);
        }

        // Deliberately declared, never defined: calling one of these from a
        // constant expression makes the expression non-constant, which turns
        // an out-of-range size passed to a `with*` call into a compile error
        // -- the function's name is the diagnostic. This stands in for
        // static_assert here because a single field's validity can't be
        // checked until its own with* method runs, not at one final spot,
        // and it works whether or not the target build has exceptions
        // (llvm-mos/NES builds normally don't).
        void PRG_ROM_size_not_representable_in_NES2_0_exponent_multiplier_form();
        void CHR_ROM_size_not_representable_in_NES2_0_exponent_multiplier_form();
        void PRG_RAM_size_must_be_0_or_64_shl_n_bytes_n_in_1_to_15();
        void PRG_NVRAM_size_must_be_0_or_64_shl_n_bytes_n_in_1_to_15();
        void CHR_RAM_size_must_be_0_or_64_shl_n_bytes_n_in_1_to_15();
        void CHR_NVRAM_size_must_be_0_or_64_shl_n_bytes_n_in_1_to_15();

        constexpr u8 prg_rom_size_byte_checked(unsigned long long bytes) {
            if (!rom_size_representable(bytes)) PRG_ROM_size_not_representable_in_NES2_0_exponent_multiplier_form();
            return rom_size_byte(bytes);
        }

        constexpr u8 chr_rom_size_byte_checked(unsigned long long bytes) {
            if (!rom_size_representable(bytes)) CHR_ROM_size_not_representable_in_NES2_0_exponent_multiplier_form();
            return rom_size_byte(bytes);
        }

        constexpr u8 prg_ram_shift_checked(unsigned long long bytes) {
            if (!ram_size_representable(bytes)) PRG_RAM_size_must_be_0_or_64_shl_n_bytes_n_in_1_to_15();
            return ram_shift(bytes);
        }

        constexpr u8 prg_nvram_shift_checked(unsigned long long bytes) {
            if (!ram_size_representable(bytes)) PRG_NVRAM_size_must_be_0_or_64_shl_n_bytes_n_in_1_to_15();
            return ram_shift(bytes);
        }

        constexpr u8 chr_ram_shift_checked(unsigned long long bytes) {
            if (!ram_size_representable(bytes)) CHR_RAM_size_must_be_0_or_64_shl_n_bytes_n_in_1_to_15();
            return ram_shift(bytes);
        }

        constexpr u8 chr_nvram_shift_checked(unsigned long long bytes) {
            if (!ram_size_representable(bytes)) CHR_NVRAM_size_must_be_0_or_64_shl_n_bytes_n_in_1_to_15();
            return ram_shift(bytes);
        }
    } // namespace detail

    /**
     * @brief The 16 raw NES2.0 header bytes, built by chaining `.withX(...)`
     *        calls off a default-constructed Header.
     *
     * Each `withX` takes `*this` by value, edits the named field on that
     * local copy, and returns it -- so a call site reads like ordinary
     * field-by-field mutation, but because every step is constexpr and the
     * whole chain is one expression, the compiler folds it down to a single
     * final 16-byte constant. There's no intermediate object to mutate at
     * runtime: no virtual dispatch, no heap, nothing but const data in
     * `.nes2_header` -- see ::NES2_PLACE_HEADER.
     *
     * Mapper number, submapper, and four-screen/alternative-nametable wiring
     * (NES2.0 flags 6, bit 3) are baked in by the constructor from the
     * ::MAPPER / ::SUBMAPPER / ::ALTERNATIVE_NAMETABLE compiler defines (set
     * per mapper subtarget in CMakeLists.txt / local.cmake): each mapper
     * subtarget in this project is a specific, known board, so these are
     * compile-time facts about the build rather than something a `with*`
     * call should restate.
     *
     * Console type is always NES/Famicom, trainer is never present, and Vs.
     * System/Extended Console Type, Miscellaneous ROMs, and Default Expansion
     * Device are always zero -- none of these are supported yet, so none of
     * them have a `with*` method.
     */
    class Header {
        u8 b[16];

    public:
        constexpr Header()
            : b{
                'N', 'E', 'S', 0x1a,
                0, 0,
                static_cast<u8>((static_cast<u8>((ALTERNATIVE_NAMETABLE) != 0) << 3) | ((MAPPER & 0xF) << 4)),
                static_cast<u8>(0b1000 | (((MAPPER >> 4) & 0xF) << 4)),
                static_cast<u8>(((MAPPER >> 8) & 0xF) | (SUBMAPPER << 4)),
                0, 0, 0, 0,
                0, 0, 0
              }
        {
            static_assert(MAPPER <= 0xFFF, "MAPPER exceeds NES2.0's 12-bit mapper number field");
            static_assert(SUBMAPPER <= 0xF, "SUBMAPPER exceeds NES2.0's 4-bit submapper field");
        }

        /** @brief Hard-wired nametable mirroring (flags 6, bit 0). */
        constexpr Header withMirroring(Mirroring mirroring) const {
            Header h = *this;
            h.b[6] = static_cast<u8>((h.b[6] & ~u8{0x01}) | static_cast<u8>(mirroring));
            return h;
        }

        /** @brief Battery-backed PRG-RAM/NVRAM present (flags 6, bit 1). */
        constexpr Header withBattery(bool battery) const {
            Header h = *this;
            h.b[6] = static_cast<u8>((h.b[6] & ~u8{0x02}) | (battery ? 0x02 : 0x00));
            return h;
        }

        /** @brief PRG-ROM size, in bytes (header byte 4, plus the MSB nibble in byte 9). */
        constexpr Header withPrgRom(unsigned long long bytes) const {
            Header h = *this;
            h.b[4] = detail::prg_rom_size_byte_checked(bytes);
            h.b[9] = static_cast<u8>((h.b[9] & 0xF0) | detail::rom_msb_nibble(bytes));
            return h;
        }

        /** @brief CHR-ROM size, in bytes; 0 for CHR-RAM-only carts (header byte 5, plus the MSB nibble in byte 9). */
        constexpr Header withChrRom(unsigned long long bytes) const {
            Header h = *this;
            h.b[5] = detail::chr_rom_size_byte_checked(bytes);
            h.b[9] = static_cast<u8>((h.b[9] & 0x0F) | (detail::rom_msb_nibble(bytes) << 4));
            return h;
        }

        /** @brief Volatile PRG-RAM size, in bytes; 0 = absent (header byte 10, low nibble). */
        constexpr Header withPrgRam(unsigned long long bytes) const {
            Header h = *this;
            h.b[10] = static_cast<u8>((h.b[10] & 0xF0) | detail::prg_ram_shift_checked(bytes));
            return h;
        }

        /** @brief Non-volatile PRG-RAM/EEPROM size, in bytes; 0 = absent (header byte 10, high nibble). */
        constexpr Header withPrgNvram(unsigned long long bytes) const {
            Header h = *this;
            h.b[10] = static_cast<u8>((h.b[10] & 0x0F) | (detail::prg_nvram_shift_checked(bytes) << 4));
            return h;
        }

        /** @brief Volatile CHR-RAM size, in bytes; 0 = absent (header byte 11, low nibble). */
        constexpr Header withChrRam(unsigned long long bytes) const {
            Header h = *this;
            h.b[11] = static_cast<u8>((h.b[11] & 0xF0) | detail::chr_ram_shift_checked(bytes));
            return h;
        }

        /** @brief Non-volatile CHR-RAM size, in bytes; 0 = absent (header byte 11, high nibble). */
        constexpr Header withChrNvram(unsigned long long bytes) const {
            Header h = *this;
            h.b[11] = static_cast<u8>((h.b[11] & 0x0F) | (detail::chr_nvram_shift_checked(bytes) << 4));
            return h;
        }

        /** @brief CPU/PPU timing region (header byte 12, bits 0-1). */
        constexpr Header withTiming(Timing timing) const {
            Header h = *this;
            h.b[12] = static_cast<u8>(timing);
            return h;
        }
    };
} // namespace nes2

#else
/** @brief Off-NES builds produce no ROM file, hence no header to emit. */
#define NES2_PLACE_HEADER(...)
#endif
