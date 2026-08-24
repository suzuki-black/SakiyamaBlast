#!/usr/bin/env python3
"""PNG を MSX SCREEN 5 (GRAPHIC 4) の画像データへ変換する。

SC5 は 256x212 / 1ピクセル4bit。パレットは 512色 (R/G/B 各0-7) の中から16色を選べる。
そこで「元絵を512色空間へ丸める → 16色へ減色 → パレットを3bitへ確定」という順で処理する。

出力:
  <out>.bin      ピクセルのみ 27,136バイト。8KB のバンク境界にそのまま乗るよう、
                 パレットは混ぜない (バンク n が VRAM の 8KB ブロック n に1対1で対応する)。
  <out>_pal.h    パレット16色を C の配列として (コード側に置く)
  <out>.png      MSX で表示されるのと同じ見た目のプレビュー (確認用)

パレット0は背景色 (R#7) として表示されるため、一番暗い色を0番に寄せておく。
"""
import argparse
import sys

from PIL import Image

SCR_W, SCR_H = 256, 212


def snap3(v):
    """0-255 を MSX の 3bit 階調 (0-7) へ"""
    return min(7, max(0, int(round(v * 7 / 255))))


def expand3(v3):
    return int(round(v3 * 255 / 7))


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("out", help="出力の拡張子なしパス (.bin と .png を作る)")
    ap.add_argument("--crop", help="切り出し x0,y0,x1,y1")
    ap.add_argument("--black", action="append", default=[],
                    help="黒で塗り潰す矩形 x0,y0,x1,y1 (複数指定可)")
    ap.add_argument("--dither", action="store_true", help="誤差拡散ディザをかける")
    ap.add_argument("--blackfloor", type=int, default=1,
                    help="全chがこの階調(0-7)以下の色を黒へ寄せる。背景ノイズに"
                         "パレットを食われるのを防ぐ。0で無効")
    ap.add_argument("--colors", type=int, default=16)
    args = ap.parse_args(argv[1:])

    im = Image.open(args.src).convert("RGB")

    # 邪魔なもの (透かし等) を背景色で消す
    for r in args.black:
        x0, y0, x1, y1 = (int(v) for v in r.split(","))
        im.paste((0, 0, 0), (x0, y0, x1, y1))

    if args.crop:
        x0, y0, x1, y1 = (int(v) for v in args.crop.split(","))
        im = im.crop((x0, y0, x1, y1))

    # 縮小は面積平均。ノイズを含む絵を大きく縮めるので、リンギングの出ない BOX が安全。
    im = im.resize((SCR_W, SCR_H), Image.BOX)

    # 先に 512色空間へ丸めてから減色する (減色後に丸めると色が潰れて重複するため)
    snapped = Image.new("RGB", im.size)
    sp = snapped.load()
    ip = im.load()
    floor = args.blackfloor
    for y in range(SCR_H):
        for x in range(SCR_W):
            r, g, b = ip[x, y]
            r3, g3, b3 = snap3(r), snap3(g), snap3(b)
            # 背景の黒にはノイズが乗っている。まとめて真っ黒にしないと、
            # 減色器が見分けのつかない暗色に何枠も使ってしまう。
            if floor and r3 <= floor and g3 <= floor and b3 <= floor:
                r3 = g3 = b3 = 0
            sp[x, y] = (expand3(r3), expand3(g3), expand3(b3))

    dither = Image.FLOYDSTEINBERG if args.dither else Image.NONE
    q = snapped.quantize(colors=args.colors, method=Image.MEDIANCUT, dither=dither)

    pal = q.getpalette()[: args.colors * 3]
    entries = [(snap3(pal[i * 3]), snap3(pal[i * 3 + 1]), snap3(pal[i * 3 + 2]))
               for i in range(args.colors)]

    # 一番暗い色を 0番へ (SC5 は 0番が背景色 R#7 として出るため、黒を置くと都合が良い)
    order = sorted(range(args.colors), key=lambda i: sum(entries[i]))
    remap = [0] * args.colors
    for new, old in enumerate(order):
        remap[old] = new
    entries = [entries[i] for i in order]

    src = q.load()
    pixels = bytearray(SCR_W * SCR_H // 2)
    prev = Image.new("RGB", (SCR_W, SCR_H))
    pv = prev.load()
    for y in range(SCR_H):
        row = y * (SCR_W // 2)
        for x in range(0, SCR_W, 2):
            a = remap[src[x, y]]
            b = remap[src[x + 1, y]]
            pixels[row + (x >> 1)] = (a << 4) | b
            for k, idx in ((0, a), (1, b)):
                r, g, bb = entries[idx]
                pv[x + k, y] = (expand3(r), expand3(g), expand3(bb))

    palbytes = bytearray()
    for (r, g, b) in entries:
        palbytes.append((r << 4) | b)      # MSX のパレット形式: 1バイト目 = R*16+B
        palbytes.append(g)                 #                     2バイト目 = G

    with open(args.out + ".bin", "wb") as f:
        f.write(pixels)
    name = args.out.rsplit("/", 1)[-1]
    with open(args.out + "_pal.h", "w") as f:
        f.write("/* %s から自動生成 (tools/png2sc5.py)。手で編集しない。 */\n" % args.src.rsplit("/", 1)[-1])
        f.write("/* MSX パレット形式: 1バイト目 = R*16+B, 2バイト目 = G (各 0-7) */\n")
        f.write("static const uint8_t %s_palette[32] = {\n" % name)
        for i in range(0, len(palbytes), 2):
            f.write("    0x%02X, 0x%02X,   /* %2d: R=%d G=%d B=%d */\n"
                    % (palbytes[i], palbytes[i + 1], i // 2,
                       entries[i // 2][0], entries[i // 2][1], entries[i // 2][2]))
        f.write("};\n")
    prev.save(args.out + ".png")

    print("%s.bin: ピクセル %d バイト (8KB バンク %d 本ぶん)"
          % (args.out, len(pixels), (len(pixels) + 8191) // 8192))
    print("パレット (R,G,B は 0-7):")
    for i, (r, g, b) in enumerate(entries):
        print("  %2d: %d,%d,%d" % (i, r, g, b))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
