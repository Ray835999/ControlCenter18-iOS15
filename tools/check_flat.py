#!/usr/bin/env python3
"""Check that the Substrate imports are flat and the shim has the ABI exports.

Pure Python Mach-O reader, deliberately independent of LIEF: LIEF 1.0.0 can
crash while parsing a freshly linked fat dylib, and this script is the build's
safety net.
"""
import struct
import sys

FAT_MAGIC = 0xCAFEBABE
MH_MAGIC_64 = 0xFEEDFACF
LC_DYLD_CHAINED_FIXUPS = 0x80000034
LC_BUILD_VERSION = 0x32
WANTED = ("_MSHookMessageEx", "_MSHookFunction", "_MSHookMemory")


def cstr(data, off):
    end = data.find(b"\0", off)
    return data[off:end].decode("utf-8", "replace")


def slices(path):
    data = open(path, "rb").read()
    if len(data) < 8:
        raise SystemExit("short file: " + path)
    magic = struct.unpack_from(">I", data, 0)[0]
    if magic == FAT_MAGIC:
        n = struct.unpack_from(">I", data, 4)[0]
        for i in range(n):
            cputype, cpusubtype, off, size, align = struct.unpack_from(">IIIII", data, 8 + 20 * i)
            yield data, off, cputype, cpusubtype
    else:
        yield data, 0, 0, 0


def check_imports(path):
    ok = True
    for data, base, cputype, cpusubtype in slices(path):
        magic = struct.unpack_from("<I", data, base)[0]
        if magic != MH_MAGIC_64:
            raise SystemExit("not thin 64-bit Mach-O at %#x" % base)
        ncmds = struct.unpack_from("<I", data, base + 16)[0]
        p = base + 32
        found = {}
        minos = None
        shim_ordinal = None
        load_ordinal = 0
        for _ in range(ncmds):
            cmd, cmdsize = struct.unpack_from("<II", data, p)
            if cmd in (0xC, 0x80000018, 0x8000001F, 0x80000023):
                # LOAD_DYLIB / LOAD_WEAK_DYLIB / REEXPORT_DYLIB /
                # LOAD_UPWARD_DYLIB all receive a dyld ordinal.
                load_ordinal += 1
                name_off = struct.unpack_from("<I", data, p + 8)[0]
                if b"CC18ObjCLegacy" in data[p + name_off:p + cmdsize]:
                    shim_ordinal = load_ordinal
            elif cmd == LC_DYLD_CHAINED_FIXUPS:
                dataoff, datasize = struct.unpack_from("<II", data, p + 8)
                off = base + dataoff
                hdr = struct.unpack_from("<7I", data, off)
                version, starts, imports, symbols, count, fmt, symfmt = hdr
                if version != 0 or fmt != 1:
                    raise SystemExit("unexpected chained fixup format %d/%d" % (version, fmt))
                imp = off + imports
                sym = off + symbols
                for i in range(count):
                    v = struct.unpack_from("<I", data, imp + 4 * i)[0]
                    name = cstr(data, sym + (v >> 9))
                    if name in WANTED:
                        found[name] = v & 0xFF
            elif cmd == LC_BUILD_VERSION:
                minos = struct.unpack_from("<I", data, p + 12)[0]
            p += cmdsize
        print("slice cpusubtype=%#x minos=%s imports=%s shim_ordinal=%s" %
              (cpusubtype, minos, found, shim_ordinal))
        for name in WANTED:
            if found.get(name) != shim_ordinal:
                print("  BAD", name, found.get(name), "expected", shim_ordinal)
                ok = False
    if not ok:
        raise SystemExit("substrate import binding check failed")
    print("substrate imports bound to shim:", path)


if __name__ == "__main__":
    check_imports(sys.argv[1])
