#!/usr/bin/env python3
"""Small ar(1) reader for Debian packages, no binutils needed."""
import io
import os
import sys
import tarfile


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: extract_deb.py package.deb out_dir")
    deb, out_dir = sys.argv[1:]
    data = open(deb, "rb").read()
    if data[:8] != b"!<arch>\n":
        raise SystemExit("not an ar archive")
    os.makedirs(out_dir, exist_ok=True)
    i = 8
    while i + 60 <= len(data):
        header = data[i:i + 60]
        name = header[0:16].decode("utf-8", "replace").strip()
        try:
            size = int(header[48:58].decode().strip())
        except ValueError:
            break
        start = i + 60
        blob = data[start:start + size]
        if name.endswith("/"):
            name = name[:-1]
        print("member", name, size)
        if name.startswith("data.tar"):
            with tarfile.open(fileobj=io.BytesIO(blob), mode="r:*") as tar:
                tar.extractall(os.path.join(out_dir, "data"))
        elif name.startswith("control.tar"):
            ctrl = os.path.join(out_dir, "ctrl")
            os.makedirs(ctrl, exist_ok=True)
            with tarfile.open(fileobj=io.BytesIO(blob), mode="r:*") as tar:
                tar.extractall(ctrl)
        i = start + size + (size & 1)


if __name__ == "__main__":
    main()
