#!/usr/bin/env python3
"""窓2 (0x8000) のオーバーレイで「見えない向こう側」を呼んでいないか検査する。

窓2は普段 PLAYCODE を向いていて、build_stage() の間だけ STAGEGEN を向く。
だから次の3つはどれも**その場で暴走する**:

  1. 組み立て (build_stage から辿れるもの) が PLAYCODE の記号に触る
  2. 遊ぶ側が STAGEGEN の記号に触る (入口は build_stage だけが許される)
  3. 割り込み処理がどちらかに触る (いつ入るか分からない)

全モジュールの .asm から呼び出し関係を作る。**置き場は .asm の `.area` 指令で
引く**。.map には static の記号が出ないので、.map から引くと static が
「どこのものでもない」ことになり、検査が穴だらけになる (一度これで誤判定した)。
`jp`/`call` だけでなく**番地を取るだけ (ld hl,#_f) も参照として数える**。
"""
import glob, os, re, sys
from collections import defaultdict

WIN2 = {'STAGEGEN': 'gen', 'PLAYCODE': 'play'}

calls = defaultdict(set)
side = {}          # 記号 -> 'gen' / 'play' / None (固定領域・窓1・RAM・変数)

def scan(path):
    cur, area = None, None
    for line in open(path):
        m = re.match(r'^\s*\.area\s+_?(\w+)', line)
        if m:
            area = m.group(1); continue
        m = re.match(r'^(_?[A-Za-z0-9_]+)::?', line)
        if m:
            cur = m.group(1)
            calls.setdefault(cur, set())
            side[cur] = WIN2.get(area)
            continue
        if cur is None:
            continue
        for m in re.finditer(r'#?(_[A-Za-z0-9_]+)\b', line):
            if m.group(1) != cur:
                calls[cur].add(m.group(1))

for p in sorted(glob.glob('build/*.asm')) + ['src/lowlevel.s', 'src/bios.s', 'src/crt0.s']:
    scan(p)

def reach(roots, barrier=()):
    """roots から辿れる記号。barrier の記号は「触った」とだけ数えて、
    **その先へは進まない**。build_stage は窓を切り替えてから呼ぶ唯一の入口なので、
    ここで止めないと組み立ての中身まで「遊ぶ側から辿れる」ことになってしまう。"""
    seen, stack = set(), list(roots)
    while stack:
        f = stack.pop()
        if f in seen:
            continue
        seen.add(f)
        if f in barrier:
            continue
        stack.extend(calls.get(f, ()))
    return seen

def hits(syms, want):
    return sorted(s for s in syms if side.get(s) == want)

bad = []

h = hits(reach(['_build_stage']), 'play')
print("1. 組み立て (build_stage) から PLAYCODE へ:", h or "なし")
if h: bad.append(1)

# 遊ぶ側の根 = 窓2 に居ない関数すべて + PLAYCODE 自身。
# **build_stage で止める** (そこは窓を切り替えてから呼ぶ、唯一の許された入口)。
roots = [s for s in calls if side.get(s) != 'gen']
h = [s for s in hits(reach(roots, barrier={'_build_stage'}), 'gen')
     if s != '_build_stage']
print("2. 遊ぶ側から STAGEGEN へ:", h or "なし (build_stage だけ)")
if h: bad.append(2)

# 2b. build_stage を呼んでいるのは1か所だけか。**窓を切り替えずに呼んだら死ぬ**ので、
#     呼び元が増えていないことを見る (増やすなら、そこでも切り替えること)。
callers = sorted(k for k, v in calls.items() if '_build_stage' in v)
ok = callers == ['_start_stage']
print(f"2b. build_stage の呼び元: {callers}", "" if ok else "<- 1か所 (_start_stage) のはず")
if not ok: bad.append('2b')

isr = [s for s in calls if 'isr' in s.lower() or 'hook' in s.lower()]
if not isr:
    print("3. 割り込みの入口が見つかりません"); bad.append(3)
else:
    h = hits(reach(isr), 'gen') + hits(reach(isr), 'play')
    print(f"3. 割り込み {isr} から窓2へ:", h or "なし")
    if h: bad.append(3)

n_gen  = sum(1 for s in side.values() if s == 'gen')
n_play = sum(1 for s in side.values() if s == 'play')
print(f"\n(STAGEGEN の記号 {n_gen} 個 / PLAYCODE の記号 {n_play} 個を見た)")
print("=> " + ("問題なし" if not bad else f"**問題あり: 検査 {bad}**"))
sys.exit(1 if bad else 0)
