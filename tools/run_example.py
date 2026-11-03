"""Build and run a supported public-API example from any working directory."""

import argparse
import os
import sys
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
	"""Validate the example name before constructing build targets or executable paths."""
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("name", choices=("counter",))
	args = parser.parse_args()
	target = f"stana-{args.name}"
	subprocess.run(["cmake", "--preset", "debug"], cwd=ROOT, check=True)
	subprocess.run(["cmake", "--build", "--preset", "debug", "--target", target,
		"--parallel", "3"], cwd=ROOT, check=True)
	suffix = ".exe" if os.name == "nt" else ""
	executable = ROOT / "build/debug" / f"{target}{suffix}"
	if sys.platform == "darwin":
		bundle = ROOT / "build/debug" / f"{target}.app"
		result = subprocess.run(["open", "-W", "-n", str(bundle)], cwd=ROOT)
	else:
		result = subprocess.run([str(executable)], cwd=ROOT)
	raise SystemExit(result.returncode)


if __name__ == "__main__":
	main()
