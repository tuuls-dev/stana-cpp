"""Launch a built Stana example binary or macOS .app (native traces on by default).

`just example counter` / `zig build run-counter` compile the example then hand
the artifact to this script. On Darwin a `.app` is opened with `/usr/bin/open
-W -n` so AppKit/Metal come up as a real bundle; other hosts exec the binary.

`STANA_TRACE_NATIVE=1` is set unless `--no-trace` (Zig `-Dtrace=false`). For
.app launches the env is also passed via `open --env`, and a TTY stderr is
forwarded so compositor traces still hit the invoking terminal.

  python3 tools/run_example.py path/to/stana-counter.app
  python3 tools/run_example.py path/to/stana-canvas --no-trace
"""

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
