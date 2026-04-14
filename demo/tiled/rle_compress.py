from os      import listdir
from pathlib import Path
from sys     import argv
from json    import load

def __main__() -> None:
    tsjs : list[Path] = [Path(fp) for fp in listdir(argv[1])]

    for tsj in tsjs:
        tsj_content : dict[str, list[dict[str, list[int]]]]
        with open(f"{argv[1]}/{tsj}", "r") as f:
            tsj_content = load(f)
        

        tile_data : list[int] = tsj_content["layers"][0]["data"]

        # rle encode

        out_s  : str = ""
        out_c  : str = ""
        ctx  : int = tile_data[0]
        size : int = 0
        for i in range(1, len(tile_data)):
            if tile_data[i] is not ctx or size == 0xff:
                out_c += f"0x{ctx:02x}, "
                out_s += f"0x{size:02x}, "
                ctx = tile_data[i]
                size = 0
            size += 1

        out_s = out_s[:-2]
        out_c = out_c[:-2]

        with open(f"{argv[2]}/{tsj.stem}_c", "w") as f:
            f.write(out_c)

        with open(f"{argv[2]}/{tsj.stem}_s", "w") as f:
            f.write(out_s)


if __name__ == "__main__":
    __main__()