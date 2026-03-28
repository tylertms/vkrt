#!/usr/bin/env python3

import argparse
import sys
from pathlib import Path


def write_u32le(handle, variable_name: str, data: bytes) -> None:
    if len(data) % 4 != 0:
        print("u32le size must be a multiple of 4 bytes", file=sys.stderr)
        raise SystemExit(1)

    handle.write("#include <stddef.h>\n")
    handle.write("#include <stdint.h>\n\n")
    handle.write(f"const uint32_t {variable_name}Data[] = {{\n")

    for i in range(0, len(data), 16):
        chunk = data[i : i + 16]
        words = []
        for word_start in range(0, len(chunk), 4):
            words.append(
                int.from_bytes(chunk[word_start : word_start + 4], byteorder="little")
            )
        handle.write("    " + ", ".join(f"0x{word:08x}u" for word in words) + ",\n")

    handle.write("};\n")
    handle.write(f"const size_t {variable_name}Size = sizeof({variable_name}Data);\n")


def write_bytes(handle, variable_name: str, data: bytes) -> None:
    handle.write("#include <stddef.h>\n")
    handle.write("#include <stdint.h>\n\n")
    handle.write(f"const uint8_t {variable_name}Data[] = {{\n")

    for i in range(0, len(data), 12):
        chunk = data[i : i + 12]
        handle.write("    " + ", ".join(f"0x{byte:02x}" for byte in chunk) + ",\n")

    handle.write("};\n")
    handle.write(f"const size_t {variable_name}Size = sizeof({variable_name}Data);\n")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Embed binary data in a C source file."
    )
    parser.add_argument(
        "--format",
        choices=("bytes", "u32le"),
        default="bytes",
        help="Array element format used in the generated C source.",
    )
    parser.add_argument("input_path")
    parser.add_argument("output_path")
    parser.add_argument("variable_name")
    args = parser.parse_args()

    data = Path(args.input_path).read_bytes()

    with open(args.output_path, "w", encoding="utf-8") as handle:
        if args.format == "u32le":
            write_u32le(handle, args.variable_name, data)
        else:
            write_bytes(handle, args.variable_name, data)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
