#!/usr/bin/env python3
from PIL import Image, ImageDraw
from pathlib import Path
import struct, sys
root=Path(sys.argv[1])
for p in root.glob('*.rgb565'):
    vals=struct.unpack('<'+str(240*320)+'H',p.read_bytes())
    im=Image.new('RGB',(240,320));im.putdata([(((v>>11)&31)*255//31,((v>>5)&63)*255//63,(v&31)*255//31) for v in vals]);im.save(p.with_suffix('.png'))
names=['home','apple','dog','quiz','correct','quiz2','poem','recall-button','recall','recall-end']
sheet=Image.new('RGB',(5*260,2*355),'#e6e9de');draw=ImageDraw.Draw(sheet)
for i,name in enumerate(names):
    x=(i%5)*260+10;y=(i//5)*355+20
    sheet.paste(Image.open(root/f'{name}.png'),(x,y));draw.text((x,y-15),name,fill='#30413b')
sheet.save(root/'overview.png')
