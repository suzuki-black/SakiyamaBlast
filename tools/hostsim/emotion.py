#!/usr/bin/env python3
"""
SakiyamaBlast - 面の「感情」の数値化

`tools/hostsim/sim --bot smart` に面を通させ、毎フレームの記録から
**遊んでいる間に何が起きていたか**を数に落とす。狙いは metrics.py と同じで、
「面白さ」を測るのではなく **面白さの必要条件が欠けている面を見つける** こと。

数えているもの (すべて実測):

    敵圧    敵が2升以内に居た時間の割合
    熱圧    体温が上限の75%を超えていた時間の割合
    袋小路  **敵が近く、かつ逃げ道が1本も無かった**時間の割合
    連鎖    1回の連鎖で採れた最大個数 (`combo`。窓は CHAIN_WINDOW = 3秒)
    放出    0.5秒の窓で入った得点の最大 (連鎖の倍率がそのまま出る)
    掘り    1秒あたりに掘った升
    初熱    体温が半分に達するまでの秒数

そこから2つの軸を作る:

    ストレス = 敵圧・熱圧・袋小路 の平均 (0〜100)
    爽快感   = 連鎖・放出 の平均 (0〜100)

**軸の基準値はまだ較正していない。** 作者に遊んでもらって申告
(「ここがしんどい」「ここが気持ちいい」) と突き合わせるまで、下の REF_* は
「この辺が満点だろう」という当て推量である。したがって**面どうしの比較には
使えるが、絶対値を信用してはいけない。**

判定の読み方:

    ストレス高 + 爽快感高 -> カタルシス  (しんどいが、連鎖で発散できている)
    ストレス高 + 爽快感低 -> 理不尽      (緊張だけあって見返りが無い)
    ストレス低 + 爽快感高 -> ごちそう    (気持ちよいが張りが足りない)
    ストレス低 + 爽快感低 -> 退屈

**クリア率は判定に使わない。** ボットの成績は重みの付け方に過敏で、罰則を1つ
足しただけで面ごとの成績が入れ替わる (docs/HOSTSIM.md「重みへの過敏さに注意」)。
抜けられなかった面には「(ボット未踏破)」と添えるだけにしてある。

使い方:
    tools/hostsim/emotion.py --stage 5 --runs 16
    tools/hostsim/emotion.py --all
    tools/hostsim/emotion.py --all --ura
    tools/hostsim/emotion.py --all --json
    tools/hostsim/emotion.py --load foo.json     # 面データを測る

**--load は面エディタとの接続点。** 形式は docs/STAGE_FORMAT.md。
"""
import argparse
import json
import os
import statistics
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SIM = os.path.join(HERE, "sim")
FPS = 60.0

TEMP_MAX = 240          # src/game.h と一致させる
NEAR_STEPS = 2          # 「敵が近い」と見なす升数
# **敵の近さは升で測る。** 坑道上の歩数 (`e[].d`) は、敵が土の中に居ると −1 になる。
# 敵は土を掘って進むので土の中に居る時間のほうが長く、歩数だけで数えたら
# 敵圧がほとんどの面で 0% になった (それでいて死因の最多は敵だった)。
# 歩数が取れるときはそちらを使い、取れないときは升の差 (チェビシェフ) で見る。
HOT_TEMP = TEMP_MAX * 3 // 4
HALF_TEMP = TEMP_MAX // 2
BURST_F = 30            # 放出量を測る窓 (0.5秒)
# **袋小路は「敵が近いときに逃げ道が無い」ことで数える。** 逃げ道の本数だけで
# 数えると、坑道は元々一本道なので全面が 40〜85% になって面の差が出なかった。

# 軸の基準値 (これで満点100)。較正前の当て推量。
REF_NEAR = 0.30         # 3割の時間を敵のそばで過ごせば敵圧は満点
REF_HOT = 0.25
REF_PINNED = 0.05       # 本当に追い詰められている時間が5%あれば満点
REF_COMBO = 8           # 1回の連鎖で8個採れれば満点
REF_BURST = 4000        # 0.5秒で4000点入れば満点


def run(stage, seed, frames, ura, load=None):
    """面番号から組み立てて走らせる。load を渡すとその面データを読む。"""
    what = ["--load", load] if load else ["--stage", str(stage)]
    cmd = [SIM] + what + ["--seed", str(seed),
           "--frames", str(frames), "--bot", "smart"]
    if ura:
        cmd.append("--ura")
    out = subprocess.run(cmd, capture_output=True, text=True, check=True).stdout
    rows, tail, head = [], None, None
    for line in out.splitlines():
        d = json.loads(line)
        if "f" in d:
            rows.append(d)
        elif d.get("type") == "stage":
            head = d
        else:
            tail = d
    return rows, tail, head


def tally(rows, tail):
    """1回の走行を数に落とす。"""
    n = len(rows)
    if n == 0:
        return None
    secs = n / FPS

    near = hot = pinned = 0
    combo_max = 0
    chains = []            # 連鎖が途切れるたびに、その山の高さを積む
    peak = 0
    t_half = None
    scores = [r["score"] for r in rows]

    for i, r in enumerate(rows):
        pc, pr = r["p"]
        ds = []
        for e in r["e"]:
            if e["on"] != 1:
                continue
            if e["d"] >= 0:
                ds.append(e["d"])
            else:
                ec, er = e["c"]
                ds.append(max(abs(ec - pc), abs(er - pr)))
        close = bool(ds) and min(ds) <= NEAR_STEPS
        if close:
            near += 1
            if r["esc"] == 0:
                pinned += 1
        if r["temp"] >= HOT_TEMP:
            hot += 1
        if t_half is None and r["temp"] >= HALF_TEMP:
            t_half = r["f"] / FPS
        c = r.get("combo", 0)
        if c > peak:
            peak = c
        elif c == 0 and peak:
            chains.append(peak)      # 窓が切れた = 1回の連鎖が終わった
            peak = 0
        combo_max = max(combo_max, c)
    if peak:
        chains.append(peak)

    burst = 0
    for i in range(n):
        j = min(n - 1, i + BURST_F)
        burst = max(burst, scores[j] - scores[i])

    return {
        "secs": secs,
        "near": near / n,
        "hot": hot / n,
        "pinned": pinned / n,
        "combo_max": combo_max,
        "chain_n": len([c for c in chains if c >= 2]),
        "burst": burst,
        "dug_rate": rows[-1]["dug"] / secs,
        "t_half": t_half if t_half is not None else secs,
        "clear": 1 if (tail or {}).get("type") == "clear" else 0,
        "why": (tail or {}).get("why", (tail or {}).get("type", "?")),
    }


def pct(v, ref):
    return max(0.0, min(100.0, 100.0 * v / ref))


def axes(t):
    stress = statistics.mean([pct(t["near"], REF_NEAR),
                              pct(t["hot"], REF_HOT),
                              pct(t["pinned"], REF_PINNED)])
    exhil = statistics.mean([pct(t["combo_max"], REF_COMBO),
                             pct(t["burst"], REF_BURST)])
    return stress, exhil


def verdict(stress, exhil, clear_rate):
    """2つの軸から判定する。

    **クリア率で判定を上書きしない。** ボットの成績は重みの付け方に過敏で、
    罰則を1つ足しただけで面ごとの成績が入れ替わる (docs/HOSTSIM.md)。
    クリア率をそのまま面の難しさとして読むと、**ボットの弱さを面のせいにする。**
    抜けられなかったことは注記として添えるだけにする。
    """
    if stress >= 60 and exhil >= 60:
        head = "カタルシス"
    elif stress >= 60 and exhil < 40:
        head = "理不尽"
    elif stress < 40 and exhil >= 60:
        head = "ごちそう"
    elif stress < 40 and exhil < 40:
        head = "退屈"
    else:
        head = "ふつう"
    if clear_rate < 0.2:
        head += " (ボット未踏破)"
    return head


def why_text(whys):
    """死因の内訳を短く。"""
    order = ["clear", "enemy", "heat", "rock", "cart", "gas", "time", "?"]
    name = {"clear": "クリア", "enemy": "敵", "heat": "過熱", "rock": "岩",
            "cart": "鉱車", "gas": "ガス", "time": "時間切れ", "?": "不明"}
    seen = {}
    for w in whys:
        seen[w] = seen.get(w, 0) + 1
    return " ".join("%s%d" % (name.get(k, k), seen[k])
                    for k in order if k in seen)


def grade(stage, runs, frames, ura, load=None):
    ts, whys = [], []
    for seed in range(runs):
        rows, tail, head = run(stage, seed, frames, ura, load)
        if head:
            stage = head["stage"]      # --load では面番号は面データが持っている
        t = tally(rows, tail)
        if t:
            ts.append(t)
            whys.append(t["why"])
    if not ts:
        return None
    med = {k: statistics.median([t[k] for t in ts])
           for k in ("secs", "near", "hot", "pinned", "combo_max",
                     "chain_n", "burst", "dug_rate", "t_half")}
    clear_rate = statistics.mean([t["clear"] for t in ts])
    stress, exhil = axes(med)
    return {
        "stage": stage,
        "runs": len(ts),
        "clear_rate": clear_rate,
        "stress": stress,
        "exhil": exhil,
        "verdict": verdict(stress, exhil, clear_rate),
        "why": why_text(whys),
        **med,
    }


def bar(v):
    """0〜100 を10段の棒にする。**色は使わない** (作者が色弱なので濃淡で読ませる)。"""
    n = int(round(v / 10.0))
    return "#" * n + "." * (10 - n)


def show(rs):
    print("面  ストレス     爽快感       クリア 連鎖 放出   敵圧 熱圧 袋小 掘/秒 初熱 測定  判定")
    for r in rs:
        print("%2d  %s %3.0f  %s %3.0f  %4.0f%%  %2d  %5.0f  %3.0f%% %3.0f%% %3.0f%%  %4.1f %4.0fs %3.0fs  %s"
              % (r["stage"], bar(r["stress"]), r["stress"],
                 bar(r["exhil"]), r["exhil"], r["clear_rate"] * 100,
                 r["combo_max"], r["burst"],
                 r["near"] * 100, r["hot"] * 100, r["pinned"] * 100,
                 r["dug_rate"], r["t_half"], r["secs"], r["verdict"]))
    print()
    print("※ 割合は**走り切った時間に対する割合**。途中で死ぬ面は測定秒が短く、")
    print("  その面の後半は測れていない (連鎖が 1 で止まるのは多くがこれ)。")
    print("※ 数字はボットの腕に左右される。敵をよく避けるボットほど敵圧が低く出る。")
    print()
    for r in rs:
        print("面%-2d %s / %s" % (r["stage"], r["verdict"], r["why"]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--stage", type=int)
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--runs", type=int, default=16)
    ap.add_argument("--frames", type=int, default=12000)
    ap.add_argument("--ura", action="store_true", help="裏面の規則で測る")
    ap.add_argument("--load", metavar="FILE",
                    help="面データを測る (面エディタとの接続点)")
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args()
    if a.load:
        if a.all:
            sys.exit("--load と --all は併せられません。")
        rs = [r for r in [grade(a.stage or 1, a.runs, a.frames, a.ura, a.load)] if r]
    else:
        stages = range(1, 16) if a.all or not a.stage else [a.stage]
        rs = [r for r in (grade(s, a.runs, a.frames, a.ura) for s in stages) if r]
    if a.json:
        json.dump(rs, sys.stdout, ensure_ascii=False, indent=1)
        print()
    else:
        show(rs)


if __name__ == "__main__":
    main()
