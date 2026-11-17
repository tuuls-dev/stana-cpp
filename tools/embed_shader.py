"""Zig build helper: bake a tracked shader file into a C++ header as a string_view.

Shaders are not loaded from disk at runtime (no CWD lookup). Two call sites:

  macOS library (`build_support/cpp.zig`)
    src/platform/macos/shaders/stana.metal
      -> generated stana_shader.hpp
      -> stana::platform::detail::gpu::builtinShader
    Used by the compositor's builtin Metal library.

  canvas example (`build_support/project.zig --canvas-template`)
    examples/canvas/field.wgsl + examples/canvas/resource.hpp.in
      -> generated canvas_resource.hpp
      -> canvas_example::shaderSource
    WGSL text is compiled by Tint at runtime; only the source is embedded.

Usage (Zig supplies the paths):
  python3 tools/embed_shader.py <source> <output> [--canvas-template header.in]
Fails if the shader contains the raw-string delimiter.
"""
import argparse
from pathlib import Path
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("source", type=Path)
parser.add_argument("output", type=Path)
parser.add_argument("--canvas-template", type=Path)
args = parser.parse_args()
source = args.source.read_text(encoding="utf-8")
delimiter = "STANA_APP" if args.canvas_template else "STANA_SHADER"
if f'){delimiter}"' in source:
    raise RuntimeError("Shader source contains the embedding delimiter")
args.output.parent.mkdir(parents=True, exist_ok=True)
if args.canvas_template:
    header = args.canvas_template.read_text(encoding="utf-8").replace("@stana_canvas_wgsl@", source)
else:
    header = ('#ifndef STANA_EMBEDDED_SHADER_HPP\n#define STANA_EMBEDDED_SHADER_HPP\n'
        '#include <string_view>\nnamespace stana::platform::detail::gpu {\n'
        f'inline constexpr std::string_view builtinShader = R"{delimiter}({source}){delimiter}";\n'
        '}\n#endif\n')
args.output.write_text(header, encoding="utf-8")
