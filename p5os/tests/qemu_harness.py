"""Drive QEMU headlessly for P5OS tests.

* keyboard input: QEMU monitor 'sendkey' (goes through the emulated i8042,
  so it exercises the real PS/2 keyboard driver)
* screen contents: 'pmemsave' of the VGA text buffer at 0xB8000 (what the
  kernel actually drew), plus optional PNG screenshots via 'screendump'
* serial log: COM1 written to a file (the console mirrors to COM1)
"""
import os
import re
import socket
import subprocess
import tempfile
import time

QEMU = "qemu-system-i386"

KEYMAP = {
    " ": "spc", "\n": "ret", "-": "minus", "=": "equal", ".": "dot", ",": "comma",
    "/": "slash", ";": "semicolon", "'": "apostrophe", "[": "bracket_left",
    "]": "bracket_right", "\\": "backslash", "`": "grave_accent", "\t": "tab",
}
SHIFTED = {
    "!": "1", "@": "2", "#": "3", "$": "4", "%": "5", "^": "6", "&": "7", "*": "8",
    "(": "9", ")": "0", "_": "minus", "+": "equal", ":": "semicolon", '"': "apostrophe",
    "<": "comma", ">": "dot", "?": "slash", "{": "bracket_left", "}": "bracket_right",
    "|": "backslash", "~": "grave_accent",
}


def key_for(ch):
    if ch.isalpha() and ch.isupper():
        return "shift-" + ch.lower()
    if ch.isalnum():
        return ch
    if ch in KEYMAP:
        return KEYMAP[ch]
    if ch in SHIFTED:
        s = SHIFTED[ch]
        return "shift-" + KEYMAP.get(s, s)
    raise ValueError(f"no key for {ch!r}")


class Qemu:
    def __init__(self, drives, boot="c", mem=16, cpu="pentium", machine=None,
                 extra=None, no_reboot=False, workdir=None):
        self.dir = workdir or tempfile.mkdtemp(prefix="p5os-test-")
        self.serial_path = os.path.join(self.dir, "serial.log")
        self.mon_path = os.path.join(self.dir, "monitor.sock")
        cmd = [QEMU, "-cpu", cpu, "-m", str(mem), "-display", "none",
               "-serial", f"file:{self.serial_path}",
               "-monitor", f"unix:{self.mon_path},server,nowait", "-boot", boot]
        if machine:
            cmd += ["-M", machine]
        if no_reboot:
            cmd += ["-no-reboot"]
        for d in drives:
            cmd += ["-drive", d]
        cmd += extra or []
        self.cmd = cmd
        self.proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.mon = None
        for _ in range(100):
            try:
                s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                s.connect(self.mon_path)
                self.mon = s
                break
            except OSError:
                time.sleep(0.05)
        if not self.mon:
            raise RuntimeError("cannot connect to QEMU monitor: " + self.output())
        self.mon.settimeout(5)
        self._read_prompt()

    def output(self):
        try:
            return self.proc.stdout.read().decode(errors="replace") if self.proc.poll() is not None else ""
        except Exception:
            return ""

    def _read_prompt(self):
        buf = b""
        while not buf.endswith(b"(qemu) "):
            chunk = self.mon.recv(4096)
            if not chunk:
                break
            buf += chunk
        return buf.decode(errors="replace")

    def monitor(self, cmd):
        self.mon.sendall(cmd.encode() + b"\n")
        return self._read_prompt()

    def alive(self):
        return self.proc.poll() is None

    def serial(self):
        try:
            with open(self.serial_path, "rb") as f:
                return f.read().decode("latin-1").replace("\r", "")
        except FileNotFoundError:
            return ""

    def wait_serial(self, pattern, timeout=20, start=0):
        deadline = time.time() + timeout
        rx = re.compile(pattern)
        while time.time() < deadline:
            m = rx.search(self.serial(), start)
            if m:
                return m
            if not self.alive():
                break
            time.sleep(0.1)
        raise TimeoutError(f"pattern {pattern!r} not seen; serial tail:\n{self.serial()[-1500:]}")

    def screen(self):
        """Return the 80x25 VGA text screen as a list of strings."""
        path = os.path.join(self.dir, "vga.bin")
        if os.path.exists(path):
            os.unlink(path)
        self.monitor(f'pmemsave 0xb8000 4000 "{path}"')
        for _ in range(50):
            if os.path.exists(path) and os.path.getsize(path) == 4000:
                break
            time.sleep(0.05)
        data = open(path, "rb").read()
        chars = bytes(data[i] for i in range(0, 4000, 2)).decode("cp437")
        return [chars[r * 80:(r + 1) * 80].rstrip() for r in range(25)]

    def screenshot(self, png_path):
        ppm = os.path.join(self.dir, "shot.ppm")
        self.monitor(f"screendump {ppm}")
        time.sleep(0.3)
        from PIL import Image
        Image.open(ppm).save(png_path)

    def type(self, text, delay=0.03):
        for ch in text:
            self.monitor("sendkey " + key_for(ch))
            time.sleep(delay)

    def key(self, name):
        self.monitor("sendkey " + name)
        time.sleep(0.05)

    def command(self, line, expect=None, timeout=20):
        """Type a shell command, wait for the next prompt, return its output."""
        start = len(self.serial())
        self.type(line + "\n")
        m = self.wait_serial(expect or r"P5OS:[^>\n]*> $", timeout=timeout, start=start)
        if expect:
            self.wait_serial(r"P5OS:[^>\n]*> $", timeout=timeout, start=m.end())
        out = self.serial()[start:]
        # strip the echoed command line and the trailing prompt
        lines = out.split("\n")
        return "\n".join(lines[1:-1])

    def quit(self):
        if self.alive():
            try:
                self.monitor("quit")
            except Exception:
                pass
            try:
                self.proc.wait(5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
