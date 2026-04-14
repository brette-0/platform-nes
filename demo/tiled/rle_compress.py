from os      import listdir
from pathlib import Path
from sys     import argv
from json    import load

def __main__() -> None:
    if len(argv) < 3:
        print("Usage: python script.py <input_dir> <output_dir>")
        return

    input_path = Path(argv[1])
    output_path = Path(argv[2])

    tsjs : list[Path] = [input_path / fp for fp in listdir(input_path) if fp.endswith(('.json', '.tmj'))]

    for tsj in tsjs:
        with open(tsj, "r") as f:
            tsj_content = load(f)

        height   : int = tsj_content["height"]
        raw_data : list[int] = tsj_content["layers"][0]["data"]
        width    : int = len(raw_data) // height

        tile_data : list[int] = []
        for x in range(width):
            tile_data.extend(raw_data[x::width])

        out_s_list : list[str] = []
        out_c_list : list[str] = []
        
        ctx  : int = tile_data[0]
        size : int = 0

        for i in range(len(tile_data)):
            if tile_data[i] != ctx or size == 0xff:
                out_c_list.append(f"0x{ctx:02x}")
                out_s_list.append(f"0x{size:02x}")
                ctx = tile_data[i]
                size = 0
            size += 1
        
        out_c_list.append(f"0x{ctx:02x}")
        out_s_list.append(f"0x{size:02x}")

        out_c = ", ".join(out_c_list)
        out_s = ", ".join(out_s_list)

        with open(output_path / f"{tsj.stem}_c", "w") as f:
            f.write(out_c)

        with open(output_path / f"{tsj.stem}_s", "w") as f:
            f.write(out_s)

if __name__ == "__main__":
    __main__()