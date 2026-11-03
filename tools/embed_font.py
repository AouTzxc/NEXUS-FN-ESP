"""Emit a C++ array with the gzip-compressed bundled TTF."""
from pathlib import Path
import gzip

root = Path(__file__).resolve().parents[1]
src = root / "assets/fonts/NotoSansSC-UI-Subset.ttf"
dst = root / "jni/src/rebootnova/embedded_font.cpp"
data = gzip.compress(src.read_bytes(), compresslevel=9)
lines = [
    "extern \"C\" const unsigned char rebootnova_embedded_font_gz[] = {",
]
chunk = []
for index, byte in enumerate(data):
    chunk.append(f"0x{byte:02x}")
    if len(chunk) == 16:
        lines.append("    " + ", ".join(chunk) + ",")
        chunk = []
if chunk:
    lines.append("    " + ", ".join(chunk) + ",")
lines.append("};")
lines.append(f"extern \"C\" const unsigned int rebootnova_embedded_font_gz_size = {len(data)};")
lines.append("")
dst.write_text("\n".join(lines), encoding="ascii")
print(f"EMBED_FONT gzip={len(data)} path={dst}")
