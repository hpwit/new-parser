#!/usr/bin/env python3
"""ChromaQuest's assets: three worlds from NASA photographs (400 x 240,
24-bit BMPs made by sips, see the README), each as its colour (RGB565,
byte-swapped like the frame buffer) and its brightness (one byte a pixel),
plus the sprites. Writes chroma_assets.h and the script's #defines.
  chroma_assets.py moon.bmp mars.bmp saturn.bmp chroma_assets.h defines.txt preview_dir"""
import math, os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from art import Img, mix, shade, poly, disc, outline, lit, OUT

KEY = 0x1FF8
W, H = 400, 240

def read_bmp(path):
    d = open(path, "rb").read()
    off = struct.unpack_from("<I", d, 10)[0]
    w, h = struct.unpack_from("<ii", d, 18)
    assert struct.unpack_from("<H", d, 28)[0] == 24 and (w, abs(h)) == (W, H)
    row = (w * 3 + 3) & ~3
    img = Img(W, H)
    for y in range(H):
        sy = y if h < 0 else H - 1 - y
        for x in range(W):
            b, g, r = d[off + sy * row + x * 3: off + sy * row + x * 3 + 3]
            img.p[y * W + x] = (r, g, b)
    return img

def add_stars(img, seed):
    """faint stars where the photograph's sky is black"""
    random.seed(seed)
    for _ in range(260):
        x, y = random.randrange(W), random.randrange(H)
        c = img.p[y * W + x]
        if c is not None and sum(c) < 30:
            b = random.random()
            img.p[y * W + x] = mix((70, 70, 90), (230, 230, 255), b)
            if b > 0.92 and x + 1 < W and sum(img.p[y * W + x + 1]) < 30:
                img.p[y * W + x + 1] = (120, 120, 150)

def lum(c):
    r, g, b = c
    return (r * 77 + g * 150 + b * 29) >> 8

# ---------------------------------------------------------------- sprites
def lumo(frame):
    """the hero: a glass lantern in a brass cage, its light pulsing"""
    s = Img(22, 24)
    glow = 1.0 if frame else 0.85
    # the hook on top
    for y in range(0, 4):
        s.put(10, y, (150, 110, 60)); s.put(11, y, (190, 150, 80))
    # the glass orb
    disc(s, 11, 13, 8.5, lambda x, y, d: mix((255, 255, int(235 * glow)), (230, 170, 60), d ** 1.5))
    disc(s, 8, 10, 2.2, lambda x, y, d: (255, 255, 255))
    # the cage: a ring and three bars
    for a in range(64):
        t = a / 64 * 6.283
        x, y = 11 + math.cos(t) * 9.5, 13 + math.sin(t) * 9.5
        s.put(x, y, (170, 120, 50) if math.sin(t) < 0 else (110, 75, 30))
    for x0 in (11,):
        for y in range(4, 23):
            s.put(x0, y, (160, 115, 50) if y < 13 else (120, 85, 35))
    for y in (13,):
        for x in range(2, 21):
            if abs(x - 11) > 2:
                s.put(x, y, (140, 100, 45))
    for x, y in ((3, 13), (19, 13), (11, 4), (11, 22)):
        s.put(x, y, (230, 200, 120))
    outline(s, OUT)
    return s

def drop(color, frame):
    """a droplet of paint: a teardrop with a shine"""
    s = Img(12, 16)
    base = {"r": (235, 40, 50), "g": (40, 210, 70), "b": (50, 110, 255)}[color]
    for y in range(16):
        for x in range(12):
            dx = x + 0.5 - 6
            if y < 6:
                w = (y + 1) * 0.75
            else:
                w = math.sqrt(max(0, 25 - (y - 10) ** 2))
            if abs(dx) <= w:
                t = (y / 16.0) * 0.6 + abs(dx) / 12.0
                s.put(x, y, mix(shade(base, 1.35), shade(base, 0.6), t))
    sx = 4 if frame else 5
    s.put(sx, 8, (255, 255, 255)); s.put(sx, 9, (255, 255, 255)); s.put(sx + 1, 7, (255, 255, 255))
    outline(s, OUT)
    return s

def moth(frame):
    """a dust moth: grey wings that beat"""
    s = Img(24, 18)
    up = frame == 0
    wing = (165, 165, 175)
    if up:
        poly(s, [(12, 9), (2, 1), (5, 9)], lambda x, y: lit(wing, x, y, 6, 5, 8))
        poly(s, [(12, 9), (22, 1), (19, 9)], lambda x, y: lit(wing, x, y, 18, 5, 8))
    else:
        poly(s, [(12, 9), (1, 14), (6, 9)], lambda x, y: lit(wing, x, y, 6, 12, 6))
        poly(s, [(12, 9), (23, 14), (18, 9)], lambda x, y: lit(wing, x, y, 18, 12, 6))
    poly(s, [(10, 5), (14, 5), (13, 16), (11, 16)], lambda x, y: lit((120, 120, 130), x, y, 12, 10, 11))
    for k in range(2):
        s.put(10 - k, 4 - k, (90, 90, 100)); s.put(13 + k, 4 - k, (90, 90, 100))
    outline(s, OUT)
    return s

def devil(frame):
    """a dust devil: a grey funnel, its bands turning"""
    s = Img(20, 26)
    for y in range(26):
        w = 2 + (25 - y) * 0.32
        for x in range(20):
            dx = x + 0.5 - 10 + math.sin(y * 0.4 + frame * 1.5) * 1.5
            if abs(dx) <= w:
                band = ((y + frame * 2) // 3) % 2
                c = (150, 140, 135) if band else (115, 108, 105)
                s.put(x, y, shade(c, 1.15 - 0.4 * abs(dx) / (w + 0.1)))
    outline(s, OUT)
    return s

def shard(frame):
    """an ice shard: a crystal turning"""
    s = Img(22, 22)
    a = frame * 0.39
    pts = [(11 + math.cos(a + k * 1.5708) * (10 if k % 2 == 0 else 6), 11 + math.sin(a + k * 1.5708) * (10 if k % 2 == 0 else 6)) for k in range(4)]
    poly(s, pts, lambda x, y: lit((190, 200, 215), x, y, 11, 11, 18))
    poly(s, [pts[0], (11, 11), pts[1]], lambda x, y: (225, 232, 245))
    outline(s, OUT)
    return s

def spark():
    s = Img(10, 6)
    for x in range(10):
        c = mix((255, 240, 180), (255, 255, 255), x / 9)
        s.put(x, 2, c); s.put(x, 3, c)
        if 2 < x < 9:
            s.put(x, 1, (255, 220, 130)); s.put(x, 4, (255, 220, 130))
    return s

def grey_bolt():
    s = Img(8, 8)
    disc(s, 4, 4, 3.4, lambda x, y, d: mix((240, 240, 245), (90, 90, 100), d))
    return s

def burst(k):
    """a paint splash: rings of colour that spread and fade"""
    s = Img(30, 30)
    r = 3 + k * 2.6
    random.seed(70 + k)
    cols = [(255, 80, 90), (90, 240, 110), (100, 150, 255), (255, 240, 140)]
    for i in range(18 + k * 4):
        a = random.random() * 6.283
        rr = r * (0.5 + random.random() * 0.6)
        c = cols[i % 4]
        if k > 3:
            c = mix(c, (60, 60, 70), (k - 3) * 0.35)
        x, y = 15 + math.cos(a) * rr, 15 + math.sin(a) * rr
        disc(s, x, y, max(0.8, 2.2 - k * 0.3), lambda xx, yy, d, c=c: c)
    if k < 2:
        disc(s, 15, 15, 3 - k, lambda x, y, d: (255, 255, 255))
    return s

def to565(c):
    if c is None:
        return KEY
    r, g, b = c
    v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    v = ((v & 0xff) << 8) | (v >> 8)
    return 0x1FF7 if v == KEY else v

def main(moon, mars, saturn, out_h, out_defs, preview):
    worlds = [read_bmp(moon), read_bmp(mars), read_bmp(saturn)]
    add_stars(worlds[0], 1)
    add_stars(worlds[2], 2)
    sprites = [("LUMO0", lumo(0)), ("LUMO1", lumo(1))]
    for c in "rgb":
        sprites += [("DROP%s0" % c.upper(), drop(c, 0)), ("DROP%s1" % c.upper(), drop(c, 1))]
    sprites += [("MOTH0", moth(0)), ("MOTH1", moth(1)), ("DEVIL0", devil(0)), ("DEVIL1", devil(1)),
                ("SHARD0", shard(0)), ("SHARD1", shard(1)), ("SPARK", spark()), ("BOLT", grey_bolt())]
    sprites += [("BURST%d" % k, burst(k)) for k in range(6)]
    data = []
    defs = ["// where each sprite is in chromaGfx[]: offset, width, height (chroma_assets.py)"]
    for name, s in sprites:
        defs.append("#define %s %d\n#define %s_W %d\n#define %s_H %d" % (name, len(data), name, s.w, name, s.h))
        data += [to565(c) for c in s.p]
    with open(out_h, "w") as f:
        f.write("// ChromaQuest's art, made by tools/chroma_assets.py: the three worlds (NASA\n"
                "// photographs, public domain: see the README) as colour, RGB565 byte-swapped\n"
                "// like the frame buffer, and brightness, one byte a pixel; then the sprites\n"
                "// (0x1FF8, magenta, is transparent). In flash.\n#pragma once\n#include <stdint.h>\n\n")
        vals = [to565(c) for wd in worlds for c in wd.p]
        f.write("// the worlds' colours, one after the other: 3 x 400 x 240\nconst uint16_t worldColor[%d] = {\n" % len(vals))
        for i in range(0, len(vals), 24):
            f.write("  " + ",".join("0x%04x" % v for v in vals[i:i + 24]) + ",\n")
        f.write("};\n\n")
        ls = [lum(c) for wd in worlds for c in wd.p]
        f.write("// their brightness, 0-255\nconst uint8_t worldLum[%d] = {\n" % len(ls))
        for i in range(0, len(ls), 32):
            f.write("  " + ",".join(str(v) for v in ls[i:i + 32]) + ",\n")
        f.write("};\n\n")
        f.write("// the sprites (see the script's #defines)\nconst uint16_t chromaGfx[%d] = {\n" % len(data))
        for i in range(0, len(data), 20):
            f.write("  " + ",".join("0x%04x" % v for v in data[i:i + 20]) + ",\n")
        f.write("};\n")
    open(out_defs, "w").write("\n".join(defs) + "\n")
    # previews: each world in colour and in grey, and the sprites
    for k, wd in enumerate(worlds):
        g = Img(W, H)
        for i, c in enumerate(wd.p):
            v = lum(c)
            g.p[i] = (v, v, v)
        both = Img(W * 2 + 8, H, (0, 0, 0))
        both.blit(g, 0, 0); both.blit(wd, W + 8, 0)
        both.save(os.path.join(preview, "world_%d.png" % (k + 1)))
    sh = Img(360, 40, (18, 16, 30))
    x = 4
    for name, s in sprites:
        if x + s.w > 356:
            break
        sh.blit(s, x, 4); x += s.w + 4
    sh.scaled(3).save(os.path.join(preview, "chroma_sprites.png"))
    print(len(data), "sprite pixels")

if __name__ == "__main__":
    main(*sys.argv[1:7])
