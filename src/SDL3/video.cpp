#include "../SDL3/internal.hpp"

#include <platform-nes/video.hpp>
#include <platform-nes/interrupts.hpp>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <atomic>
#include <SDL3/SDL.h>

#include "platform-nes/technology.hpp"

u16 xScroll;
u16 yScroll;
u8* paletteRAM;
static SDL_Texture *bgTexture;
static int yScroll_written;

namespace ppu {
u8 PPUCTRL;
u8 PPUMASK;
}   // namespace ppu

#define SPRITE_ZERO_IRQ_ID 0xFF

static spriteZeroHandler_t sprite0_zero;

void SetSpriteZeroHandler(const u16 px, const u16 py, void (*fn)()) {
    sprite0_zero = (spriteZeroHandler_t){ .method = fn, .px = px, .py = py };
    RegisterIRQHandler(SPRITE_ZERO_IRQ_ID, fn);
}

SDL_Window   *window;
SDL_Renderer *renderer;
SDL_TimerID   timer_id;
std::atomic_int    _vblank_flag;
void        (*_nmi_callback)();
int           quit;
const SDL_DisplayMode *mode;
u8       scale;
u8      *VideoRAM;

const u8 *patternTable = CHR_ROM;

static constexpr u32 nes_rgb[64] = {
    0xFF626262, 0xFF012090, 0xFF1B0CA4, 0xFF3B009E,
    0xFF520080, 0xFF5A004E, 0xFF521610, 0xFF3F2E00,
    0xFF234400, 0xFF0A5200, 0xFF005804, 0xFF004E30,
    0xFF003C62, 0xFF000000, 0xFF000000, 0xFF000000,
    0xFFABABAB, 0xFF1F56D8, 0xFF423CF2, 0xFF6E24EC,
    0xFF9218C4, 0xFF9E1A80, 0xFF933434, 0xFF7A5200,
    0xFF576E00, 0xFF2E8400, 0xFF118E0E, 0xFF008848,
    0xFF007898, 0xFF000000, 0xFF000000, 0xFF000000,
    0xFFFBFBFB, 0xFF6BA4FF, 0xFF8C88FF, 0xFFB87AFF,
    0xFFE072FF, 0xFFF076D0, 0xFFE88C78, 0xFFCCA830,
    0xFFA8C410, 0xFF7EDC24, 0xFF5AE84E, 0xFF48E490,
    0xFF48D4E0, 0xFF4E4E4E, 0xFF000000, 0xFF000000,
    0xFFFBFBFB, 0xFFBED4FF, 0xFFCACAFF, 0xFFDCC4FF,
    0xFFECC0FF, 0xFFF2C0EA, 0xFFF2C8C4, 0xFFE8D4A4,
    0xFFD8E09C, 0xFFC8EC9C, 0xFFBCF0AC, 0xFFB4F0CC,
    0xFFB4E8F0, 0xFFB8B8B8, 0xFF000000, 0xFF000000,
};



#ifdef _WIN32
__asm__(
    ".pushsection chr_rom$a,\"dr\"\n"
    ".global _chr_rom\n"
    "_chr_rom:\n"
    ".popsection\n"
);
#endif

u32 vblank_tick(void *userdata, SDL_TimerID id, const u32 interval) {
    _vblank_flag = 1;
    return interval;  // repeat every 16ms
}

static u64 last_frame;

/* PPU-side OAM: a per-frame snapshot of the application's OAM buffer,
 * mirroring the NES OAMDMA. GenerateFrame renders from this, never from the
 * live buffer, so mid-frame writes (e.g. from the sprite-zero IRQ handler)
 * only appear on the next frame — exactly as on hardware. */
static struct oam::sprite_t oamShadow[OAM_SPRITES];

static void toggle_fullscreen() {
    if (const u32 flags = SDL_GetWindowFlags(window); flags & SDL_WINDOW_FULLSCREEN) {
        SDL_SetWindowFullscreen(window, false); // back to windowed
    } else {
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);
    }
}

#pragma region PPU_EMU

/* Per-pixel NES PPU emulator.
 *
 * Scroll model (matches real PPU V-register behaviour):
 *   yScroll is an ABSOLUTE source address into VRAM — not an offset added
 *   to the screen row. The PPU maintains an internal Y counter (ppu_y here)
 *   that starts at yScroll and auto-increments once per scanline. When code
 *   writes yScroll (SetScroll / DeltaScroll), ppu_y is reset to that value
 *   at the next IRQ boundary, so the remaining pixels on that scanline read
 *   from the new address. xScroll works the same way in X: it defines the
 *   absolute VRAM column of screen pixel 0, and px is added as the scan
 *   offset within the line.
 *
 * IRQ dispatch: each scanline is split at the px of the next queued IRQ.
 * The handler fires before the pixel at its (px, py) renders, so it can
 * mutate xScroll, yScroll, ppu::PPUCTRL, palette — anything — and the very
 * next pixel sees the new state in both axes. */
static void GenerateFrame() {
    const int vpw = video::viewport_px();
    constexpr int vph = video::viewport_py();

    if (!bgTexture) {
        bgTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                      SDL_TEXTUREACCESS_STREAMING, vpw, vph);
        if (!bgTexture) return;
        SDL_SetTextureScaleMode(bgTexture, SDL_SCALEMODE_NEAREST);
    }

    void *raw; int pitch;
    if (!SDL_LockTexture(bgTexture, nullptr, &raw, &pitch)) return;
    const auto pixels = static_cast<u32 *>(raw);
    const int stride = pitch / 4;

    const int nt_cols  = vpw < 512 ? 2 : (vpw + 255) / 256;
    const int world_w  = nt_cols * 256;
    const int spr_base = (ppu::PPUCTRL & ppu::ctrl::SPRITE_ADDR) ? 0x1000 : 0x0000;

    /* PPU Y counter: the absolute VRAM row currently being sourced.
     * Initialised from yScroll, then auto-incremented after each scanline.
     * Any write to yScroll (via SetScroll/DeltaScroll) sets yScroll_written;
     * we pick that up after the IRQ fires and reset ppu_y to the new value,
     * so the next segment renders from the new absolute row. */
    int ppu_y = (int)yScroll;
    yScroll_written = 0;

    size_t irq_idx = 0;

    for (int py = 0; py < vph; py++) {
        /* Sprites use screen-space Y — they don't scroll with the background. */
        int line_spr[64];
        int n_line = 0;
        if (ppu::PPUMASK & ppu::mask::SPRITE) {
            for (size_t s = 0; s < OAM_SPRITES && n_line < 64; s++) {
                if (const int sy = static_cast<int>(oamShadow[s].y) + 1; py >= sy && py < sy + 8)
                    line_spr[n_line++] = static_cast<int>(s);
            }
        }

        int seg_start = 0;
        while (seg_start < vpw) {
            /* Find the next IRQ on this scanline at or after seg_start. */
            int seg_end = vpw;
            int fire    = 0;
            while (irq_idx < irqCount) {
                const irq_t ev = irqBuffer[irq_idx];
                if (static_cast<int>(ev.py) < py
                    || (static_cast<int>(ev.py) == py && static_cast<int>(ev.px) < seg_start)) {
                    irq_idx++;
                    continue;
                }
                if (static_cast<int>(ev.py) == py) {
                    seg_end = static_cast<int>(ev.px);
                    fire    = 1;
                }
                break;
            }

            /* Derive Y source from ppu_y (the PPU's current absolute row).
             * This is computed once per segment: ppu_y only changes at IRQ
             * boundaries, so it is constant within a segment. xScroll is
             * added to px inside the loop for the horizontal scan offset. */
            const int wy        = ppu_y % 240;
            const int tile_row  = wy / 8;
            const int local_row = tile_row % 30;
            const int nt_row    = tile_row / 30;
            const int fine_y    = wy & 7;

            for (int px = seg_start; px < seg_end; px++) {

                /* --- Background ---------------------------------------- */
                int     bg_cidx = 0;
                u8 bg_pal  = 0;
                if ((ppu::PPUMASK & ppu::mask::BG) && ((ppu::PPUMASK & 0x02) || px >= 8)) {
                    const int wx        = (static_cast<int>(xScroll) + px) % world_w;
                    const int tile_col  = wx / 8;
                    const int local_col = tile_col % 32;
                    const int nt_col    = tile_col / 32;
                    const int fine_x    = wx & 7;
                    const int nt_off    = (nt_col + nt_row * nt_cols) * 0x400;

                    const u8 tile_id = VideoRAM[nt_off + local_row * 32 + local_col];
                    const u8 attr    = VideoRAM[nt_off + 0x3C0
                                                   + (local_row / 4) * 8
                                                   + (local_col / 4)];
                    const int shift = ((local_col >> 1) & 1) * 2
                                    + ((local_row >> 1) & 1) * 4;
                    bg_pal = (attr >> shift) & 3;

                    const int chr_base = ((ppu::PPUCTRL & ppu::ctrl::BG_ADDR) ? 0x1000 : 0)
                                       + tile_id * 16 + fine_y;
                    const int bit = 7 - fine_x;
                    bg_cidx = ((patternTable[chr_base]     >> bit) & 1)
                            | (((patternTable[chr_base + 8] >> bit) & 1) << 1);
                }
                const int bg_opaque = bg_cidx != 0;

                /* --- Sprites ------------------------------------------- */
                int     spr_hit    = 0;
                int     spr_behind = 0;
                u8 spr_nes    = 0;
                if ((ppu::PPUMASK & ppu::mask::SPRITE) && ((ppu::PPUMASK & 0x04) || px >= 8)) {
                    for (int k = 0; k < n_line; k++) {
                        const auto [y, tile, attributes, x] = oamShadow[line_spr[k]];
                        const int sx  = (int)x;
                        if (px < sx || px >= sx + 8) continue;
                        const int sy      = static_cast<int>(y + 1);
                        const u8 att = attributes;
                        const int row     = (att & 0x80) ? (7 - (py - sy)) : (py - sy);
                        const int col_bit = (att & 0x40) ? (px - sx) : (7 - (px - sx));
                        const int addr    = spr_base + tile * 16 + row;
                        const int cidx    = ((patternTable[addr]      >> col_bit) & 1)
                                          | (((patternTable[addr + 8]  >> col_bit) & 1) << 1);
                        if (cidx == 0) continue;
                        spr_nes    = paletteRAM[0x10 + (att & 0x03) * 4 + cidx];
                        spr_behind = att & 0x20;
                        spr_hit    = 1;
                        break;
                    }
                }

                /* --- Compose ------------------------------------------- */
                u8 final_nes;
                if      (spr_hit && (!spr_behind || !bg_opaque)) final_nes = spr_nes;
                else if (bg_opaque)                               final_nes = paletteRAM[bg_pal * 4 + bg_cidx];
                else                                              final_nes = paletteRAM[0];

                if (ppu::PPUMASK & 0x01) final_nes &= 0x30;

                u32 col = nes_rgb[final_nes & 0x3F];

                if (ppu::PPUMASK & 0xE0) {
                    u32 r = (col >> 16) & 0xFF;
                    u32 g = (col >>  8) & 0xFF;
                    u32 b =  col        & 0xFF;
                    if (ppu::PPUMASK & 0x20) { g = g * 3 / 4; b = b * 3 / 4; }
                    if (ppu::PPUMASK & 0x40) { r = r * 3 / 4; b = b * 3 / 4; }
                    if (ppu::PPUMASK & 0x80) { r = r * 3 / 4; g = g * 3 / 4; }
                    col = 0xFF000000u | (r << 16) | (g << 8) | b;
                }

                pixels[py * stride + px] = col;
            }

            /* Fire the IRQ, then check if yScroll was written by the handler.
             * If so, reset ppu_y to the new value — the next segment (which
             * starts at seg_end) will derive wy from the updated counter. */
            if (fire) {
                if (const irq_t ev = irqBuffer[irq_idx++]; ev.id < irqTableCount && irqTable[ev.id])
                    irqTable[ev.id]();
                if (yScroll_written) {
                    ppu_y = static_cast<int>(yScroll);
                    yScroll_written = 0;
                }
            }

            seg_start = seg_end;
        }

        /* Advance the PPU Y counter by one scanline, exactly as the real
         * PPU increments its V register at the end of each active line. */
        ppu_y++;
    }

    SDL_UnlockTexture(bgTexture);
    SDL_RenderTexture(renderer, bgTexture, nullptr, nullptr);
}

#pragma endregion

inline static u16 xy_to_nt_addr(u16 x, u16 y) {
    const u16 nt_cols = (video::viewport_tx() < 64 ? 2 : (video::viewport_tx() + 31) / 32);
    const u16 nt_h = (x / 32) % nt_cols;
    const u16 nt_v = y / 30;
    const u16 col  = x % 32;
    const u16 row  = y % 30;

    return (nt_h + nt_v * nt_cols) * 0x400 + row * 32 + col;
}

inline static u16 xy_to_at_addr(u16 x, u16 y) {
    const u16 nt_cols = (video::viewport_tx() < 64 ? 2 : (video::viewport_tx() + 31) / 32);
    const u16 nt_h = (x / 32) % nt_cols;
    const u16 nt_v = y / 30;
    const u16 col  = x % 32;
    const u16 row  = y % 30;

    return (nt_h + nt_v * nt_cols) * 0x400
         + 0x3C0 + (row / 4) * 8 + (col / 4);
}

namespace video {

void WaitForPresent() {
    // pump events once
    SDL_Event e;
    while (SDL_PollEvent(&e)) {

        switch (e.type) {

        case SDL_EVENT_QUIT:
            quit = 1;
            break;

        case SDL_EVENT_KEY_DOWN: {
                const SDL_Keycode key = e.key.key;

                if (const SDL_Keymod mod = SDL_GetModState(); key == SDLK_RETURN && (mod & SDL_KMOD_ALT)) {
                    toggle_fullscreen();
                }

                if (key == SDLK_F11) {
                    toggle_fullscreen();
                }

                input_handle_event(&e);
                break;
        }

        default:
            input_handle_event(&e);
            break;
        }
    }

    const u64 elapsed = SDL_GetTicksNS() - last_frame;
    if (constexpr u64 target = 16666667; elapsed < target) {
        SDL_DelayPrecise(target - elapsed);
    }
    last_frame = SDL_GetTicksNS();

    if (ppu::PPUMASK & (ppu::mask::BG | ppu::mask::SPRITE)) {
        GenerateFrame();
        SDL_RenderPresent(renderer);
    } else {
        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);
    }
    /* No IRQs permitted post-frame; discard anything still queued
     * from this frame's render before NMI enqueues for the next one. */
    irqCount = 0;
    nmi();
}

}   // namespace video

namespace ppu {

void EnableRendering(u8 ppuCtrl_, u8 ppuMask_) {
    ppu::PPUMASK = ppuMask_;
    ppu::PPUCTRL = ppuCtrl_;
}

void Flush(const u8 nt, const u8 at) {

    for (u16 page = 0;
        mode->w / scale < 512 ? page < 2 : page < mode->w / scale;
        page++
    ) {
        for (u16 i = 0; i < 0x3c0; i++) {
            VideoRAM[page * 0x400 + i] = nt;
        }

        for (u16 i = 0; i < 0x40; i++) {
            VideoRAM[page * 0x400 + 0x3c0 + i] = at;
        }
    }
}

void WriteFromBufferToNameTable(
    const u16 x, const u16 y, const u8* source, const u8 sBuffer, u8 polarity
) {
    ppu::PPUCTRL &= ~ppu::ctrl::POLARITY;
    if (polarity) ppu::PPUCTRL |= ppu::ctrl::POLARITY;
    const u16 offset = xy_to_nt_addr(x, y);
    for (u8 i = 0; i < sBuffer; i++) {
        VideoRAM[offset + i * (ppu::PPUCTRL & ppu::ctrl::POLARITY ? 32 : 1)] = source[i];
    }
}

void WriteSingleToNameTable(const u16 x, const u16 y, u8 value) {
    const u16 offset = xy_to_nt_addr(x, y);
    VideoRAM[offset] = value;
}

// Address overload: @p address is the 0-based VRAM offset CartesianToAddress returns
// on this backend (xy_to_nt_addr is already 0-based here), so it indexes VideoRAM
// directly -- the desktop mirror of the NES poke-by-address path.
void WriteSingleToNameTable(const int address, u8 value) {
    VideoRAM[address] = value;
}

void SetScroll(const u16 x, const u16 y) {
    xScroll = x; yScroll = y;
    yScroll_written = 1;
}

void DeltaScroll(const i8 x, const i8 y) {
    xScroll = static_cast<u16>(xScroll + x);
    yScroll = static_cast<u16>(yScroll + y);
    yScroll_written = 1;
}

template <typename Idx>
void WriteFromProviderToNameTable(
    const u16 x, const u16 y, u8 (*fn)(Idx), const u8 amt,
    const u8 polarity
) {
    ppu::PPUCTRL &= ~ppu::ctrl::POLARITY;
    if (polarity) ppu::PPUCTRL |= ppu::ctrl::POLARITY;

    const u16 offset = xy_to_nt_addr(x, y);
    for (Idx i = 0; i < amt; ++i) {
        VideoRAM[offset + i * (ppu::PPUCTRL & ppu::ctrl::POLARITY ? 32 : 1)] = fn(i);
    }
}

// Explicit instantiations for the provider index types in use. The body writes
// host video RAM, so it must stay in this backend rather than the header.
template void WriteFromProviderToNameTable<u8>(u16, u16, u8 (*)(u8), u8, u8);
template void WriteFromProviderToNameTable<u16>(u16, u16, u8 (*)(u16), u8, u8);

void WriteFromBufferToAttributeTable(
    const u16 x, const u16 y, const u8* source,
    const u8 sBuffer, const u8 polarity
) {
    const u16 offset = xy_to_at_addr(x, y);
    for (u8 i = 0; i < sBuffer; i++) {
        VideoRAM[offset + i * (polarity ? 8 : 1)] = source[i];
    }
}

void WriteSingleToAttributeTable(const u16 x, const u16 y, const u8 value) {
    const u16 offset = xy_to_at_addr(x, y);
    VideoRAM[offset] = value;
}

u16 CartesianToAddress(const u16 x, const u16 y) {
    return xy_to_nt_addr(x, y);
}

scroll_t CartesianToScroll(const u16 px, const u16 py) {
    return (scroll_t){ .x = px, .y = py };
}

void SetColorPriority(const u8 priority) {
    ppu::PPUMASK = (ppu::PPUMASK & ~(ppu::mask::RED | ppu::mask::GREEN | ppu::mask::BLUE)) |
        (priority & (ppu::mask::RED | ppu::mask::GREEN | ppu::mask::BLUE)
    );
}

namespace pal {

void WriteFromBuffer(const u8 offset, const u8* source, const u8 sBuffer) {
    memcpy(paletteRAM + offset, source, sBuffer);
}

void WriteSingle(const u8 offset, const u8 value) {
    paletteRAM[offset] = value;
}

}   // namespace pal

}   // namespace ppu

namespace oam {

void OAMFromBuffer(sprite_t* oam, const u8 slot, const u16 off,
                   const u8 width, const u8* src, const u16 count) {
    u8* dst = reinterpret_cast<u8 *>(oam) + static_cast<size_t>(slot) * spriteStride + off;
    const u8* s = src + off;
    for (u16 i = 0; i < count; i++)
        memcpy(dst + static_cast<size_t>(i) * spriteStride, s + static_cast<size_t>(i) * spriteStride, width);
}

void OAMFromProvider(sprite_t* oam, const u8 slot, const u16 off,
                     const u8 width, oam_t (*fn)(u16), const u16 count) {
    u8* base = reinterpret_cast<u8 *>(oam) + static_cast<size_t>(slot) * spriteStride + off;
    for (u16 i = 0; i < count; i++) {
        oam_t v = fn(i);
        memcpy(base + static_cast<size_t>(i) * spriteStride, &v, width);  /* low `width` bytes (LE) */
    }
}

/* SDL analogue of OAMDMA: freeze the passed OAM buffer into the PPU-side
 * snapshot that GenerateFrame renders from. Called from the app's NMI. */
void RefreshSprites(const sprite_t* oam) {
    memcpy(oamShadow, oam, OAM_SPRITES * sizeof(struct sprite_t));
}

}   // namespace oam

namespace ppu {

void StreamFromVideoMemory(const u16 offset, atomic u8* target, const u8 size) {
    for (u8 i = 0; i < size; i++) {
        target[i] = VideoRAM[offset + i];
    }
}

}   // namespace ppu

void WaitThenReactToSpriteZero(const u16 px, const u16 py, void (*fn)(), atomic u8* latch) {
    *latch = true;
    SetSpriteZeroHandler(px, py, fn);
    SetNextIRQHandler((irq_t){ .id = SPRITE_ZERO_IRQ_ID, .px = px, .py = py });
}

void init() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return;
    }

    const SDL_DisplayID display = SDL_GetPrimaryDisplay();
    mode = SDL_GetCurrentDisplayMode(display);

    paletteRAM = static_cast<u8 *>(malloc(32));

#ifdef LANDSCAPE
    scale = mode->h / 240;
    VideoRAM = static_cast<u8 *>(malloc(

        mode->w / scale < 512 ? 0x800 : mode->w / scale * 0x400
    ));
#endif
#if PORTRAIT
    scale = mode->w / 256;
    VideoRAM = (u8 *)malloc(
        mode->h / scale < 480 ? 0x800 : mode->w / scale * 0x400
    );
#endif


    if (!SDL_CreateWindowAndRenderer(PROJECT_NAME, mode->w >> 1, mode->h >> 1, 0, &window, &renderer)) {
        SDL_Log("Window creation failed: %s", SDL_GetError());
        return;
    }
    timer_id = SDL_AddTimer(16, vblank_tick, nullptr);
}

void post() {
    SDL_RemoveTimer(timer_id);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}
