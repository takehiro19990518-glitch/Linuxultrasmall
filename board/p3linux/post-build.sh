#!/bin/sh
# P3 Linux post-build: inittab entries, release file, permissions
set -e
T="$1"
VERSION="${P3_VERSION:-0.1}"
echo "P3 Linux $VERSION (i686/Pentium III)" > "$T/etc/p3linux-release"
cat > "$T/etc/os-release" <<EOT
NAME="P3 Linux"
VERSION="$VERSION"
ID=p3linux
PRETTY_NAME="P3 Linux $VERSION"
EOT
# GUI on tty1, serial console login on ttyS0 (used by automated tests)
grep -q p3-startx "$T/etc/inittab" || \
  sed -i '/# Put a getty on the serial port/i tty1::respawn:/usr/sbin/p3-startx' "$T/etc/inittab"
grep -q '^ttyS0::' "$T/etc/inittab" || \
  echo 'ttyS0::respawn:/sbin/getty -L ttyS0 115200 vt100' >> "$T/etc/inittab"
chmod 0755 "$T"/usr/sbin/p3-* "$T"/usr/bin/p3-session "$T"/usr/bin/p3-setres "$T"/usr/sbin/p3-install 2>/dev/null || true
mkdir -p "$T/mnt/cdrom" "$T/mnt/hd" "$T/mnt/floppy"
# docs / man pages are useless on a 64MB machine
rm -rf "$T/usr/share/man" "$T/usr/share/doc" "$T/usr/share/info"
