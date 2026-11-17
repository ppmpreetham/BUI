"""Build a blenderui app for the web: a .wasm, the script that runs it in a <canvas>, and the page.

    python platform/wasm/build.py examples/wasm/counter            # -> examples/wasm/counter/dist/
    python platform/wasm/build.py examples/wasm/counter --serve    # ... and open http://localhost:8000
    python platform/wasm/build.py --fetch-sysroot                  # download the WASI sysroot once (0.2 GB)

The app is a Rust crate that builds a `cdylib` and calls `blenderui_web::export!` (see
examples/wasm/counter). It is compiled for wasm32-wasip1, with BUI's C kit built for it by CMake and
clang, so this needs:

  * Rust with the target (`rustup target add wasm32-wasip1`; this adds it when it is missing),
  * clang 19 or newer with the wasm32 backend (LLVM's own; Apple's clang has none) and CMake,
  * make or Ninja,
  * the WASI sysroot: WASI_SYSROOT=<dir>, or --fetch-sysroot (wasi-sdk's, kept in ~/.bui/wasi-sysroot).

SPDX-License-Identifier: GPL-2.0-or-later
"""

import argparse
import functools
import http.server
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TARGET = "wasm32-wasip1"
WASI_SDK = "34"
# wasi-sdk 34's libc++ is built from a recent LLVM and does not compile with an older clang: clang 19 stops at
# `#pragma clang attribute` on `__visibility__` in its headers. clang 21 is the oldest verified to work.
MIN_CLANG = 21
WASI_SDK_URL = ("https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-%s/wasi-sdk-%s.0-%s.tar.gz")
SYSROOT_HOME = os.path.join(os.path.expanduser("~"), ".bui", "wasi-sysroot")

PAGE = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>%(title)s</title>
<link rel="icon" href="data:,">
<style>
  html, body { margin: 0; height: 100%%; background: #303030; }
  canvas { display: block; width: 100vw; height: 100vh; outline: none; }
  #error { color: #ff8080; font: 14px system-ui; padding: 16px; }
</style>
</head>
<body>
<canvas></canvas>
<script type="module">
  import { mount } from './bui-web.js'
  mount(document.querySelector('canvas'), './app.wasm').catch((e) => {
    document.body.innerHTML = '<pre id="error"></pre>'
    document.getElementById('error').textContent = String(e)
  })
</script>
</body>
</html>
"""


def run(cmd, **kw):
    print("+", " ".join(str(c) for c in cmd))
    subprocess.run(cmd, check=True, **kw)


def host_tarball():
    machine = {"AMD64": "x86_64", "x86_64": "x86_64", "arm64": "arm64", "aarch64": "arm64"}.get(os.uname().machine if hasattr(os, "uname") else os.environ.get("PROCESSOR_ARCHITECTURE", "AMD64"), "x86_64")
    # the sysroot is the same on every host: the Linux tarball is the smallest that has libc++
    return "%s-linux" % machine


def sysroot_dir():
    return os.path.join(SYSROOT_HOME, "wasi-sdk-%s" % WASI_SDK, "share", "wasi-sysroot")


def fetch_sysroot():
    """wasi-sdk's sysroot (wasi-libc and libc++ for wasm32-wasip1), kept in ~/.bui/wasi-sysroot."""
    dest = sysroot_dir()
    if os.path.isdir(os.path.join(dest, "lib", TARGET)):
        return dest
    sdk = os.path.join(SYSROOT_HOME, "wasi-sdk-%s" % WASI_SDK)
    url = WASI_SDK_URL % (WASI_SDK, WASI_SDK, host_tarball())
    print("downloading", url, "(about 0.2 GB, once)")
    wanted = ("share/wasi-sysroot/include/%s/" % TARGET, "share/wasi-sysroot/lib/%s/" % TARGET)
    os.makedirs(SYSROOT_HOME, exist_ok=True)
    # extracted aside and moved into place when whole: an interrupted download leaves nothing that looks done
    with tempfile.TemporaryDirectory(dir=SYSROOT_HOME, prefix=".partial-") as part:
        with urllib.request.urlopen(url) as response, tarfile.open(fileobj=io.BufferedReader(response), mode="r|gz") as tar:
            for member in tar:
                rel = member.name.split("/", 1)[-1]
                if rel.startswith(wanted) and (member.isfile() or member.isdir()):
                    member.name = rel
                    tar.extract(member, part, filter="data")
        if not os.path.isdir(os.path.join(part, "share", "wasi-sysroot", "lib", TARGET)):
            sys.exit("the download did not hold the sysroot for " + TARGET)
        shutil.rmtree(sdk, ignore_errors=True)  # what an older, interrupted run left
        os.replace(part, sdk)
        os.makedirs(part)  # for the temporary directory's own cleanup
    return dest


def clang_major(exe):
    """The major version of this clang (0 when it does not say)."""
    out = subprocess.run([exe, "--version"], capture_output=True, text=True).stdout
    m = re.search(r"clang version (\d+)", out)
    return int(m.group(1)) if m else 0


def find_clang():
    """The folder of an LLVM clang (MIN_CLANG or newer) that has the wasm32 backend."""
    folder = os.environ.get("WASI_CLANG")
    candidates = [folder] if folder else []
    which = shutil.which("clang")
    if which:
        candidates.append(os.path.dirname(which))
    candidates += ["/usr/lib/llvm-%d/bin" % v for v in range(25, MIN_CLANG - 1, -1)]
    candidates += ["C:/Program Files/LLVM/bin", "/opt/homebrew/opt/llvm/bin", "/usr/local/opt/llvm/bin", "/usr/lib/llvm/bin"]
    too_old = []
    for c in candidates:
        exe = os.path.join(c, "clang.exe" if os.name == "nt" else "clang")
        if not os.path.exists(exe):
            continue
        major = clang_major(exe)
        if major < MIN_CLANG:
            too_old.append("%s (clang %d)" % (exe, major))
            continue
        if "wasm32" in subprocess.run([exe, "--print-targets"], capture_output=True, text=True).stdout:
            return c
    sys.exit("no clang %d or newer with the wasm32 backend (wasi-sdk 34's libc++ needs a recent clang: clang 19 fails on its"
             " `#pragma clang attribute` / `__visibility__` headers): install LLVM (https://releases.llvm.org, or apt.llvm.org) or set"
             " WASI_CLANG to its bin folder (Apple's clang has no wasm32 backend)%s"
             % (MIN_CLANG, "; too old: " + ", ".join(too_old) if too_old else ""))


def build(crate, out, debug):
    clang = find_clang()
    sysroot = os.environ.get("WASI_SYSROOT") or (sysroot_dir() if os.path.isdir(sysroot_dir()) else None)
    if not sysroot:
        sys.exit("no WASI sysroot: set WASI_SYSROOT, or run  python platform/wasm/build.py --fetch-sysroot")
    if "wasm32-wasip1" not in subprocess.run(["rustup", "target", "list", "--installed"], capture_output=True, text=True).stdout:
        run(["rustup", "target", "add", TARGET])
    exe = ".exe" if os.name == "nt" else ""
    env = dict(os.environ, WASI_SYSROOT=sysroot.replace("\\", "/"), WASI_CLANG=clang)
    var = TARGET.replace("-", "_")
    # llvm-ar next to clang, else on PATH (Debian and Ubuntu name it llvm-ar-18 and so on)
    ar = next((p for p in (os.path.join(clang, "llvm-ar" + exe), shutil.which("llvm-ar")) if p and os.path.exists(p)), None)
    env.update({"CC_" + var: os.path.join(clang, "clang" + exe), "CXX_" + var: os.path.join(clang, "clang++" + exe)})
    if ar:
        env["AR_" + var] = ar
    if "CMAKE_GENERATOR" not in env:
        # CMake's default on Windows is Visual Studio, which cannot drive a clang cross-compile
        if shutil.which("ninja"):
            env["CMAKE_GENERATOR"] = "Ninja"
        elif shutil.which("make"):
            env["CMAKE_GENERATOR"] = "Unix Makefiles"
        else:
            sys.exit("the kit is built with CMake and Ninja (or make), and neither is on PATH.\n"
                     "  Windows:  winget install Ninja-build.Ninja   (or: scoop install ninja)\n"
                     "  macOS:    brew install ninja\n"
                     "  Linux:    sudo apt install ninja-build")
    manifest = os.path.join(crate, "Cargo.toml")
    cmd = ["cargo", "build", "--target", TARGET, "--manifest-path", manifest]
    if not debug:
        cmd.append("--release")
    # cargo says where the cdylib went (a workspace member builds into the workspace's target folder)
    print("+", " ".join(cmd))
    built = subprocess.run(cmd + ["--message-format=json-render-diagnostics"], env=env, check=True, stdout=subprocess.PIPE, text=True)
    wasm = None
    for line in built.stdout.splitlines():
        if not line.startswith("{"):
            continue
        msg = json.loads(line)
        if msg.get("reason") == "compiler-artifact" and "cdylib" in msg["target"]["crate_types"] and os.path.samefile(msg["manifest_path"], manifest):
            wasm = next((f for f in msg["filenames"] if f.endswith(".wasm")), wasm)
    if not wasm or not os.path.exists(wasm):
        sys.exit("cargo built no .wasm: the crate needs  crate-type = [\"cdylib\"]  under [lib] in " + manifest)
    os.makedirs(out, exist_ok=True)
    final = os.path.join(out, "app.wasm")
    shutil.copyfile(wasm, final)
    if not debug and shutil.which("wasm-opt"):
        run(["wasm-opt", "-Oz", "--enable-bulk-memory", "--enable-sign-ext", "--enable-nontrapping-float-to-int", final, "-o", final])
    shutil.copyfile(os.path.join(ROOT, "bindings", "rust", "blenderui-web", "js", "bui-web.js"), os.path.join(out, "bui-web.js"))
    title = os.path.basename(os.path.abspath(crate)).replace("-", " ").replace("_", " ").title()
    with open(os.path.join(out, "index.html"), "w", encoding="utf-8") as f:
        f.write(PAGE % {"title": title})
    return final


def serve(out, port):
    handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=out)
    handler.extensions_map = dict(http.server.SimpleHTTPRequestHandler.extensions_map, **{".wasm": "application/wasm", ".js": "text/javascript"})
    print("serving %s at http://localhost:%d  (Ctrl+C to stop)" % (out, port))
    http.server.ThreadingHTTPServer(("127.0.0.1", port), handler).serve_forever()


def main():
    ap = argparse.ArgumentParser(description="Build a blenderui app for the web")
    ap.add_argument("crate", nargs="?", help="the app's crate folder (a cdylib using blenderui_web::export!)")
    ap.add_argument("--out", help="where to write app.wasm, bui-web.js and index.html (default: CRATE/dist)")
    ap.add_argument("--debug", action="store_true", help="a debug build (large and slow)")
    ap.add_argument("--serve", nargs="?", const=8000, type=int, metavar="PORT", help="then serve it on localhost")
    ap.add_argument("--fetch-sysroot", action="store_true", help="download the WASI sysroot to ~/.bui/wasi-sysroot and stop")
    a = ap.parse_args()
    if a.fetch_sysroot:
        print(fetch_sysroot())
        return
    if not a.crate:
        ap.error("name the app's crate folder")
    out = os.path.abspath(a.out or os.path.join(a.crate, "dist"))
    final = build(os.path.abspath(a.crate), out, a.debug)
    print("%s  %.2f MB  (%s)" % (final, os.path.getsize(final) / 1048576, out))
    if a.serve is not None:
        serve(out, a.serve)


if __name__ == "__main__":
    main()
