"""Package verified builds and versioned source archives for GitHub Releases."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def package(output: Path, config: str) -> None:
	with tempfile.TemporaryDirectory(prefix="stana-release-") as work:
		command = ["zig", "build", "package", "--prefix", work]
		if config == "Release":
			command.append("--release=fast")
		subprocess.run(command, cwd=ROOT, check=True)
		for artifact in (Path(work) / "dist").glob("*.zip"):
			shutil.copy2(artifact, output)


def archive(sdk: Path, output: Path) -> None:
	info = json.loads((sdk / "share/stana/build-info.json").read_text(encoding="utf-8"))
	if info["sanitizer"] != "none":
		raise RuntimeError("Sanitized builds cannot be packaged")
	if list(sdk.rglob("*.pcm")):
		raise RuntimeError("Packages must not ship compiler-specific BMIs")
	system = {"Darwin": "macos", "Windows": "windows", "Linux": "linux"}[info["system"]]
	machine = info["architecture"]
	arch = {"arm64": "arm64", "x86_64": "x64"}.get(machine, machine)
	name = f"stana-{info['version']}-{system}-{arch}-clang{info['compiler_version']}-{info['configuration'].lower()}"
	info["host"] = platform.platform()
	info["minimum_os"] = {"linux": "Ubuntu 24.04 / glibc 2.39",
		"macos": f"macOS {info['macos_deployment_target']}", "windows": "Windows 11"}[system]
	info["standard_library"] = {"linux": "libstdc++ (GCC 14+)",
		"macos": "system libc++", "windows": "MSVC STL"}[system]
	info["libc"] = platform.libc_ver()
	info["windows_runtime"] = ("/MDd (Debug CRT, iterator level 2)" if info["configuration"] == "Debug"
		else "/MD (Release CRT, iterator level 0)") if system == "windows" else None
	with zipfile.ZipFile(output / f"{name}.zip", "w", compression=zipfile.ZIP_DEFLATED) as result:
		for path in sorted(sdk.rglob("*")):
			if path.is_file():
				member = Path(name) / path.relative_to(sdk)
				if member.as_posix() == f"{name}/share/stana/build-info.json":
					result.writestr(member.as_posix(), json.dumps(info, indent="\t") + "\n")
				else:
					result.write(path, member)


def source(output: Path, tag: str) -> None:
	manifest = (ROOT / "build.zig.zon").read_text(encoding="utf-8")
	version = re.search(r'^\s*\.version = "([0-9.]+)"', manifest, re.M)[1]
	if tag != f"v{version}":
		raise RuntimeError(f"Release tag {tag!r} must match v{version}")
	for kind in ("zip", "tar.gz"):
		subprocess.run(["git", "archive", f"--format={kind}", f"--prefix=stana-{version}/",
			"--output", str(output / f"stana-{version}-source.{kind}"), tag], cwd=ROOT, check=True)


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
	parser.add_argument("action", choices=("package", "archive", "source", "checksums"))
	parser.add_argument("--config", choices=("Debug", "Release"), default="Release")
	parser.add_argument("--sdk", type=Path)
	parser.add_argument("--output", type=Path, default=Path("dist"))
	parser.add_argument("--tag", default=os.environ.get("GITHUB_REF_NAME", ""))
	args = parser.parse_args()
	args.output = args.output.resolve()
	args.output.mkdir(parents=True, exist_ok=True)
	if args.action == "package":
		package(args.output, args.config)
	elif args.action == "archive":
		if args.sdk is None:
			parser.error("archive requires --sdk")
		archive(args.sdk, args.output)
	elif args.action == "source":
		source(args.output, args.tag)
	else:
		checksums(args.output)
