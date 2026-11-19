#!/usr/bin/env python3
"""Ironforge's title picture, TITLEPIC in the WAD: 240x144, the size of the
game's view. A foundry at night under a red sky, its furnace towers lit
from inside, a river of molten iron, sparks, and the name in beveled metal
letters glowing from below. title() gives the rows (RGB); freedoom.py and
wad.py put it in the WAD as a picture."""
import math, random

W, H = 240, 144

# the letters, 5 x 7, drawn big
GLYPHS = {
    "I": ["#####", "..#..", "..#..", "..#..", "..#..", "..#..", "#####"],
    "R": ["####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"],
    "O": [".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
    "N": ["#...#", "##..#", "#.#.#", "#.#.#", "#..##", "#...#", "#...#"],
    "F": ["#####", "#....", "#....", "####.", "#....", "#....", "#...."],
    "G": [".####", "#....", "#....", "#.###", "#...#", "#...#", ".####"],
    "E": ["#####", "#....", "#....", "####.", "#....", "#....", "#####"],
}


def mix(a, b, t):
    t = max(0.0, min(1.0, t))
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


def title(name="IRONFORGE"):
    random.seed(7)
    img = [[(0, 0, 0)] * W for _ in range(H)]
    # the sky: dark at the top, red to the horizon, a glow over the river
    horizon = 92
    for y in range(H):
        for x in range(W):
            t = y / horizon
            c = mix((12, 4, 10), (120, 28, 14), t ** 1.6)
            glow = math.exp(-((x - 120) / 70.0) ** 2) * max(0, t - 0.4) * 1.2
            img[y][x] = mix(c, (230, 110, 30), glow * 0.6)
    # far stars of ash
    for _ in range(60):
        x, y = random.randrange(W), random.randrange(40)
        img[y][x] = (90, 60, 60)
    # the towers: dark blocks, chimneys, windows lit orange
    random.seed(11)
    x = 0
    while x < W:
        w = random.randint(14, 30)
        top = random.randint(48, 78)
        for yy in range(top, horizon + 20):
            for xx in range(x, min(W, x + w)):
                img[yy][xx] = mix((22, 14, 16), (40, 24, 22), (xx - x) / w)
        # a chimney, sometimes
        if random.random() < 0.6:
            cx = x + random.randint(2, w - 6)
            ct = top - random.randint(10, 26)
            for yy in range(ct, top):
                for xx in range(cx, cx + 4):
                    if 0 <= xx < W:
                        img[yy][xx] = (26, 16, 18)
            # its smoke, lit from below
            for k in range(30):
                sx = cx + 2 + int(math.sin(k * 0.4) * 3) + k // 3
                sy = ct - k
                if 0 <= sy and 0 <= sx < W:
                    for dx in range(-2, 3):
                        if 0 <= sx + dx < W:
                            img[sy][sx + dx] = mix(img[sy][sx + dx], (90, 50, 45), 0.2)
        # windows
        for yy in range(top + 4, horizon + 10, 7):
            for xx in range(x + 3, x + w - 3, 6):
                if random.random() < 0.55:
                    for a in range(3):
                        for b in range(2):
                            if 0 <= xx + b < W:
                                img[yy + a][xx + b] = mix((255, 150, 40), (255, 220, 120), random.random())
        x += w + random.randint(0, 4)
    # the river of molten iron at the bottom
    for y in range(horizon + 20, H):
        for x in range(W):
            t = (y - horizon - 20) / (H - horizon - 20)
            wave = math.sin(x * 0.15 + y * 0.6) * 0.5 + math.sin(x * 0.05 - y * 0.3) * 0.5
            c = mix((150, 30, 8), (255, 170, 40), 0.5 + wave * 0.35 - t * 0.2)
            if wave > 0.8:
                c = (255, 235, 150)
            img[y][x] = c
    # sparks rising
    random.seed(3)
    for _ in range(90):
        x, y = random.randrange(W), random.randrange(30, H - 20)
        img[y][x] = mix((255, 120, 30), (255, 240, 160), random.random())

    # the name: beveled iron, a dark outline, a glow under it
    sc = 4
    gw = 5 * sc + 4
    total = len(name) * gw - 4
    x0 = (W - total) // 2
    y0 = 14
    mask = [[0] * W for _ in range(H)]
    for i, ch in enumerate(name):
        g = GLYPHS[ch]
        for r in range(7):
            for c in range(5):
                if g[r][c] == "#":
                    for a in range(sc):
                        for b in range(sc):
                            mask[y0 + r * sc + a][x0 + i * gw + c * sc + b] = 1
    hgt = 7 * sc
    for y in range(H):
        for x in range(W):
            if mask[y][x]:
                continue
            # the outline and the glow below the letters
            near = any(0 <= y + dy < H and 0 <= x + dx < W and mask[y + dy][x + dx]
                       for dy in (-1, 0, 1, 2) for dx in (-1, 0, 1))
            if near:
                img[y][x] = (8, 4, 4)
            elif y > y0 + hgt + 1 and y < y0 + hgt + 5 and any(mask[y0 + hgt - 1][xx] for xx in range(max(0, x - 2), min(W, x + 3))):
                img[y][x] = mix(img[y][x], (255, 140, 40), 0.35 * (1 - (y - y0 - hgt) / 5))
    for y in range(H):
        for x in range(W):
            if not mask[y][x]:
                continue
            ry = (y - y0) / hgt
            # light from above on the top edges, dark at the bottom edges,
            # the metal going from pale steel to orange where the fire lights it
            up = y > 0 and not mask[y - 1][x]
            down = y + 1 < H and not mask[y + 1][x]
            left = x > 0 and not mask[y][x - 1]
            c = mix((210, 214, 225), (120, 110, 115), ry)
            c = mix(c, (255, 140, 50), max(0, ry - 0.55) * 1.6)
            if up or left:
                c = mix(c, (255, 255, 255), 0.6)
            if down:
                c = mix(c, (60, 30, 20), 0.7)
            # a rivet or two, scratches
            if (x * 7 + y * 13) % 53 == 0:
                c = mix(c, (60, 60, 70), 0.6)
            img[y][x] = c
    return img


def picture_rows(img):
    """the rows with no clear pixel: the column format still wants runs"""
    return img


if __name__ == "__main__":
    import sys, os
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from wad import save_png, quantised
    rows = quantised(title())
    save_png(sys.argv[1] if len(sys.argv) > 1 else "title.png", [[p for p in r for _ in range(2)] for r in rows for _ in range(2)])
