#!/usr/bin/env python3
"""
smb1_metatiles.py — Render every SMB1 background metatile from a NES .chr file.

Metatile byte in MetatileBuffer: %PPmmmmmm
  PP     = BG palette index (0-3), selects Palette{N}_MTiles table
  mmmmmm = metatile index within that table

Each metatile = 4 CHR tile indices written in column-mode (vertical VRAM writes):
  Byte 0 = Upper-Left     Byte 2 = Upper-Right
  Byte 1 = Lower-Left     Byte 3 = Lower-Right

Palette system:
  Base loaded from AreaType (Water / Ground / Underground / Castle).
  BG pal 0 ($3F00) overwritten by: Mushroom / DaySnow / NightSnow.
  BG pal 3 ($3F0C) always overwritten per-AreaType (Palette3Data + color rotation).
  Sprite pal 1 ($3F14) overwritten by Bowser when Axe object present.
  Sprite pal 0 ($3F10) always = player colours.

Usage:  python smb1_metatiles.py <file.chr>

Output:
  metatiles_out/bg/<areatype>[_<override>]/  — BG metatile sheets + palette swatch
  metatiles_out/spr/<areatype>[_bowser]/     — sprite pattern table sheets
"""

import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow required:  pip install Pillow")

# ═══════════════════════════════════════════════════════════════════════════
# NES SYSTEM PALETTE (standard NTSC, 64 entries)
# ═══════════════════════════════════════════════════════════════════════════

NES_RGB = [
    (0x62,0x62,0x62),(0x00,0x2E,0x98),(0x12,0x12,0xB2),(0x3A,0x08,0x9C),
    (0x52,0x02,0x6E),(0x5A,0x04,0x16),(0x4C,0x10,0x00),(0x32,0x24,0x00),
    (0x16,0x36,0x00),(0x00,0x42,0x00),(0x00,0x44,0x00),(0x00,0x3C,0x14),
    (0x00,0x30,0x58),(0x00,0x00,0x00),(0x00,0x00,0x00),(0x00,0x00,0x00),
    (0xAA,0xAA,0xAA),(0x0C,0x5C,0xD8),(0x32,0x40,0xFC),(0x6E,0x2C,0xF4),
    (0x92,0x22,0xBC),(0x9E,0x20,0x5C),(0x90,0x2C,0x08),(0x6E,0x46,0x00),
    (0x46,0x60,0x00),(0x1E,0x74,0x00),(0x04,0x7A,0x00),(0x00,0x72,0x2C),
    (0x00,0x64,0x82),(0x00,0x00,0x00),(0x00,0x00,0x00),(0x00,0x00,0x00),
    (0xFC,0xFC,0xFC),(0x4E,0xAE,0xFC),(0x72,0x90,0xFC),(0xA8,0x7C,0xFC),
    (0xD2,0x72,0xFC),(0xDE,0x70,0xA8),(0xD6,0x78,0x48),(0xBA,0x94,0x00),
    (0x8E,0xAE,0x00),(0x5E,0xC4,0x00),(0x38,0xCC,0x2C),(0x2C,0xC4,0x7C),
    (0x2E,0xB4,0xCC),(0x3C,0x3C,0x3C),(0x00,0x00,0x00),(0x00,0x00,0x00),
    (0xFC,0xFC,0xFC),(0xB2,0xD8,0xFC),(0xC0,0xC8,0xFC),(0xD4,0xC0,0xFC),
    (0xE8,0xBA,0xFC),(0xF0,0xBA,0xD8),(0xEC,0xC0,0xAC),(0xDE,0xCC,0x86),
    (0xCC,0xD8,0x74),(0xB6,0xE0,0x76),(0xA8,0xE4,0x90),(0xA2,0xE0,0xB4),
    (0xA4,0xDA,0xDC),(0xA8,0xA8,0xA8),(0x00,0x00,0x00),(0x00,0x00,0x00),
]

# ═══════════════════════════════════════════════════════════════════════════
# SMB1 METATILE TABLES  (from disassembly)
# Each entry: (UL, DL, UR, DR), "name"
#   UL = upper-left tile    UR = upper-right tile
#   DL = lower-left tile    DR = lower-right tile
# (column-mode VRAM write order: left column top-to-bottom, then right)
# ═══════════════════════════════════════════════════════════════════════════

Palette0_MTiles = [
    ((0x24,0x24,0x24,0x24), "blank"),
    ((0x27,0x27,0x27,0x27), "black metatile"),
    ((0x24,0x24,0x24,0x35), "bush left"),
    ((0x36,0x25,0x37,0x25), "bush middle"),
    ((0x24,0x38,0x24,0x24), "bush right"),
    ((0x24,0x30,0x30,0x26), "mountain left"),
    ((0x26,0x26,0x34,0x26), "mountain left bottom/middle"),
    ((0x24,0x31,0x24,0x32), "mountain middle top"),
    ((0x33,0x26,0x24,0x33), "mountain right"),
    ((0x34,0x26,0x26,0x26), "mountain right bottom"),
    ((0x26,0x26,0x26,0x26), "mountain middle bottom"),
    ((0x24,0xC0,0x24,0xC0), "bridge guardrail"),
    ((0x24,0x7F,0x7F,0x24), "chain"),
    ((0xB8,0xBA,0xB9,0xBB), "tall tree top, top half"),
    ((0xB8,0xBC,0xB9,0xBD), "short tree top"),
    ((0xBA,0xBC,0xBB,0xBD), "tall tree top, bottom half"),
    ((0x60,0x64,0x61,0x65), "warp pipe end left"),
    ((0x62,0x66,0x63,0x67), "warp pipe end right"),
    ((0x60,0x64,0x61,0x65), "deco pipe end left"),
    ((0x62,0x66,0x63,0x67), "deco pipe end right"),
    ((0x68,0x68,0x69,0x69), "pipe shaft left"),
    ((0x26,0x26,0x6A,0x6A), "pipe shaft right"),
    ((0x4B,0x4C,0x4D,0x4E), "tree ledge left"),
    ((0x4D,0x4F,0x4D,0x4F), "tree ledge middle"),
    ((0x4D,0x4E,0x50,0x51), "tree ledge right"),
    ((0x6B,0x70,0x2C,0x2D), "mushroom left"),
    ((0x6C,0x71,0x6D,0x72), "mushroom middle"),
    ((0x6E,0x73,0x6F,0x74), "mushroom right"),
    ((0x86,0x8A,0x87,0x8B), "sideways pipe end top"),
    ((0x88,0x8C,0x88,0x8C), "sideways pipe shaft top"),
    ((0x89,0x8D,0x69,0x69), "sideways pipe joint top"),
    ((0x8E,0x91,0x8F,0x92), "sideways pipe end bottom"),
    ((0x26,0x93,0x26,0x93), "sideways pipe shaft bottom"),
    ((0x90,0x94,0x69,0x69), "sideways pipe joint bottom"),
    ((0xA4,0xE9,0xEA,0xEB), "seaplant"),
    ((0x24,0x24,0x24,0x24), "blank (hit block)"),
    ((0x24,0x2F,0x24,0x3D), "flagpole ball"),
    ((0xA2,0xA2,0xA3,0xA3), "flagpole shaft"),
    ((0x24,0x24,0x24,0x24), "blank (vine)"),
]

Palette1_MTiles = [
    ((0xA2,0xA2,0xA3,0xA3), "vertical rope"),
    ((0x99,0x24,0x99,0x24), "horizontal rope"),
    ((0x24,0xA2,0x3E,0x3F), "left pulley"),
    ((0x5B,0x5C,0x24,0xA3), "right pulley"),
    ((0x24,0x24,0x24,0x24), "blank (balance rope)"),
    ((0x9D,0x47,0x9E,0x47), "castle top"),
    ((0x47,0x47,0x27,0x27), "castle window left"),
    ((0x47,0x47,0x47,0x47), "castle brick wall"),
    ((0x27,0x27,0x47,0x47), "castle window right"),
    ((0xA9,0x47,0xAA,0x47), "castle top w/ brick"),
    ((0x9B,0x27,0x9C,0x27), "entrance top"),
    ((0x27,0x27,0x27,0x27), "entrance bottom"),
    ((0x52,0x52,0x52,0x52), "green ledge stump"),
    ((0x80,0xA0,0x81,0xA1), "fence"),
    ((0xBE,0xBE,0xBF,0xBF), "tree trunk"),
    ((0x75,0xBA,0x76,0xBB), "mushroom stump top"),
    ((0xBA,0xBA,0xBB,0xBB), "mushroom stump bottom"),
    ((0x45,0x47,0x45,0x47), "breakable brick w/ line"),
    ((0x47,0x47,0x47,0x47), "breakable brick"),
    ((0x45,0x47,0x45,0x47), "breakable brick (unused)"),
    ((0xB4,0xB6,0xB5,0xB7), "cracked rock terrain"),
    ((0x45,0x47,0x45,0x47), "brick w/ line (power-up)"),
    ((0x45,0x47,0x45,0x47), "brick w/ line (vine)"),
    ((0x45,0x47,0x45,0x47), "brick w/ line (star)"),
    ((0x45,0x47,0x45,0x47), "brick w/ line (coins)"),
    ((0x45,0x47,0x45,0x47), "brick w/ line (1-up)"),
    ((0x47,0x47,0x47,0x47), "brick (power-up)"),
    ((0x47,0x47,0x47,0x47), "brick (vine)"),
    ((0x47,0x47,0x47,0x47), "brick (star)"),
    ((0x47,0x47,0x47,0x47), "brick (coins)"),
    ((0x47,0x47,0x47,0x47), "brick (1-up)"),
    ((0x24,0x24,0x24,0x24), "hidden block (1 coin)"),
    ((0x24,0x24,0x24,0x24), "hidden block (1-up)"),
    ((0xAB,0xAC,0xAD,0xAE), "solid block (3-D)"),
    ((0x5D,0x5E,0x5D,0x5E), "solid block (white wall)"),
    ((0xC1,0x24,0xC1,0x24), "bridge"),
    ((0xC6,0xC8,0xC7,0xC9), "bullet bill barrel"),
    ((0xCA,0xCC,0xCB,0xCD), "bullet bill top"),
    ((0x2A,0x2A,0x40,0x40), "bullet bill bottom"),
    ((0x24,0x24,0x24,0x24), "blank (jumpspring)"),
    ((0x24,0x47,0x24,0x47), "half brick (jumpspring)"),
    ((0x82,0x83,0x84,0x85), "solid block (water, green rock)"),
    ((0x24,0x47,0x24,0x47), "half brick (???)"),
    ((0x86,0x8A,0x87,0x8B), "water pipe top"),
    ((0x8E,0x91,0x8F,0x92), "water pipe bottom"),
    ((0x24,0x2F,0x24,0x3D), "flag ball (residual)"),
]

Palette2_MTiles = [
    ((0x24,0x24,0x24,0x35), "cloud left"),
    ((0x36,0x25,0x37,0x25), "cloud middle"),
    ((0x24,0x38,0x24,0x24), "cloud right"),
    ((0x24,0x24,0x39,0x24), "cloud bottom left"),
    ((0x3A,0x24,0x3B,0x24), "cloud bottom middle"),
    ((0x3C,0x24,0x24,0x24), "cloud bottom right"),
    ((0x41,0x26,0x41,0x26), "water/lava top"),
    ((0x26,0x26,0x26,0x26), "water/lava"),
    ((0xB0,0xB1,0xB2,0xB3), "cloud level terrain"),
    ((0x77,0x79,0x77,0x79), "bowser's bridge"),
]

Palette3_MTiles = [
    ((0x53,0x55,0x54,0x56), "question block (coin)"),
    ((0x53,0x55,0x54,0x56), "question block (power-up)"),
    ((0xA5,0xA7,0xA6,0xA8), "coin"),
    ((0xC2,0xC4,0xC3,0xC5), "underwater coin"),
    ((0x57,0x59,0x58,0x5A), "empty block"),
    ((0x7B,0x7D,0x7C,0x7E), "axe"),
]

METATILE_TABLES = [Palette0_MTiles, Palette1_MTiles, Palette2_MTiles, Palette3_MTiles]

# ═══════════════════════════════════════════════════════════════════════════
# SMB1 AREA PALETTES
# Full 32-byte writes: BG pals 0-3 ($3F00-$3F0F), sprite pals 0-3 ($3F10-$3F1F)
# ═══════════════════════════════════════════════════════════════════════════

AREA_PALETTES = {
    "water": {
        "bg": [
            [0x0F,0x15,0x12,0x25], [0x0F,0x3A,0x1A,0x0F],
            [0x0F,0x30,0x12,0x0F], [0x0F,0x27,0x12,0x0F],
        ],
        "spr": [
            [0x22,0x16,0x27,0x18], [0x0F,0x10,0x30,0x27],
            [0x0F,0x16,0x30,0x27], [0x0F,0x0F,0x30,0x10],
        ],
    },
    "ground": {
        "bg": [
            [0x0F,0x29,0x1A,0x0F], [0x0F,0x36,0x17,0x0F],
            [0x0F,0x30,0x21,0x0F], [0x0F,0x27,0x17,0x0F],
        ],
        "spr": [
            [0x0F,0x16,0x27,0x18], [0x0F,0x1A,0x30,0x27],
            [0x0F,0x16,0x30,0x27], [0x0F,0x0F,0x36,0x17],
        ],
    },
    "underground": {
        "bg": [
            [0x0F,0x29,0x1A,0x09], [0x0F,0x3C,0x1C,0x0F],
            [0x0F,0x30,0x21,0x1C], [0x0F,0x27,0x17,0x1C],
        ],
        "spr": [
            [0x0F,0x16,0x27,0x18], [0x0F,0x1C,0x36,0x17],
            [0x0F,0x16,0x30,0x27], [0x0F,0x0C,0x3C,0x1C],
        ],
    },
    "castle": {
        "bg": [
            [0x0F,0x30,0x10,0x00], [0x0F,0x30,0x10,0x00],
            [0x0F,0x30,0x16,0x00], [0x0F,0x27,0x17,0x00],
        ],
        "spr": [
            [0x0F,0x16,0x27,0x18], [0x0F,0x1C,0x36,0x17],
            [0x0F,0x16,0x30,0x27], [0x0F,0x00,0x30,0x10],
        ],
    },
}

# ── BG palette 0 overrides (4 bytes to $3F00) ─────────────────────────────
BG_PAL0_OVERRIDES = {
    "none":      None,
    "mushroom":  [0x22, 0x27, 0x16, 0x0F],  # AreaStyle == 1
    "daysnow":   [0x22, 0x30, 0x00, 0x10],  # BackgroundColorCtrl == 5
    "nightsnow": [0x0F, 0x30, 0x00, 0x10],  # BackgroundColorCtrl == 6
}

# ── BG palette 3 always overwritten per-AreaType (Palette3Data) ───────────
# Slot 1 from ColorRotatePalette; we bake in $27 (frame 0, most common).
PALETTE3_OVERRIDE = {
    "water":       [0x0F, 0x27, 0x12, 0x0F],
    "ground":      [0x0F, 0x27, 0x17, 0x0F],
    "underground": [0x0F, 0x27, 0x17, 0x1C],
    "castle":      [0x0F, 0x27, 0x17, 0x00],
}

# ── Sprite palette overrides ──────────────────────────────────────────────
PLAYER_COLORS = {
    "mario": [0x22, 0x16, 0x27, 0x18],
    "luigi": [0x22, 0x30, 0x27, 0x19],
    "fiery": [0x22, 0x37, 0x27, 0x16],
}
BOWSER_SPR_PAL1 = [0x0F, 0x1A, 0x30, 0x27]  # $3F14, triggered by Axe object


# ═══════════════════════════════════════════════════════════════════════════
# EFFECTIVE PALETTE BUILDERS
# ═══════════════════════════════════════════════════════════════════════════

def build_bg_palettes(area_type, override="none"):
    """4 effective BG sub-palettes for a given area type + optional BG pal 0 override."""
    pals = [list(sp) for sp in AREA_PALETTES[area_type]["bg"]]
    pals[3] = list(PALETTE3_OVERRIDE[area_type])
    ovr = BG_PAL0_OVERRIDES[override]
    if ovr is not None:
        pals[0] = list(ovr)
    return pals


def build_spr_palettes(area_type, bowser=False):
    """4 effective sprite sub-palettes for a given area type + optional Bowser."""
    pals = [list(sp) for sp in AREA_PALETTES[area_type]["spr"]]
    pals[0] = list(PLAYER_COLORS["mario"])  # always overwritten
    if bowser:
        pals[1] = list(BOWSER_SPR_PAL1)
    return pals


# ═══════════════════════════════════════════════════════════════════════════
# CHR DECODING + RENDERING
# ═══════════════════════════════════════════════════════════════════════════

def decode_chr_tile(chrdata, tile_index):
    """8×8 CHR tile → list of 8 rows of 8 colour indices (0-3)."""
    off = tile_index * 16
    if off + 16 > len(chrdata):
        return [[0]*8 for _ in range(8)]
    raw = chrdata[off:off+16]
    pixels = []
    for y in range(8):
        lo, hi = raw[y], raw[y + 8]
        row = []
        for x in range(8):
            s = 7 - x
            row.append((((hi >> s) & 1) << 1) | ((lo >> s) & 1))
        pixels.append(row)
    return pixels


def render_metatile(chrdata, metatile_tiles, palette):
    """
    Render one 16×16 metatile as RGBA.  Colour index 0 = transparent.
    metatile_tiles order: (UL, DL, UR, DR)
    BG uses pattern table $1000 (CHR tiles 256-511), so offset by 256.
    """
    ul, dl, ur, dr = metatile_tiles
    img = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
    for tile_idx, ox, oy in [(ul, 0, 0), (dl, 0, 8), (ur, 8, 0), (dr, 8, 8)]:
        tile_idx += 256  # BG pattern table = $1000 = second 4KB
        pixels = decode_chr_tile(chrdata, tile_idx)
        for y, row in enumerate(pixels):
            for x, ci in enumerate(row):
                if ci == 0:
                    img.putpixel((ox + x, oy + y), (0, 0, 0, 0))
                else:
                    r, g, b = NES_RGB[palette[ci] & 0x3F]
                    img.putpixel((ox + x, oy + y), (r, g, b, 255))
    return img


def make_swatch(palettes):
    """Render a palette swatch: rows = sub-palettes, cols = 4 colours, 16×16 each."""
    swatch = Image.new("RGBA", (4 * 16, len(palettes) * 16))
    for sp_i, sp in enumerate(palettes):
        for ci, nes_idx in enumerate(sp):
            r, g, b = NES_RGB[nes_idx & 0x3F]
            for py in range(16):
                for px in range(16):
                    swatch.putpixel((ci * 16 + px, sp_i * 16 + py), (r, g, b, 255))
    return swatch


def render_tile_sheet(chrdata, start_tile, count, palette):
    """Render raw CHR tiles (not metatiles) in a 16-column grid."""
    cols = 16
    rows = (count + cols - 1) // cols
    sheet = Image.new("RGBA", (cols * 8, rows * 8), (0, 0, 0, 0))
    for t in range(count):
        pixels = decode_chr_tile(chrdata, start_tile + t)
        ox = (t % cols) * 8
        oy = (t // cols) * 8
        for y, row in enumerate(pixels):
            for x, ci in enumerate(row):
                if ci == 0:
                    sheet.putpixel((ox + x, oy + y), (0, 0, 0, 0))
                else:
                    r, g, b = NES_RGB[palette[ci] & 0x3F]
                    sheet.putpixel((ox + x, oy + y), (r, g, b, 255))
    return sheet


# ═══════════════════════════════════════════════════════════════════════════
# MAIN
# ═══════════════════════════════════════════════════════════════════════════

def main():
    if len(sys.argv) != 2:
        sys.exit(f"Usage: {sys.argv[0]} <file.chr>")

    chrdata = Path(sys.argv[1]).read_bytes()
    ntiles = len(chrdata) // 16
    print(f"CHR: {sys.argv[1]} ({len(chrdata)} bytes, {ntiles} tiles)\n")

    root = Path("metatiles_out")
    total = 0

    # ── Print metatile inventory ──
    for pg, table in enumerate(METATILE_TABLES):
        print(f"  Palette{pg}_MTiles: {len(table)} metatiles")
        for i, (tiles, name) in enumerate(table):
            full_byte = (pg << 6) | i
            ul, dl, ur, dr = tiles
            print(f"    ${full_byte:02X}  UL=${ul:02X} DL=${dl:02X} UR=${ur:02X} DR=${dr:02X}  {name}")
        print()

    # ══════════════════════════════════════════════════════════════════════
    # BG METATILE SHEETS — area type × BG pal 0 override
    # ══════════════════════════════════════════════════════════════════════
    print("=== BG METATILE SHEETS ===\n")
    area_types = ["water", "ground", "underground", "castle"]
    overrides  = ["none", "mushroom", "daysnow", "nightsnow"]

    for at in area_types:
        for ovr in overrides:
            bg_pals = build_bg_palettes(at, ovr)
            dirname = at if ovr == "none" else f"{at}_{ovr}"
            outdir = root / "bg" / dirname
            outdir.mkdir(parents=True, exist_ok=True)

            # Per-palette-group metatile sheets
            for pg in range(4):
                table = METATILE_TABLES[pg]
                pal = bg_pals[pg]
                cols = min(len(table), 16)
                rows = (len(table) + cols - 1) // cols
                sheet = Image.new("RGBA", (cols * 16, rows * 16), (0, 0, 0, 0))
                for i, (tiles, name) in enumerate(table):
                    img = render_metatile(chrdata, tiles, pal)
                    sheet.paste(img, ((i % cols) * 16, (i // cols) * 16))
                sheet.save(outdir / f"metatiles_pal{pg}.png")
                total += 1

            # Combined sheet: all 4 palette groups
            max_cols = 16
            all_entries = []
            for pg in range(4):
                for i, (tiles, name) in enumerate(METATILE_TABLES[pg]):
                    all_entries.append((tiles, bg_pals[pg]))
            n = len(all_entries)
            cols = min(n, max_cols)
            rows = (n + cols - 1) // cols
            combined = Image.new("RGBA", (cols * 16, rows * 16), (0, 0, 0, 0))
            for i, (tiles, pal) in enumerate(all_entries):
                img = render_metatile(chrdata, tiles, pal)
                combined.paste(img, ((i % cols) * 16, (i // cols) * 16))
            combined.save(outdir / "all_metatiles.png")
            total += 1

            # BG palette swatch
            make_swatch(bg_pals).save(outdir / "palette_swatch.png")
            total += 1

            pal_str = " ".join(f"[{'|'.join(f'${c:02X}' for c in sp)}]" for sp in bg_pals)
            print(f"  bg/{dirname:24s}  {pal_str}")

    # ══════════════════════════════════════════════════════════════════════
    # SPRITE TILE SHEETS — area type × bowser override
    # ══════════════════════════════════════════════════════════════════════
    print("\n=== SPRITE TILE SHEETS ===\n")

    spr_tile_start = 0                          # sprites = pattern table $0000 = first 4KB
    spr_tile_count = min(256, ntiles)

    for at in area_types:
        for bowser in [False, True]:
            spr_pals = build_spr_palettes(at, bowser)
            tag = f"{at}_bowser" if bowser else at
            outdir = root / "spr" / tag
            outdir.mkdir(parents=True, exist_ok=True)

            for sp_i in range(4):
                sheet = render_tile_sheet(chrdata, spr_tile_start, spr_tile_count, spr_pals[sp_i])
                sheet.save(outdir / f"sprites_pal{sp_i}.png")
                total += 1

            make_swatch(spr_pals).save(outdir / "palette_swatch.png")
            total += 1

            pal_str = " ".join(f"[{'|'.join(f'${c:02X}' for c in sp)}]" for sp in spr_pals)
            bstr = " +bowser" if bowser else ""
            print(f"  spr/{tag:24s}  {pal_str}")

    print(f"\nDone. {total} PNGs → {root}/")


if __name__ == "__main__":
    main()