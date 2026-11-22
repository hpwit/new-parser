"""Builds lumenfall.wad and refreshes the generated part of lumenfall.sc.

    python make_wad.py [out.wad] [lumenfall.sc]

The WAD (as Valmora's and Ironforge's: "PWAD", u32 count, u32 directory;
16 bytes an entry: u32 offset, u32 size, 8 bytes of name) holds:

  BG<land>_<n>  the three parallax layers of each land       (images)
  TILES<land>   the land's 36 tiles, 16x16                     (image)
  CRIT<land>    its three creatures: walker (2 frames and
                flattened), flyer (2), special (2); 16x16      (image)
  BOSS<land>    its guardian: 3 frames (the last: hit), 48x48  (image)
  PLAT<land>    its moving platform, 48x8                      (image)
  WREN          Wren's 22 frames, 16x24                        (image)
  BOLT, BIGBOLT, ORB, SPORE, SHARD, FIRE   shots, 8x6          (images)
  HEART, PRISM, LIFE                       pickups, 12x12      (images)
  PUFF, SPARK, CHECK, BEACON, HUD          the rest            (images)
  LOGO          the title, 256x56                              (image)
  FONT          95 characters: u8 width each, then 8 bytes each
                (a row a byte, bit 7 the left pixel)
  LEVEL1..8     see levels.py
  TEXT          zero-terminated strings (the story)

An image lump is compressed (below): unpacked, it is u16 width, u16 height,
u16 frames, u16 colours, u16 y, u16 first row drawn, u16 first full row,
u16 0, then the colours (RGB565, byte-swapped as the panel wants them;
colour 0 is "transparent"), then width*height*frames bytes, one colour a
pixel, the frames one under the other.

Compression (every lump but FONT and TEXT): u32 unpacked size, then
tokens: a byte t < 128 is followed by t+1 bytes to copy; a byte t >= 128
copies (t & 127) + 3 bytes from d bytes back, d the next u16.
"""
import os
import struct
import sys

from PIL import Image

import art
import font6
import levels
import sprites
import tiles

HERE = os.path.dirname(os.path.abspath(__file__))
KEY = 0x1FF8          # "transparent" as the script sees it (magenta, byte-swapped)


# ------------------------------------------------------------ compression

def lz(data):
    out = bytearray(struct.pack('<I', len(data)))
    n = len(data)
    head = {}
    lit = bytearray()

    def flush():
        i = 0
        while i < len(lit):
            k = min(128, len(lit) - i)
            out.append(k - 1)
            out.extend(lit[i:i + k])
            i += k
        lit.clear()

    i = 0
    while i < n:
        best_len, best_d = 0, 0
        if i + 3 <= n:
            key = bytes(data[i:i + 3])
            cands = head.get(key, [])
            for p in reversed(cands[-48:]):
                d = i - p
                if d > 65535:
                    break
                l = 3
                while l < 130 and i + l < n and data[p + l] == data[i + l]:
                    l += 1
                if l > best_len:
                    best_len, best_d = l, d
                    if l == 130:
                        break
        if best_len >= 3:
            flush()
            out.append(128 + best_len - 3)
            out += struct.pack('<H', best_d)
            for k in range(best_len):
                if i + k + 3 <= n:
                    head.setdefault(bytes(data[i + k:i + k + 3]), []).append(i + k)
            i += best_len
        else:
            if i + 3 <= n:
                head.setdefault(bytes(data[i:i + 3]), []).append(i)
            lit.append(data[i])
            i += 1
    flush()
    return bytes(out)


def unlz(blob):
    n = struct.unpack('<I', blob[:4])[0]
    out = bytearray()
    i = 4
    while len(out) < n:
        t = blob[i]
        i += 1
        if t < 128:
            out += blob[i:i + t + 1]
            i += t + 1
        else:
            d = blob[i] | (blob[i + 1] << 8)
            i += 2
            for _ in range((t & 127) + 3):
                out.append(out[-d])
    return bytes(out)


# ------------------------------------------------------------ images

def swap565(c):
    r, g, b = c[:3]
    v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    v = ((v & 0xFF) << 8) | (v >> 8)
    if v == KEY:
        v ^= 0x0100   # a hair off magenta: never mistaken for transparent
    return v


def image_lump(frames, y=0):
    """frames: RGBA images of one size. Returns (lump, info)."""
    w, h = frames[0].size
    sheet = Image.new('RGBA', (w, h * len(frames)))
    for i, f in enumerate(frames):
        sheet.paste(f, (0, i * h))
    px = sheet.load()
    opaque = {}
    for yy in range(sheet.height):
        for xx in range(w):
            p = px[xx, yy]
            if p[3] >= 128:
                opaque[p[:3]] = opaque.get(p[:3], 0) + 1
    cols = list(opaque)
    if len(cols) > 255:
        # too many: reduce the opaque pixels to 255 colours, as the panel would show them
        rgb_img = Image.new('RGB', (len(cols), 1))
        rgb_img.putdata(cols)
        big = Image.new('RGB', (sheet.width, sheet.height))
        bpx = big.load()
        for yy in range(sheet.height):
            for xx in range(w):
                p = px[xx, yy]
                bpx[xx, yy] = p[:3]
        q = big.quantize(colors=255, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
        pal = q.getpalette()[:255 * 3]
        cols = [art.q565(tuple(pal[i * 3:i * 3 + 3]), dither=False) for i in range(len(pal) // 3)]
        qpx = q.load()
        index = lambda xx, yy: qpx[xx, yy] + 1
    else:
        lookup = {c: i + 1 for i, c in enumerate(cols)}
        index = lambda xx, yy: lookup[px[xx, yy][:3]]
    first_row = None
    for yy in range(h):
        if any(px[xx, yy][3] >= 128 for xx in range(w)):
            first_row = yy
            break
    first_row = h if first_row is None else first_row
    # the first row from which every row down to the bottom is opaque
    full_row = h
    for yy in range(h - 1, -1, -1):
        if all(px[xx, yy][3] >= 128 for xx in range(w)):
            full_row = yy
        else:
            break
    raw = bytearray(struct.pack('<8H', w, h, len(frames), len(cols) + 1, y, first_row, full_row, 0))
    raw += struct.pack('<H', KEY)
    for c in cols:
        raw += struct.pack('<H', swap565(c))
    for yy in range(sheet.height):
        for xx in range(w):
            raw.append(index(xx, yy) if px[xx, yy][3] >= 128 else 0)
    return lz(bytes(raw)), dict(w=w, h=h, frames=len(frames), y=y, first=first_row, full=full_row, colours=len(cols))


# ------------------------------------------------------------ the rest

def font_lump():
    widths = bytearray()
    rows = bytearray()
    for ch in range(32, 127):
        w, g = font6.glyph(chr(ch))
        widths.append(w + 1)
        for r in g:
            b = 0
            for i, v in enumerate(r[:8]):
                if v:
                    b |= 0x80 >> i
            rows.append(b)
    return bytes(widths + rows)


STORY = [
    # 0: the opening, a page per |
    "Long ago the four lands kept a light each, burning in a great beacon:|"
    "the dawn in Dawnmere, the dusk in Mirewood, the deep glow of "
    "Glimmerdeep, and the moon over the Hollow Spire.|"
    "Then the Gloam King climbed the Spire and drank the lights one by one, "
    "and set a guardian at each cold beacon.|"
    "Wren, the last of the lamplighters, takes up her lantern. Every beacon "
    "she lights gives a land its morning back.",
    # 1: the end
    "The Gloam King falls, and the four lights pour out of his crown.|"
    "Beacon by beacon the lands wake: dawn over the meadows, fireflies in "
    "the old wood, the caves ringing with crystal, the Spire silver under "
    "the moon.|"
    "Wren hangs her lantern by the door. It still glows, a little, all night "
    "long.|"
    "THE END - thank you for playing Lumenfall",
]


def logo_lump():
    """LUMENFALL in the pixel font, 4x, a gold gradient with a dark rim and a
    glow; a lantern for the U's dot."""
    text = 'LUMENFALL'
    scale = 4
    gl = [font6.glyph(c) for c in text]
    width = sum((w + 1) * scale for w, _ in gl) + 8
    h = 8 * scale + 24
    img = Image.new('RGBA', (width, h), (0, 0, 0, 0))
    px = img.load()
    mask = [[0] * width for _ in range(h)]
    x = 4
    for w, rows in gl:
        for ry, r in enumerate(rows):
            for rx, v in enumerate(r):
                if v:
                    for dy in range(scale):
                        for dx in range(scale):
                            mask[8 + ry * scale + dy][x + rx * scale + dx] = 1
        x += (w + 1) * scale
    for yy in range(h):
        for xx in range(width):
            if mask[yy][xx]:
                t = (yy - 8) / (8 * scale)
                c = art.ramp([art.rgb('#fff8d0'), art.rgb('#ffd060'), art.rgb('#f09030'), art.rgb('#b04a18')], t)
                if yy % scale == 0 and t < 0.5:
                    c = art.mix(c, art.rgb('#ffffff'), 0.4)
                px[xx, yy] = art.q565(c, xx, yy, False) + (255,)
    for yy in range(h):
        for xx in range(width):
            if mask[yy][xx]:
                continue
            near = any(0 <= yy + dy < h and 0 <= xx + dx < width and mask[yy + dy][xx + dx]
                       for dy in (-2, -1, 0, 1, 2) for dx in (-2, -1, 0, 1, 2) if abs(dx) + abs(dy) <= 2)
            if near:
                px[xx, yy] = art.q565(art.rgb('#2a1430'), dither=False) + (255,)
    # the subtitle bar
    for xx in range(24, width - 24):
        for yy in (h - 8, h - 7):
            px[xx, yy] = art.q565(art.mix(art.rgb('#ffd060'), art.rgb('#6a2a40'), abs(xx - width / 2) / (width / 2)), dither=False) + (255,)
    return img


def text_lump():
    out = bytearray()
    offs = []
    for s in STORY:
        offs.append(len(out))
        out += s.encode('ascii') + b'\0'
    return bytes(out), offs


def build(out_path, sc_path):
    lumps = []
    info = {}

    def add(name, data):
        assert len(name) <= 8, name
        lumps.append((name, data))

    for land in range(4):
        imgs = art.layers(land)
        for n, (img, y) in enumerate(zip(imgs, (art.L0_Y, art.L1_Y, art.L2_Y))):
            data, inf = image_lump([img], y)
            add('BG%d_%d' % (land, n), data)
            info['BG%d_%d' % (land, n)] = inf
        ts = [t.image() for t in tiles.tile_set(land)]
        data, inf = image_lump(ts)
        add('TILES%d' % land, data)
        walk, fly, spec = sprites.CREATURES[land]
        frames = [walk(0), walk(1), walk(2), fly(0), fly(1), spec(0), spec(1)]
        data, inf = image_lump([f.image() for f in frames])
        add('CRIT%d' % land, data)
        data, inf = image_lump([sprites.BOSSES[land](f).image() for f in range(3)])
        add('BOSS%d' % land, data)
        data, inf = image_lump([sprites.platform(land)(0).image()])
        add('PLAT%d' % land, data)
        print('land', land, art.LANDS[land][0], 'done', flush=True)

    data, inf = image_lump([f().image() for _, f in sprites.WREN_FRAMES])
    add('WREN', data)
    for name, (fn, k) in sprites.SMALL.items():
        data, inf = image_lump([fn(f).image() for f in range(k)])
        add(name, data)
        info[name] = inf
    data, inf = image_lump([logo_lump()])
    add('LOGO', data)
    info['LOGO'] = inf
    add('FONT', font_lump())
    for i in range(8):
        data, grid, things = levels.build_level(i)
        add('LEVEL%d' % (i + 1), lz(data))
    tdata, toffs = text_lump()
    add('TEXT', tdata)

    for name, data in lumps:
        if name not in ('FONT', 'TEXT'):
            assert len(unlz(data)) == struct.unpack('<I', data[:4])[0]

    # the WAD
    body = bytearray()
    entries = []
    pos = 12
    for name, data in lumps:
        entries.append((pos, len(data), name))
        body += data
        pos += len(data)
        while pos % 4:
            body.append(0)
            pos += 1
    out = bytearray(b'PWAD' + struct.pack('<II', len(lumps), pos))
    out += body
    for p, n, name in entries:
        out += struct.pack('<II', p, n) + name.encode().ljust(8, b'\0')
    open(out_path, 'wb').write(out)
    print('%s: %d bytes, %d lumps' % (out_path, len(out), len(lumps)))
    for name, data in lumps:
        print('  %-8s %7d' % (name, len(data)))

    # the generated part of the script
    gen = ['// --- generated by tools/make_wad.py: do not edit by hand ---']
    for i, n in enumerate(tiles.NAMES):
        gen.append('#define T_%s %d' % (n, i))
    for i, n in enumerate(levels.THINGS):
        gen.append('#define K_%s %d' % (n, i))
    for i, (n, _) in enumerate(sprites.WREN_FRAMES):
        gen.append('#define W_%s %d' % (n, i))
    gen.append('#define TX_INTRO %d' % toffs[0])
    gen.append('#define TX_END %d' % toffs[1])
    gen.append('#define KEY 0x%04X' % KEY)
    gen.append('// --- end of the generated part ---')
    if sc_path and os.path.exists(sc_path):
        sc = open(sc_path).read()
        a = sc.index('// --- generated by tools/make_wad.py')
        b = sc.index('// --- end of the generated part ---') + len('// --- end of the generated part ---')
        sc = sc[:a] + '\n'.join(gen) + sc[b:]
        open(sc_path, 'w').write(sc)
        print('%s: generated part refreshed' % sc_path)
    else:
        print('\n'.join(gen))


if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', 'data', 'lumenfall.wad')
    sc = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, 'lumenfall.sc')
    build(out, sc)
