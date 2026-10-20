#!/usr/bin/env python3
"""openMSX から落とした VRAM のダンプを PNG にする（画面を目で見るための道具）。

  python3 tools/vram2png.py dump.txt out.png [倍率] [x,y,幅,高さ]

**生きている VDP パレットで塗る。** 土の色は幕ごとに変えているので、
`tools/palette.py` の既定の表で塗ると**色が嘘になる**（面10 の紫を茶色で
見ていたことがある）。ダンプの先頭に `PAL <64桁>` の行があればそれを使う。

**スプライトも重ねる。** ビットマップだけを落とすと、自機も敵も岩も写らない
（スプライトは VDP が重ねているだけで、ビットマップには描かれていない）。
`SPRATR` / `SPRCOL` / `SPRPAT` の行があれば mode 2 の規則どおりに重ねる
（1行1色、面番号の小さいほうが手前、色0は透明）。**1走査線に9枚以上あると
実機では9枚目から消える**ので、そのときは警告を出す。

ダンプの作り方（openMSX の -script に渡す Tcl）:

    proc hx {addr len} { binary scan [debug read_block VRAM $addr $len] H* h; return $h }
    set f [open dump.txt w]
    set pal ""
    for {set i 0} {$i<32} {incr i} { append pal [format %02x [debug read {VDP palette} $i]] }
    puts $f "PAL $pal"
    puts $f "SPRATR [hx 0x7600 128]"      ;# 属性 (Y, X, パターン) x 32面
    puts $f "SPRCOL [hx 0x7400 512]"      ;# 行ごとの色 x 32面
    puts $f "SPRPAT [hx 0x7800 2048]"     ;# 形
    set b [hx 0 [expr {212*128}]]
    for {set y 0} {$y<212} {incr y} { puts $f [string range $b [expr {$y*256}] [expr {$y*256+255}]] }
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
SPR_PLANES = 32
LINE_MAX = 8          # 1走査線に出せる枚数 (9枚目から消える)


def tagged(lines, tag, nbytes):
    """`TAG <16進>` の行を取り出してバイト列にする。無ければ None。"""
    for l in lines:
        if l.startswith(tag + ' '):
            h = l[len(tag) + 1:].strip()
            if len(h) < nbytes * 2:
                sys.exit('%s の桁数が足りない (%d 桁、%d 桁必要)' % (tag, len(h), nbytes * 2))
            return bytes.fromhex(h[:nbytes * 2])
    return None


def load_palette(lines):
    """`PAL <64桁>` の行から 16色を起こす。1色2バイト (R<<4|B, G)。"""
    raw = tagged(lines, 'PAL', 32)
    if raw is None:
        return None
    pal = {}
    for i in range(16):
        b0, b1 = raw[i * 2], raw[i * 2 + 1]
        r, b, g = b0 >> 4, b0 & 15, b1 & 7
        pal[i] = (round(r * 255 / 7), round(g * 255 / 7), round(b * 255 / 7))
    return pal


def sprite_pixels(lines, height):
    """mode 2 のスプライトを (y, x) -> 色番号 の辞書にする。

    面番号の小さいほうが手前なので、**大きいほうから順に塗る**。
    色のバイトは bit7=EC (32px 左へ), bit6=CC, bit5=IC, 下位4bit が色。
    """
    atr = tagged(lines, 'SPRATR', SPR_PLANES * 4)
    col = tagged(lines, 'SPRCOL', SPR_PLANES * 16)
    pat = tagged(lines, 'SPRPAT', 2048)
    if atr is None or col is None or pat is None:
        return {}, []
    px = {}
    per_line = [[] for _ in range(height)]
    for p in range(SPR_PLANES - 1, -1, -1):
        y, x, pn = atr[p * 4], atr[p * 4 + 1], atr[p * 4 + 2] & 0xFC
        if y == 216:                      # 以降の面は表示されない (行終端)
            continue
        base = pn * 8                     # パターン番号 x 32 バイト (16x16)
        yy = y - 256 if y >= 225 else y   # **225〜255 は負の座標** (-31〜-1)。
                                          # 画面の上から入ってくる絵のため。
                                          # ここを素直に足すと、行0 に置いた岩が
                                          # 「写らない」と誤診する (2026-10-20)
        for r in range(16):
            sy = yy + 1 + r               # VDP は Y+1 行目から出す
            if sy < 0 or sy >= height:
                continue
            # **枚数は形や色に関係なく数える。** その行に掛かっていれば、
            # 透明でも VDP は1枚ぶん使う。
            per_line[sy].append(p)
            c = col[p * 16 + r]
            bits = (pat[base + r] << 8) | pat[base + 16 + r]
            if not bits or not (c & 15):  # 色0 = 透明 (形があっても描かない)
                continue
            sx = x - 32 if (c & 0x80) else x
            for k in range(16):
                if bits & (1 << (15 - k)):
                    xx = sx + k
                    if 0 <= xx < 256:
                        px[(sy, xx)] = c & 15
    over = [(y, len(v)) for y, v in enumerate(per_line) if len(v) > LINE_MAX]
    return px, over


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
    spr, over = sprite_pixels(lines, len(rows))
    img = Image.new('RGB', (256, len(rows)))
    px = img.load()
    for y, line in enumerate(rows):
        for x in range(128):
            v = int(line[x * 2:x * 2 + 2], 16)
            px[x * 2, y] = col(v >> 4)
            px[x * 2 + 1, y] = col(v & 15)
    for (y, x), c in spr.items():
        px[x, y] = col(c)
    if len(sys.argv) > 4:
        x0, y0, w, h = (int(v) for v in sys.argv[4].split(','))
        img = img.crop((x0, y0, x0 + w, y0 + h))
    img = img.resize((img.width * zoom, img.height * zoom), Image.NEAREST)
    img.save(dst)
    print('%s %dx%d %s / スプライト %d 画素'
          % (dst, img.width, img.height,
             '生きたパレット' if pal else '既定のパレット', len(spr)))
    for y, n in over[:4]:
        print('  警告: %d 行目にスプライトが %d 枚 (実機では %d 枚目から消える)'
              % (y, n, LINE_MAX + 1))


main()
