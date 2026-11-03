from __future__ import annotations

import argparse
import hashlib
import re
from pathlib import Path

from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont


TEXT_PATHS = (
    "jni/include/rebootnova",
    "jni/src/rebootnova",
    "third_party/surface_imgui/jni/src/Android_my_imgui/my_imgui.cpp",
)

OFFICIAL_SIMPLIFIED_RANGES_SOURCE = Path(
    "third_party/surface_imgui/jni/src/Android_my_imgui/my_imgui.cpp"
)


def collect_official_simplified_codepoints(root: Path) -> set[int]:
    """Read the same official Simplified Chinese glyph set used by ImGui."""
    source = (root / OFFICIAL_SIMPLIFIED_RANGES_SOURCE).read_text(encoding="utf-8")
    start = source.index("const ImWchar *GetGlyphRangesChineseSimplifiedOfficial()")
    end = source.index("const ImWchar *GetGlyphRangesChineseTraditionalOfficial()", start)
    section = source[start:end]
    match = re.search(
        r"accumulative_offsets_from_0x4E00\[\]\s*=\s*\{(.*?)\};",
        section,
        re.DOTALL,
    )
    if not match:
        raise RuntimeError("official Simplified Chinese glyph table not found")

    codepoints: set[int] = set()
    value = 0x4E00
    for offset in re.findall(r"\b\d+\b", match.group(1)):
        value += int(offset)
        codepoints.add(value)
    if len(codepoints) < 7000:
        raise RuntimeError(f"official Simplified Chinese glyph table is unexpectedly short: {len(codepoints)}")
    return codepoints


def collect_codepoints(root: Path) -> set[int]:
    codepoints = set(range(0x20, 0x7F))
    codepoints.update(range(0xA0, 0x100))
    codepoints.update(ord(c) for c in "…、。，！？：（）【】《》“”‘’—·™")
    for relative in TEXT_PATHS:
        path = root / relative
        files = path.rglob("*") if path.is_dir() else (path,)
        for file in files:
            if not file.is_file() or file.suffix.lower() not in {".h", ".hpp", ".c", ".cpp"}:
                continue
            codepoints.update(
                ord(c) for c in file.read_text(encoding="utf-8-sig")
                if ord(c) >= 0x100 and ord(c) != 0xFEFF
            )
    # Dynamic player names cannot be derived from static source strings. Keep
    # this in sync with the runtime range to render standard Chinese nicknames
    # without constructing the much larger full-CJK atlas.
    codepoints.update(collect_official_simplified_codepoints(root))
    # The runtime atlas also requests Japanese punctuation, Hiragana,
    # Katakana and half-width forms. They must exist in the subset itself or
    # valid scoreboard names render as fallback question marks.
    codepoints.update(range(0x3000, 0x3040))
    codepoints.update(range(0x3041, 0x3097))
    codepoints.update(range(0x3099, 0x3100))
    codepoints.update(range(0x31F0, 0x3200))
    codepoints.update(range(0xFF01, 0xFFA0))
    codepoints.update(range(0xFFE0, 0xFFE7))
    return codepoints


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()

    codepoints = collect_codepoints(args.root)
    font = TTFont(args.input)
    if "fvar" in font:
        # The variable source defaults to Thin (wght=100), which reads poorly
        # on a transparent overlay. Bake a medium static face for stable UI.
        font = instantiateVariableFont(font, {"wght": 500}, inplace=False, optimize=True)

    options = subset.Options()
    options.hinting = False
    options.layout_features = ["*"]
    options.name_IDs = [0, 1, 2, 3, 4, 5, 6]
    options.name_legacy = True
    options.name_languages = [0x409, 0x804]
    options.recalc_bounds = True
    options.recalc_timestamp = False
    options.canonical_order = True
    subsetter = subset.Subsetter(options=options)
    subsetter.populate(unicodes=codepoints)
    subsetter.subset(font)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    font.save(args.output, reorderTables=True)
    output_font = TTFont(args.output)
    output_codepoints = set(output_font.getBestCmap())
    missing = sorted(codepoints - output_codepoints)
    output_font.close()
    if missing:
        formatted = " ".join(f"U+{value:04X}" for value in missing)
        raise RuntimeError(f"subset font is missing requested glyphs: {formatted}")
    digest = hashlib.sha256(args.output.read_bytes()).hexdigest().upper()
    print(f"SUBSET_FONT={args.output} GLYPHS={len(codepoints)} SIZE={args.output.stat().st_size} SHA256={digest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
