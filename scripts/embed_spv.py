#!/usr/bin/env python3
"""Convert a SPIR-V binary into a C source file with an embedded byte array."""

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

    with open(output_path, "w") as f:
        f.write("#include <stddef.h>\n\n")
        f.write(f"const unsigned char {var_name}_data[] = {{\n")
        for i in range(0, len(data), 16):
            chunk = data[i : i + 16]
            hex_str = ", ".join(f"0x{b:02x}" for b in chunk)
            f.write(f"    {hex_str},\n")
        f.write("};\n")
        f.write(f"const size_t {var_name}_size = sizeof({var_name}_data);\n")


if __name__ == "__main__":
    main()
