"""Exercise source and relocated SDK consumers through independent Zig builds."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def verify_failure(command: list[str], cwd: Path, env: dict[str, str], diagnostic: str) -> None:
	result = subprocess.run(command, cwd=cwd, env=env, capture_output=True, text=True)
	message = result.stdout + result.stderr
	if result.returncode == 0 or diagnostic not in message:
		raise RuntimeError(f"Expected {diagnostic!r} failure from {command}:\n{message}")


def main() -> None:
	parser = argparse.ArgumentParser()
	parser.add_argument("--root", type=Path, required=True)
	parser.add_argument("--sdk", type=Path, required=True)
	parser.add_argument("--zig", required=True)
	parser.add_argument("--config", choices=("Debug", "Release"), required=True)
	parser.add_argument("--deployment", required=True)
	args = parser.parse_args()
	rejects = {
		"old_canvas": "Canvas",
		"grouped_window_options": "geometry",
		"native_shader": "metalSource",
		"native_library": "metalLibrary",
		"raw_points": "Points",
		"reactive_mismatch": "Property",
		"asset_point": "AssetPoint",
		"asset_transform": "AssetTransform",
		"frame_clock": "PresentationTimePoint",
	}
	if sys.platform != "darwin":
		rejects["macos_window_options"] = "PlatformWindowOptions"
	temp_root = Path(tempfile.gettempdir()).resolve()
	if sys.platform == "win32" and temp_root.drive.casefold() != args.root.drive.casefold():
		temp_root = args.root / ".zig-cache"
		temp_root.mkdir(exist_ok=True)
	with tempfile.TemporaryDirectory(prefix="stana-consumer-", dir=temp_root) as temporary:
		work = Path(temporary)
		stage = work / "original-prefix"
		shutil.copytree(args.sdk, stage)
		relocated = stage.rename(work / "relocated prefix with spaces")
		if list(relocated.rglob("*.pcm")):
			raise RuntimeError("Installed SDK must not ship compiler-specific BMIs")
		for path in relocated.rglob("*"):
			if path.is_file() and path.suffix in (".zig", ".zon", ".json"):
				if any(str(original) in path.read_text(encoding="utf-8") for original in (args.root, args.sdk)):
					raise RuntimeError(f"Installed metadata leaks a source/build path: {path}")
		env = os.environ.copy()
		for kind, dependency in (("installed", relocated), ("source", args.root)):
			consumer = work / f"{kind} consumer"
			shutil.copytree(args.root / "tests/consumer", consumer)
			path = os.path.relpath(dependency, consumer)
			(consumer / "build.zig.zon").write_text(
				'.{ .name = .stana_consumer, .fingerprint = 0xa1a6db56620928d, '
				'.version = "0.0.0", .paths = .{""}, '
				f'.dependencies = .{{ .stana = .{{ .path = {json.dumps(path, ensure_ascii=False)} }} }} }}\n',
				encoding="utf-8")
			command = [args.zig, "build", "-j2", "--summary", "failures"]
			if args.config == "Release":
				command.append("--release=fast")
			if sys.platform == "darwin":
				verify_failure([*command, "-Dmacos-deployment-target=15.0"], consumer, env,
					"requires a macOS deployment target")
			command.append(f"-Dmacos-deployment-target={args.deployment}")
			subprocess.run(command, cwd=consumer, env=env, check=True)
			for fixture, diagnostic in rejects.items():
				verify_failure([*command, f"-Dreject={fixture}"], consumer, env, diagnostic)


if __name__ == "__main__":
	main()
