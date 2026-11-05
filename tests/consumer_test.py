"""Exercise relocated install and source consumers without reusing project BMIs."""

import argparse
from pathlib import Path
import shutil
import subprocess
import sys


def run(*args: str) -> None:
	subprocess.run(args, check=True)


def main() -> None:
	parser = argparse.ArgumentParser()
	for arg in ("root", "build", "compiler", "config"):
		parser.add_argument(f"--{arg}", required=True)
	args = parser.parse_args()
	root, build = Path(args.root), Path(args.build)
	work = build / "consumer-test"
	if work.exists():
		shutil.rmtree(work)
	work.mkdir()
	stage = work / "original-prefix"
	run("cmake", "--install", str(build), "--prefix", str(stage))
	relocated = work / "relocated prefix with spaces"
	stage.rename(relocated)
	for path in relocated.rglob("*.cmake"):
		text = path.read_text()
		for original in (str(root), root.as_posix(), str(build), build.as_posix()):
			if original in text:
				raise RuntimeError(f"Installed metadata leaks original source/build path: {path}")
	# A distinct source/build tree and relocated prefix expose leaked build paths.
	consumer_source = work / "consumer source"
	shutil.copytree(root / "tests/consumer", consumer_source)
	for kind in ("installed", "source"):
		consumer_build = work / kind
		dependency = (
			f"-DCMAKE_PREFIX_PATH={relocated}"
			if kind == "installed"
			else f"-DSTANA_SOURCE={root}"
		)
		cache = (build / "CMakeCache.txt").read_text()
		sdk = next((line.split("=", 1)[1] for line in cache.splitlines()
			if line.startswith("CMAKE_OSX_SYSROOT:")), "")
		deployment_target = next((line.split("=", 1)[1] for line in cache.splitlines()
			if line.startswith("CMAKE_OSX_DEPLOYMENT_TARGET:")), "")
		configure = ["cmake", "-S", str(consumer_source), "-G", "Ninja",
			f"-DCMAKE_CXX_COMPILER={args.compiler}", f"-DCMAKE_BUILD_TYPE={args.config}",
			f"-DCMAKE_OSX_SYSROOT={sdk}", dependency]
		if sys.platform == "darwin":
			# A stale cache must be rejected by Stana, not merely emit native linker warnings.
			for target in ("15.0", ""):
				rejected = work / f"rejected-{kind}-{target or 'unset'}"
				result = subprocess.run([*configure, "-B", str(rejected),
					f"-DCMAKE_OSX_DEPLOYMENT_TARGET={target}"], capture_output=True, text=True)
				message = result.stdout + result.stderr
				if result.returncode == 0 or "requires a macOS deployment target" not in message:
					raise RuntimeError(f"Expected {kind} consumer target {target!r} rejection:\n{message}")
			run(*configure, "-B", str(consumer_build),
				f"-DCMAKE_OSX_DEPLOYMENT_TARGET={deployment_target}")
		else:
			run(*configure, "-B", str(consumer_build))
		run("cmake", "--build", str(consumer_build), "--parallel", "2")
		run("ctest", "--test-dir", str(consumer_build), "--output-on-failure")
		for target, name in (("old-canvas", "Canvas"), ("native_shader", "metalSource"), ("native_library", "metalLibrary")):
			rejected = subprocess.run(["cmake", "--build", str(consumer_build), "--target", f"rejected-{target}"], capture_output=True, text=True)
			if rejected.returncode == 0 or name not in rejected.stdout + rejected.stderr:
				raise RuntimeError(f"The forbidden {name} API must fail compilation")
		# Each consumer has already proved a clean rebuild. Reclaim its generated
		# BMIs before testing the next integration mode on space-limited machines.
		shutil.rmtree(consumer_build)


if __name__ == "__main__":
	main()
