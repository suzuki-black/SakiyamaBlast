#!/bin/sh
# ゲーム中の fps を両機種で測る。
#
#   sh tools/fps.sh [rom] [map]
#
# **openMSX の設定ファイルは退避したものを渡す** (CLAUDE.md)。計測で何かを変えても
# 作者の環境が汚れないようにするため。音量には触らない。
set -e
ROM=${1:-build/sakiyama.rom}
MAP=${2:-build/sakiyama.map}
TMP=$(mktemp -d -t sakiyama-fps)
trap 'rm -rf "$TMP"' EXIT
cp ~/.openMSX/share/settings.xml "$TMP/s.xml" 2>/dev/null || true

addr() {
    a=$(awk -v s="_$1" '$2==s {print $1; exit}' "$MAP")
    [ -n "$a" ] || { echo "fps: シンボル _$1 が見つかりません" >&2; exit 1; }
    printf '%d' "$((16#$a))"
}

FRAMEN=$(addr frame_n); CFGSTAGE=$(addr cfg_start_stage); TEMP=$(addr temp)

for M in ${MACHINES:-C-BIOS_MSX2+_JP My_FS-A1ST_512K}; do
    OUT="$TMP/$M.txt"
    FPSOUT="$OUT" FRAMEN="$FRAMEN" CFGSTAGE="$CFGSTAGE" TEMP="$TEMP" \
        timeout 180 openmsx -setting "$TMP/s.xml" -machine "$M" \
            -carta "$ROM" -romtype ASCII8 -script tools/fps.tcl >/dev/null 2>&1 || true
    if [ -s "$OUT" ]; then
        printf '%-20s %s\n' "$M" "$(cat "$OUT")"
    else
        printf '%-20s 測れませんでした\n' "$M"
    fi
done
