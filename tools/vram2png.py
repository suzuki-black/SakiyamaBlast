#!/usr/bin/env python3
"""openMSX から落とした VRAM のダンプを PNG にする（画面を目で見るための道具）。

  python3 tools/vram2png.py dump.txt out.png [倍率] [x,y,幅,高さ]

**生きている VDP パレットで塗る。** 土の色は幕ごとに変えているので、
`tools/palette.py` の既定の表で塗ると**色が嘘になる**（面10 の紫を茶色で
見ていたことがある）。ダンプの先頭に `PAL <64桁>` の行があればそれを使う。

ダンプの作り方（openMSX の -script に渡す Tcl）:

    set f [open dump.txt w]
    set pal ""
    for {set i 0} {$i<32} {incr i} { append pal [format %02x [debug read {VDP palette} $i]] }
    puts $f "PAL $pal"
    for {set y 0} {$y<212} {incr y} {
        set line ""
        for {set x 0} {$x<128} {incr x} { append line [format %02x [debug read VRAM [expr {$y*128+$x}]]] }
        puts $f $line }
    close $f

SCREEN 5 は 1バイト2画素（上位ニブルが左）。1行 128バイト = 256画素。
盤面だけなら 176行、HUD まで見るなら 212行を落とす。
"""
import sys

sys.path.insert(0, __file__.rsplit('/tools/', 1)[0] + '/tools')
from palette import rgb255                                    # noqa: E402

try:
    from PIL import Image
except ImportError:
    sys.exit('Pillow が要る: pip3 install pillow')

HEX = set('0123456789abcdefABCDEF')


def load_palette(lines):
    """`PAL <64桁>` の行から 16色を起こす。1色2バイト (R<<4|B, G)。"""
    for l in lines:
        if not l.startswith('PAL '):
            continue
        h = l[4:].strip()
        if len(h) < 64:
            break
        pal = {}
        for i in range(16):
            b0, b1 = int(h[i * 4:i * 4 + 2], 16), int(h[i * 4 + 2:i * 4 + 4], 16)
            r, b, g = b0 >> 4, b0 & 15, b1 & 7
            pal[i] = (round(r * 255 / 7), round(g * 255 / 7), round(b * 255 / 7))
        return pal
    return None


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    src, dst = sys.argv[1], sys.argv[2]
    zoom = int(sys.argv[3]) if len(sys.argv) > 3 else 3
    lines = [l.strip() for l in open(src)]
    pal = load_palette(lines)
    # パレットが無いダンプは既定の色で塗る。0番は背景なので坑道の黒へ寄せる。
    col = (lambda i: pal[i]) if pal else (lambda i: rgb255(i or 1))
    rows = [l for l in lines if len(l) == 256 and all(c in HEX for c in l)]
    if not rows:
        sys.exit('VRAM の行が見つからない (1行 256桁の16進を期待している)')
    img = Image.new('RGB', (256, len(rows)))
    px = img.load()
    for y, line in enumerate(rows):
        for x in range(128):
            v = int(line[x * 2:x * 2 + 2], 16)
            px[x * 2, y] = col(v >> 4)
            px[x * 2 + 1, y] = col(v & 15)
    if len(sys.argv) > 4:
        x0, y0, w, h = (int(v) for v in sys.argv[4].split(','))
        img = img.crop((x0, y0, x0 + w, y0 + h))
    img = img.resize((img.width * zoom, img.height * zoom), Image.NEAREST)
    img.save(dst)
    print('%s %dx%d %s' % (dst, img.width, img.height,
                           '生きたパレット' if pal else '既定のパレット'))


main()
