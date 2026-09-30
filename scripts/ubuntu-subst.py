#!/usr/bin/env python3
"""Substitute unreachable upstream sources with Ubuntu archive sources.

Run after fetch-sources.py.  For every Buildroot package whose exact upstream
tarball could not be obtained, this script:

  * picks a source tarball of the same software from the Ubuntu archive
    (pinned in scripts/ubuntu-sources.lock once chosen; the lock records the
    archive path and its SHA256, which is checked on download),
  * repacks it deterministically under the file name Buildroot expects for
    that version (inner upstream tarballs, e.g. gcc, are used directly;
    Debian "bundle" packages such as xfonts-utils are split per component),
  * changes the package's <PKG>_VERSION in the Buildroot tree when the version
    differs, and appends the SHA256 of the repacked tarball to the package's
    .hash file.

Buildroot then performs its normal extract / patch / build steps, including
its own hash verification.  build.sh resets and patches the Buildroot
checkout before running this script, so the whole sequence is idempotent.

On an unrestricted network none of this is needed; see BUILD.md.

Usage: ubuntu-subst.py <buildroot-dir> <output-dir> <dl-dir>
"""
import hashlib, json, lzma, os, re, shutil, subprocess, sys, tarfile, tempfile, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
UBUNTU = os.environ.get("UBUNTU_MIRROR", "http://archive.ubuntu.com/ubuntu")
LOCK = os.path.join(HERE, "ubuntu-sources.lock")
MAP = os.path.join(HERE, "ubuntu-src-map.txt")
SUITES = ["stonking", "resolute", "resolute-updates", "questing", "questing-updates",
          "plucky", "plucky-updates", "noble", "noble-updates", "noble-security",
          "jammy", "jammy-updates", "focal", "focal-updates"]


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for c in iter(lambda: f.read(1 << 20), b""):
            h.update(c)
    return h.hexdigest()


def fetch(url, dest):
    tmp = dest + ".part"
    with urllib.request.urlopen(url, timeout=600) as r, open(tmp, "wb") as f:
        shutil.copyfileobj(r, f, 1 << 20)
    os.replace(tmp, dest)


def vkey(v):
    return [(1, int(x)) if x.isdigit() else (0, x) for x in re.split(r"[.\-+~_]", v) if x != ""]


def vlt(a, b):
    ka, kb = vkey(a), vkey(b)
    for x, y in zip(ka, kb):
        if x == y:
            continue
        return x < y
    return len(ka) < len(kb)


def upstream_version(v):
    v = v.split(":")[-1]
    if "really" in v:
        v = v.split("really")[-1]
    v = re.sub(r"\+(20\d{6})$", r"-\1", v)  # ncurses snapshot dates
    v = re.sub(r"[+.~]dfsg\d*.*$", "", v)
    v = re.sub(r"(\+|build|ubuntu).*$", "", v) if re.match(r"^7\.7\+", v) is None else v
    return v


def ubuntu_index(cache):
    """source package -> list of (filename, pool path, sha256)."""
    idx = {}
    for s in SUITES:
        for c in ("main", "universe"):
            f = os.path.join(cache, f"{s}-{c}.xz")
            if not os.path.exists(f):
                try:
                    fetch(f"{UBUNTU}/dists/{s}/{c}/source/Sources.xz", f)
                except Exception:  # noqa: BLE001
                    continue
            pkg = d = None
            ins = False
            for line in lzma.open(f, "rt", errors="replace"):
                if line.startswith("Package:"):
                    pkg = line.split()[1]
                elif line.startswith("Directory:"):
                    d = line.split()[1]
                elif line.startswith("Checksums-Sha256:"):
                    ins = True
                elif ins and line.startswith(" "):
                    h, _, name = line.split()
                    if re.search(r"\.tar\.(gz|xz|bz2)$", name) and ".debian." not in name \
                            and not re.search(r"\.orig-[^.]+\.tar", name):
                        idx.setdefault(pkg, {})[name] = (f"{d}/{name}", h)
                else:
                    ins = False
    return idx


def choose(cands, src, bver):
    vers = []
    for name, (path, h) in cands.items():
        m = re.match(re.escape(src) + r"_(.+?)(\.orig)?\.tar\.\w+$", name)
        if m:
            vers.append((upstream_version(m.group(1)), m.group(1), name, path, h))
    # ignore date snapshots (e.g. gcc-15_15-20250404) unless Buildroot uses one
    if not re.search(r"20\d{6}", bver):
        vers = [v for v in vers if not re.search(r"(^|[-+~.])20\d{6}", v[0])]
    if not vers:
        return None
    exact = [v for v in vers if v[0] == bver]
    if exact:
        return exact[0]
    newer = sorted([v for v in vers if not vlt(v[0], bver)], key=lambda v: vkey(v[0]))
    if newer:
        return newer[0]
    return sorted(vers, key=lambda v: vkey(v[0]))[-1]


def detect_version(root):
    for fn in ("configure.ac", "meson.build", "configure.in"):
        p = os.path.join(root, fn)
        if os.path.exists(p):
            t = open(p, errors="replace").read()
            m = re.search(r"AC_INIT\(\s*\[?[^,\]]+\]?\s*,\s*\[?([0-9][^,\]\)\s]*)", t) or \
                re.search(r"version\s*:\s*'([0-9][^']*)'", t)
            if m:
                return m.group(1)
    return None


def repack(root, topname, dest):
    ext = dest.rsplit(".tar.", 1)[-1] if ".tar." in dest else ("gz" if dest.endswith(".tgz") else None)
    raw = dest + ".tar"
    subprocess.check_call(["tar", "--sort=name", "--mtime=@1767225600", "--clamp-mtime", "--owner=0", "--group=0",
                           "--numeric-owner", "--format=gnu", "-cf", raw,
                           "--transform", f"s,^\\.,{topname},", "-C", root, "."])
    comp = {"xz": ["xz", "-T1", "-6", "-f"], "gz": ["gzip", "-n", "-9", "-f"],
            "bz2": ["bzip2", "-9", "-f"]}[ext]
    subprocess.check_call(comp + [raw])
    produced = raw + {"xz": ".xz", "gz": ".gz", "bz2": ".bz2"}[ext]
    os.replace(produced, dest)


def _gmp_fixup(root):
    # Ubuntu's gmp is "+dfsg" (GFDL manual removed); give automake an empty doc/
    os.makedirs(os.path.join(root, "doc"), exist_ok=True)
    open(os.path.join(root, "doc", "Makefile.am"), "a").close()


def _gawk_fixup(root):
    # Ubuntu's gawk is "+dfsg" (GFDL manual removed); make doc/ a no-op
    d = os.path.join(root, "doc")
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, "Makefile.in"), "w") as f:
        f.write("all install install-strip install-data install-exec uninstall check installcheck "
                "clean distclean mostlyclean maintainer-clean info dvi pdf html:\n\t@:\n")


# per-package fixups for Debian-modified (dfsg) source trees
FIXUPS = {"gmp": _gmp_fixup, "gawk": _gawk_fixup}


def patch_dirs(pdir, ver):
    d = os.path.join(pdir, ver)
    return [d] if os.path.isdir(d) else [pdir]


def trial_patches(root, pdir, ver):
    """Apply the package's patches to root; return names of those that fail.
    Successfully applied patches are reverted again afterwards."""
    applied, failed = [], []
    for d in patch_dirs(pdir, ver):
        for pf in sorted(f for f in os.listdir(d) if f.endswith(".patch")):
            full = os.path.join(d, pf)
            r = subprocess.run(["patch", "-F0", "-p1", "-N", "-s", "--dry-run", "-d", root, "-i", full],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            if r.returncode == 0:
                subprocess.check_call(["patch", "-F0", "-p1", "-N", "-s", "-d", root, "-i", full],
                                      stdout=subprocess.DEVNULL)
                applied.append(full)
            else:
                failed.append(pf)
    for full in reversed(applied):
        subprocess.check_call(["patch", "-p1", "-R", "-s", "-d", root, "-i", full], stdout=subprocess.DEVNULL)
    return failed


def write_lock(lock):
    with open(LOCK, "w") as f:
        f.write("# dl_dir  ubuntu-pool-path  sha256  version  (generated by ubuntu-subst.py)\n")
        for k in sorted(lock):
            f.write(f"{k} {' '.join(lock[k])}\n")


def main():
    brdir, outdir, dldir = (os.path.abspath(a) for a in sys.argv[1:4])
    # (the Buildroot tree is reset and patched by build.sh before this runs)
    info = json.loads(subprocess.check_output(["make", "-s", "-C", outdir, "show-info"], text=True))
    mapping = {}
    for line in open(MAP):
        p = line.split("#")[0].split()
        if p:
            mapping[p[0]] = (p[1], p[2] if len(p) > 2 else None)
    lock = {}
    if os.path.exists(LOCK):
        for line in open(LOCK):
            p = line.split()
            if p and not p[0].startswith("#"):
                lock[p[0]] = p[1:]
    idx = None
    report = []
    done = set()
    for name, pkg in sorted(info.items()):
        dld = pkg.get("dl_dir")
        if dld not in mapping or dld in done:
            continue
        dls = [d for d in pkg.get("downloads", []) if d["uris"] and not d["uris"][0].startswith(("git", "local"))]
        if not dls:
            continue
        src_file = dls[0]["source"]
        exact = os.path.join(dldir, dld, src_file)
        want = None
        for hfile in pkg.get("hashes", []):
            hp = os.path.join(brdir, hfile)
            if os.path.exists(hp):
                for l in open(hp):
                    q = l.split()
                    if len(q) == 3 and q[0] == "sha256" and q[2] == src_file:
                        want = q[1]
        if os.path.exists(exact) and want and sha256(exact) == want:
            # exact upstream file already fetched: nothing to substitute
            done.add(dld)
            continue
        bver = pkg["version"]
        usrc, subdir = mapping[dld]
        if dld in lock:
            path, h, newver = lock[dld][0], lock[dld][1], lock[dld][2]
            skipped = None if len(lock[dld]) < 4 else ([] if lock[dld][3] == "-" else lock[dld][3].split(","))
            uname = os.path.basename(path)
        else:
            if idx is None:
                idx = ubuntu_index(os.path.join(dldir, ".ubuntu-idx"))
            c = choose(idx.get(usrc, {}), usrc, bver)
            if not c:
                print(f"!! no Ubuntu source for {dld} ({usrc})")
                continue
            _, _, uname, path, h = c
            newver = None
            skipped = None
        cache = os.path.join(dldir, ".ubuntu-src")
        os.makedirs(cache, exist_ok=True)
        ufile = os.path.join(cache, uname)
        if not os.path.exists(ufile) or sha256(ufile) != h:
            print(f"   download {path}")
            fetch(f"{UBUNTU}/{path}", ufile)
        if sha256(ufile) != h:
            sys.exit(f"SHA256 mismatch for {path}")
        # Ubuntu often ships the pristine upstream tarball: if it matches any
        # hash Buildroot already knows for this file, use it unchanged.
        known = []
        for hfile in pkg.get("hashes", []):
            hp = os.path.join(brdir, hfile)
            if os.path.exists(hp):
                known += [l.split() for l in open(hp) if len(l.split()) == 3 and l.split()[2] == src_file]
        if known and os.path.splitext(uname)[1] == os.path.splitext(src_file)[1]:
            if all(hashlib.new(a, open(ufile, "rb").read()).hexdigest() == v for a, v, _ in known):
                os.makedirs(os.path.join(dldir, dld), exist_ok=True)
                shutil.copyfile(ufile, exact)
                lock[dld] = [path, h, bver, "-"]
                report.append(f"{dld:28s} {bver:>14s} == pristine upstream  {path}")
                print(report[-1])
                done.add(dld)
                write_lock(lock)
                continue
        cached = os.path.join(dldir, dld, src_file.replace(bver, newver)) if newver else None
        tmp = tempfile.mkdtemp(prefix="p3subst-")
        try:
            if cached and os.path.exists(cached) and newver != bver and skipped is not None:
                new_src, dest = os.path.basename(cached), cached  # repacked on an earlier run
                raise StopIteration
            with tarfile.open(ufile) as t:
                t.extractall(tmp, filter="tar") if hasattr(tarfile, "data_filter") else t.extractall(tmp)
            tops = os.listdir(tmp)
            root = os.path.join(tmp, tops[0]) if len(tops) == 1 else tmp
            upname = re.match(r"(.+?)-" + re.escape(bver), src_file)
            upname = upname.group(1) if upname else src_file.split("-")[0]
            if subdir:
                root = os.path.join(root, subdir)
            else:
                inner = [f for f in os.listdir(root) if re.match(re.escape(upname) + r"-[0-9].*\.tar\.\w+$", f)]
                if inner:
                    inner_dir = os.path.join(tmp, "__inner")
                    os.makedirs(inner_dir)
                    with tarfile.open(os.path.join(root, inner[0])) as t:
                        t.extractall(inner_dir)
                    root = os.path.join(inner_dir, os.listdir(inner_dir)[0])
                    newver = newver or re.match(re.escape(upname) + r"-(.+?)\.tar", inner[0]).group(1)
            if not newver:
                newver = (detect_version(root) if subdir else None) or \
                    upstream_version(re.match(re.escape(usrc) + r"_(.+?)(\.orig)?\.tar", uname).group(1))
            if dld in FIXUPS:
                FIXUPS[dld](root)
            shutil.rmtree(os.path.join(root, "debian"), ignore_errors=True)
            shutil.rmtree(os.path.join(root, ".git"), ignore_errors=True)
            if skipped is None:
                pdir0 = os.path.join(brdir, pkg["package_dir"])
                if os.path.basename(pdir0.rstrip("/")) in ("gcc-final", "gcc-initial"):
                    pdir0 = os.path.dirname(pdir0.rstrip("/"))
                vdir0 = os.path.join(pdir0, bver)
                if newver != bver and os.path.isdir(vdir0) and not os.path.isdir(os.path.join(pdir0, newver)):
                    shutil.copytree(vdir0, os.path.join(pdir0, newver))
                skipped = trial_patches(root, pdir0, newver)
            new_src = src_file.replace(bver, newver)
            dest = os.path.join(dldir, dld, new_src)
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            repack(root, new_src.split(".t")[0] if not new_src.endswith("-source.tar.bz2") else f"{upname}-{newver}", dest)
        except StopIteration:
            pass
        finally:
            shutil.rmtree(tmp)
        lock[dld] = [path, h, newver, ",".join(skipped) if skipped else "-"]
        # patch the Buildroot package
        pdir = os.path.join(brdir, pkg["package_dir"]) if not os.path.isabs(pkg["package_dir"]) else pkg["package_dir"]
        if os.path.basename(pdir.rstrip("/")) in ("gcc-final", "gcc-initial"):
            pdir = os.path.dirname(pdir.rstrip("/"))  # version lives in package/gcc/gcc.mk
        raw = os.path.basename(pdir.rstrip("/"))
        mk = os.path.join(pdir, raw + ".mk")
        var = re.sub(r"[^A-Za-z0-9]", "_", raw).upper() + "_VERSION"
        if not os.path.exists(mk) or not re.search(rf"^{var}\s*=", open(mk).read(), re.M):
            parent = os.path.dirname(pdir.rstrip("/"))
            raw = os.path.basename(parent)
            pdir = parent
            mk = os.path.join(pdir, raw + ".mk")
            var = re.sub(r"[^A-Za-z0-9]", "_", raw).upper() + "_VERSION"
        if newver != bver and os.path.exists(mk):
            t = open(mk).read()
            t2, n = re.subn(rf"^{var}\s*=.*$", f"# P3LINUX: Ubuntu archive source\n{var} = {newver}", t, count=1, flags=re.M)
            if n:
                open(mk, "w").write(t2)
            else:
                print(f"!! {var} not found in {mk}; handle manually")
        # patches in a version directory must follow the version
        vdir = os.path.join(pdir, bver)
        if newver != bver and os.path.isdir(vdir) and not os.path.exists(os.path.join(pdir, newver)):
            shutil.copytree(vdir, os.path.join(pdir, newver))
        for pf in skipped or []:
            for d in patch_dirs(pdir, newver):
                if os.path.exists(os.path.join(d, pf)):
                    os.unlink(os.path.join(d, pf))
        hf = os.path.join(pdir, raw + ".hash")
        if os.path.exists(hf):  # drop stale lines for the same file name
            keep = [l for l in open(hf) if not (len(l.split()) == 3 and l.split()[2] == new_src)]
            open(hf, "w").writelines(keep)
        with open(hf, "a") as f:
            f.write(f"# P3LINUX: repacked from Ubuntu {path} (sha256 {h})\n"
                    f"sha256  {sha256(dest)}  {new_src}\n")
        report.append(f"{raw:28s} {bver:>14s} -> {newver:<14s} {path}"
                      + (f"  [dropped patches: {', '.join(skipped)}]" if skipped else ""))
        done.add(dld)
        print(report[-1])
        write_lock(lock)
    write_lock(lock)
    with open(os.path.join(outdir, "ubuntu-subst-report.txt"), "w") as f:
        f.write("\n".join(report) + "\n")


if __name__ == "__main__":
    main()
