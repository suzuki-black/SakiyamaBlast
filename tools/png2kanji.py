#!/usr/bin/env python3
"""PNG の字を 16x16 のドット絵に落として tools/mkkanji.py へ差し込む。

  python3 tools/png2kanji.py assets/kanji/第.png 第 --patch

外部で描いてもらった字を取り込むための道具。画像の大きさ・色数は問わない。
明るさで二値化し、字のある範囲を正方形に切り出して 16x16 に均す。
"""
import argparse, re, sys
from PIL import Image

ROOT = __file__.rsplit('/tools/', 1)[0]
MK = ROOT + '/tools/mkkanji.py'


def to_grid(path, thresh, cover, badge, square, pad):
    im = Image.open(path).convert('L')
    w, h = im.size
    px = im.load()
    lo, hi = im.getextrema()
    if hi - lo < 32:
        sys.exit('画像がほぼ一色。字が見当たらない')
    cut = lo + (hi - lo) * thresh

    # 「Made with AI」の札は右上に出るので既定では無視する
    bx0, by0 = int(w * (1.0 - badge)), 0
    by1 = int(h * 0.12)

    def ink(x, y):
        if px[x, y] >= cut:
            return False
        if badge and x >= bx0 and by0 <= y < by1:
            return False
        return True

    xs = [x for x in range(w) if any(ink(x, y) for y in range(h))]
    ys = [y for y in range(h) if any(ink(x, y) for x in range(w))]
    if not xs or not ys:
        sys.exit('二値化したら何も残らなかった。--thresh を上げてみる')
    x0, x1, y0, y1 = xs[0], xs[-1] + 1, ys[0], ys[-1] + 1

    cw, ch = x1 - x0, y1 - y0
    if square and cw and ch and not 0.8 < cw / float(ch) < 1.25:
        print('警告: 字のある範囲が %dx%d で正方形から外れている。'
              '字面いっぱいに描かれていない字 (一 二 十 など) は、'
              'この道具では余白の取り方まで復元できない。'
              '--no-square と --pad で合わせるか、手で直すこと。'
              % (cw, ch), file=sys.stderr)

    if square:  # 漢字の字面は正方形前提。短い側を中心に広げる
        if cw > ch:
            d = cw - ch
            y0 -= d // 2; y1 += d - d // 2
        elif ch > cw:
            d = ch - cw
            x0 -= d // 2; x1 += d - d // 2

    if pad:  # 余白ぶん外へ広げる (セル数で指定)
        s = (x1 - x0) / 16.0 * pad
        x0 -= s; x1 += s; y0 -= s; y1 += s

    rows = []
    for r in range(16):
        line = ''
        for c in range(16):
            sx0 = x0 + (x1 - x0) * c / 16.0
            sx1 = x0 + (x1 - x0) * (c + 1) / 16.0
            sy0 = y0 + (y1 - y0) * r / 16.0
            sy1 = y0 + (y1 - y0) * (r + 1) / 16.0
            n = tot = 0
            for y in range(int(sy0), max(int(sy0) + 1, int(sy1))):
                for x in range(int(sx0), max(int(sx0) + 1, int(sx1))):
                    tot += 1
                    if 0 <= x < w and 0 <= y < h and ink(x, y):
                        n += 1
            line += '#' if tot and n / tot >= cover else '.'
        rows.append(line)
    return rows


def patch(char, rows):
    src = open(MK).read()
    key = "G['%s'] = [" % char
    if key not in src:
        sys.exit('tools/mkkanji.py に %s が無い' % key)
    a = src.index(key)
    b = src.index('\n]\n', a) + 3
    body = key + '\n' + ''.join('"%s",\n' % r for r in rows) + ']\n'
    open(MK, 'w').write(src[:a] + body + src[b:])


def main():
    p = argparse.ArgumentParser()
    p.add_argument('png')
    p.add_argument('char', help='どの字か (例: 第)')
    p.add_argument('--thresh', type=float, default=0.5, help='二値化のしきい (0-1)')
    p.add_argument('--cover', type=float, default=0.5, help='マスを塗る占有率 (0-1)')
    p.add_argument('--badge', type=float, default=0.45,
                   help='右上のこの幅を無視する (0 で無効)')
    p.add_argument('--no-square', dest='square', action='store_false')
    p.add_argument('--pad', type=float, default=0.0, help='外へ広げる余白 (マス)')
    p.add_argument('--patch', action='store_true', help='mkkanji.py を書き換える')
    a = p.parse_args()

    rows = to_grid(a.png, a.thresh, a.cover, a.badge, a.square, a.pad)
    for r in rows:
        print(r.replace('#', '██').replace('.', '  '))
    print()
    print("G['%s'] = [" % a.char)
    for r in rows:
        print('"%s",' % r)
    print(']')
    if a.patch:
        patch(a.char, rows)
        print('\n-> tools/mkkanji.py の %s を差し替えた' % a.char, file=sys.stderr)


main()
