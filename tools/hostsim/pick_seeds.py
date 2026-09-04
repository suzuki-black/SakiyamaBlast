#!/usr/bin/env python3
"""面ごとの盤面の種を総当たりして選ぶ。

盤面は面番号だけで決まる（`src/field.c` の `stage_seed[]`）。**その1つが悪ければ
そのまま出荷される**ので、狙った手応えになる種を探して固定する。

貪欲ボットは面1すらクリアできないので判定には使わない。代わりに**岩と炭層の
位置関係だけを静的に測る**（`tools/hostsim/sim.c` の analyse）。

    free  : 岩の真下（同じ列）にある黒ダイヤの数。落ちる岩は列を貫いて全部採るので、
            **そのまま「何もしなくても手に入る連鎖の長さ」**になる
    haul  : 一番長い列へ岩を寄せるのに要る横移動の升数＝**岩の移動の面倒さ**
    colmax: 一番長い縦の炭層＝一気に狙える連鎖の上限

狙い（作者の指示）:
    低次 … 何もしなくても連鎖してそのままクリアできる  → free 大 / haul 0
    高次 … 一気の連鎖が難しく、岩の移動が面倒          → free 0 / haul 大 / colmax 小

    python3 tools/hostsim/pick_seeds.py            # 探して表を出すだけ
    python3 tools/hostsim/pick_seeds.py --write    # src/field.c に書き込む
"""
import json, re, subprocess, sys

SIM = 'tools/hostsim/sim'
FIELD = 'src/field.c'
SWEEP = 60000

#      面: (free, haul, colmax)
TARGET = {
    1: (4, 0, 4), 2: (4, 0, 4), 3: (3, 0, 4),
    4: (3, 1, 3), 5: (3, 1, 3), 6: (2, 1, 3),
    7: (2, 2, 3), 8: (2, 2, 3), 9: (1, 3, 2),
    10: (1, 3, 2), 11: (1, 4, 2), 12: (0, 4, 2),
    13: (0, 5, 2), 14: (0, 5, 2), 15: (0, 6, 2),
}
W_FREE, W_HAUL, W_COLMAX = 5.0, 2.0, 1.0   # free を最優先する (狙いの中心)


def sweep(stage):
    out = subprocess.run([SIM, '--stage', str(stage), '--sweep', str(SWEEP)],
                         capture_output=True, text=True)
    if out.returncode:
        sys.exit('sim が失敗した: ' + out.stderr[:200])
    return [json.loads(l) for l in out.stdout.splitlines() if l.startswith('{')]


def score(row, want):
    tf, th, tc = want
    return (W_FREE * abs(row['free'] - tf)
            + W_HAUL * abs(row['haul'] - th)
            + W_COLMAX * abs(row['colmax'] - tc))


def main():
    print('%3s %6s %5s %5s %7s %6s %6s  %s' %
          ('面', '種', 'free', 'haul', 'colmax', '炭', '塊', '狙い (free/haul/colmax)'))
    seeds = []
    for st in range(1, 16):
        rows = sweep(st)
        want = TARGET[st]
        best = min(rows, key=lambda r: (score(r, want), r['seed']))
        seeds.append(best['seed'])
        print('%3d %6d %5d %5d %7d %6d %6d  %d/%d/%d%s' %
              (st, best['seed'], best['free'], best['haul'], best['colmax'],
               best['coal'], best['veins'], want[0], want[1], want[2],
               '' if score(best, want) == 0 else '  (完全一致なし)'))
    if '--write' in sys.argv:
        src = open(FIELD).read()
        body = ',\n    '.join(', '.join('%5d' % s for s in seeds[i:i + 5])
                              for i in range(0, 15, 5))
        src = re.sub(r'(const uint16_t stage_seed\[15\] = \{\n)(.*?)(\n\};)',
                     lambda m: m.group(1) + '    ' + body + ',' + m.group(3),
                     src, count=1, flags=re.S)
        open(FIELD, 'w').write(src)
        print('\n-> %s の stage_seed[] を書き換えた' % FIELD)


main()
