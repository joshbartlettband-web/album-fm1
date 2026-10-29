#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Hausway
"""A crash screen's addresses to function names, from an ELF's symbol table (no toolchain needed).

  tools/addr2sym.py ELF ADDR [ADDR ...]       e.g. addr2sym.py build/album-ble.elf 020036E0 02005C34

For each address: the function (or object) it falls in and the offset into it, and the symbols either side."""
import struct
import sys


def symbols(path):
    d = open(path, "rb").read()
    if d[:4] != b"\x7fELF" or d[4] != 1:
        raise SystemExit("addr2sym: not a 32-bit ELF")
    end = "<" if d[5] == 1 else ">"
    shoff, = struct.unpack_from(end + "I", d, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from(end + "HHH", d, 0x2E)
    secs = [struct.unpack_from(end + "IIIIIIIIII", d, shoff + i * shentsize) for i in range(shnum)]
    out = []
    for s in secs:
        if s[1] != 2:                    # SHT_SYMTAB
            continue
        strtab = secs[s[6]]
        for i in range(s[5] // 16):
            name, value, size, info, other, shndx = struct.unpack_from(end + "IIIBBH", d, s[4] + i * 16)
            if not name or shndx == 0 or (info & 15) not in (1, 2):   # objects and functions
                continue
            nm = d[strtab[4] + name:d.index(b"\0", strtab[4] + name)].decode(errors="replace")
            out.append((value, size, nm))
    return sorted(out)


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    syms = symbols(sys.argv[1])
    for a in sys.argv[2:]:
        x = int(a, 16)
        below = [s for s in syms if s[0] <= x]
        above = [s for s in syms if s[0] > x]
        if not below:
            print(f"{x:08X}: below every symbol")
            continue
        v, size, nm = below[-1]
        inside = size == 0 or x < v + size
        print(f"{x:08X}: {nm}+0x{x - v:X}" + ("" if inside else f" (past its end, size {size})")
              + f"   [before: {below[-2][2] if len(below) > 1 else '-'}; after: {above[0][2] if above else '-'}]")


if __name__ == "__main__":
    main()
