#!/usr/bin/env python3
"""Disassemble the kernel and reject instructions the original Pentium (P5,
no MMX) does not implement.  Usage: check_isa.py kernel.elf"""
import re
import subprocess
import sys

# P6+ / MMX / SSE / later instructions and registers
FORBIDDEN = [
    r"\bcmov\w*", r"\bfcmov\w*", r"\bfcomi\w*", r"\bfucomi\w*",
    r"\bsysenter\b", r"\bsysexit\b", r"\bsyscall\b", r"\bud2\b",
    r"\brdpmc\b", r"\bfxsave\b", r"\bfxrstor\b", r"\bclflush\b",
    r"\blfence\b", r"\bsfence\b", r"\bmfence\b", r"\bpause\b",
    r"%xmm\d", r"%ymm\d", r"%mm\d", r"\bemms\b", r"\bmovbe\b",
    r"\bpopcnt\b", r"\blzcnt\b", r"\btzcnt\b", r"\bprefetch\w*",
    r"\bendbr32\b",
]


def main():
    elf = sys.argv[1]
    out = subprocess.run(["objdump", "-d", "-M", "att", elf],
                         check=True, capture_output=True, text=True).stdout
    pat = re.compile("|".join(FORBIDDEN))
    bad = [l for l in out.splitlines() if "\t" in l and pat.search(l.split("\t")[-1])]
    if bad:
        print("check-isa: FAIL - instructions not available on a P5 Pentium:")
        for l in bad[:40]:
            print("  " + l.strip())
        sys.exit(1)
    n = sum(1 for l in out.splitlines() if re.match(r"\s+[0-9a-f]+:\t", l))
    print(f"check-isa: OK ({n} instructions, none beyond the P5 instruction set)")


if __name__ == "__main__":
    main()
