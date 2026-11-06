"""Compile symbol fixtures that must be rejected during constant evaluation."""

import argparse
import json
from pathlib import Path
import shlex
import subprocess
import sys


def compiler_arguments(build: Path, source: Path, output: Path) -> list[str]:
	commands = json.loads((build / "compile_commands.json").read_text())
	command = next(
		entry["command"]
		for entry in commands
		if entry["file"].endswith("tests/symbols/compiler.cpp")
	)
	arguments = shlex.split(command)
	skip_next = False
	filtered: list[str] = []
	drop_with_value = {"-o", "-c", "-MF", "-MT"}
	for argument in arguments:
		if skip_next:
			skip_next = False
			continue
		if argument in drop_with_value or argument in {"-MD"}:
			skip_next = argument in drop_with_value
			continue
		if argument.startswith("--") or argument.endswith("compiler.cpp"):
			continue
		filtered.append(argument)
	return [*filtered, "-c", str(source), "-o", str(output)]


def main() -> None:
	parser = argparse.ArgumentParser()
	parser.add_argument("--build", required=True)
	parser.add_argument("--root", required=True)
	args = parser.parse_args()
	build = Path(args.build)
	fixtures = sorted((Path(args.root) / "tests/symbols/fail").glob("*.cpp"))
	if not fixtures:
		sys.exit("no symbol compile-fail fixtures")
	if not (build / "compile_commands.json").is_file():
		sys.exit("missing compile_commands.json; configure the debug preset first")
	accepted: list[str] = []
	for fixture in fixtures:
		output = build / "symbol-fail" / f"{fixture.stem}.o"
		output.parent.mkdir(exist_ok=True)
		result = subprocess.run(
			compiler_arguments(build, fixture, output),
			cwd=build,
			capture_output=True,
			text=True,
			check=False,
		)
		if result.returncode == 0:
			accepted.append(fixture.name)
			continue
		diagnostic = result.stderr
		if "constant expression" not in diagnostic and "not a constant expression" not in diagnostic:
			sys.stderr.write(diagnostic)
			sys.exit(f"{fixture.name} failed for a reason other than constant evaluation")
	if accepted:
		sys.exit("expected these fixtures to fail constant evaluation: " + ", ".join(accepted))


if __name__ == "__main__":
	main()
