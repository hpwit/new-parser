#!/usr/bin/env python3
"""The shooter's assets: the alien world in three layers (a sky, far
mountains, a near crystal ridge; both ridges tile seamlessly over 512
pixels), and every sprite (two frames for those that move). Writes
shmup_assets.h (RGB565 byte-swapped, as the frame buffer wants it; 0x1FF8,
magenta, is transparent) and the script's #defines (where each sprite is)."""
import math, random, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from art import Img, hsv, mix, shade, poly, disc, outline, lit, fbm, vnoise, OUT, ship_manta, flame

KEY = 0x1FF8
W, H = 400, 240
LW = 512                       # the ridges' width: they wrap around

def seamless(f, x, *a):
    t = x / LW
    return f(x, *a) * (1 - t) + f(x - LW, *a) * t

# ---------------------------------------------------------------- layers
def sky():
    img = Img(W, H)
    for y in range(H):
        for x in range(W):
            img.p[y * W + x] = mix((24, 8, 56), (210, 76, 128), (y / 170.0) ** 1.6) if y < 170 else (210, 76, 128)
    random.seed(5)
    for _ in range(170):
        x, y = random.randrange(W), random.randrange(120)
        img.put(x, y, mix((150, 140, 200), (240, 230, 255), random.random()))
    # a big ringed planet: the ring's far half behind it, its near half in front
    cx, cy, r = 296, 58, 27
    def ring(front):
        for y in range(H):
            for x in range(W):
                dx, dy = x + 0.5 - cx, y + 0.5 - cy
                u = dx * 0.94 + dy * 0.34
                v = (-dx * 0.34 + dy * 0.94) * 3.6
                e = (u * u + v * v) / (r * 1.95) ** 2
                if 0.62 < e < 1.0 and (v > 0) == front:
                    band = 0.55 + 0.45 * math.sin(e * 40)
                    img.p[y * W + x] = mix(img.p[y * W + x], shade((235, 205, 190), band), 0.75)
    ring(False)
    disc(img, cx, cy, r, lambda x, y, d: shade(mix((250, 236, 222), (196, 170, 200), (math.sin((y - cy) / 3.5 + fbm(x / 9.0, y / 5.0) * 3) + 1) / 2),
                                         max(0.25, min(1.1, 1.05 - 0.55 * ((x - cx + (y - cy) * 0.4) / r)))))
    for ccx, ccy, cr in ((286, 50, 4), (305, 66, 3), (292, 70, 2.5), (300, 46, 2)):
        disc(img, ccx, ccy, cr, lambda x, y, d: shade(img.get(int(x), int(y)) or (200, 190, 200), 0.85))
    ring(True)
    disc(img, 350, 30, 9, lambda x, y, d: mix((190, 255, 235), (60, 160, 165), d))
    # thin clouds across the low sky
    for y in range(100, 170):
        for x in range(W):
            n = fbm(x / 70.0, y / 9.0, 4)
            if n > 0.56:
                img.p[y * W + x] = mix(img.p[y * W + x], (255, 170, 200), (n - 0.56) * 2.5)
    return img

def far_ridge():
    """far mountains, 512 x 150, transparent above their tops"""
    h = 150
    img = Img(LW, h)
    for x in range(LW):
        top = int(10 + seamless(lambda xx: fbm(xx / 70.0, 1.5), x) * 70)
        for y in range(top, h):
            t = (y - top) / (h - top + 1)
            c = mix((110, 50, 140), (46, 18, 76), t ** 0.7)
            # snow-lit edges facing the moon
            if y - top < 2:
                c = (170, 110, 190)
            img.put(x, y, c)
    return img

def near_ridge():
    """near crystal ridge, 512 x 80: faceted crystals, lit on one face"""
    h = 80
    img = Img(LW, h)
    for x in range(LW):
        k = 2 * math.pi * x / LW
        top = int(18 + abs(math.sin(k * 11)) * 26 + seamless(lambda xx: fbm(xx / 24.0, 7), x) * 22)
        face = math.cos(k * 11) > 0
        for y in range(top, h):
            t = (y - top) / (h - top + 1)
            if face:
                c = mix((150, 245, 255), (30, 70, 130), t ** 0.6)
            else:
                c = mix((80, 170, 230), (16, 36, 84), t ** 0.6)
            if y - top < 1:
                c = (230, 255, 255)
            img.put(x, y, c)
    return img

def forest():
    """strange trees, 512 x 150, tiling: mushroom trees with glowing caps,
    twisted trunks with hanging bulbs, curled ferns, balloon pods"""
    h = 150
    img = Img(LW, h)
    def put(x, y, c):
        img.put(int(x) % LW, y, c)
    def trunk(x0, top, width, sway, col):
        for y in range(top, h):
            t = (y - top) / max(1, h - top)
            cx = x0 + math.sin(t * 3.0 + x0) * sway * (1 - t)
            w = width * (0.6 + 0.6 * t)
            for dx in range(-int(w) - 1, int(w) + 2):
                if abs(dx) <= w:
                    put(cx + dx, y, shade(col, 1.15 - 0.5 * (dx + w) / (2 * w + 1)))
    def blob(cx, cy, rx, ry, c1, c2, spots=None):
        for y in range(int(cy - ry), int(cy + ry) + 1):
            for x in range(int(cx - rx), int(cx + rx) + 1):
                d = ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2
                if d <= 1 and 0 <= y < h:
                    c = mix(c1, c2, d * 0.5 + (y - cy + ry) / (4 * ry))
                    if spots and vnoise(x / 2.5 + cx, y / 2.5) > 0.78:
                        c = spots
                    put(x, y, c)
    random.seed(21)
    xs = [10, 70, 128, 190, 250, 300, 362, 420, 470]
    for i, x0 in enumerate(xs):
        kind = i % 4
        top = random.randint(30, 70)
        if kind == 0:                                     # mushroom tree
            trunk(x0, top + 10, 4, 6, (120, 60, 150))
            blob(x0 + 2, top + 6, 26, 12, (230, 90, 200), (120, 30, 120), (120, 255, 240))
            for k in range(6):
                for y in range(top + 14, top + 22 + k % 3 * 4):
                    put(x0 - 18 + k * 8, y, (255, 150, 230))
        elif kind == 1:                                   # twisted trunk with bulbs
            trunk(x0, top, 3, 12, (90, 50, 120))
            for k in range(5):
                bx = x0 - 16 + k * 8 + math.sin(k) * 3
                by = top + 8 + (k % 2) * 10
                for y in range(top, int(by)):
                    put(bx, y, (150, 90, 170))
                blob(bx, by + 3, 3.5, 4.5, (255, 240, 160), (240, 120, 30))
        elif kind == 2:                                   # curled fern
            for s in range(160):
                a = s * 0.09
                r = 22 - s * 0.12
                fx = x0 + math.cos(a) * r
                fy = top + 24 - math.sin(a) * r
                for dd in range(-1, 2):
                    put(fx + dd, int(fy), (60, 170, 120))
                if s % 12 == 0:
                    for k in range(7):
                        put(fx + k * 0.7, int(fy) - k, (130, 230, 150))
            trunk(x0 + 20, top + 26, 2, 3, (50, 120, 90))
        else:                                             # balloon pods
            trunk(x0, top + 30, 2, 4, (110, 60, 140))
            for k in range(4):
                blob(x0 + math.sin(k * 1.7) * 6, top + 30 - k * 11, 8 - k, 6 - k * 0.6, (160, 255, 220), (40, 140, 150))
    # haze: the forest is farther than the crystals
    for i, c in enumerate(img.p):
        if c is not None:
            img.p[i] = mix(c, (150, 80, 150), 0.18)
    return img

def front():
    """dark plant silhouettes passing in front, 640 x 60, sparse"""
    w, h = 640, 60
    img = Img(w, h)
    random.seed(33)
    for x0 in (40, 230, 380, 560):
        hgt = random.randint(34, 58)
        for y in range(h - hgt, h):
            t = (y - (h - hgt)) / hgt
            for dx in range(-3, 4):
                if abs(dx) <= 1 + t * 2:
                    img.put((x0 + dx + int(math.sin(t * 4) * 4)) % w, y, (20, 6, 30))
        for k in range(5):
            a = -0.8 + k * 0.4
            for s in range(18):
                img.put(int(x0 + math.sin(a) * s * 1.4) % w, h - hgt + 6 + int(abs(math.cos(a)) * s * 0.4) + k, (34, 10, 46))
        disc(img, x0, h - hgt + 2, 3, lambda x, y, d: mix((255, 120, 220), (120, 20, 120), d))
    return img

def solid_row(img):
    """the first row from which every pixel down is opaque"""
    for j in range(img.h):
        if all(img.p[jj * img.w + x] is not None for jj in range(j, img.h) for x in range(img.w)):
            return j
    return img.h

# ---------------------------------------------------------------- sprites
def manta(frame):
    s = ship_manta()
    if frame:
        disc(s, 19, 11, 4, lambda x, y, d: mix((255, 255, 255), (60, 220, 210), d))
    return s

def bug(frame):
    s = Img(22, 18)
    disc(s, 12, 9, 6.5, lambda x, y, d: mix((170, 255, 90), (30, 120, 30), d * 0.7 + (y - 3) / 25))
    for k in range(3):
        dy = 1 if frame else 0
        s.put(14 + k * 2, 2 + dy, (60, 140, 40)); s.put(15 + k * 2, 1 + dy, (60, 140, 40))
        s.put(14 + k * 2, 16 - dy, (60, 140, 40)); s.put(15 + k * 2, 17 - dy, (60, 140, 40))
    disc(s, 7, 7, 2, lambda x, y, d: (255, 230, 40)); disc(s, 7, 11, 2, lambda x, y, d: (255, 230, 40))
    s.put(6, 7, OUT); s.put(6, 11, OUT)
    outline(s, OUT)
    return s

def jelly(frame):
    s = Img(20, 26)
    disc(s, 11, 9, 7, lambda x, y, d: mix((240, 170, 255), (110, 30, 160), d))
    for k in range(4):
        for t in range(9):
            s.put(14 - k * 3 + int(math.sin(t * 0.9 + k + frame * 1.6) * 1.4), 15 + t, mix((200, 120, 230), (90, 20, 130), t / 9))
    disc(s, 9, 8, 1.6, lambda x, y, d: (255, 255, 255))
    outline(s, OUT)
    return s

def crawler(frame):
    s = Img(28, 20)
    poly(s, [(4, 10), (12, 3), (26, 5), (24, 15), (12, 17)], lambda x, y: lit((190, 90, 40), x, y, 15, 10, 14))
    for k in range(4):
        disc(s, 13 + k * 3.5, 10, 1.4, lambda x, y, d: (255, 200, 90) if frame and k % 2 else (240, 170, 80))
    o = 1 if frame else 0
    poly(s, [(5, 8), (0, 2 + o), (8, 6)], lambda x, y: (240, 230, 200))
    poly(s, [(5, 12), (0, 18 - o), (8, 14)], lambda x, y: (240, 230, 200))
    disc(s, 8, 10, 1.6, lambda x, y, d: (255, 40, 40))
    outline(s, OUT)
    return s

def boss(frame):
    """the hive mother: a carapace in plates, tentacles, one great eye"""
    s = Img(84, 66)
    # tentacles behind, trailing right
    for k in range(5):
        for t in range(26):
            y = 14 + k * 10 + int(math.sin(t * 0.35 + k + frame * 1.2) * 3)
            x = 52 + t
            r = 3 - t // 10
            disc(s, x, y, max(1, r), lambda xx, yy, d, t=t: mix((150, 60, 170), (70, 20, 90), t / 26))
    # body
    for y in range(66):
        for x in range(66):
            dx, dy = (x - 34) / 30.0, (y - 33) / 28.0
            d = dx * dx + dy * dy
            if d <= 1:
                plate = (int((x + 3) / 9) + int(y / 11)) % 2
                c = mix((150, 230, 120), (40, 110, 60), d * 0.6 + (y - 5) / 120)
                if plate:
                    c = shade(c, 0.82)
                if abs(d - 0.85) < 0.06:
                    c = (210, 255, 170)
                s.put(x, y, c)
    # mandibles on the left
    poly(s, [(8, 26), (0, 18), (12, 24)], lambda x, y: (240, 230, 200))
    poly(s, [(8, 40), (0, 48), (12, 42)], lambda x, y: (240, 230, 200))
    # the eye
    disc(s, 22, 33, 11, lambda x, y, d: mix((255, 250, 220), (200, 140, 60), d))
    if frame:
        disc(s, 20, 33, 6, lambda x, y, d: mix((255, 80, 80), (150, 0, 30), d))
        disc(s, 19, 31, 2, lambda x, y, d: (255, 255, 255))
    else:
        for x in range(12, 33):
            for y in range(31, 36):
                s.put(x, y, (60, 110, 50))
    outline(s, OUT)
    return s

def shot():
    s = Img(14, 5)
    for x in range(14):
        t = x / 13
        s.put(x, 2, mix((120, 255, 240), (255, 255, 255), t))
        if 3 < x < 13:
            s.put(x, 1, mix((40, 200, 200), (180, 255, 250), t)); s.put(x, 3, mix((40, 200, 200), (180, 255, 250), t))
    return s

def shot_small():
    s = Img(8, 4)
    for x in range(8):
        c = mix((180, 120, 255), (255, 240, 255), x / 7)
        s.put(x, 1, c); s.put(x, 2, c)
    return s

def orb(c1, c2, r):
    n = int(r * 2 + 2)
    s = Img(n, n)
    disc(s, n / 2, n / 2, r, lambda x, y, d: mix(c1, c2, d))
    s.put(int(n / 2 - r / 3), int(n / 2 - r / 3), (255, 255, 255))
    return s

def explosion(k):
    s = Img(32, 32)
    r = 4 + k * 2.4
    random.seed(40 + k)
    for y in range(32):
        for x in range(32):
            d = math.hypot(x + 0.5 - 16, y + 0.5 - 16) / r
            n = vnoise(x / 3.0 + k * 7, y / 3.0)
            if d < 1 and n > d * 0.7 + k * 0.08:
                if k < 2:
                    c = mix((255, 255, 230), (255, 200, 80), d)
                elif k < 4:
                    c = mix((255, 230, 120), (240, 80, 20), d)
                else:
                    c = mix((200, 90, 50), (90, 70, 80), d)
                s.put(x, y, c)
    for _ in range(4 + k * 2):
        a = random.random() * 6.283
        rr = r * (1.0 + random.random() * 0.5)
        s.put(16 + math.cos(a) * rr, 16 + math.sin(a) * rr, (255, 220, 120) if k < 4 else (140, 120, 130))
    return s

def pickup(letter_rows, c1, c2):
    s = orb(c1, c2, 6.5)
    for j, row in enumerate(letter_rows):
        for i, ch in enumerate(row):
            if ch == "#":
                s.put(5 + i, 4 + j, (255, 255, 255))
    outline(s, OUT)
    return s

def icon():
    s = ship_manta()
    o = Img(18, 11)
    for y in range(11):
        for x in range(18):
            o.p[y * 18 + x] = s.get(x * 2, y * 2)
    return o

def to565(c):
    if c is None:
        return KEY
    r, g, b = c
    v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    v = ((v & 0xff) << 8) | (v >> 8)
    return 0x1FF7 if v == KEY else v

def main(out_h, out_defs, preview_dir):
    sprites = [
        ("PLAYER0", manta(0)), ("PLAYER1", manta(1)),
        ("BUG0", bug(0)), ("BUG1", bug(1)),
        ("JELLY0", jelly(0)), ("JELLY1", jelly(1)),
        ("CRAWL0", crawler(0)), ("CRAWL1", crawler(1)),
        ("BOSS0", boss(0)), ("BOSS1", boss(1)),
        ("SHOT", shot()), ("SHOT2", shot_small()),
        ("EBUL", orb((255, 230, 250), (255, 40, 160), 3)),
        ("SPORE", orb((230, 255, 200), (60, 200, 40), 3.5)),
        ("POWER", pickup(["##.", "#.#", "##.", "#..", "#.."], (120, 255, 240), (0, 140, 160))),
        ("BOMB", pickup(["##.", "#.#", "##.", "#.#", "##."], (255, 200, 120), (200, 80, 0))),
        ("ICON", icon()),
    ] + [("EXPL%d" % k, explosion(k)) for k in range(6)]
    data = []
    defs = ["// where each sprite is in gfx[]: offset, width, height (assets.py)"]
    for name, s in sprites:
        defs.append("#define %s %d\n#define %s_W %d\n#define %s_H %d" % (name, len(data), name, s.w, name, s.h))
        data += [to565(c) for c in s.p]
    layers = [("sky", sky()), ("farRidge", far_ridge()), ("forest", forest()), ("nearRidge", near_ridge()), ("frontPlants", front())]
    for name, img in layers[1:]:
        N = {"farRidge": "FAR", "forest": "FOREST", "nearRidge": "NEAR", "frontPlants": "FRONT"}[name]
        defs.append("#define %s_W %d\n#define %s_H %d\n#define %s_SOLID %d" % (N, img.w, N, img.h, N, solid_row(img)))
    with open(out_h, "w") as f:
        f.write("// The shooter's art, made by assets.py: RGB565 byte-swapped as the frame\n"
                "// buffer wants it; 0x1FF8 (magenta) is transparent. In flash.\n#pragma once\n#include <stdint.h>\n\n")
        for name, img in layers:
            vals = [to565(c) for c in img.p]
            f.write("// %s: %d x %d\nconst uint16_t %s[%d] = {\n" % (name, img.w, img.h, name, len(vals)))
            for i in range(0, len(vals), 20):
                f.write("  " + ",".join("0x%04x" % v for v in vals[i:i + 20]) + ",\n")
            f.write("};\n\n")
        f.write("// the sprites, one after the other (see the script's #defines)\nconst uint16_t gfx[%d] = {\n" % len(data))
        for i in range(0, len(data), 20):
            f.write("  " + ",".join("0x%04x" % v for v in data[i:i + 20]) + ",\n")
        f.write("};\n")
    open(out_defs, "w").write("\n".join(defs) + "\n")
    # previews
    sh = Img(420, 90, (18, 16, 30))
    x = 4
    for name, s in sprites[:10]:
        sh.blit(s, x, 4); x += s.w + 4
    x = 4
    for name, s in sprites[10:]:
        sh.blit(s, x, 72 - s.h // 2 + 6); x += s.w + 4
    sh.scaled(3).save(os.path.join(preview_dir, "assets_sprites.png"))
    comp = Img(W, H); comp.blit(layers[0][1], 0, 0)
    # where the script puts them
    comp.blit(layers[1][1], 0, H - layers[1][1].h)
    comp.blit(layers[2][1], 0, H - layers[2][1].h + 6)
    comp.blit(layers[3][1], 0, H - 56)
    comp.blit(layers[4][1], 0, H - layers[4][1].h)
    comp.blit(sprites[0][1], 70, 110)
    for k, (x, y) in enumerate(((240, 70), (290, 120), (230, 150))):
        comp.blit(sprites[2 + k * 2][1], x, y)
    comp.scaled(2).save(os.path.join(preview_dir, "assets_layers.png"))
    print(len(data), "sprite pixels;", sum(i.w * i.h for _, i in layers), "layer pixels")

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], sys.argv[3])
