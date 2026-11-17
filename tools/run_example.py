"""Launch a Stana example produced by the Zig build."""

import argparse
import os
from pathlib import Path
import subprocess
import sys


def main() -> None:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("artifact", type=Path)
	parser.add_argument("--trace", action=argparse.BooleanOptionalAction, default=True)
	args = parser.parse_args()
	artifact = args.artifact.resolve()
	environment = os.environ.copy()
	if args.trace:
		environment["STANA_TRACE_NATIVE"] = "1"
	else:
		environment.pop("STANA_TRACE_NATIVE", None)
	if artifact.suffix == ".app" and sys.platform == "darwin":
		command = ["/usr/bin/open", "-W", "-n"]
		if args.trace:
			command.extend(["--env", "STANA_TRACE_NATIVE=1"])
		if sys.stderr.isatty():
			command.extend(["--stderr", os.ttyname(sys.stderr.fileno())])
		command.append(str(artifact))
	else:
		command = [str(artifact)]
	raise SystemExit(subprocess.run(command, env=environment).returncode)


if __name__ == "__main__":
	main()
