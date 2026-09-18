import argparse
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description="Embed binary data as a C array.")
    parser.add_argument("--format", choices=("bytes", "u32le"), default="bytes")
    parser.add_argument("input_path", type=Path)
    parser.add_argument("output_path", type=Path)
    parser.add_argument("variable_name")
    args = parser.parse_args()
    data = args.input_path.read_bytes()
    width = 4 if args.format == "u32le" else 1
    if not data or len(data) % width:
        parser.error(f"Input must contain a nonzero multiple of {width} bytes.")
    with args.output_path.open("w", encoding="utf-8") as output:
        output.write("#include <stddef.h>\n#include <stdint.h>\n\n")
        output.write(f"const uint{width * 8}_t {args.variable_name}Data[] = {{\n")
        for start in range(0, len(data), 16):
            values = [
                int.from_bytes(data[index : index + width], "little")
                for index in range(start, min(start + 16, len(data)), width)
            ]
            output.write("    " + ", ".join(f"0x{value:0{width * 2}x}u" for value in values) + ",\n")
        output.write(f"}};\nconst size_t {args.variable_name}Size = sizeof({args.variable_name}Data);\n")


if __name__ == "__main__":
    main()
