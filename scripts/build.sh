#!/bin/bash
# P3 Linux build script.
#   ./scripts/build.sh            full build (downloads Buildroot if needed)
# Environment:
#   P3_DL_DIR=<dir>               Buildroot download cache (default: ./dl)
#   P3_UBUNTU_SOURCES=0|1         1 (default) = when an upstream tarball cannot be
#                                 downloaded, substitute the Ubuntu archive source
#                                 (see BUILD.md). 0 = pure upstream Buildroot.
#   JOBS=<n>                      parallel jobs (default: nproc)
set -euo pipefail
TOP="$(cd "$(dirname "$0")/.." && pwd)"
BR_TAG=2026.08
BR_COMMIT=d5180309b1b66ef3b8eaccca70ad69be8e0729a1
BR="$TOP/buildroot"
OUT="$TOP/output"
export BR2_DL_DIR="${P3_DL_DIR:-$TOP/dl}"
mkdir -p "$BR2_DL_DIR"

if [ ! -d "$BR/.git" ]; then
  git clone --depth 1 --branch "$BR_TAG" https://gitlab.com/buildroot.org/buildroot.git "$BR"
fi
[ "$(git -C "$BR" rev-parse HEAD)" = "$BR_COMMIT" ] || { echo "buildroot is not at $BR_COMMIT"; exit 1; }

# reset the Buildroot tree, then apply P3 Linux patches
git -C "$BR" checkout -q -- .
git -C "$BR" clean -fdq -- package toolchain
for p in "$TOP"/patches/buildroot/*.patch; do git -C "$BR" apply "$p"; done

make -C "$BR" BR2_EXTERNAL="$TOP" O="$OUT" p3linux_defconfig

# The kernel is fetched from the GitLab linux-stable mirror (git).
KTAG=$(sed -n 's/^BR2_LINUX_KERNEL_CUSTOM_REPO_VERSION="\(.*\)"/\1/p' "$TOP/configs/p3linux_defconfig")
KTAR="$BR2_DL_DIR/linux/linux-$KTAG-git4.tar.gz"
if [ ! -s "$KTAR" ]; then
  mkdir -p "$BR2_DL_DIR/linux"; rm -rf "$BR2_DL_DIR/linux-src"
  git clone -q --depth 1 --branch "$KTAG" https://gitlab.com/linux-kernel/stable.git "$BR2_DL_DIR/linux-src"
  git -C "$BR2_DL_DIR/linux-src" archive --format=tar --prefix="linux-$KTAG/" HEAD | gzip -n > "$KTAR"
  rm -rf "$BR2_DL_DIR/linux-src"
fi

if [ "${P3_UBUNTU_SOURCES:-1}" = 1 ]; then
  python3 "$TOP/scripts/fetch-sources.py" "$BR" "$OUT" "$BR2_DL_DIR" || true
  python3 "$TOP/scripts/ubuntu-subst.py" "$BR" "$OUT" "$BR2_DL_DIR"
fi

make -C "$OUT" -j"${JOBS:-$(nproc)}"
ls -l "$OUT/images/P3-Linux.iso" "$OUT/images/P3-Linux-hdd.img"
