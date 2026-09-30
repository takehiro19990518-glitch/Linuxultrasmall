#!/bin/sh
# Automated QEMU test of the P3 Linux ISO (Pentium III CPU model, IDE, VGA).
#   ./scripts/test-qemu.sh [RAM_MB] [iso|hdd]
exec python3 "$(dirname "$0")/qemu-test.py" "$@"
