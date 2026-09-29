#!/usr/bin/env python3
"""P3 Linux source fetcher.

Pre-populates Buildroot's download directory (BR2_DL_DIR) for the current
configuration.  For every source file Buildroot needs it:

  1. skips it if it is already present with the correct SHA256,
  2. looks the expected SHA256 (from Buildroot's own .hash files) up in the
     Ubuntu archive Sources indices and downloads the byte-identical upstream
     tarball from archive.ubuntu.com (Ubuntu ships most upstream tarballs
     unmodified as <pkg>_<ver>.orig.tar.*),
  3. otherwise tries the package's upstream URI(s) directly.

Every file is verified against Buildroot's hash before being accepted, so the
result is exactly what Buildroot would have downloaded itself.  This exists
because the original build environment could not reach many upstream hosts
(kernel.org, gnu.org, x.org, ...).  On an unrestricted network you can skip
this script entirely; Buildroot downloads everything itself.

Usage: fetch-sources.py <buildroot-dir> <output-dir> <dl-dir>
"""
import hashlib, json, lzma, os, subprocess, sys, urllib.request

UBUNTU = os.environ.get("UBUNTU_MIRROR", "http://archive.ubuntu.com/ubuntu")
SUITES = ["stonking", "resolute", "resolute-updates", "questing", "questing-updates",
          "plucky", "plucky-updates", "noble", "noble-updates", "noble-security",
          "jammy", "jammy-updates", "focal", "focal-updates"]
COMPONENTS = ["main", "universe"]


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def fetch(url, dest, timeout=60):
    tmp = dest + ".part"
    try:
        with urllib.request.urlopen(url, timeout=timeout) as r, open(tmp, "wb") as f:
            while True:
                b = r.read(1 << 20)
                if not b:
                    break
                f.write(b)
        os.replace(tmp, dest)
        return True
    except Exception as e:  # noqa: BLE001
        if os.path.exists(tmp):
            os.unlink(tmp)
        print(f"    fail {url}: {e}")
        return False


def ubuntu_index(cache):
    """sha256 -> pool path, from all Sources indices."""
    os.makedirs(cache, exist_ok=True)
    idx = {}
    for s in SUITES:
        for c in COMPONENTS:
            f = os.path.join(cache, f"{s}-{c}.xz")
            if not os.path.exists(f):
                fetch(f"{UBUNTU}/dists/{s}/{c}/source/Sources.xz", f, 300)
            if not os.path.exists(f):
                continue
            directory, insha = None, False
            for line in lzma.open(f, "rt", errors="replace"):
                if line.startswith("Directory:"):
                    directory = line.split()[1]
                elif line.startswith("Checksums-Sha256:"):
                    insha = True
                elif insha and line.startswith(" "):
                    h, _, name = line.split()
                    idx.setdefault(h, f"{directory}/{name}")
                else:
                    insha = False
    return idx


def hashes_for(brdir, hashfiles, extdir):
    out = {}
    for hf in hashfiles:
        for base in (brdir, extdir):
            p = hf if os.path.isabs(hf) else os.path.join(base, hf)
            if os.path.exists(p):
                for line in open(p):
                    parts = line.split()
                    if len(parts) == 3 and parts[0] == "sha256":
                        out[parts[2]] = parts[1]
                break
    return out


def main():
    brdir, outdir, dldir = (os.path.abspath(a) for a in sys.argv[1:4])
    extdir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    info = json.loads(subprocess.check_output(["make", "-s", "-C", outdir, "show-info"],
                                              text=True))
    idx = None
    missing = []
    for name, pkg in sorted(info.items()):
        hashes = hashes_for(brdir, pkg.get("hashes", []), extdir)
        for dl in pkg.get("downloads", []):
            src = dl["source"]
            uris = dl["uris"]
            if not uris or uris[0].startswith(("local", "git", "file")):
                continue
            want = hashes.get(src)
            dest_dir = os.path.join(dldir, pkg["dl_dir"])
            dest = os.path.join(dest_dir, src)
            if os.path.exists(dest) and (want is None or sha256(dest) == want):
                continue
            os.makedirs(dest_dir, exist_ok=True)
            print(f"[{name}] {src}")
            ok = False
            if want:
                if idx is None:
                    idx = ubuntu_index(os.path.join(dldir, ".ubuntu-idx"))
                    print(f"  (indexed {len(idx)} Ubuntu source files)")
                if want in idx and fetch(f"{UBUNTU}/{idx[want]}", dest, 600):
                    ok = sha256(dest) == want
                    print("    ubuntu:", idx[want], "OK" if ok else "HASH MISMATCH")
            if not ok:
                for u in uris:
                    if "sources.buildroot.net" in u:
                        continue
                    u = u.split("+", 1)[1] if "+" in u.split("://")[0] else u
                    if fetch(f"{u}/{src}", dest, 60):
                        ok = want is None or sha256(dest) == want
                        print("    upstream:", u, "OK" if ok else "HASH MISMATCH")
                        if ok:
                            break
            if not ok:
                if os.path.exists(dest):
                    os.unlink(dest)
                missing.append(f"{name} {src} {want}")
    print("\n%d missing" % len(missing))
    for m in missing:
        print("  MISSING", m)
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
