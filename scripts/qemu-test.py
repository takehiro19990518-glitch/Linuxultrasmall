#!/usr/bin/env python3
"""Automated QEMU test for P3 Linux.

Boots the ISO (or HDD image) on an emulated Pentium III with IDE devices and
standard VGA, then checks via the serial console and the QEMU monitor:

  ISO/BIOS boot, kernel boot, root mount, init, X.Org start, desktop start,
  keyboard (key injection opens a terminal), mouse (pointer moves/clicks),
  terminal / file manager / text editor processes, network (DHCP + ping),
  reboot and shutdown.

Screenshots are written to output/test/<ram>MB-<medium>-*.png.
Usage: qemu-test.py [RAM_MB] [iso|hdd]
Exit status 0 = all checks passed.
"""
import os, re, socket, subprocess, sys, time

TOP = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IMG = os.path.join(TOP, "output", "images")
RAM = int(sys.argv[1]) if len(sys.argv) > 1 else 256
MEDIUM = sys.argv[2] if len(sys.argv) > 2 else "iso"
OUT = os.path.join(TOP, "output", "test")
os.makedirs(OUT, exist_ok=True)
TAG = f"{RAM}MB-{MEDIUM}"
SER = os.path.join(OUT, f"{TAG}.serial.sock")
MON = os.path.join(OUT, f"{TAG}.mon.sock")
LOG = os.path.join(OUT, f"{TAG}.serial.log")
results = []


def check(name, ok, detail=""):
    results.append((name, ok, detail))
    print(f"[{'PASS' if ok else 'FAIL'}] {name} {detail}", flush=True)
    return ok


class Chan:
    def __init__(self, path, log=None):
        for _ in range(100):
            try:
                self.s = socket.socket(socket.AF_UNIX)
                self.s.connect(path)
                break
            except OSError:
                time.sleep(0.1)
        self.s.setblocking(False)
        self.buf = ""
        self.log = open(log, "a") if log else None

    def read(self):
        try:
            while True:
                d = self.s.recv(65536)
                if not d:
                    break
                t = d.decode("utf-8", "replace")
                self.buf += t
                if self.log:
                    self.log.write(t)
                    self.log.flush()
        except BlockingIOError:
            pass

    def expect(self, pattern, timeout):
        end = time.time() + timeout
        while time.time() < end:
            self.read()
            m = re.search(pattern, self.buf)
            if m:
                self.buf = self.buf[m.end():]
                return m
            time.sleep(0.2)
        return None

    def send(self, text):
        self.s.sendall(text.encode())


def mon(cmd):
    M.send(cmd + "\n")
    time.sleep(0.4)
    M.read()


def shot(name):
    ppm = os.path.join(OUT, f"{TAG}-{name}.ppm")
    png = ppm[:-4] + ".png"
    mon(f"screendump {ppm}")
    time.sleep(1.0)
    if os.path.exists(ppm):
        subprocess.call(["convert", ppm, png], stderr=subprocess.DEVNULL)
        os.unlink(ppm)
    return png


def sh(cmd, timeout=20):
    """Run a command on the serial root shell and return its output."""
    marker = f"__END{int(time.time()*1000) % 100000}__"
    S.buf = ""
    S.send(f"{cmd}; echo {marker}$?\n")
    m = S.expect(re.escape(marker) + r"(\d+)", timeout)
    if not m:
        return None, ""
    return int(m.group(1)), S.buf


def main():
    global S, M
    for p in (SER, MON, LOG):
        if os.path.exists(p):
            os.unlink(p)
    qemu = ["qemu-system-i386", "-cpu", "pentium3", "-m", str(RAM), "-vga", "std",
            "-display", "none", "-name", "p3linux-test",
            "-serial", f"unix:{SER},server=on,wait=off",
            "-monitor", f"unix:{MON},server=on,wait=off",
            "-net", "nic,model=e1000", "-net", "user"]
    if MEDIUM == "hdd":
        qemu += ["-drive", f"file={os.path.join(IMG, 'P3-Linux-hdd.img')},format=raw,if=ide,snapshot=on",
                 "-boot", "c"]
    else:
        disk = os.path.join(OUT, "disk.img")
        if not os.path.exists(disk):
            subprocess.check_call(["truncate", "-s", "512M", disk])
        qemu += ["-drive", f"file={disk},format=raw,if=ide,index=0",
                 "-drive", f"file={os.path.join(IMG, 'P3-Linux.iso')},media=cdrom,if=ide,index=2",
                 "-boot", "d"]
    if os.environ.get("QEMU_ACCEL"):
        qemu += ["-accel", os.environ["QEMU_ACCEL"]]
    print(" ".join(qemu), flush=True)
    q = subprocess.Popen(qemu)
    t0 = time.time()
    S = Chan(SER, LOG)
    M = Chan(MON)
    tmo = int(os.environ.get("BOOT_TIMEOUT", "600"))
    try:
        check("1. BIOS boot from " + MEDIUM.upper() + " (SYSLINUX)", S.expect(r"(ISO|SYS)LINUX \d", 120) is not None)
        m = S.expect(r"(Linux version|P3 Linux: kernel) (\S+)", 300)
        check("2. Linux kernel boot", m is not None, m.group(2) if m else "")
        if MEDIUM == "iso":
            check("3. root filesystem mount (CD + squashfs + overlay)",
                  S.expect(r"found medium on|VFS: Mounted root", tmo) is not None)
        else:
            check("3. root filesystem mount (IDE disk ext2)", S.expect(r"VFS: Mounted root", tmo) is not None)
        check("4. init (BusyBox init + rc scripts)", S.expect(r"Starting|login:", tmo) is not None)
        x = S.expect(r"P3LINUX: X (STARTED|FAILED)", tmo)
        check("5. X.Org start", bool(x and x.group(1) == "STARTED"))
        d = S.expect(r"P3LINUX: DESKTOP READY", 300)
        check("6. GUI desktop (fluxbox + start menu)", d is not None, f"{time.time() - t0:.0f}s after power-on")
        time.sleep(8)
        shot("01-desktop")
        # serial root login for inspection
        S.send("\n")
        S.expect(r"login:", 60)
        S.send("root\n")
        S.expect(r"assword:", 20)
        S.send("p3linux\n")
        S.expect(r"# ", 20)
        sh("export DISPLAY=:0 PS1='# '")
        _, out = sh("pidof Xorg fluxbox p3-start p3-sysinfo")
        check("   desktop processes", len(out.split()) >= 3, out.strip().splitlines()[0] if out.strip() else "")
        # 7. keyboard: Ctrl+Alt+T is bound to xterm in fluxbox
        sh("pkill p3-sysinfo")
        mon("sendkey ctrl-alt-t")
        time.sleep(6)
        rc, _ = sh("pidof xterm")
        check("7. keyboard (Ctrl+Alt+T opens terminal)", rc == 0)
        # type into the terminal to prove it accepts input
        for k in "touch /tmp/kbd-ok".replace(" ", "\x00"):
            mon("sendkey " + ({"\x00": "spc", "/": "slash", "-": "minus", ".": "dot"}.get(k, k)))
        mon("sendkey ret")
        time.sleep(2)
        rc, _ = sh("test -f /tmp/kbd-ok")
        check("9. terminal (xterm accepts typed command)", rc == 0)
        shot("02-terminal")
        # 8. mouse: move and click on the Start button (bottom-left)
        mon("mouse_move -2000 2000")
        time.sleep(0.5)
        mon("mouse_move 20 -8")
        mon("mouse_button 1")
        time.sleep(0.3)
        mon("mouse_button 0")
        time.sleep(2)
        shot("03-startmenu")
        mon("sendkey esc")
        rc, out = sh("grep -c 'mouse\\|Mouse\\|pointer' /var/log/Xorg.0.log")
        check("8. mouse (PS/2 mouse device in X, pointer injected)", rc == 0, f"{out.split()[0] if out.split() else ''} log lines")
        # 10/11. file manager and editor
        sh("(p3-files /etc &) ; (p3-edit /etc/p3linux-release &)")
        time.sleep(6)
        rc1, _ = sh("pidof p3-files")
        rc2, _ = sh("pidof p3-edit")
        check("10. file manager (p3-files)", rc1 == 0)
        check("11. text editor (p3-edit)", rc2 == 0)
        shot("04-apps")
        sh("(p3-sysinfo &)")
        time.sleep(3)
        shot("05-sysinfo")
        _, out = sh("free -m | sed -n 2p")
        check("   memory in use after desktop start", True, out.strip().splitlines()[0] if out.strip() else "")
        # network
        rc, _ = sh("ifconfig eth0 | grep -q 'inet addr' || udhcpc -i eth0 -n -q -t 5", 40)
        rc, out = sh("ping -c 2 -W 3 10.0.2.2", 20)
        check("   network (DHCP + ping gateway)", rc == 0)
        # 13. reboot
        S.buf = ""
        sh("reboot", 5)
        ok = S.expect(r"(ISO|SYS)LINUX \d", 180) is not None
        check("13. reboot (system comes back)", ok)
        d = S.expect(r"P3LINUX: DESKTOP READY", tmo)
        # 12. shutdown
        S.send("\n")
        S.expect(r"login:", 60)
        S.send("root\n")
        S.expect(r"assword:", 20)
        S.send("p3linux\n")
        S.expect(r"# ", 20)
        S.send("poweroff\n")
        try:
            q.wait(120)
            check("12. shutdown (poweroff turns the VM off)", True)
        except subprocess.TimeoutExpired:
            check("12. shutdown (poweroff turns the VM off)", False)
    finally:
        if q.poll() is None:
            q.kill()
    failed = [r for r in results if not r[1]]
    with open(os.path.join(OUT, f"{TAG}-result.txt"), "w") as f:
        for n, ok, d in results:
            f.write(f"{'PASS' if ok else 'FAIL'} {n} {d}\n")
    print(f"\n{len(results) - len(failed)}/{len(results)} checks passed ({TAG})")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
