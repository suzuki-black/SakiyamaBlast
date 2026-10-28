#!/usr/bin/env python3
"""敵のアニメのコマを作って assets/sprites.png の**該当セルだけ**書き替える。

絵の正は PNG のほう (tools/png2art.py を参照)。これは `tools/make_art.py` と同じ
**たたき台を作る道具**で、コマの揺れ方を数字で直したいとき用。手で描き替えた
あとに実行すると上書きしてしまうので、動かすときは気をつけること。

    python3 tools/gen_enemy_anim.py --names <ART_SPR_NAMES> [--preview build/]

## 16x16 でうねうね見せるときの勘どころ

- **絵ごと平行移動しても動いて見えない。** 生き物が身をよじったのではなく、
  表示が揺れているように見える。身をよじる = **行ごとに違う量ずらす**こと。
  ここでは上半身と下半身が逆へ寄る S 字の波を、時間とともに下ろしている。
- **一番広い行を [1,14] に詰めてある。** 端まで使っていると、ずらしたぶんが
  画面外へ削れるだけでうねりが出ない。左右に1px ずつ遊びが要る。
- **行の役は動かせない。** mode-2 のスプライトは1行1色で、`src/enemy.c` は
  行の役 (耳2-3 / 目7 / 影11 / 足12) で色を当てている。縦にずらすと
  色の帯と絵が合わなくなるので、**横にしかずらさない**。
- **耳をぱたぱたさせない。** コマごとに形を変えると、うねりではなく
  瞬きに見える。耳は体のうねりに乗せるだけにしてある。

コウモリ (透明化中の姿) は16行ぜんぶ同じ淡い色なので行の役に縛られない。
こちらは羽ばたきなので、波ではなく4枚を直接書いてある。
"""
import argparse
import math
import os
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from palette import rgb255

# ---- ネズミ: 行ごとの塗る範囲 (波を足す前の形) ----
BODY = {4: (2, 13), 5: (1, 14), 6: (1, 14), 8: (1, 14), 9: (1, 14),
        10: (2, 13), 11: (3, 12)}
EYES = [(1, 3), (5, 10), (12, 14)]          # 行7。目は穴 (x=4 と x=11)
EARS = ["..OO........OO..", "..OOO......OOO.."]        # 行2-3
FEET = {"四": "..OO.OO..OO.OO..",                      # 行12
        "左": "..OO.....OO.OO..",
        "右": "..OO.OO.....OO.."}
FOOT_SEQ = ["四", "左", "四", "右"]
AMP = 1.5           # うねりの振れ幅 (px)
EAR_AMP = 0.6       # 耳は体より控えめに振る

# ---- コウモリ: 羽ばたきの4枚 ----
BAT = [
    [   # 0 羽は水平
        "................", "................",
        "PP............PP", "PPP..........PPP", "PPPPP......PPPPP",
        "PPPPPPPPPPPPPPPP", ".PPPPPPPPPPPPPP.", "..PPPP.PP.PPPP..",
        "...PPPPPPPPPP...", "....PPPPPPPP....", ".....PPPPPP.....",
        "......PPPP......", ".......PP.......",
        "................", "................", "................",
    ],
    [   # 1 打ち上げ
        "PP............PP", "PPP..........PPP", "PPPP........PPPP",
        ".PPPP......PPPP.", "..PPP......PPP..", "...PPPPPPPPPP...",
        "..PPPPPPPPPPPP..", "..PPPP.PP.PPPP..", "...PPPPPPPPPP...",
        "....PPPPPPPP....", ".....PPPPPP.....", "......PPPP......",
        ".......PP.......",
        "................", "................", "................",
    ],
    [   # 2 水平 (体が一段沈む)
        "................", "................", "................",
        "PP............PP", "PPP..........PPP", "PPPPP......PPPPP",
        "PPPPPPPPPPPPPPPP", ".PPPPPPPPPPPPPP.", "..PPPP.PP.PPPP..",
        "...PPPPPPPPPP...", "....PPPPPPPP....", ".....PPPPPP.....",
        "......PPPP......", ".......PP.......",
        "................", "................",
    ],
    [   # 3 打ち下げ
        "................", "................", "................",
        "................", ".....PP..PP.....", "...PPPPPPPPPP...",
        ".PPPPPPPPPPPPPP.", "PPPPPP.PP.PPPPPP", "PPPPPPPPPPPPPPPP",
        "PPPP.PPPPPP.PPPP", "PPP...PPPP...PPP", "PP.....PP.....PP",
        ".......PP.......",
        "................", "................", "................",
    ],
]

# ---- 見本を描くための色 (src/enemy.c の row_role[0] と kind_pal) ----
ROLE = [0, 0, 1, 1, 0, 0, 0, 2, 0, 0, 0, 3, 1, 0, 0, 0]
RAT_PAL = (9, 2, 15, 3)          # COL_RAT / COL_SKIN / COL_WHITE / COL_DARK
BAT_PAL = (5, 5, 5, 5)           # COL_BATH 一色 (透明化中)


def shift(s, o):
    row = ["."] * 16
    for x, ch in enumerate(s):
        if ch != "." and 0 <= x + o < 16:
            row[x + o] = "O"
    return "".join(row)


def rat_frame(ph):
    """ph = 0..3。上半身と下半身が逆へ寄る S 字の波。"""
    rows = []
    for y in range(16):
        if y in (2, 3):
            o = int(round(AMP * EAR_AMP * math.sin(2 * math.pi * ph / 4.0)))
            rows.append(shift(EARS[y - 2], o))
        elif y == 12:
            o = int(round(AMP * math.sin(2 * math.pi * (0.5 + ph / 4.0))))
            rows.append(shift(FEET[FOOT_SEQ[ph]], o))
        elif y == 7 or y in BODY:
            t = (y - 2) / 10.0
            o = int(round(AMP * math.sin(2 * math.pi * (t + ph / 4.0))))
            row = ["."] * 16
            for a, b in (EYES if y == 7 else [BODY[y]]):
                for x in range(a + o, b + o + 1):
                    if 0 <= x < 16:
                        row[x] = "O"
            rows.append("".join(row))
        else:
            rows.append("." * 16)
    return rows


def paste(im, idx, rows, idxcol):
    px = im.load()
    r, g, b = rgb255(idxcol)
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            px[idx * 16 + x, y] = (r, g, b, 255) if ch != "." else (0, 0, 0, 0)


def preview(path, cells, pal, ghost, scale=8):
    """見本。**行ごとの色を当ててから**並べる (絵だけ見ても実機の見え方にならない)。"""
    bgs = ((0, 0, 0), (146, 73, 36))          # 坑道の黒と土の茶
    w = 16 * scale
    im = Image.new("RGB", (w * len(cells) * len(bgs), w), (30, 30, 30))
    for bi, bg in enumerate(bgs):
        for ci, rows in enumerate(cells):
            t = Image.new("RGB", (w, w), bg)
            tp = t.load()
            for y, row in enumerate(rows):
                c = rgb255(pal[0] if ghost else pal[ROLE[y]])
                for x, ch in enumerate(row):
                    if ch == ".":
                        continue
                    for dy in range(scale):
                        for dx in range(scale):
                            tp[x * scale + dx, y * scale + dy] = c
            im.paste(t, ((bi * len(cells) + ci) * w, 0))
    im.save(path)
    return path


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default="assets/sprites.png")
    ap.add_argument("--names", required=True, help="Makefile の ART_SPR_NAMES")
    ap.add_argument("--preview", help="見本の出力先ディレクトリ")
    args = ap.parse_args(argv[1:])

    names = args.names.split(",")
    rat = [rat_frame(p) for p in range(4)]
    cells = dict(zip(["rat", "rat1", "rat2", "rat3"], rat))
    cells.update(zip(["bat", "bat1", "bat2", "bat3"], BAT))

    missing = [n for n in cells if n not in names]
    if missing:
        raise SystemExit("--names に %s がありません。Makefile の ART_SPR_NAMES へ"
                         "足してください" % ",".join(sorted(missing)))

    old = Image.open(args.src).convert("RGBA")
    need = 16 * len(names)
    if old.size[0] < need:
        im = Image.new("RGBA", (need, 16), (0, 0, 0, 0))
        im.paste(old, (0, 0))
    else:
        im = old
    for name, rows in cells.items():
        paste(im, names.index(name), rows, 13 if name.startswith("bat") else 9)
    im.save(args.src)
    print("%s: %d セルのうち %s を書き替えました"
          % (args.src, len(names), ",".join(sorted(cells))))

    if args.preview:
        print(preview(os.path.join(args.preview, "anim_rat.png"), rat,
                      RAT_PAL, False))
        print(preview(os.path.join(args.preview, "anim_bat.png"), BAT,
                      BAT_PAL, True))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
