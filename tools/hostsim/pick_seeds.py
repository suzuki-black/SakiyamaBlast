#!/usr/bin/env python3
"""面ごとの盤面の種を総当たりして選ぶ。

盤面は面番号だけで決まる（`src/field.c` の `stage_seed[]`）。**その1つが悪ければ
そのまま出荷される**ので、狙った手応えになる種を探して固定する。

貪欲ボットは面1すらクリアできないので判定には使わない。代わりに**岩と炭層の
位置関係だけを静的に測る**（`tools/hostsim/sim.c` の analyse）。

    free  : 岩の真下（同じ列）にある黒ダイヤの数。落ちる岩は列を貫いて全部採るので、
            **そのまま「何もしなくても手に入る連鎖の長さ」**になる
    slide : 落盤を実際に起こしたとき、岩が横滑りした回数（静的には測れないので実行する）
    sgain : **滑り終わった岩の列の下にある黒ダイヤの数。**
            落ちる岩は列を貫いて全部採るので「滑ったことで手に入る炭の数」。
            滑っても何も採らない盤面と区別するために要る
    combo : そのとき届いた連鎖の長さ
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

# **指標を作り替えた (2026-09-09)。**
#
# 旧: free (岩の真下の黒ダイヤ数) を最大化していた。
# 新: **着地衝撃波が主機構**なので、岩は炭層の真上ではなく**横**に要る。
#     真上に置くと、着地する頃には真下の炭を縦に採り終えていて横に何も残らない。
#     縦の連鎖と横の連鎖が同じ炭層を奪い合う。
#
#   taken … 落盤ひとつで採れる黒ダイヤの数。**難易度の軸**
#   free  … 岩の真下の黒ダイヤ数。**低く抑える** (縦に頼らない)
#   side  … 岩が沈んだ先の左右3升にある黒ダイヤ数。**衝撃波の的。最大化**
#           耐久に依存しないので、高次面で「いまは割れないが仕込めば割れる」形に
#           なっているかを測れる
#
# 高次面で taken が下がるのは耐久が高くて衝撃で割れないため。**それが狙い**で、
# つるはしで仕込んでから落とす形になる (発熱と切羽圧を払う既存の課税構造に乗る)。

#      面: (taken, free)
TARGET = {
    1: (4, 0), 2: (4, 0), 3: (4, 0),
    4: (3, 0), 5: (3, 0), 6: (3, 0),
    7: (2, 0), 8: (2, 0), 9: (2, 0),
    10: (1, 0), 11: (1, 0), 12: (1, 0),
    13: (0, 0), 14: (0, 0), 15: (0, 0),
}
W_TAKEN, W_FREE, W_SIDE = 3.0, 2.0, 0.2   # side は最大化なので符号が逆

# **必ず満たさなければならない条件。** 満たさない種は候補から外す。
#
# 面1 は**倒れ込みを目の前で見せる教材**。衝撃で真横を砕いてそこへ倒れ込み、
# そのまま縦に落ちる、という一続きの因果が1回で見えることを条件にする。
REQUIRE = {
    1: lambda r: r['topple'] >= 1 and r['taken'] >= 4,
}

# 点数では選べないときに、実行して目で見て決めた種を固定する。
PINNED = {}

def sweep(stage):
    out = subprocess.run([SIM, '--stage', str(stage), '--sweep', str(SWEEP)],
                         capture_output=True, text=True)
    if out.returncode:
        sys.exit('sim が失敗した: ' + out.stderr[:200])
    return [json.loads(l) for l in out.stdout.splitlines() if l.startswith('{')]


def score(row, want):
    tt, tf = want
    return (W_TAKEN * abs(row['taken'] - tt)
            + W_FREE * abs(row['free'] - tf)
            - W_SIDE * row['side'])


def main():
    print('%3s %6s %6s %5s %5s %6s %6s  %s' %
          ('面', '種', '採れる', 'free', 'side', '倒れ込', '連鎖', '狙い (採/free)'))
    seeds = []
    for st in range(1, 16):
        rows = sweep(st)
        want = TARGET[st]
        need = REQUIRE.get(st)
        if need:
            keep = [r for r in rows if need(r)]
            if not keep:
                sys.exit('面%d: 必須条件を満たす種が %d 通りの中に無い' % (st, len(rows)))
            rows = keep
        best = min(rows, key=lambda r: (score(r, want), r['seed']))
        if st in PINNED:
            pin = [r for r in rows if r['seed'] == PINNED[st]]
            if not pin:
                sys.exit('面%d: 固定した種 %d が条件を満たさない' % (st, PINNED[st]))
            best = pin[0]
        seeds.append(best['seed'])
        print('%3d %6d %6d %5d %5d %6d %6d  %d/%d%s' %
              (st, best['seed'], best['taken'], best['free'], best['side'],
               best['topple'], best['combo'], want[0], want[1],
               ('  <- 目で見て固定' if st in PINNED else '')))
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
