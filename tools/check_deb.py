#!/usr/bin/env python3
"""Sanity-check a built Debian package before uploading it.

The important check is that every file has an explicit parent-directory entry
in data.tar.  dpkg creates "<file>.dpkg-new" inside the parent and fails with
ENOENT if the parent was never listed (the exact error from the first v17
build on device).
"""
import io
import os
import sys
import tarfile


def ar_members(path):
    data = open(path, "rb").read()
    if data[:8] != b"!<arch>\n":
        raise SystemExit("not an ar archive")
    i = 8
    while i + 60 <= len(data):
        hdr = data[i:i + 60]
        name = hdr[0:16].decode("utf-8", "replace").strip().rstrip("/")
        size = int(hdr[48:58].decode().strip())
        start = i + 60
        yield name, data[start:start + size]
        i = start + size + (size & 1)


def norm(name):
    parts = []
    for p in name.split("/"):
        if p in ("", "."):
            continue
        parts.append(p)
    return "/".join(parts)


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: check_deb.py package.deb")
    deb = sys.argv[1]
    members = dict(ar_members(deb))
    if "data.tar.gz" not in members:
        raise SystemExit("missing data.tar.gz")

    dirs = set()
    files = []
    with tarfile.open(fileobj=io.BytesIO(members["data.tar.gz"]), mode="r:gz") as tar:
        for m in tar.getmembers():
            n = norm(m.name)
            if not n:
                continue
            if m.isdir():
                dirs.add(n)
            elif m.isfile() or m.issym() or m.islnk():
                files.append((n, m.size, m.linkname if m.issym() else ""))

    missing = []
    for name, size, link in files:
        parent = os.path.dirname(name)
        if parent and parent not in dirs:
            missing.append((name, parent))
    if missing:
        for name, parent in missing[:20]:
            print("MISSING PARENT DIR:", parent, "for", name)
        raise SystemExit("data.tar is missing %d parent directories" % len(missing))

    control = members.get("control.tar.gz")
    if not control:
        raise SystemExit("missing control.tar.gz")
    with tarfile.open(fileobj=io.BytesIO(control), mode="r:gz") as tar:
        members_by_name = {norm(m.name): m for m in tar.getmembers()}
        for want in ("control", "postinst"):
            if want not in members_by_name:
                raise SystemExit("control.tar missing " + want)
        post = tar.extractfile(members_by_name["postinst"]).read()
        if b"SpringBoard" not in post:
            raise SystemExit("postinst does not respring SpringBoard")

    print("deb OK: %d dirs, %d files, control+postinst present" % (len(dirs), len(files)))
    for needle in ("ControlGlyphs18.json", "CC18ObjCLegacy.dylib",
                   "ControlCenter18Prefs"):
        if not any(needle in n for n, _, _ in files):
            raise SystemExit("missing payload: " + needle)
    print("payload OK")


if __name__ == "__main__":
    main()
