#!/usr/bin/env python3
"""
SkyRecomp Android Native Game Library Builder (ARM64-v8a)
Translates Switch AArch64 binaries (exefs) into C, cross-compiles them with Android NDK Clang,
and packages the production libskygame.so for Android devices.
"""

import os
import sys
import subprocess
import time
import shutil
from concurrent.futures import ThreadPoolExecutor, as_completed

GAMELIB_ID = "22a4b9fe650c1801"

def find_ndk():
    candidates = [
        r"C:\Users\Dio\AppData\Local\Android\Sdk\ndk\28.2.13676358",
        os.environ.get("ANDROID_NDK_HOME", ""),
        os.environ.get("ANDROID_NDK_ROOT", ""),
        os.environ.get("NDK_HOME", ""),
    ]
    sdk = os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT")
    if sdk and os.path.exists(os.path.join(sdk, "ndk")):
        ndk_dir = os.path.join(sdk, "ndk")
        versions = sorted(os.listdir(ndk_dir), reverse=True)
        if versions:
            candidates.append(os.path.join(ndk_dir, versions[0]))

    for c in candidates:
        if c and os.path.isdir(c):
            return os.path.abspath(c)
    return None

def find_clang(ndk_root):
    host_tag = "windows-x86_64" if sys.platform == "win32" else "linux-x86_64" if sys.platform.startswith("linux") else "darwin-x86_64"
    llvm_bin = os.path.join(ndk_root, "toolchains", "llvm", "prebuilt", host_tag, "bin")
    clang_exe = os.path.join(llvm_bin, "clang.exe" if sys.platform == "win32" else "clang")
    if os.path.exists(clang_exe):
        return clang_exe, llvm_bin
    return None, None

def compile_unit(clang, cflags, src, obj, cache_valid):
    if cache_valid and os.path.exists(obj) and os.path.getmtime(obj) >= os.path.getmtime(src):
        return True, src, None, 0.0

    t0 = time.time()
    cmd = [clang] + cflags + ["-c", src, "-o", obj]
    res = subprocess.run(cmd, capture_output=True, text=True)
    dt = time.time() - t0
    if res.returncode != 0:
        return False, src, res.stderr, dt
    return True, src, None, dt

def main():
    print("=" * 60)
    print("  SkyRecomp Android Game Library Builder (arm64-v8a)")
    print("=" * 60)

    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    os.chdir(repo_root)

    ndk_root = find_ndk()
    if not ndk_root:
        print("[ERROR] Android NDK not found! Set ANDROID_NDK_HOME or install via Android SDK.", file=sys.stderr)
        sys.exit(1)

    clang, llvm_bin = find_clang(ndk_root)
    if not clang:
        print(f"[ERROR] Clang compiler not found inside NDK: {ndk_root}", file=sys.stderr)
        sys.exit(1)

    print(f"NDK Path: {ndk_root}")
    print(f"Compiler: {clang}")
    print(f"Target ABI: arm64-v8a (Android API 33)")

    nsp_arg = None
    keys_arg = None
    for idx, a in enumerate(sys.argv):
        if a in ("--nsp", "--rom") and idx + 1 < len(sys.argv):
            nsp_arg = sys.argv[idx + 1]
        elif a in ("--keys", "-k") and idx + 1 < len(sys.argv):
            keys_arg = sys.argv[idx + 1]

    if nsp_arg:
        try:
            from tools.extract_nsp import extract
        except ImportError:
            from extract_nsp import extract
        out_dir = os.path.abspath("extracted")
        print(f"\n[NSP] Automatic ROM extraction requested for: {nsp_arg}")
        if not extract(nsp_arg, keys_arg, out_dir=out_dir):
            print("[ERROR] ROM extraction failed!", file=sys.stderr)
            sys.exit(1)

    build_dir = os.path.abspath("build-android")
    obj_dir = os.path.join(build_dir, "obj_skygame")
    unity_dir = os.path.join(build_dir, "unity_src")
    os.makedirs(obj_dir, exist_ok=True)
    os.makedirs(unity_dir, exist_ok=True)

    cache_id_file = os.path.join(obj_dir, "build.id")
    cache_valid = False
    if os.path.exists(cache_id_file):
        try:
            with open(cache_id_file, "r") as f:
                cache_valid = (f.read().strip() == GAMELIB_ID)
        except Exception:
            cache_valid = False

    inc_runtime = os.path.abspath("runtime")
    inc_hle = os.path.abspath("runtime/hle")

    cflags = [
        "--target=aarch64-none-linux-android33",
        "-O3",
        "-march=armv8-a+simd+crc",
        "-fomit-frame-pointer",
        "-fno-strict-aliasing",
        "-fPIC",
        "-fvisibility=hidden",
        "-D__ANDROID__",
        "-DNDEBUG",
        "-Wno-return-type",
        "-Wno-unused-label",
        "-Wno-unused-variable",
        "-Wno-format",
        f"-DGAMELIB_ID=\"{GAMELIB_ID}\"",
        f"-I{inc_runtime}",
        f"-I{inc_hle}",
    ]

    modules = ["main", "sdk", "subsdk0", "subsdk1"]
    exefs_dir = os.path.abspath("extracted/exefs")
    gen_dir = os.path.abspath("ssh_generated")

    missing_mods = []
    for m in modules:
        m_dir = os.path.join(gen_dir, m)
        stamp = os.path.join(m_dir, "build.id")
        current = False
        if os.path.exists(stamp):
            try:
                with open(stamp, "r") as f:
                    current = (f.read().strip() == GAMELIB_ID)
            except Exception:
                current = False
        if not current or not os.path.exists(m_dir) or not any(f.endswith(".c") for f in os.listdir(m_dir)):
            missing_mods.append(m)

    if missing_mods:
        print("\n[INFO] Generating C code from Switch NSO binaries...")
        if not os.path.exists(os.path.join(exefs_dir, "main")):
            print(f"[WARN] Game ExeFS not found in {exefs_dir}.")
            print("Using compilation stubs (tools/skygame_stubs.c) for clean link validation.")
            stubs_src = os.path.abspath("tools/skygame_stubs.c")
            stubs_obj = os.path.join(obj_dir, "skygame_stubs.o")
            ok, _, err, dt = compile_unit(clang, cflags, stubs_src, stubs_obj, cache_valid)
            if not ok:
                print(f"[FATAL] Failed to compile skygame_stubs.c: {err}", file=sys.stderr)
                sys.exit(1)
            sources = [(stubs_src, stubs_obj)]
        else:
            gen_script = os.path.abspath("tools/gen.py")
            hle_file = os.path.abspath("runtime/hle/hle_symbols.txt")
            mod_configs = {
                "main": ("0x7100004000", ["--simd", "asm"]),
                "sdk": ("0x712E74B000", ["--hle", hle_file, "--simd", "asm"]),
                "subsdk0": ("0x712D037000", ["--simd", "asm"]),
                "subsdk1": ("0x712D9FF000", ["--simd", "asm"]),
            }
            def run_gen(mod):
                base_addr, extra = mod_configs[mod]
                nso_path = os.path.join(exefs_dir, mod)
                out_mod_dir = os.path.join(gen_dir, mod)
                os.makedirs(out_mod_dir, exist_ok=True)
                cmd = [sys.executable, gen_script, mod, nso_path, base_addr, out_mod_dir] + extra
                print(f"Generating C code for module '{mod}'...")
                t_m0 = time.time()
                res = subprocess.run(cmd, cwd=os.path.abspath("tools"), capture_output=True, text=True)
                if res.returncode != 0:
                    print(f"[ERROR] gen.py failed for {mod}:\n{res.stderr}", file=sys.stderr)
                    return False, mod
                with open(os.path.join(out_mod_dir, "build.id"), "w") as f:
                    f.write(GAMELIB_ID + "\n")
                print(f"[OK] Module '{mod}' generated in {time.time() - t_m0:.1f}s")
                return True, mod

            with ThreadPoolExecutor(max_workers=min(4, os.cpu_count() or 4)) as pool:
                futures = [pool.submit(run_gen, m) for m in missing_mods]
                for fut in as_completed(futures):
                    ok, mod = fut.result()
                    if not ok:
                        print(f"[FATAL] Code generation failed for {mod}", file=sys.stderr)
                        sys.exit(1)

    sources = []
    # Entry point & subsdk8
    entry_src = os.path.abspath("tools/gamelib_entry.c")
    entry_obj = os.path.join(obj_dir, "gamelib_entry.o")
    sources.append((entry_src, entry_obj))

    subsdk8_src = os.path.abspath("tools/subsdk8_empty.c")
    subsdk8_obj = os.path.join(obj_dir, "subsdk8_empty.o")
    sources.append((subsdk8_src, subsdk8_obj))

    # Add modules if present
    BATCH_SIZE = 25
    for mod in modules:
        mod_dir = os.path.join(gen_dir, mod)
        if not os.path.exists(mod_dir):
            continue
        c_files = sorted([f for f in os.listdir(mod_dir) if f.endswith(".c")])
        tables = [f for f in c_files if f.endswith("_table.c")]
        chunks = [f for f in c_files if not f.endswith("_table.c")]

        for tf in tables:
            src_path = os.path.abspath(os.path.join(mod_dir, tf))
            stem = tf[:-2]
            obj_path = os.path.join(obj_dir, f"{mod}_{stem}.o")
            sources.append((src_path, obj_path))

        for b_idx in range(0, len(chunks), BATCH_SIZE):
            batch = chunks[b_idx:b_idx + BATCH_SIZE]
            unity_src = os.path.join(unity_dir, f"unity_{mod}_{b_idx // BATCH_SIZE:03d}.c")
            unity_obj = os.path.join(obj_dir, f"unity_{mod}_{b_idx // BATCH_SIZE:03d}.o")
            lines = ['#include "cpu.h"\n']
            for cf in batch:
                full_p = os.path.abspath(os.path.join(mod_dir, cf)).replace("\\", "/")
                lines.append(f'#include "{full_p}"\n')
            unity_content = "".join(lines)
            needs_write = True
            if os.path.exists(unity_src):
                try:
                    with open(unity_src, "r", encoding="utf-8") as f:
                        if f.read() == unity_content:
                            needs_write = False
                except Exception:
                    pass
            if needs_write:
                with open(unity_src, "w", encoding="utf-8") as f:
                    f.write(unity_content)
            sources.append((unity_src, unity_obj))

    # If no generated code yet, fallback to stubs
    if len(sources) <= 2:
        stubs_src = os.path.abspath("tools/skygame_stubs.c")
        stubs_obj = os.path.join(obj_dir, "skygame_stubs.o")
        sources.append((stubs_src, stubs_obj))

    total = len(sources)
    cores = min(os.cpu_count() or 4, 16)
    print(f"\nCompiling {total} units with {cores} parallel threads...")
    t0 = time.time()
    compiled = 0
    errors = []

    with ThreadPoolExecutor(max_workers=cores) as executor:
        futures = {executor.submit(compile_unit, clang, cflags, src, obj, cache_valid): src for src, obj in sources}
        for fut in as_completed(futures):
            ok, src, err, dt = fut.result()
            fname = os.path.basename(src)
            if not ok:
                errors.append((src, err))
                print(f"\n[ERROR] Failed to compile {fname}:\n{err}", file=sys.stderr)
            compiled += 1
            if compiled % 25 == 0 or compiled == total:
                pct = (compiled / total) * 100
                print(f"Progress: {compiled}/{total} ({pct:.1f}%)", flush=True)

    if errors:
        print(f"\n[FATAL] Compilation failed with {len(errors)} error(s).", file=sys.stderr)
        sys.exit(1)

    print(f"\nCompilation finished in {time.time() - t0:.1f}s.")

    # Link libskygame.so
    out_so = os.path.abspath("build-android/libskygame.so")
    print(f"Linking {out_so}...")

    rsp_path = os.path.join(obj_dir, "objects.rsp")
    with open(rsp_path, "w") as rf:
        for _, obj in sources:
            rf.write(f'"{obj.replace(chr(92), "/")}"\n')

    main_so_dir = os.path.abspath("build-android")
    link_cmd = [
        clang,
        "--target=aarch64-none-linux-android33",
        "-shared",
        "-fuse-ld=lld",
        "-Wl,-soname,libskygame.so",
        "-Wl,--gc-sections",
        f"-L{main_so_dir}",
        "-lmain",
        "-lm",
        "-ldl",
        "-lc",
        f"@{rsp_path}",
        "-o",
        out_so
    ]

    t_link = time.time()
    lres = subprocess.run(link_cmd, capture_output=True, text=True)
    if lres.returncode != 0:
        print(f"[LINK ERROR]:\n{lres.stderr}", file=sys.stderr)
        sys.exit(1)

    with open(cache_id_file, "w") as f:
        f.write(GAMELIB_ID + "\n")

    so_size_mb = os.path.getsize(out_so) / (1024 * 1024)
    print(f"[SUCCESS] Linked {out_so} ({so_size_mb:.2f} MB) in {time.time() - t_link:.1f}s!")

    # Copy to android jniLibs
    jni_dest = os.path.abspath("android/app/src/main/jniLibs/arm64-v8a/libskygame.so")
    os.makedirs(os.path.dirname(jni_dest), exist_ok=True)
    shutil.copy2(out_so, jni_dest)
    print(f"[COPIED] -> {jni_dest}")

    # Rebuild APK option
    if "--apk" in sys.argv or "-a" in sys.argv:
        print("\nRebuilding release APK with Gradle...")
        gradlew = os.path.abspath("android/gradlew.bat" if sys.platform == "win32" else "android/gradlew")
        subprocess.run([gradlew, "assembleRelease"], cwd=os.path.abspath("android"), check=True, shell=(sys.platform == "win32"))
        apk_dest = os.path.abspath("android/app/build/outputs/apk/release/app-release.apk")
        print(f"[DONE] Final APK ready: {apk_dest}")

    # Push to ADB if requested
    if "--push" in sys.argv or "-p" in sys.argv:
        print("\nPushing game assets and library to connected Android device via ADB...")
        dest_dir = "/sdcard/Android/data/com.apolinario.skyrecomp/files"
        subprocess.run(["adb", "shell", f"mkdir -p {dest_dir}"])
        subprocess.run(["adb", "push", out_so, f"{dest_dir}/libskygame.so"])
        print("[DONE] libskygame.so pushed to device!")
        romfs_bin = os.path.abspath("extracted/romfs.bin")
        if os.path.exists(romfs_bin):
            print("Pushing romfs.bin to device...")
            subprocess.run(["adb", "push", romfs_bin, f"{dest_dir}/romfs.bin"])
            print("[DONE] romfs.bin pushed to device!")

    print("\n[COMPLETE] Android Game Library build finished successfully.")

if __name__ == "__main__":
    main()
