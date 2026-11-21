"""Valmora's world, people, words and monsters (make_wad.py packs them)."""

# ------------------------------------------------------------ the world
# 48 x 40, each character 2 x 2 tiles of the 96 x 80 world.
#   ~ sea  r lake/river  . grass  o meadow  f forest  F pines  h hills
#   m mountains  M snow peaks  s sand  w swamp  l lava  a ash
WORLD = r"""
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
~~~~~~~~~~~~~~~MMMMMMMMMMMMMMMMM~~~~~~~~~~~~~~~~
~~~~~~~~~~~MMMMMMMMmaaaaaaaammMMMMMM~~~~~~~~~~~~
~~~~~~~~~MMMMMmmmaaalllllllaaammmMMMMM~~~~~~~~~~
~~~~~~~~MMMMmmmaallllllllllllaammmmMMMM~~~~~~~~~
~~~~~~~MMMmmmhaallllllllllllaahhmmmmMMM~~~~~~~~~
~~~~~~MMMmmhhhaallllllllllllaahhhhmmmMM~~~~~~~~~
~~~~~~MMmmhhhaaalllllllllllaaahhhhhmmMM~~~~~~~~~
~~~~~~MmmhhFFaaaaalllllllaaaaaFhhhhhmmM~~~~~~~~~
~~~~~mmmhhFFFFaaaaaaaaaaaaaaFFFFhh..hmm~~~~~~~~~
~~~~~mmhhFFFFFFFaaaaaaaaaaFFFFFFF.....h~~~~~~~~~
~~~~~mhhFFFFFFFFF.........FFFFFFF......~~~~~~~~~
~~~~hhhFFFFFFFFF...........FFFFF........~~~~~~~~
~~~~hhFFFFFFFFf.....ffff.......ff........~~~~~~~
~~~hhFFFFFFFFff....ffffff.................~~~~~~
~~~hFFFFFFFFFf....rrrr.ff.......hhh.......~~~~~~
~~~hFFFFFFFFf....rrrrrr.........hh....www..~~~~~
~~~hhFFFFFFff....rrrrrrr...........wwwwwww..~~~~
~~~~hhFFFFff.....rrrrrr...........wwwwwwwww..~~~
~~~~~hhFFff.......rrrr...........wwwwwwwwww...~~
~~~~~~hhff.........rr...........wwwwwwwwwww..~~~
~~~~~~hh...........rr..........wwwwwwwwwwww..~~~
~~~~~~h............rr.........fwwwwwwwwwww...~~~
~~~~~hh...ff.......rr........ffwwwwwwwwwww..~~~~
~~~~~h...ffff.......rr.......ffwwwwwwwwww...~~~~
~~~~hh..ffffff......rr........fwwwwwwwww...~~~~~
~~~~h...fffff........rr.........wwwwwww....~~~~~
~~~~h....fff.........rr..........www......~~~~~~
~~~~.................rr.........hhh.......~~~~~~
~~~~..ooo............rr........hhhhh.....~~~~~~~
~~~~.oooooo..........rr.......hhhmhh....~~~~~~~~
~~~~..oooooo.........rr........hhhh.....~~~~~~~~
~~~~s..oooo..........rr..........ff....s~~~~~~~~
~~~~ss...............rr.........ffff..ss~~~~~~~~
~~~~~ss.....ff.......rr..........ff..ss~~~~~~~~~
~~~~~~sss..ffff......rr.............ss~~~~~~~~~~
~~~~~~~ssss.ff......ssrrss........sss~~~~~~~~~~~
~~~~~~~~~sssssssssssss~~sssssssssss~~~~~~~~~~~~~
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
"""

# Places on the 96 x 80 world: tile, target map, where you arrive there.
PLACES = {
    'brindle': ((24, 60), 'village', 1),
    'highmere': ((48, 36), 'castle', 2),
    'saltmarsh': ((88, 40), 'port', 3),
    'mossdeep': ((8, 34), 'cave', 4),
    'crypt': ((74, 46), 'crypt', 6),
    'mines': ((66, 13), 'mine', 8),
    'spire': ((44, 8), 'spire', 10),
}
ROADS = [('brindle', 'highmere'), ('highmere', 'saltmarsh'), ('highmere', 'gate')]

# ------------------------------------------------------------ towns
# . grass  ` meadow  , cobble  ; cobble2  : wood floor  _ stone floor  = carpet
# B brick  b brick window  d brick door  S stone wall  D stone door  n torch
# W wood wall  w wood window  o wood door  k counter  t table  m bed  x barrel
# X crate  h shelf  a anvil  Q throne  O well  F fountain  * flowers  H hedge
# | fence  T tree  p pillar  ~ water  1 weapons 2 armour 3 potions 4 inn 5 healer
# @ where you arrive; any other capital or lower-case letter not above is a person.
TOWN_TILES = {
    '.': 'grass', '`': 'meadow', ',': 'cobble', ';': 'cobble2', ':': 'woodfloor', '_': 'stonefloor',
    '=': 'carpet', 'B': 'brick', 'b': 'brickwin', 'd': 'brickdoor', 'S': 'stonewall', 'D': 'stonedoor',
    'n': 'torch', 'W': 'woodwall', 'w': 'woodwin', 'o': 'wooddoor', 'k': 'counter', 't': 'table',
    'm': 'bed', 'x': 'barrel', 'X': 'crate', 'h': 'shelf', 'a': 'anvil', 'Q': 'throne', 'O': 'well',
    'F': 'fountain', '*': 'flowerbed', 'H': 'hedge', '|': 'fence', 'T': 'tree', 'p': 'pillar',
    '~': 'shallow', '1': 'sign_weapon', '2': 'sign_armor', '3': 'sign_potion', '4': 'sign_inn',
    '5': 'sign_heal', '@': None,
}

BRINDLE = r"""
TTTTTTTTTTTTTTTTT,,,TTTTTTTTTTTTTTTT
T..``.........T..,,,..T....```.....T
T..WWwWWWwW......,,,......BBbBBbBB.T
T..W::::::W..*.*.,,,......B::::::B.T
T..W:t::::W......,,,......Bkkkk::B.T
T..W:t:Ix:W......,,,......B:A::::B.T
T..W::::::W..T...,,,..T...B::::hhB.T
T..WWWoWWWW......,,,......BBBdBBBB.T
T.....,...3......,,,.........,..1..T
T.....,,,,,,,,,,,,,,,,,,,,,,,,,,,,.T
T.....,,,,,,,,,,,,,,,,,,,,,,,,,,,,.T
T.....,......,,,,,,,,,,,,......,...T
T..BBBdBBBB..,,,,,O,,,,,,..WWWWoWWWT
T..B::::::B..,,,,,,,,,,,,..W::::::WT
T..Bkkkkk:B..,,,,,,,,,G,,..W:m::m:WT
T..B:::K::B..,,,,,,,,,,,,..W::::::WT
T..BhhxX::B.....J....,.....W:m::m:WT
T..BBBBBBBB...*****..,.....WWwWWWWWT
T........2...........,.......L.....T
T..|||||||....``.....,,,....T.5....T
T..|``````|..........,,,......BBdBBT
T..|``C```|...T......,,,......B:::BT
T..|||||||......E....,,,......B:N:BT
T...................@,,,......BBBBBT
TTTTTTTTTTTTTTTTTTTT,,,,TTTTTTTTTTTT
"""

HIGHMERE = r"""
TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT
T....SSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS....T
T....S____h__h__S==========S_______S.....T
T....S__________S=====Q====S_______S.....T
T....S___E______S=====A====S___m_m_S.....T
T....S__________S==========S_______S.....T
T....S_t__t_____n====p=p===n_______S.....T
T....S__________S==Y=====Z=S___I___S.....T
T....SSSSDSSSSSSS==========SSSSDSSSS.....T
T....S__________S==p=====p=S_______S.....T
T....S__________D==========D_______S.....T
T....S__________S==========S_______S.....T
T....SSSSSSSSSSSSSSSSD=DSSSSSSSSSSSS.....T
T...................,,,,,..................T
T..*****......G.....,,,,,.....H...*****...T
T...................,,,,,.................T
T..BBBbBBBB......,,,,,,,,,,,......BBbBBBB.T
T..B::::::B......,,,,,F,,,,,......B:::::B.T
T..Bkkkk::B......,,,,,,,,,,,......B:kkkkB.T
T..B:J::::B.T....,,,,,,,,,,,....T.B::::KB.T
T..Bh:a:xXB......,,,,,,,,,,,......B:hh::B.T
T..BBBBdBBB..1...,,,,,,,,,,,....2.BBBdBBB.T
T.......,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,....T
T.......,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,....T
T..WWWoWWWW..4...,,,,,,,,,,,....5.WWWoWWW.T
T..W::::::W......,,,,,,,,,,,......W:::::W.T
T..Wkkk:::W...O..,,,,,,,,,,,..M...W:::::W.T
T..W:L::m:W......,,,,,,,,,,,......W:::N:W.T
T..W::::m:W.......,,,,,,,,,.......W:::::W.T
T..WWWWWWWW.....P..,,,,,,,..R.....WWWWWWW.T
T..................,,,,,..................T
T..````.......T....,,@,,....T.......````..T
TTTTTTTTTTTTTTTTTTT,,,,,TTTTTTTTTTTTTTTTTTT
"""

SALTMARSH = r"""
TTTTTTTTTTTTTTTTTTTTTTTTTT~~~~~~~~~~
T........```......T.......~~~~~~~~~~
T..WWwWWWW...BBbBBBB......~~~~~~~~~~
T..W:::::W...B:::::B....X.~~~~~~~~~~
T..Wkkk::W...Bkkkk:B.......:::::::~~
T..W:A:::W...B:Y:::B..x.G..:~~~~~:~~
T..Wh::xxW...Bhh:::B.......:~~~~~:~~
T..WWWoWWW...BBBdBBB.......:~~~~~:~~
T.....,...3.....,.1........:::::::~~
T.....,,,,,,,,,,,,,,,,,,,,,,......~~
T..........,,,,,,,,,,,,,,.....X...~~
T..BBbBBBB.,,,,,,,O,,,,,,.........~~
T..B:::::B.,,,,,,,,,,,,,,..E.......~
T..Bkkkk:B.,,,,,,,,,,,,,,..........~
T..B:::C:B.,,,,,,,,,,,,,,....x.....~
T..B::hh:B.,,,,,,,,,,,,,,..........~
T..BBBdBBB....,,,,,,,......WWWWoWW.~
T......2......,,,,,,,....4.W:::::W.~
T..*****......,,,,,,,......Wkkk::W.~
T.............,,,,,,,......W:K:m:W.~
T..H....T.....,,,,,,,..U...W::::mW.~
T.............,,,,,,,......WWWWWWW.~
T....````...L.,,,,,,,..............~
T.............,,,@,,,..............~
TTTTTTTTTTTTTT,,,,,,,TTTTTTTTTTTTTTT
"""

# roles: talk, weapons, armour, potions, inn, healer, king
TOWNS = [
    (1, 'Brindle', BRINDLE, {
        'A': ('Hal the Smith', 'smith', 'weapons', (1, 2), 'hal', False),
        'I': ('Marta', 'innkeeper', 'inn', (10, 0), 'marta', False),
        'K': ('Pell the Herbalist', 'merchant', 'potions', (15, 0), 'pell', False),
        'N': ('Sister Ione', 'healer', 'healer', (0, 0), 'ione', False),
        'G': ('Elder Corwin', 'sage', 'talk', (0, 0), 'corwin', False),
        'J': ('Tam', 'child', 'talk', (0, 0), 'tam', True),
        'C': ('Oswin', 'man', 'talk', (0, 0), 'oswin', True),
        'E': ('Lida', 'woman', 'talk', (0, 0), 'lida', True),
        'L': ('Brenna', 'woman', 'armour', (1, 2), 'brenna', False),
    }),
    (2, 'Highmere', HIGHMERE, {
        'A': ('King Aldren', 'king', 'king', (0, 0), 'king', False),
        'Y': ('Royal Guard', 'guard', 'talk', (0, 0), 'guard1', False),
        'Z': ('Royal Guard', 'guard', 'talk', (0, 0), 'guard2', False),
        'E': ('Sage Elowen', 'sage', 'talk', (0, 0), 'elowen', False),
        'I': ('Lady Merel', 'woman', 'talk', (0, 0), 'merel', True),
        'G': ('Captain Brannoc', 'guard', 'talk', (0, 0), 'brannoc', True),
        'H': ('Pip', 'child', 'talk', (0, 0), 'pip', True),
        'J': ('Gerrit the Smith', 'smith', 'weapons', (1, 3), 'gerrit', False),
        'K': ('Ysolde', 'merchant', 'armour', (1, 3), 'ysolde', False),
        'L': ('Bram', 'innkeeper', 'inn', (15, 0), 'bram', False),
        'N': ('Brother Aldo', 'healer', 'healer', (0, 0), 'aldo', False),
        'M': ('Old Fenwick', 'man', 'talk', (0, 0), 'fenwick', True),
        'P': ('Gate Guard', 'guard', 'talk', (0, 0), 'gate1', False),
        'R': ('Gate Guard', 'guard', 'talk', (0, 0), 'gate2', False),
    }),
    (3, 'Saltmarsh', SALTMARSH, {
        'A': ('Rook', 'smith', 'weapons', (3, 4), 'rook', False),
        'Y': ('Ilse', 'merchant', 'armour', (3, 4), 'ilse', False),
        'C': ('Old Nan', 'woman', 'potions', (12, 0), 'nan', False),
        'K': ('Dory', 'innkeeper', 'inn', (12, 0), 'dory', False),
        'E': ('Agnar the Fisher', 'man', 'talk', (0, 0), 'agnar', True),
        'G': ('Wenna', 'woman', 'talk', (0, 0), 'wenna', True),
        'L': ('Corin the Bard', 'merchant', 'talk', (0, 0), 'corin', True),
        'U': ('Harbour Guard', 'guard', 'talk', (0, 0), 'hguard', False),
    }),
]

# ------------------------------------------------------------ dungeons
# map id, name, look, floors' monsters, boss (kind, shard) on the last floor
DUNGEONS = [
    (4, 'Mossdeep Caves', 'cave', 1, ['rat', 'bat', 'slime', 'spider'], None, 'mossdeep'),
    (5, 'Mossdeep Depths', 'cave', 2, ['spider', 'slime', 'goblin', 'bat'], ('golem', 0), 'mossdeep'),
    (6, 'The Drowned Crypt', 'crypt', 1, ['skeleton', 'ghoul', 'blueslime', 'bat'], None, 'crypt'),
    (7, 'The Sunken Vault', 'crypt', 2, ['skeleton', 'ghoul', 'ghost', 'blueslime'], ('drowned', 1), 'crypt'),
    (8, 'The Ember Mines', 'mine', 1, ['orc', 'goblin', 'spider', 'troll'], None, 'mines'),
    (9, 'The Molten Deep', 'mine', 2, ['orc', 'troll', 'drake'], ('wyrm', 2), 'mines'),
    (10, 'The Hollow Spire', 'spire', 1, ['wraith', 'ghost', 'skeleton', 'troll', 'drake'], None, 'spire'),
    (11, 'The Spire Crown', 'spire', 2, ['wraith', 'drake', 'ghost'], ('morvath', 3), 'spire'),
]

# special chests: map id -> (content, amount)
SPECIAL_CHESTS = {5: ('armour', 2), 7: ('weapon', 3), 8: ('armour', 3), 9: ('weapon', 4), 10: ('armour', 4)}

# ------------------------------------------------------------ monsters
# name, sprite, hp, attack, defence, xp, gold
MONSTERS = [
    ('rat', 'Giant Rat', 'rat', 6, 3, 0, 3, 1),
    ('bat', 'Cave Bat', 'bat', 5, 3, 0, 3, 0),
    ('slime', 'Green Slime', 'slime', 10, 4, 1, 5, 2),
    ('wolf', 'Grey Wolf', 'wolf', 14, 6, 1, 8, 0),
    ('goblin', 'Goblin', 'goblin', 16, 7, 2, 10, 6),
    ('bandit', 'Bandit', 'bandit', 20, 8, 2, 14, 12),
    ('skeleton', 'Skeleton', 'skeleton', 24, 10, 3, 18, 5),
    ('spider', 'Giant Spider', 'spider', 22, 10, 2, 18, 0),
    ('orc', 'Orc Brute', 'orc', 34, 13, 4, 28, 14),
    ('ghoul', 'Ghoul', 'ghoul', 30, 12, 4, 26, 8),
    ('blueslime', 'Bog Slime', 'blueslime', 26, 10, 3, 20, 4),
    ('wraith', 'Wraith', 'wraith', 44, 16, 6, 44, 12),
    ('troll', 'Hill Troll', 'troll', 60, 17, 6, 60, 25),
    ('drake', 'Fire Drake', 'drake', 70, 20, 8, 80, 40),
    ('ghost', 'Lost Soul', 'ghost', 30, 13, 7, 32, 0),
    ('golem', 'the Moss Golem', 'golem', 110, 15, 6, 150, 120),
    ('drowned', 'the Drowned King', 'drowned', 160, 20, 8, 260, 180),
    ('wyrm', 'the Ember Wyrm', 'wyrm', 210, 25, 10, 400, 250),
    ('morvath', 'Morvath the Hollow', 'morvath', 320, 30, 12, 1000, 0),
]

# what roams the world, by ground: (monster, from level)
ZONES = {
    1: [('rat', 1), ('wolf', 1), ('goblin', 2), ('bandit', 3), ('orc', 5)],      # grass, roads' sides
    2: [('wolf', 1), ('spider', 2), ('goblin', 2), ('orc', 4), ('troll', 7)],   # forests
    3: [('goblin', 1), ('wolf', 2), ('orc', 3), ('troll', 6)],                  # hills
    4: [('slime', 1), ('blueslime', 2), ('ghoul', 4), ('ghost', 6)],             # swamp
    5: [('wraith', 1), ('drake', 1), ('troll', 1)],                             # ash
}

WEAPONS = [('Dagger', 2, 0), ('Short Sword', 4, 40), ('War Mace', 7, 120), ('Longsword', 10, 280),
           ('Runeblade', 14, 620)]
ARMOURS = [('Tunic', 0, 0), ('Leather', 2, 35), ('Scale Mail', 4, 110), ('Chain Mail', 7, 260),
           ('Plate Armour', 10, 580)]

# ------------------------------------------------------------ words
# One entry per person: variants (minimum shards, text). | starts a new page.
DIALOG = {
    'corwin': [
        (0, "Thank the light you are awake. Three nights ago the Sunstone Crown of Highmere was "
            "shattered. Morvath the Hollow, once the King's own sorcerer, split it into three shards "
            "and hid them in the deep places of Valmora.|Without the Crown the old wards are failing: "
            "wolves come down from the hills and the dead stir in the marsh. Go north-east along the "
            "road, cross the river and kneel before King Aldren in Highmere.|Spend your coin with Hal "
            "and Brenna before you go. A dagger and a tunic will not carry you far."),
        (1, "A shard! I can feel its warmth from here. Keep going: the Crown must be whole again."),
        (3, "All three... Take them to the King. Valmora's hope walks with you."),
    ],
    'tam': [(0, "Mama says the old caves past the western woods are full of moss that moves! "
                "I saw green eyes in there once. Big ones.")],
    'oswin': [(0, "Wolves took two of my sheep last night. Keep to the road, and when you're hurt, "
                  "rest at Marta's inn. A bed mends what potions can't.")],
    'lida': [(0, "The river cuts Valmora in two. The bridge on the north-east road is the only way "
                 "to Highmere from here.|They say the lake by the castle never freezes, even in "
                 "the deepest winter.")],
    'hal': [(0, "Steel for the road? Hal's blades are honest blades.")],
    'brenna': [(0, "Leather won't stop a troll, but it'll stop a rat. Have a look.")],
    'marta': [(0, "A warm bed and a hot stew, friend.")],
    'pell': [(0, "Red tonic, brewed this morning. One swallow closes a wound. Press P to drink one "
                 "when a fight turns against you.")],
    'ione': [(0, "Be still. The light remembers you.")],
    'king': [
        (0, "Corwin sent you? Then hear me, child of Brindle. Morvath has fled to his Spire beyond "
            "the burning lake to the north, and the causeway gate answers only to the Sunstone "
            "Crown.|Its shards lie in the Mossdeep Caves in the western woods, in the Drowned Crypt "
            "beneath the eastern marsh, and in the Ember Mines under the northern peaks.|Bring them "
            "to me, and the gate will open. Sage Elowen will teach you what she can."),
        (1, "One shard of three... The Crown hums when its pieces are near each other. Do not rest "
            "long."),
        (2, "Two of three! One more, champion, and the gate is yours."),
        (3, "The Crown is whole! I have set it upon the throne. Do you hear that, far away to the "
            "north? The gate on the causeway has opened.|End this. Morvath waits at the top of his "
            "Spire."),
    ],
    'guard1': [(0, "Long live King Aldren.")],
    'guard2': [(0, "The throne room is open to all who come in peace. And to you.")],
    'elowen': [(0, "Magic answers to will, not to wands. Press M to Mend your wounds, or F and then a "
                   "direction to hurl a Spark. Both cost magic, which comes back as you walk.|The "
                   "dead in the crypt fear no blade, but a Spark burns them all the same.")],
    'merel': [(0, "Morvath and the King were friends once, you know. Friends make the bitterest "
                  "enemies.")],
    'brannoc': [(0, "The Ember Mines lie past the northern pines, where the hills meet the mountains. "
                    "Trolls hunt there. Don't go without a good blade and good armour.")],
    'pip': [(0, "I'm going to be a knight! I practise on the castle cat. The cat is winning.")],
    'gerrit': [(0, "Forged in Highmere, tempered in the lake. Finest steel north of the river.")],
    'ysolde': [(0, "Armour fitted while you wait. Well, while you wait a little.")],
    'bram': [(0, "Rooms are clean, the ale less so.")],
    'aldo': [(0, "Rest a moment. The light is patient.")],
    'fenwick': [(0, "When I was young, Morvath lit the castle every midwinter with lights of every "
                    "colour. Now look at him.")],
    'gate1': [(0, "Highmere welcomes you. Mind the wolves on the road south.")],
    'gate2': [(0, "Saltmarsh lies east, past the marsh road. The crypt lies in that bog too, "
                  "they say.")],
    'rook': [(0, "Blades from over the sea. Not cheap, but they don't break.")],
    'ilse': [(0, "Chain and plate, oiled against the salt.")],
    'nan': [(0, "Tonics, dearie. Cheaper than Pell's, and stronger too, whatever he tells you.")],
    'dory': [(0, "Sleep with the sound of the sea, traveller.")],
    'agnar': [(0, "The Drowned Crypt? South-west of here, deep in the marsh: an old tomb that sank "
                  "into the bog. Lights dance over it at night, and the fish won't bite near it.")],
    'wenna': [(0, "My brother went into the marsh to look for the crypt's gold. He came back with "
                  "a cough and no gold.")],
    'corin': [(0, "A song for a coin? No? Then have it free:|'Three shards of sun, in moss, in mire, "
                  "in mines of ember, under fire. Bring them home, and the gate will fall, and the "
                  "hollow man will fall with all.'")],
    'hguard': [(0, "Bandits work the coast road. They'll have your purse and your boots.")],
}

INTRO = ("The kingdom of Valmora sleeps under the Sunstone Crown, whose light has kept "
         "its roads safe for three hundred years.|Then Morvath, the King's own sorcerer, "
         "turned hollow. One night he broke the Crown in three and vanished, and the light "
         "went out.|You wake in the village of Brindle. Elder Corwin is waiting for you.")
VICTORY = ("Morvath the Hollow falls, and his Spire falls with him.|Far to the south, the "
           "Sunstone Crown blazes on its throne, and its light runs over the hills, the "
           "woods, the marsh and the mountains.|Valmora is safe again. The songs will remember "
           "your name.")
