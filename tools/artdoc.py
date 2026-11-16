#!/usr/bin/env python3
"""README の一覧表で使う、盤面の要素の絵を書き出す。

**ROM に入っている実体から起こす。** `build/tiles.bin` と `build/sprites.bin` は
`tools/png2art.py` が `assets/*.png` から作ったもので、**パレットへの寄せも、
スプライトの「1行1色」も通ったあと**なので、実機で出るのと同じ絵になる
(`assets/*.png` を直に切ると、寄せる前の色で出てしまう)。

    make artdoc        # -> docs/media/elem/*.png

絵を描き替えたら走らせ直すこと。**`make all` からは呼ばない** —— ROM のビルドが
Pillow の出力に依存すると、絵を触っていない日まで差分が出る。
"""
import os
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from palette import PALETTE

SCALE = 4                       # 16x16 -> 64x64 (README では width=48 で出す)
BG = (0, 0, 0)                  # 坑道の黒。要素はこの上に重なって見える

TILE_NAMES = ("powder,bath,bscore,bcool,blife,cave,dia,dyna,diacrk,pillar,"
              "spring,springoff,toir,toil,toibr,toibl,rail,raill,railr,gas,"
              "diahard").split(",")
SPR_NAMES = ("miner,bomb,rock,rat,bat,split,stun1,stun2,stun3,brace,wd1,wd2,"
             "wu1,wu2,wl1,wl2,wr1,wr2,bru,brl,brr,std,stu,stl,str,chip,cart,"
             "cartrock,rockprop,rat1,rat2,rat3,bat1,bat2,bat3").split(",")

# 書き出すもの。README の表に載せる順。
WANT_TILES = ("dia", "diacrk", "pillar", "diahard", "toir", "toibr",
              "bath", "spring", "gas", "powder", "dyna", "cave",
              "bscore", "bcool", "blife", "rail")
WANT_SPRS = ("rock", "rockprop", "split", "cart", "cartrock")


def rgb(idx):
    """パレット番号を 24bit の色へ。0番は背景 (坑道) として扱う。"""
    if idx == 0:
        return BG
    r, g, b = PALETTE[idx]
    return (r * 36, g * 36, b * 36)      # 0-7 -> 0-252


def tile_img(blob, i):
    im = Image.new("RGB", (16, 16), BG)
    px = im.load()
    off = i * 128
    for y in range(16):
        for x in range(0, 16, 2):
            v = blob[off + y * 8 + (x >> 1)]
            px[x, y] = rgb(v >> 4)
            px[x + 1, y] = rgb(v & 15)
    return im


def spr_img(blob, i, n):
    """mode-2 スプライト。形は 1行 16ビット (下位バイトが先)、色は1行1バイト。"""
    im = Image.new("RGB", (16, 16), BG)
    px = im.load()
    pat = i * 32
    col = n * 32 + i * 16
    for y in range(16):
        bits = blob[pat + y * 2] | (blob[pat + y * 2 + 1] << 8)
        c = rgb(blob[col + y])
        for x in range(16):
            if bits & (1 << (15 - x)):
                px[x, y] = c
    return im


def main():
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    tiles = open(os.path.join(here, "build/tiles.bin"), "rb").read()
    sprs = open(os.path.join(here, "build/sprites.bin"), "rb").read()
    out = os.path.join(here, "docs/media/elem")
    os.makedirs(out, exist_ok=True)

    n = 0
    for name in WANT_TILES:
        im = tile_img(tiles, TILE_NAMES.index(name))
        im.resize((16 * SCALE, 16 * SCALE), Image.NEAREST).save(
            os.path.join(out, name + ".png"))
        n += 1
    for name in WANT_SPRS:
        im = spr_img(sprs, SPR_NAMES.index(name), len(SPR_NAMES))
        im.resize((16 * SCALE, 16 * SCALE), Image.NEAREST).save(
            os.path.join(out, name + ".png"))
        n += 1
    print("docs/media/elem/: %d 枚 (%dx%d)" % (n, 16 * SCALE, 16 * SCALE))
    return 0


if __name__ == "__main__":
    sys.exit(main())
