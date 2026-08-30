#!/bin/sh
# ROM が実際に起動して SCREEN 5 を組み、面データ (坑道・火薬・岩・風呂) と自機を
# 正しく用意したかを openMSX 上で検査する。画面を開かない (renderer none) ので CI でも回せる。
#
# スプライト (OAM) は無敵中の点滅や敵の追跡で揺れるため見ない。代わりにリンクマップから
# 変数のアドレスを引いて RAM を直接読む。
set -e

ROM=${1:-build/sakiyama.rom}
MAP=${MAP:-build/sakiyama.map}
TITLE=${TITLE:-build/title.bin}
MACHINE=${MACHINE:-C-BIOS_MSX2+_JP}
OUT=$(mktemp -t sakiyama-smoke)
trap 'rm -f "$OUT"' EXIT

if [ ! -f "$ROM" ] || [ ! -f "$MAP" ] || [ ! -f "$TITLE" ]; then
    echo "smoketest: $ROM / $MAP がありません。先に make を実行してください。" >&2
    exit 1
fi

# リンクマップから C 変数のアドレスを引く (例: _px -> 0xC173)
addr() {
    a=$(awk -v s="_$1" '$2==s {print $1; exit}' "$MAP")
    if [ -z "$a" ]; then
        echo "smoketest: シンボル _$1 がマップに見つかりません" >&2
        exit 1
    fi
    printf '0x%s' "$(echo "$a" | sed 's/^0*//')"
}

A_PX=$(addr px);        A_PY=$(addr py)
A_STARTC=$(addr start_c); A_STARTR=$(addr start_r)
A_NESTC=$(addr nest_c); A_NESTR=$(addr nest_r)
A_BATHON=$(addr bath_on); A_BATHC=$(addr bath_c); A_BATHR=$(addr bath_r)
A_POWDER=$(addr powder); A_COAL=$(addr coal); A_DUG=$(addr dug)
A_COALLEFT=$(addr coal_left)
A_PILLAR=$(addr pillar)
A_PCT=$(addr dug_pct)

# 面1は掘り抜きが数字の「1」(15升) ＋ 敵の湧き出し口1升 = 16升。
# 自機は掘り抜きの上端、湧き出し口は土の中の独立した1ブロック (位置は乱数)。
# 火薬・岩・黒ダイヤ・湧き出し口の位置は乱数なので、位置ではなく性質で検査する。
openmsx -machine "$MACHINE" -carta "$ROM" -romtype ASCII8 \
    -command "set renderer none; set throttle off; \
        after time 6 {keymatrixdown 8 0x01}; after time 6.2 {keymatrixup 8 0x01}; \
        proc ready {} { \
            if {[debug read {VDP regs} 1] != 98} { after time 0.1 ready; return }; \
            after time 0.5 sample }; \
        after time 6.5 ready; \
        proc sample {} { \
            set f [open $OUT w]; \
            puts \$f \"R0 [debug read {VDP regs} 0]\"; \
            puts \$f \"R1 [debug read {VDP regs} 1]\"; \
            puts \$f \"DIRT [debug read VRAM 0]\"; \
            set sc [debug read memory $A_STARTC]; set sr [debug read memory $A_STARTR]; \
            puts \$f \"TUNNELCELL [debug read VRAM [expr {(\$sr*16+8)*128 + (\$sc*16+8)/2}]]\"; \
            puts \$f \"PX [debug read memory $A_PX]\"; \
            puts \$f \"PY [debug read memory $A_PY]\"; \
            puts \$f \"STARTC [debug read memory $A_STARTC]\"; \
            puts \$f \"STARTR [debug read memory $A_STARTR]\"; \
            set nc [debug read memory $A_NESTC]; set nr [debug read memory $A_NESTR]; \
            set sc [debug read memory $A_STARTC]; set sr [debug read memory $A_STARTR]; \
            puts \$f \"NESTDUG [debug read memory [expr {$A_DUG+\$nr*16+\$nc}]]\"; \
            puts \$f \"NESTFAR [expr {(abs(\$nc-\$sc)+abs(\$nr-\$sr)) >= 6}]\"; \
            puts \$f \"NESTOPEN [expr {[debug read memory [expr {$A_DUG+(\$nr-1)*16+\$nc}]] || [debug read memory [expr {$A_DUG+(\$nr+1)*16+\$nc}]] || [debug read memory [expr {$A_DUG+\$nr*16+\$nc-1}]] || [debug read memory [expr {$A_DUG+\$nr*16+\$nc+1}]]}]\"; \
            puts \$f \"BATHON [debug read memory $A_BATHON]\"; \
            set p 0; set d 0; set g 0; \
            for {set i 0} {\$i<176} {incr i} { \
                incr p [debug read memory [expr {$A_POWDER+\$i}]]; \
                if {[debug read memory [expr {$A_COAL+\$i}]]} {incr d}; \
                incr g [debug read memory [expr {$A_DUG+\$i}]] }; \
            set pl 0; \
            for {set i 0} {\$i<176} {incr i} { incr pl [debug read memory [expr {$A_PILLAR+\$i}]] }; \
            puts \$f \"PILLARN \$pl\"; \
            puts \$f \"POWDERN \$p\"; puts \$f \"COALN \$d\"; puts \$f \"COALLEFT [debug read memory $A_COALLEFT]\"; puts \$f \"DUGN \$g\"; \
            puts \$f \"PCT [debug read memory $A_PCT]\"; \
            close \$f; exit }; \
        " >/dev/null 2>&1

fail=0
val() { awk -v k="$1" '$1==k {print $2}' "$OUT"; }
# 下限だけを見る検査 (乱数で値が動くが、0 では困るもの)
check_min() {
    got=$(val "$1")
    if [ -n "$got" ] && [ "$got" -ge "$2" ] 2>/dev/null; then
        echo "  ok   $3 ($1=$got >= $2)"
    else
        echo "  FAIL $3 ($1: expected >= $2, got ${got:-none})"
        fail=1
    fi
}
check() {
    got=$(awk -v k="$1" '$1==k {print $2}' "$OUT")
    if [ "$got" = "$2" ]; then
        echo "  ok   $3 ($1=$got)"
    else
        echo "  FAIL $3 ($1: expected $2, got ${got:-none})"
        fail=1
    fi
}

echo "smoketest: $ROM on $MACHINE"
check R0       6   "SCREEN 5 (GRAPHIC 4) になっている"
check R1       98  "表示ON・垂直帰線割り込みON・16x16スプライト"
check DIRT     102 "盤面が土で塗られている (0x66)"
check TUNNELCELL 17 "自機の出発点が坑道として描かれている (0x11)"
check DUGN     16  "掘り抜き「1」(15升) ＋ 湧き出し口(1升)"
check PCT      9   "開始時の採掘率"
check POWDERN  10  "火薬が10個置かれている"
# 黒ダイヤは coal_n_tbl[0]=4 個 ＋ 炭柱 (PILLAR_MAX 本まで)。岩の真下が既に
# 埋まっていれば炭柱は置かれないので、実際に立った本数で照合する。
PILLARN=$(val PILLARN)
check_min PILLARN 1 "炭柱が岩の真下に置かれている"
check COALN    $((4 + PILLARN)) "黒ダイヤ = 面1の4個 + 炭柱${PILLARN}個"
check COALLEFT $((4 + PILLARN)) "残りの黒ダイヤ数が一致している"
check STARTC   7   "自機の出発点の列 (掘り抜きの上端)"
check STARTR   1   "自機の出発点の行"
check NESTDUG  1   "敵の湧き出し口が1ブロック開いている"
check NESTFAR  1   "湧き出し口が自機の出発点から6升以上離れている"
check NESTOPEN 1   "湧き出し口が坑道に面している (敵が歩いて出てこられる)"
check PX       112 "自機のX (= 出発点の列 x16)"
check PY       16  "自機のY (= 出発点の行 x16)"
check BATHON   1   "風呂が健在"

[ $fail -eq 0 ] && echo "smoketest: PASS" || echo "smoketest: FAIL"
exit $fail
