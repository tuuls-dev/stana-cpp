"""Format or check first-party C++/Objective-C++ with the qualified LLVM formatter.

No configure/build is needed for `just format`; dependencies and native services
are irrelevant to formatting. LLVM_ROOT remains an explicit toolchain override.
"""

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SOURCE_SUFFIXES = {".cpp", ".cppm", ".cxx", ".cc", ".hpp", ".h", ".hxx", ".ixx", ".mm", ".m"}


def formatter() -> Path:
	"""Find the matching formatter without changing the user's system toolchain."""
	version = re.search(r'set\(STANA_LLVM_VERSION "([0-9.]+)"\)',
		(ROOT / "cmake/Safety.cmake").read_text())[1]
	executable = "clang-format.exe" if os.name == "nt" else "clang-format"
	llvm_root = os.environ.get("LLVM_ROOT")
	if llvm_root:
		candidates = [Path(llvm_root) / "bin" / executable]
	else:
		candidates = [ROOT / ".tools/llvm/bin" / executable,
			Path("/opt/homebrew/opt/llvm@22/bin") / executable,
			Path("/usr/local/opt/llvm@22/bin") / executable]
		if discovered := shutil.which(executable):
			candidates.append(Path(discovered))
	for candidate in candidates:
		if candidate.is_file():
			result = subprocess.run([str(candidate), "--version"], text=True, capture_output=True, check=True)
			if re.search(rf"version {re.escape(version)}(?:[^0-9.]|$)", result.stdout):
				return candidate
	raise RuntimeError(f"clang-format {version} is required; install the qualified LLVM tools or set LLVM_ROOT")


def source_files() -> list[Path]:
	"""Select only first-party sources; exclude dependencies and build-generated files."""
	return sorted(path for folder in ("modules", "src", "tests", "examples")
		for path in (ROOT / folder).rglob("*") if path.suffix in SOURCE_SUFFIXES)


def main() -> None:
	"""Apply repository formatting or fail on a formatting difference."""
	parser = argparse.ArgumentParser(description=__doc__)
	mode = parser.add_mutually_exclusive_group(required=True)
	mode.add_argument("--write", action="store_true")
	mode.add_argument("--check", action="store_true")
	args = parser.parse_args()
	options = ["--dry-run", "--Werror"] if args.check else ["-i"]
	subprocess.run([str(formatter()), *options, *map(str, source_files())], cwd=ROOT, check=True)


if __name__ == "__main__":
	main()
