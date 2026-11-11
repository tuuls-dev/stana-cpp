"""Launch the native test through LaunchServices and preserve its actual result.

Launching an unbundled test behind an unrelated foreground app can correctly keep
all rendering occluded. No synthetic timestamp or visibility override is used.
"""
from pathlib import Path
import os
import plistlib
import signal
import subprocess
import sys
import tempfile
import time


def main() -> int:
    bundle = Path(sys.argv[1]).resolve()
    with (bundle / "Contents/Info.plist").open("rb") as stream:
        executable = bundle / "Contents/MacOS" / plistlib.load(stream)["CFBundleExecutable"]
    with tempfile.TemporaryDirectory(prefix="stana-native-") as folder:
        directory = Path(folder)
        result = directory / "result"
        output = directory / "stdout"
        error = directory / "stderr"
        validation = [item for key in ("MTL_DEBUG_LAYER", "MTL_SHADER_VALIDATION", "STANA_MANUAL_FACILITIES")
                      if key in os.environ for item in ("--env", f"{key}={os.environ[key]}")]
        try:
            launch = subprocess.Popen(
                ["/usr/bin/open", "-n", "-W", "-o", str(output), "--stderr", str(error),
                 "--env", f"STANA_NATIVE_RESULT_FILE={result}", "--env", "STANA_TRACE_NATIVE=1", *validation,
                 str(bundle)],
            )
            try:
                # Reopen once after AppKit registration, rather than racing the
                # initial LaunchServices activation against programmatic startup.
                deadline = time.monotonic() + 8
                ready = Path(str(result) + ".ready")
                while not ready.exists() and launch.poll() is None and time.monotonic() < deadline:
                    time.sleep(0.05)
                if ready.exists() and launch.poll() is None:
                    subprocess.run(["/usr/bin/open", "-a", str(bundle)], check=True, timeout=5)
                launch.wait(timeout=30)
                if launch.returncode != 0:
                    return 1
            finally:
                if launch.poll() is None:
                    launch.terminate()
                    launch.wait(timeout=5)
            for log in (output, error):
                if log.exists():
                    print(log.read_text(), end="")
            if not result.exists():
                print("Native process exited without an explicit result", file=sys.stderr)
                return 1
            return 0 if result.read_text().strip() == "0" else 1
        finally:
            # Only clean up the exact launched fixture executable, never another app.
            identity = Path(str(result) + ".pid")
            if identity.exists():
                process = int(identity.read_text())
                command = subprocess.run(["/bin/ps", "-p", str(process), "-o", "comm="],
                                         capture_output=True, text=True, check=False)
                if command.returncode == 0 and Path(command.stdout.strip()).resolve() == executable:
                    try:
                        os.kill(process, signal.SIGTERM)
                    except ProcessLookupError:
                        pass


if __name__ == "__main__":
    sys.exit(main())
