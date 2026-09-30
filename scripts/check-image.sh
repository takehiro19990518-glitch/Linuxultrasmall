#!/bin/bash
# Static checks of the built P3 Linux images:
#  - ISO is El Torito bootable and contains kernel, initrd, squashfs root
#  - HDD image has an MBR with a bootable FAT partition + ext2 root partition
#  - every ELF binary in the root filesystem is 32-bit x86
#  - no binary uses SSE2+ instructions (Pentium III has MMX + SSE only)
set -u
D="$(cd "$(dirname "$0")/.." && pwd)/output"
IMG="$D/images"; T="$D/target"
fail=0
ok()  { echo "[PASS] $*"; }
bad() { echo "[FAIL] $*"; fail=1; }

ISO="$IMG/P3-Linux.iso"
if [ -f "$ISO" ]; then
  ok "ISO exists ($(du -h "$ISO" | cut -f1))"
  xorriso -indev "$ISO" -report_el_torito plain 2>/dev/null | grep -q "El Torito boot img" \
    && ok "ISO is El Torito bootable" || bad "ISO has no El Torito boot image"
  for f in /isolinux/isolinux.bin /boot/bzImage /boot/initrd.gz /p3linux/rootfs.sqfs; do
    xorriso -indev "$ISO" -find "$f" 2>/dev/null | grep -q "$f" && ok "ISO contains $f" || bad "ISO lacks $f"
  done
else bad "ISO missing"; fi

HDD="$IMG/P3-Linux-hdd.img"
if [ -f "$HDD" ]; then
  ok "HDD image exists ($(du -h --apparent-size "$HDD" | cut -f1))"
  sfdisk -l "$HDD" 2>/dev/null | grep -q "^$HDD""1 \+\*" && ok "HDD partition 1 is bootable" || bad "HDD partition 1 not bootable"
  [ "$(xxd -s 510 -l 2 -p "$HDD")" = 55aa ] && ok "HDD has MBR signature" || bad "HDD MBR signature"
else bad "HDD image missing"; fi

file -L "$IMG/bzImage" | grep -q "Linux kernel x86 boot" && ok "bzImage is an x86 kernel" || bad "bzImage type"

n64=$(find "$T" -type f -exec file {} + 2>/dev/null | grep ELF | grep -vc "ELF 32-bit LSB.*Intel 80386" || true)
[ "$n64" = 0 ] && ok "all ELF files are 32-bit i386" || bad "$n64 non-i386 ELF files"

# SSE2/SSE3/SSSE3/SSE4/AVX use: look for instructions on xmm registers that
# only exist from SSE2 on (integer SIMD on xmm, double precision ops, etc.)
cnt=0; list=""
while IFS= read -r f; do
  c=$(objdump -d --no-show-raw-insn "$f" 2>/dev/null | grep -cE \
      "\s(movdqa|movdqu|paddq|psubq|pmuludq|pshufd|pshufhw|pshuflw|punpck[lh]qdq|cvt(si2sd|sd2si|tsd2si|ss2sd|sd2ss|dq2pd|pd2dq)|(add|sub|mul|div|sqrt|max|min|and|or|xor|mov|comi|ucomi)(sd|pd)|movq|movd|movq2dq|lfence|mfence|pause|vzeroupper|v[a-z]+)\s.*%(x|y)mm" || true)
  if [ "$c" -gt 0 ]; then cnt=$((cnt+1)); list="$list ${f#$T}($c)"; fi
done < <(find "$T" -type f \( -perm -u+x -o -name "*.so*" \) -exec sh -c 'file "$1" | grep -q ELF && echo "$1"' _ {} \;)
[ $cnt = 0 ] && ok "no SSE2+ instructions found in any binary (Pentium III safe)" \
  || bad "SSE2+ instructions found in $cnt files:$list"
exit $fail
