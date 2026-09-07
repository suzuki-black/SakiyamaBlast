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

#      面: (free, haul, colmax)
TARGET = {
    1: (4, 0, 4), 2: (4, 0, 4), 3: (3, 0, 4),
    4: (3, 1, 3), 5: (3, 1, 3), 6: (2, 1, 3),
    7: (2, 2, 3), 8: (2, 2, 3), 9: (1, 3, 2),
    10: (1, 3, 2), 11: (1, 4, 2), 12: (0, 4, 2),
    13: (0, 5, 2), 14: (0, 5, 2), 15: (0, 6, 2),
}
W_FREE, W_HAUL, W_COLMAX = 5.0, 2.0, 1.0   # free を最優先する (狙いの中心)

# **必ず満たさなければならない条件。** 満たさない種は候補から外す。
#
# 面1 の `slide`: 落盤で岩が横滑りする盤面であること。横滑りは「下の岩の左右のうち
# 坑道がちょうど一方だけ」という条件が要るので、素のままだと 15面で1回しか起きず、
# **規則の存在にプレイヤーが気づけない**。面1で目の前に見せて教える。
# **`combo` も要る。** 滑っただけで止まる盤面だと、滑りが何の役にも立っておらず
# 「だから何なのか」が伝わらない。滑った岩がそのまま落ちて連鎖に加わる盤面を選ぶ。
# **`sgain` が要。** 「滑った」だけでは足りない。滑った先が土や坑道だと岩は何も
# 採らずに止まり、**プレイヤーからは「今の何だったのか」で終わる**。
# 滑った岩がそのまま炭の列へ入って採る盤面でなければ、教材にならない。
REQUIRE = {
    1: lambda r: r['sgain'] >= 2 and r['combo'] >= 4,
}

# **点数では選べないので、実行して目で見て決めた種。**
#
# 面1: 滑る条件を満たす種は 60000 通り中4つ (10051 / 22735 / 25989 / 48540) あり、
# `free` `haul` `colmax` `slide` `combo` がすべて同点で機械的には区別できない。
# 落盤を1フレームずつ追って比べたところ、差は**滑りが連鎖に繋がるかどうか**だった。
#
#   種25989 … 滑った先が自機の縦坑で**炭が1個も無い**。滑るには滑るが何も起きない
#             (作者の指摘「これのどこが横滑りして鉱脈取得するのです？」で発覚)
#   種10051 … **`sgain` が 3 で唯一。** 71f に岩が岩へ着地して下の岩が横滑り、
#             79f に**4升の炭層がある12列目へ着地**、85f に落下して 101f に連鎖3。
#             滑った岩が炭の列へ入って採る、が目で見て分かる
#
# 種を選び直すときは、ここも実行して見比べること。
PINNED = {
    1: 10051,
}


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
        print('%3d %6d %5d %5d %7d %6d %6d  %d/%d/%d%s' %
              (st, best['seed'], best['free'], best['haul'], best['colmax'],
               best['coal'], best['veins'], want[0], want[1], want[2],
               ('  <- 目で見て固定' if st in PINNED
               else ('' if score(best, want) == 0 else '  (完全一致なし)'))))
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
