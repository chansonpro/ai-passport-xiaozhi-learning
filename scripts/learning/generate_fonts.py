#!/usr/bin/env python3
"""Generate the compact learning fonts from a supplied CJK TTF/OTF/TTC.
Usage: python3 scripts/learning/generate_fonts.py /path/to/font.ttc
Requires fontTools and npx lv_font_conv. No hand editing generated C files.
"""
from pathlib import Path
from fontTools.ttLib import TTFont
import subprocess, sys, tempfile, re
board=Path(__file__).resolve().parents[2]/'main/boards/folotoy/ai-passport'
# Include only displayed literals, not comments or the legacy 300-poem catalog.
texts=''.join(re.findall(r'"([^"\n]*)"', '\n'.join((board/f).read_text() for f in ['learning_render.cc','learning_ui.cc','learning_model.h'])))
chars=''.join(sorted(set(texts)))
with tempfile.TemporaryDirectory() as td:
    path=Path(td)/'source.ttf'
    font=TTFont(sys.argv[1],fontNumber=0)
    missing=[c for c in chars if ord(c)>127 and ord(c) not in font.getBestCmap()]
    assert not missing, missing
    font.save(path)
    for size in [12,16,22,34]:
        args=['npx','--yes','lv_font_conv@1.5.3','--font',str(path),'--size',str(size),'--bpp','4','--no-compress','-r','0x20-0x7e','--format','lvgl','--lv-font-name',f'learning_v1_{size}','-o',str(board/f'learning_v1_{size}.c')]
        if size!=34: args+=['--symbols',chars]
        subprocess.run(args,check=True)
