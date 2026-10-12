#!/usr/bin/env python3
"""Build P5OS boot images.

  mkimage.py floppy --out IMG --stage1 S1 --stage2 S2 --kernel K
      1.44 MB floppy: [stage1][stage2 x16][kernel ...]

  mkimage.py hdd --out IMG --size-mb N --stage1 S1 --stage2 S2 --kernel K [--rootfs DIR]
      hard disk: same boot area, plus an MBR partition (type 0x7F) at
      LBA 2048 formatted with P5FS and populated from DIR.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import p5fs  # noqa: E402

SECTOR = 512
FLOPPY_SIZE = 1474560
STAGE2_SECTORS = 16
PART_START = 2048
HDD_HEADS, HDD_SPT = 16, 63     # geometry used for partition CHS fields


def boot_area(a):
    s1 = open(a.stage1, "rb").read()
    s2 = open(a.stage2, "rb").read()
    k = open(a.kernel, "rb").read()
    if len(s1) != SECTOR or s1[510:] != b"\x55\xaa":
        sys.exit("stage1 must be 512 bytes ending in 55 AA")
    if len(s2) != STAGE2_SECTORS * SECTOR or s2[3:7] != b"P5S2":
        sys.exit("stage2 has wrong size or signature")
    k += bytes(-len(k) % SECTOR)
    return bytearray(s1), s2 + k


def chs(lba):
    c = lba // (HDD_HEADS * HDD_SPT)
    if c > 1023:
        return bytes([0xFE, 0xFF, 0xFF])
    h = (lba // HDD_SPT) % HDD_HEADS
    s = lba % HDD_SPT + 1
    return bytes([h, ((c >> 2) & 0xC0) | s, c & 0xFF])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("kind", choices=["floppy", "hdd"])
    ap.add_argument("--out", required=True)
    ap.add_argument("--stage1", required=True)
    ap.add_argument("--stage2", required=True)
    ap.add_argument("--kernel", required=True)
    ap.add_argument("--size-mb", type=int, default=8)
    ap.add_argument("--rootfs")
    a = ap.parse_args()

    mbr, rest = boot_area(a)
    boot_sectors = 1 + len(rest) // SECTOR

    if a.kind == "floppy":
        if boot_sectors * SECTOR > FLOPPY_SIZE:
            sys.exit("kernel does not fit on a 1.44 MB floppy")
        with open(a.out, "wb") as f:
            f.write(mbr + rest)
            f.truncate(FLOPPY_SIZE)
        print(f"{a.out}: floppy image, {boot_sectors} boot sectors used")
        return

    total = a.size_mb * 1024 * 1024 // SECTOR
    if boot_sectors > PART_START:
        sys.exit("boot area overlaps the file system partition")
    psize = total - PART_START
    entry = bytes([0x00]) + chs(PART_START) + bytes([p5fs.PART_TYPE]) + chs(total - 1) \
        + struct.pack("<II", PART_START, psize)
    mbr[446:462] = entry
    with open(a.out, "w+b") as f:
        f.truncate(0)
        f.write(mbr + rest)
        f.truncate(total * SECTOR)
        fs = p5fs.mkfs(f, PART_START, psize, "P5OS")
        nfiles = 0
        if a.rootfs and os.path.isdir(a.rootfs):
            for root, dirs, files in os.walk(a.rootfs):
                dirs.sort()
                rel = os.path.relpath(root, a.rootfs)
                for d in dirs:
                    fs.mkdir("/" + os.path.normpath(os.path.join(rel, d)))
                for name in sorted(files):
                    with open(os.path.join(root, name), "rb") as src:
                        fs.write_file("/" + os.path.normpath(os.path.join(rel, name)), src.read())
                    nfiles += 1
        errs = fs.check()
        if errs:
            sys.exit("file system check failed: " + "; ".join(errs))
    print(f"{a.out}: HDD image {a.size_mb} MB, {boot_sectors} boot sectors, "
          f"P5FS partition at LBA {PART_START} ({psize} blocks, {nfiles} files)")


if __name__ == "__main__":
    main()
