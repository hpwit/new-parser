"""Wickwire's levels, built with geometry.World.

Each level runs mostly toward +z (into the screen), the camera chasing
behind; zones turn the camera where the way turns, look from the side on
ledges, and orbit the planetoids.
"""
import math

from geometry import World, TEX, SOLID, KILL, WATER, CAM_CHASE, CAM_SIDE, CAM_FIXED, CAM_PLANET

T = TEX


def gear_line(w, x0, y, z0, x1, z1, n):
    for i in range(n):
        t = i / max(1, n - 1)
        w.thing('GEAR', x0 + (x1 - x0) * t, y, z0 + (z1 - z0) * t)


def courtyard():
    """1: The courtyard at dusk: a drawbridge over the moat, the gatehouse,
    a walled yard of grass and flagstones, stairs to the wall walk."""
    w = World('The Courtyard')
    w.sky = ((30, 60, 200), (255, 140, 120))
    w.fog = (150, 110, 190)
    w.fog_near, w.fog_far = 12.0, 30.0
    w.ambient = 7.0
    w.moon = (-0.35, 0.75, 0.55)

    # the approach: a flagstone path between grass banks
    w.floor(-3, -4, 3, 12, 0, T['FLAGS'])
    w.floor(-12, -4, -3, 12, 0, T['GRASS'])
    w.floor(3, -4, 12, 12, 0, T['GRASS'])
    for x in (-3.3, 3.0):
        w.box(x, 0, -4, x + 0.3, 0.6, 12, T['STONE'], T['MOSS_STONE'])
    w.thing('START', 0, 0, -2)
    gear_line(w, 0, 0.6, 2, 0, 10, 5)
    # the approach: trees on the banks
    for (x, z) in ((-7, -1), (8, 2), (-9, 7), (6.5, 9)):
        w.tree(x, z, h=3.2)
    for (x, z) in ((-5, 4), (5, 0), (-4.6, 10.5), (9.5, 5.5)):
        w.bush(x, z)
    w.thing('CRATE', -1.5, 0, 6)
    w.thing('CRATE', 1.5, 0, 8)

    # the moat and the drawbridge
    w.quad((-14, -1.6, 12), (-14, -1.6, 18), (14, -1.6, 18), (14, -1.6, 12), T['WATER'])
    w.boxes.append((-14, -4, 12, 14, -1.2, 18, KILL))
    for x0, x1 in ((-14, -2), (2, 14)):
        w.wall(x0, 12, x1, 12, -1.6, 0, T['MOSS_STONE'], collide=False)
        w.wall(x1, 18, x0, 18, -1.6, 0, T['MOSS_STONE'], collide=False)
    w.box(-2, -0.3, 12, 2, 0, 18, T['WOOD'], T['WOOD'])
    for z in (12.2, 17.5):
        for x in (-2.2, 1.9):
            w.box(x, 0, z, x + 0.3, 1.2, z + 0.3, T['IRON'])
    w.thing('GHOST', 0, 1.2, 15)

    # the gatehouse: two walls, an arch, round towers with slate cones
    w.box(-14, 0, 18, -2.5, 8, 22, T['STONE'])
    w.box(2.5, 0, 18, 14, 8, 22, T['STONE'])
    w.box(-2.5, 5, 18, 2.5, 8, 22, T['STONE'], bottom=True)
    w.floor(-2.5, 18, 2.5, 22, 0, T['FLAGS'])
    w.wall(-2.5, 18, -2.5, 22, 0, 5, T['STONE'], collide=False)
    w.wall(2.5, 22, 2.5, 18, 0, 5, T['STONE'], collide=False)
    w.battlements(-14, 18, -2.5, 18, 8, T['STONE'])
    w.battlements(2.5, 18, 14, 18, 8, T['STONE'])
    for x in (-14, 14):
        w.cylinder(x, 20, 3, 0, 11, 12, T['STONE'])
        w.cone(x, 20, 3.6, 11, 5, 12, T['ROOF'])
        w.picture(x - (1 if x > 0 else -1) * 0.0, 7, 20 - 3.05, 0, -1, 'WINDOW', 0.8, 1.4)
    w.sconce(-2.6, 3.0, 17.9, 0, -1)
    w.sconce(2.6, 3.0, 17.9, 0, -1)
    w.picture(-0.9, 5.4, 17.95, 0, -1, 'BANNER', 1.8, 2.2)

    # the courtyard: grass with a flagstone cross, high walls all round
    w.floor(-14, 22, 14, 46, 0, T['GRASS'])
    w.quad((-2, 0.01, 22), (-2, 0.01, 46), (2, 0.01, 46), (2, 0.01, 22), T['FLAGS'])
    w.quad((-14, 0.01, 32), (-14, 0.01, 36), (14, 0.01, 36), (14, 0.01, 32), T['FLAGS'])
    w.wall(-14, 46, -14, 22, 0, 7, T['STONE'])
    w.wall(14, 22, 14, 46, 0, 7, T['STONE'])
    w.battlements(-14, 22, -14, 46, 7, T['STONE'])
    w.battlements(14, 46, 14, 22, 7, T['STONE'])
    for z in (26, 34, 42):
        w.sconce(-13.9, 3, z, 1, 0)
        w.sconce(13.9, 3, z, -1, 0)
    # a well in the middle
    w.cylinder(0, 34, 1.4, 0, 1.0, 12, T['STONE'], top=None)
    w.cylinder(0, 34, 1.1, 0.2, 1.0, 12, T['STONE_DARK'], collide=False, inside=True)
    for a in (0, math.pi):
        x = math.cos(a) * 1.3
        w.box(x - 0.12, 1, 33.9, x + 0.12, 2.6, 34.1, T['WOOD'])
    w.box(-1.4, 2.6, 33.8, 1.4, 2.8, 34.2, T['WOOD'])
    w.cone(0, 34, 1.8, 2.8, 1.2, 8, T['ROOF'])
    # crates, gears, the yard's ghosts and a bomb
    for (x, z, k) in ((-8, 26, 'CRATE'), (-7, 26, 'CRATE'), (-7.5, 26.9, 'CRATE_GEAR'), (8, 30, 'CRATE'),
                      (9, 40, 'CRATE'), (-9, 42, 'CRATE_GEAR')):
        w.thing(k, x, 0, z)
    gear_line(w, -6, 0.6, 34, 6, 34, 7)
    gear_line(w, 0, 0.6, 37, 0, 44, 4)
    w.thing('CANDLE', 2.6, 0, 23)
    w.thing('GHOST', -5, 1.2, 30)
    w.thing('GHOST', 6, 1.2, 38)
    w.thing('BOMB', 4, 0, 42)
    w.thing('HEART', -10, 0.6, 44)
    # trees, bushes and barrels about the yard, lamps along the path
    for (x, z, h) in ((-10, 24.5, 3.4), (10.5, 25, 3.0), (-11, 39, 3.6), (11, 42, 3.2), (-6.5, 44, 2.6)):
        w.tree(x, z, h=h, r=1.1 + h * 0.08)
    for (x, z) in ((-12.5, 30), (12.5, 33), (-4.5, 23.5), (4.5, 23.5), (12.6, 37.5), (-12.6, 36)):
        w.bush(x, z, r=0.55)
    for (x, z) in ((7, 44.5), (7.9, 44.8), (-12.8, 27.5), (12.8, 45)):
        w.barrel(x, z)
    for z in (27, 40):
        w.lamp_post(-2.6, z)
        w.lamp_post(2.6, z)

    # the keep's front: a wall across with stairs up to the wall walk
    w.box(-14, 0, 46, -3, 4, 50, T['STONE'], T['FLAGS'])
    w.box(3, 0, 46, 14, 4, 50, T['STONE'], T['FLAGS'])
    w.stairs(-3, 46, 6, 8, 0.5, 0.5, 0, axis='z', tex='RED_BRICK', top='FLAGS')
    w.lamp_post(-3.6, 45.4)
    w.lamp_post(3.6, 45.4)
    w.floor(-3, 50, 3, 50.5, 4, T['FLAGS'])
    w.wall(-14, 50, -14, 46, 4, 10, T['STONE'])
    w.wall(14, 46, 14, 50, 4, 10, T['STONE'])

    # the wall walk: a raised way over the dry ditch, broken in places
    w.boxes.append((-30, -8, 50, 30, -3, 100, KILL))
    w.quad((-14, -3, 50), (-14, -3, 100), (14, -3, 100), (14, -3, 50), T['STONE_DARK'])
    segs = [(50.5, 58), (60, 66), (68.5, 72), (74, 82), (84.5, 92)]
    for i, (z0, z1) in enumerate(segs):
        w.box(-2, -3, z0, 2, 4, z1, T['STONE'], T['FLAGS'])
        w.battlements(-2, z1, -2, z0, 4, T['STONE'], merlon=0.6, gap=0.9, h=0.6, thick=0.35)
        w.battlements(2, z0, 2, z1, 4, T['STONE'], merlon=0.6, gap=0.9, h=0.6, thick=0.35)
        gear_line(w, 0, 4.7, z0 + 1, 0, z1 - 1, max(2, int((z1 - z0) / 2)))
    w.thing('CANDLE', 0, 4, 62)
    w.thing('BOMB', 0, 4, 76)
    w.thing('GHOST', 0, 5.5, 67)
    w.thing('MOVER', 0, 4, 82.8, 0, 2)
    w.thing('CRATE_GEAR', 1, 4, 88)
    w.thing('BOMB', -1, 4, 90)
    # the side walls of the ditch, and the keep beyond
    w.wall(-14, 100, -14, 50, -3, 4, T['STONE_DARK'])
    w.wall(14, 50, 14, 100, -3, 4, T['STONE_DARK'])
    w.box(-14, -3, 92, 14, 4, 104, T['STONE'], T['FLAGS'])
    w.box(-14, 4, 100, 14, 14, 104, T['STONE'])
    w.battlements(-14, 100, 14, 100, 14, T['STONE'])
    w.picture(-1.5, 4, 99.95, 0, -1, 'DOOR', 3, 4)
    w.sconce(-2.6, 6.5, 99.9, 0, -1)
    w.sconce(2.6, 6.5, 99.9, 0, -1)
    for x in (-10, 10):
        w.picture(x - 0.5, 8, 99.95, 0, -1, 'GLASS_ROSE', 1.0, 2.0)
    for x in (-14, 14):
        w.cylinder(x, 102, 3.2, 4, 18, 12, T['STONE'])
        w.cone(x, 102, 3.8, 18, 6, 12, T['ROOF'])
    w.thing('EXIT', 0, 4, 98)

    w.zone(-40, -10, -10, 40, 30, 110, CAM_CHASE, 0.0, 5.0, 2.6)
    return w


def hall_walls(w, x0, x1, z0, z1, h, tex=T['STONE'], windows=None, deco=True):
    """Two long walls along z, sconces and pictures on them."""
    w.wall(x0, z1, x0, z0, 0, h, tex)
    w.wall(x1, z0, x1, z1, 0, h, tex)
    z = z0 + 3
    i = 0
    while deco and z < z1 - 2:
        k = i % 3
        if k == 0:
            w.sconce(x0 + 0.05, 2.6, z, 1, 0)
            w.sconce(x1 - 0.05, 2.6, z, -1, 0)
        elif k == 1:
            pic = 'PORTRAIT1' if i % 2 else 'PORTRAIT2'
            w.picture(x0 + 0.02, 1.6, z, 1, 0, pic, 1.4, 1.6)
            w.picture(x1 - 0.02, 1.6, z, -1, 0, 'BANNER', 1.2, 2.6)
        else:
            win = windows or 'WINDOW'
            w.picture(x0 + 0.02, 2.0, z, 1, 0, win, 1.2, 2.6)
            w.picture(x1 - 0.02, 2.0, z, -1, 0, win, 1.2, 2.6)
        z += 4
        i += 1


def staircase():
    """2: The grand staircase: a marble hall of pillars, banners and
    suits of armour, stairs up to a gallery, a locked door, and a spiral
    stair round a column to the top of the tower."""
    w = World('The Grand Staircase')
    w.sky = ((30, 20, 90), (120, 60, 140))
    w.fog = (90, 50, 110)
    w.fog_near, w.fog_far = 14.0, 30.0
    w.ambient = 6.0
    w.stars = 0
    w.scenery = 0
    w.birds = 0

    # the hall
    w.floor(-9, -2, 9, 34, 0, T['MARBLE'])
    w.quad((-1.5, 0.01, -2), (-1.5, 0.01, 30), (1.5, 0.01, 30), (1.5, 0.01, -2), T['CARPET'], us=1, vs=16)
    hall_walls(w, -9, 9, -2, 34, 9)
    w.wall(-9, -2, 9, -2, 0, 9, T['STONE'])
    w.ceiling(-9, -2, 9, 34, 9, T['WOOD'])
    for z in range(3, 30, 6):
        for x in (-5, 5):
            w.cylinder(x, z, 0.55, 0, 9, 8, T['PILLAR'])
            w.box(x - 0.8, 0, z - 0.8, x + 0.8, 0.4, z + 0.8, T['MARBLE'])
    w.thing('START', 0, 0, 0)
    for z in (6, 18, 28):
        w.thing('CHANDELIER', 0, 6.5, z)
        w.light(0, 6, z, 8.0, 9.0)
    for z in (4, 10, 16, 22):
        w.thing('ARMOUR_DECO', -7.6, 0, z, 256)
        w.thing('ARMOUR_DECO', 7.6, 0, z, 768)
    gear_line(w, 0, 0.6, 3, 0, 12, 5)
    gear_line(w, -3, 0.6, 14, 3, 14, 4)
    w.thing('KNIGHT', -2.5, 0, 12, 0)
    w.thing('KNIGHT', 2.5, 0, 22, 1)
    w.thing('CRATE', -7, 0, 26)
    w.thing('CRATE_GEAR', 7, 0, 26)
    w.thing('CANDLE', -3, 0, 20)
    w.thing('HEART', 7, 0.6, 12)

    # the stairs up to the gallery
    w.stairs(-3, 26, 6, 8, 0.5, 0.75, 0, axis='z', tex='MARBLE', top='CARPET')
    w.box(-9, 0, 26, -3, 4, 34, T['STONE'], T['MARBLE'])
    w.box(3, 0, 26, 9, 4, 34, T['STONE'], T['MARBLE'])
    w.thing('KEY', 7, 4.2, 30)
    w.thing('CRATE', -7, 4, 31)
    w.thing('BAT', 0, 7, 30)

    # the door into the tower, and the tower: a round room, a spiral stair
    w.box(-9, 4, 34, -1.5, 9, 35, T['STONE'])
    w.box(1.5, 4, 34, 9, 9, 35, T['STONE'])
    w.box(-1.5, 7.5, 34, 1.5, 9, 35, T['STONE'], bottom=True)
    w.thing('DOOR', 0, 4, 34.5, 0)
    cx, cz = 0, 42
    w.floor(-6, 35, 6, 49, 4, T['FLAGS'])
    w.cylinder(cx, cz, 6.2, 4, 22, 20, T['STONE'], collide=False, inside=True)
    for a in range(0, 360, 30):
        r = math.radians(a)
        k = 5.9
        w.boxes.append((cx + math.cos(r) * k - 0.6, 4, cz + math.sin(r) * k - 0.6,
                        cx + math.cos(r) * k + 0.6, 22, cz + math.sin(r) * k + 0.6, SOLID))
    w.cylinder(cx, cz, 1.0, 4, 22, 10, T['PILLAR'])
    w.spiral(cx, cz, 1.1, 4.0, 4, 30, math.pi * 0.5, 0.32, 0.5, tex='STONE', top='FLAGS')
    for i in range(0, 30, 3):
        a = math.pi * 0.5 + 0.32 * i + 0.16
        w.thing('GEAR', cx + math.cos(a) * 2.6, 4 + 0.5 * (i + 1) + 0.6, cz + math.sin(a) * 2.6)
    for a, y in ((0.6, 9), (2.4, 13), (4.2, 17)):
        w.picture(cx + math.cos(a) * 6.1, y, cz + math.sin(a) * 6.1, -math.cos(a), -math.sin(a), 'GLASS_ROSE', 1.2, 2.4)
        w.light(cx + math.cos(a) * 5, y + 1, cz + math.sin(a) * 5, 6.0, 6.0)
    w.thing('SPIDER', cx + 2.5, 14, cz + 1)
    w.thing('SPIDER', cx - 2.5, 18, cz - 1)
    w.thing('CANDLE', 2, 4, 37)
    # the top: a landing, the exit
    top = 4 + 0.5 * 30
    w.floor(-6, 35, 6, 49, top, T['FLAGS'], collide=False)
    w.boxes.append((-6, top - 0.5, 35, 6, top, 39.5, SOLID))
    w.thing('EXIT', 0, top, 37)
    w.zone(-40, -10, -10, 40, 40, 34, CAM_CHASE, 0.0, 5.0, 2.8)
    w.zone(-8, 3, 34, 8, 40, 50, CAM_CHASE, 0.0, 4.0, 3.6)
    return w


def gallery():
    """3: The ghost gallery: a long hall of portraits and stained glass,
    its floor fallen in places; ghosts drift through the walls."""
    w = World('The Ghost Gallery')
    w.sky = ((20, 30, 110), (80, 90, 200))
    w.fog = (60, 70, 170)
    w.fog_near, w.fog_far = 10.0, 26.0
    w.ambient = 5.5
    w.stars = 0
    w.scenery = 0
    w.birds = 0
    L = 96
    w.wall(-4, L, -4, 0, -6, 6, T['STONE_DARK'])
    w.wall(4, 0, 4, L, -6, 6, T['STONE_DARK'])
    w.wall(-4, 0, 4, 0, 0, 6, T['STONE_DARK'])
    w.ceiling(-4, 0, 4, L, 6, T['WOOD'])
    hall_walls(w, -4, 4, 0, L, 6, T['STONE_DARK'], windows='GLASS_SEA', deco=False)
    # the floor: solid stretches and holes over the dark
    solid = [(0, 14), (18, 26), (30, 34), (44, 52), (56, 58), (62, 70), (82, 96)]
    for z0, z1 in solid:
        w.box(-4, -1, z0, 4, 0, z1, T['WOOD'], T['WOOD'], sides=(1, 0, 1, 0))
    w.boxes.append((-10, -12, 0, 10, -5, L, KILL))
    w.quad((-4, -6, 0), (-4, -6, L), (4, -6, L), (4, -6, 0), T['STONE_DARK'])
    for z in range(2, L - 2, 5):
        side = -1 if (z // 5) % 2 else 1
        tex = ['PORTRAIT1', 'GLASS_ROSE', 'PORTRAIT2', 'WINDOW', 'BOOKS'][(z // 5) % 5]
        w.picture(side * 3.98, 1.4, z, -side, 0, tex, 1.6, 2.2)
        if (z // 5) % 3 == 0:
            w.sconce(-side * 3.95, 2.8, z + 2, side, 0)
    for z in (6, 46, 86):
        w.box(-4, 0, z, -3.2, 2.2, z + 2.5, T['BOOKS'], T['WOOD'])
        w.box(3.2, 0, z, 4, 2.2, z + 2.5, T['BOOKS'], T['WOOD'])
    w.thing('START', 0, 0, 2)
    gear_line(w, 0, 0.6, 4, 0, 12, 5)
    w.thing('GHOST', 0, 1.2, 10)
    # crossing: a moving platform, fallers, more ghosts
    w.thing('MOVER', 0, 0, 16, 0, 1)
    gear_line(w, 0, 1.0, 15, 0, 17, 2)
    w.thing('CANDLE', -2.5, 0, 20)
    w.thing('GHOST', -2, 1.2, 22)
    w.thing('FALLER', 0, 0, 28)
    w.thing('FALLER', 1, 0, 37)
    w.thing('FALLER', -1, 0, 40)
    w.thing('GHOST', 1, 1.6, 38)
    gear_line(w, 0, 1.0, 28, 0, 42, 6)
    w.thing('CRATE_GEAR', 2.5, 0, 46)
    w.thing('BOMB', 0, 0, 50)
    w.thing('GHOST', 2, 1.2, 49)
    w.thing('MOVER', 0, 0, 60, 2, 0)
    w.thing('HEART', 0, 0.6, 57)
    w.thing('CANDLE', 2.5, 0, 64)
    w.thing('MOVER', 0, 0, 72, 0, 2)
    w.thing('FALLER', 0, 0, 78)
    w.thing('GHOST', 0, 1.4, 75)
    w.thing('GHOST', -2, 1.4, 88)
    w.thing('KNIGHT', 0, 0, 89, 0)
    gear_line(w, -2, 0.6, 84, 2, 84, 4)
    w.thing('EXIT', 0, 0, 94)
    w.zone(-40, -20, -10, 40, 40, 110, CAM_CHASE, 0.0, 4.6, 2.4)
    return w


def roof_block(w, x0, z0, x1, z1, y, ridge=1.6):
    """A roofed house top: a flat walk along the ridge, slates both sides."""
    xm = (x0 + x1) / 2
    w.box(x0, y - 4, z0, x1, y, z1, T['STONE'], T['ROOF'])
    # sloped slates either side of a walkway 1.6 wide
    for side in (-1, 1):
        xa = xm + side * 0.8
        xb = x1 if side > 0 else x0
        if side > 0:
            w.quad((xa, y + ridge, z0), (xa, y + ridge, z1), (xb, y, z1), (xb, y, z0), T['ROOF'])
        else:
            w.quad((xb, y, z0), (xb, y, z1), (xa, y + ridge, z1), (xa, y + ridge, z0), T['ROOF'])
    w.box(xm - 0.8, y, z0, xm + 0.8, y + ridge, z1, T['RED_BRICK'], T['FLAGS'])


def rooftops():
    """4: The rooftops: slate ridges to run along, chimneys, towers, gaps
    over the dark yards, bats and bombs, the moon high up."""
    w = World('The Rooftops')
    w.sky = ((20, 30, 140), (200, 110, 220))
    w.fog = (120, 90, 200)
    w.fog_near, w.fog_far = 14.0, 30.0
    w.ambient = 7.0
    w.moon = (-0.4, 0.8, 0.45)
    w.boxes.append((-60, -30, -20, 60, -6, 160, KILL))
    blocks = [(-3, 0, 3, 16, 0), (-3, 19, 3, 30, 0.5), (-3, 33, 3, 44, 1.5), (-3, 48, 3, 56, 1.0),
              (-3, 66, 3, 80, 2.0), (-3, 84, 3, 100, 2.5)]
    for (x0, z0, x1, z1, dy) in blocks:
        roof_block(w, x0, z0, x1, z1, dy)
    # neighbours to either side, for the skyline
    for z in range(0, 100, 12):
        for side in (-1, 1):
            x = side * 9
            h = 1 + (z // 12) % 3
            w.box(x - 3, -6, z, x + 3, h, z + 9, T['STONE'], T['ROOF'], collide=False)
            if (z // 12) % 2:
                w.box(x - 0.6, h, z + 3, x + 0.6, h + 2, z + 4.2, T['RED_BRICK'], collide=False)
    for z in (8, 40, 76):
        side = 1.6 if (z // 8) % 2 else -1.6
        w.box(side - 0.5, 1.5, z, side + 0.5, 4.6, z + 1.0, T['RED_BRICK'], T['STONE_DARK'])
    for (x, z) in ((-6, 18), (6, 46), (-6, 64), (6, 82)):
        w.tree(x, z, y=-6, h=7.0, r=1.8)
    for (x, z) in ((-12, 30), (12, 62), (-12, 92)):
        w.cylinder(x, z, 2.5, -6, 12, 12, T['STONE'], collide=False)
        w.cone(x, z, 3.0, 12, 5, 12, T['ROOF'])
        w.picture(x - (2.55 if x < 0 else -2.55) * 0, 8, z - 2.5, 0, -1, 'WINDOW', 0.8, 1.6)
    w.thing('START', 0, 1.6, 1)
    w.thing('MOVER', 0, 2.6, 60.5, 2, 0)
    for (x0, z0, x1, z1, dy) in blocks:
        gear_line(w, 0, dy + 2.2, z0 + 1.5, 0, z1 - 1.5, max(2, int((z1 - z0) / 3)))
    w.thing('BAT', 0, 5, 24)
    w.thing('BOMB', 0, 3.1, 38)
    w.thing('CANDLE', 0.4, 2.6, 50)
    w.thing('BAT', 1, 6, 70)
    w.thing('BOMB', 0, 4.1, 74)
    w.thing('CRATE_GEAR', 0, 4.1, 86)
    w.thing('BAT', -1, 6, 90)
    w.thing('HEART', 0, 4.6, 92)
    w.thing('EXIT', 0, 4.1, 98)
    w.zone(-60, -30, -20, 60, 40, 160, CAM_CHASE, 0.0, 5.0, 2.8)
    return w


def moonlets():
    """5: The moonlets: little round moons floating over the keep; a launch
    lantern throws Wick from one to the next, gravity pulls him round."""
    w = World('The Moonlets')
    w.sky = ((20, 10, 90), (120, 60, 220))
    w.fog = (90, 60, 200)
    w.fog_near, w.fog_far = 16.0, 30.0
    w.ambient = 7.5
    w.scenery = 2
    w.birds = 0
    # the starting platform of star-stone
    w.box(-3, -1, -3, 3, 0, 5, T['STARSTONE'], T['STARSTONE'])
    w.thing('START', 0, 0, -1)
    gear_line(w, 0, 0.6, 0, 0, 3, 3)
    w.boxes.append((-80, -60, -80, 80, -30, 120, KILL))
    moons = [(0, 4, 16, 4.0, 'MOON_ROCK'), (10, 8, 30, 4.5, 'MOON_MOSS'), (-4, 12, 46, 5.0, 'MOON_ROCK'),
             (6, 15, 62, 4.0, 'MOON_MOSS')]
    # landing marks on top of each moon: things of type NONE the lanterns aim at
    marks = []
    for (cx, cy, cz, r, tex) in moons:
        w.sphere(cx, cy, cz, r, 14, T[tex], pull=r + 3.2)
        marks.append(len(w.things))
        w.thing('NONE', cx, cy + r + 0.2, cz - 0.5)
        # gears in a ring round its middle, a ghost circling
        for k in range(10):
            a = 2 * math.pi * k / 10
            w.thing('GEAR', cx + math.cos(a) * (r + 0.6), cy + 0.2, cz + math.sin(a) * (r + 0.6))
        w.thing('GHOST', cx + r + 1.5, cy + 1, cz)
    end_mark = len(w.things)
    w.thing('NONE', 0, 20.2, 76)
    w.box(-3, 19, 72, 3, 20, 80, T['STARSTONE'], T['STARSTONE'])
    w.thing('EXIT', 0, 20, 78)
    # the lanterns: the platform's, then one on top of each moon
    w.thing('LAUNCH', 0, 0, 3.5, marks[0])
    for i, (cx, cy, cz, r, tex) in enumerate(moons):
        target = marks[i + 1] if i + 1 < len(moons) else end_mark
        w.thing('LAUNCH', cx, cy - r, cz, target)
    w.zone(-80, -60, -80, 80, 60, 120, CAM_CHASE, 0.0, 5.5, 3.0)
    return w


def belltower():
    """6: The bell tower: a round chamber open to the sky, the great bronze
    bell, and the Bell-Wraith wound round it."""
    w = World('The Bell Tower')
    w.sky = ((60, 10, 90), (255, 90, 120))
    w.fog = (160, 60, 120)
    w.fog_near, w.fog_far = 14.0, 30.0
    w.ambient = 6.5
    cx, cz = 0, 12
    w.floor(-11, 0, 11, 24, 0, T['FLAGS'])
    w.cylinder(cx, cz, 11, 0, 7, 24, T['STONE'], collide=False, inside=True)
    for a in range(0, 360, 20):
        r = math.radians(a)
        w.boxes.append((cx + math.cos(r) * 10.8 - 0.8, 0, cz + math.sin(r) * 10.8 - 0.8,
                        cx + math.cos(r) * 10.8 + 0.8, 8, cz + math.sin(r) * 10.8 + 0.8, SOLID))
    for a in range(0, 360, 45):
        r = math.radians(a)
        x, z = cx + math.cos(r) * 8, cz + math.sin(r) * 8
        w.cylinder(x, z, 0.5, 0, 7, 8, T['PILLAR'])
        w.light(x, 2.5, z, 6.0, 5.0)
    for a in range(0, 360, 40):
        r = math.radians(a)
        w.picture(cx + math.cos(r) * 10.9, 3, cz + math.sin(r) * 10.9, -math.cos(r), -math.sin(r), 'GLASS_ROSE',
                  1.2, 2.4)
    # the bell: a bronze cone with a lip, on a beam
    w.cone(cx, cz, 3.0, 6.5, 3.5, 16, T['BRONZE'])
    w.cylinder(cx, cz, 3.1, 6.2, 6.6, 16, T['BRONZE'], collide=False)
    w.box(-11, 10.2, cz - 0.4, 11, 10.8, cz + 0.4, T['WOOD'], collide=False)
    w.thing('START', 0, 0, 2)
    w.thing('CANDLE', 2, 0, 3)
    w.thing('BOSS', cx, 0, cz)
    w.thing('HEART', -7, 0.6, 4)
    w.thing('HEART', 7, 0.6, 20)
    gear_line(w, -6, 0.6, 12, 6, 12, 0)
    w.zone(-40, -10, -20, 40, 40, 40, CAM_CHASE, 0.0, 6.5, 3.6)
    return w


LEVELS = [courtyard, staircase, gallery, rooftops, moonlets, belltower]
NAMES = ['The Courtyard', 'The Grand Staircase', 'The Ghost Gallery', 'The Rooftops', 'The Moonlets',
         'The Bell Tower']
