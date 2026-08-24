#!/usr/bin/env python3
"""PNG の絵を MSX 用のデータへ変換する。絵の差し替えはこの PNG を描き替えるだけでよい。

2 種類を扱う:

  --mode sprite  16x16 のスプライト。MSX の mode-2 スプライトは **1ビットの形** ＋
                 **1行に1色** という制約があるので、各行で一番多い色をその行の色にする。
                 出力: <name>_pat[16] (16bitの形) と <name>_col[16] (行ごとの色)

  --mode tile    16x16 の背景タイル。SCREEN 5 の 4bpp でそのまま持つ (1セル128バイト)。
                 出力: <name>[N][128]

透明 (アルファ0) または完全な黒でない背景色は「描かない画素」として扱う。
セルは左から右へ並べる。名前は --names で順に与える。
"""
import argparse
import sys

from PIL import Image

sys.path.insert(0, __file__.rsplit('/', 1)[0])
from palette import nearest


def cells(im, n):
    w, h = im.size
    if h < 16 or w < 16 * n:
        raise SystemExit("PNG が小さすぎます: %dx%d に %d セル入りません" % (w, h, n))
    return [im.crop((i * 16, 0, i * 16 + 16, 16)) for i in range(n)]


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("out")
    ap.add_argument("--mode", choices=("sprite", "tile"), required=True)
    ap.add_argument("--names", required=True, help="セルの名前をカンマ区切りで")
    ap.add_argument("--array", default="art", help="tile モードでの配列名")
    args = ap.parse_args(argv[1:])

    names = args.names.split(",")
    im = Image.open(args.src).convert("RGBA")
    cs = cells(im, len(names))

    lines = ["/* %s から自動生成 (tools/png2art.py)。手で編集しない。" % args.src,
             "   絵を変えるときは PNG のほうを描き替えて make し直す。 */"]

    if args.mode == "sprite":
        pats, cols = [], []
        for cell, name in zip(cs, names):
            px = cell.load()
            pat, col = [], []
            for y in range(16):
                bits = 0
                count = {}
                for x in range(16):
                    r, g, b, a = px[x, y]
                    if a < 128:
                        continue
                    bits |= 1 << (15 - x)
                    idx = nearest((r, g, b))
                    count[idx] = count.get(idx, 0) + 1
                pat.append(bits)
                col.append(max(count, key=count.get) if count else 0)
            pats.append((name, pat))
            cols.append((name, col))
        lines.append("")
        lines.append("static const uint16_t art_spr_pat[%d][16] = {" % len(names))
        for name, pat in pats:
            lines.append("    {   /* %s */" % name)
            for y in range(0, 16, 4):
                lines.append("        " + " ".join("0x%04X," % v for v in pat[y:y + 4]))
            lines.append("    },")
        lines.append("};")
        lines.append("")
        lines.append("static const uint8_t art_spr_col[%d][16] = {" % len(names))
        for name, col in cols:
            lines.append("    { " + " ".join("%2d," % v for v in col) + " },   /* %s */" % name)
        lines.append("};")
        for i, name in enumerate(names):
            lines.append("#define ART_%s %d" % (name.upper(), i))
    else:
        lines.append("")
        lines.append("static const uint8_t %s[%d][128] = {" % (args.array, len(names)))
        for cell, name in zip(cs, names):
            px = cell.load()
            lines.append("    {   /* %s */" % name)
            for y in range(16):
                row = []
                for x in range(0, 16, 2):
                    v = []
                    for k in (0, 1):
                        r, g, b, a = px[x + k, y]
                        v.append(0 if a < 128 else nearest((r, g, b)))
                    row.append((v[0] << 4) | v[1])
                lines.append("        " + " ".join("0x%02X," % b for b in row))
            lines.append("    },")
        lines.append("};")
        for i, name in enumerate(names):
            lines.append("#define TILE_%s %d" % (name.upper(), i))

    open(args.out, "w").write("\n".join(lines) + "\n")
    print("%s: %d セル (%s)" % (args.out, len(names), args.mode))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
