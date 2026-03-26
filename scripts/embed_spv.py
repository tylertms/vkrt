#!/usr/bin/env python3

import argparse
import sys
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(
        description="Embed a SPIR-V binary in a C source file."
    )
    parser.add_argument("input_path")
    parser.add_argument("output_path")
    parser.add_argument("variable_name")
    args = parser.parse_args()

    data = Path(args.input_path).read_bytes()

    if len(data) % 4 != 0:
        print(
            f"SPIR-V size must be a multiple of 4 bytes: {args.input_path}",
            file=sys.stderr,
        )
        return 1

    with open(args.output_path, "w", encoding="utf-8") as handle:
        handle.write("#include <stddef.h>\n")
        handle.write("#include <stdint.h>\n\n")
        handle.write(f"const uint32_t {args.variable_name}Data[] = {{\n")

        for i in range(0, len(data), 16):
            chunk = data[i : i + 16]
            words = []
            for word_start in range(0, len(chunk), 4):
                words.append(
                    int.from_bytes(
                        chunk[word_start : word_start + 4], byteorder="little"
                    )
                )
            handle.write("    " + ", ".join(f"0x{word:08x}u" for word in words) + ",\n")

        handle.write("};\n")
        handle.write(
            f"const size_t {args.variable_name}Size = sizeof({args.variable_name}Data);\n"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
