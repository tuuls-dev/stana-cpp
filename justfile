# Developer entry points. Recipes run from this file's directory.
set windows-shell := ["powershell.exe", "-NoLogo", "-Command"]
python := if os() == "windows" { "python" } else { "python3" }

# List available commands.
default:
    @just --list

# Format first-party sources using the pinned formatter.
format:
    {{python}} tools/format.py --write

# Check formatting without changing files.
format-check:
    {{python}} tools/format.py --check

# Build with mandatory source analysis.
build preset="debug": (zig-step "install" preset)

# Build and run the contracts.
test preset="debug": (zig-step "test" preset)

[private]
zig-step step preset:
    zig build {{step}} -j3 {{ if preset == "release" { "--release=fast" } else if preset == "asan" { "-Dsanitizer=address" } else if preset == "asan-ubsan" { "-Dsanitizer=address-undefined" } else { "-Doptimize=" + quote(preset) } }}

# Launch an existing example. Native rendering traces are on by default.
example name trace="--trace":
    zig build {{quote("run-" + name)}} -j3 -Dchecks=false {{ if trace == "--no-trace" { "-Dtrace=false" } else if trace == "--trace" { "-Dtrace=true" } else { quote(trace) } }}
