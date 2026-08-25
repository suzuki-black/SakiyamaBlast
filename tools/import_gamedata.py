#!/usr/bin/env python3
"""gamedata.json をゲームへ取り込む（パターンエディタで編集したものを反映する）。

絵の正は assets の PNG なので、JSON の `pixels` から PNG を書き戻す。
書き戻したあとは既存の `tools/png2art.py` がそのまま C の配列へ変換する。

  python3 tools/import_gamedata.py gamedata.json \\
      --sprites assets/sprites.png --sprite-names miner,bomb,rock,rat,bat \\
      --tiles   assets/tiles.png   --tile-names powder,bath,...

**検証に通らなければ PNG は一切書き換えない。** MSX で表示できない絵を
そのまま取り込むと、原因の分かりにくい不具合になるため。
形式の仕様は docs/GAMEDATA_FORMAT.md。
"""
import argparse
import json
import sys

from PIL import Image

sys.path.insert(0, __file__.rsplit('/', 1)[0])
from palette import PALETTE, rgb255

HEX = "0123456789ABCDEF"
FORMAT = "msx-gamedata"
VERSION = 1


class Bad(Exception):
    pass


def check_pixels(rows, where, sprite):
    """16行x16文字であること。スプライトは1行1色であること。"""
    if not isinstance(rows, list) or len(rows) != 16:
        raise Bad("%s: pixels は16行でなければならない (実際 %s行)"
                  % (where, len(rows) if isinstance(rows, list) else "?"))
    for y, row in enumerate(rows):
        if not isinstance(row, str) or len(row) != 16:
            raise Bad("%s 行%d: 16文字でなければならない (実際 %d文字)"
                      % (where, y, len(row) if isinstance(row, str) else -1))
        used = set()
        for x, ch in enumerate(row):
            if ch == ".":
                continue
            if ch not in HEX:
                raise Bad("%s 行%d 桁%d: '%s' は使えない。'.' か 0-F のみ。"
                          % (where, y, x, ch))
            idx = HEX.index(ch)
            if idx == 0:
                raise Bad("%s 行%d 桁%d: 0番は背景色かつ透明。"
                          "黒を塗るなら '1' を使う (仕様書 §9-4)。" % (where, y, x))
            if idx not in PALETTE:
                raise Bad("%s 行%d 桁%d: パレット %d番は定義されていない。" % (where, y, x, idx))
            used.add(idx)
        if sprite and len(used) > 1:
            raise Bad("%s 行%d: mode-2 スプライトは1行1色。この行に %d色ある (%s)。"
                      % (where, y, len(used), ",".join(str(u) for u in sorted(used))))


def check_inset(rows, inset, where):
    if inset != 2:
        return None
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch == ".":
                continue
            if y < 2 or y > 13 or x < 2 or x > 13:
                return ("%s: drawInsetPx=2 なので外周2pxは描かれない。"
                        "行%d 桁%d の点は表示されない (絵は内側12x12に収めること)。"
                        % (where, y, x))
    return None


def cell_order(items, kind):
    """PNG のセル順に並べ替える。source.cell が正、無ければ配列順。"""
    order = []
    for i, it in enumerate(items):
        c = it.get("source", {}).get("cell")
        order.append((i if c is None else c, it))
    order.sort(key=lambda t: t[0])
    seen = [c for c, _ in order]
    if seen != list(range(len(order))):
        raise Bad("%s: source.cell が 0..%d の連番になっていない (%s)。"
                  % (kind, len(order) - 1, seen))
    return [it for _, it in order]


def build_png(items, names, kind):
    """16x16 のセルを cell 番号順に横へ並べた PNG を作る。"""
    items = cell_order(items, kind)
    if len(items) != len(names):
        raise Bad("%s: JSON に %d枚あるが Makefile の名前は %d個 "
                  "(%s)。名前の並びを JSON と揃えること。"
                  % (kind, len(items), len(names), ",".join(names)))

    im = Image.new("RGBA", (16 * len(items), 16), (0, 0, 0, 0))
    px = im.load()
    warns = []
    for i, it in enumerate(items):
        rows = it["pixels"]
        where = "%s cell%d '%s' (-> %s)" % (kind, i, it["id"], names[i])
        check_pixels(rows, where, kind == "sprite")
        w = check_inset(rows, it.get("drawInsetPx", 0), where)
        if w:
            warns.append(w)
        for y in range(16):
            for x in range(16):
                ch = rows[y][x]
                if ch == ".":
                    continue
                r, g, b = rgb255(HEX.index(ch))
                px[i * 16 + x, y] = (r, g, b, 255)
    return im, warns


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("--sprites", default="assets/sprites.png")
    ap.add_argument("--sprite-names", required=True)
    ap.add_argument("--tiles", default="assets/tiles.png")
    ap.add_argument("--tile-names", required=True)
    ap.add_argument("--dry-run", action="store_true", help="検証だけして書き込まない")
    args = ap.parse_args(argv[1:])

    data = json.load(open(args.src))
    if data.get("format") != FORMAT:
        raise Bad("format が '%s' ではない (%r)。" % (FORMAT, data.get("format")))
    if data.get("formatVersion") != VERSION:
        raise Bad("formatVersion %s には未対応 (このツールは %d)。"
                  % (data.get("formatVersion"), VERSION))

    # パレットの食い違いは黙って直さない。絵の意味が変わるので必ず知らせる。
    pal_warn = []
    for e in data.get("palette", {}).get("entries", []):
        i = e["index"]
        if i == 0:
            continue
        if i not in PALETTE:
            pal_warn.append("パレット %d番が JSON にあるがゲーム側に無い。" % i)
        elif list(PALETTE[i]) != list(e["rgb3"]):
            pal_warn.append("パレット %2d番の色が違う: ゲーム %s / JSON %s  -> "
                            "tools/palette.py を直すこと。"
                            % (i, list(PALETTE[i]), e["rgb3"]))

    sp_names = args.sprite_names.split(",")
    tl_names = args.tile_names.split(",")
    spr_im, w1 = build_png(data["spritePatterns"], sp_names, "sprite")
    tile_im, w2 = build_png(data["tiles"], tl_names, "tile")

    for w in pal_warn + w1 + w2:
        print("  注意: %s" % w, file=sys.stderr)
    if args.dry_run:
        print("検証のみ: 問題なし (書き込みはしていない)")
        return 0
    spr_im.save(args.sprites)
    tile_im.save(args.tiles)
    print("%s: スプライト %d枚 / %s: タイル %d枚 を書き戻した"
          % (args.sprites, len(sp_names), args.tiles, len(tl_names)))
    if pal_warn:
        print("パレットは自動では直していない。上の注意を見て tools/palette.py を直すこと。",
              file=sys.stderr)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv))
    except Bad as e:
        print("取り込み中止: %s" % e, file=sys.stderr)
        sys.exit(1)
