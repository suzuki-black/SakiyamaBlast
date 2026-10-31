#!/usr/bin/env python3
"""
SakiyamaBlast - 面の採点

tools/hostsim/sim が吐く JSONL を読み、面の当たり外れを数字にする。

指標は「面白さ」ではなく **面白さの必要条件の欠落** を測る道具として使う。
ヒヤリ0回の面はつまらないと断言できるが、ヒヤリ5回の面が面白いとは断言できない。
目的関数にすると「敵を増やして通路を狭くする」のが最適解になり理不尽な面ができるので、
必ず合格/不合格のゲートとして使うこと。

使い方:
    tools/hostsim/metrics.py --stage 1 --runs 24
    tools/hostsim/metrics.py --stage 1 --runs 24 --json
    tools/hostsim/metrics.py --all                  # 面1〜15をまとめて検品
    tools/hostsim/metrics.py --load foo.json        # 面データを採点する

**--load は面エディタとの接続点。** 面番号ではなくファイルを採点できるので、
置いた面をそのままゲートに通せる。形式は docs/STAGE_FORMAT.md。
"""
import argparse, json, os, statistics, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SIM  = os.path.join(HERE, "sim")
FPS  = 60.0

# しきい値。人に遊んでもらって申告と突き合わせる較正がまだなので暫定値。
SCARE_S  = 1.0    # これを下回った瞬間が「ヒヤリ」
RELIEF_S = 0.5    # ヒヤリがこの時間内に解消し、かつ報酬があれば「爽快」
NUMB_S   = 5.0    # 危険が続くとそれは緊張ではなく「麻痺」
BORED_S  = 3.0    # 敵が遠く報酬も無い時間が続けば「退屈」
SAFE_S   = 3.0    # 「敵が遠い」と見なす到達余裕
BURST_S  = 0.3    # 「放出量」を測る窓


def run_sim(stage, seed, frames=5400, safe=2, load=None):
    """面番号から組み立てて走らせる。load を渡すとその面データを読む。"""
    what = ["--load", load] if load else ["--stage", str(stage)]
    out = subprocess.run([SIM] + what + ["--seed", str(seed),
                          "--frames", str(frames), "--safe", str(safe)],
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


def analyse(head, rows, tail):
    """1回の走行を指標に落とす。"""
    n = len(rows)
    if n == 0:
        return None
    secs = n / FPS
    scare_f  = int(SCARE_S * FPS)
    relief_f = int(RELIEF_S * FPS)
    numb_f   = int(NUMB_S * FPS)
    bored_f  = int(BORED_S * FPS)
    burst_f  = max(1, int(BURST_S * FPS))

    scares, reliefs, numbs, cheap_deaths, bored = 0, 0, 0, 0, 0
    in_danger = False
    danger_start = 0
    danger_had_reward = False
    danger_entry_esc = 0
    calm_run = 0

    for i, r in enumerate(rows):
        t = r["treach"]
        reward = r["dscore"] > 0
        if t < scare_f:
            if not in_danger:
                in_danger = True
                danger_start = i
                danger_had_reward = False
                danger_entry_esc = r["esc"]
                scares += 1
                if danger_entry_esc == 0:
                    cheap_deaths += 1        # 入った時点で退路が無い = 判断で避けられていない
            if reward:
                danger_had_reward = True
            if i - danger_start == numb_f:
                numbs += 1
        else:
            if in_danger:
                if (i - danger_start) <= relief_f and danger_had_reward:
                    reliefs += 1
                in_danger = False
        # 退屈: 敵が遠く、報酬も無い時間
        if t > SAFE_S * FPS and not reward:
            calm_run += 1
            if calm_run == bored_f:
                bored += 1
                calm_run = 0
        else:
            calm_run = 0

    # 放出量: 窓内の (得点 + 鳴った効果音) のピーク。派手さの振幅。
    burst = 0
    for i in range(n):
        w = rows[i:i + burst_f]
        v = sum(x["dscore"] for x in w) // 50 + sum(x["sfx"] for x in w)
        burst = max(burst, v)

    per_min = 60.0 / secs if secs > 0 else 0
    return {
        "result": tail.get("type") if tail else "?",
        "secs": round(secs, 1),
        "coal_left": rows[-1]["coal"],
        "temp_max": max(r["temp"] for r in rows),
        "dug": rows[-1]["dug"] - head["dug0"],
        "score": rows[-1]["score"],
        "scares": scares,
        "scares_per_min": round(scares * per_min, 1),
        "relief_rate": round(reliefs / scares, 2) if scares else None,
        "numb": numbs,
        "cheap_death": cheap_deaths,
        "bored": bored,
        "burst": burst,
    }


def summarise(stage, runs, frames, safe, load=None):
    rs = []
    for seed in range(1, runs + 1):
        head, rows, tail = run_sim(stage, seed, frames, safe, load)
        a = analyse(head, rows, tail)
        if a:
            a["seed"] = seed
            rs.append(a)
        if head:
            stage = head["stage"]      # --load では面番号は面データが持っている
    if not rs:
        return None
    clears = [r for r in rs if r["result"] == "clear"]
    def avg(key):
        vs = [r[key] for r in rs if r[key] is not None]
        return round(statistics.mean(vs), 2) if vs else None
    return {
        "stage": stage,
        "runs": len(rs),
        "clear_rate": round(len(clears) / len(rs), 2),
        "clear_secs": round(statistics.mean([r["secs"] for r in clears]), 1) if clears else None,
        "scares_per_min": avg("scares_per_min"),
        "relief_rate": avg("relief_rate"),
        "numb": avg("numb"),
        "cheap_death": avg("cheap_death"),
        "bored": avg("bored"),
        "burst": avg("burst"),
        "temp_max": avg("temp_max"),
        "coal_left": avg("coal_left"),
    }


# 合格条件。**目的関数ではなくゲート**。外れた面は配置を振り直す。
GATES = [
    ("scares_per_min", 3.0, 12.0, "緊張感 (ヒヤリ/分)"),
    ("relief_rate",    0.25, None, "爽快率 (自分の手で解消できた割合)"),
    ("numb",           None, 0.5,  "麻痺 (危険が常態化)"),
    ("cheap_death",    None, 0.5,  "凡死 (退路0で危険に入った)"),
    ("bored",          None, 4.0,  "退屈 (掘るだけの時間)"),
]


def verdict(s):
    bad = []
    for key, lo, hi, label in GATES:
        v = s.get(key)
        if v is None:
            continue
        if lo is not None and v < lo:
            bad.append(f"{label} {v} < {lo}")
        if hi is not None and v > hi:
            bad.append(f"{label} {v} > {hi}")
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--stage", type=int, default=1)
    ap.add_argument("--runs", type=int, default=24)
    ap.add_argument("--frames", type=int, default=5400)
    ap.add_argument("--safe", type=int, default=2)
    ap.add_argument("--all", action="store_true", help="面1〜15をまとめて検品")
    ap.add_argument("--load", metavar="FILE",
                    help="面データを採点する (面エディタとの接続点)")
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args()

    if not os.path.exists(SIM):
        sys.exit(f"{SIM} がありません。make hostsim を先に実行してください。")
    if a.load and a.all:
        sys.exit("--load と --all は併せられません。")

    out = []
    if a.load:
        s = summarise(a.stage, a.runs, a.frames, a.safe, a.load)
        if s:
            s["source"] = a.load
            s["fails"] = verdict(s)
            out.append(s)
    else:
        for st in (range(1, 16) if a.all else [a.stage]):
            s = summarise(st, a.runs, a.frames, a.safe)
            if s:
                s["fails"] = verdict(s)
                out.append(s)

    if a.json:
        print(json.dumps(out, ensure_ascii=False, indent=2))
        return

    src = f", {a.load}" if a.load else ""
    print(f"面の採点 ({a.runs}回/面, ボット=貪欲, safe={a.safe}{src})")
    print(f"{'面':>3} {'クリア率':>7} {'秒':>5} {'ヒヤリ/分':>8} {'爽快率':>6} "
          f"{'麻痺':>4} {'凡死':>4} {'退屈':>4} {'放出':>4} {'体温':>4}  判定")
    for s in out:
        cs = f"{s['clear_secs']}" if s["clear_secs"] else "-"
        rr = f"{s['relief_rate']}" if s["relief_rate"] is not None else "-"
        mark = "OK" if not s["fails"] else "NG: " + " / ".join(s["fails"])
        print(f"{s['stage']:>3} {s['clear_rate']:>7} {cs:>5} {s['scares_per_min']:>8} {rr:>6} "
              f"{s['numb']:>4} {s['cheap_death']:>4} {s['bored']:>4} {s['burst']:>4} "
              f"{s['temp_max']:>4}  {mark}")


if __name__ == "__main__":
    main()
