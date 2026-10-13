#!/usr/bin/env python3
"""生成学习界面所需的 LVGL 字体。

1. 从 learning_tangshi_data.c / learning_english_words.c / learning_ui.cc
   中收集全部需要渲染的字符（汉字、UI 文案、音标 IPA）。
2. 调用 lv_font_conv 生成两个字体文件：
   - learning_font_16.c    16px，ASCII + 全部汉字（诗句/释义/UI 文案）
   - learning_font_en_28.c 28px，ASCII + IPA（英文单词大字 + 音标）

用法：python3 gen_learning_font.py <字体目录> <输出目录>
字体目录需包含 NotoSansCJKsc-Regular.otf 和 DejaVuSans.ttf。
"""

import re
import subprocess
import sys
from pathlib import Path

BOARD_DIR = Path(__file__).resolve().parent


def collect_text_sources():
    sources = {}
    for name in (
        "learning_tangshi_data.c",
        "learning_english_words.c",
        "learning_ui.cc",
        "learning_ui.h",
    ):
        path = BOARD_DIR / name
        sources[name] = path.read_text(encoding="utf-8")
    return sources


def cjk_chars(text):
    return set(re.findall(r"[\u4e00-\u9fff\u3000-\u303f\uff00-\uffef·]", text))


def ipa_chars(text):
    # 拉丁扩展 / IPA 区段
    return set(re.findall(r"[\u0100-\u02ff]", text))


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    font_dir = Path(sys.argv[1])
    out_dir = Path(sys.argv[2])
    out_dir.mkdir(parents=True, exist_ok=True)

    noto = font_dir / "NotoSansCJKsc-Regular.otf"
    dejavu = font_dir / "DejaVuSans.ttf"
    for f in (noto, dejavu):
        if not f.exists():
            print(f"ERROR: missing font file {f}")
            sys.exit(1)

    sources = collect_text_sources()
    all_text = "\n".join(sources.values())

    han = cjk_chars(all_text)
    # UI 字符串里被引号包裹的中文一定是文案；诗句数据本身就是全部中文。
    print(f"CJK chars: {len(han)}")

    ipa = ipa_chars(all_text)
    print(f"IPA/extended chars: {len(ipa)}: {''.join(sorted(ipa))}")

    cjk_symbols = "".join(sorted(han))

    # --- learning_font_16: CJK + ASCII ---
    cmd16 = [
        "npx", "--yes", "lv_font_conv",
        "--font", str(noto),
        "-r", "0x20-0x7e",
        "--size", "16",
        "--bpp", "4",
        "--no-compress",
        "--symbols", cjk_symbols,
        "--format", "lvgl",
        "--lv-font-name", "learning_font_16",
        "-o", str(out_dir / "learning_font_16.c"),
    ]
    print("Generating learning_font_16.c ...")
    subprocess.run(cmd16, check=True)

    # --- learning_font_en_28: ASCII + IPA (DejaVu) ---
    ipa_ranges = "0x20-0x7e,0x100-0x17f,0x180-0x24f,0x250-0x2af,0x2c0-0x2ff"
    cmd28 = [
        "npx", "--yes", "lv_font_conv",
        "--font", str(dejavu),
        "-r", ipa_ranges,
        "--size", "28",
        "--bpp", "4",
        "--no-compress",
        "--format", "lvgl",
        "--lv-font-name", "learning_font_en_28",
        "-o", str(out_dir / "learning_font_en_28.c"),
    ]
    print("Generating learning_font_en_28.c ...")
    subprocess.run(cmd28, check=True)

    # 校验：所有收集到的字符都应出现在生成的字形表里
    for gen_name, charset in (
        ("learning_font_16.c", han | set(chr(c) for c in range(0x20, 0x7F))),
        ("learning_font_en_28.c", ipa | set(chr(c) for c in range(0x20, 0x7F))),
    ):
        gen = (out_dir / gen_name).read_text(encoding="utf-8")
        missing = [c for c in charset if c != "\\" and f"{ord(c):x}" not in gen.lower()]
        # 粗校验：unicode 值出现在注释里
        miss2 = [c for c in missing if f"U+{ord(c):04X}" not in gen and f"0x{ord(c):X}" not in gen]
        if miss2:
            print(f"WARNING: {gen_name} may miss glyphs: {''.join(miss2[:30])}")
        else:
            print(f"{gen_name}: coverage OK ({len(charset)} chars)")
    print("Done.")


if __name__ == "__main__":
    main()
