"""Require the diagnostic contract executable to leave both output streams empty."""

import subprocess
import sys


result = subprocess.run(sys.argv[1:], capture_output=True, check=False)
if result.stdout or result.stderr:
	sys.stdout.buffer.write(result.stdout)
	sys.stderr.buffer.write(result.stderr)
	sys.exit(1)
sys.exit(result.returncode)
