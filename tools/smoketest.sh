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
A_ROCKX=$(addr rock_x); A_ROCKY=$(addr rock_y)
A_BATHON=$(addr bath_on); A_BATHC=$(addr bath_c); A_BATHR=$(addr bath_r)
A_NESTC=$(addr nest_c); A_NESTR=$(addr nest_r)

# タイトル1枚絵の照合用。坑夫の胴あたり (x=128,y=120) の VRAM = 120*128 + 64
TITLE_OFS=15424
TITLE_EXPECT=$(python3 -c "print(open('$TITLE','rb').read()[$TITLE_OFS])")

# 自機の初期位置はセル(8,9) = px 128,144。掘り跡は (130,146)-(141,157)。
# その内側 (132,150) の VRAM アドレス = 150*128 + 132/2 = 19266。
# 縦坑のセル(8,5)中心 (132,82) = 82*128 + 66 = 10562。火薬(2,3)の樽 (38,55) = 55*128+19 = 7059。
openmsx -machine "$MACHINE" -carta "$ROM" -romtype ASCII8 \
    -command "set renderer none; set throttle off; \
        after time 6.5 { set g [open $OUT.t w]; \
            puts \$g \"TITLE [debug read VRAM $TITLE_OFS]\"; close \$g }; \
        after time 7.0 { keymatrixdown 8 0x01 }; \
        after time 7.2 { keymatrixup 8 0x01 }; \
        after time 9 { \
            set f [open $OUT w]; \
            puts \$f \"R0 [debug read {VDP regs} 0]\"; \
            puts \$f \"R1 [debug read {VDP regs} 1]\"; \
            puts \$f \"DIRT [debug read VRAM 0]\"; \
            puts \$f \"TUNNEL [debug read VRAM 19266]\"; \
            puts \$f \"SPINE [debug read VRAM 10562]\"; \
            puts \$f \"POWDER [debug read VRAM 7059]\"; \
            puts \$f \"PX [debug read memory $A_PX]\"; \
            puts \$f \"PY [debug read memory $A_PY]\"; \
            puts \$f \"ROCKX [debug read memory $A_ROCKX]\"; \
            puts \$f \"ROCKY [debug read memory $A_ROCKY]\"; \
            puts \$f \"BATHON [debug read memory $A_BATHON]\"; \
            puts \$f \"BATHC [debug read memory $A_BATHC]\"; \
            puts \$f \"BATHR [debug read memory $A_BATHR]\"; \
            puts \$f \"NESTC [debug read memory $A_NESTC]\"; \
            puts \$f \"NESTR [debug read memory $A_NESTR]\"; \
            close \$f; exit }" >/dev/null 2>&1

cat "$OUT.t" >> "$OUT" 2>/dev/null || true
rm -f "$OUT.t"

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
check TITLE  "$TITLE_EXPECT" "タイトル1枚絵が VRAM へ展開されている"
check R0     6   "SCREEN 5 (GRAPHIC 4) になっている"
check R1     98  "表示ON・垂直帰線割り込みON・16x16スプライト"
check DIRT   102 "盤面が土で塗られている (0x66)"
check TUNNEL 17  "自機の足元が掘られている (0x11)"
check SPINE  17  "面データの縦坑が掘られている (0x11)"
check POWDER 136 "火薬が置かれ描かれている (0x88)"
check PX     128 "自機の初期位置 X (セル8)"
check PY     144 "自機の初期位置 Y (セル9)"
check ROCKX  64  "岩の初期位置 X (セル4)"
check ROCKY  80  "岩の初期位置 Y (セル5)"
check BATHON 1   "風呂が健在"
check BATHC  8   "風呂の列 (自機の出現位置の近く)"
check BATHR  8   "風呂の行 (面1)"
check NESTC  8   "敵の巣の列 (画面中央)"
check NESTR  5   "敵の巣の行 (画面中央)"

[ $fail -eq 0 ] && echo "smoketest: PASS" || echo "smoketest: FAIL"
exit $fail
