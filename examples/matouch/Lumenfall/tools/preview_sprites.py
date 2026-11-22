"""A sheet of sprites, 4x: python preview_sprites.py out.png [group]"""
import sys
from PIL import Image
import sprites

group = sys.argv[2] if len(sys.argv) > 2 else 'wren'
items = [(n, f()) for n, f in getattr(sprites, group.upper() + '_FRAMES')]
Z = 4
cols = min(12, len(items))
w = max(s.w for _, s in items) + 2
h = max(s.h for _, s in items) + 2
sheet = Image.new('RGB', (cols * w * Z, ((len(items) + cols - 1) // cols) * h * Z), (110, 140, 190))
for i, (n, s) in enumerate(items):
    im = s.image()
    sheet.paste(im.resize((s.w * Z, s.h * Z), Image.NEAREST), ((i % cols) * w * Z, (i // cols) * h * Z), im.resize((s.w * Z, s.h * Z), Image.NEAREST))
sheet.save(sys.argv[1])
