"""Pictures of Lumenfall's art without the game: python preview.py out.png
(the four lands' parallax, composed as the game shows them)."""
import sys
from PIL import Image
import art


def compose(imgs, scroll):
    screen = Image.new('RGB', (art.SCREEN_W, art.SCREEN_H), (0, 0, 0))
    for img, y, f in zip(imgs, (art.L0_Y, art.L1_Y, art.L2_Y), (8, 4, 2)):
        off = (scroll // f) % art.W
        wide = Image.new('RGBA', (art.W * 2, img.height))
        wide.paste(img, (0, 0))
        wide.paste(img, (art.W, 0))
        part = wide.crop((off, 0, off + art.SCREEN_W, img.height))
        screen.paste(part, (0, y), part)
    return screen


if __name__ == '__main__':
    lands = [int(a) for a in sys.argv[2:]] or range(4)
    shots = []
    for land in lands:
        imgs = art.layers(land)
        shots.append(compose(imgs, 0))
        shots.append(compose(imgs, 700))
    sheet = Image.new('RGB', (art.SCREEN_W * 2, art.SCREEN_H * len(lands)))
    for i, s in enumerate(shots):
        sheet.paste(s, ((i % 2) * art.SCREEN_W, (i // 2) * art.SCREEN_H))
    sheet.save(sys.argv[1])
