#!/usr/bin/env python3
"""macOS development asset generator; no network or cloud speech service.
Requires Pillow, fontTools, ffmpeg, macOS Daniel/Tingting voices, lv_font_conv.
Generated files are committed source assets; never hand-edit them.
"""
import pathlib, subprocess, tempfile, json
from PIL import Image
ROOT = pathlib.Path(__file__).resolve().parents[2]
BOARD = ROOT / 'main/boards/folotoy/ai-passport'
ASSETS = BOARD / 'learning_assets'
def run(args): subprocess.run(args, check=True)
def array(name, data):
    return f'alignas(4) static const uint8_t {name}[] = {{\n' + '\n'.join(','.join(str(b) for b in data[i:i+32])+',' for i in range(0,len(data),32))+'\n};\n'
with tempfile.TemporaryDirectory() as td:
    tmp = pathlib.Path(td)
    run(['swift', str(ROOT/'scripts/learning/render_emoji.swift'),str(tmp)])
    out = '#include "learning_media.h"\nnamespace learning {\n'
    sizes_by_art = [(96,76,38),(96,76),(96,76),(96,76),(64,38),(64,)]
    for i, sizes in enumerate(sizes_by_art):
        original = Image.open(tmp/f'art{i}.png').convert('RGBA')
        original.save(ASSETS/f'art{i}.png')
        for size in sizes:
            im = original.resize((size,size),Image.Resampling.LANCZOS)
            rgb, alpha = bytearray(), bytearray()
            for r,g,b,a in im.getdata():
                color = ((r>>3)<<11)|((g>>2)<<5)|(b>>3)
                rgb.extend((color&255,color>>8)); alpha.append(a)
            out += array(f'art{i}_{size}', rgb + alpha)
    # Native RGB565 + alpha avoids a full ARGB decode allocation on no-PSRAM C3.
    out += 'const lv_image_dsc_t kArt[6][4] = {\n'
    for i,sizes in enumerate(sizes_by_art):
        out += '{\n'
        for requested in (96,76,64,38):
            size=requested if requested in sizes else sizes[0]
            name=f'art{i}_{size}'
            out += f'{{.header = {{.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565A8, .flags = 0, .w = {size}, .h = {size}, .stride = {size*2}}}, .data_size = sizeof({name}), .data = {name}}},\n'
        out += '},\n'
    out += '};\n'
    words = ['apple','banana','cat','dog']
    texts = words + ['Find the '+w for w in words] + ['床前明月光','疑是地上霜','举头望明月','低头思故乡']
    manifest=[]
    for i,text in enumerate(texts):
        voice = 'Daniel' if i<8 else 'Tingting'
        run(['say','-v',voice,'-r','125' if i<8 else '145','-o',str(tmp/'speech.aiff'),text])
        path=ASSETS/f'voice{i}.ogg'
        run(['ffmpeg','-hide_banner','-loglevel','error','-y','-i',str(tmp/'speech.aiff'),'-af','silenceremove=start_periods=1:start_threshold=-50dB,apad=pad_dur=0.12','-ar','16000','-ac','1','-c:a','libopus','-b:a','20000','-frame_duration','60',str(path)])
        out += array(f'voice{i}',path.read_bytes())
        manifest.append(dict(file=path.name,text=text,voice=voice,source='macOS local speech synthesis',bytes=path.stat().st_size))
    out += 'const std::string_view kVoice[12] = {\n'
    out += '\n'.join(f'{{reinterpret_cast<const char*>(voice{i}), sizeof(voice{i})}},' for i in range(12))
    out += '\n};\n}\n'
    (BOARD/'learning_media.cc').write_text(out)
    (ASSETS/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')
