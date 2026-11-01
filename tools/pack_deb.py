#!/usr/bin/env python3
"""Pack a rootless Debian package without dpkg-deb.

Usage: pack_deb.py <data_root> <control_file> <out.deb> [<version>]

data_root is the directory that contains var/jb/... (i.e. the extracted
data.tar root).  The resulting archive always uses gzip compression and root
ownership so Sileo/Zebra/dpkg accept it.
"""
import io
import os
import sys
import tarfile
import time

import gzip


def add_tree(tar, root, extra_control=None):
    entries = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames.sort()
        filenames.sort()
        for name in filenames:
            full = os.path.join(dirpath, name)
            rel = os.path.relpath(full, root)
            entries.append((full, rel))
    for full, rel in entries:
        info = tar.gettarinfo(full, arcname="./" + rel.replace(os.sep, "/"))
        info.uid = 0
        info.gid = 0
        info.uname = "root"
        info.gname = "root"
        info.mtime = 1700000000
        mode = os.stat(full).st_mode
        info.mode = 0o755 if (mode & 0o111) else 0o644
        with open(full, "rb") as f:
            tar.addfile(info, f)


def gz_tar(build):
    raw = io.BytesIO()
    with tarfile.open(fileobj=raw, mode="w") as tar:
        build(tar)
    out = io.BytesIO()
    with gzip.GzipFile(fileobj=out, mode="wb", mtime=0) as gz:
        gz.write(raw.getvalue())
    return out.getvalue()


def ar_member(name, payload):
    header = "{:<16}{:<12}{:<6}{:<6}{:<8}{:<10}`\n".format(
        name + "/", "0", "0", "0", "100644", str(len(payload))
    ).encode("ascii")
    return header + payload + (b"\n" if len(payload) & 1 else b"")


def main():
    if len(sys.argv) not in (4, 5):
        raise SystemExit(__doc__)
    data_root, control_path, out_path = sys.argv[1:4]
    version = sys.argv[4] if len(sys.argv) == 5 else None

    control_text = open(control_path, "r", encoding="utf-8").read()
    if version:
        lines = []
        for line in control_text.splitlines():
            if line.startswith("Version:"):
                line = "Version: " + version
            lines.append(line)
        control_text = "\n".join(lines) + "\n"

    def add_control_file(tar, name, data, mode=0o644):
        info = tarfile.TarInfo("./" + name)
        info.size = len(data)
        info.mode = mode
        info.uid = info.gid = 0
        info.uname = info.gname = "root"
        info.mtime = 1700000000
        tar.addfile(info, io.BytesIO(data))

    def build_control(tar):
        add_control_file(tar, "control", control_text.encode("utf-8"))
        # Keep maintainer scripts (postinst/prerm/...) that came with the base
        # package.  The postinst force-enables the tweak and resprings.
        ctrl_dir = os.path.dirname(os.path.abspath(control_path))
        for name in sorted(os.listdir(ctrl_dir)):
            if name == "control" or not os.path.isfile(os.path.join(ctrl_dir, name)):
                continue
            with open(os.path.join(ctrl_dir, name), "rb") as f:
                payload = f.read()
            mode = 0o755 if name in ("postinst", "preinst", "prerm", "postrm") else 0o644
            add_control_file(tar, name, payload, mode)

    def build_data(tar):
        add_tree(tar, data_root)

    control_tar = gz_tar(build_control)
    data_tar = gz_tar(build_data)

    with open(out_path, "wb") as f:
        f.write(b"!<arch>\n")
        f.write(ar_member("debian-binary", b"2.0\n"))
        f.write(ar_member("control.tar.gz", control_tar))
        f.write(ar_member("data.tar.gz", data_tar))
    print("wrote", out_path, os.path.getsize(out_path), "bytes")


if __name__ == "__main__":
    main()
