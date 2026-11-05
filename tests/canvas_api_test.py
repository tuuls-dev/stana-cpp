"""Audit direct/transitive public shader declarations and complete consumer examples."""
from pathlib import Path
import re
import sys
root = Path(sys.argv[1])
for path in [*root.joinpath("modules").rglob("*.cppm"), *root.joinpath("examples").rglob("*.cpp")]:
    text = path.read_text()
    text = re.sub(r"//[^\n]*|/\*.*?\*/", "", text, flags=re.S)
    if re.search(r"\b(?:metalSource|metalLibrary|metallib|MTL\w+|HLSL|DXIL|SPIRV|BackendSelection)\b", text):
        raise RuntimeError(f"Native payload/selection leaked into public source: {path}")
    if path.parent.name == "graphics" and re.search(r"#\s*(?:if|ifdef|ifndef).*?(?:APPLE|WIN32|METAL|VULKAN)", text):
        raise RuntimeError(f"Platform-selected graphics API: {path}")
