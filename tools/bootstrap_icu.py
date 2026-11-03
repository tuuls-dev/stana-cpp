"""Install a SHA-256-pinned ICU4C dependency without modifying system packages.

Unix builds use upstream source with static libraries/data. Windows uses upstream's
MSVC 2022 x64 binaries; its DLL directory is exported for runtime discovery in CI.
The dependency is external code, not a first-party warning-policy exception.
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
import tempfile
import urllib.request
import zipfile

VERSION = "78.3"
RELEASE = f"https://github.com/unicode-org/icu/releases/download/release-{VERSION}"
ARCHIVES = {
	"source": ("icu4c-78.3-sources.tgz", "3a2e7a47604ba702f345878308e6fefeca612ee895cf4a5f222e7955fabfe0c0"),
	"windows": ("icu4c-78.3-Win64-MSVC2022.zip", "446b671f9437227daa79e221d4521d75793f9ecd65ac44c06e34dd848f201ac2"),
}
ROOT = Path(__file__).resolve().parents[1]


def verify_archive(archive: Path, expected: str) -> None:
	"""Reject a downloaded dependency before extraction if its digest differs."""
	with archive.open("rb") as stream:
		actual = hashlib.file_digest(stream, "sha256").hexdigest()
	if actual != expected:
		raise RuntimeError(f"ICU archive digest mismatch: expected {expected}, got {actual}")


def install(prefix: Path, jobs: int) -> None:
	"""Build or unpack the selected official release into a project-local prefix."""
	is_windows = platform.system() == "Windows"
	if is_windows and platform.machine().lower() not in ("amd64", "x86_64"):
		raise RuntimeError("The supported Windows ICU package is x64")
	identity = {"version": VERSION, "system": platform.system(), "architecture": platform.machine()}
	marker = prefix / "stana-dependency.json"
	if marker.exists() and json.loads(marker.read_text()) == identity:
		return
	if prefix.exists() and any(prefix.iterdir()):
		raise RuntimeError(f"Refusing to overwrite an unrecognized ICU installation: {prefix}")
	name, digest = ARCHIVES["windows" if is_windows else "source"]
	work_root = ROOT / "build"
	work_root.mkdir(exist_ok=True)
	with tempfile.TemporaryDirectory(prefix="icu-bootstrap-", dir=work_root) as temporary:
		work = Path(temporary)
		archive = work / name
		request = urllib.request.Request(f"{RELEASE}/{name}", headers={"User-Agent": "stana-dependency-bootstrap"})
		with urllib.request.urlopen(request, timeout=120) as response, archive.open("wb") as output:
			shutil.copyfileobj(response, output)
		verify_archive(archive, digest)
		if is_windows:
			with zipfile.ZipFile(archive) as package:
				# Official package content still gets checked against path traversal.
				for entry in package.infolist():
					candidate = (work / entry.filename).resolve()
					if not candidate.is_relative_to(work.resolve()):
						raise RuntimeError("Unsafe path in ICU archive")
				package.extractall(work)
			shutil.copytree(work / "icu", prefix, dirs_exist_ok=True)
		else:
			with tarfile.open(archive) as package:
				package.extractall(work, filter="data")
			source = work / "icu"
			build = work / "compiled"
			build.mkdir()
			environment = dict(os.environ)
			llvm_root = environment.get("LLVM_ROOT")
			if llvm_root:
				environment["CC"] = str(Path(llvm_root) / "bin/clang")
				environment["CXX"] = str(Path(llvm_root) / "bin/clang++")
			for key in ("CFLAGS", "CXXFLAGS"):
				environment[key] = environment.get(key, "") + " -fPIC"
			preset = "MacOSX" if platform.system() == "Darwin" else "Linux"
			subprocess.run(["sh", str(source / "source/runConfigureICU"), preset,
				f"--prefix={prefix}", "--enable-static", "--disable-shared",
				"--with-data-packaging=static", "--disable-tests", "--disable-samples"],
				cwd=build, env=environment, check=True)
			subprocess.run(["make", f"-j{jobs}"], cwd=build, env=environment, check=True)
			subprocess.run(["make", "install"], cwd=build, env=environment, check=True)
			license_path = source / "LICENSE"
			if license_path.exists():
				shutil.copy2(license_path, prefix / "LICENSE")
	prefix.mkdir(parents=True, exist_ok=True)
	marker.write_text(json.dumps(identity, indent=2) + "\n")


def main() -> None:
	"""Install ICU and optionally export its prefix to subsequent GitHub Actions steps."""
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--prefix", type=Path, default=ROOT / ".tools/icu")
	parser.add_argument("--jobs", type=int, default=3)
	parser.add_argument("--github-env", action="store_true")
	args = parser.parse_args()
	if args.jobs < 1:
		parser.error("--jobs must be positive")
	prefix = args.prefix.resolve()
	install(prefix, args.jobs)
	if args.github_env:
		with Path(os.environ["GITHUB_ENV"]).open("a") as output:
			output.write(f"ICU_ROOT={prefix}\n")
		if platform.system() == "Windows":
			with Path(os.environ["GITHUB_PATH"]).open("a") as output:
				output.write(f"{prefix / 'bin64'}\n")
	print(f"ICU_ROOT={prefix}")


if __name__ == "__main__":
	main()
