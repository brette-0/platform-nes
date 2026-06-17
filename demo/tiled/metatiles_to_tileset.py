#!/usr/bin/env python3
"""Render the demo's metatile table into one PNG tileset for Tiled.

WHAT THIS DOES
    Each metatile id (0..255) is a 2x2 arrangement of CHR tiles plus a 2-bit
    background-palette index (its attribute).  This script composites every
    metatile into a 16x16 cell and lays all 256 out in a 16x16 grid -> a single
    256x256 PNG whose tile order matches the metatile ids exactly (Tiled
    localid == metatile id).  A companion .tsx is emitted so Tiled can import it
    with no manual tile-size fiddling.

WHERE THE DATA COMES FROM
    Everything NES-side is read straight out of a *debug* build's pass-1 ELF
    (demo_pass1.elf) by symbol name -- contents only, so the provisional pass-1
    offsets are irrelevant:
        Metatiles_UL/UR/BL/BR  four 256-byte planes: the corner CHR-tile ids
        Metatiles_ATTR         256-byte plane: 2-bit BG sub-palette per id
        BGColours              16 bytes: 4 sub-palettes x 4 NES colour indices
        chr_rom_image          8 KB: the cartridge CHR (two 4 KB pattern tables)
    The NES colour-index -> RGB master palette is parsed from the desktop
    renderer (src/SDL3/video.cpp, `nes_rgb[64]`) so the PNG matches exactly what
    the SDL build paints on screen.  (Both have an embedded fallback.)

    Nothing here cares about static vs dynamic layers -- it just visualises the
    metatile table as authored.  Metatiles whose ids point at empty CHR render
    blank; that is the truth of the data, not a bug in the tool.

USAGE
    python3 metatiles_to_tileset.py                # auto-discovers ELF + paths
    python3 metatiles_to_tileset.py --elf path/to/demo_pass1.elf
    python3 metatiles_to_tileset.py --transparent  # backdrop (cidx 0) -> alpha 0
    python3 metatiles_to_tileset.py --scale 4       # 4x preview (NOT for Tiled)

Requires Pillow (pip install pillow).
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
from pathlib import Path

# --------------------------------------------------------------------------- #
# Repo layout: this script lives in demo/tiled/, so the repo root is two up.
# --------------------------------------------------------------------------- #
SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[1]

# Symbols pulled from the ELF, with the size we expect each to be.  Size is a
# sanity check / fallback only -- the real size comes from the symbol table.
METATILE_PLANES = ("Metatiles_UL", "Metatiles_UR", "Metatiles_BL", "Metatiles_BR")
ATTR_PLANE = "Metatiles_ATTR"
BGCOLOURS_SYM = "BGColours"
CHR_SYM = "chr_rom_image"

# Standard 2C02-ish NES master palette (ARGB, 0xFFrrggbb).  Used only if the
# renderer's nes_rgb[] can't be parsed; kept byte-identical to src/SDL3/video.cpp.
FALLBACK_NES_RGB = [
    0xFF626262, 0xFF012090, 0xFF1B0CA4, 0xFF3B009E, 0xFF520080, 0xFF5A004E,
    0xFF521610, 0xFF3F2E00, 0xFF234400, 0xFF0A5200, 0xFF005804, 0xFF004E30,
    0xFF003C62, 0xFF000000, 0xFF000000, 0xFF000000, 0xFFABABAB, 0xFF1F56D8,
    0xFF423CF2, 0xFF6E24EC, 0xFF9218C4, 0xFF9E1A80, 0xFF933434, 0xFF7A5200,
    0xFF576E00, 0xFF2E8400, 0xFF118E0E, 0xFF008848, 0xFF007898, 0xFF000000,
    0xFF000000, 0xFF000000, 0xFFFBFBFB, 0xFF6BA4FF, 0xFF8C88FF, 0xFFB87AFF,
    0xFFE072FF, 0xFFF076D0, 0xFFE88C78, 0xFFCCA830, 0xFFA8C410, 0xFF7EDC24,
    0xFF5AE84E, 0xFF48E490, 0xFF48D4E0, 0xFF4E4E4E, 0xFF000000, 0xFF000000,
    0xFFFBFBFB, 0xFFBED4FF, 0xFFCACAFF, 0xFFDCC4FF, 0xFFECC0FF, 0xFFF2C0EA,
    0xFFF2C8C4, 0xFFE8D4A4, 0xFFD8E09C, 0xFFC8EC9C, 0xFFBCF0AC, 0xFFB4F0CC,
    0xFFB4E8F0, 0xFFB8B8B8, 0xFF000000, 0xFF000000,
]


# --------------------------------------------------------------------------- #
# Minimal ELF32 (little-endian) reader: just enough to map a global symbol to
# its bytes.  Avoids a pyelftools dependency; the mos ELF is always ELF32-LE.
# --------------------------------------------------------------------------- #
class Elf:
    def __init__(self, path: Path):
        self.data = path.read_bytes()
        self.path = path
        if self.data[:4] != b"\x7fELF":
            raise ValueError(f"{path}: not an ELF file")
        if self.data[4] != 1 or self.data[5] != 1:
            raise ValueError(f"{path}: expected 32-bit little-endian ELF")

        # ELF32 header fields we need.
        (e_shoff,) = struct.unpack_from("<I", self.data, 0x20)
        (e_shentsize, e_shnum, e_shstrndx) = struct.unpack_from("<HHH", self.data, 0x2E)

        # Parse the section header table.
        self.sections = []  # (name, sh_type, sh_addr, sh_offset, sh_size)
        raw_sections = []
        for i in range(e_shnum):
            base = e_shoff + i * e_shentsize
            (sh_name, sh_type, _flags, sh_addr, sh_offset, sh_size) = struct.unpack_from(
                "<IIIIII", self.data, base
            )
            raw_sections.append((sh_name, sh_type, sh_addr, sh_offset, sh_size))

        # Resolve section names via the section-header string table.
        shstr_off = raw_sections[e_shstrndx][3]
        for (sh_name, sh_type, sh_addr, sh_offset, sh_size) in raw_sections:
            self.sections.append(
                (self._cstr(shstr_off + sh_name), sh_type, sh_addr, sh_offset, sh_size)
            )

        # Locate .symtab (type 2) and its linked string table (sh_link).
        symtab = next((s for s in self.sections if s[0] == ".symtab"), None)
        strtab = next((s for s in self.sections if s[0] == ".strtab"), None)
        if symtab is None or strtab is None:
            raise ValueError(
                f"{path}: no .symtab/.strtab -- need a debug (unstripped) ELF"
            )

        # Build name -> (value, size, shndx) over every symbol.
        self.symbols = {}
        sym_off, sym_size = symtab[3], symtab[4]
        str_off = strtab[3]
        for off in range(sym_off, sym_off + sym_size, 16):
            (st_name, st_value, st_size, _info, _other, st_shndx) = struct.unpack_from(
                "<IIIBBH", self.data, off
            )
            name = self._cstr(str_off + st_name)
            if name:
                self.symbols[name] = (st_value, st_size, st_shndx)

    def _cstr(self, off: int) -> str:
        end = self.data.index(b"\x00", off)
        return self.data[off:end].decode("ascii", "replace")

    def symbol_bytes(self, name: str, expected: int | None = None) -> bytes:
        if name not in self.symbols:
            raise KeyError(f"{self.path}: symbol '{name}' not found")
        value, size, shndx = self.symbols[name]
        if size == 0 and expected is not None:
            size = expected
        # Prefer the symbol's own section; fall back to address containment.
        sec = self.sections[shndx] if 0 <= shndx < len(self.sections) else None
        if not (sec and sec[2] <= value < sec[2] + sec[4]):
            sec = next(
                (s for s in self.sections if s[3] and s[2] <= value < s[2] + s[4]), None
            )
        if sec is None:
            raise ValueError(f"{self.path}: no section contains symbol '{name}'")
        file_off = sec[3] + (value - sec[2])
        data = self.data[file_off : file_off + size]
        if expected is not None and len(data) != expected:
            raise ValueError(
                f"{self.path}: '{name}' is {len(data)} bytes, expected {expected}"
            )
        return data


# --------------------------------------------------------------------------- #
# Inputs: master palette + ELF discovery.
# --------------------------------------------------------------------------- #
def parse_nes_rgb(video_cpp: Path) -> list[int]:
    """Pull the 64-entry nes_rgb[] master palette out of the SDL renderer."""
    try:
        text = video_cpp.read_text()
    except OSError:
        return FALLBACK_NES_RGB
    m = re.search(r"nes_rgb\s*\[\s*64\s*\]\s*=\s*\{(.*?)\}", text, re.S)
    if not m:
        return FALLBACK_NES_RGB
    vals = [int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]{8}", m.group(1))]
    return vals if len(vals) == 64 else FALLBACK_NES_RGB


def discover_elf() -> Path | None:
    """Prefer a debug pass-1 ELF; fall back to any *pass1.elf or *.elf."""
    candidates = [
        REPO_ROOT / "cmake-build-nes-debug" / "demo_pass1.elf",
        REPO_ROOT / "cmake-build-nes-release" / "demo_pass1.elf",
    ]
    for c in candidates:
        if c.is_file():
            return c
    for pat in ("**/demo_pass1.elf", "**/demo*.elf"):
        found = sorted(REPO_ROOT.glob(pat))
        if found:
            return found[0]
    return None


# --------------------------------------------------------------------------- #
# CHR decode: one NES 2bpp planar tile -> 8x8 list of colour indices (0..3).
# --------------------------------------------------------------------------- #
def decode_tile(chr_bytes: bytes, base: int, tile_id: int) -> list[list[int]]:
    off = base + tile_id * 16
    plane0 = chr_bytes[off : off + 8]
    plane1 = chr_bytes[off + 8 : off + 16]
    rows = []
    for y in range(8):
        p0 = plane0[y] if y < len(plane0) else 0
        p1 = plane1[y] if y < len(plane1) else 0
        row = []
        for x in range(8):
            bit = 7 - x
            row.append(((p0 >> bit) & 1) | (((p1 >> bit) & 1) << 1))
        rows.append(row)
    return rows


def tile_is_empty(chr_bytes: bytes, base: int, tile_id: int) -> bool:
    off = base + tile_id * 16
    return not any(chr_bytes[off : off + 16])


def choose_bg_base(chr_bytes: bytes, planes: list[bytes]) -> tuple[int, dict]:
    """Pick the pattern-table half ($0000 or $1000) the metatiles actually use.

    Counts, for each candidate base, how many *referenced* corner tile ids land
    on non-empty CHR.  The demo sets PPUCTRL BG_ADDR ($1000), but if the CHR was
    authored into the low half this auto-detect keeps the tool honest.  Override
    with --bg-base.
    """
    referenced = {planes[c][i] for i in range(256) for c in range(4)}
    referenced.discard(0)  # tile 0 is the shared air/blank tile
    stats = {}
    for base in (0x0000, 0x1000):
        hit = sum(1 for t in referenced if not tile_is_empty(chr_bytes, base, t))
        stats[base] = hit
    best = max(stats, key=lambda b: (stats[b], -b))  # ties -> low half
    return best, {"referenced": len(referenced), "coverage": stats}


# --------------------------------------------------------------------------- #
# Compose the tileset image.
# --------------------------------------------------------------------------- #
def build_image(elf: Elf, nes_rgb: list[int], bg_base: int, transparent: bool):
    from PIL import Image

    planes = [elf.symbol_bytes(p, 256) for p in METATILE_PLANES]
    attr = elf.symbol_bytes(ATTR_PLANE, 256)
    bgcolours = elf.symbol_bytes(BGCOLOURS_SYM, 16)
    chr_bytes = elf.symbol_bytes(CHR_SYM, 0x2000)

    if bg_base is None:
        bg_base, stats = choose_bg_base(chr_bytes, planes)
    else:
        _, stats = choose_bg_base(chr_bytes, planes)

    img = Image.new("RGBA", (16 * 16, 16 * 16), (0, 0, 0, 0))
    px = img.load()

    # Corner -> (cell-x, cell-y) origin within the 16x16 metatile.
    corners = ((0, 0), (8, 0), (0, 8), (8, 8))  # UL, UR, BL, BR

    nonblank = 0
    for mid in range(256):
        pal = attr[mid] & 0x03
        cell_col, cell_row = mid % 16, mid // 16
        ox, oy = cell_col * 16, cell_row * 16
        any_opaque = False
        for corner, (cx, cy) in enumerate(corners):
            tile_id = planes[corner][mid]
            pixels = decode_tile(chr_bytes, bg_base, tile_id)
            for y in range(8):
                for x in range(8):
                    cidx = pixels[y][x]
                    if cidx == 0 and transparent:
                        continue  # leave fully transparent
                    if cidx != 0:
                        any_opaque = True
                    # Faithful to the renderer: paletteRAM[pal*4 + cidx].
                    nes_index = bgcolours[pal * 4 + cidx] & 0x3F
                    argb = nes_rgb[nes_index]
                    rgba = (
                        (argb >> 16) & 0xFF,
                        (argb >> 8) & 0xFF,
                        argb & 0xFF,
                        0xFF,
                    )
                    px[ox + cx + x, oy + cy + y] = rgba
        if any_opaque:
            nonblank += 1

    return img, bg_base, stats, nonblank


def write_tsx(path: Path, png_name: str, scale: int) -> None:
    tw = th = 16 * scale
    dim = 16 * 16 * scale
    path.write_text(
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        f'<tileset version="1.10" tiledversion="1.10.2" name="{path.stem}" '
        f'tilewidth="{tw}" tileheight="{th}" tilecount="256" columns="16">\n'
        f' <image source="{png_name}" width="{dim}" height="{dim}"/>\n'
        "</tileset>\n"
    )


# --------------------------------------------------------------------------- #
def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--elf", type=Path, default=None,
                    help="debug pass-1 ELF (default: auto-discover demo_pass1.elf)")
    ap.add_argument("--video-cpp", type=Path,
                    default=REPO_ROOT / "src" / "SDL3" / "video.cpp",
                    help="renderer source to read nes_rgb[] from")
    ap.add_argument("--out-png", type=Path, default=SCRIPT_DIR / "metatiles.png")
    ap.add_argument("--out-tsx", type=Path, default=SCRIPT_DIR / "metatiles.tsx")
    ap.add_argument("--no-tsx", action="store_true", help="skip the .tsx companion")
    ap.add_argument("--bg-base", choices=("auto", "0", "0x1000"), default="auto",
                    help="CHR pattern-table half for BG tiles (default: auto-detect)")
    ap.add_argument("--transparent", action="store_true",
                    help="render backdrop (colour index 0) as transparent")
    ap.add_argument("--scale", type=int, default=1,
                    help="integer upscale for previewing (NOT for Tiled import)")
    args = ap.parse_args()

    try:
        from PIL import Image  # noqa: F401
    except ImportError:
        print("error: this script needs Pillow  (pip install pillow)", file=sys.stderr)
        return 2

    elf_path = args.elf or discover_elf()
    if elf_path is None or not elf_path.is_file():
        print("error: no ELF found. Build a debug NES target (it produces "
              "demo_pass1.elf) or pass --elf PATH.", file=sys.stderr)
        return 2

    try:
        elf = Elf(elf_path)
    except (ValueError, OSError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 2

    nes_rgb = parse_nes_rgb(args.video_cpp)
    bg_base = None if args.bg_base == "auto" else int(args.bg_base, 16)

    try:
        img, bg_base, stats, nonblank = build_image(
            elf, nes_rgb, bg_base, args.transparent
        )
    except (KeyError, ValueError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 2

    if args.scale > 1:
        from PIL import Image
        img = img.resize((img.width * args.scale, img.height * args.scale),
                         Image.NEAREST)

    args.out_png.parent.mkdir(parents=True, exist_ok=True)
    img.save(args.out_png)
    if not args.no_tsx:
        write_tsx(args.out_tsx, args.out_png.name, args.scale)

    # Report what happened -- coverage makes stale metatile ids obvious.
    print(f"elf          : {elf_path}")
    print(f"palette      : {'nes_rgb[] from ' + str(args.video_cpp) if nes_rgb is not FALLBACK_NES_RGB else 'embedded fallback'}")
    cov = stats["coverage"]
    print(f"bg pattern   : ${bg_base:04x}  "
          f"(coverage  $0000:{cov[0x0000]}  $1000:{cov[0x1000]}  "
          f"of {stats['referenced']} referenced tile ids)")
    print(f"metatiles    : {nonblank}/256 render with art "
          f"({256 - nonblank} blank -- air or ids pointing at empty CHR)")
    print(f"wrote        : {args.out_png}  ({img.width}x{img.height})")
    if not args.no_tsx:
        print(f"wrote        : {args.out_tsx}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
