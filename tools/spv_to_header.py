#!/usr/bin/env python3
"""Embed a SPIR-V module in a C header (uint32_t words)."""
import sys


def main() -> int:
    if len(sys.argv) != 4:
        print("usage: spv_to_header.py <input.spv> <output.h> <symbol>", file=sys.stderr)
        return 2
    src, dst, symbol = sys.argv[1:]
    data = open(src, "rb").read()
    if len(data) % 4:
        print(f"{src}: size is not a multiple of 4", file=sys.stderr)
        return 1
    words = [int.from_bytes(data[i:i + 4], "little") for i in range(0, len(data), 4)]
    with open(dst, "w", encoding="utf-8") as out:
        out.write("#pragma once\n#include <stdint.h>\n\n")
        out.write(f"static const uint32_t {symbol}[] = {{\n")
        for i in range(0, len(words), 8):
            out.write("    " + ", ".join(f"0x{w:08x}u" for w in words[i:i + 8]) + ",\n")
        out.write("};\n")
        out.write(f"static const unsigned {symbol}_count = {len(words)}u;\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
