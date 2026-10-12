#!/usr/bin/env python3
"""Host-side implementation of P5FS, the P5OS file system.

Used to format disk images, preload files, and to independently verify what
the P5OS kernel wrote (tests read images back with this tool).

On-disk format (version 1), all integers little endian, block = 512 bytes:

  block 0                superblock
  blocks 1..             block allocation bitmap (bit set = block in use)
  blocks ..              inode table, 64-byte inodes, 8 per block
  data_start..           data blocks

  superblock: magic u32 'P5FS', version u32, total_blocks u32,
              bitmap_start u32, bitmap_blocks u32, inode_start u32,
              inode_blocks u32, inode_count u32, data_start u32,
              root_inode u32, label char[16], checksum u32 (at offset 56)
  inode:      type u16 (0 free, 1 file, 2 dir), links u16, size u32,
              mtime u32, direct u32[12], indirect u32
  dirent:     inode u32 (0 = free slot), name char[28] NUL terminated
  Inode numbers start at 1 (inode 1 = root).  Block number 0 in a block
  pointer means "no block".  Every directory holds "." and "..".

The format is mirrored by kernel/fs/p5fs.c and include/p5fs.h.

CLI:
  p5fs.py mkfs IMAGE --size-mb N [--label L]        create whole-disk FS
  p5fs.py ls IMAGE [PATH] [--offset LBA]
  p5fs.py cat IMAGE PATH [--offset LBA]
  p5fs.py put IMAGE HOSTFILE PATH [--offset LBA]
  p5fs.py mkdir IMAGE PATH [--offset LBA]
  p5fs.py check IMAGE [--offset LBA]
  p5fs.py tree IMAGE [--offset LBA]
--offset is the partition start in sectors; use "auto" to find a type 0x7F
partition in an MBR (default: auto).
"""
import argparse
import struct
import sys
import time

BLOCK = 512
MAGIC = 0x53463550
VERSION = 1
INODE_SIZE = 64
INODES_PER_BLOCK = BLOCK // INODE_SIZE
DIRENT_SIZE = 32
DIRENTS_PER_BLOCK = BLOCK // DIRENT_SIZE
NAME_MAX = 27
NDIRECT = 12
PTRS_PER_BLOCK = BLOCK // 4
MAX_FILE_BLOCKS = NDIRECT + PTRS_PER_BLOCK
MAX_FILE_SIZE = MAX_FILE_BLOCKS * BLOCK
ROOT_INO = 1
T_FREE, T_FILE, T_DIR = 0, 1, 2
PART_TYPE = 0x7F
SB_FMT = "<10I16s"          # fields before checksum (56 bytes)
CHECKSUM_SALT = 0x5035F5A5


def checksum(sb_bytes):
    words = struct.unpack("<14I", sb_bytes[:56])
    return (sum(words) + CHECKSUM_SALT) & 0xFFFFFFFF


def layout(total_blocks):
    bitmap_blocks = (total_blocks + BLOCK * 8 - 1) // (BLOCK * 8)
    inode_count = max(64, min(4096, total_blocks // 16))
    inode_count = (inode_count + INODES_PER_BLOCK - 1) // INODES_PER_BLOCK * INODES_PER_BLOCK
    inode_blocks = inode_count // INODES_PER_BLOCK
    bitmap_start = 1
    inode_start = bitmap_start + bitmap_blocks
    data_start = inode_start + inode_blocks
    if data_start + 8 > total_blocks:
        raise ValueError("file system too small")
    return bitmap_start, bitmap_blocks, inode_start, inode_blocks, inode_count, data_start


class FSError(Exception):
    pass


class P5FS:
    def __init__(self, f, offset_lba=0):
        self.f = f
        self.base = offset_lba * BLOCK
        sb = self.read_block(0)
        (magic, ver, self.total, self.bm_start, self.bm_blocks, self.ino_start,
         self.ino_blocks, self.ino_count, self.data_start, self.root,
         label) = struct.unpack(SB_FMT, sb[:56])
        if magic != MAGIC:
            raise FSError("no P5FS superblock (bad magic)")
        if ver != VERSION:
            raise FSError(f"unsupported P5FS version {ver}")
        if struct.unpack("<I", sb[56:60])[0] != checksum(sb):
            raise FSError("superblock checksum mismatch")
        self.label = label.rstrip(b"\0").decode("ascii", "replace")
        self.bitmap = bytearray(b"".join(self.read_block(self.bm_start + i)
                                         for i in range(self.bm_blocks)))

    # -- raw block I/O -------------------------------------------------------
    def read_block(self, n):
        self.f.seek(self.base + n * BLOCK)
        d = self.f.read(BLOCK)
        if len(d) != BLOCK:
            raise FSError(f"short read at block {n}")
        return d

    def write_block(self, n, data):
        assert len(data) == BLOCK
        if n == 0 and data[:4] != struct.pack("<I", MAGIC):
            raise FSError("refusing to overwrite superblock")
        self.f.seek(self.base + n * BLOCK)
        self.f.write(data)

    # -- bitmap ----------------------------------------------------------------
    def used(self, b):
        return bool(self.bitmap[b // 8] & (1 << (b % 8)))

    def set_used(self, b, on):
        if on:
            self.bitmap[b // 8] |= 1 << (b % 8)
        else:
            self.bitmap[b // 8] &= ~(1 << (b % 8)) & 0xFF
        blk = b // (BLOCK * 8)
        self.write_block(self.bm_start + blk,
                         bytes(self.bitmap[blk * BLOCK:(blk + 1) * BLOCK]))

    def alloc_block(self):
        for b in range(self.data_start, self.total):
            if not self.used(b):
                self.set_used(b, True)
                self.write_block(b, bytes(BLOCK))
                return b
        raise FSError("no free blocks")

    # -- inodes ----------------------------------------------------------------
    def read_inode(self, ino):
        if not 1 <= ino <= self.ino_count:
            raise FSError(f"bad inode number {ino}")
        blk = self.ino_start + (ino - 1) // INODES_PER_BLOCK
        off = ((ino - 1) % INODES_PER_BLOCK) * INODE_SIZE
        raw = self.read_block(blk)[off:off + INODE_SIZE]
        v = struct.unpack("<HHII12II", raw)
        return {"type": v[0], "links": v[1], "size": v[2], "mtime": v[3],
                "direct": list(v[4:16]), "indirect": v[16]}

    def write_inode(self, ino, node):
        blk = self.ino_start + (ino - 1) // INODES_PER_BLOCK
        off = ((ino - 1) % INODES_PER_BLOCK) * INODE_SIZE
        data = bytearray(self.read_block(blk))
        data[off:off + INODE_SIZE] = struct.pack(
            "<HHII12II", node["type"], node["links"], node["size"], node["mtime"],
            *node["direct"], node["indirect"])
        self.write_block(blk, bytes(data))

    def alloc_inode(self, typ):
        for ino in range(1, self.ino_count + 1):
            if self.read_inode(ino)["type"] == T_FREE:
                node = {"type": typ, "links": 1, "size": 0, "mtime": int(time.time()),
                        "direct": [0] * NDIRECT, "indirect": 0}
                self.write_inode(ino, node)
                return ino, node
        raise FSError("no free inodes")

    def file_blocks(self, node):
        blocks = [b for b in node["direct"] if b]
        if node["indirect"]:
            ptrs = struct.unpack(f"<{PTRS_PER_BLOCK}I", self.read_block(node["indirect"]))
            blocks += [b for b in ptrs if b]
        return blocks

    def read_data(self, node):
        n = (node["size"] + BLOCK - 1) // BLOCK
        data = b"".join(self.read_block(b) for b in self.file_blocks(node)[:n])
        return data[:node["size"]]

    def set_data(self, ino, node, data):
        """Replace file contents.  New blocks are written before the inode is
        updated, old blocks are freed afterwards (crash leaves leaks only)."""
        if len(data) > MAX_FILE_SIZE:
            raise FSError("file too large")
        old = self.file_blocks(node) + ([node["indirect"]] if node["indirect"] else [])
        nblk = (len(data) + BLOCK - 1) // BLOCK
        new = [self.alloc_block() for _ in range(nblk)]
        for i, b in enumerate(new):
            self.write_block(b, data[i * BLOCK:(i + 1) * BLOCK].ljust(BLOCK, b"\0"))
        node["direct"] = (new[:NDIRECT] + [0] * NDIRECT)[:NDIRECT]
        node["indirect"] = 0
        if nblk > NDIRECT:
            ind = self.alloc_block()
            ptrs = new[NDIRECT:] + [0] * (PTRS_PER_BLOCK - (nblk - NDIRECT))
            self.write_block(ind, struct.pack(f"<{PTRS_PER_BLOCK}I", *ptrs))
            node["indirect"] = ind
        node["size"] = len(data)
        node["mtime"] = int(time.time())
        self.write_inode(ino, node)
        for b in old:
            self.set_used(b, False)

    # -- directories -------------------------------------------------------------
    def entries(self, ino):
        node = self.read_inode(ino)
        if node["type"] != T_DIR:
            raise FSError("not a directory")
        data = self.read_data(node)
        out = []
        for i in range(0, len(data), DIRENT_SIZE):
            e_ino, name = struct.unpack("<I28s", data[i:i + DIRENT_SIZE])
            if e_ino:
                out.append((name.split(b"\0")[0].decode("ascii", "replace"), e_ino))
        return out

    def lookup(self, path):
        ino = self.root
        for part in [p for p in path.split("/") if p]:
            for name, e in self.entries(ino):
                if name == part:
                    ino = e
                    break
            else:
                raise FSError(f"{path}: not found")
        return ino

    def add_entry(self, dir_ino, name, ino):
        if not name or len(name) > NAME_MAX or "/" in name:
            raise FSError(f"bad name {name!r}")
        if any(n == name for n, _ in self.entries(dir_ino)):
            raise FSError(f"{name}: already exists")
        node = self.read_inode(dir_ino)
        data = bytearray(self.read_data(node))
        ent = struct.pack("<I28s", ino, name.encode("ascii"))
        for i in range(0, len(data), DIRENT_SIZE):
            if struct.unpack("<I", data[i:i + 4])[0] == 0:
                data[i:i + DIRENT_SIZE] = ent
                break
        else:
            data += ent + bytes(BLOCK - DIRENT_SIZE)
        self.set_data(dir_ino, node, bytes(data))

    def split(self, path):
        parts = [p for p in path.split("/") if p]
        if not parts:
            raise FSError("bad path")
        return self.lookup("/".join(parts[:-1])), parts[-1]

    def mkdir(self, path):
        parent, name = self.split(path)
        ino, node = self.alloc_inode(T_DIR)
        d = struct.pack("<I28s", ino, b".") + struct.pack("<I28s", parent, b"..")
        self.set_data(ino, node, d + bytes(BLOCK - len(d)))
        self.add_entry(parent, name, ino)
        return ino

    def write_file(self, path, data):
        parent, name = self.split(path)
        for n, e in self.entries(parent):
            if n == name:
                node = self.read_inode(e)
                if node["type"] != T_FILE:
                    raise FSError(f"{path}: is a directory")
                self.set_data(e, node, data)
                return e
        ino, node = self.alloc_inode(T_FILE)
        self.set_data(ino, node, data)
        self.add_entry(parent, name, ino)
        return ino

    # -- consistency check ---------------------------------------------------------
    def check(self):
        errors = []
        owner = {}
        expect = bytearray(len(self.bitmap))

        def mark(b, who):
            if not self.data_start <= b < self.total:
                errors.append(f"{who}: block {b} outside data area")
                return
            if b in owner:
                errors.append(f"{who}: block {b} also used by {owner[b]}")
            owner[b] = who
            expect[b // 8] |= 1 << (b % 8)

        for b in range(self.data_start):
            expect[b // 8] |= 1 << (b % 8)
        for b in range(self.total, len(self.bitmap) * 8):
            expect[b // 8] |= 1 << (b % 8)
        referenced = {self.root}
        for ino in range(1, self.ino_count + 1):
            node = self.read_inode(ino)
            if node["type"] == T_FREE:
                continue
            if node["type"] not in (T_FILE, T_DIR):
                errors.append(f"inode {ino}: bad type {node['type']}")
                continue
            if node["indirect"]:
                mark(node["indirect"], f"inode {ino} (indirect)")
            blocks = self.file_blocks(node)
            for b in blocks:
                mark(b, f"inode {ino}")
            if len(blocks) * BLOCK < node["size"]:
                errors.append(f"inode {ino}: size {node['size']} exceeds blocks")
            if node["type"] == T_DIR:
                for name, e in self.entries(ino):
                    if name not in (".", ".."):
                        referenced.add(e)
                        if self.read_inode(e)["type"] == T_FREE:
                            errors.append(f"dir {ino}: entry {name} -> free inode {e}")
        for ino in range(1, self.ino_count + 1):
            if self.read_inode(ino)["type"] != T_FREE and ino not in referenced:
                errors.append(f"inode {ino}: not referenced by any directory")
        if bytes(expect) != bytes(self.bitmap):
            diff = [b for b in range(self.total)
                    if bool(expect[b // 8] & (1 << b % 8)) != self.used(b)]
            errors.append(f"bitmap mismatch on {len(diff)} block(s), e.g. {diff[:8]}")
        return errors

    def free_blocks(self):
        return sum(1 for b in range(self.data_start, self.total) if not self.used(b))


def mkfs(f, offset_lba, total_blocks, label="P5OS"):
    bm_start, bm_blocks, ino_start, ino_blocks, ino_count, data_start = layout(total_blocks)
    base = offset_lba * BLOCK
    f.seek(base)
    zero = bytes(BLOCK)
    for _ in range(data_start + 1):
        f.write(zero)
    sb = bytearray(BLOCK)
    sb[:56] = struct.pack(SB_FMT, MAGIC, VERSION, total_blocks, bm_start, bm_blocks,
                          ino_start, ino_blocks, ino_count, data_start, ROOT_INO,
                          label.encode("ascii")[:15])
    sb[56:60] = struct.pack("<I", checksum(sb))
    f.seek(base)
    f.write(sb)
    bitmap = bytearray(bm_blocks * BLOCK)
    for b in list(range(data_start)) + list(range(total_blocks, bm_blocks * BLOCK * 8)):
        bitmap[b // 8] |= 1 << (b % 8)
    f.seek(base + bm_start * BLOCK)
    f.write(bitmap)
    fs = P5FS(f, offset_lba)
    # root directory
    node = {"type": T_DIR, "links": 1, "size": 0, "mtime": int(time.time()),
            "direct": [0] * NDIRECT, "indirect": 0}
    fs.write_inode(ROOT_INO, node)
    d = struct.pack("<I28s", ROOT_INO, b".") + struct.pack("<I28s", ROOT_INO, b"..")
    fs.set_data(ROOT_INO, node, d + bytes(BLOCK - len(d)))
    return fs


def find_partition(f):
    f.seek(0)
    mbr = f.read(BLOCK)
    if len(mbr) == BLOCK and struct.unpack("<I", mbr[:4])[0] == MAGIC:
        return 0
    if mbr[510:512] == b"\x55\xaa":
        for i in range(4):
            e = mbr[446 + 16 * i:446 + 16 * (i + 1)]
            if e[4] == PART_TYPE:
                return struct.unpack("<I", e[8:12])[0]
    raise FSError("no P5FS file system or type 0x7F partition found")


def main():
    ap = argparse.ArgumentParser(description="P5FS image tool")
    ap.add_argument("cmd", choices=["mkfs", "ls", "cat", "put", "mkdir", "check", "tree"])
    ap.add_argument("image")
    ap.add_argument("args", nargs="*")
    ap.add_argument("--offset", default="auto")
    ap.add_argument("--size-mb", type=int, default=4)
    ap.add_argument("--label", default="P5OS")
    a = ap.parse_args()

    if a.cmd == "mkfs":
        size = a.size_mb * 1024 * 1024
        with open(a.image, "wb") as f:
            f.truncate(size)
            mkfs(f, 0, size // BLOCK, a.label)
        print(f"{a.image}: P5FS, {size // BLOCK} blocks")
        return

    mode = "r+b" if a.cmd in ("put", "mkdir") else "rb"
    with open(a.image, mode) as f:
        off = find_partition(f) if a.offset == "auto" else int(a.offset)
        fs = P5FS(f, off)
        if a.cmd == "ls":
            ino = fs.lookup(a.args[0] if a.args else "/")
            for name, e in fs.entries(ino):
                n = fs.read_inode(e)
                kind = "<DIR>" if n["type"] == T_DIR else str(n["size"])
                print(f"{kind:>8}  {name}")
        elif a.cmd == "cat":
            sys.stdout.buffer.write(fs.read_data(fs.read_inode(fs.lookup(a.args[0]))))
        elif a.cmd == "put":
            with open(a.args[0], "rb") as src:
                fs.write_file(a.args[1], src.read())
        elif a.cmd == "mkdir":
            fs.mkdir(a.args[0])
        elif a.cmd == "tree":
            def walk(ino, prefix):
                for name, e in fs.entries(ino):
                    if name in (".", ".."):
                        continue
                    n = fs.read_inode(e)
                    if n["type"] == T_DIR:
                        print(f"{prefix}{name}/")
                        walk(e, prefix + "  ")
                    else:
                        print(f"{prefix}{name} ({n['size']} bytes)")
            walk(fs.root, "/")
        elif a.cmd == "check":
            errs = fs.check()
            print(f"label={fs.label} blocks={fs.total} free={fs.free_blocks()} "
                  f"inodes={fs.ino_count}")
            for e in errs:
                print("ERROR: " + e)
            print("check: " + ("OK" if not errs else f"{len(errs)} error(s)"))
            sys.exit(1 if errs else 0)


if __name__ == "__main__":
    try:
        main()
    except FSError as e:
        print(f"p5fs: {e}", file=sys.stderr)
        sys.exit(1)
