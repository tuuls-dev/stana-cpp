"""Fail closed on first-party source-level diagnostic/analysis bypasses."""

from pathlib import Path
import re
import sys

SOURCE_SUFFIXES = {".cpp", ".cppm", ".cxx", ".cc", ".h", ".hpp", ".hxx", ".ixx", ".mm", ".m"}
FORBIDDEN = {
	"lint suppression": re.compile(r"\bNOLINT(?:NEXTLINE|BEGIN|END)?\b"),
	"diagnostic pragma": re.compile(r"\b(?:clang|GCC)\s+diagnostic\b|\bwarning\s*\("),
	"pragma operator": re.compile(r"\b(?:_Pragma|__pragma)\s*\("),
	"system-header reclassification": re.compile(r"\bsystem_header\b"),
	"analysis/sanitizer suppression": re.compile(
		r"\b(?:no_sanitize\w*|disable_sanitizer_instrumentation|suppress|analyzer_noreturn)\b"
	),
}


def violations(path: Path) -> list[str]:
	# Checking comments as well is intentional: suppression comments affect tooling.
	# Join escaped physical lines so split pragma spellings cannot bypass the scan.
	text = re.sub(r"\\\r?\n", "", path.read_text(encoding="utf-8"))
	without_comments = re.sub(r"/\*.*?\*/", lambda m: " " + "\n" * m[0].count("\n"), text, flags=re.S)
	return [
		f"{path}:{candidate.count(chr(10), 0, match.start()) + 1}: forbidden {name}"
		for name, pattern in FORBIDDEN.items()
		for candidate in [text if name == "lint suppression" else without_comments]
		for match in pattern.finditer(candidate)
	]


def check(root: Path) -> list[str]:
	return [
		message
		for folder in (root / "modules", root / "src", root / "tests", root / "examples")
		for path in sorted(folder.rglob("*"))
		if path.suffix in SOURCE_SUFFIXES
		for message in violations(path)
	]


if __name__ == "__main__":
	errors = check(Path(sys.argv[1]).resolve())
	if errors:
		print("\n".join(errors), file=sys.stderr)
		sys.exit(1)
