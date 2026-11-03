"""Policy regression tests must fail for a named rule, not an unrelated error."""

import argparse
import importlib.util
import json
import os
from pathlib import Path
import shlex
import subprocess


CASES = {
	"raw-new": ("export auto rejected() -> int* { return new int{0}; }", "custom-no-raw-allocation"),
	"raw-delete": ("export auto rejected(int* ptr) -> void { delete ptr; }", "custom-no-raw-allocation"),
	"c-allocation": ("export auto rejected() -> void* { return std::malloc(sizeof(int)); }", "cppcoreguidelines-no-malloc"),
	"c-array": ("export auto rejected() -> int { int values[2]{0, 1}; return values[0]; }", "modernize-avoid-c-arrays"),
	"pointer-arithmetic": ("export auto rejected(int* ptr) -> int* { return ptr + 1; }", "cppcoreguidelines-pro-bounds-pointer-arithmetic"),
	"c-cast": ("export auto rejected(int value) -> long { return (long)value; }", "custom-no-c-style-cast"),
	"const-cast": ("export auto rejected(const int* ptr) -> int* { return const_cast<int*>(ptr); }", "cppcoreguidelines-pro-type-const-cast"),
	"reinterpret-cast": ("export auto rejected(int* ptr) -> char* { return reinterpret_cast<char*>(ptr); }", "cppcoreguidelines-pro-type-reinterpret-cast"),
	"downcast": ("export struct Base {}; export struct Derived : Base {}; export auto rejected(Base* ptr) -> Derived* { return static_cast<Derived*>(ptr); }", "cppcoreguidelines-pro-type-static-cast-downcast"),
	"union": ("export union Bits { int integer; float floating; }; export auto rejected(Bits bits) -> int { return bits.integer; }", "cppcoreguidelines-pro-type-union-access"),
	"varargs": ("export auto rejected(int value, ...) -> int { return value; }", "modernize-avoid-variadic-functions"),
	"unsafe-c-api": ("export auto rejected(char* target, const char* source) -> char* { return std::strcpy(target, source); }", "custom-no-unsafe-c-api"),
	"legacy-null": ("export auto rejected() -> int* { return 0; }", "modernize-use-nullptr"),
	"legacy-typedef": ("export typedef int OldAlias;", "modernize-use-using"),
	"narrowing": ("export auto rejected(long value) -> int { return value; }", "bugprone-narrowing-conversions"),
	"unchecked-access": ("export auto rejected(const std::vector<int>& values, std::size_t index) -> int { return values[index]; }", "cppcoreguidelines-pro-bounds-avoid-unchecked-container-access"),
}
MODERN = """
export auto accepted() -> std::expected<int, int> {
	const auto value = std::make_unique<int>(0);
	const std::vector<int> values{*value};
	return values.at(0);
}
export auto accepted_ranges(const std::vector<int>& values) -> bool {
	return std::ranges::all_of(values, [](int value) -> bool { return value >= 0; });
}
"""
PREFIX = """module;
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <memory>
#include <vector>
export module policy_fixture;
"""


def compiler_flags(build: Path) -> list[str]:
	database = json.loads((build / "compile_commands.json").read_text())
	entry = next(row for row in database if row["file"].endswith("stana.cppm"))
	command = entry.get("arguments") or shlex.split(entry["command"], posix=os.name != "nt")
	command = [arg.strip('"') for arg in command]
	flags = []
	skip = False
	for arg in command[1:]:
		if skip:
			skip = False
		elif arg in ("-o", "-MF", "-MT", "-MQ"):
			skip = True
		elif (arg in ("-c", "-MD", "-MMD", "/c", "-nologo") or arg == entry["file"]
			  or arg.startswith(("@", "-fmodule-output=", "/Fo", "/Fd", "/showIncludes"))):
			continue
		else:
			flags.append(arg)
	if os.name == "nt":
		flags.insert(0, "--driver-mode=cl")
	return flags


def main() -> None:
	parser = argparse.ArgumentParser()
	for arg in ("root", "build", "tidy", "compiler"):
		parser.add_argument(f"--{arg}", required=True)
	args = parser.parse_args()
	root, build = Path(args.root), Path(args.build)
	work = build / "policy-fixtures"
	work.mkdir(exist_ok=True)
	flags = compiler_flags(build)
	for name, (source, diagnostic) in {**CASES, "modern": (MODERN, "")}.items():
		for suffix in (".cppm", ".cpp"):
			# Check both exported interfaces and implementation-unit bodies.
			path = work / f"{name}{suffix}"
			if suffix == ".cppm":
				text = PREFIX + source
				extra = []
			else:
				text = PREFIX.replace("export module policy_fixture;", "module stana;")
				text += source.replace("export ", "")
				# Only a build-generated module map is allowed; no stale shared BMI cache.
				maps = list((build / "CMakeFiles/stana.dir/src").glob("version.cpp.*.modmap"))
				if len(maps) != 1:
					raise RuntimeError(f"Expected one implementation module map, found {maps}")
				extra = [f"@{maps[0]}"]
			path.write_text(text, encoding="utf-8")
			result = subprocess.run(
				[args.tidy, "--experimental-custom-checks", f"--config-file={root / '.clang-tidy'}",
				 str(path), "--", *flags, *extra],
				cwd=build, text=True, capture_output=True,
			)
			output = result.stdout + result.stderr
			if diagnostic:
				if result.returncode == 0 or diagnostic not in output:
					raise RuntimeError(f"{name}{suffix}: expected rejection by {diagnostic}\n{output}")
			elif result.returncode:
				raise RuntimeError(f"Modern alternatives must pass ({suffix}):\n{output}")
	spec = importlib.util.spec_from_file_location("stana_policy", root / "tools/policy.py")
	policy = importlib.util.module_from_spec(spec)
	spec.loader.exec_module(policy)
	suppressions = [
		"// NOLINT", "/* NOLINT */", "// NOLINTNEXTLINE(custom-no-raw-allocation)",
		"#pragma clang diagnostic ignored \"-Wconversion\"", "#pragma GCC diagnostic ignored \"-Wconversion\"",
		"#pragma warning(disable: 4996)", "_Pragma(\"clang diagnostic ignored\")",
		"__pragma(warning(disable: 4996))", "#pragma clang system_header",
		'[[gsl::suppress("type")]]', '__attribute__((no_sanitize("address")))',
		"#pragma clang /* gap */ diagnostic ignored \"-Wconversion\"",
		"#pragma clang diag\\\nnostic ignored \"-Wconversion\"",
	]
	for index, source in enumerate(suppressions):
		path = work / f"suppression-{index}.cppm"
		path.write_text(source, encoding="utf-8")
		if not policy.violations(path):
			raise RuntimeError(f"Suppression escaped policy: {source}")
	print(f"Passed {len(CASES) * 2} AST rejection cases, modern alternatives and suppression checks.")


if __name__ == "__main__":
	main()
