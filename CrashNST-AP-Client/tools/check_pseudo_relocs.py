"""Fail the build when a DLL has a runtime pseudo relocation narrower than 64 bits.

MinGW links a call to a function another DLL exports, made without dllimport, as a 32-bit relative call that the
C runtime patches when the DLL loads ("runtime pseudo relocation"). When Windows puts the DLL more than 2 GB away from
that other DLL, the patch can't reach and the C runtime stops the process (exception 0x40000015) before anything else
runs: the game just closes (V1.3.10-V1.3.11 and Stable V1.3, October 2026: libmingwex's fesetenv calling msvcrt's
_fpreset). 64-bit ones (data reached through a pointer) are safe.

python check_pseudo_relocs.py --nm <path to nm> <dll> [<dll> ...]
"""

import struct
import subprocess
import sys


def check(nm: str, path: str) -> list[str]:
    data = open(path, "rb").read()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    count, optional = struct.unpack_from("<H", data, pe + 6)[0], struct.unpack_from("<H", data, pe + 20)[0]
    base = struct.unpack_from("<Q", data, pe + 24 + 24)[0]
    sections = []
    at = pe + 24 + optional
    for _ in range(count):
        virtual_size, address, raw_size, raw = struct.unpack_from("<IIII", data, at + 8)
        sections.append((address, max(virtual_size, raw_size), raw))
        at += 40

    def offset(rva: int) -> int:
        for address, size, raw in sections:
            if address <= rva < address + size:
                return raw + rva - address
        raise ValueError(f"{path}: {rva:#x} is in no section")

    symbols = {}
    for line in subprocess.run([nm, path], capture_output=True, text=True, check=True).stdout.splitlines():
        parts = line.split()
        if len(parts) == 3:
            symbols[parts[2]] = int(parts[0], 16)
    if "__RUNTIME_PSEUDO_RELOC_LIST__" not in symbols:
        return []
    start, end = symbols["__RUNTIME_PSEUDO_RELOC_LIST__"], symbols["__RUNTIME_PSEUDO_RELOC_LIST_END__"]
    words = struct.unpack_from(f"<{(end - start) // 4}I", data, offset(start - base))
    names = {address - base: name for name, address in symbols.items()}
    bad = []
    # version 2: a header of three words (0, 0, 1), then (imported symbol, place patched, flags: bits in the low byte)
    for i in range(3, len(words) - 2, 3):
        symbol, target, flags = words[i : i + 3]
        if flags & 0xFF != 64:
            bad.append(f"{path}: {flags & 0xFF}-bit pseudo relocation to {names.get(symbol, hex(symbol))} at {target:#x}")
    return bad


def main() -> int:
    args = sys.argv[1:]
    if len(args) < 3 or args[0] != "--nm":
        print(__doc__)
        return 2
    nm, dlls = args[1], args[2:]
    bad = [line for dll in dlls for line in check(nm, dll)]
    for line in bad:
        print(f"::error::{line}: it can stop the DLL from loading (the game closes at once)")
    if not bad:
        print("no pseudo relocations narrower than 64 bits in " + ", ".join(dlls))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
