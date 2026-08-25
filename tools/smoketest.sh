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
A_POWDER=$(addr powder); A_DIA=$(addr diamond); A_DUG=$(addr dug)
A_PCT=$(addr dug_pct)

# 面1は掘り抜きが数字の「1」(15升)。自機は巣から一番遠い升目、巣は画面中央付近。
# 火薬・岩・ダイヤの位置は乱数なので、位置ではなく個数で検査する。
openmsx -machine "$MACHINE" -carta "$ROM" -romtype ASCII8 \
    -command "set renderer none; set throttle off; \
        after time 6 {keymatrixdown 8 0x01}; after time 6.2 {keymatrixup 8 0x01}; \
        after time 8.5 { \
            set f [open $OUT w]; \
            puts \$f \"R0 [debug read {VDP regs} 0]\"; \
            puts \$f \"R1 [debug read {VDP regs} 1]\"; \
            puts \$f \"DIRT [debug read VRAM 0]\"; \
            puts \$f \"NESTCELL [debug read VRAM 11322]\"; \
            puts \$f \"PX [debug read memory $A_PX]\"; \
            puts \$f \"PY [debug read memory $A_PY]\"; \
            puts \$f \"STARTC [debug read memory $A_STARTC]\"; \
            puts \$f \"STARTR [debug read memory $A_STARTR]\"; \
            puts \$f \"NESTC [debug read memory $A_NESTC]\"; \
            puts \$f \"NESTR [debug read memory $A_NESTR]\"; \
            puts \$f \"BATHON [debug read memory $A_BATHON]\"; \
            set p 0; set d 0; set g 0; \
            for {set i 0} {\$i<176} {incr i} { \
                incr p [debug read memory [expr {$A_POWDER+\$i}]]; \
                incr d [debug read memory [expr {$A_DIA+\$i}]]; \
                incr g [debug read memory [expr {$A_DUG+\$i}]] }; \
            puts \$f \"POWDERN \$p\"; puts \$f \"DIAN \$d\"; puts \$f \"DUGN \$g\"; \
            puts \$f \"PCT [debug read memory $A_PCT]\"; \
            close \$f; exit }" >/dev/null 2>&1

fail=0
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
check NESTCELL 17  "掘り抜きの縦線が掘られている (0x11)"
check DUGN     15  "面1の掘り抜きが数字の「1」の形 (15升)"
check PCT      8   "開始時の採掘率"
check POWDERN  10  "火薬が10個置かれている"
check DIAN     0   "面1のダイヤは0個 ((面-1)x2% = 0%)"
check STARTC   5   "自機の出発点の列 (巣から一番遠い升目)"
check STARTR   9   "自機の出発点の行"
check NESTC    7   "敵の巣の列 (画面中央に一番近い掘り抜き)"
check NESTR    5   "敵の巣の行"
check PX       80  "自機のX (= 出発点の列 x16)"
check PY       144 "自機のY (= 出発点の行 x16)"
check BATHON   1   "風呂が健在"

[ $fail -eq 0 ] && echo "smoketest: PASS" || echo "smoketest: FAIL"
exit $fail
