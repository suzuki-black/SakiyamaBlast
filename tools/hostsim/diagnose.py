#!/usr/bin/env python3
"""
SakiyamaBlast - 面の診断 (どこを直せばいいかを名指しする)

`metrics.py` は**合否のゲート**で、「面7 は爽快率が低い」までしか言わない。
この道具は同じ JSONL から**升を名指しする**ところまで持っていく。
「面12 は 47回の死のうち 35回が (2,5)」のように出るので、振り直すのではなく
**どの配置をどちらへ動かすか**を決められる。

使い方:
    make hostsim
    tools/hostsim/diagnose.py --stage 12 --runs 48            # 1面を詳しく
    tools/hostsim/diagnose.py --all --runs 24 --bot smart     # 15面の一覧
    tools/hostsim/diagnose.py --load s1.json --runs 48        # 面データを診る
    tools/hostsim/diagnose.py --stage 12 --runs 48 --json

**`src/` は触っていない。** 診断はほとんど既存の JSONL
(`p` / `treach` / `esc` / `coal` / `temp` / `stock` / `e` / `r` と頭の `grid`)
から作っていて、`sim.c` に足したのは1つだけ ── 採られた黒ダイヤの升 (`ct`)。
理由は `observe()` のところにある (向きから当てると当たらない)。
足りないものは下の「測れないもの」に書いてある。

**腕を添えずに読んではいけない。** `metrics.py` と同じで、数字はボットの腕で
動く。既定は `smart` ── こちらは「盤面の形のせいで損をしている升」を探す道具で、
弱いボットだと序盤で死んで面の後半が測れない (docs/HOSTSIM.md §2b)。

## 測れないもの (JSONL に出ていない)

- **炭柱の機能率** は推定でしか出せない。岩が落ちた列で敵が消えたかを見ている
  ので、落石とは無関係に死んだ敵も混ざる。出力では「岩で消えた敵 (推定)」と
  書いてある
- **ボーナスの未接触率**・**梅干しの回収率** は出せない。どちらも毎フレームの
  出力に入っていないので、足すには `sim.c` を触ることになる
"""
import argparse, json, os, statistics, subprocess, sys
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
SIM  = os.path.join(HERE, "sim")
FPS  = 60.0
COLS, ROWS = 16, 11

SCARE_S = 1.0       # metrics.py と同じしきい値。ここを変えるなら両方変える
NEAR    = 2         # 「風呂/巣の近く」と見なす升の数 (マンハッタン距離)

BOTS = ("greedy", "smart", "idle")


def run_sim(stage, seed, frames, safe, load, bot):
    what = ["--load", load] if load else ["--stage", str(stage)]
    out = subprocess.run([SIM] + what + ["--seed", str(seed),
                          "--frames", str(frames), "--safe", str(safe),
                          "--bot", bot],
                         capture_output=True, text=True, check=True).stdout
    head, rows, tail = None, [], None
    for line in out.splitlines():
        d = json.loads(line)
        if d.get("type") == "stage":
            head = d
        elif "f" in d:
            rows.append(d)
        else:
            tail = d
    return head, rows, tail


# ---------------------------------------------------------------- 盤面の静的な形

def grid_cell(grid, c, r):
    if c < 0 or c >= COLS or r < 0 or r >= ROWS:
        return None
    return grid[r][c]


def dig_bfs(grid, sc, sr):
    """**掘れば通れる**とした距離。岩だけが通れない。
    「湧き出し口から黒ダイヤまで何升か」を測るのに使う (掘る手間は数えない)。"""
    d = [[-1] * COLS for _ in range(ROWS)]
    if grid_cell(grid, sc, sr) is None:
        return d
    d[sr][sc] = 0
    q = [(sc, sr)]
    while q:
        nq = []
        for c, r in q:
            for dc, dr in ((0, -1), (0, 1), (-1, 0), (1, 0)):
                nc, nr = c + dc, r + dr
                ch = grid_cell(grid, nc, nr)
                if ch is None or ch == "O" or d[nr][nc] >= 0:
                    continue
                d[nr][nc] = d[r][c] + 1
                nq.append((nc, nr))
        q = nq
    return d


def open_cells(grid):
    """はじめから開いている升 (坑道・風呂・巣・火薬)。歩かれる前提の網。
    未到達率の分母はここ ── 土を分母に入れると、掘られなかった土まで
    「死んだ升」に数えてしまう。"""
    return {(c, r) for r in range(ROWS) for c in range(COLS)
            if grid[r][c] in " BNpd"}


def coal_cells(grid):
    """黒ダイヤの升と耐久。`x` (割れた) は耐久0として数える。"""
    out = {}
    for r in range(ROWS):
        for c in range(COLS):
            ch = grid[r][c]
            if ch.isdigit():
                out[(c, r)] = int(ch)
            elif ch == "x":
                out[(c, r)] = 0
    return out


# ---------------------------------------------------------------- 1回ぶんの観測

def observe(head, rows, tail, grid, coals):
    """1回の走行から、升に紐づく出来事だけを拾う。"""
    o = {
        "result": tail.get("type") if tail else "?",
        "why": tail.get("why") if tail else None,
        "death_cell": None,
        "death_esc": None,           # 死んだ升の退路の数
        "dwell": Counter(),          # 升ごとの滞在フレーム
        "scare_cell": Counter(),     # ヒヤリに入った升
        "coal_taken": [],            # (升, 秒, 直前60フレームの最小 treach, 岩が採ったか)
        "coal_by_rock": 0,           # 岩が突き抜けて採ったぶん
        "bath": [],                  # (秒, 入る直前の体温, 前回出てからのフレーム)
        "temp_max": 0,
        "rock_moved": set(),         # 動いた岩の番号
        "rock_total": 0,
        "crush": 0,                  # 岩が落ちた列で消えた敵 (推定)
        "powder_got": 0,
        "esc0": 0,                   # 退路0 で過ごしたフレーム
    }
    if not rows:
        return o
    bath = tuple(head["bath"])
    o["rock_total"] = sum(1 for s, _, _ in rows[0]["r"] if s != 0)
    rock0 = [(c, r) for _, c, r in rows[0]["r"]]
    scare_f = int(SCARE_S * FPS)
    in_danger = False
    in_bath = False
    left_bath = 0
    prev_stock = rows[0]["stock"]
    prev_on = [e["on"] for e in rows[0]["e"]]
    treach_win = []

    for i, row in enumerate(rows):
        c, r = row["p"]
        o["dwell"][(c, r)] += 1
        o["temp_max"] = max(o["temp_max"], row["temp"])
        if row["esc"] == 0:
            o["esc0"] += 1
        treach_win.append(row["treach"])
        if len(treach_win) > 60:
            treach_win.pop(0)

        # ヒヤリの発生位置
        if row["treach"] < scare_f:
            if not in_danger:
                in_danger = True
                o["scare_cell"][(c, r)] += 1
        else:
            in_danger = False

        # 採られた黒ダイヤの升。**`sim` が升をそのまま出す** ("ct")。
        # 自機の向きから当てる形も書いたが、`coal_take` が繋がっている塊を
        # まとめて採り、落ちる岩も突き抜けて採るので当たらない (面7 で
        # 8枚のうち2枚を取り落とした)。
        for tc, tr in row.get("ct", ()):
            # 岩がその升に居るなら、突き抜けて採ったぶん (= 炭柱が効いた)。
            by_rock = any(st != 0 and (rc, rr) == (tc, tr)
                          for st, rc, rr in row["r"])
            o["coal_taken"].append(((tc, tr), round(i / FPS, 1),
                                    min(treach_win), int(by_rock)))
            o["coal_by_rock"] += int(by_rock)

        # 水風呂の往復
        if (c, r) == bath:
            if not in_bath:
                in_bath = True
                o["bath"].append((round(i / FPS, 1), row["temp"], i - left_bath))
        elif in_bath:
            in_bath = False
            left_bath = i

        # 火薬を拾った
        if row["stock"] > prev_stock:
            o["powder_got"] += row["stock"] - prev_stock
        prev_stock = row["stock"]

        # 岩が動いたか / 岩の列で敵が消えたか
        fell_cols = set()
        for k, (st, rc, rr) in enumerate(row["r"]):
            if st == 0:
                continue
            if (rc, rr) != rock0[k]:
                o["rock_moved"].add(k)
                fell_cols.add(rc)
        for k, e in enumerate(row["e"]):
            if prev_on[k] and not e["on"]:
                if e["c"][0] in fell_cols:
                    o["crush"] += 1
            prev_on[k] = e["on"]

        if row["dead"]:
            o["death_cell"] = (c, r)
            o["death_esc"] = row["esc"]
    if o["result"] == "dead" and o["death_cell"] is None:
        o["death_cell"] = tuple(rows[-1]["p"])
        o["death_esc"] = rows[-1]["esc"]
    return o


# ---------------------------------------------------------------- まとめ

def md(xs):
    return round(statistics.median(xs), 1) if xs else None


def summarise(stage, runs, frames, safe, load, bot):
    obs, head = [], None
    for seed in range(1, runs + 1):
        h, rows, tail = run_sim(stage, seed, frames, safe, load, bot)
        if not h:
            continue
        head = h
        stage = h["stage"]
        obs.append(observe(h, rows, tail, h["grid"], coal_cells(h["grid"])))
    if not obs:
        return None

    grid  = head["grid"]
    coals = coal_cells(grid)
    opens = open_cells(grid)
    bath  = tuple(head["bath"])
    nest  = tuple(head["nest"])
    start = tuple(head["start"])
    nd    = dig_bfs(grid, *nest)
    sd    = dig_bfs(grid, *start)
    bd    = dig_bfs(grid, *bath)

    n = len(obs)
    deaths = [o["death_cell"] for o in obs if o["death_cell"]]
    dcnt   = Counter(deaths)
    dwell  = Counter()
    scare  = Counter()
    for o in obs:
        dwell.update(o["dwell"])
        scare.update(o["scare_cell"])
    dwell_n = sum(dwell.values()) or 1

    # 黒ダイヤごとの採掘コスト: 採れた割合・採れた秒・掘るあいだの危なさ。
    # **危なさは秒で出す。** `treach` はフレームで、敵が誰も来られない升では
    # 99999 が入るので、そのまま median を取ると表が読めない。10秒で頭打ち。
    per_coal = {}
    for cell, hp in sorted(coals.items(), key=lambda kv: (kv[0][1], kv[0][0])):
        took = [(s, t, k) for o in obs for (p, s, t, k) in o["coal_taken"] if p == cell]
        per_coal[cell] = {
            "cell": list(cell),
            "hp": hp,
            "take_rate": round(len(took) / n, 2),
            "secs": md([s for s, _, _ in took]),
            "treach_s": md([min(t / FPS, 10.0) for _, t, _ in took]),
            "by_rock": round(sum(k for _, _, k in took) / len(took), 2) if took else None,
            "nest_d": nd[cell[1]][cell[0]],
        }

    baths = [b for o in obs for b in o["bath"]]
    rocks = [len(o["rock_moved"]) / o["rock_total"] for o in obs if o["rock_total"]]
    powder = sum(1 for r in range(ROWS) for c in range(COLS) if grid[r][c] == "p")

    def near(cell, of):
        return abs(cell[0] - of[0]) + abs(cell[1] - of[1]) <= NEAR

    return {
        "stage": stage,
        "runs": n,
        "bot": bot,
        "clear_rate": round(sum(1 for o in obs if o["result"] == "clear") / n, 2),
        "start": list(start), "bath": list(bath), "nest": list(nest),

        # --- 死の集中
        "deaths": len(deaths),
        "death_top": [[list(c), v, round(v / len(deaths), 2)]
                      for c, v in dcnt.most_common(4)] if deaths else [],
        "death_near_bath": round(sum(1 for d in deaths if near(d, bath)) / len(deaths), 2)
                           if deaths else None,
        "death_near_nest": round(sum(1 for d in deaths if near(d, nest)) / len(deaths), 2)
                           if deaths else None,
        # 死んだ升に退路が無かった割合。**掘っている最中はいつも退路0 なので、
        # 盤面の袋小路を測るには「死んだ瞬間」で見ないと意味が出ない**
        # (全フレームで見ると、いちばん易しい面1 でも 48% になる)。
        "death_esc0": round(sum(1 for o in obs if o["death_esc"] == 0) / len(deaths), 2)
                      if deaths else None,
        "why": dict(Counter(o["why"] for o in obs if o["why"]).most_common()),

        # --- 滞在の偏りと未到達。
        # **分母は「はじめから開いている升」。** 176升で割ると、掘られていない
        # 土まで未到達に数えてしまって、どの面も踏破率1割になる。
        "walked": len(dwell),
        "open": len(opens),
        "open_used": round(len(set(dwell) & opens) / len(opens), 2) if opens else None,
        "unwalked": [list(c) for c in sorted(opens - set(dwell), key=lambda p: (p[1], p[0]))],
        "dwell_top": [[list(c), round(v / dwell_n, 3)] for c, v in dwell.most_common(3)],
        "esc0_rate": round(sum(o["esc0"] for o in obs) / dwell_n, 2),

        # --- ヒヤリの発生位置
        "scares": sum(scare.values()),
        "scare_top": [[list(c), v] for c, v in scare.most_common(3)],

        # --- 黒ダイヤごとの採掘コスト
        "coal": list(per_coal.values()),
        "coal_worst": min(per_coal.values(), key=lambda v: v["take_rate"])
                      if per_coal else None,
        "coal_by_rock": round(statistics.mean([o["coal_by_rock"] for o in obs]), 2),

        # --- 水風呂の往復
        "temp_max": round(statistics.mean([o["temp_max"] for o in obs])),
        "bath_visits": round(len(baths) / n, 1),
        "bath_temp": md([t for _, t, _ in baths]),
        "bath_gap_s": round(md([g for _, _, g in baths]) / FPS, 1)
                      if baths else None,
        "bath_nest_d": nd[bath[1]][bath[0]],
        "bath_start_d": sd[bath[1]][bath[0]],
        # 風呂から鉱脈までの距離。**近いほうと遠いほう**の両方を出す
        # (「往復が割に合うか」は、いちばん遠い黒ダイヤで決まる)。
        "bath_coal_d": min([bd[c[1]][c[0]] for c in coals], default=None),
        "bath_coal_far": max([bd[c[1]][c[0]] for c in coals], default=None),

        # --- 湧き出し口からの距離
        "nest_start_d": nd[start[1]][start[0]],
        "nest_coal_d": min([v["nest_d"] for v in per_coal.values()], default=None),

        # --- 要素が死んでいないか
        "rock_dud": round(1 - statistics.mean(rocks), 2) if rocks else None,
        "crush": round(statistics.mean([o["crush"] for o in obs]), 2),
        "powder": powder,
        "powder_unused": round(1 - statistics.mean([min(1.0, o["powder_got"] / powder)
                                                    for o in obs]), 2)
                         if powder else None,
    }


# 処方。**ゲートではない。** 外れていたら「ここを動かすと効くはず」という当たりで、
# 当たっているかは測り直して確かめること (docs/HOSTSIM.md の作法)。
#
# **火薬の未使用率は処方にしていない。** 数字は出すが、15面すべてで8割を超える。
# 理由は盤面ではなくボットの方針で、「火薬を拾いに行かせるのは実測で損」
# (docs/HOSTSIM.md §2) なので、わざと拾いに行かない。盤面の話と切り分けられない
# 数字を処方にすると、15面ぜんぶに同じ指示が出る。
RX = [
    # **死の話は5回以上死んでから。** 1回しか死なない面でも「100% 集中」に
    # なってしまう (面3 と面5 が実際そうなった)。
    ("death_top", lambda s: s["deaths"] >= 5 and s["death_top"][0][2] >= 0.30,
     lambda s: f"死が {s['death_top'][0][0]} に {int(s['death_top'][0][2]*100)}% 集中。"
               f"その升の周りの退路を増やす"),
    ("death_near_nest", lambda s: s["deaths"] >= 5 and (s["death_near_nest"] or 0) >= 0.50,
     lambda s: f"死の {int(s['death_near_nest']*100)}% が湧き出し口の近く。"
               f"巣を黒ダイヤから離す (いま最寄りの黒ダイヤまで {s['nest_coal_d']} 升)"),
    ("death_near_bath", lambda s: s["deaths"] >= 5 and (s["death_near_bath"] or 0) >= 0.50,
     lambda s: f"死の {int(s['death_near_bath']*100)}% が水風呂の近く。"
               f"風呂の周りの退路を増やすか、風呂を動かす"),
    ("open_used", lambda s: (s["open_used"] or 1) < 0.50,
     lambda s: f"はじめから掘ってある坑道のうち {int(s['open_used']*100)}% しか"
               f"使われていない。使われない升 {s['unwalked'][:6]}"),
    # **「その升が悪い」とは読まないこと (2026-11-06)。** はじめは
    # 「耐久を下げるか、巣から離す」と書いていたが、採取率は**その升が
    # 回る順のどこに来るか**でほぼ決まる (面12・14・15 では、採取率が
    # 採られた秒の順にそのまま並ぶ)。いちばん採れない升は多くの場合
    # **ただの最後の1枚**で、そこへ着く前に死んでいるという意味しかない。
    # 全採掘がクリアの条件なので、この数字は「この面は採り切れるか」を
    # 言っているのであって、升の置き場を言ってはいない。
    ("coal_worst", lambda s: s["coal_worst"] and s["coal_worst"]["take_rate"] <= 0.20,
     lambda s: f"黒ダイヤ {s['coal_worst']['cell']} (耐久 {s['coal_worst']['hp']}, "
               f"巣から {s['coal_worst']['nest_d']} 升, "
               f"採られるのは中央 {s['coal_worst']['secs']}秒) が "
               f"{int(s['coal_worst']['take_rate']*100)}% しか採れていない = "
               f"採り切れない面。まず盤面の引きを疑う "
               f"(tools/hostsim/pick_seeds.py の最低採取の床)"),
    # **しきい値は 0.95。** 0.80 だと15面のうち7面で鳴ったが、岩の多くは
    # 天井を支えているだけなので、落ちないのが普通 (実測の幅は 0.5〜0.97)。
    # 外れ値だけを拾う。
    ("rock_dud", lambda s: (s["rock_dud"] or 0) >= 0.95,
     lambda s: f"岩の {int(s['rock_dud']*100)}% が一度も落ちていない。飾りになっている"),
    # **水風呂は体温と組にして見る。** 入らない理由が「遠い」のか
    # 「熱くならない」のかで、動かす先が逆になる。
    # **「遠い」と「巣に近すぎる」は逆の処方になる。** 面5 は風呂が巣から2升で、
    # 距離ではなく危なさで寄れていない。巣からの距離で言い分けること。
    ("bath_far", lambda s: s["bath_visits"] < 0.5 and s["temp_max"] >= 150,
     lambda s: f"体温が {s['temp_max']} まで上がるのに、水風呂に入るのが "
               f"1回あたり {s['bath_visits']} 回。"
               + (f"巣から {s['bath_nest_d']} 升しかないので、"
                  f"危なくて寄れない。風呂を巣から離す"
                  if s["bath_nest_d"] <= 3 else
                  f"鉱脈から遠い (風呂からいちばん遠い黒ダイヤまで "
                  f"{s['bath_coal_far']} 升、近いものでも {s['bath_coal_d']} 升)。"
                  f"風呂を鉱脈へ寄せる")),
    ("bath_idle", lambda s: s["bath_visits"] < 0.5 and s["temp_max"] < 150,
     lambda s: f"体温が {s['temp_max']} までしか上がらないので、水風呂が"
               f"飾りになっている。面が短いか、掘る量が少ない"),
    ("death_esc0", lambda s: s["deaths"] >= 5 and (s["death_esc0"] or 0) >= 0.60,
     lambda s: f"死の {int(s['death_esc0']*100)}% が退路0 の升。"
               f"袋小路の奥に用事がある盤面"),
]


def prescribe(s):
    return [f(s) for _, pred, f in RX if pred(s)]


def show_one(s):
    print(f"\n=== 面{s['stage']} ({s['runs']}回, クリア率 {s['clear_rate']}, "
          f"出発 {s['start']} / 風呂 {s['bath']} / 巣 {s['nest']})")
    print(f"  死 {s['deaths']}回" + (": " + ", ".join(
        f"{c} {v}回({int(p*100)}%)" for c, v, p in s["death_top"])
        if s["death_top"] else ""))
    if s["deaths"]:
        print(f"      内訳 {s['why']}")
        print(f"      風呂の近く {s['death_near_bath']} / 巣の近く {s['death_near_nest']}"
              f" / 退路0 で死んだ割合 {s['death_esc0']}")
    print(f"  滞在: 踏んだ升 {s['walked']}, はじめから掘ってある坑道 {s['open']}升 の"
          f"使用率 {s['open_used']}, 退路0 の時間 {s['esc0_rate']}")
    print(f"      長居 " + ", ".join(f"{c} {p}" for c, p in s["dwell_top"]))
    print(f"      使われなかった升 {s['unwalked']}")
    print(f"  ヒヤリ {s['scares']}回: " + ", ".join(f"{c} {v}回" for c, v in s["scare_top"]))
    print(f"  水風呂: 1回あたり {s['bath_visits']}回, 入る体温 {s['bath_temp']}, "
          f"間隔 {s['bath_gap_s']}秒 (最高体温 {s['temp_max']})")
    print(f"      風呂から: 巣まで {s['bath_nest_d']}升, 出発点まで {s['bath_start_d']}升, "
          f"黒ダイヤまで {s['bath_coal_d']}〜{s['bath_coal_far']}升")
    print(f"  岩の不発率 {s['rock_dud']}, 岩で消えた敵 (推定) {s['crush']}, "
          f"岩が採った黒ダイヤ {s['coal_by_rock']}枚, "
          f"火薬 {s['powder']}個 未使用率 {s['powder_unused']}")
    print(f"  黒ダイヤ ({len(s['coal'])}枚, 巣から最寄り {s['nest_coal_d']}升):")
    print(f"    {'升':>8} {'耐久':>4} {'採れた':>6} {'秒':>5} {'危なさ秒':>8} "
          f"{'岩が':>5} {'巣から':>6}")
    for v in sorted(s["coal"], key=lambda v: v["take_rate"]):
        print(f"    {str(v['cell']):>8} {v['hp']:>4} {v['take_rate']:>6} "
              f"{str(v['secs']):>5} {str(v['treach_s']):>8} "
              f"{str(v['by_rock']):>5} {v['nest_d']:>6}")
    rx = prescribe(s)
    print("  処方: " + ("なし" if not rx else ""))
    for line in rx:
        print(f"    - {line}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--stage", type=int, default=1)
    ap.add_argument("--runs", type=int, default=24)
    ap.add_argument("--frames", type=int, default=5400)
    ap.add_argument("--safe", type=int, default=2)
    ap.add_argument("--all", action="store_true", help="面1〜15をまとめて診る")
    ap.add_argument("--bot", choices=BOTS, default="smart",
                    help="ボットの腕 (既定 smart = 面の後半まで到達する)")
    ap.add_argument("--load", metavar="FILE", help="面データを診る")
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args()

    if not os.path.exists(SIM):
        sys.exit(f"{SIM} がありません。make hostsim を先に実行してください。")
    if a.load and a.all:
        sys.exit("--load と --all は併せられません。")

    out = []
    if a.load:
        s = summarise(a.stage, a.runs, a.frames, a.safe, a.load, a.bot)
        if s:
            s["source"] = a.load
            out.append(s)
    else:
        for st in (range(1, 16) if a.all else [a.stage]):
            s = summarise(st, a.runs, a.frames, a.safe, None, a.bot)
            if s:
                out.append(s)
    for s in out:
        s["rx"] = prescribe(s)

    if a.json:
        print(json.dumps(out, ensure_ascii=False, indent=2))
        return

    bots = {"greedy": "貪欲", "smart": "先読み", "idle": "動かない"}
    print(f"面の診断 ({a.runs}回/面, ボット={bots[a.bot]})")
    if a.all:
        print(f"{'面':>3} {'クリア':>6} {'死':>4} {'死の一点':>12} {'退路0死':>7} "
              f"{'坑道':>5} {'岩不発':>6} {'風呂':>5} {'黒ダイヤ最低':>13}  処方")
        for s in out:
            top = (f"{s['death_top'][0][0]} {int(s['death_top'][0][2]*100)}%"
                   if s["death_top"] else "-")
            cw = s["coal_worst"]
            cwn = f"{cw['cell']} {int(cw['take_rate']*100)}%" if cw else "-"
            print(f"{s['stage']:>3} {s['clear_rate']:>6} {s['deaths']:>4} {top:>12} "
                  f"{str(s['death_esc0']):>7} {str(s['open_used']):>5} "
                  f"{str(s['rock_dud']):>6} {s['bath_visits']:>5} "
                  f"{cwn:>13}  {len(s['rx'])}件")
        print()
        for s in out:
            if s["rx"]:
                print(f"面{s['stage']}:")
                for line in s["rx"]:
                    print(f"  - {line}")
    else:
        for s in out:
            show_one(s)


if __name__ == "__main__":
    main()
