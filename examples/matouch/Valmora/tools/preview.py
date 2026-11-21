"""Contact sheet of every tile and creature, 2x."""
import sys
from PIL import Image
import art

terrain = art.make_terrain()
over = art.make_overlays()
creatures = [(n + str(i), f) for n, fs in art.make_creatures() for i, f in enumerate(fs)]
items = terrain + over + creatures
cols = 16
rows = (len(items) + cols - 1) // cols
sheet = Image.new('RGB', (cols * 34 * 2, rows * 34 * 2), (40, 40, 48))
checker = Image.new('RGB', (32, 32), (90, 140, 70))
for i, (n, im) in enumerate(items):
    x, y = (i % cols) * 34, (i // cols) * 34
    t = checker.copy()
    if im.mode == 'RGBA':
        t.paste(im, (0, 0), im)
    else:
        t = im
    sheet.paste(t.resize((64, 64), Image.NEAREST), (x * 2 + 2, y * 2 + 2))
sheet.save(sys.argv[1])
print(len(terrain), len(over), len(creatures))
