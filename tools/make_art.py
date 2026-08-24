#!/usr/bin/env python3
"""assets/sprites.png と assets/tiles.png の**たたき台**を作る。

絵の正は PNG のほう。差し替えるときは PNG を好きなドット絵エディタで描き替えればよく、
このスクリプトを使う必要はない。手で ASCII を直したいときのために残してある。

MSX の mode-2 スプライトは **1行に1色**しか持てないので、スプライトの絵は
「1行の中では記号を1種類だけ使う」こと (背景の '.' は除く)。
タイル (火薬/風呂/ダイヤ) はビットマップなので1行に何色でも使える。
"""
import sys

from PIL import Image

sys.path.insert(0, __file__.rsplit('/', 1)[0])
from palette import LEGEND, rgb255

SPRITES = [
    ("miner", [                    # 丹野3時。ヘルメット・カンテラ・作業着・長靴
        "................",
        ".....WWWWWW.....",
        "....WWWWWWWW....",
        "...WWWWWWWWWW...",
        "..YYYYYYYYYYYY..",
        "....SSSSSSSS....",
        "....SS.SS.SS....",
        ".....SSSSSS.....",
        "...BBBBBBBBBB...",
        "..BBBBBBBBBBBB..",
        "..BBBBBBBBBBBB..",
        "..BBBBBBBBBBBB..",
        "...BBBBBBBBBB...",
        "...BBB....BBB...",
        "..KKK......KKK..",
        "..KK........KK..",
    ]),
    ("bomb", [                     # ダイナマイト。導火線が上
        "................",
        "........Y.......",
        ".......Y.Y......",
        "........Y.......",
        ".......YYY......",
        ".....RRRRRR.....",
        ".....RRRRRR.....",
        ".....RRRRRR.....",
        ".....YYYYYY.....",
        ".....RRRRRR.....",
        ".....RRRRRR.....",
        ".....RRRRRR.....",
        ".....RRRRRR.....",
        ".....RRRRRR.....",
        "................",
        "................",
    ]),
    ("rock", [                     # 岩。上に光、下に影
        "................",
        ".....WWWWWW.....",
        "...GGGGGGGGGG...",
        "..GGGGGGGGGGGG..",
        ".GGGGGGGGGGGGGG.",
        ".GGGGGGGGGGGGGG.",
        "GGGGGGGGGGGGGGGG",
        "GGGGGGGGGGGGGGGG",
        "GGGGGGGGGGGGGGGG",
        "GGGGGGGGGGGGGGGG",
        ".GGGGGGGGGGGGGG.",
        ".GGGGGGGGGGGGGG.",
        "..KKKKKKKKKKKK..",
        "...KKKKKKKKKK...",
        ".....KKKKKK.....",
        "................",
    ]),
    ("rat", [                      # ネズミ (坑道を走る通常種)
        "................",
        "................",
        "..OO........OO..",
        ".OOOO......OOOO.",
        "..OOOOOOOOOOOO..",
        ".OOOOOOOOOOOOOO.",
        "OOOOOOOOOOOOOOOO",
        "OOOO.OOOOOO.OOOO",
        "OOOOOOOOOOOOOOOO",
        ".OOOOOOOOOOOOOO.",
        "..OOOOOOOOOOOO..",
        "...OOOOOOOOOO...",
        "..OO.OO..OO.OO..",
        "................",
        "................",
        "................",
    ]),
    ("bat", [                      # コウモリ (掘り屋へ変異した姿)
        "................",
        "................",
        "PP............PP",
        "PPP..........PPP",
        "PPPPP......PPPPP",
        "PPPPPPPPPPPPPPPP",
        ".PPPPPPPPPPPPPP.",
        "..PPPP.PP.PPPP..",
        "...PPPPPPPPPP...",
        "....PPPPPPPP....",
        ".....PPPPPP.....",
        "......PPPP......",
        ".......PP.......",
        "................",
        "................",
        "................",
    ]),
]

TILES = [
    ("powder", [                   # 火薬 (弾薬)。拾うとダイナマイトが増える
        "................",
        "................",
        ".........Y......",
        "........Y.......",
        "........Y.......",
        "....KKKKKKKK....",
        "...KRRRRRRRRK...",
        "...KRRRRRRRRK...",
        "...YYYYYYYYYY...",
        "...KRRRRRRRRK...",
        "...KRRRRRRRRK...",
        "...KRRRRRRRRK...",
        "...YYYYYYYYYY...",
        "....KKKKKKKK....",
        "................",
        "................",
    ]),
    ("bath", [                     # 風呂 (温泉)。通ると体温が下がる
        "................",
        "......W...W.....",
        ".....W...W......",
        "......W...W.....",
        "................",
        ".KKKKKKKKKKKKKK.",
        ".KWWWWWWWWWWWWK.",
        ".KUUUUUUUUUUUUK.",
        ".KUUUUUUUUUUUUK.",
        ".KUUUUUUUUUUUUK.",
        ".KUUUUUUUUUUUUK.",
        ".KUUUUUUUUUUUUK.",
        ".KKKKKKKKKKKKKK.",
        "..K..........K..",
        "................",
        "................",
    ]),
    ("dia", [                      # ダイヤモンドブロック。ダイナマイトでしか砕けない
        "LLLLLLLLLLLLLLLL",
        "LCCCCCCCCCCCCCCL",
        "LCWWWWWWCCCCCCCL",
        "LCWWWWCCCCCCCCCL",
        "LCWWCCCCCCCCCCCL",
        "LCCCCCCCCCCCCCCL",
        "LCCCCCCCCCCCCCCL",
        "LCCCCCCCLLLCCCCL",
        "LCCCCCCLLLLLCCCL",
        "LCCCCCCCLLLCCCCL",
        "LCCCCCCCCCCCCCCL",
        "LCCCCCCCCCCCCCCL",
        "LCCCCCCCCCCCCCCL",
        "LCCCCCCCCCCCCCCL",
        "LCCCCCCCCCCCCCCL",
        "LLLLLLLLLLLLLLLL",
    ]),
]


def build(items, path, check_one_colour_per_row):
    im = Image.new("RGBA", (16 * len(items), 16), (0, 0, 0, 0))
    px = im.load()
    for i, (name, rows) in enumerate(items):
        for y, row in enumerate(rows):
            if check_one_colour_per_row:
                used = {c for c in row if c != '.'}
                if len(used) > 1:
                    raise SystemExit("%s の %d 行目に色が %d 種あります。"
                                     "mode-2 スプライトは1行1色です: %s"
                                     % (name, y, len(used), row))
            for x, ch in enumerate(row):
                idx = LEGEND[ch]
                if idx == 0:
                    continue
                r, g, b = rgb255(idx)
                px[i * 16 + x, y] = (r, g, b, 255)
    im.save(path)
    print("%s: %d セル (%s)" % (path, len(items), ", ".join(n for n, _ in items)))


build(SPRITES, "assets/sprites.png", True)
build(TILES, "assets/tiles.png", False)
