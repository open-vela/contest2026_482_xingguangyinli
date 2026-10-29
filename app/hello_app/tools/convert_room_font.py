#!/usr/bin/env python3
"""Reproduce the embedded room font without depending on a system Chinese font.

Source: notofonts/noto-cjk/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf
Converter: lv_font_conv 1.5.3 (install in a temporary directory, not this repo).
Usage: python3 convert_room_font.py --font SOURCE.otf --converter lv_font_conv.js
No network/downloads are performed by this script. Keep Noto-OFL.txt with output.
"""
import argparse
import hashlib
from pathlib import Path
import subprocess

SOURCE_SHA256 = "2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b"
SYMBOLS = "￥¥，。？！：；、（）—–…·℃"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font", type=Path, required=True)
    parser.add_argument("--converter", type=Path, required=True)
    parser.add_argument("--node", default="node")
    args = parser.parse_args()
    if hashlib.sha256(args.font.read_bytes()).hexdigest() != SOURCE_SHA256:
        parser.error("font source checksum differs from approved OFL source")
    version = subprocess.check_output(
        [args.node, str(args.converter), "--version"], text=True).strip()
    if version != "1.5.3":
        parser.error("use lv_font_conv 1.5.3 for reproducible SDK-compatible output")
    target = Path(__file__).resolve().parents[1] / "src/generated/lc_font_cjk_14.c"
    subprocess.run([
        args.node, str(args.converter), "--font", str(args.font),
        "--range", "0x20-0x7e,0x4e00-0x9fff", "--symbols", SYMBOLS,
        "--size", "14", "--bpp", "2", "--format", "lvgl",
        "--no-compress", "--no-kerning", "--lv-include", "lvgl/lvgl.h",
        "--lv-font-name", "lc_font_cjk_14", "-o", str(target)
    ], check=True)
    # Converter headers contain machine-specific paths. Canonicalize only that
    # comment; glyph bytes and metrics remain the official converter's output.
    generated = target.read_text()
    body = generated[generated.index("#ifdef LV_LVGL_H_INCLUDE_SIMPLE"):]
    target.write_text(
        "/* Living Canvas CJK 14px, 2bpp, uncompressed. Generated; do not edit.\n"
        " * Derived from Noto Sans CJK SC Regular; copyright 2014-2021 Adobe.\n"
        " * Font data: SIL Open Font License 1.1; see tools/Noto-OFL.txt.\n"
        " * Source SHA256: " + SOURCE_SHA256 + "\n"
        " * Reproduce with tools/convert_room_font.py (lv_font_conv 1.5.3).\n"
        " * ASCII, U+4E00..U+9FFF and required UI punctuation.\n"
        " */\n\n" + body)
    print(f"Generated {target.name}: {target.stat().st_size} C source bytes")


if __name__ == "__main__":
    main()
