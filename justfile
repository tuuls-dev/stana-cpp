# Developer entry points. LLVM 22.1.8 must already exist at .tools/llvm.
set windows-shell := ["powershell.exe", "-NoLogo", "-Command"]
python := if os() == "windows" { "python" } else { "python3" }

jobs := "2"

default:
    @just --list

# Compile and stage the SDK without running analysis or tests.
build preset="debug": (zig-step "install" preset)

examples preset="debug": (zig-step "examples" preset)

test preset="debug": (zig-step "test" preset)

# Select window tests by default, or pass native-offscreen-tests for Metal only.
test-native preset="debug" mode="native-tests":
    just --set jobs {{quote(jobs)}} zig-step test-native {{quote(preset)}} -D{{mode}}=true

audit: (zig-step "audit" "debug")

analyze preset="debug": (zig-step "analyze" preset)

format: (zig-step "format" "debug")

format-check: (zig-step "format-check" "debug")

compdb preset="debug": (zig-step "compdb" preset)

# Run the complete required verification for the selected preset.
verify preset="debug": (zig-step "verify" preset)

# Stage and archive only. Run verify separately before releasing.
package preset="release": (zig-step "package" preset)

# Provision ICU or Tint. This tooling never downloads LLVM.
provision dependency *args:
    {{python}} {{ if dependency == "icu" { "tools/bootstrap_icu.py" } else if dependency == "tint" { "tools/bootstrap_tint.py" } else { error("Choose icu or tint; install LLVM externally at .tools/llvm") } }} --jobs {{quote(jobs)}} {{args}}

release-source tag output="dist":
    {{python}} tools/release.py source --tag {{quote(tag)}} --output {{quote(output)}}

release-checksums output="dist":
    {{python}} tools/release.py checksums --output {{quote(output)}}

example name trace="--trace":
    zig build {{quote("run-" + name)}} -j{{jobs}} {{ if trace == "--no-trace" { "-Dtrace=false" } else if trace == "--trace" { "-Dtrace=true" } else { error("Choose --trace or --no-trace") } }}

[private]
zig-step step preset *args:
    zig build {{step}} -j{{jobs}} {{ if preset == "release" { "--release=fast" } else if preset == "asan" { "-Dsanitizer=address" } else if preset == "asan-ubsan" { "-Dsanitizer=address-undefined" } else if preset == "debug" { "-Doptimize=debug" } else { error("Choose debug, release, asan, or asan-ubsan") } }} {{args}}
