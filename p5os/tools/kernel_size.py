#!/usr/bin/env python3
"""Print the kernel's memory footprint from its ELF symbols."""
import os
import subprocess
import sys

LIMIT = 512 * 1024   # stage 2 staging area


def main():
    elf, binf = sys.argv[1], sys.argv[2]
    syms = {}
    out = subprocess.run(["nm", elf], check=True, capture_output=True, text=True).stdout
    for line in out.splitlines():
        p = line.split()
        if len(p) == 3:
            syms[p[2]] = int(p[0], 16)
    start, img_end = syms["__kernel_start"], syms["__image_end"]
    bss = syms["__bss_end"] - syms["__bss_start"]
    end = syms["__kernel_end"]
    size = os.path.getsize(binf)
    print(f"kernel: image {size} bytes ({(size + 511) // 512} sectors), "
          f"bss {bss} bytes, memory 0x{start:x}-0x{end:x} ({(end - start) // 1024} KB)")
    if size > LIMIT:
        print("ERROR: kernel image exceeds the 512 KB boot staging limit")
        sys.exit(1)


if __name__ == "__main__":
    main()
