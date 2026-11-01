#!/usr/bin/env python3
"""Patch selected two-level undefined symbols to flat-namespace lookup.

ControlCenter18 is linked with a normal two-level namespace.  Its new-iOS
objc ABI imports (objc_release_xN, objc_retain_xN,
objc_claimAutoreleasedReturnValue) point at libobjc's ordinal, and its
Substrate imports point at CydiaSubstrate's ordinal.  On iOS 15 those symbols
do not exist in libobjc, and CydiaSubstrate may not be installed at all.

Changing only the affected entries in the chained-fixups import table to
BIND_SPECIAL_DYLIB_FLAT_LOOKUP (0xFE) makes dyld search all loaded images for
those names, where our bundled CC18ObjCLegacy.dylib provides them.  Every other
symbol keeps its normal two-level binding.

Usage: patch_imports.py <macho> <symbol> [<symbol> ...]
"""
import struct
import sys

import lief

FLAT_LOOKUP = 0xFE
CHAINED_FIXUPS = "DYLD_CHAINED_FIXUPS"


def read_cstr(buf, off):
    end = buf.find(b"\0", off)
    if end < 0:
        end = len(buf)
    return buf[off:end].decode("utf-8", "replace")


def patch_slice(path, fat, binary, wanted):
    for cmd in binary.commands:
        if cmd.command.name != CHAINED_FIXUPS:
            continue
        base = binary.fat_offset + cmd.data_offset
        raw = open(path, "rb").read()
        header = struct.unpack_from("<7I", raw, base)
        version, starts_off, imports_off, symbols_off, count, fmt, symfmt = header
        if version != 0:
            raise RuntimeError("unsupported chained fixups version %d" % version)
        imports = base + imports_off
        symbols = base + symbols_off
        patch = bytearray(raw)
        changed = 0
        for i in range(count):
            field_off = imports + 4 * i
            value = struct.unpack_from("<I", raw, field_off)[0]
            ordinal = value & 0xFF
            name_off = value >> 9
            name = read_cstr(raw, symbols + name_off)
            if name in wanted and ordinal != FLAT_LOOKUP:
                value = (value & ~0xFF) | FLAT_LOOKUP
                struct.pack_into("<I", patch, field_off, value)
                changed += 1
                print("  flat %-40s was ordinal %d" % (name, ordinal))
        if changed:
            with open(path, "wb") as f:
                f.write(patch)
        print("  %d import(s) patched in %s slice %s" %
              (changed, path, binary.header.cpu_subtype))


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    path = sys.argv[1]
    wanted = set(sys.argv[2:])
    fat = lief.MachO.parse(path)
    if fat is None:
        raise SystemExit("not a Mach-O: " + path)
    for binary in fat:
        patch_slice(path, fat, binary, wanted)


if __name__ == "__main__":
    main()
