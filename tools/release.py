"""Package verified builds and versioned source archives for GitHub Releases."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def run(*args: str, **kwargs) -> None:
	subprocess.run(args, check=True, **kwargs)


def zip_tree(folder: Path, archive: Path) -> None:
	with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as output:
		for path in sorted(folder.rglob("*")):
			if path.is_file():
				output.write(path, path.relative_to(folder.parent))


def package(build: Path, output: Path) -> None:
	cache = (build / "CMakeCache.txt").read_text()
	if not re.search(r"^STANA_SANITIZER:STRING=$", cache, re.M):
		raise RuntimeError("Sanitized/unrecognized builds must not be packaged")
	# Never package untested build outputs, even when invoked outside CI.
	run("cmake", "--build", str(build), "--parallel", "2")
	run("ctest", "--test-dir", str(build), "--output-on-failure", "--no-tests=error")
	info = json.loads((build / "build-info.json").read_text())
	system = {"Darwin": "macos", "Windows": "windows", "Linux": "linux"}[info["system"]]
	machine = info["architecture"].lower()
	arch = "arm64" if machine in ("arm64", "aarch64") else "x64"
	name = f"stana-{info['version']}-{system}-{arch}-clang{info['compiler_version']}-{info['configuration'].lower()}"
	with tempfile.TemporaryDirectory(prefix="stana-package-", dir=build) as temporary:
		stage = Path(temporary) / name
		run("cmake", "--install", str(build), "--prefix", str(stage))
		metadata = stage / "share/stana/build-info.json"
		info["host"] = platform.platform()
		info["minimum_os"] = {"linux": "Ubuntu 24.04 / glibc 2.39", "macos": f"macOS {info['macos_deployment_target']}", "windows": "Windows 11"}[system]
		info["standard_library"] = {"linux": "libstdc++ (GCC 14)", "macos": "system libc++", "windows": "MSVC STL"}[system]
		info["libc"] = platform.libc_ver()
		info["windows_runtime"] = ("/MDd (Debug CRT, iterator level 2)" if info["configuration"] == "Debug"
								   else "/MD (Release CRT, iterator level 0)") if system == "windows" else None
		compiler = next(line.split("=", 1)[1] for line in cache.splitlines()
						if line.startswith("CMAKE_CXX_COMPILER:"))
		flags = shlex.split(os.environ.get("CXXFLAGS", "")) if system == "linux" else []
		if system == "macos":
			sdk = next((line.split("=", 1)[1] for line in cache.splitlines()
				if line.startswith("CMAKE_OSX_SYSROOT:")), "")
			if sdk:
				flags.extend(["-isysroot", sdk])
		# Record actual installed STL revision macros, not just a vendor label.
		preprocessor = subprocess.run(
			[str(Path(compiler).with_name("clang++" + (".exe" if os.name == "nt" else ""))),
			 *flags, "-std=c++23", "-dM", "-E", "-x", "c++", "-include", "version", "-"],
			input="", capture_output=True, text=True, check=True,
		)
		info["standard_library_macros"] = {
			match[1]: match[2] for match in re.finditer(
				r"^#define (_LIBCPP_VERSION|__GLIBCXX__|_GLIBCXX_RELEASE|_MSVC_STL_VERSION|_MSVC_STL_UPDATE|_MSC_VER) (.+)$",
				preprocessor.stdout, re.M,
			)
		}
		metadata.write_text(json.dumps(info, indent="\t") + "\n", encoding="utf-8")
		if list(stage.rglob("*.pcm")):
			raise RuntimeError("Packages must not ship compiler-specific BMIs")
		zip_tree(stage, output / f"{name}.zip")


def source(output: Path, tag: str) -> None:
	version = re.search(r"project\(stana VERSION ([0-9.]+)", (ROOT / "CMakeLists.txt").read_text())[1]
	if tag != f"v{version}":
		raise RuntimeError(f"Release tag {tag!r} must match v{version}")
	run("git", "archive", "--format=zip", f"--prefix=stana-{version}/",
		"--output", str(output / f"stana-{version}-source.zip"), tag, cwd=ROOT)
	run("git", "archive", "--format=tar.gz", f"--prefix=stana-{version}/",
		"--output", str(output / f"stana-{version}-source.tar.gz"), tag, cwd=ROOT)


def checksums(output: Path) -> None:
	lines = []
	for path in sorted(output.iterdir()):
		if path.is_file() and path.name != "SHA256SUMS":
			with path.open("rb") as stream:
				digest = hashlib.file_digest(stream, "sha256").hexdigest()
			lines.append(f"{digest}  {path.name}")
	(output / "SHA256SUMS").write_text("\n".join(lines) + "\n", encoding="utf-8")


if __name__ == "__main__":
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("action", choices=("package", "source", "checksums"))
	parser.add_argument("--build", type=Path)
	parser.add_argument("--output", type=Path, default=Path("dist"))
	parser.add_argument("--tag", default=os.environ.get("GITHUB_REF_NAME", ""))
	args = parser.parse_args()
	args.output = args.output.resolve()
	args.output.mkdir(parents=True, exist_ok=True)
	if args.action == "package":
		if args.build is None:
			parser.error("package requires --build")
		package(args.build.resolve(), args.output)
	elif args.action == "source":
		source(args.output, args.tag)
	else:
		checksums(args.output)
