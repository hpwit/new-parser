#!/usr/bin/env python3
"""Ironforge's levels, drawn here as text, written as the script's
initializers (python3 data.py > data.txt, then pasted into Ironforge.ino).

The map is a grid of cells, each a kind from KINDS: a solid wall, or an open
cell with its own floor height, ceiling height (or the sky), floor and
ceiling textures and light. Heights are in cells: a wall is 1.0 high, a
step 0.25. The things are on a second grid of the same size.

Each level is checked: the start, one key, one exit; the key reached from
the start without the blue door, the exit reached with it (steps up of a
quarter of a cell at most, any step down, doors open).
"""

# textures (made by the script): walls 0 stone, 1 metal, 2 tech panel,
# 3 brick, 4 wooden door, 5 exit switch, 6 blue door; floors 7 tiles,
# 8 grate, 9 toxic slime, 10 dirt, 13 lava; ceilings 11 lamps, 12 rock
# kind: 0 open, 1 solid, 2 door, 3 blue door (needs the key), 4 exit switch,
# 5 toxic floor or lava (hurts)
SKY = 9.0
KINDS = {
    #     kind solid wallTex floorH ceilH floorTex ceilTex light
    "#": (1, 1, 0, 0.0, 0.0, 0, 0, 0),
    "M": (1, 1, 1, 0.0, 0.0, 0, 0, 0),
    "T": (1, 1, 2, 0.0, 0.0, 0, 0, 0),
    "B": (1, 1, 3, 0.0, 0.0, 0, 0, 0),
    "X": (4, 1, 5, 0.0, 0.0, 0, 0, 0),
    ".": (0, 0, 2, 0.0, 1.25, 7, 11, 13),
    ":": (0, 0, 1, 0.0, 1.0, 8, 12, 5),
    "h": (0, 0, 3, 0.0, 2.0, 7, 12, 11),
    "1": (0, 0, 3, 0.25, 2.0, 8, 12, 11),
    "2": (0, 0, 3, 0.5, 2.0, 8, 12, 12),
    "3": (0, 0, 3, 0.75, 2.0, 8, 12, 12),
    "4": (0, 0, 3, 1.0, 2.0, 8, 12, 13),
    "n": (5, 0, 3, -0.25, 2.0, 9, 12, 14),
    "o": (0, 0, 0, 0.0, SKY, 10, 0, 15),
    "p": (0, 0, 0, 0.5, SKY, 10, 0, 15),
    "r": (0, 0, 0, 0.25, SKY, 10, 0, 15),
    "q": (0, 0, 0, 0.75, SKY, 10, 0, 15),
    "D": (2, 0, 4, 0.0, 1.0, 8, 12, 9),
    "E": (2, 0, 4, 1.0, 2.0, 8, 12, 9),
    "K": (3, 0, 6, 0.0, 1.0, 8, 12, 9),
    "l": (5, 0, 3, -0.25, 2.0, 13, 12, 15),
}

LEVELS = [
# 1: the foundry
("""
########################
#####....#ooopppooooooo#
#####....Dooopporooopoo#
#####....#oooooopooopoo#
##########ooooooqoooooo#
############BBBBEBBBBB##
###########B444444444B##
###########Bhh33333hhB##
#.....#####Bhh22222hhB##
#.....#####Bhh11111hhB##
#.....D:::::hhhnnnhhhB##
#.....#####BhhhnnnhhhB##
#.....#####BhhhhhhhhhB##
###########BhhhhhhhhhB##
###########BhhhhhhhhhB##
###########BBBBBKBBBBB##
##############.....#####
##############.....#####
##############.....#####
################X#######""", """
........................
......k.......d.........
......h.........d....+..
.....s.......d.......d..
........................
........................
....................a...
........................
..+.....................
........................
...P.....a..............
................a.......
.............h.....e....
.............w....h.....
............+..w........
........................
...............h........
.................h......
................+.......
........................"""),
# 2: the furnace
("""
########################
#....#hhhhhhhhhhhhhh####
#....Dhhhhhhhhhhhhhh####
#....#hhhhhhhhhhhhhh####
#....#lllhllllllllll####
######lllhllllll122l####
######lllhllllll122l####
######lllhhhhhhhhlll####
######lllhllllllllll####
######lllhllllllllll#..#
######hhhhhhhhhhhhhhK..X
######hhhhhhhhhhhhhh#..#
######hhhhhhhhhhhhhh####
########################
########################
########################
########################
########################
########################
########################""", """
........................
.......a...........+....
..P.........h.....d.....
........................
...+....................
............w...........
..................k.....
........................
..............w.........
........................
........................
........h..c......h.....
...................e....
........................
........................
........................
........................
........................
........................
........................"""),
# 3: the smelter yard
("""
########################
#ooooooooooo############
#oooooooooooo#.....#####
#oorpoo#oooo#.....######
#ooooo##ooooD.....######
#ooooooooooo#.....######
#oooppoooooo###:########
#ooqppqooooo###:########
#ooooooooooo###:########
#ooooooooooo###:########
#ooooooooooo###:########
#############hhhhhhhh###
#############hhhhhhhh###
#############hh1234hh###
#############hhhhhhhhK.X
#############hhhhhhhh###
########################
########################
########################
########################""", """
........................
..a.......+.............
......d.................
...............h........
........................
................e.......
.........d..............
........................
....d...................
........................
..P.....................
........................
..............h...w.....
..................k.....
....................+...
.............e.....h....
........................
........................
........................
........................"""),
]

# P the start, h a husk, d a drone, w a wisp, + health, a bullets, k the
# key, e shells, s the shotgun, c the chaingun
THING = {".": 0, "h": 1, "d": 2, "w": 3, "+": 4, "a": 5, "k": 6, "P": 7, "e": 8, "s": 9, "c": 10}


def rows(art):
    r = art.strip("\n").split("\n")
    assert len(r) == 20 and all(len(x) == 24 for x in r), [len(x) for x in r]
    return r


def reachable(grid, start, can_key_door):
    """the cells walked to from start: a step up of 0.25 at most, any step
    down; doors open; the blue door only when can_key_door"""
    seen = {start}
    todo = [start]
    while todo:
        x, y = todo.pop()
        here = KINDS[grid[y][x]]
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = x + dx, y + dy
            if (nx, ny) in seen or not (0 <= nx < 24 and 0 <= ny < 20):
                continue
            k = KINDS[grid[ny][nx]]
            if k[1] == 1:
                continue
            if k[0] == 3 and not can_key_door:
                continue
            if k[3] > here[3] + 0.3:
                continue
            seen.add((nx, ny))
            todo.append((nx, ny))
    return seen


def check(n, grid, things):
    pos = {c: [(x, y) for y in range(20) for x in range(24) if things[y][x] == c] for c in "Pk"}
    assert len(pos["P"]) == 1 and len(pos["k"]) == 1, ("level", n, pos)
    exits = [(x, y) for y in range(20) for x in range(24) if grid[y][x] == "X"]
    assert len(exits) == 1, ("level", n, "exits", exits)
    for y in range(20):
        for x in range(24):
            if things[y][x] != ".":
                assert KINDS[grid[y][x]][1] == 0, ("level", n, "thing in a wall", x, y)
    without = reachable(grid, pos["P"][0], False)
    assert pos["k"][0] in without, ("level", n, "the key is out of reach")
    allc = reachable(grid, pos["P"][0], True)
    ex, ey = exits[0]
    assert any((ex + dx, ey + dy) in allc for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))), ("level", n, "the exit is out of reach")


def main():
    names = list(KINDS)
    cells, things = [], []
    for n, (m, th) in enumerate(LEVELS):
        g, tg = rows(m), rows(th)
        check(n + 1, g, tg)
        cells += [names.index(c) for row in g for c in row]
        things += [THING[c] for row in tg for c in row]
    cols = list(zip(*KINDS.values()))
    print("// the kinds of cell: " + " ".join(names))
    print("#define KINDS %d" % len(names))
    print("#define LEVELS %d" % len(LEVELS))
    print("uint8_t levelCells[%d] = {%s};" % (len(cells), ", ".join(map(str, cells))))
    print("uint8_t levelThings[%d] = {%s};" % (len(things), ", ".join(map(str, things))))
    for name, col, typ in (("kKind", 0, "uint8_t"), ("kSolid", 1, "uint8_t"), ("kWall", 2, "uint8_t"),
                           ("kFloor", 3, "float"), ("kCeil", 4, "float"), ("kFloorTex", 5, "uint8_t"),
                           ("kCeilTex", 6, "uint8_t"), ("kLight", 7, "uint8_t")):
        vals = ", ".join(str(v) for v in cols[col])
        print("%s %s[%d] = {%s};" % (typ, name, len(names), vals))


if __name__ == "__main__":
    main()
