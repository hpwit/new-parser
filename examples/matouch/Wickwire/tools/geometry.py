"""Builds Wickwire's 3D levels out of parts, as textured quads.

A unit is a metre-ish: Wick is about 0.9 tall, a texture covers one unit
square. Every quad is cut into pieces of at most MAXP units, so the
script's affine texturing doesn't swim, and its near-plane clipping has
small polygons to deal with.

Lighting is baked into each quad: the moon (a direction), the sky
(ambient) and the candles and sconces (points with a warm reach), as a
level 0..15; emissive quads (windows, stained glass, flames) are always
full bright and don't fade in the fog.

Everything solid also goes in as an axis-aligned box (the script collides
with boxes); planetoids are spheres that pull Wick toward their centre.
"""
import math

import textures

TEX = {name: i for i, (name, _) in enumerate(textures.TEXTURES)}
EMISSIVE = {TEX['WINDOW'], TEX['GLASS_ROSE'], TEX['GLASS_SEA'], TEX['SCONCE'], TEX['STARSTONE']}
MAXP = 2.0

# what a level places (the script has the same numbers)
THINGS = ['NONE', 'START', 'GEAR', 'CRATE', 'CRATE_GEAR', 'CANDLE', 'GHOST', 'BOMB', 'KNIGHT',
          'BAT', 'SPIDER', 'KEY', 'DOOR', 'MOVER', 'FALLER', 'HEART', 'EXIT', 'BOSS', 'LAUNCH',
          'CHANDELIER', 'ARMOUR_DECO']
TH = {n: i for i, n in enumerate(THINGS)}

# camera modes of a zone
CAM_CHASE, CAM_SIDE, CAM_FIXED, CAM_PLANET = 0, 1, 2, 3

# collision box kinds
SOLID, HAZARD, WATER, KILL = 0, 1, 2, 3


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def mul(a, k):
    return (a[0] * k, a[1] * k, a[2] * k)


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def length(a):
    return math.sqrt(dot(a, a))


def norm(a):
    l = length(a) or 1.0
    return (a[0] / l, a[1] / l, a[2] / l)


def lerp(a, b, t):
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t)


class World:
    def __init__(self, name):
        self.name = name
        self.quads = []        # (p0, p1, p2, p3, uv0..uv3, tex, flags)
        self.boxes = []        # (x0, y0, z0, x1, y1, z1, kind)
        self.spheres = []      # (cx, cy, cz, r, pull radius)
        self.things = []       # (type, x, y, z, a, b)
        self.zones = []        # (x0, y0, z0, x1, y1, z1, mode, yaw, dist, height, pitch)
        self.lights = []       # (x, y, z, strength, reach)
        self.moon = norm((-0.4, 0.8, 0.45))
        self.ambient = 4.0
        self.sky = ((10, 12, 40), (60, 40, 80))
        self.fog = (20, 18, 40)
        self.fog_near = 10.0
        self.fog_far = 26.0
        self.stars = 1

    # ---------------------------------------------------- quads

    def quad(self, p0, p1, p2, p3, tex, u0=0.0, v0=0.0, us=None, vs=None, flags=0, maxp=MAXP, face=None):
        """A quad p0 p1 p2 p3: its front is the side its normal,
        cross(p1 - p0, p3 - p0), points to (`face`, a direction, turns it
        round when it points away). The texture runs u along p0->p1 and v
        along p0->p3, a repeat a unit, unless us/vs give the repeats over
        the whole quad."""
        if face is not None and dot(cross(sub(p1, p0), sub(p3, p0)), face) < 0:
            p0, p1, p2, p3 = p1, p0, p3, p2
        lu = length(sub(p1, p0))
        lv = length(sub(p3, p0))
        if us is None:
            us = lu
        if vs is None:
            vs = lv
        nu = max(1, int(math.ceil(lu / maxp - 1e-6)))
        nv = max(1, int(math.ceil(lv / maxp - 1e-6)))
        if tex in EMISSIVE:
            flags |= 1
        for j in range(nv):
            for i in range(nu):
                a, b = i / nu, (i + 1) / nu
                c, d = j / nv, (j + 1) / nv

                def P(s, t):
                    return lerp(lerp(p0, p1, s), lerp(p3, p2, s), t)
                q = (P(a, c), P(b, c), P(b, d), P(a, d))
                uv = ((u0 + us * a, v0 + vs * c), (u0 + us * b, v0 + vs * c),
                      (u0 + us * b, v0 + vs * d), (u0 + us * a, v0 + vs * d))
                self.quads.append((q, uv, tex, flags))

    def tri(self, p0, p1, p2, tex, uv=((0, 0), (1, 0), (0.5, 1)), flags=0):
        if tex in EMISSIVE:
            flags |= 1
        self.quads.append(((p0, p1, p2, p2), (uv[0], uv[1], uv[2], uv[2]), tex, flags | 2))

    # ---------------------------------------------------- parts

    def floor(self, x0, z0, x1, z1, y, tex, collide=True, thick=0.5):
        self.quad((x0, y, z0), (x0, y, z1), (x1, y, z1), (x1, y, z0), tex)
        if collide:
            self.boxes.append((x0, y - thick, z0, x1, y, z1, SOLID))

    def ceiling(self, x0, z0, x1, z1, y, tex):
        self.quad((x0, y, z0), (x1, y, z0), (x1, y, z1), (x0, y, z1), tex)

    def wall(self, x0, z0, x1, z1, y0, y1, tex, collide=True, both=False, thick=0.3, flags=0, u0=0.0):
        """A vertical wall from (x0,z0) to (x1,z1); its front faces left of
        the way from the first point to the second."""
        self.quad((x0, y0, z0), (x1, y0, z1), (x1, y1, z1), (x0, y1, z0), tex, u0=u0, flags=flags)
        if both:
            self.quad((x1, y0, z1), (x0, y0, z0), (x0, y1, z0), (x1, y1, z1), tex, flags=flags)
        if collide:
            ax0, ax1 = min(x0, x1), max(x0, x1)
            az0, az1 = min(z0, z1), max(z0, z1)
            if ax1 - ax0 < thick:
                ax0 -= thick / 2
                ax1 += thick / 2
            if az1 - az0 < thick:
                az0 -= thick / 2
                az1 += thick / 2
            self.boxes.append((ax0, y0, az0, ax1, y1, az1, SOLID))

    def box(self, x0, y0, z0, x1, y1, z1, tex, top=None, bottom=False, collide=True, kind=SOLID,
            sides=(1, 1, 1, 1)):
        """A solid block; `top` its top's texture, sides (−z, +x, +z, −x)."""
        top = tex if top is None else top
        self.quad((x0, y1, z0), (x0, y1, z1), (x1, y1, z1), (x1, y1, z0), top)
        if bottom:
            self.quad((x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1), tex)
        if sides[0]:
            self.quad((x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0), tex)
        if sides[1]:
            self.quad((x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1), tex)
        if sides[2]:
            self.quad((x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1), tex)
        if sides[3]:
            self.quad((x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0), tex)
        if collide:
            self.boxes.append((x0, y0, z0, x1, y1, z1, kind))

    def stairs(self, x0, z0, width, steps, rise, run, y0, axis='z', tex='STONE', top='FLAGS'):
        """A straight flight going up toward +z (axis 'z') or +x."""
        for i in range(steps):
            y1 = y0 + rise * (i + 1)
            if axis == 'z':
                self.box(x0, y0, z0 + i * run, x0 + width, y1, z0 + (i + 1) * run, TEX[tex], TEX[top],
                         sides=(1, 1, 0, 1))
            else:
                self.box(x0 + i * run, y0, z0, x0 + (i + 1) * run, y1, z0 + width, TEX[tex], TEX[top],
                         sides=(1, 1, 1, 0))

    def spiral(self, cx, cz, r0, r1, y0, steps, a0, da, rise, tex='STONE', top='FLAGS'):
        """A spiral stair round a column: each step a wedge (and a box to stand on)."""
        for i in range(steps):
            a = a0 + da * i
            b = a + da
            y = y0 + rise * (i + 1)
            ca, sa, cb, sb = math.cos(a), math.sin(a), math.cos(b), math.sin(b)
            p = [(cx + ca * r0, y, cz + sa * r0), (cx + ca * r1, y, cz + sa * r1),
                 (cx + cb * r1, y, cz + sb * r1), (cx + cb * r0, y, cz + sb * r0)]
            self.quad(p[0], p[1], p[2], p[3], TEX[top], face=(0, 1, 0), us=1, vs=1)
            # the riser faces down the stair; the outer edge faces out
            yb = y - rise
            down = (math.sin(a), 0, -math.cos(a)) if da > 0 else (-math.sin(a), 0, math.cos(a))
            self.quad((cx + ca * r0, yb, cz + sa * r0), (cx + ca * r1, yb, cz + sa * r1),
                      (cx + ca * r1, y, cz + sa * r1), (cx + ca * r0, y, cz + sa * r0), TEX[tex], face=down)
            m = (a + b) / 2
            self.quad((cx + ca * r1, y - 0.6, cz + sa * r1), (cx + cb * r1, y - 0.6, cz + sb * r1),
                      (cx + cb * r1, y, cz + sb * r1), (cx + ca * r1, y, cz + sa * r1), TEX[tex],
                      face=(math.cos(m), 0, math.sin(m)))
            # standing room: a box round the step's middle
            rm = (r0 + r1) / 2
            mx, mz = cx + math.cos(m) * rm, cz + math.sin(m) * rm
            h = (r1 - r0) * 0.5
            self.boxes.append((mx - h, y - 0.4, mz - h, mx + h, y, mz + h, SOLID))

    def cylinder(self, cx, cz, r, y0, y1, n, tex, top=None, collide=True, inside=False, ureps=None):
        ureps = ureps or max(1, int(round(2 * math.pi * r)))
        for i in range(n):
            a, b = 2 * math.pi * i / n, 2 * math.pi * (i + 1) / n
            pa = (cx + math.cos(a) * r, cz + math.sin(a) * r)
            pb = (cx + math.cos(b) * r, cz + math.sin(b) * r)
            u0 = ureps * i / n
            if inside:
                self.quad((pa[0], y0, pa[1]), (pb[0], y0, pb[1]), (pb[0], y1, pb[1]), (pa[0], y1, pa[1]), tex,
                          u0=u0, us=ureps / n)
            else:
                self.quad((pb[0], y0, pb[1]), (pa[0], y0, pa[1]), (pa[0], y1, pa[1]), (pb[0], y1, pb[1]), tex,
                          u0=u0, us=ureps / n)
            if top is not None:
                self.tri((cx, y1, cz), (pb[0], y1, pb[1]), (pa[0], y1, pa[1]), top)
        if collide and not inside:
            k = r * 0.8
            self.boxes.append((cx - k, y0, cz - k, cx + k, y1, cz + k, SOLID))

    def cone(self, cx, cz, r, y0, h, n, tex):
        for i in range(n):
            a, b = 2 * math.pi * i / n, 2 * math.pi * (i + 1) / n
            pa = (cx + math.cos(a) * r, y0, cz + math.sin(a) * r)
            pb = (cx + math.cos(b) * r, y0, cz + math.sin(b) * r)
            # a slim quad with its top pinched: the slates keep their size
            top = (cx, y0 + h, cz)
            ta = lerp(pa, top, 0.98)
            tb = lerp(pb, top, 0.98)
            self.quad(pb, pa, ta, tb, tex, us=max(1, 2 * math.pi * r / n), vs=math.hypot(r, h))

    def sphere(self, cx, cy, cz, r, n, tex, pull=None):
        rings = n // 2
        for j in range(rings):
            t0, t1 = math.pi * j / rings, math.pi * (j + 1) / rings
            for i in range(n):
                a, b = 2 * math.pi * i / n, 2 * math.pi * (i + 1) / n

                def P(t, p):
                    return (cx + r * math.sin(t) * math.cos(p), cy + r * math.cos(t), cz + r * math.sin(t) * math.sin(p))
                p0, p1, p2, p3 = P(t0, a), P(t0, b), P(t1, b), P(t1, a)
                us = max(1, 2 * math.pi * r / n * math.sin((t0 + t1) / 2))
                vs = math.pi * r / rings
                if j == 0:
                    self.tri(p0, p2, p3, tex)
                elif j == rings - 1:
                    self.tri(p0, p1, p3, tex)
                else:
                    self.quad(p0, p1, p2, p3, tex, us=us, vs=vs, maxp=99)
        self.spheres.append((cx, cy, cz, r, pull or r * 2.2))

    def battlements(self, x0, z0, x1, z1, y, tex, merlon=0.8, gap=0.8, h=0.7, thick=0.5):
        """Merlons along a wall top from (x0,z0) to (x1,z1)."""
        L = math.hypot(x1 - x0, z1 - z0)
        dx, dz = (x1 - x0) / L, (z1 - z0) / L
        t = 0.0
        while t + merlon <= L + 1e-6:
            ax, az = x0 + dx * t, z0 + dz * t
            bx, bz = ax + dx * merlon, az + dz * merlon
            nx, nz = -dz * thick, dx * thick
            self.box(min(ax, bx, ax + nx, bx + nx), y, min(az, bz, az + nz, bz + nz),
                     max(ax, bx, ax + nx, bx + nx), y + h, max(az, bz, az + nz, bz + nz), tex)
            t += merlon + gap

    def light(self, x, y, z, strength=9.0, reach=6.0):
        self.lights.append((x, y, z, strength, reach))

    def sconce(self, x, y, z, nx, nz):
        """A lit sconce on a wall facing (nx, nz): a glowing tile and a light."""
        px, pz = -nz * 0.5, nx * 0.5
        o = 0.02
        self.quad((x - px + nx * o, y - 0.5, z - pz + nz * o), (x + px + nx * o, y - 0.5, z + pz + nz * o),
                  (x + px + nx * o, y + 0.5, z + pz + nz * o), (x - px + nx * o, y + 0.5, z - pz + nz * o),
                  TEX['SCONCE'], us=1, vs=1)
        self.light(x + nx * 0.6, y, z + nz * 0.6, 10.0, 6.5)

    def picture(self, x, y, z, nx, nz, tex, w=1.4, h=1.4):
        px, pz = -nz * w / 2, nx * w / 2
        o = 0.03
        self.quad((x - px + nx * o, y, z - pz + nz * o), (x + px + nx * o, y, z + pz + nz * o),
                  (x + px + nx * o, y + h, z + pz + nz * o), (x - px + nx * o, y + h, z - pz + nz * o),
                  TEX[tex], us=1, vs=1)

    def thing(self, kind, x, y, z, a=0, b=0):
        self.things.append((TH[kind], x, y, z, a, b))

    def zone(self, x0, y0, z0, x1, y1, z1, mode, yaw=0.0, dist=5.0, height=2.6, pitch=0.0):
        self.zones.append((x0, y0, z0, x1, y1, z1, mode, yaw, dist, height, pitch))

    # ---------------------------------------------------- lighting

    def bake(self):
        out = []
        for (q, uv, tex, flags) in self.quads:
            n = norm(cross(sub(q[1], q[0]), sub(q[3], q[0]))) if not flags & 2 else \
                norm(cross(sub(q[1], q[0]), sub(q[2], q[0])))
            c = mul(add(add(q[0], q[1]), add(q[2], q[3])), 0.25)
            if flags & 1:
                lv = 15
            else:
                v = self.ambient + max(0.0, dot(n, self.moon)) * 6.0
                for (lx, ly, lz, s, reach) in self.lights:
                    d = sub((lx, ly, lz), c)
                    dl = length(d)
                    if dl < reach:
                        facing = max(0.15, dot(n, mul(d, 1.0 / (dl or 1))))
                        v += s * (1 - dl / reach) ** 1.5 * facing
                lv = int(max(1, min(14, round(v))))
            out.append((q, uv, tex, flags, n, lv))
        return out
