#!/usr/bin/env python3
"""面ごとの盤面の種を総当たりして選ぶ。

盤面は面番号だけで決まる（`src/field.c` の `stage_seed[]`）。**その1つが悪ければ
そのまま出荷される**ので、狙った手応えになる種を探して固定する。

貪欲ボットは面1すらクリアできないので判定には使わない。代わりに
`tools/hostsim/sim.c` の analyse が、盤面を静的に測ったうえで**実際に岩を落として**
数える。

    free  : 岩の真下（同じ列）にある黒ダイヤの数。落ちる岩は列を貫いて全部採るので、
            **そのまま「何もしなくても手に入る連鎖の長さ」**になる
    hand  : **つるはしを1本入れるだけで採れる最大数（掘らない）。難易度の軸。**
            盤面をそのまま使い、黒ダイヤ1枚を耐久ぶん叩いて engine を回して数える。
            **遊んだときに実際に起きることを測っている唯一の指標**
    hbend : **その最善手の中で岩が樋を通った回数。1以上を必須にしてある**
    hpick : **炭柱以外を1枚割って、岩が樋を通って採れた数。**
            低次面で4以上、高次面で2以上を必須にしてある（指でなぞれる線の保証）
    reach : 炭以外の升を全部掘ったうえでの、1回の落下で採れる最大数（上限の参考値）
    bend  : 実際に樋で曲がった岩の数。0 なら板が飾りになっている
    taken : 落盤ひとつで採れる黒ダイヤの数。**難易度の軸**
    combo : そのとき届いた連鎖の長さ
    haul  : 一番長い列へ岩を寄せるのに要る横移動の升数＝**岩の移動の面倒さ**
    colmax: 一番長い縦の炭層＝一気に狙える連鎖の上限

    python3 tools/hostsim/pick_seeds.py            # 探して表を出すだけ
    python3 tools/hostsim/pick_seeds.py --write    # src/field.c に書き込む
"""
import json, re, subprocess, sys

SIM = 'tools/hostsim/sim'
FIELD = 'src/field.c'
SWEEP = 60000

# **指標を作り替えた (2026-09-12)。**
#
# 旧: reach (炭以外を全部掘ってから岩を1つ落として採れる数) を必須条件にしていた。
# 問題: **上限しか測っていない。** 盤面が「岩→炭層→樋→炭柱」と並んでいても、
#       途中が土で塞がっていれば遊んでいるときの岩は止まる。実際に面1がそれで、
#       作者に「ルールおかしくない？」と指摘されて発覚した。
#
# 新: **hand = つるはしを1本入れるだけで採れる最大数（掘らない）。**
#     盤面をそのまま使って engine を回すので、**遊んだときに起きることそのもの**。
#     炭柱を叩いた場合の落盤もこの中に含まれる。
#
#   低次 … 1手で大きく繋がる（何もしなくても連鎖してクリアできる）
#   高次 … 1手ではほとんど繋がらない（掘って仕込まないと連鎖しない）

#      面: つるはし1本で採れる数の狙い
# 面の黒ダイヤ総数に対する割合で見ると 面1 で 5/6 = 83%、面15 で 2/12 = 17%。
# 低次は一手でほぼ片付き、高次は掘って仕込まないと進まない。
TARGET = {
    1: 5, 2: 5, 3: 5,
    4: 4, 5: 4, 6: 4,
    7: 4, 8: 4, 9: 4,
    10: 3, 11: 3, 12: 3,
    13: 2, 14: 2, 15: 2,
}
W_HAND, W_HBEND = 3.0, 0.5   # hbend は最大化なので符号が逆

# **必ず満たさなければならない条件。** 満たさない種は候補から外す。
#
# `hbend` = **その面の最善手（つるはし1本）の中で、岩が樋を通った回数。**
# 取れ高 (hand) だけで選ぶと「炭柱を叩いて落盤」が最善手になり、**板が一度も
# 使われない面が出荷される**（実際に一度そうなった）。盤面が絵で約束している
# 連鎖が嘘にならないよう、最善手に板が入っていることを必須にする。
#
# `hpick` = **炭柱以外を1枚割って、岩が樋を通って採れた数。**
# 炭柱を叩くと盤面全体が動くので、樋を通っても**原因が目で追えない**。
# 普通の黒ダイヤ1枚なら「この岩が落ちて、この板で曲がって、ここを採る」という
# 一本の線になり、落とす前に指でなぞれる。**低次面ほど強く要求する。**
_HPICK_MIN = {**{st: 4 for st in (1, 2, 3)},
              **{st: 3 for st in (4, 5, 6, 7, 8, 9)},
              **{st: 2 for st in (10, 11, 12, 13, 14, 15)}}
REQUIRE = {st: (lambda r, m=_HPICK_MIN[st]: r['hbend'] >= 1 and r['hpick'] >= m)
           for st in range(1, 16)}

# 点数では選べないときに、実行して目で見て決めた種を固定する。
PINNED = {}


def sweep(stage):
    out = subprocess.run([SIM, '--stage', str(stage), '--sweep', str(SWEEP)],
                         capture_output=True, text=True)
    if out.returncode:
        sys.exit('sim が失敗した: ' + out.stderr[:200])
    return [json.loads(l) for l in out.stdout.splitlines() if l.startswith('{')]


def score(row, want):
    return W_HAND * abs(row['hand'] - want) - W_HBEND * row['hbend']


def main():
    print('%3s %6s %6s %6s %6s %6s %6s %6s  %s' %
          ('面', '種', '一手', 'hpick', 'hbend', '落盤', '樋', '連鎖', '狙い/最低'))
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
        print('%3d %6d %6d %6d %6d %6d %6d %6d  %d/%d%s' %
              (st, best['seed'], best['hand'], best['hpick'], best['hbend'],
               best['taken'], best['chutes'], best['combo'], want, _HPICK_MIN[st],
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
