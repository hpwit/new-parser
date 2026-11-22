"""Builds wickwire.wad and refreshes the generated part of wickwire.sc.

    python make_wad.py [out.wad] [wickwire.sc]

The WAD ("PWAD", u32 count, u32 directory, 16-byte entries as Valmora's):

  PAL       256 colours, r g b bytes; the last 24 are the characters' flat
            colours (textures.FLAT)
  TEX       the textures, 32x32 a byte a pixel (a palette index), one after
            the other                                          (compressed)
  FONT      95 widths, then 8 row bytes a character (font6.py)
  TEXT      the story, zero-terminated strings
  LEVEL1..  a level                                            (compressed)

A level, unpacked (little endian; positions are int16 in 64ths of a unit):
  header, 32 int16: quads, boxes, spheres, things, zones, cells across,
  cells down, grid x0, grid z0, cell size, list length, fog r g b,
  sky top r g b, sky bottom r g b, fog near, fog far, stars, scenery
  (0 none, 1 mountains and hills, 2 a sea of clouds), birds, then 0s
  quads:   4 corners x, y, z; 4 x (u, v) in texels (32 a repeat); normal
           x, y, z (int8, 127 = 1); texture; light 0..15; flags
           (1 emissive, 2 a triangle)                       (46 bytes)
  boxes:   x0 y0 z0 x1 y1 z1, kind                          (13 bytes)
  spheres: x y z r pull                                     (10 bytes)
  things:  type, x y z, a b                                 (11 bytes)
  zones:   x0 y0 z0 x1 y1 z1, mode, yaw (degrees), distance, height,
           pitch (degrees)                                  (21 bytes)
  cells:   per cell (row by row) u16 first, u16 count in the list; then
           the list: u16 quad numbers

Compression as Lumenfall's: u32 size, then tokens (t < 128: t+1 bytes
follow; t >= 128: copy (t & 127) + 3 bytes from the u16 distance back).
"""
import os
import struct
import sys

from PIL import Image

import font6
import geometry
import levels
import textures

HERE = os.path.dirname(os.path.abspath(__file__))
CELL = 4.0
NTEXCOL = 232


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
            for p in reversed(head.get(bytes(data[i:i + 3]), [])[-32:]):
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


def vivid(pal):
    """Colours that pop: saturation up by half, a little brighter, the
    darks lifted (a toy-bright look rather than a murky one)."""
    import colorsys
    out = []
    for i in range(0, len(pal), 3):
        r, g, b = (c / 255.0 for c in pal[i:i + 3])
        h, l, s = colorsys.rgb_to_hls(r, g, b)
        s = min(1.0, s * 1.55 + 0.08)
        l = min(0.92, 0.1 + l * 1.02)
        r, g, b = colorsys.hls_to_rgb(h, l, s)
        out += [int(round(min(1, max(0, v)) * 255)) for v in (r, g, b)]
    return out


def palette_and_textures():
    imgs = [f().image() for _, f in textures.TEXTURES]
    sheet = Image.new('RGB', (32, 32 * len(imgs)))
    for i, im in enumerate(imgs):
        sheet.paste(im, (0, i * 32))
    q = sheet.quantize(colors=NTEXCOL, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = q.getpalette()[:NTEXCOL * 3]
    while len(pal) < NTEXCOL * 3:
        pal += [0, 0, 0]
    pal = vivid(pal)
    for _, c in textures.FLAT:
        pal += list(c)
    assert len(pal) == 256 * 3
    idx = bytes(q.getdata())
    return bytes(pal), idx


def i16(v):
    v = int(round(v * 64))
    assert -32768 <= v < 32768, v
    return v


def level_lump(w):
    quads = w.bake()
    xs = [p[0] for q in quads for p in q[0]]
    zs = [p[2] for q in quads for p in q[0]]
    gx0 = int((min(xs) - 1) // CELL * CELL)
    gz0 = int((min(zs) - 1) // CELL * CELL)
    gw = int((max(xs) - gx0) // CELL) + 1
    gh = int((max(zs) - gz0) // CELL) + 1
    cells = [[] for _ in range(gw * gh)]
    for i, (q, uv, tex, flags, n, lv) in enumerate(quads):
        cx = sum(p[0] for p in q) / 4
        cz = sum(p[2] for p in q) / 4
        ix = min(gw - 1, max(0, int((cx - gx0) // CELL)))
        iz = min(gh - 1, max(0, int((cz - gz0) // CELL)))
        cells[iz * gw + ix].append(i)
    lst = []
    table = bytearray()
    for c in cells:
        table += struct.pack('<HH', len(lst), len(c))
        lst += c
    assert len(quads) < 65536 and len(lst) < 65536

    hdr = [len(quads), len(w.boxes), len(w.spheres), len(w.things), len(w.zones), gw, gh, gx0, gz0, int(CELL),
           len(lst)] + list(w.fog) + list(w.sky[0]) + list(w.sky[1]) + [int(w.fog_near), int(w.fog_far), w.stars, w.scenery, w.birds]
    hdr += [0] * (32 - len(hdr))
    data = bytearray(struct.pack('<32h', *hdr))
    for (q, uv, tex, flags, n, lv) in quads:
        for p in q:
            data += struct.pack('<3h', i16(p[0]), i16(p[1]), i16(p[2]))
        for (u, v) in uv:
            data += struct.pack('<2h', int(round(u * 32)), int(round(v * 32)))
        data += struct.pack('<3b', *[max(-127, min(127, int(round(c * 127)))) for c in n])
        data += struct.pack('<3B', tex, lv, flags)
    for b in w.boxes:
        data += struct.pack('<6hB', *[i16(v) for v in b[:6]], b[6])
    for s in w.spheres:
        data += struct.pack('<5h', *[i16(v) for v in s])
    for t in w.things:
        data += struct.pack('<B3h2h', t[0], i16(t[1]), i16(t[2]), i16(t[3]), int(t[4]), int(t[5]))
    for z in w.zones:
        data += struct.pack('<6hBhhhh', *[i16(v) for v in z[:6]], z[6], int(round(z[7])), i16(z[8]), i16(z[9]),
                            int(round(z[10])))
    data += table
    for i in lst:
        data += struct.pack('<H', i)
    info = dict(quads=len(quads), boxes=len(w.boxes), things=len(w.things), grid=(gw, gh), raw=len(data))
    return lz(bytes(data)), info


def font_lump():
    widths = bytearray()
    rows = bytearray()
    for ch in range(32, 127):
        wd, g = font6.glyph(chr(ch))
        widths.append(wd + 1)
        for r in g:
            b = 0
            for i, v in enumerate(r[:8]):
                if v:
                    b |= 0x80 >> i
            rows.append(b)
    return bytes(widths + rows)


STORY = [
    "Every night the old keep on the hill lit its windows, one by one, "
    "until the whole hill glowed.|"
    "Then the Bell-Wraith woke in the tower and rang the dark in. The candles "
    "went out, the halls filled with ghosts, and the clockmaker's bombs "
    "began to walk on their own.|"
    "In the clockmaker's workshop a small brass fox wound himself up. "
    "His name is Wick, and there is a candle in his chest.|"
    "Light the candles. Climb to the bell. Ring the morning back.",
    "The Bell-Wraith unwinds into the night wind, and the great bell rings "
    "true.|"
    "One by one the windows of the keep light again, down the stairs, along "
    "the gallery, across the roofs, out to the little moons.|"
    "Wick sits on the bell and watches the sun come up. His candle can rest "
    "now.|"
    "THE END - thank you for playing Wickwire",
]


def text_lump():
    out = bytearray()
    offs = []
    for s in STORY:
        offs.append(len(out))
        out += s.encode('ascii') + b'\0'
    return bytes(out), offs


def build(out_path, sc_path):
    lumps = []
    pal, tex = palette_and_textures()
    lumps.append(('PAL', pal))
    lumps.append(('TEX', lz(tex)))
    lumps.append(('FONT', font_lump()))
    tdata, toffs = text_lump()
    lumps.append(('TEXT', tdata))
    for i, make in enumerate(levels.LEVELS):
        data, info = level_lump(make())
        lumps.append(('LEVEL%d' % (i + 1), data))
        print('level', i + 1, levels.NAMES[i], info, len(data), 'bytes', flush=True)
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
    out = bytearray(b'PWAD' + struct.pack('<II', len(lumps), pos)) + body
    for p, n, name in entries:
        out += struct.pack('<II', p, n) + name.encode().ljust(8, b'\0')
    open(out_path, 'wb').write(out)
    print('%s: %d bytes' % (out_path, len(out)))

    gen = ['// --- generated by tools/make_wad.py: do not edit by hand ---']
    for i, (n, _) in enumerate(textures.TEXTURES):
        gen.append('#define TX_%s %d' % (n, i))
    gen.append('#define NTEX %d' % len(textures.TEXTURES))
    for i, (n, _) in enumerate(textures.FLAT):
        gen.append('#define C_%s %d' % (n, NTEXCOL + i))
    for i, n in enumerate(geometry.THINGS):
        gen.append('#define K_%s %d' % (n, i))
    gen.append('#define NLEVELS %d' % len(levels.LEVELS))
    gen.append('#define STORY_INTRO %d' % toffs[0])
    gen.append('#define STORY_END %d' % toffs[1])
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
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', 'data', 'wickwire.wad')
    sc = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, 'wickwire.sc')
    build(out, sc)
