"""BDF から必要な字だけ 16x16 の ASCII アートで取り出す (tools/mkkanji.py へ貼る用)。

**なぜ要るか**: タイトルの副題に使う 先・山・丹・野・時 は、16x16 で手で起こすと
字として成立しなかった (2026-10-25)。**Public Domain のビットマップフォントから
必要な字だけを取り出して組み込む**ほうが確実で、権利の扱いもはっきりする。

使った書体: 東雲フォント 0.9.11 (The Electronic Font Open Laboratory)
            http://openlab.ring.gr.jp/efont/
            同梱の LICENSE に「すべて Public Domain で提供されています」
            「自由な改造、他フォーマットへの変換、組込み、再配布を行うことができます」
            と明記されている。**元絵の重い字面に合わせてボールド (shnmk16b.bdf) を使った。**

  python3 -I bdfpick.py <bdf> <文字...>        全角 (16x16)
  python3 -I bdfpick.py --half <bdf> <文字...>  半角 (8x16 を 16x16 の中央へ)
"""
import sys

def load(path):
    glyphs, enc, rows, inbits, bbx = {}, None, [], False, None
    for line in open(path, 'rb'):
        line = line.strip()
        if line.startswith(b'ENCODING'):
            enc = int(line.split()[1])
        elif line.startswith(b'BBX'):
            bbx = [int(v) for v in line.split()[1:]]
        elif line == b'BITMAP':
            inbits, rows = True, []
        elif line == b'ENDCHAR':
            glyphs[enc] = (bbx, rows)
            inbits = False
        elif inbits:
            rows.append(line.decode())
    return glyphs

def jis(ch):
    b = ch.encode('euc_jp')
    return ((b[0] - 0x80) << 8) | (b[1] - 0x80)

def art(bbx, rows, half):
    w, h, _, _ = bbx
    out = []
    for r in rows:
        v = int(r, 16) >> (len(r) * 4 - w)
        out.append(''.join('#' if (v >> (w - 1 - i)) & 1 else '.' for i in range(w)))
    while len(out) < 16:
        out.append('.' * w)
    out = out[:16]
    if half:                      # 8x16 を 16x16 の中央へ
        out = [('....' + r + '....')[:16] for r in out]
    return [(r + '.' * 16)[:16] for r in out]

def main(argv):
    half = argv[0] == '--half'
    if half:
        argv = argv[1:]
    g = load(argv[0])
    for ch in argv[1]:
        key = ord(ch) if half else jis(ch)
        if key not in g:
            print("見つからない: %s (0x%04X)" % (ch, key), file=sys.stderr)
            continue
        bbx, rows = g[key]
        print("G['%s'] = [" % ch)
        for r in art(bbx, rows, half):
            print('    "%s",' % r)
        print("]")

main(sys.argv[1:])
