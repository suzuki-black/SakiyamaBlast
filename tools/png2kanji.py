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


def to_grid(path, thresh, cover, badge, square, pad, align, shift):
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
    if square:
        # マスは正方形。広いほうの辺から一辺 (16分の1) を決め、
        # 狭いほうは --align で寄せる。縦横で別々に伸ばすと字が歪む。
        p16 = max(cw, ch)
        if cw < p16:
            d = p16 - cw
            x0 -= d if align == 'end' else (d // 2 if align == 'center' else 0)
        if ch < p16:
            d = p16 - ch
            y0 -= d if align == 'end' else (d // 2 if align == 'center' else 0)
        x1, y1 = x0 + p16, y0 + p16
        if abs(cw - ch) > p16 / 12.0:
            print('警告: 字のある範囲が %dx%d で正方形から外れている。'
                  '狭いほうを --align start|center|end で寄せた (いまは %s)。'
                  '見て合わなければ他の寄せ方も試すこと。' % (cw, ch, align),
                  file=sys.stderr)

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

    if shift:                      # マス単位でずらす (寄せの微調整)
        dx, dy = shift
        blank = '.' * 16
        rows = ([blank] * dy + rows)[:16] if dy > 0 else (rows[-dy:] + [blank] * -dy)
        if dx > 0:
            rows = [(('.' * dx) + r)[:16] for r in rows]
        elif dx < 0:
            rows = [(r[-dx:] + '.' * -dx) for r in rows]
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
    p.add_argument('--no-square', dest='square', action='store_false',
                   help='縦横を別々に伸ばして枠いっぱいにする（字は歪む）')
    p.add_argument('--align', default='center', choices=['start', 'center', 'end'],
                   help='正方形にするとき狭い側をどこへ寄せるか')
    p.add_argument('--pad', type=float, default=0.0, help='外へ広げる余白 (マス)')
    p.add_argument('--shift', default='0,0',
                   help='結果をマス単位でずらす (例: 0,1 で1マス下へ)')
    p.add_argument('--patch', action='store_true', help='mkkanji.py を書き換える')
    a = p.parse_args()

    rows = to_grid(a.png, a.thresh, a.cover, a.badge, a.square, a.pad, a.align,
                   tuple(int(v) for v in a.shift.split(',')))
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
