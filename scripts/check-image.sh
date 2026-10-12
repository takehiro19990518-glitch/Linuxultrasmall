#!/bin/bash
# Static checks of the built P3 Linux images:
#  - ISO is El Torito bootable and contains kernel, initrd, squashfs root
#  - HDD image has an MBR with a bootable FAT partition + ext2 root partition
#  - every ELF binary in the root filesystem is 32-bit x86
#  - no binary uses SSE2+ instructions (Pentium III has MMX + SSE only)
set -u
export PATH="$PATH:/sbin:/usr/sbin"
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
  # MBR partition entry 1 at offset 446: boot flag 0x80, type 0x06 (FAT16)
  p1=$(od -An -tx1 -j446 -N5 "$HDD" | tr -s ' ' | sed 's/^ //')
  case "$p1" in 80*" 06") ok "HDD partition 1 is bootable FAT16";; *) bad "HDD partition 1 not bootable ($p1)";; esac
  [ "$(od -An -tx1 -j510 -N2 "$HDD" | tr -d ' \n')" = 55aa ] && ok "HDD has MBR signature" || bad "HDD MBR signature"
else bad "HDD image missing"; fi

file -L "$IMG/bzImage" | grep -q "Linux kernel x86 boot" && ok "bzImage is an x86 kernel" || bad "bzImage type"

n64=$(find "$T" -type f -exec file {} + 2>/dev/null | grep ELF | grep -vc "ELF 32-bit LSB.*Intel 80386" || true)
[ "$n64" = 0 ] && ok "all ELF files are 32-bit i386" || bad "$n64 non-i386 ELF files"

# SSE2/SSE3/SSSE3/SSE4/AVX use: look for instructions on xmm registers that
# only exist from SSE2 on, per function.  Allowed (documented) exceptions:
#  - libgcc _Float16/bfloat16 helpers (__*hc3, __*hf2, __*bf2): only used by
#    code that itself uses _Float16, which needs SSE2 anyway; nothing here does.
#  - libraries that select SIMD code at run time via CPUID (MMX/SSE paths on
#    a P3): libjpeg-turbo, OpenSSL libcrypto (OPENSSL_ia32cap), xz liblzma
#    (CRC32 via function pointer), libFLAC (cpu.c).  The check below also
#    requires a CPUID instruction to be present in these files.
SSE2RE='[[:space:]](movdqa|movdqu|paddq|psubq|pmuludq|pshufd|pshufhw|pshuflw|punpck[lh]qdq|cvt(si2sd|sd2si|tsd2si|ss2sd|sd2ss|dq2pd|pd2dq)|(add|sub|mul|div|sqrt|max|min|and|or|xor|mov|comi|ucomi)(sd|pd)|movq|movd|movq2dq|vzeroupper|v[a-z]+)[[:space:]].*%(x|y)mm'
cnt=0; list=""; allowed=""
while IFS= read -r f; do
  rel=${f#$T}
  fns=$(objdump -d --no-show-raw-insn "$f" 2>/dev/null | awk -v re="$SSE2RE" '/^[0-9a-f]+ <.*>:$/ {fn=$2} $0 ~ re {c[fn]++} END{for (k in c) print k}')
  [ -z "$fns" ] && continue
  case "$rel" in
    */libjpeg.so*|*/libturbojpeg.so*|*/libcrypto.so*|*/liblzma.so*|*/libFLAC.so*)
      if objdump -d "$f" | grep -q cpuid; then allowed="$allowed $rel(runtime-dispatch)"; continue; fi;;
  esac
  bad_fns=$(echo "$fns" | grep -vE '^<__[a-z0-9]*(hc3|hf[0-9]?|bf[0-9]?)@' || true)
  if [ -n "$bad_fns" ]; then
    cnt=$((cnt+1)); list="$list $rel[$(echo $bad_fns | head -c 120)]"
  else
    allowed="$allowed $rel(_Float16-helpers)"
  fi
done < <(find "$T" -type f \( -perm -u+x -o -name "*.so*" -o -name "*.ko" \) -exec sh -c 'file "$1" | grep -q ELF && echo "$1"' _ {} \;)
[ $cnt = 0 ] && ok "no SSE2+ instructions in code paths used on a Pentium III (allowed:$allowed)" \
  || bad "SSE2+ instructions found in $cnt files:$list"
exit $fail
