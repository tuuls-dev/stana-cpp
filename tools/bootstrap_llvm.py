"""Install verified LLVM 22.1.8 locally, never replacing the system compiler."""

import argparse
import hashlib
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tarfile
import urllib.request

VERSION = "22.1.8"
BASE_URL = f"https://github.com/llvm/llvm-project/releases/download/llvmorg-{VERSION}/"
PACKAGES = {
	("Linux", "x86_64"): (
		f"LLVM-{VERSION}-Linux-X64.tar.xz",
		"df0e1ecf16caf3489a272a5eea4eec9b0d82878f6477fa309504f918a0006384",
	),
	("Darwin", "arm64"): (
		f"LLVM-{VERSION}-macOS-ARM64.tar.xz",
		"f260f4f7c0d430828a81ae8a3826a1d63fc0963ec2459489308cc23b1f7eab4f",
	),
	("Windows", "amd64"): (
		f"clang+llvm-{VERSION}-x86_64-pc-windows-msvc.tar.xz",
		"d96c2cc1736f4eb7fa43cb9bbdf56d93551a9ae0a9aadb9c99c3c3b2b712a234",
	),
	("Darwin", "x86_64"): (
		f"llvm-project-{VERSION}.src.tar.xz",
		"922f1817a0df7b1489272d18134ee0087a8b068828f87ac63b9861b1a9965888",
	),
}


def run(*args: str) -> None:
	subprocess.run(args, check=True)


def verified_download(name: str, digest: str, folder: Path) -> Path:
	archive = folder / name
	if not archive.exists():
		partial = archive.with_suffix(".download")
		print(f"Downloading {name}", flush=True)
		with urllib.request.urlopen(BASE_URL + name, timeout=120) as response, partial.open("wb") as output:
			shutil.copyfileobj(response, output)
		partial.rename(archive)
	with archive.open("rb") as stream:
		actual = hashlib.file_digest(stream, "sha256").hexdigest()
	if actual != digest:
		raise RuntimeError(f"SHA-256 mismatch for {archive}: expected {digest}, got {actual}")
	return archive


def initialize_msvc() -> None:
	vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
	installation = subprocess.check_output([
		str(vswhere), "-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
		"-property", "installationPath",
	], text=True).strip()
	if not installation:
		raise RuntimeError("Install Visual Studio C++ Build Tools first.")
	script = Path(installation) / "Common7/Tools/VsDevCmd.bat"
	environment = subprocess.check_output(
		["cmd.exe", "/s", "/c", f'""{script}" -arch=x64 -host_arch=x64 >nul && set"'], text=True,
	)
	previous = {key.casefold(): value for key, value in os.environ.items()}
	with open(os.environ["GITHUB_ENV"], "a", encoding="utf-8") as output:
		for line in environment.splitlines():
			key, separator, value = line.partition("=")
			if (separator and key and not key.startswith("=")
				and not key.upper().startswith(("GITHUB_", "RUNNER_", "NODE_OPTIONS"))
				and previous.get(key.casefold()) != value):
				output.write(f"{key}={value}\n")


def main() -> None:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--destination", type=Path, default=Path(".tools/llvm"))
	parser.add_argument("--github-env", action="store_true")
	args = parser.parse_args()
	system, machine = platform.system(), platform.machine().lower()
	package = PACKAGES.get((system, machine))
	if package is None:
		raise RuntimeError(f"Unsupported target {system}/{machine}")
	destination = args.destination.resolve()
	marker = destination / ".stana-toolchain"
	if not marker.exists():
		if destination.exists():
			raise RuntimeError(f"Refusing to overwrite existing unmarked directory {destination}")
		scratch = destination.parent / "llvm-download"
		scratch.mkdir(parents=True, exist_ok=True)
		archive = verified_download(*package, scratch)
		extraction = scratch / "extracted"
		if extraction.exists():
			shutil.rmtree(extraction)
		extraction.mkdir()
		print("Extracting verified LLVM archive", flush=True)
		with tarfile.open(archive) as source:
			# data filter rejects traversal, device files and escaping links.
			source.extractall(extraction, filter="data")
		roots = list(extraction.iterdir())
		if len(roots) != 1 or not roots[0].is_dir():
			raise RuntimeError("Unexpected archive layout")
		if (system, machine) == ("Darwin", "x86_64"):
			build = scratch / "build"
			run("cmake", "-S", str(roots[0] / "llvm"), "-B", str(build), "-G", "Ninja",
				"-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_INSTALL_PREFIX={destination}",
				"-DLLVM_ENABLE_PROJECTS=clang;clang-tools-extra", "-DLLVM_TARGETS_TO_BUILD=X86",
				"-DLLVM_ENABLE_RUNTIMES=compiler-rt", "-DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON",
				"-DCOMPILER_RT_BUILD_XRAY=OFF", "-DCOMPILER_RT_BUILD_LIBFUZZER=OFF",
				"-DCOMPILER_RT_BUILD_PROFILE=OFF", "-DCOMPILER_RT_BUILD_MEMPROF=OFF",
				"-DLLVM_INCLUDE_TESTS=OFF", "-DLLVM_INCLUDE_EXAMPLES=OFF", "-DLLVM_INCLUDE_BENCHMARKS=OFF",
				"-DCLANG_INCLUDE_TESTS=OFF", "-DCLANG_TIDY_ENABLE_QUERY_BASED_CUSTOM_CHECKS=ON",
				"-DLLVM_ENABLE_ASSERTIONS=OFF", "-DLLVM_ENABLE_ZSTD=OFF", "-DLLVM_ENABLE_ZLIB=OFF")
			run("cmake", "--build", str(build), "--parallel", "3", "--target",
				"install-clang", "install-clang-resource-headers", "install-clang-tidy",
				"install-clang-format", "install-clang-scan-deps", "install-llvm-ar", "install-runtimes")
		else:
			roots[0].rename(destination)
		marker.write_text(f"{VERSION}\n{package[1]}\n", encoding="utf-8")
		shutil.rmtree(scratch)
	elif marker.read_text().splitlines() != [VERSION, package[1]]:
		raise RuntimeError("Cached LLVM identity does not match the pinned release")
	for tool in ("clang++", "clang-cl", "clang-tidy", "clang-format", "clang-scan-deps"):
		executable = destination / "bin" / (tool + (".exe" if system == "Windows" else ""))
		run(str(executable), "--version")
	if args.github_env:
		if system == "Windows":
			initialize_msvc()
		with open(os.environ["GITHUB_ENV"], "a", encoding="utf-8") as output:
			output.write(f"LLVM_ROOT={destination}\n")
		with open(os.environ["GITHUB_PATH"], "a", encoding="utf-8") as output:
			output.write(f"{destination / 'bin'}\n")
	print(f"Set LLVM_ROOT to {destination}", flush=True)


if __name__ == "__main__":
	main()
