#!/usr/bin/env python3
"""和太鼓の音を合成して turbo R の PCM 用の波形にする。

  python3 tools/gen_taiko.py build/taiko.bin build/taiko.h

**サンプルは録音ではなく合成**（素材の権利を持ち込まないため。絵と曲と同じ方針）。
宮太鼓・締太鼓・縁打ちの3つを、膜の振動の模型から作る。

| 音 | 使うところ | 中身 |
|---|---|---|
| ドン（宮太鼓） | バスドラの拍 | 低い基音が**下がりながら**減衰する。太鼓の「鳴き」 |
| タン（締太鼓） | スネアの拍 | 高く張った膜。短く締まった音 |
| カッ（縁打ち） | ハイハットの拍 | ほぼ撥の当たる音だけ |

**どれも「基音＋非調和な上の振動＋撥の当たり」**で出来ている。和太鼓の膜は円形で、
倍音が整数比にならない（1 : 1.59 : 2.14 …）。そこをそのまま入れると「太鼓らしさ」が出る。

turbo R の PCM は 8bit 符号なし（中心 0x80）。刻みは 15,700Hz。本作は**走査線割り込み**で
1サンプルずつ出していて、**4行ごと = 3,925Hz**（`src/lowlevel.s`）。

**2行ごと（7,850Hz）は出せなかった。** 割り込みの入口（BIOS の H.KEYI 経由）と VDP の
ポート待ちで1回あたり 100µs 以上かかり、127µs の間隔に間に合わず**1回おきに取りこぼして**
実効 3,925Hz になっていた（録音した太鼓の基音が 80Hz ではなく 40Hz になって気付いた）。
取りこぼして揺れるより、**最初から間に合う間隔で等間隔に出す**ほうが音が良い。
"""
import math
import struct
import sys

RATE = 3925.0            # 走査線4行ごとに1サンプル (src/lowlevel.s)。**実測で決めた値**


def env(t, attack, decay):
    """立ち上がり attack 秒、以後 decay 秒の時定数で減衰する包絡。"""
    if t < attack:
        return t / attack
    return math.exp(-(t - attack) / decay)


def noise(seed=[1]):
    """撥（ばち）の当たる音。線形帰還の擬似乱数を -1..1 で返す。"""
    seed[0] = (seed[0] * 1103515245 + 12345) & 0x7FFFFFFF
    return (seed[0] >> 15) / 32768.0 - 0.5


def drum(ms, f0, f1, bend_ms, partials, decay, noise_amt, noise_ms, drive=1.0):
    """膜の振動の模型。

    f0 -> f1 へ bend_ms で下がる基音に、非調和な上の振動を重ね、
    頭に撥の当たる雑音を少し混ぜる。drive は軽い飽和（太鼓の胴の鳴り）。
    """
    n = int(RATE * ms / 1000.0)
    out = []
    phase = [0.0] * len(partials)
    for i in range(n):
        t = i / RATE
        f = f1 + (f0 - f1) * math.exp(-t / (bend_ms / 1000.0))
        v = 0.0
        for k, (ratio, amp, dec) in enumerate(partials):
            phase[k] += 2.0 * math.pi * f * ratio / RATE
            v += amp * math.sin(phase[k]) * math.exp(-t / dec)
        if t < noise_ms / 1000.0:
            v += noise_amt * noise() * (1.0 - t / (noise_ms / 1000.0))
        v *= env(t, 0.0006, decay)
        v = math.tanh(v * drive) / math.tanh(drive)
        out.append(v)
    return out


def quantize(sig):
    """-1..1 を 8bit 符号なし (中心 0x80) へ。**両端は中心へ寄せる**
    （いきなり跳ぶと D/A がパチッと鳴る）。"""
    peak = max(1e-9, max(abs(v) for v in sig))
    b = bytearray()
    n = len(sig)
    for i, v in enumerate(sig):
        w = v / peak
        if i < 4:                      # 頭の数サンプルで中心から立ち上げる
            w *= i / 4.0
        if i > n - 9:                  # 尻尾も中心へ落とす
            w *= (n - i) / 8.0
        q = int(round(128 + w * 120))
        b.append(max(1, min(254, q)))
    return bytes(b)


def main(argv):
    if len(argv) < 3:
        sys.exit(__doc__)
    # 宮太鼓。低い基音が 95Hz -> 68Hz へ落ちる。膜の非調和な振動を2つ重ねる。
    don = drum(78, 95, 68, 26,
               [(1.0, 1.00, 0.030), (1.59, 0.38, 0.016), (2.14, 0.20, 0.009)],
               decay=0.026, noise_amt=0.55, noise_ms=4.0, drive=1.7)
    # 締太鼓。高く張ってあるので基音が高く、落ちも速い。
    tan = drum(34, 232, 186, 10,
               [(1.0, 1.00, 0.012), (1.59, 0.45, 0.007), (2.30, 0.28, 0.004)],
               decay=0.010, noise_amt=0.85, noise_ms=3.0, drive=1.3)
    # 縁打ち。ほぼ撥の当たる音。木の胴が少し鳴る。
    ka = drum(14, 900, 760, 4,
              [(1.0, 0.75, 0.004), (2.7, 0.45, 0.002)],
              decay=0.0035, noise_amt=1.3, noise_ms=2.2, drive=1.0)

    names = [("DON", don), ("TAN", tan), ("KA", ka)]
    blob = bytearray()
    heads = []
    for name, sig in names:
        heads.append((name, len(blob), len(sig)))
        blob += quantize(sig)
    open(argv[1], "wb").write(bytes(blob))

    with open(argv[2], "w") as f:
        f.write("/* tools/gen_taiko.py から自動生成。手で編集しない。\n"
                "   和太鼓の波形の位置と長さ (ROM の TAIKO_SEG 内の相対番地)。 */\n")
        for name, off, ln in heads:
            f.write("#define TAIKO_%s_OFF %d\n" % (name, off))
            f.write("#define TAIKO_%s_LEN %d\n" % (name, ln))
        f.write("#define TAIKO_TOTAL %d\n" % len(blob))
    print("%s: %d バイト (%s)" % (argv[1], len(blob),
                                  ", ".join("%s %d" % (n, l) for n, _, l in heads)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
