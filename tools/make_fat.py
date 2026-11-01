#!/usr/bin/env python3
"""Create a minimal Mach-O universal binary from two thin slices.

Usage: make_fat.py out.dylib arm64.dylib arm64e.dylib

The fat header format is the classic one (FAT_MAGIC, big endian).  iOS uses
0x4000 (16 KiB) alignment for arm64 slices, which is what Xcode/lipo use for
modern devices.
"""
import struct
import sys

CPU_TYPE_ARM64 = 0x0100000C
CPU_SUBTYPE_ARM64_ALL = 0
CPU_SUBTYPE_ARM64E = 0x80000002
ALIGN = 0x4000


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    out_path, arm_path, arm64e_path = sys.argv[1:]

    slices = [
        (CPU_TYPE_ARM64, CPU_SUBTYPE_ARM64_ALL, open(arm_path, "rb").read()),
        (CPU_TYPE_ARM64, CPU_SUBTYPE_ARM64E, open(arm64e_path, "rb").read()),
    ]

    header = b"\xca\xfe\xba\xbe" + struct.pack(">I", len(slices))
    table_size = 8 + 20 * len(slices)
    offset = ALIGN
    table = b""
    body = b""
    for cputype, cpusubtype, data in slices:
        table += struct.pack(">IIIII", cputype, cpusubtype, offset, len(data), 14)
        body += data
        pad = (-len(data)) % ALIGN
        body += b"\x00" * pad
        offset += len(data) + pad

    if table_size > ALIGN:
        raise SystemExit("fat header does not fit in one alignment page")
    blob = header + table + b"\x00" * (ALIGN - table_size) + body
    with open(out_path, "wb") as f:
        f.write(blob)
    print("wrote", out_path, len(blob), "bytes")


if __name__ == "__main__":
    main()
