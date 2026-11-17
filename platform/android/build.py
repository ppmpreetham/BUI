"""Build the Android app: libbui_app.so per ABI and a signed APK, with the NDK and SDK tools only
(no Gradle, no Java code).

    python platform/android/build.py                     # arm64-v8a and x86_64 -> build-android/bui.apk
    python platform/android/build.py --abi arm64-v8a     # one ABI (what a phone needs)
    python platform/android/build.py --install           # adb install -r, then start it
    python platform/android/build.py --lua examples/android    # a Lua app: the folder's *.lua are assets/
    python platform/android/build.py --rust platform/android/rust   # a Rust app (a staticlib crate)
    python platform/android/build.py --js app.jsx        # a JavaScript app (React, Vue, Svelte): QuickJS + the bundle
    python platform/android/build.py --imgui [app.cpp]   # a Dear ImGui app (C++; the default is the demo in imgui_app.cpp)

ANDROID_HOME / ANDROID_SDK_ROOT (else the usual per-user SDK folder) locates the SDK; the NDK is
the newest one in it (or ANDROID_NDK_HOME).

SPDX-License-Identifier: GPL-2.0-or-later
"""

import argparse
import glob
import json
import os
import shlex
import shutil
import subprocess
import sys
import zipfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
API = 26           # minSdkVersion
PLATFORM = 34      # android.jar to link against
# The optional parts a phone app leaves out (features.json): GPU renderers, Dear ImGui, the
# file browser's native dialogs. (Video stays on: it has no backend here, but video.c needs audio.c.) Images stay (SVG icons, PNG).
OFF = ["BUI_GPU", "BUI_BUILD_IMGUI", "BUI_FILEBROWSER", "BUI_IMAGE_GIF", "BUI_IMAGE_BMP",
       "BUI_IMAGE_TGA", "BUI_IMAGE_QOI", "BUI_IMAGE_WEBP", "BUI_IMAGE_HDR", "BUI_IMAGE_PNM",
       "BUI_IMAGE_JPEG"]


def sdk():
    for var in ("ANDROID_HOME", "ANDROID_SDK_ROOT"):
        if os.environ.get(var):
            return os.environ[var]
    for p in (os.path.expanduser("~/AppData/Local/Android/Sdk"), os.path.expanduser("~/Android/Sdk"),
              os.path.expanduser("~/Library/Android/sdk")):
        if os.path.isdir(p):
            return p
    sys.exit("no Android SDK: set ANDROID_HOME")


def newest(pattern):
    found = sorted(glob.glob(pattern))
    if not found:
        sys.exit("not found: " + pattern)
    return found[-1]


def run(cmd, **kw):
    print("+", " ".join(cmd))
    subprocess.run(cmd, check=True, **kw)


RUST_TARGETS = {"arm64-v8a": "aarch64-linux-android", "x86_64": "x86_64-linux-android",
                "armeabi-v7a": "armv7-linux-androideabi"}


def staticlib(messages, manifest):
    """The staticlib cargo built for `manifest`, from its JSON messages: wherever its target
    directory is (the workspace's, CARGO_TARGET_DIR, the crate's own), and whatever [lib].name is."""
    found = None
    for line in messages.splitlines():
        try:
            m = json.loads(line)
        except ValueError:
            continue
        if m.get("reason") != "compiler-artifact" or "staticlib" not in m.get("target", {}).get("crate_types", []):
            continue
        if os.path.normcase(os.path.abspath(m.get("manifest_path", ""))) != os.path.normcase(manifest):
            continue
        libs = [f for f in m.get("filenames", []) if f.endswith(".a") or f.endswith(".lib")]
        if libs:
            found = libs[-1]
    if not found:
        sys.exit("cargo built no staticlib for %s (crate-type = [\"staticlib\"] in its [lib])" % manifest)
    return found


def build_rust(crate, ndk, abi, make):
    """cargo builds the app's staticlib (and, through blenderui-sys, the kit) for one ABI."""
    triple = RUST_TARGETS[abi]
    # the cmake crate reads the toolchain file from the environment; `make` is found on PATH. CC /
    # CXX / AR name the NDK's clang so the crate passes clang's flags, not the host compiler's
    exe = ".exe" if os.name == "nt" else ""
    cmd = ".cmd" if os.name == "nt" else ""
    host = os.path.basename(os.path.dirname(os.path.dirname(make)))
    llvm = os.path.join(ndk, "toolchains", "llvm", "prebuilt", host, "bin")
    clang = os.path.join(llvm, "%s%d-clang" % (triple.replace("armv7", "armv7a"), API))
    var = triple.replace("-", "_")
    env = dict(os.environ, ANDROID_NDK_HOME=ndk, ANDROID_NDK=ndk, CMAKE_GENERATOR="Unix Makefiles",
               CMAKE_TOOLCHAIN_FILE=os.path.join(ndk, "build", "cmake", "android.toolchain.cmake").replace("\\", "/"),
               ANDROID_PLATFORM="android-%d" % API, ANDROID_ABI=abi,
               **{"CC_" + var: clang + cmd, "CXX_" + var: clang + "++" + cmd,
                  "AR_" + var: os.path.join(llvm, "llvm-ar" + exe)},
               PATH=os.path.dirname(make) + os.pathsep + os.environ["PATH"])
    # bindgen (QuickJS bindings): the NDK's libclang and sysroot
    sysroot = os.path.join(llvm, "..", "sysroot")
    # libclang is in bin/ on Windows and lib/ elsewhere; a LIBCLANG_PATH the user set wins
    env.setdefault("LIBCLANG_PATH", llvm if os.name == "nt" else os.path.normpath(os.path.join(llvm, "..", "lib")))
    env.update(**{"BINDGEN_EXTRA_CLANG_ARGS_" + var: shlex.quote("--sysroot=" + sysroot.replace("\\", "/"))})
    manifest = os.path.abspath(os.path.join(crate, "Cargo.toml"))
    cmd = ["cargo", "build", "--release", "--target", triple, "--manifest-path", manifest,
           "--message-format=json-render-diagnostics"]
    print("+", " ".join(cmd))
    done = subprocess.run(cmd, env=env, check=True, stdout=subprocess.PIPE, text=True)
    return staticlib(done.stdout, manifest)


def build_abi(ndk, abi, out, app_args, rust=None):
    exe = ".exe" if os.name == "nt" else ""
    host = {"nt": "windows-x86_64", "posix": "linux-x86_64"}[os.name]
    if sys.platform == "darwin":
        host = "darwin-x86_64"
    make = os.path.join(ndk, "prebuilt", host, "bin", "make" + exe)
    bdir = os.path.join(out, abi)
    if rust:
        app_args = ["-DBUI_ANDROID_APP=rust", "-DBUI_ANDROID_RUST_LIB=" + build_rust(rust, ndk, abi, make).replace("\\", "/")]
    args = ["cmake", "-S", ROOT, "-B", bdir, "-G", "Unix Makefiles",
            "-DCMAKE_MAKE_PROGRAM=" + make.replace("\\", "/"),
            "-DCMAKE_TOOLCHAIN_FILE=" + os.path.join(ndk, "build", "cmake", "android.toolchain.cmake").replace("\\", "/"),
            "-DANDROID_ABI=" + abi, "-DANDROID_PLATFORM=android-%d" % API, "-DCMAKE_BUILD_TYPE=Release",
            "-DBUI_BUILD_TESTS=OFF", "-DBUI_BUILD_PYTHON_BINDINGS=OFF"] + ["-D%s=OFF" % o for o in OFF] + app_args
    run(args)
    run(["cmake", "--build", bdir, "--target", "bui_app", "-j", str(os.cpu_count() or 4)])
    return os.path.join(bdir, "platform", "android", "libbui_app.so")


def keystore(out):
    path = os.path.expanduser("~/.android/debug.keystore")
    if os.path.exists(path):
        return path
    path = os.path.join(out, "debug.keystore")
    if not os.path.exists(path):
        run(["keytool", "-genkeypair", "-keystore", path, "-storepass", "android", "-keypass", "android",
             "-alias", "androiddebugkey", "-keyalg", "RSA", "-keysize", "2048", "-validity", "10000",
             "-dname", "CN=Android Debug,O=Android,C=US"])
    return path


def apk(root_sdk, libs, out, assets):
    tools = newest(os.path.join(root_sdk, "build-tools", "*"))
    jar = os.path.join(root_sdk, "platforms", "android-%d" % PLATFORM, "android.jar")
    exe = ".exe" if os.name == "nt" else ""
    bat = ".bat" if os.name == "nt" else ""
    base = os.path.join(out, "base.apk")
    run([os.path.join(tools, "aapt2" + exe), "link", "--manifest",
         os.path.join(ROOT, "platform", "android", "AndroidManifest.xml"), "-I", jar,
         "--min-sdk-version", str(API), "--target-sdk-version", str(PLATFORM), "-o", base])
    raw = os.path.join(out, "unaligned.apk")
    with zipfile.ZipFile(base) as src, zipfile.ZipFile(raw, "w", zipfile.ZIP_DEFLATED) as dst:
        for item in src.infolist():
            dst.writestr(item, src.read(item.filename))
        for abi, lib in libs.items():  # deflated: the installer extracts it (extractNativeLibs)
            dst.write(lib, "lib/%s/libbui_app.so" % abi, compress_type=zipfile.ZIP_DEFLATED)
        for name, path in assets.items():  # assets/<name>: the app's scripts and data
            dst.write(path, "assets/" + name, compress_type=zipfile.ZIP_DEFLATED)
    aligned = os.path.join(out, "aligned.apk")
    run([os.path.join(tools, "zipalign" + exe), "-f", "-P", "16", "4", raw, aligned])
    final = os.path.join(out, "bui.apk")
    run([os.path.join(tools, "apksigner" + bat), "sign", "--ks", keystore(out), "--ks-pass", "pass:android",
         "--out", final, aligned])
    return final


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--abi", action="append", help="arm64-v8a, x86_64, armeabi-v7a (default: arm64-v8a and x86_64)")
    ap.add_argument("--out", default=os.path.join(ROOT, "build-android"))
    ap.add_argument("--install", action="store_true")
    ap.add_argument("--rust", metavar="CRATE", help="a Rust app: a staticlib crate that defines bui_app_main (platform/android/rust)")
    ap.add_argument("--lua", metavar="DIR", help="a Lua app: DIR/main.lua returns the root node; every *.lua in DIR is an asset")
    ap.add_argument("--js", metavar="ENTRY", help="a JavaScript app: ENTRY (.js, .jsx, .vue, .svelte) is bundled (bindings/node: bui bundle, needs Bun) and run on QuickJS")
    ap.add_argument("--imgui", nargs="?", const="", metavar="FILE", help="a Dear ImGui app: a C++ file that defines bui_app_main (default: platform/android/imgui_app.cpp)")
    a = ap.parse_args()
    app_args, assets = ["-DBUI_ANDROID_APP=demo", "-DBUI_BUILD_LUA_BINDINGS=OFF"], {}
    rust = a.rust
    if a.lua:
        app_args = ["-DBUI_ANDROID_APP=lua", "-DBUI_BUILD_LUA_BINDINGS=ON"]
        if not os.path.exists(os.path.join(a.lua, "main.lua")):
            sys.exit("%s has no main.lua" % a.lua)
        for dirpath, _, files in os.walk(a.lua):
            for f in files:
                if f.endswith(".lua"):
                    full = os.path.join(dirpath, f)
                    assets[os.path.relpath(full, a.lua).replace(os.sep, "/")] = full
        assets["bui.lua"] = os.path.join(ROOT, "bindings", "lua", "bui.lua")  # the wrapper `require("bui")` finds
    if a.imgui is not None:
        app_args = ["-DBUI_ANDROID_APP=imgui", "-DBUI_BUILD_IMGUI=ON", "-DBUI_BUILD_LUA_BINDINGS=OFF"]
        # always named: CMake caches it, so a bare --imgui after --imgui custom.cpp must reset it
        imgui_src = a.imgui or os.path.join(ROOT, "platform", "android", "imgui_app.cpp")
        app_args.append("-DBUI_ANDROID_IMGUI_SRC=" + os.path.abspath(imgui_src).replace("\\", "/"))
    if a.js:
        rust = os.path.join(ROOT, "platform", "android", "js")
        main_js = os.path.join(a.out, "main.js")
        os.makedirs(a.out, exist_ok=True)
        run(["node", os.path.join(ROOT, "bindings", "node", "cli.js"), "bundle", a.js, "-o", main_js])
        assets["main.js"] = main_js
    root_sdk = sdk()
    ndk = os.environ.get("ANDROID_NDK_HOME") or newest(os.path.join(root_sdk, "ndk", "*"))
    os.makedirs(a.out, exist_ok=True)
    libs = {abi: build_abi(ndk, abi, a.out, app_args, rust) for abi in (a.abi or ["arm64-v8a", "x86_64"])}
    final = apk(root_sdk, libs, a.out, assets)
    for abi, lib in libs.items():
        print("%-12s %8.1f KB" % (abi, os.path.getsize(lib) / 1024))
    print("%s  %.1f KB" % (final, os.path.getsize(final) / 1024))
    if a.install:
        adb = shutil.which("adb") or os.path.join(root_sdk, "platform-tools", "adb")
        run([adb, "install", "-r", final])
        run([adb, "shell", "am", "start", "-n", "org.bui.app/android.app.NativeActivity"])


if __name__ == "__main__":
    main()
