"""Compile symbol fixtures that must be rejected during constant evaluation."""

import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def main() -> None:
	parser = argparse.ArgumentParser()
	parser.add_argument("--command", type=Path, required=True)
	parser.add_argument("--root", type=Path, required=True)
	args = parser.parse_args()
	command = json.loads(args.command.read_text(encoding="utf-8"))
	compiler = command["arguments"][0]
	fixtures = sorted((args.root / "tests/symbols/fail").glob("*.cpp"))
	if not fixtures:
		sys.exit("no symbol compile-fail fixtures")
	accepted: list[str] = []
	with tempfile.TemporaryDirectory(prefix="stana-symbol-fail-") as work:
		for fixture in fixtures:
			output = Path(work) / f"{fixture.stem}.o"
			result = subprocess.run([compiler, *command["flags"], "-c", str(fixture), "-o", str(output)],
				cwd=command["directory"], capture_output=True, text=True)
			if result.returncode == 0:
				accepted.append(fixture.name)
			elif "constant expression" not in result.stderr:
				sys.stderr.write(result.stderr)
				sys.exit(f"{fixture.name} failed for a reason other than constant evaluation")
	if accepted:
		sys.exit("expected these fixtures to fail constant evaluation: " + ", ".join(accepted))


if __name__ == "__main__":
	main()
