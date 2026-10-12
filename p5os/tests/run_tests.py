#!/usr/bin/env python3
"""P5OS automated test suite (QEMU, headless).

  python3 tests/run_tests.py                 # everything, 2-minute soak test
  python3 tests/run_tests.py --soak 600      # 10-minute soak test
  python3 tests/run_tests.py -k fs           # only tests whose name contains 'fs'

Disk images are copied to a temporary directory before each test, so the
images in images/ are never written.  Results go to tests/results/.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
from qemu_harness import Qemu  # noqa: E402
import p5fs  # noqa: E402

FLOPPY = os.path.join(ROOT, "images", "p5os-floppy.img")
HDD = os.path.join(ROOT, "images", "p5os-hdd.img")
RESULTS = os.path.join(HERE, "results")
PROMPT = r"P5OS[^>\n]*> $"
CPU_P5 = "pentium,model=1,stepping=7,-mmx,-apic"   # closest to a P5 60/66

WORK = tempfile.mkdtemp(prefix="p5os-tests-")


class Fail(Exception):
    pass


def check(cond, msg):
    if not cond:
        raise Fail(msg)


def scratch(src, name):
    dst = os.path.join(WORK, name)
    shutil.copyfile(src, dst)
    return dst


def data_disk(name="data.img", mb=4):
    path = os.path.join(WORK, name)
    with open(path, "w+b") as f:
        f.truncate(mb * 1024 * 1024)
        p5fs.mkfs(f, 0, mb * 2048, "DATA")
    return path


def hdd_vm(img=None, extra_drives=(), **kw):
    img = img or scratch(HDD, "hdd.img")
    drives = [f"file={img},format=raw,index=0,media=disk"]
    for i, d in enumerate(extra_drives, start=1):
        drives.append(f"file={d},format=raw,index={i},media=disk")
    return Qemu(drives, boot="c", **kw)


def floppy_vm(hdd=None, **kw):
    drives = [f"file={FLOPPY},format=raw,if=floppy,readonly=on"]
    if hdd:
        drives.append(f"file={hdd},format=raw,index=0,media=disk")
    return Qemu(drives, boot="a", **kw)


def boot(q, timeout=20):
    t0 = time.time()
    q.wait_serial(PROMPT, timeout)
    return time.time() - t0


def parse_uptime(out):
    m = re.search(r"(\d+) days, (\d\d):(\d\d):(\d\d)\.(\d{3})", out)
    if not m:
        raise Fail("cannot parse uptime: " + out)
    d, h, mi, s, ms = (int(x) for x in m.groups())
    return d * 86400 + h * 3600 + mi * 60 + s + ms / 1000


def host_fs(img):
    f = open(img, "rb")
    return f, p5fs.P5FS(f, p5fs.find_partition(f))


# --------------------------------------------------------------------------- tests

def t_boot_hdd_16mb(log):
    q = hdd_vm(cpu=CPU_P5)
    try:
        dt = boot(q)
        scr = "\n".join(q.screen())
        log(f"booted to prompt in {dt:.1f} s (QEMU, -cpu {CPU_P5}, -m 16)")
        for s in ["P5OS Booted Successfully", "P5OS 1.0", "Memory: 16 MB",
                  "Intel Pentium (P5, 60/66 MHz class)", "Storage: P5FS mounted",
                  "Type 'help' for commands."]:
            check(s in scr, f"'{s}' not on screen")
        q.screenshot(os.path.join(RESULTS, "boot-hdd.png"))
        log("screen:\n    " + "\n    ".join(l for l in q.screen() if l))
    finally:
        q.quit()


def t_boot_floppy_data_disk(log):
    data = data_disk()
    q = floppy_vm(hdd=data)
    try:
        dt = boot(q)
        log(f"floppy boot to prompt in {dt:.1f} s")
        check("boot: drive 00" in q.serial(), "boot drive is not the floppy")
        out = q.command("mount")
        check('"DATA" on hd0 (LBA 0)' in out, "data disk not mounted: " + out)
        log(out)
    finally:
        q.quit()


def t_boot_isapc(log):
    """ISA-only PC (no PCI), like early Pentium boards' legacy devices."""
    q = floppy_vm(hdd=scratch(HDD, "isa.img"), machine="isapc", cpu=CPU_P5)
    try:
        boot(q)
        out = q.command("ls")
        check("README.TXT" in out, "ls on isapc failed")
        log("isapc: floppy boot + ISA IDE disk OK")
    finally:
        q.quit()


def t_memory_sizes(log):
    for mem, expect_boot in [(1, False), (2, True), (4, True), (8, True), (16, True), (64, True)]:
        q = hdd_vm(mem=mem)
        try:
            if expect_boot:
                boot(q)
                out = q.command("mem")
                m = re.search(r"Installed RAM : (\d+) MB", out)
                check(m and int(m.group(1)) == mem, f"-m {mem}: wrong size: {out}")
                heap = re.search(r"Kernel heap   : (\d+) KB", out).group(1)
                free = re.search(r"Free memory   : (\d+) KB", out).group(1)
                log(f"-m {mem:3}: boots, heap {heap} KB, {free} KB free")
            else:
                time.sleep(4)
                scr = "\n".join(q.screen())
                check("needs at least 512 KB of RAM above 1 MB" in scr,
                      f"-m {mem}: expected memory error, screen: {scr[-300:]}")
                log(f"-m {mem:3}: rejected by stage 2 with an error message (expected)")
        finally:
            q.quit()


def t_shell_commands(log):
    q = hdd_vm()
    try:
        boot(q)
        out = q.command("help")
        for c in ["help", "ver", "mem", "cls", "echo", "uptime", "reboot", "halt", "ls",
                  "cat", "mkdir", "write", "rm"]:
            check(re.search(rf"^  {c}\b", out, re.M), f"help lacks '{c}'")
        check("P5OS 1.0" in q.command("ver"), "ver")
        check(q.command("echo Hello, World!").strip() == "Hello, World!", "echo")
        check(q.command('echo  spaced   "text"').strip() == 'spaced   "text"', "echo spacing")
        up = q.command("uptime")
        check(re.search(r"Uptime: 0 days, 00:00:\d\d\.\d{3}", up), "uptime: " + up)
        check("unknown command" in q.command("frobnicate"), "unknown command")
        check("Installed RAM" in q.command("mem"), "mem")
        check("usable" in q.command("mem map"), "mem map")
        check("Features:" in q.command("cpu"), "cpu")
        check("pmm:" in q.command("dmesg"), "dmesg")
        # history (Up arrow) and line editing
        q.command("echo first-entry")
        start = len(q.serial())
        q.key("up")
        q.type("\n")
        q.wait_serial(PROMPT, 10, start)
        check("first-entry" in q.serial()[start:], "history recall")
        start = len(q.serial())
        q.type("echo abcX")
        q.key("backspace")
        q.type("d\n")
        q.wait_serial(PROMPT, 10, start)
        check("\nabcd\n" in q.serial()[start:], "backspace editing")
        start = len(q.serial())
        q.type("echo never")
        q.key("ctrl-c")
        q.wait_serial(PROMPT, 10, start)
        check("^C" in q.serial()[start:] and "\nnever\n" not in q.serial()[start:], "Ctrl+C")
        q.command("cls")
        time.sleep(0.3)
        scr = [l for l in q.screen() if l]
        check(len(scr) == 1 and scr[0].startswith("P5OS"), f"cls left {scr}")
        log("help/ver/echo/uptime/mem/cpu/dmesg/cls, history, backspace, Ctrl+C: OK")
        out = q.command("cpu")
        check("keys dropped=0" in out, "keyboard dropped keys")
        log(out.splitlines()[-1])
    finally:
        q.quit()


def t_memtest(log):
    q = hdd_vm(cpu=CPU_P5)
    try:
        boot(q)
        before = q.command("mem")
        out = q.command("memtest", timeout=60)
        log(out)
        check("all tests passed" in out, "memtest failed")
        after = q.command("mem")
        used = lambda s: re.search(r"used (\d+) B in (\d+) blocks", s).groups()
        check(used(before) == used(after), f"heap usage changed {used(before)} -> {used(after)}")
        log(f"heap in use before/after: {used(before)} / {used(after)} (no leak)")
    finally:
        q.quit()


def t_fs_operations(log):
    img = scratch(HDD, "fs.img")
    q = hdd_vm(img)
    try:
        boot(q)
        steps = [
            ("ls", "README.TXT"),
            ("cat hello.txt", "Hello from P5FS."),
            ("write note.txt Hello, P5FS!", "note.txt: 13 bytes"),
            ("append note.txt line two", "note.txt: 22 bytes"),
            ("cat note.txt", "Hello, P5FS!\nline two"),
            ("mkdir docs", ""),
            ("mkdir docs", "already exists"),
            ("cd docs", ""),
            ("pwd", "/docs"),
            ("write a.txt alpha", "a.txt: 6 bytes"),
            ("cd ..", ""),
            ("ls docs", "a.txt"),
            ("rm docs", "directory not empty"),
            ("rm docs/a.txt", ""),
            ("rmdir docs", ""),
            ("cat docs/a.txt", "no such file or directory"),
            ("write tmp.txt x", "tmp.txt: 2 bytes"),
            ("rm tmp.txt", ""),
            ("cat README.TXT", "Welcome to P5OS!"),
            ("rm /", "invalid argument"),
            ("write abcdefghijklmnopqrstuvwxyz0123.txt x", "name too long"),
            ("fsck", "clean (0 problem(s))"),
        ]
        for cmd, expect in steps:
            out = q.command(cmd, timeout=30)
            check(expect in out, f"'{cmd}': expected '{expect}', got: {out}")
        # interactive multi-line write
        start = len(q.serial())
        q.type("write multi.txt\n")
        q.wait_serial(r"\| $", 10, start)
        for line in ["first line", "second line", "."]:
            s2 = len(q.serial())
            q.type(line + "\n")
            q.wait_serial(r"(\| |P5OS[^>\n]*> )$", 10, s2)
        check("multi.txt: 23 bytes" in q.serial()[start:], "interactive write")
        log("write/append/cat/mkdir/cd/pwd/ls/rm/rmdir/fsck + error cases + interactive write: OK")
    finally:
        q.quit()
    f, fs = host_fs(img)
    try:
        errs = fs.check()
        check(not errs, f"host fsck errors: {errs}")
        names = sorted(n for n, _ in fs.entries(fs.root) if n not in (".", ".."))
        check(names == ["README.TXT", "hello.txt", "multi.txt", "note.txt"], f"files {names}")
        check(fs.read_data(fs.read_inode(fs.lookup("note.txt"))) == b"Hello, P5FS!\nline two\n",
              "note.txt content")
        check(fs.read_data(fs.read_inode(fs.lookup("multi.txt"))) == b"first line\nsecond line\n",
              "multi.txt content")
        log(f"host-side check of the disk image: OK, files {names}")
    finally:
        f.close()


def t_disktest_and_persistence(log):
    data = data_disk("persist.img")
    q = floppy_vm(hdd=data)
    try:
        boot(q)
        out = q.command("disktest", timeout=120)
        log(out)
        check("disktest: PASS" in out, "disktest failed")
        q.command("mkdir keep")
        q.command("write keep/survivor.txt still here after reboot")
        q.command("reboot", expect="P5OS Booted Successfully", timeout=60)
    finally:
        q.quit()
    q = floppy_vm(hdd=data)                     # fresh power-on
    try:
        boot(q)
        out = q.command("cat keep/survivor.txt")
        check("still here after reboot" in out, "file lost after reboot: " + out)
        check("clean" in q.command("fsck"), "fsck after reboot")
        log("file written before reboot is intact after a cold restart; fsck clean")
    finally:
        q.quit()
    f, fs = host_fs(data)
    try:
        check(not fs.check(), "host fsck")
    finally:
        f.close()


def t_mount_switch(log):
    data = data_disk("second.img")
    q = hdd_vm(extra_drives=[data])
    try:
        boot(q)
        check('"P5OS" on hd0' in q.command("mount"), "hd0 not mounted first")
        out = q.command("mount hd1")
        check('"DATA" on hd1' in out, "mount hd1: " + out)
        q.command("write on-hd1.txt data disk")
        check("no such disk" in q.command("sector hd3 0"), "sector on missing disk")
        out = q.command("mount hd9")
        check("no such disk" in out and '"DATA" on hd1' in out, "bad device handling: " + out)
        out = q.command("mount hd0")
        check('"P5OS" on hd0' in out, "remount hd0")
        check("on-hd1.txt" not in q.command("ls"), "wrong disk listed")
        log("mount hd0 -> hd1 -> (bad hd9 rejected) -> hd0: OK")
    finally:
        q.quit()
    f, fs = host_fs(data)
    try:
        check(fs.read_data(fs.read_inode(fs.lookup("on-hd1.txt"))) == b"data disk\n", "hd1 file")
    finally:
        f.close()


def t_reboot(log):
    q = hdd_vm()
    try:
        boot(q)
        q.command("reboot", expect="P5OS Booted Successfully", timeout=60)
        boot(q)
        check(q.serial().count("P5OS Booted Successfully") == 2, "no second boot")
        log("'reboot' (8042 reset pulse): system restarted and reached the prompt")
        start = len(q.serial())
        q.key("ctrl-alt-delete")
        q.wait_serial("P5OS Booted Successfully", 60, start)
        q.wait_serial(PROMPT, 30, start)
        log("Ctrl+Alt+Del: system restarted")
        for i in range(3):
            q.command("reboot", expect="P5OS Booted Successfully", timeout=60)
        n = q.serial().count("P5OS Booted Successfully")
        check(n == 6, f"expected 6 boots, saw {n}")
        log(f"{n} consecutive boots in one QEMU session")
    finally:
        q.quit()


def t_halt(log):
    q = hdd_vm()
    try:
        boot(q)
        q.type("halt\n")
        q.wait_serial("System halted", 10)
        time.sleep(0.5)
        regs = q.monitor("info registers")
        check("HLT=1" in regs, "CPU is not in HLT state")
        n = len(q.serial())
        q.type("echo x\n")
        time.sleep(1)
        check(len(q.serial()) == n, "system still reacting after halt")
        check(q.alive(), "QEMU died")
        log("'halt': message shown, CPU in HLT with interrupts off, no further reaction")
    finally:
        q.quit()


def t_exceptions(log):
    cases = {
        "div0": "#DE Divide error",
        "gpf": "#GP General protection fault",
        "ud": "#UD Invalid opcode",
        "null": "NULL pointer dereference",
        "stack": "KERNEL STACK OVERFLOW",
        "panic": "test panic requested from shell",
    }
    for kind, expect in cases.items():
        q = hdd_vm(no_reboot=True)
        try:
            boot(q)
            q.type(f"crash {kind}\n")
            q.wait_serial("System halted. Press reset", 10)
            time.sleep(0.3)
            scr = "\n".join(q.screen())
            check("*** P5OS KERNEL PANIC ***" in scr and expect in scr,
                  f"crash {kind}: screen lacks '{expect}'")
            check(q.alive(), f"crash {kind}: machine reset (triple fault?)")
            if kind == "stack":
                q.screenshot(os.path.join(RESULTS, "panic-stack-overflow.png"))
            log(f"crash {kind:5}: panic screen shows '{expect}'")
        finally:
            q.quit()


def t_soak(log, seconds):
    data = data_disk("soak.img")
    q = hdd_vm(extra_drives=[data], cpu=CPU_P5)
    try:
        boot(q)
        q.command("mount hd1")
        base = q.command("mem")
        heap_used = lambda s: re.search(r"used (\d+) B in (\d+) blocks", s).groups()
        t0 = time.time()
        up0 = parse_uptime(q.command("uptime"))
        iters = 0
        while time.time() - t0 < seconds:
            q.command(f"write soak{iters % 5}.txt iteration {iters}")
            q.command(f"append log.txt line {iters}")
            if iters % 10 == 0:
                check("all tests passed" in q.command("memtest heap", timeout=60), "memtest")
            if iters % 25 == 0:
                check("PASS" in q.command("disktest", timeout=120), "disktest")
            check("clean" in q.command("fsck", timeout=60), "fsck")
            iters += 1
        wall = time.time() - t0
        up = parse_uptime(q.command("uptime"))
        drift = (up - up0 - wall) / wall * 100
        check(abs(drift) < 3, f"uptime drift {drift:.2f}%")
        after = q.command("mem")
        check(heap_used(base) == heap_used(after), f"heap leak {heap_used(base)} -> {heap_used(after)}")
        check("canary ok" in after, "stack canary")
        peak = re.search(r"peak use (\d+) bytes", after).group(1)
        lines = q.command("cat log.txt").strip().splitlines()
        check(len(lines) == iters and lines[-1] == f"line {iters - 1}", "log.txt content")
        log(f"{wall:.0f} s soak: {iters} iterations of write/append/fsck (+memtest, disktest); "
            f"uptime drift {drift:+.2f}%, heap usage unchanged, peak kernel stack {peak} bytes")
        log(q.command("cpu").splitlines()[-1])
    finally:
        q.quit()
    f, fs = host_fs(data)
    try:
        check(not fs.check(), "host fsck after soak")
    finally:
        f.close()


TESTS = [
    ("boot_hdd_16mb_p5", t_boot_hdd_16mb),
    ("boot_floppy_data_disk", t_boot_floppy_data_disk),
    ("boot_isapc_no_pci", t_boot_isapc),
    ("memory_sizes", t_memory_sizes),
    ("shell_commands", t_shell_commands),
    ("memtest", t_memtest),
    ("fs_operations", t_fs_operations),
    ("fs_disktest_persistence", t_disktest_and_persistence),
    ("fs_mount_switch", t_mount_switch),
    ("reboot", t_reboot),
    ("halt", t_halt),
    ("exceptions", t_exceptions),
    ("soak", None),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-k", default="", help="run tests whose name contains this")
    ap.add_argument("--soak", type=int, default=120, help="soak test duration (s)")
    a = ap.parse_args()
    os.makedirs(RESULTS, exist_ok=True)
    qv = subprocess.run(["qemu-system-i386", "--version"], capture_output=True,
                        text=True).stdout.splitlines()[0]
    report = [f"P5OS test report - {time.strftime('%Y-%m-%d %H:%M:%S')}", qv, ""]
    passed = failed = 0
    for name, fn in TESTS:
        if a.k not in name:
            continue
        lines = []
        log = lambda s: lines.append(s)
        t0 = time.time()
        try:
            if name == "soak":
                t_soak(log, a.soak)
            else:
                fn(log)
            status = "PASS"
            passed += 1
        except Exception as e:
            status = "FAIL"
            failed += 1
            lines.append(f"ERROR: {e}" if isinstance(e, Fail) else traceback.format_exc())
        hdr = f"[{status}] {name} ({time.time() - t0:.1f} s)"
        print(hdr, flush=True)
        report.append(hdr)
        for l in lines:
            for sub in l.splitlines():
                report.append("    " + sub)
                if status == "FAIL":
                    print("    " + sub)
    summary = f"\n{passed} passed, {failed} failed"
    print(summary)
    report.append(summary)
    if not a.k:
        with open(os.path.join(RESULTS, "test-report.txt"), "w") as f:
            f.write("\n".join(report) + "\n")
    shutil.rmtree(WORK, ignore_errors=True)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
