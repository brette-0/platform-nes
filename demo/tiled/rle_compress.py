from pathlib import Path
from json    import load

input_path  : Path = Path("exports")
output_path : Path = Path("include")


FLIP_MASK : int = 0x1FFFFFFF   # strips Tiled's H/V/D flip flags from a GID


def decode_gids(raw_data: list[int], firstgids: list[int]) -> list[int]:
    """Tiled stores 1-based *global* tile ids (GID = local id + tileset
    firstgid); an empty cell is GID 0.  Convert each to the engine's 0-based
    metatile id by subtracting the owning tileset's firstgid.  Empty cells map
    to metatile 0 (air)."""
    out : list[int] = []
    for gid in raw_data:
        gid &= FLIP_MASK
        if gid == 0:
            out.append(0)
            continue
        for fg in firstgids:          # firstgids sorted high->low
            if gid >= fg:
                out.append(gid - fg)
                break
        else:
            out.append(0)
    return out


def column_major(raw_data: list[int], height: int) -> list[int]:
    """Tiled stores rows; the engine reads columns top-to-bottom, so transpose
    the flat row-major buffer into column-major order."""
    width : int = len(raw_data) // height
    out   : list[int] = []
    for x in range(width):
        out.extend(raw_data[x::width])
    return out


def rle(tile_data: list[int]) -> tuple[str, str]:
    """Run-length encode into parallel (tiles, lengths) streams.  A run is
    capped at 0xff so each length fits one byte; returns comma-joined hex."""
    out_c_list : list[str] = []
    out_s_list : list[str] = []

    ctx  : int = tile_data[0]
    size : int = 0

    for v in tile_data:
        if v != ctx or size == 0xff:
            out_c_list.append(f"0x{ctx:02x}")
            out_s_list.append(f"0x{size:02x}")
            ctx = v
            size = 0
        size += 1

    out_c_list.append(f"0x{ctx:02x}")
    out_s_list.append(f"0x{size:02x}")

    return ", ".join(out_c_list), ", ".join(out_s_list)


def rle_dynamic(tile_data: list[int]) -> tuple[str, str, int]:
    """RLE the dynamic plane, but ONLY merge air (0x00).  Every non-air tile is
    emitted as its own length-1 run.

    Why air-only: a dynamic tile is consumed by blanking the RUN that owns it
    (RemoveDynamic sets DynData[run]=0), so the run is the removal granularity.
    If adjacent coins shared one run, eating one would erase the whole run --
    every coin touching it.  Air is never consumed, so it compresses freely.

    The summed lengths still equal the cell count (one length-1 run per non-air
    cell, run-merged air otherwise), so a DynamicCursor stays in lockstep with
    the static Cursor cell-for-cell.  Returns (tiles, lengths, run_count)."""
    out_c_list : list[str] = []
    out_s_list : list[str] = []

    i : int = 0
    n : int = len(tile_data)
    while i < n:
        v = tile_data[i]
        if v == 0:                       # merge a run of air (cap at 0xff)
            size = 0
            while i < n and tile_data[i] == 0 and size < 0xff:
                size += 1
                i += 1
            out_c_list.append("0x00")
            out_s_list.append(f"0x{size:02x}")
        else:                            # one consumable tile -> one length-1 run
            out_c_list.append(f"0x{v:02x}")
            out_s_list.append("0x01")
            i += 1

    return ", ".join(out_c_list), ", ".join(out_s_list), len(out_c_list)


def pick_layers(layers: list[dict]) -> tuple[dict, dict]:
    """Return (static, dynamic) layers.  Prefer matching by Tiled layer name;
    if the layers are unnamed, presume first = static, second = dynamic."""
    by_name = {l.get("name", "").lower(): l for l in layers if l.get("name")}
    if "static" in by_name and "dynamic" in by_name:
        return by_name["static"], by_name["dynamic"]
    return layers[0], layers[1]


def __main__() -> None:
    tmjs : list[Path] = list(input_path.iterdir())

    if tmjs:
        print("found files: " + "".join(f"{tmj.name}\n" for tmj in tmjs))
    else:
        print("no tmj files found")
        return

    for tmj in tmjs:
        with open(tmj, "r") as f:
            tmj_content = load(f)

        height : int = tmj_content["height"]
        width  : int = tmj_content["width"]
        static, dynamic = pick_layers(tmj_content["layers"])

        # Tileset firstgids, sorted high->low so the first <= a GID owns it.
        firstgids = sorted((t["firstgid"] for t in tmj_content["tilesets"]),
                           reverse=True)

        st = column_major(decode_gids(static["data"], firstgids), height)
        dt = column_major(decode_gids(dynamic["data"], firstgids), height)

        # Static plane: flat column-major dump, no compression.  The engine
        # streams directly from ROM; no lengths array, no run tracking.
        st_flat = ", ".join(f"0x{v:02x}" for v in st)

        # Dynamic plane: air-only RLE so each consumable tile is its own run.
        dt_c, dt_s, dyn_runs = rle_dynamic(dt)

        # DynData[] is a fixed RAM pool (DynRunCapacity in dynamic.hpp).  Each
        # run costs one byte there, so warn loudly if a level overflows it.
        DYN_RUN_CAPACITY = 128
        if dyn_runs > DYN_RUN_CAPACITY:
            print(f"  WARNING: {tmj.stem} dynamic plane has {dyn_runs} runs > "
                  f"DynRunCapacity ({DYN_RUN_CAPACITY}); raise the RAM pool or "
                  f"thin the layer")
        else:
            print(f"  {tmj.stem}: {dyn_runs} dynamic runs "
                  f"(cap {DYN_RUN_CAPACITY}), static {width*height} bytes flat")

        outputs = {
            f"{tmj.stem}_st": st_flat, # static: flat column-major, no compression
            f"{tmj.stem}_dt": dt_c,    # dynamic tiles (air-only RLE)
            f"{tmj.stem}_dl": dt_s,    # dynamic lengths
        }

        for name, body in outputs.items():
            with open(output_path / name, "w") as f:
                f.write(body)


if __name__ == "__main__":
    __main__()
