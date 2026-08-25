#!/usr/bin/env python3
"""ゲーム1本ぶんのグラフィックデータを1つの JSON へ書き出す。

パターンエディタなど外部のツールへ渡すための「生きたデータ」。
絵は assets の PNG が正なので、ここはそこから吸い出す。パレットは tools/palette.py、
面番号の掘り抜きに使う数字は src/field.c から読む。付属情報（キャラ名や動作の別）は
PNG からは分からないので、このファイル内の表で持つ。

  python3 tools/export_gamedata.py gamedata.json

形式の仕様は docs/GAMEDATA_FORMAT.md。
"""
import json
import re
import sys

from PIL import Image

sys.path.insert(0, __file__.rsplit('/', 1)[0])
from palette import PALETTE, nearest, rgb255

HEX = "0123456789ABCDEF"

# ---- 付属情報（PNG からは分からないので手で持つ）-------------------------------

SPRITE_META = [
    ("miner", "丹野3時", "Tanno Sanji", "player",
     "先山（坑内で採炭を主導する炭鉱夫）。主人公。ヘルメットにカンテラ。",
     ["walk_right", "walk_left", "walk_up", "walk_down", "dig", "miss"]),
    ("bomb", "ダイナマイト", "Dynamite", "item",
     "設置したダイナマイト。導火が燃えている間だけ表示する。",
     ["fuse"]),
    ("rock", "岩", "Rock", "obstacle",
     "押せる／落ちる岩。Mr. Do! の apple 相当。",
     ["idle", "wobble"]),
    ("rat", "ネズミ", "Rat", "enemy",
     "坑内の小動物。坑道の中だけを歩く通常形態。",
     ["walk_right", "walk_left", "walk_up", "walk_down"]),
    ("bat", "コウモリ", "Bat", "enemy",
     "透明化して土をすり抜けている形態。トンネルへ入ると実体化してネズミへ戻る。",
     ["float_right", "float_left", "float_up", "float_down"]),
]

ANIM_NAME = {
    "walk_right": ("右移動", "Walk right"), "walk_left": ("左移動", "Walk left"),
    "walk_up": ("上移動", "Walk up"), "walk_down": ("下移動", "Walk down"),
    "float_right": ("右移動", "Float right"), "float_left": ("左移動", "Float left"),
    "float_up": ("上移動", "Float up"), "float_down": ("下移動", "Float down"),
    "dig": ("掘り", "Dig"), "miss": ("ミス", "Miss"),
    "fuse": ("導火", "Fuse burning"), "idle": ("静止", "Idle"),
    "wobble": ("ぐらつき", "Wobble"),
}

TILE_META = [
    ("powder", "火薬", "Gunpowder", "pickup", 2,
     "拾うとダイナマイトのストックが1増える。次の面へ持ち越す。"),
    ("bath", "風呂", "Bath", "field", 2,
     "通過すると体温が一気に下がる。何度でも入れる。落下する岩と爆風でだけ壊れる。"),
    ("bscore", "金塊", "Gold nugget", "bonus", 2, "ボーナス：得点。"),
    ("bcool", "水滴", "Water drop", "bonus", 2, "ボーナス：体温が全回復する。"),
    ("blife", "ヘルメット", "Helmet", "bonus", 2, "ボーナス：残機が1増える。"),
    ("cave", "横穴", "Cave mouth", "field", 0,
     "敵の湧き出し口。数字の掘り抜きとは別に、土の中へ独立して1ブロックだけ開ける。"),
    ("dia", "ダイヤモンド鉱石", "Diamond block", "field", 0,
     "自機は掘れず通れない。ダイナマイトの爆風だけが壊せ、壊すと採掘率に加算される。"),
]

PROCEDURAL = [
    ("dirt", "土", "Dirt", "面ごとに色が変わる。矩形塗りで一面を塗ってから掘り抜きを彫る。",
     {"colorIndex": 6, "note": "色は paletteVariants で面ごとに差し替える"}),
    ("tunnel", "坑道", "Tunnel", "掘り済みの升目。隣が土の辺だけ内側2pxにして、"
     "隣接する平行トンネルの間に薄い土の膜を残す（本作の肝）。",
     {"colorIndex": 1, "insetPx": 2}),
    ("blast", "爆風", "Blast flash", "爆発の瞬間だけ、十字の5升を1色で塗る。",
     {"colorIndex": 11, "durationFrames": 8}),
    ("hud", "HUD の帯", "HUD band", "画面下 y=176..211 の36px。文字は本体 BIOS の8x8フォント。",
     {"colorIndex": 4, "rect": [0, 176, 256, 36]}),
]


def sprite_cells(path, n):
    im = Image.open(path).convert("RGBA")
    return [im.crop((i * 16, 0, i * 16 + 16, 16)) for i in range(n)]


def sprite_pixels(cell):
    """16行 x 16文字。'.' は透明、それ以外はパレット番号の16進1桁。"""
    px = cell.load()
    rows, line_colors = [], []
    for y in range(16):
        s, count = "", {}
        for x in range(16):
            r, g, b, a = px[x, y]
            if a < 128:
                s += "."
                continue
            idx = nearest((r, g, b))
            count[idx] = count.get(idx, 0) + 1
            s += HEX[idx]
        # mode-2 スプライトは1行1色。念のため最頻色へ寄せる。
        col = max(count, key=count.get) if count else 0
        s = "".join(c if c == "." else HEX[col] for c in s)
        rows.append(s)
        line_colors.append(col)
    return rows, line_colors


def tile_pixels(cell):
    px = cell.load()
    rows = []
    for y in range(16):
        s = ""
        for x in range(16):
            r, g, b, a = px[x, y]
            s += "." if a < 128 else HEX[nearest((r, g, b))]
        rows.append(s)
    return rows


def read_digits(path="src/field.c"):
    src = open(path).read()
    m = re.search(r'static const uint8_t digit_rows\[10\]\[9\] = \{(.*?)\};', src, re.S)
    out = []
    for line in m.group(1).splitlines():
        mm = re.match(r'\s*\{([^}]*)\}', line)
        if not mm:
            continue
        vals = [int(v.strip(), 0) for v in mm.group(1).split(',') if v.strip()]
        out.append(["".join('#' if v & (0x10 >> c) else '.' for c in range(5)) for v in vals])
    return out


def read_stage_dirt(path="src/field.c"):
    src = open(path).read()
    return re.findall(r'vdp_set_palette\(COL_DIRT,\s*(\d+),\s*(\d+),\s*(\d+)\);\s*/\*\s*(.*?)\s*\*/', src)


def main(argv):
    out_path = argv[1] if len(argv) > 1 else "gamedata.json"

    pal_names = {
        1: ("坑道", "COL_TUNNEL"), 2: ("肌", "COL_SKIN"), 3: ("影・長靴", "COL_DARK"),
        4: ("HUDの帯", "COL_HUD"), 5: ("湯", "COL_BATH"), 6: ("土", "COL_DIRT"),
        7: ("作業着", "COL_BODY"), 8: ("ダイナマイト", "COL_RED"), 9: ("ネズミ", "COL_RAT"),
        10: ("導火線・たが", "COL_FUSE"), 11: ("爆風", "COL_BLAST"), 12: ("ダイヤ", "COL_DIA"),
        13: ("コウモリ", "COL_BAT"), 14: ("岩", "COL_ROCK"), 15: ("白", "COL_WHITE"),
    }
    palette = [{
        "index": 0, "rgb3": [0, 0, 0], "rgb8": [0, 0, 0],
        "name": {"ja": "背景色（透明）", "en": "Backdrop / transparent"},
        "symbol": None,
        "note": "SCREEN 5 では 0番は背景色レジスタ R#7 の色で出る。本作は R#7=0（黒）。"
                "スプライトでは透明。",
    }]
    for i in sorted(PALETTE):
        ja, sym = pal_names[i]
        palette.append({
            "index": i, "rgb3": list(PALETTE[i]), "rgb8": list(rgb255(i)),
            "name": {"ja": ja, "en": sym.replace("COL_", "").title()},
            "symbol": sym, "note": None,
        })

    sprites = sprite_cells("assets/sprites.png", len(SPRITE_META))
    tiles_img = sprite_cells("assets/tiles.png", len(TILE_META))

    sprite_patterns, characters = [], []
    for pi, (sid, ja, en, role, desc, anims) in enumerate(SPRITE_META):
        rows, cols = sprite_pixels(sprites[pi])
        pid = "%s_00" % sid
        sprite_patterns.append({
            "id": pid, "patternNumber": pi, "size": [16, 16],
            "pixels": rows,
            "lineColors": cols,
            "derived": ["lineColors"],
            "source": {"file": "assets/sprites.png", "cell": pi},
        })
        animations = []
        for aid in anims:
            aja, aen = ANIM_NAME[aid]
            frames = []
            for k in range(2):
                frames.append({
                    "pattern": pid,
                    "durationFrames": 8,
                    "label": {"ja": "%s%d" % (aja, k + 1), "en": "%s %d" % (aen, k + 1)},
                    "status": "final" if (pi >= 0 and k == 0 and aid in
                                          ("fuse", "idle", "walk_right", "float_right"))
                              else "placeholder",
                })
            animations.append({
                "id": aid, "name": {"ja": aja, "en": aen},
                "loop": True, "frames": frames,
            })
        characters.append({
            "id": sid, "name": {"ja": ja, "en": en}, "role": role,
            "description": desc,
            "spritePlanes": {"miner": [0], "bomb": [1], "rock": [2, 3, 4, 5],
                             "rat": [6, 7, 8, 9], "bat": [6, 7, 8, 9]}[sid],
            "defaultPattern": pid,
            "animations": animations,
        })

    tiles = []
    for ti, (tid, ja, en, kind, inset, desc) in enumerate(TILE_META):
        tiles.append({
            "id": tid, "tileNumber": ti, "size": [16, 16],
            "name": {"ja": ja, "en": en}, "kind": kind,
            "description": desc,
            "drawInsetPx": inset,
            "pixels": tile_pixels(tiles_img[ti]),
            "source": {"file": "assets/tiles.png", "cell": ti},
        })

    dirt = read_stage_dirt()
    variants = []
    for k, (r, g, b, note) in enumerate(dirt):
        variants.append({
            "id": "dirt_%d" % k,
            "name": {"ja": note, "en": "Dirt layer %d" % k},
            "appliesTo": "stages where (stage-1) %% 3 == %d" % k,
            "overrides": [{"index": 6, "rgb3": [int(r), int(g), int(b)]}],
        })

    data = {
        "format": "msx-gamedata",
        "formatVersion": 1,
        "generator": {"tool": "tools/export_gamedata.py", "sourceOfTruth": "assets/*.png"},
        "game": {
            "id": "sakiyama-blast",
            "title": {"ja": "クロダイヤ 〜先山丹野3時〜", "en": "Kuro Daiya"},
            "workspace": "SakiyamaBlast",
            "target": {"machine": "MSX turbo R", "minimum": "MSX2", "cpu": "z80 code"},
            "description": "昭和初期・常磐炭鉱の坑内を舞台にした1画面固定の掘り＋発破アクション。",
        },
        "screen": {
            "mode": "SCREEN5",
            "vdpMode": "GRAPHIC 4",
            "resolution": [256, 212],
            "bitsPerPixel": 4,
            "colorsOnScreen": 16,
            "colorSpace": {"channels": 3, "bitsPerChannel": 3, "totalColors": 512},
            "bytesPerLine": 128,
            "playfield": {"rect": [0, 0, 256, 176], "grid": [16, 11], "cellSize": [16, 16]},
            "hud": {"rect": [0, 176, 256, 36]},
            "vram": {"bitmapPage0": "0x0000", "spriteColorTable": "0x7400",
                     "spriteAttributeTable": "0x7600", "spritePatternTable": "0x7800"},
            "sprite": {"mode": 2, "size": [16, 16], "planes": 32, "activePlanes": 10,
                       "colorsPerLine": 1, "parkY": 212,
                       "note": "mode 2 は1行に1色。Y=216 は行終端マーカーなので使わない。"},
        },
        "palette": {
            "note": "src/main.c の set_game_palette() と tools/palette.py に対応。"
                    "rgb3 が正（各0-7）。rgb8 は表示用の目安。",
            "entries": palette,
        },
        "paletteVariants": variants,
        "spriteFormat": {
            "pixels": "16行 x 16文字。'.' は透明、それ以外はパレット番号の16進1桁。",
            "constraint": "MSX の mode-2 スプライトは1行につき1色しか持てない。"
                          "1行に2色以上を置いてはならない。",
            "lineColors": "各行の色（派生データ。pixels から求められる）。",
        },
        "spritePatterns": sprite_patterns,
        "tileFormat": {
            "pixels": "16行 x 16文字。'.' は透明、それ以外はパレット番号の16進1桁。"
                      "タイルは4bppのビットマップなので1行に何色でも使える。",
            "drawInsetPx": "0 なら16x16全部を描く。2 なら外周2pxを描かない"
                           "（坑道の薄い土の膜を消さないため）。",
        },
        "tiles": tiles,
        "characters": characters,
        "procedural": [
            {"id": i, "name": {"ja": ja, "en": en}, "description": d, "params": p}
            for (i, ja, en, d, p) in PROCEDURAL
        ],
        "stages": {
            "count": 15,
            "carving": {
                "description": "各面の掘り抜きは面番号の算用数字の形。1桁は中央、"
                               "2桁は左右に並べる。自機の出発点・風呂・火薬は数字の線の上。",
                "digitSize": [5, 9],
                "digitTopRow": 1,
                "singleDigitCol": 5,
                "twoDigitCols": [1, 9],
                "digits": read_digits(),
            },
            "perStage": {
                "diamondPercentOfDirt": "(stage-1) * 2  （土の面積に対する割合。ブロック単位で切り上げ）",
                "powderCount": 10,
                "enemyTotal": "4 + stage",
                "enemyOnScreenMax": 4,
                "rockCount": "2 + (stage-1)/7",
                "clearCondition": "採掘率 90%",
            },
        },
        "sources": {
            "spriteSheet": "assets/sprites.png",
            "tileSheet": "assets/tiles.png",
            "titleImage": "assets/title.png",
            "palette": "tools/palette.py",
            "spec": "docs/GAMEDATA_FORMAT.md",
        },
    }

    with open(out_path, "w") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)
        f.write("\n")
    print("%s: スプライト %d / タイル %d / パレット %d色 / 面 %d"
          % (out_path, len(sprite_patterns), len(tiles), len(palette), data["stages"]["count"]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
