"""Format first-party C++/ObjC++ with the pinned LLVM 22 clang-format. No build required.

Walks `modules/`, `src/`, `tests/`, and `examples/` only. Does not touch
`.tools/`, generated headers, or third-party trees. Uses `stana_build.llvm_tool`
so the formatter is the same 22.1.8 binary as compiles (`LLVM_ROOT` override
still applies). ICU/Tint/Metal are irrelevant.

  just format            # or: python3 tools/format.py --write
  just format-check      # or: python3 tools/format.py --check / zig build format-check

`--check` is `--dry-run --Werror`: any diff fails the step.
"""

import argparse
from pathlib import Path
import subprocess

from stana_build import llvm_tool

ROOT = Path(__file__).resolve().parents[1]
SOURCE_SUFFIXES = {".cpp", ".cppm", ".cxx", ".cc", ".hpp", ".h", ".hxx", ".ixx", ".mm", ".m"}


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
	subprocess.run([str(llvm_tool(ROOT, "clang-format")), *options, *map(str, source_files())], cwd=ROOT, check=True)


if __name__ == "__main__":
	main()
