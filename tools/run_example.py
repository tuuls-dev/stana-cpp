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
	parser.add_argument("name", choices=("counter", "symbols"))
	parser.add_argument("--trace", action=argparse.BooleanOptionalAction, default=True,
		help="Enable native rendering traces (on by default)")
	args = parser.parse_args()
	target = f"stana-{args.name}"
	subprocess.run(["cmake", "--preset", "debug"], cwd=ROOT, check=True)
	subprocess.run(["cmake", "--build", "--preset", "debug", "--target", target,
		"--parallel", "3"], cwd=ROOT, check=True)
	suffix = ".exe" if os.name == "nt" else ""
	executable = ROOT / "build/debug" / f"{target}{suffix}"
	environment = os.environ.copy()
	if args.trace:
		environment["STANA_TRACE_NATIVE"] = "1"
	else:
		environment.pop("STANA_TRACE_NATIVE", None)
	if sys.platform == "darwin":
		bundle = ROOT / "build/debug" / f"{target}.app"
		command = ["/usr/bin/open", "-W", "-n"]
		if args.trace:
			command.extend(["--env", "STANA_TRACE_NATIVE=1"])
		if sys.stderr.isatty():
			command.extend(["--stderr", os.ttyname(sys.stderr.fileno())])
		command.append(str(bundle))
		result = subprocess.run(command, cwd=ROOT, env=environment)
	else:
		result = subprocess.run([str(executable)], cwd=ROOT, env=environment)
	raise SystemExit(result.returncode)


if __name__ == "__main__":
	main()
