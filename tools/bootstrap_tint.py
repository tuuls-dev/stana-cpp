"""Provision Stana's pinned private WGSL compiler; normal CMake never fetches.

Only Tint reader/validator/inspector/IR/MSL writer and required support are built.
The merged private archive makes installed consumers independent of Tint headers,
CWD, network access, and the original source tree.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
REVISION = "cb91d23e247042a2a5490c27d9b06300db91afef"
DEPENDENCIES = (
    ("dawn", REVISION,
     f"https://codeload.github.com/google/dawn/tar.gz/{REVISION}",
     "4a6bd27850326ae8828a047f5ee268358741898f42a950149f0f1aa9f80f8546", ""),
    ("abseil", "34ede0d51ae535e13886d04e7bbb557fe34acdd5",
     "https://chromium.googlesource.com/chromium/src/third_party/abseil-cpp/+archive/34ede0d51ae535e13886d04e7bbb557fe34acdd5.tar.gz",
     "a1f6c1f4278a939be5e07274d3cb587d495af6b70f1613b50a8afd0dd205c43f", "third_party/abseil-cpp"),
    ("spirv-headers", "71747ddd16f20e9b379ce293dba2954931d3b3c1",
     "https://codeload.github.com/KhronosGroup/SPIRV-Headers/tar.gz/71747ddd16f20e9b379ce293dba2954931d3b3c1",
     "5545e25a5784dbd6417dab6b1f38a1012fbe17199b708fb1491ba773824c4cd4", "third_party/spirv-headers/src"),
)
OFF = (
    "DAWN_ENABLE_D3D11", "DAWN_ENABLE_D3D12", "DAWN_ENABLE_METAL", "DAWN_ENABLE_NULL",
    "DAWN_ENABLE_DESKTOP_GL", "DAWN_ENABLE_OPENGLES", "DAWN_ENABLE_VULKAN",
    "DAWN_ENABLE_SPIRV_VALIDATION", "DAWN_BUILD_SAMPLES", "DAWN_BUILD_TESTS",
    "DAWN_BUILD_PROTOBUF", "DAWN_BUILD_MONOLITHIC_LIBRARY", "DAWN_FETCH_DEPENDENCIES",
    "DAWN_USE_GLFW", "TINT_BUILD_CMD_TOOLS", "TINT_BUILD_TESTS", "TINT_BUILD_SPV_READER",
    "TINT_BUILD_SPV_WRITER", "TINT_BUILD_GLSL_WRITER", "TINT_BUILD_GLSL_VALIDATOR",
    "TINT_BUILD_HLSL_WRITER", "TINT_BUILD_WGSL_WRITER", "TINT_BUILD_NULL_WRITER",
    "TINT_BUILD_IR_BINARY", "TINT_ENABLE_INSTALL", "TINT_BUILD_BENCHMARKS", "TINT_BUILD_FUZZERS",
)


def run(*args: str, **kwargs) -> None:
    subprocess.run(args, check=True, **kwargs)


def sources(cache: Path, source: Path, offline: bool) -> None:
    cache.mkdir(parents=True, exist_ok=True)
    for name, revision, url, expected, relative in DEPENDENCIES:
        archive = cache / f"{name}-{revision}.tar.gz"
        if not archive.exists():
            if offline:
                raise RuntimeError(f"Offline source archive missing: {archive}")
            partial = archive.with_suffix(".download")
            with urllib.request.urlopen(url, timeout=60) as response, partial.open("wb") as output:
                shutil.copyfileobj(response, output)
            partial.rename(archive)
        with archive.open("rb") as stream:
            actual = hashlib.file_digest(stream, "sha256").hexdigest()
        if actual != expected:
            raise RuntimeError(f"Digest mismatch for {name}: {actual}")
        destination = source / relative
        marker = destination / ".stana-source"
        if marker.exists() and marker.read_text() == expected:
            continue
        if destination.exists():
            shutil.rmtree(destination)
        destination.mkdir(parents=True)
        with tarfile.open(archive) as package:
            # Chromium archives have no wrapper; GitHub archives have one.
            if name == "abseil":
                package.extractall(destination, filter="data")
            else:
                with __import__("tempfile").TemporaryDirectory(dir=cache) as temporary:
                    extracted = Path(temporary)
                    package.extractall(extracted, filter="data")
                    roots = list(extracted.iterdir())
                    if len(roots) != 1 or not roots[0].is_dir():
                        raise RuntimeError("Unexpected source archive layout")
                    shutil.copytree(roots[0], destination, dirs_exist_ok=True)
        marker.write_text(expected)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=Path)
    parser.add_argument("--work", type=Path, default=ROOT / "build/tint-bootstrap")
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--sanitizer", choices=("", "address", "address-undefined"), default="")
    parser.add_argument("--config", choices=("Debug", "Release"), default="Release")
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--offline", action="store_true")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    prefix = (args.prefix or ROOT / (".tools/tint" + (f"-{args.sanitizer}" if args.sanitizer else ""))).resolve()
    work = args.work.resolve()
    compiler = args.compiler.resolve()
    version = subprocess.check_output([str(compiler), "--version"], text=True)
    identity = {"schema": 1, "revision": REVISION, "sanitizer": args.sanitizer, "config": args.config,
                "system": platform.system(), "architecture": platform.machine(), "compiler": version,
                "dependencies": [{"name": n, "revision": r, "sha256": h} for n, r, _, h, _ in DEPENDENCIES]}
    marker = prefix / "stana-tint.json"
    if marker.exists() and json.loads(marker.read_text()) == identity:
        return
    source = work / "source"
    sources(work / "cache", source, args.offline)
    binary = work / (args.config + (f"-{args.sanitizer}" if args.sanitizer else ""))
    configure = ["cmake", "-S", str(source), "-B", str(binary), "-G", "Ninja",
                 f"-DCMAKE_CXX_COMPILER={compiler}", f"-DCMAKE_BUILD_TYPE={args.config}",
                 "-DCMAKE_CXX_STANDARD=23", "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
                 "-DTINT_BUILD_WGSL_READER=ON", "-DTINT_BUILD_MSL_WRITER=ON",
                 "-DDAWN_ENABLE_RTTI=ON", *[f"-D{option}=OFF" for option in OFF]]
    flags = []
    if platform.system() == "Darwin":
        sdk = subprocess.check_output(["xcrun", "--show-sdk-path"], text=True).strip()
        configure += [f"-DCMAKE_OSX_SYSROOT={sdk}", "-DCMAKE_OSX_DEPLOYMENT_TARGET=26.0"]
        flags += [f"-nostdinc++ -isystem {sdk}/usr/include/c++/v1", "-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE"]
    if args.sanitizer:
        if platform.system() == "Windows":
            flags += ["/fsanitize=address"]
        else:
            selection = "address,undefined" if args.sanitizer == "address-undefined" else "address"
            flags += [f"-fsanitize={selection}", "-fno-omit-frame-pointer", "-fno-sanitize-recover=all"]
    configure += [f"-DCMAKE_CXX_FLAGS={' '.join(flags)}"]
    run(*configure)
    run("cmake", "--build", str(binary), "--target", "tint_api", "--parallel", str(args.jobs))
    libraries = sorted(binary.rglob("*.lib" if platform.system() == "Windows" else "*.a"))
    if not libraries:
        raise RuntimeError("Tint build produced no static archives")
    (prefix / "lib").mkdir(parents=True, exist_ok=True)
    archive = prefix / "lib" / ("stana-tint.lib" if platform.system() == "Windows" else "libstana-tint.a")
    if platform.system() == "Windows":
        run(str(compiler.parent / "llvm-lib.exe"), f"/OUT:{archive}", *map(str, libraries))
    else:
        # MRI merges archive members without ever running a command per shader/frame.
        script = f"CREATE {archive}\n" + "".join(f"ADDLIB {lib}\n" for lib in libraries) + "SAVE\nEND\n"
        run(str(compiler.parent / "llvm-ar"), "-M", input=script, text=True)
    include = prefix / "include"
    for folder in (source / "src/tint", source / "src/utils", source / "third_party/abseil-cpp/absl"):
        relative = Path("absl") if folder.name == "absl" else Path("src") / folder.name
        for header in folder.rglob("*.h"):
            target = include / relative / header.relative_to(folder)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(header, target)
    licenses = prefix / "licenses"
    licenses.mkdir(exist_ok=True)
    for name, _, _, _, relative in DEPENDENCIES:
        path = source / relative / "LICENSE"
        if path.exists():
            shutil.copy2(path, licenses / f"{name}.txt")
    marker.write_text(json.dumps(identity, indent=2) + "\n")


if __name__ == "__main__":
    main()
