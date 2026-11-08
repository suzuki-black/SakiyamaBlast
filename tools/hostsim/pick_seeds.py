#!/usr/bin/env python3
"""面ごとの盤面の種を総当たりして選ぶ。

盤面は面番号だけで決まる（`src/stagegen.c` の `stage_seed[]`）。**その1つが悪ければ
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
    python3 tools/hostsim/pick_seeds.py --write    # src/stagegen.c に書き込む
    python3 tools/hostsim/pick_seeds.py --no-play  # 遊ばせる検査を省く (速いが書けない)

**静的な指標のあとに、実際に遊ばせる検査を1つ掛けてある** (`PLAY_FLOOR`、
2026-11-06)。上の指標は「置いた物の形」までしか見ないので、**最後の数枚へ
行けない盤面**が通ってしまう。全採掘がクリアの条件なので、それはそのまま
抜けられない面になる。詳しくは `PLAY_FLOOR` のところ。

**ただし床は面10から先では盤面の検査ではない** (2026-11-08)。あのあたりの面は
ボットが順路を歩き切る前に死ぬので、「いちばん採れない1枚」が 0 になるのは
その升が届かないからではない。**床が落ちた面は盤面そのものを別に検査して**
(`_board_ok`)、盤面の話なのか難度の話なのかを警告に書き分けてある。
"""
import json, os, re, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

SIM = 'tools/hostsim/sim'
# stage_seed[] の置き場。**field.c から stagegen.c へ分けた (2026-10-30)。**
# ここが古いと --write が黙って何もしないので、下で置換できたか確かめている。
SEEDS_SRC = 'src/stagegen.c'
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
# 面の黒ダイヤ総数に対する割合で見ると 面1 で 5/6 = 83%、面15 で 1/12 = 8%。
# 低次は一手でほぼ片付き、高次は掘って仕込まないと進まない。
# **幕の頭 (面4・7・10・13) だけ1つ上げる** (2026-10-12)。新しい仕掛けを覚える面で、
# 盤面まで一番渋くすると「覚える面が一番難しい面」になってしまう。
TARGET = {
    1: 5,                              # 第一坑
    2: 3, 3: 3,
    4: 4, 5: 3, 6: 2,                  # 第二坑 (頭で上げ、峰で下げる)
    7: 3, 8: 3, 9: 2,                  # 第三坑
    10: 3, 11: 2, 12: 2,               # 第四坑
    13: 2, 14: 2, 15: 1,               # 第五坑
}
# **狙いを下げた (2026-09-19)。** 面2から岩をずらして置き、「何個か動かして準備してから
# 一気に採る」を主軸にしたので、押さない一手 (hand) は小さめの目安にとどめ、
# 揃えたあとの取れ高 (hprep) が大きい盤面を選ぶ。
W_HAND, W_HBEND = 3.0, 0.5   # hbend は最大化なので符号が逆
W_HPUSH = 0.3                # 揃えたときの伸び (hpush - hand) も少しだけ大きいほうを採る
W_HPREP = 1.0                # **全部揃えてからの取れ高 (hprep) を大きく。準備の見返り**
BAL_MIN = 30                 # 左右の散らばり: 少ない側が黒ダイヤ・岩とも3割以上

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

# **樋は第二坑 (面4) から (2026-09-21)。** 第一坑 (面1〜3) は「岩は真下にしか落ちない」で
# 完結させ、面4で初めて「横に曲がる」が入る。
# 樋が無い面では hbend も hpick も 0 になるので、線の保証は **hpick0**
# (炭柱以外を1枚割って採れた数。樋は問わない) で見る。
#
# **枚数は src/levels.c の表から読む (2026-11-07)。** 定数2段 (低次2枚 / 面8以降4枚) を
# 面ごとの表へ替えたので、ここで数字を持たない (持つと生成側とずれる)。
# 表を読むのは `_tbl()` の定義より後 (下の CHUTE_N / CHUTE_START_ST)。
# **出水 (第三坑の試験面)。** 落盤で湧いた熱水が遊びに関与する盤面だけを選ぶ。
# wet が小さい種は、敵の巣や岩で水路が塞がって数升しか広がらない (実測で3升の種があった)。
WATER_ST = 7
WATER_ST_END = 9
CART_ST = 10          # 鉱車 (第四坑から)
WATER_WET_MIN = 10
# **面2・3 を 3 から 2 へ下げた (2026-10-20)。**
# 「指でなぞれる一本の線」は、仕込んだ鉱脈 (支柱を折ると岩が隙間を抜けて全段を貫く)
# のほうが担うようになった。その保証は digpay で別に掛けてある。
# 第一坑は黒ダイヤが6個しかなく、うち2個を仕込みが使うので、**一手で3個**は
# 6万通りに1つも無い (実測: hpick0>=3 は41通りしか無く、他の条件と両立しない)。
# 面1 は仕込みを置かない教材なので 4 のまま。
_HPICK0_MIN = {1: 4, 2: 2, 3: 2}
# **ずらして置いた岩 (2026-09-16)。** 面が進むと岩の一部を炭層の真上からずらして置く
# (`src/field.c` の `rock_off_tbl`)。**押して揃える手間を払ったぶんだけ伸びる**ことを保証する。
#
#   npush  >= その面のずらす数   … 置いたつもりの岩が、実際に押して揃えられる
#   hpush  >= hand + PUSH_GAIN   … 揃えてから一手打つと、押さない最善手より明らかに多く採れる
#                                  (一手 = 真下を割る / 炭柱を割って揃えた岩も落盤に加える)
#   rowdup == 0                  … 1行に岩1個 (横一列8枚のスプライト制限)
#
# rowdup はずらす岩の無い面にも課す。**以前の面3が1行に岩2個で出荷されていた**
# (真上の候補が尽きて「どこでも」に回ったとき、行を落とさずに候補を作り直していた)。
#
# **表を探す先は複数見る。** もとは `src/field.c` 決め打ちだったが、2026-10-30 に
# 組み立てを `src/stagegen.c` へ分けたときに表も一緒に動き、このツールは
# 起動しなくなっていた (`m` が None で AttributeError)。見つからなければ
# どこを探したかを言って止める ── 黙って既定値を使うと、**条件を課したつもりで
# 課していない種**が選ばれる。
_TBL_SRC = ('src/stagegen.c', 'src/field.c', 'src/levels.c', 'src/tlimit.c')


def _tbl(name):
    for path in _TBL_SRC:
        m = re.search(name + r'\[MAX_STAGE\] = \{(.*?)\}', open(path).read(), re.S)
        if m:
            body = re.sub(r'/\*.*?\*/', ' ', m.group(1), flags=re.S)   # 注釈は落とす
            return [int(x) for x in body.replace('\n', ' ').split(',') if x.strip()]
    sys.exit('%s[MAX_STAGE] が %s に見つからない (置き場が動いた?)'
             % (name, ' / '.join(_TBL_SRC)))


ROCK_OFF = dict(zip(range(1, 16), _tbl('rock_off_tbl')))
# 樋の枚数と、樋が入る最初の面 (上の説明を参照)。
CHUTE_N = dict(zip(range(1, 16), _tbl('chute_tbl')))
CHUTE_START_ST = min(st for st, n in CHUTE_N.items() if n)
# **押して揃えたときの伸び。** 下ごしらえ (隙間を掘る) を仕込んだ面では 1 に緩める。
# 準備の軸が「押して揃える」と「掘って繋げる」の2本になったので、両方に満額を
# 課すと盤面が作れない (面11 で 6万通り中 0 になった。2026-10-18 の実測)。
PUSH_GAIN = 2
PUSH_GAIN_GAP = 1

# **掘らないと繋がらない鉱脈 (2026-10-17)。**
# field.c の vgap_tbl / vgap2_tbl がそのまま狙い。隙間を掘ったぶんだけ取れ高が
# 伸びること (digpay) と、仕込んだ本数 (gapn) が揃っていることを必須にする。
# **本数が足りない種は落とす。** 掘り抜きが広くて1列に土が8行続かない面では
# 生成側が段数を落として置くので、狙いどおりの深さで入った種だけを選ぶ。
VGAP = dict(zip(range(1, 16), _tbl('vgap_tbl')))
VGAP2 = dict(zip(range(1, 16), _tbl('vgap2_tbl')))


def _require(r, st):
    if r['rowdup']:
        return False
    # **下ごしらえ (隙間を掘る) が狙いどおり効くこと。**
    want = VGAP[st] + VGAP2[st]
    veins = (1 if VGAP[st] else 0) + (1 if VGAP2[st] else 0)
    if r['gapn'] != veins or r['digpay'] < want:
        return False
    if veins == 2 and r['digmin'] < min(VGAP[st], VGAP2[st]):
        return False
    if WATER_ST <= st <= WATER_ST_END and (r['wet'] < WATER_WET_MIN or r['wseal']):
        return False
    # **鉱車。** レールが置けていること、そのレールの上の列に岩があること
    # (載せる物が無いと飾りになる)、そして**車止めの下に黒ダイヤがあること**
    # (運んだ先に見返りが無いと、運ぶ意味が無い)。
    if st >= CART_ST and (not r['rail'] or r['railrock'] < 1 or r['railpay'] < 1):
        return False
    if st < CHUTE_START_ST and r['hbend']:
        return False                      # 第一坑に樋は置かない (置いたら生成側の不具合)
    if st >= CHUTE_START_ST and r['hbend'] < 1:
        return False
    # **左右の散らばり (2026-09-19)。** 固まった盤面ほど少ない手で連鎖が伸びるので、
    # 条件を課さないと種選びが「右側だけ・左側だけ」の盤面を選んでいた (作者の指摘)。
    if r['cbal'] < BAL_MIN or r['rbal'] < BAL_MIN:
        return False
    if st < CHUTE_START_ST:
        # 第一坑 (教材の幕)。面1 は岩が真上にあって1手で大きく採れること、
        # 面2〜3 は1個押して揃えると明らかに伸びることを課す。
        if r['hpick0'] < _HPICK0_MIN[st]:
            return False
        if not ROCK_OFF[st]:
            return True
        return (r['npush'] >= ROCK_OFF[st]
                and r['hpush'] >= r['hand'] + (PUSH_GAIN_GAP if VGAP[st] else PUSH_GAIN))
    if not ROCK_OFF[st]:
        return r['hpick'] >= _HPICK_MIN[st]
    # **樋を通る一本の線は、押して揃えたあとに成立してもよい。**
    # 押さずに hpick を満たす盤面は押さなくても十分採れてしまい、
    # 「押すと伸びる」と両立しなかった (面4で 6万通り中 0)。高次面の線は揃えて作るもの。
    line = (r['hpick'] >= _HPICK_MIN[st] or (r['hpush'] >= _HPICK_MIN[st] and r['hpbend'] >= 1)
            or (r['hprep'] >= _HPICK_MIN[st] and r['hprepbend'] >= 1))
    gain = PUSH_GAIN_GAP if VGAP[st] else PUSH_GAIN
    if not (line and r['npush'] >= ROCK_OFF[st] and r['hpush'] >= r['hand'] + gain):
        return False
    # **2個以上ずらす面では、全部揃えると1個だけより明らかに伸びること。**
    # 1個押せば済む盤面だと「何個も動かして準備してから」にならない。
    if ROCK_OFF[st] >= 2 and r['hprep'] < r['hpush'] + gain:
        return False
    return True


REQUIRE = {st: (lambda r, st=st: _require(r, st)) for st in range(1, 16)}

# 点数では選べないときに、実行して目で見て決めた種を固定する。
PINNED = {}


def sweep(stage):
    out = subprocess.run([SIM, '--stage', str(stage), '--sweep', str(SWEEP)],
                         capture_output=True, text=True)
    if out.returncode:
        sys.exit('sim が失敗した: ' + out.stderr[:200])
    return [json.loads(l) for l in out.stdout.splitlines() if l.startswith('{')]


def score(row, want):
    return (W_HAND * abs(row['hand'] - want) - W_HBEND * row['hbend']
            - W_HPUSH * max(0, row['hpush'] - row['hand'])
            - W_HPREP * row['hprep'])


# ---------------------------------------------------------------- 実際に採れるか
#
# **静的な指標だけでは「最後の数枚へ行けない盤面」が通る (2026-11-06)。**
#
# 上の指標はどれも「置いた物の形」と「一手でどれだけ採れるか」までで、
# **採り切れるか**を見ていない。全採掘がクリアの条件なので、1枚でも事実上
# 採れない升があれば、その面は抜けられない面として出荷される。
# 面6 が実際にそれで、必須条件を通った242通りのうち **164通り (68%) は
# ボットが全採掘して抜ける**のに、引いていた種は最低採取 0.06 だった
# (docs/HOSTSIM.md「面6 は盤面の引きが悪かった」)。
#
# そこで **`smart` ボットに実際に遊ばせて、どの黒ダイヤも採れることを課す。**
#
# - **測るのは「いちばん採れない1枚の採取率」で、クリア率ではない。**
#   クリア率に床を張ると、ボットにとって易しい盤面を選ぶことになり
#   面ごとの難度の階段をボットの腕で踏み潰す (面11 と面13 は最低採取
#   0.50 / 0.56 で**採り切れる**面だが、クリア率は 0.46 / 0.58 しかない)。
#   床は**壊れた盤面を落とすため**のもので、難度を揃えるためのものではない。
#   同じ理由で、床を満たす種のうち**いちばんクリア率が高いもの**も採らない
#   (面6 なら最低採取 1.00 の種になる)。採るのは点のいちばん良いもの。
# - **床を満たす種が1つも無い面は、落とさずに警告して通す。** 原因が盤面では
#   ない面があるので (面12 は敵の表、面14 は掘る量。docs/HOSTSIM.md の
#   「抜けられない4面を1面ずつ切り分けた」)、ここで止めると種を選び直せなくなる。
#
# **床は面10から先では「盤面の検査」ではなく「生存の検査」になっている**
# (2026-11-08。docs/HOSTSIM.md「盤面は無罪だった」)。**床が落ちた面では
# 盤面そのものを別に検査して、どちらの話なのかを警告に書き分ける**
# (`_board_ok`)。検査に使うのは `par_route()` の2つで、どちらも敵を数えない。
#
#     parleft == 0          全部の黒ダイヤと風呂を回れる (回れない升が無い)
#     par < 持ち時間        その順路が時計に収まる (実測は 36〜46秒 / 160〜180秒)
#
# これが通っていれば**盤面は健全**で、床に届かないのはボットが順路を歩き切る前に
# 死ぬからである。実際、敵を外すと同じ盤面が面12 は34秒・面15 は56秒で毎回抜ける。
# 採れていない升は毎回**2本ある仕込み鉱脈のうち遠いほうが丸ごと**で、近いほうは
# 1.00 で採れている —— 片方を採り切ったところで力尽きる形。
# **この警告を見て盤面の種を選び直しても直らない。** 面12 では条件を満たす
# 10通りを全部遊ばせても最低採取は 0.00〜0.17 だった。
# - **持ち時間は模していない** (`src/tlimit.c` はホスト版に入っていない)。
#   いちばん長い面の 180秒ぶんのフレームを渡している。実測では4面とも
#   90秒でも同じ数字で、ボットは時間切れではなく死んで終わる。
PLAY_RUNS   = 16          # 1つの種あたりの試行数
PLAY_FRAMES = 10800       # 180秒 (いちばん長い面の持ち時間)
PLAY_FLOOR  = 0.3         # どの黒ダイヤも、この割合以上で採れること
PLAY_TRY    = 40          # 点の良い順にここまで試す (全部試すと面1つで数分)

_POOL = ThreadPoolExecutor(max_workers=os.cpu_count())

# 持ち時間 (秒)。`src/tlimit.c` の表から読む (10秒単位で持っている)。
_TLIMIT = [n * 10 for n in _tbl('tlimit_tbl')]


def _board_ok(st, row):
    """盤面そのものが健全か。敵を数えない2つの検査で見る (`PLAY_FLOOR` の説明)。

    返すのは (健全か, 理由の文)。
    """
    par = row['par'] / 60.0
    sec = _TLIMIT[st - 1]
    why = '理想の順路 %.0f秒 / 持ち時間 %d秒・回れない黒ダイヤ %d枚' \
          % (par, sec, row['parleft'])
    return (row['parleft'] == 0 and par < sec), why


def _play_one(args):
    stage, lseed, seed = args
    out = subprocess.run([SIM, '--stage', str(stage), '--layout-seed', str(lseed),
                          '--seed', str(seed), '--frames', str(PLAY_FRAMES),
                          '--safe', '2', '--bot', 'smart'],
                         capture_output=True, text=True, check=True).stdout
    grid, taken, tail = None, set(), None
    for line in out.splitlines():
        d = json.loads(line)
        if d.get('type') == 'stage':
            grid = d['grid']
        elif 'f' in d:
            taken |= {tuple(x) for x in d.get('ct', ())}
        else:
            tail = d
    coals = {(c, r) for r in range(11) for c in range(16) if grid[r][c].isdigit()}
    return (tail.get('type') if tail else '?'), coals, taken


def play_rate(stage, lseed):
    """その種を 16回遊ばせて (いちばん採れない1枚の採取率, クリア率) を返す。"""
    res = list(_POOL.map(_play_one, [(stage, lseed, s)
                                     for s in range(1, PLAY_RUNS + 1)]))
    coals = res[0][1]
    worst = min(sum(1 for _, _, t in res if c in t) for c in coals) / len(res)
    clear = sum(1 for r, _, _ in res if r == 'clear') / len(res)
    return worst, clear


def _sweep_all():
    """15面を並列に総当たりする (1面ずつだと十数分かかる)。"""
    procs = {st: subprocess.Popen([SIM, '--stage', str(st), '--sweep', str(SWEEP)],
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
             for st in range(1, 16)}
    out = {}
    for st, p in procs.items():
        o, e = p.communicate()
        if p.returncode:
            sys.exit('sim が失敗した: ' + e[:200])
        out[st] = [json.loads(l) for l in o.splitlines() if l.startswith('{')]
    return out


def main():
    play = '--no-play' not in sys.argv
    # **`--no-play --write` は断る。** 床を外して選ぶと面6 は床に落ちる種
    # (29123) へ戻るので、黙って書くと直したものが元へ戻る。
    if not play and '--write' in sys.argv:
        sys.exit('--no-play と --write は併せられない '
                 '(床を外して選んだ種を書くと、直した面が元へ戻る)')
    print('%3s %6s %6s %6s %6s %6s %6s %6s %6s %6s %6s %5s %5s %6s  %s' %
          ('面', '種', '一手', 'hpick', 'hbend', '落盤', '樋', '連鎖', 'ずらす', '1個', '全部',
           '炭左右', '岩左右', '最低採取', '狙い/最低'))
    seeds, warn = [], []
    allrows = _sweep_all()
    for st in range(1, 16):
        rows = allrows[st]
        want = TARGET[st]
        need = REQUIRE.get(st)
        if need:
            keep = [r for r in rows if need(r)]
            if not keep:
                sys.exit('面%d: 必須条件を満たす種が %d 通りの中に無い' % (st, len(rows)))
            rows = keep
        order = sorted(rows, key=lambda r: (score(r, want), r['seed']))
        best, got = order[0], None
        if play and st not in PINNED:
            # **点の良い順に、採り切れる種を探す。** 先頭が床を満たせばそれが答えで
            # (いまの種はそうやって選ばれている)、満たさなければ下へ降りる。
            for r in order[:PLAY_TRY]:
                worst, _ = play_rate(st, r['seed'])
                if worst >= PLAY_FLOOR:
                    best, got = r, worst
                    break
            if got is None:
                # **落とさずに通す。** 原因が盤面でない面がある (docs/HOSTSIM.md)。
                # **どちらの話なのかを書き分ける** (上の `PLAY_FLOOR` の説明)。
                got = play_rate(st, best['seed'])[0]
                head = ('面%d: 最低採取が %.2f しかない種しか無い '
                        '(%d通りのうち上位%d通りを試した)。'
                        % (st, got, len(rows), min(PLAY_TRY, len(rows))))
                ok, why = _board_ok(st, best)
                warn.append(head + (
                    '**盤面は健全** (%s)。床に届かないのはボットが順路を歩き切る前に'
                    '死ぬからで、種を選び直しても直らない。難度のほうの話として'
                    '扱うこと (docs/HOSTSIM.md「盤面は無罪だった」)' % why
                    if ok else
                    '**盤面を疑うこと** (%s)' % why))
        if st in PINNED:
            pin = [r for r in rows if r['seed'] == PINNED[st]]
            if not pin:
                sys.exit('面%d: 固定した種 %d が条件を満たさない' % (st, PINNED[st]))
            best = pin[0]
        seeds.append(best['seed'])
        print('%3d %6d %6d %6d %6d %6d %6d %6d %4d/%d %6d %6d %5d %5d %6s  %d/%d%s' %
              (st, best['seed'], best['hand'], best['hpick'], best['hbend'],
               best['taken'], best['chutes'], best['combo'], best['npush'], ROCK_OFF[st],
               best['hpush'], best['hprep'], best['cbal'], best['rbal'],
               ('%.2f' % got) if got is not None else '-', want,
               _HPICK0_MIN.get(st, _HPICK_MIN[st]),
               ('  <- 目で見て固定' if st in PINNED else '')), flush=True)
    for line in warn:
        print('警告: ' + line)
    if '--write' in sys.argv:
        src = open(SEEDS_SRC).read()
        body = ',\n    '.join(', '.join('%5d' % s for s in seeds[i:i + 5])
                              for i in range(0, 15, 5))
        src, n = re.subn(r'(const uint16_t stage_seed\[15\] = \{\n)(.*?)(\n\};)',
                         lambda m: m.group(1) + '    ' + body + ',' + m.group(3),
                         src, count=1, flags=re.S)
        if n != 1:
            sys.exit('%s に stage_seed[15] が見つからない (置き場が動いた?)' % SEEDS_SRC)
        open(SEEDS_SRC, 'w').write(src)
        print('\n-> %s の stage_seed[] を書き換えた' % SEEDS_SRC)


# **import できるようにしてある。** 必須条件 (`_require`) は種を選ぶ道具の本体なので、
# 別の道具から同じ条件で候補を絞れるようにしておく (条件を書き写すと必ずずれる)。
if __name__ == '__main__':
    main()
