#!/bin/bash
# P3 Linux post-image: build live ISO (isolinux + initramfs + squashfs root)
# and a bootable IDE hard disk image (MBR + FAT boot partition with SYSLINUX
# + ext2 root partition).  Uses host tools: xorriso, mksquashfs, syslinux,
# mtools, cpio (see BUILD.md).
set -euo pipefail
BOARD="$(dirname "$(readlink -f "$0")")"
IMG="$BINARIES_DIR"
T="$TARGET_DIR"
SYSLINUX_BIOS=${SYSLINUX_BIOS:-/usr/lib/syslinux/modules/bios}
ISOLINUX_BIN=${ISOLINUX_BIN:-/usr/lib/ISOLINUX/isolinux.bin}
ISOHDPFX=${ISOHDPFX:-/usr/lib/ISOLINUX/isohdpfx.bin}
MBR_BIN=${MBR_BIN:-/usr/lib/syslinux/mbr/mbr.bin}
VERSION=$(sed -n 's/^VERSION="\(.*\)"/\1/p' "$T/etc/os-release")
CMDLINE_COMMON="console=tty0 console=ttyS0,115200 vga=0x314 loglevel=4 net.ifnames=0"

# ---------- initramfs (busybox + musl only) ----------
R="$IMG/initramfs-root"
rm -rf "$R"; mkdir -p "$R"/{bin,lib,proc,sys,dev,medium,ro,rw,newroot}
cp -a "$T/bin/busybox" "$R/bin/"
cp -aL "$T/lib/ld-musl-i386.so.1" "$R/lib/"
for a in sh mount umount mkdir sleep switch_root cat echo ls uname; do ln -sf busybox "$R/bin/$a"; done
install -m 0755 "$BOARD/initramfs/init" "$R/init"
(cd "$R" && find . | LC_ALL=C sort | cpio --quiet -o -H newc --owner=0:0 | gzip -9n) > "$IMG/initrd.gz"

# ---------- squashfs root ----------
rm -f "$IMG/rootfs.sqfs"
mksquashfs "$T" "$IMG/rootfs.sqfs" -comp gzip -b 131072 -noappend -all-root -no-progress \
  -e "$T/THIS_IS_NOT_YOUR_ROOT_FILESYSTEM" >/dev/null

# ---------- ISO ----------
ISO="$IMG/iso"
rm -rf "$ISO"; mkdir -p "$ISO/isolinux" "$ISO/boot" "$ISO/p3linux"
cp "$ISOLINUX_BIN" "$ISO/isolinux/"
cp "$SYSLINUX_BIOS/ldlinux.c32" "$ISO/isolinux/"
cp "$IMG/bzImage" "$ISO/boot/bzImage"
cp "$IMG/initrd.gz" "$ISO/boot/initrd.gz"
cp "$IMG/rootfs.sqfs" "$ISO/p3linux/"
sed "s|@CMDLINE@|$CMDLINE_COMMON|g; s|@VERSION@|$VERSION|g" "$BOARD/isolinux.cfg" > "$ISO/isolinux/isolinux.cfg"

# ---------- HDD image: p1 = FAT16 boot (SYSLINUX), p2 = ext2 root ----------
BOOTMB=16
BOOT="$IMG/bootpart.img"
rm -f "$BOOT"; truncate -s ${BOOTMB}M "$BOOT"
mkfs.vfat -n P3BOOT "$BOOT" >/dev/null
sed "s|@CMDLINE@|$CMDLINE_COMMON|g; s|@VERSION@|$VERSION|g" "$BOARD/syslinux-hdd.cfg" > "$IMG/syslinux.cfg"
mcopy -i "$BOOT" "$IMG/bzImage" ::/BZIMAGE
mcopy -i "$BOOT" "$IMG/syslinux.cfg" ::/SYSLINUX.CFG
syslinux --install "$BOOT"
# installer payload on the CD: boot partition image + MBR code
cp "$BOOT" "$ISO/p3linux/bootpart.img"
cp "$MBR_BIN" "$ISO/p3linux/mbr.bin"

xorriso -as mkisofs -quiet -o "$IMG/P3-Linux.iso" -V P3LINUX -R -J \
  -b isolinux/isolinux.bin -c isolinux/boot.cat -no-emul-boot -boot-load-size 4 -boot-info-table \
  -isohybrid-mbr "$ISOHDPFX" "$ISO"

ROOTFS="$IMG/rootfs.ext2"
ROOT_SECTORS=$(( $(stat -c %s "$ROOTFS") / 512 ))
BOOT_START=2048
BOOT_SECTORS=$(( BOOTMB * 2048 ))
ROOT_START=$(( BOOT_START + BOOT_SECTORS ))
TOTAL=$(( ROOT_START + ROOT_SECTORS ))
HDD="$IMG/P3-Linux-hdd.img"
rm -f "$HDD"; truncate -s $(( TOTAL * 512 )) "$HDD"
sfdisk -q "$HDD" <<EOT
label: dos
start=$BOOT_START, size=$BOOT_SECTORS, type=6, bootable
start=$ROOT_START, size=$ROOT_SECTORS, type=83
EOT
dd if="$MBR_BIN" of="$HDD" bs=440 count=1 conv=notrunc status=none
dd if="$BOOT" of="$HDD" bs=512 seek=$BOOT_START conv=notrunc status=none
dd if="$ROOTFS" of="$HDD" bs=512 seek=$ROOT_START conv=notrunc status=none

ls -l "$IMG/P3-Linux.iso" "$HDD"
