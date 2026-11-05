"""Check functional namespace ownership and the single public module convention."""

from pathlib import Path
import re
import sys

SOURCE_SUFFIXES = {".cpp", ".cppm", ".hpp", ".h", ".mm"}
ROOT_HELPERS = {"nextIdentity", "requireFinite", "requireNonnegative"}
RETIRED = {"application_detail", "graphics_detail", "shader_detail"}
LEXER = re.compile(
    r'(?P<skip>//[^\n]*|/\*[\s\S]*?\*/|R"(?P<delimiter>[^ ()\\\t\r\n]{0,16})'
    r'\([\s\S]*?\)(?P=delimiter)"|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\')'
    r'|(?P<token>::|[A-Za-z_]\w*|[^\s])'
)


def tokens(source: str) -> list[str]:
    return [match["token"] for match in LEXER.finditer(source) if match["token"]]


def root_helper_declaration(declaration: list[str]) -> bool:
    # Ignore attributes before locating the function declarator's first `(`.
    # Looking for a helper call anywhere in the signature would also accept an
    # unrelated function mentioning nextIdentity in decltype/noexcept.
    signature = []
    attribute_depth = 0
    index = 0
    while index < len(declaration):
        pair = declaration[index:index + 2]
        if pair == ["[", "["]:
            attribute_depth += 1
            index += 2
        elif pair == ["]", "]"] and attribute_depth:
            attribute_depth -= 1
            index += 2
        else:
            if not attribute_depth:
                signature.append(declaration[index])
            index += 1
    if "(" not in signature or "=" in signature:
        return False
    position = signature.index("(")
    return position > 0 and signature[position - 1] in ROOT_HELPERS and not any(
        word in signature for word in ("class", "struct", "enum", "using", "typedef")
    )


def violations(source: str) -> list[str]:
    """Track lexical namespace scopes, including nested and qualified spellings."""
    words = tokens(source)
    errors = []
    scopes = []
    current = ()
    pending = []
    index = 0
    while index < len(words):
        word = words[index]
        if word in RETIRED:
            errors.append(f"retired namespace identifier: {word}")
        if words[index:index + 3] in (["stana", "::", "native"], ["stana", "::", "fixtures"]):
            errors.append("retired namespace qualification")
        if word == "module" and index + 1 < len(words) and words[index + 1] not in {";", "stana"}:
            errors.append("stana is the only first-party named module")
        if word in {"module", "import"} and words[index + 1:index + 2] == ["stana"]:
            if words[index + 2:index + 3] == ["."]:
                errors.append("component named modules are retired; use module stana or its partitions")
        if word == "namespace" and (index == 0 or words[index - 1] != "using"):
            cursor = index + 1
            names = []
            while cursor < len(words) and words[cursor] not in {"{", "=", ";"}:
                if words[cursor] != "::":
                    names.append(words[cursor])
                cursor += 1
            if cursor < len(words) and words[cursor] == "{":
                next_namespace = current + tuple(names)
                if RETIRED.intersection(names):
                    errors.append("retired namespace identifier")
                if next_namespace[:2] in (("stana", "native"), ("stana", "fixtures")):
                    errors.append("retired nested namespace")
                if next_namespace[:2] == ("stana", "detail") and len(next_namespace) > 2:
                    errors.append("root detail may not contain subsystem namespaces")
                scopes.append((current, True))
                current = next_namespace
                pending = []
                index = cursor + 1
                continue
            # Namespace aliases must not recreate retired names.
            if names and names[0] in {"native", "fixtures"} and current == ("stana",):
                errors.append("retired namespace alias")
            if current + tuple(names) == ("stana", "detail") and cursor < len(words) and words[cursor] == "=":
                errors.append("root detail may not be a namespace alias")
        direct_root_detail = current == ("stana", "detail") and (not scopes or scopes[-1][1])
        if word in {"{", ";"} and direct_root_detail and pending:
            if not root_helper_declaration(pending):
                errors.append("root detail declaration is not an allowlisted primitive helper")
        if word == "{":
            scopes.append((current, False))
            pending = []
        elif word == "}":
            if scopes:
                current, _ = scopes.pop()
            pending = []
        elif word == ";":
            pending = []
        else:
            pending.append(word)
        index += 1
    return sorted(set(errors))


def check(root: Path) -> list[str]:
    errors = []
    primary_interfaces = []
    for folder in ("modules", "src", "tests", "examples"):
        for path in sorted((root / folder).rglob("*")):
            if path.suffix not in SOURCE_SUFFIXES:
                continue
            source = path.read_text(encoding="utf-8")
            errors.extend(f"{path}: {message}" for message in violations(source))
            words = tokens(source)
            if any(words[i:i + 4] == ["export", "module", "stana", ";"] for i in range(len(words))):
                primary_interfaces.append(path)
    if primary_interfaces != [root / "modules/stana.cppm"]:
        errors.append("expected exactly one primary public module at modules/stana.cppm")
    return errors


if __name__ == "__main__":
    messages = check(Path(sys.argv[1]).resolve())
    if messages:
        print("\n".join(messages), file=sys.stderr)
        sys.exit(1)
