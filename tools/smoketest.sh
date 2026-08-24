#!/bin/sh
# ROM が実際に起動して SCREEN 5 を組み、盤面と自機を描いたかを openMSX 上で検査する。
# 画面を開かない (renderer none) ので CI でも回せる。
set -e

ROM=${1:-build/sakiyama.rom}
MACHINE=${MACHINE:-C-BIOS_MSX2+_JP}
OUT=$(mktemp -t sakiyama-smoke)
trap 'rm -f "$OUT"' EXIT

if [ ! -f "$ROM" ]; then
    echo "smoketest: $ROM がありません。先に make を実行してください。" >&2
    exit 1
fi

# 自機の初期位置はセル(8,9) = px 128,144。掘り跡は (130,146)-(141,157)。
# その内側 (132,150) の VRAM アドレス = 150*128 + 132/2 = 19266。
openmsx -machine "$MACHINE" -carta "$ROM" -romtype ASCII8 \
    -command "set renderer none; set throttle off; \
        after time 6 { \
            set f [open $OUT w]; \
            puts \$f \"R0 [debug read {VDP regs} 0]\"; \
            puts \$f \"R1 [debug read {VDP regs} 1]\"; \
            puts \$f \"DIRT [debug read VRAM 0]\"; \
            puts \$f \"TUNNEL [debug read VRAM 19266]\"; \
            puts \$f \"OAMY [debug read VRAM 30208]\"; \
            puts \$f \"OAMX [debug read VRAM 30209]\"; \
            puts \$f \"SPINE [debug read VRAM 10562]\"; \
            puts \$f \"POWDER [debug read VRAM 7059]\"; \
            puts \$f \"ROCKY [debug read VRAM 30216]\"; \
            puts \$f \"ROCKX [debug read VRAM 30217]\"; \
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
check R0     6   "SCREEN 5 (GRAPHIC 4) になっている"
check R1     98  "表示ON・垂直帰線割り込みON・16x16スプライト"
check DIRT   102 "盤面が土で塗られている (0x66)"
check TUNNEL 17  "自機の足元が掘られている (0x11)"
check OAMY   143 "自機スプライトの Y (py-1 = 143)"
check OAMX   128 "自機スプライトの X (px = 128)"
check SPINE  17  "面データの縦坑が掘られている (0x11)"
check POWDER 136 "火薬が置かれ描かれている (0x88)"
check ROCKY  79  "岩スプライトの Y (セル(4,5) = 79)"
check ROCKX  64  "岩スプライトの X (セル(4,5) = 64)"

[ $fail -eq 0 ] && echo "smoketest: PASS" || echo "smoketest: FAIL"
exit $fail
