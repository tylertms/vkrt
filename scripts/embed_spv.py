#!/usr/bin/env python3
"""Convert a SPIR-V binary into a C source file with an embedded word array."""

import sys


def main():
    if len(sys.argv) != 4:
        print(
            f"Usage: {sys.argv[0]} <input.spv> <output.c> <variable_name>",
            file=sys.stderr,
        )
        sys.exit(1)

    input_path = sys.argv[1]
    output_path = sys.argv[2]
    var_name = sys.argv[3]

    with open(input_path, "rb") as f:
        data = f.read()
    if len(data) % 4 != 0:
        print(f"SPIR-V size must be a multiple of 4 bytes: {input_path}", file=sys.stderr)
        sys.exit(1)

    with open(output_path, "w") as f:
        f.write("#include <stddef.h>\n")
        f.write("#include <stdint.h>\n\n")
        f.write(f"const uint32_t {var_name}Data[] = {{\n")
        for i in range(0, len(data), 16):
            chunk = data[i : i + 16]
            words = [
                int.from_bytes(chunk[word_start : word_start + 4], byteorder="little")
                for word_start in range(0, len(chunk), 4)
            ]
            hex_str = ", ".join(f"0x{word:08x}u" for word in words)
            f.write(f"    {hex_str},\n")
        f.write("};\n")
        f.write(f"const size_t {var_name}Size = sizeof({var_name}Data);\n")


if __name__ == "__main__":
    main()
