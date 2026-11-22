"""A sheet of every land's tiles, 3x: python preview_tiles.py out.png"""
import sys
from PIL import Image
import tiles

rows = []
for land in range(4):
    rows.append([t.image() for t in tiles.tile_set(land)])
n = len(rows[0])
Z = 3
sheet = Image.new('RGB', (n * (16 + 2) * Z, 4 * (16 + 2) * Z), (255, 0, 255))
for j, row in enumerate(rows):
    for i, im in enumerate(row):
        bg = Image.new('RGBA', (16, 16), (120, 150, 200, 255))
        bg.alpha_composite(im)
        sheet.paste(bg.resize((16 * Z, 16 * Z), Image.NEAREST), (i * 18 * Z, j * 18 * Z))
sheet.save(sys.argv[1])
