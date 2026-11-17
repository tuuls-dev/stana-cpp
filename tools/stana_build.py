"""Host toolchain discovery and small command adapters for Stana's Zig build."""

import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import subprocess
import sys
import sysconfig
import tempfile

LLVM_VERSION = "22.1.8"
ICU_VERSION = "78.3"
TINT_REVISION = "cb91d23e247042a2a5490c27d9b06300db91afef"
WARNINGS = """-Wall -Wextra -Wpedantic -Werror -pedantic-errors
-Wconversion -Wsign-conversion -Wshadow-all -Wold-style-cast -Wcast-align
-Wcast-qual -Wformat=2 -Wundef -Wdouble-promotion -Wimplicit-fallthrough
-Wnon-virtual-dtor -Woverloaded-virtual -Wextra-semi -Wzero-as-null-pointer-constant
-Wdeprecated -Wdeprecated-declarations -Wdeprecated-copy-dtor
-Wnull-dereference -Wunreachable-code-aggressive -Wswitch-enum
-Wcovered-switch-default -Wmissing-prototypes -Wmissing-noreturn
-Wimplicit-int-conversion -Wshorten-64-to-32 -Wfloat-equal
-Wthread-safety -Wdangling -Wunsafe-buffer-usage""".split()


def llvm_tool(root: Path, name: str) -> Path:
	if os.name == "nt":
		name += ".exe"
	override = os.environ.get("LLVM_ROOT")
	if override:
		candidates = [Path(override).absolute() / "bin" / name]
	else:
		candidates = [root / ".tools/llvm/bin" / name]
		if sys.platform == "darwin":
			candidates += [Path(prefix) / "opt/llvm@22/bin" / name
				for prefix in ("/opt/homebrew", "/usr/local")]
		if found := shutil.which(name):
			candidates.append(Path(found).absolute())
	for path in candidates:
		if path.is_file():
			version = subprocess.check_output([str(path), "--version"], text=True, stderr=subprocess.STDOUT)
			if re.search(rf"version {re.escape(LLVM_VERSION)}(?![.0-9])", version):
				# Preserve the driver name, which controls the runtime and linker defaults.
				return path
	raise RuntimeError(f"{name} {LLVM_VERSION} is required. Set LLVM_ROOT to its installation prefix.")


def find_icu(root: Path) -> tuple[Path, list[str], str]:
	override = os.environ.get("ICU_ROOT")
	roots = [Path(override).absolute()] if override else [root / ".tools/icu"]
	if not override and sys.platform == "darwin":
		roots += [Path(prefix) / "opt/icu4c@78" for prefix in ("/opt/homebrew", "/usr/local")]
	if not override and os.name != "nt":
		roots += [Path("/usr/local"), Path("/usr")]
	for prefix in roots:
		include = prefix / "include"
		header = include / "unicode/uvernum.h"
		if not header.is_file():
			continue
		if not re.search(rf'^#\s*define\s+U_ICU_VERSION\s+"{re.escape(ICU_VERSION)}"', header.read_text(), re.M):
			continue
		if os.name == "nt":
			names = [("icuin.lib",), ("icuuc.lib",), ("icudt.lib", "icudata.lib")]
			dirs = [prefix / sub for sub in ("lib64", "lib")]
		else:
			suffix = ".dylib" if sys.platform == "darwin" else ".so"
			names = [(f"libicu{part}{suffix}", f"libicu{part}.a") for part in ("i18n", "uc", "data")]
			dirs = [prefix / "lib", prefix / "lib64"]
			if multiarch := sysconfig.get_config_var("MULTIARCH"):
				dirs.append(prefix / "lib" / multiarch)
		libraries = [next((str(directory / name) for name in group for directory in dirs
			if (directory / name).is_file()), "") for group in names]
		if all(libraries):
			bin_dir = str(next((prefix / sub for sub in ("bin64", "bin") if (prefix / sub).is_dir()),
				prefix / "bin")) if os.name == "nt" else ""
			return include, libraries, bin_dir
	raise RuntimeError(f"ICU4C {ICU_VERSION} (i18n, uc, data) is required. Set ICU_ROOT to its installation prefix.")


def toolchain(root: str, configuration: str, sanitizer: str, deployment: str,
		checks: str, installed: str) -> None:
	if sys.version_info < (3, 11):
		raise RuntimeError("Stana requires Python 3.11 or newer")
	root = Path(root).resolve()
	system = platform.system()
	machine = platform.machine().lower()
	arch = {"amd64": "x86_64", "aarch64": "arm64"}.get(machine, machine)
	if system not in ("Darwin", "Linux", "Windows") or (system == "Windows" and arch != "x86_64"):
		raise RuntimeError(f"Unsupported native Stana host {system}/{arch}")
	cxx = llvm_tool(root, "clang++")
	tidy = llvm_tool(root, "clang-tidy") if checks == "true" else ""
	scanner = llvm_tool(root, "clang-scan-deps") if checks == "true" and installed == "false" else ""
	include, icu_libs, icu_bin = find_icu(root)
	common = ["-std=c++23", "-fexceptions", "-frtti"]
	link = []
	release = configuration == "Release"
	if system == "Darwin":
		if not re.fullmatch(r"\d+(\.\d+){0,2}", deployment) or int(deployment.split(".")[0]) < 26:
			raise RuntimeError("Stana requires a macOS deployment target of 26.0 or newer")
		sdk = os.environ.get("SDKROOT") or subprocess.check_output(
			["xcrun", "--sdk", "macosx", "--show-sdk-path"], text=True).strip()
		sdk_flags = ["-isysroot", sdk, f"-mmacosx-version-min={deployment}"]
		common += sdk_flags + ["-nostdinc++", "-isystem", str(Path(sdk) / "usr/include/c++/v1"),
			"-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE"]
		link += sdk_flags
	elif system == "Windows":
		if sanitizer == "address-undefined":
			raise RuntimeError("Windows supports the address sanitizer only")
		if not os.environ.get("INCLUDE") or not os.environ.get("LIB"):
			raise RuntimeError("Run Zig in a Visual Studio x64 developer shell")
		windows_flags = ["--target=x86_64-pc-windows-msvc",
			"-fms-runtime-lib=" + ("dll" if release else "dll_dbg")]
		common += windows_flags + ["-D_MSVC_STL_HARDENING=1"]
		link += windows_flags
	else:
		user_flags = shlex.split(os.environ.get("CXXFLAGS", ""))
		common += user_flags + ["-pthread", "-D_GLIBCXX_ASSERTIONS=1"]
		link += user_flags + ["-pthread"]
		if release:
			common.append("-D_FORTIFY_SOURCE=3")
	common += (["-O3", "-DNDEBUG"] if release else ["-O0", "-g"])
	common += WARNINGS + ["-fstack-protector-strong"]
	if system == "Windows":
		common += ["-WCL4"]
	if sanitizer != "none":
		instrumentation = "-fsanitize=" + ("address,undefined" if sanitizer == "address-undefined" else "address")
		common += [instrumentation, "-fno-omit-frame-pointer", "-fno-sanitize-recover=all"]
		link += [instrumentation, "-fno-sanitize-recover=all"]
		if system == "Windows":
			link += ["-Xlinker", "/INCREMENTAL:NO", "-shared-libasan"]

	contract = {
		"Darwin": "defined(_LIBCPP_VERSION) && defined(_LIBCPP_HARDENING_MODE_EXTENSIVE) && _LIBCPP_HARDENING_MODE == _LIBCPP_HARDENING_MODE_EXTENSIVE",
		"Linux": "defined(_GLIBCXX_RELEASE) && _GLIBCXX_RELEASE >= 14 && defined(_GLIBCXX_ASSERTIONS)",
		"Windows": "defined(_MSVC_STL_VERSION) && defined(_MSVC_STL_HARDENING_VECTOR)",
	}[system]
	source = ('#include <version>\n#include <vector>\n'
		f'#if !({contract}) || !defined(__cpp_exceptions) || !defined(__cpp_rtti)\n'
		'#error Stana requires the platform STL with hardening, RTTI and exceptions\n#endif\n')
	macros = subprocess.check_output([str(cxx), *common, "-dM", "-E", "-x", "c++", "-"],
		input=source, text=True)
	stl = dict(re.findall(
		r"^#define (_LIBCPP_VERSION|_LIBCPP_ABI_VERSION|__GLIBCXX__|_GLIBCXX_RELEASE|"
		r"_GLIBCXX_USE_CXX11_ABI|_MSVC_STL_VERSION|_MSVC_STL_UPDATE|_MSC_VER|_ITERATOR_DEBUG_LEVEL) (.+)$",
		macros, re.M))
	metadata = {"configuration": configuration, "system": system, "architecture": arch,
		"system_version": platform.release(), "compiler": "Clang", "compiler_version": LLVM_VERSION,
		"compiler_frontend": "GNU", "macos_deployment_target": deployment if system == "Darwin" else "",
		"cxx_standard": 23, "private_shader_compiler_revision": TINT_REVISION, "sanitizer": sanitizer,
		"standard_library_macros": stl, "application_shader_contract": "WGSL and logical group/binding coordinates",
		"bmi_policy": "Rebuilt from installed module interfaces by each consumer"}
	archive = "stana-tint.lib" if system == "Windows" else "libstana-tint.a"
	if installed == "true":
		previous = read_json(root / "share/stana/build-info.json")
		for key in ("system", "architecture", "compiler_version", "configuration", "sanitizer",
				"private_shader_compiler_revision"):
			if previous[key] != metadata[key]:
				raise RuntimeError(f"Installed Stana SDK requires {key} {previous[key]!r}, found {metadata[key]!r}")
		abi_macros = {
			"Darwin": ("_LIBCPP_ABI_VERSION",),
			"Linux": ("_GLIBCXX_USE_CXX11_ABI",),
			"Windows": ("_MSVC_STL_VERSION", "_ITERATOR_DEBUG_LEVEL"),
		}[system]
		for name in abi_macros:
			if previous["standard_library_macros"].get(name) != stl.get(name):
				raise RuntimeError(f"Installed Stana SDK requires matching {name}")
		if system == "Darwin":
			minimum = previous["macos_deployment_target"]
			if tuple(map(int, (deployment + ".0.0").split(".")[:3])) < tuple(map(int, (minimum + ".0.0").split(".")[:3])):
				raise RuntimeError(f"Installed Stana SDK requires macOS {minimum} or newer")
		tint = root
		tint_manifest = root / "share/stana/stana-tint.json"
	else:
		suffix = "" if sanitizer == "none" else f"-{sanitizer}"
		tint = Path(os.environ.get("STANA_TINT_ROOT", root / f".tools/tint{suffix}")).absolute()
		tint_manifest = tint / "stana-tint.json"
		if not tint_manifest.is_file():
			raise RuntimeError("Provision Tint with tools/bootstrap_tint.py or set STANA_TINT_ROOT")
	tint_info = read_json(tint_manifest)
	if tint_info.get("revision") != TINT_REVISION or tint_info.get("sanitizer") != sanitizer.replace("none", ""):
		raise RuntimeError("Tint revision or sanitizer does not match Stana")
	if tint_info.get("system") != system or tint_info.get("architecture", "").lower() != machine:
		raise RuntimeError("Tint was provisioned for another host")
	if not re.search(rf"version {re.escape(LLVM_VERSION)}(?![.0-9])", tint_info.get("compiler", "")):
		raise RuntimeError(f"Provision Tint with LLVM {LLVM_VERSION}")
	if system == "Windows" and tint_info.get("config") != configuration:
		raise RuntimeError(f"Tint must use the Windows {configuration} configuration and CRT")
	if not (tint / "lib" / archive).is_file():
		raise RuntimeError(f"Missing Tint archive: {tint / 'lib' / archive}")
	if icu_bin:
		icu_bin += os.pathsep + os.environ.get("PATH", "")
	if sanitizer != "none" and system == "Windows":
		resource = subprocess.check_output([str(cxx), "-print-resource-dir"], text=True).strip()
		icu_bin = os.pathsep.join([str(cxx.parent), str(Path(resource) / "lib/windows"), icu_bin])
	print(json.dumps({"cxx": str(cxx), "tidy": str(tidy), "scanner": str(scanner),
		"compile_flags": common, "link_flags": link, "icu_include": str(include), "icu_libs": icu_libs,
		"runtime_path": icu_bin, "tint_include": str(tint / "include"),
		"tint_archive": str(tint / "lib" / archive), "tint_manifest": str(tint_manifest),
		"tint_licenses": str(tint / "licenses"), "metadata": json.dumps(metadata)}))


def read_json(path: str | Path):
	return json.loads(Path(path).read_text(encoding="utf-8"))


def write_json(path: str, data) -> None:
	Path(path).write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def compile_unit(config: str, record: str, tidy_config: str, source: str, output: str,
		depfile: str, *extra: str) -> None:
	tools = read_json(config)
	flags = tools["compile_flags"]
	if not tidy_config:
		# Match the module's hardening and sanitizer modes while leaving warning policy to the application.
		private = {*WARNINGS, "-WCL4"}
		flags = [flag for flag in flags if flag not in private]
	flags += extra
	if source.endswith(".mm") and "-fobjc-arc" not in flags:
		flags.append("-fobjc-arc")
	precompile = "--precompile" in flags
	command = [tools["cxx"], *flags, "-MD", "-MF", depfile, source, "-o", output]
	if not precompile:
		command.append("-c")
	subprocess.run(command, check=True)
	arguments = [flag for flag in flags if flag != "--precompile" and not flag.startswith("-fmodule-output=")]
	if tidy_config and tools["tidy"]:
		result = subprocess.run([tools["tidy"], "--experimental-custom-checks", f"--config-file={tidy_config}",
			source, "--", *arguments], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
		if result.returncode:
			sys.stderr.buffer.write(result.stdout)
			sys.exit(result.returncode)
	write_json(record, {"directory": os.getcwd(), "file": source, "output": output,
		"arguments": command, "flags": arguments})


def link(config: str, output: str, *arguments: str) -> None:
	separator = arguments.index("--")
	inputs, extra = arguments[:separator], arguments[separator + 1:]
	tools = read_json(config)
	flags = tools["link_flags"]
	if sys.platform == "darwin":
		for framework in ("AppKit", "UniformTypeIdentifiers", "ApplicationServices", "CoreGraphics",
				"CoreText", "Metal", "QuartzCore", "Foundation"):
			flags += ["-framework", framework]
	elif sys.platform == "linux":
		# An explicitly provisioned shared ICU must also be discoverable at runtime.
		dirs = sorted({str(Path(lib).parent) for lib in tools["icu_libs"] if lib.endswith(".so")})
		for directory in dirs:
			flags += ["-Xlinker", "-rpath", "-Xlinker", directory]
	subprocess.run([tools["cxx"], *inputs, *flags, *extra, "-o", output], check=True)


def verify_modules(scanner: str, output: str, *records: str) -> None:
	# P1689 reports the direct dependencies. Zig's graph also carries transitive BMIs.
	entries = [read_json(record) for record in records]
	count = len(entries) // 2
	database = entries[:count]
	expected = entries[count:]
	for entry in database:
		entry["arguments"] = [entry["arguments"][0], *[flag for flag in entry.pop("flags")
			if not flag.startswith("-fmodule-file=")], "-c", entry["file"], "-o", entry["output"]]
	with tempfile.TemporaryDirectory(prefix="stana-scan-") as work:
		path = str(Path(work) / "compile_commands.json")
		write_json(path, database)
		rules = json.loads(subprocess.check_output([scanner, "-format=p1689", "-j", "3",
			"-resource-dir-recipe", "invoke-compiler", "-compilation-database", path], text=True))["rules"]
	by_output = {os.path.normcase(os.path.normpath(rule["primary-output"])): rule for rule in rules}
	for entry, declared in zip(database, expected, strict=True):
		rule = by_output[os.path.normcase(os.path.normpath(entry["output"]))]
		for key in ("provides", "requires"):
			names = sorted(item["logical-name"] for item in rule.get(key, []))
			if names != sorted(declared[key]):
				raise RuntimeError(f'{entry["file"]} {key} {names}, Zig declares {declared[key]}')
	Path(output).touch()


def tidy_probe(compiler: str, config: str, output: str) -> None:
	with tempfile.TemporaryDirectory(prefix="stana-tidy-") as work:
		work = Path(work)
		verification = work / "verify-tidy.yaml"
		verification.write_text(Path(config).read_text().replace("  custom-*", ""), encoding="utf-8")
		subprocess.run([compiler, "--experimental-custom-checks", f"--config-file={verification}",
			"--verify-config"], check=True)
		source = work / "tidy-probe.cppm"
		source.write_text("export module stana_toolchain_probe;\n"
			"export auto rejected() -> int* { return new int((int)1); }\n", encoding="utf-8")
		result = subprocess.run([compiler, "--experimental-custom-checks", f"--config-file={config}",
			str(source), "--", "-std=c++23"], text=True, capture_output=True)
		diagnostics = result.stdout + result.stderr
		if not result.returncode or not all(name in diagnostics for name in
				("custom-no-raw-allocation", "custom-no-c-style-cast")):
			raise RuntimeError(f"Required clang-tidy AST checks are unavailable\n{diagnostics}")
	Path(output).touch()


def compdb(output: str, *entries: str) -> None:
	database = []
	for path in entries:
		entry = read_json(path)
		entry.pop("flags")
		# Zig moves a completed Run's output directory from tmp to its cache.
		temporary = Path(entry["output"]).parent
		directory = Path(path).parent
		entry["output"] = str(directory / Path(entry["output"]).name)
		for index, arg in enumerate(entry["arguments"]):
			prefix, value = ("-fmodule-output=", arg.split("=", 1)[1]) if arg.startswith("-fmodule-output=") else ("", arg)
			if Path(value).is_absolute() and Path(value).parent == temporary:
				entry["arguments"][index] = prefix + str(directory / Path(value).name)
		database.append(entry)
	write_json(output, database)


def main() -> None:
	cmd, *args = sys.argv[1:]
	try:
		if cmd == "compile":
			compile_unit(*args)
		elif cmd == "toolchain":
			toolchain(*args)
		elif cmd == "link":
			link(*args)
		elif cmd == "compdb":
			compdb(*args)
		elif cmd == "verify-modules":
			verify_modules(*args)
		elif cmd == "tidy-probe":
			tidy_probe(*args)
		elif cmd == "run":
			timeout, *command = args
			sys.exit(subprocess.run(command, timeout=int(timeout)).returncode)
		else:
			raise RuntimeError(f"Unknown Stana build command: {cmd}")
	except (OSError, RuntimeError, subprocess.SubprocessError) as error:
		sys.exit(f"stana: {error}")


if __name__ == "__main__":
	main()
