# Developer entry points. Recipes run from this file's directory.
set windows-shell := ["powershell.exe", "-NoLogo", "-Command"]
python := if os() == "windows" { "python" } else { "python3" }

# List available commands.
default:
    @just --list

# Format every first-party C++ module, implementation, native adapter and example.
format:
    {{python}} tools/format.py --write

# Check formatting without changing files.
format-check:
    {{python}} tools/format.py --check

# Configure the selected CMake preset (debug by default).
configure preset="debug":
    cmake --preset {{quote(preset)}}

# Configure and build the selected preset with mandatory source analysis.
build preset="debug": (configure preset)
    cmake --build --preset {{quote(preset)}} --parallel 3

# Build and run the selected preset's headless tests.
test preset="debug": (build preset)
    ctest --preset {{quote(preset)}}

# Build and run an example: just example counter, or just example symbols.
# Native rendering traces are on by default; pass --no-trace to disable.
example name *args:
    {{python}} tools/run_example.py {{quote(name)}} {{args}}
