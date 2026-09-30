#!/bin/sh
# Interactive QEMU run: Pentium III, 256MB, IDE CD-ROM + IDE disk, std VGA.
#   ./scripts/run-qemu.sh [RAM_MB] [iso|hdd]
D="$(cd "$(dirname "$0")/.." && pwd)/output/images"
RAM=${1:-256}
if [ "${2:-iso}" = hdd ]; then
  exec qemu-system-i386 -cpu pentium3 -m "$RAM" -vga std -net nic,model=e1000 -net user \
    -drive file="$D/P3-Linux-hdd.img",format=raw,if=ide -boot c
fi
[ -f "$D/test-disk.img" ] || truncate -s 1G "$D/test-disk.img"
exec qemu-system-i386 -cpu pentium3 -m "$RAM" -vga std -net nic,model=e1000 -net user \
  -drive file="$D/test-disk.img",format=raw,if=ide,index=0 \
  -drive file="$D/P3-Linux.iso",media=cdrom,if=ide,index=2 -boot d
