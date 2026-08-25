#!/usr/bin/env python3
"""tools/palette.py から C のパレット表を生成する。

パレットを tools/palette.py と src/main.c の2か所に書くと、片方だけ直す事故が起きる。
色の値はここで一本化し、C 側はこの表を舐めるだけにする。
"""
import sys

sys.path.insert(0, __file__.rsplit('/', 1)[0])
from palette import PALETTE

out = sys.argv[1] if len(sys.argv) > 1 else "build/palette.h"
lines = [
    "/* tools/palette.py から自動生成。手で編集しない。 */",
    "/* MSX2 のパレットは R/G/B 各 0-7。0番は背景色(R#7)として出るので触らない。 */",
    "static const uint8_t game_palette[16][3] = {",
    "    { 0, 0, 0 },   /*  0: 背景色 (透明) */",
]
names = {1: "坑道", 2: "肌", 3: "影・長靴", 4: "HUDの帯", 5: "湯", 6: "土", 7: "作業着",
         8: "ダイナマイト", 9: "ネズミ", 10: "導火線・たが", 11: "爆風", 12: "ダイヤ",
         13: "コウモリ", 14: "岩", 15: "白"}
for i in range(1, 16):
    r, g, b = PALETTE[i]
    lines.append("    { %d, %d, %d },   /* %2d: %s */" % (r, g, b, i, names[i]))
lines.append("};")
open(out, "w").write("\n".join(lines) + "\n")
print("%s: 16色" % out)
