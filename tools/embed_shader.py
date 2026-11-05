"""Deterministically embed tracked private shader source, with no runtime CWD lookup."""
import argparse
from pathlib import Path
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("source", type=Path)
parser.add_argument("output", type=Path)
args = parser.parse_args()
source = args.source.read_text(encoding="utf-8")
delimiter = "STANA_SHADER"
if f'){delimiter}"' in source:
    raise RuntimeError("Shader source contains the embedding delimiter")
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text('#ifndef STANA_EMBEDDED_SHADER_HPP\n#define STANA_EMBEDDED_SHADER_HPP\n'
    '#include <string_view>\nnamespace stana::platform::detail::gpu {\n'
    f'inline constexpr std::string_view builtinShader = R"{delimiter}({source}){delimiter}";\n'
    '}\n#endif\n', encoding="utf-8")
