#!/bin/sh
# 面データの往復を確かめる。
#
#   sh tools/hostsim/roundtrip.sh
#
# 面1〜15 を表と裏で組み立て、面データへ書き出して読み直し、
#
#   1. 盤面を決める変数の一覧 (--state) が組み立て直後と一致するか
#      = **面データが盤面を言い尽くせているか**
#   2. もう一度書き出した面データが元と一致するか
#      = **読み書きが対称か** (generator は出どころを書く欄なので除く)
#
# を見る。1 が落ちたら形式に取りこぼしがある (変数を足したのに
# tools/hostsim/stagefile.c を直していない、など)。
set -e

SIM=tools/hostsim/sim
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

[ -x "$SIM" ] || { echo "$SIM がありません。make hostsim を先に。" >&2; exit 1; }

ng=0
n=0
for ura in "" "--ura"; do
    for st in $(seq 1 15); do
        f=$TMP/s$st.json
        $SIM --stage "$st" $ura --dump > "$f"
        n=$((n + 1))

        $SIM --stage "$st" $ura --state     > "$TMP/a.txt"
        $SIM --load "$f"          --state   > "$TMP/b.txt"
        if ! diff -q "$TMP/a.txt" "$TMP/b.txt" > /dev/null; then
            echo "NG 面$st $ura: --state が一致しない"
            diff "$TMP/a.txt" "$TMP/b.txt" | head -8
            ng=$((ng + 1))
        fi

        $SIM --load "$f" --dump > "$TMP/c.json"
        grep -v '"generator"' "$f"          > "$TMP/d.json"
        grep -v '"generator"' "$TMP/c.json" > "$TMP/e.json"
        if ! diff -q "$TMP/d.json" "$TMP/e.json" > /dev/null; then
            echo "NG 面$st $ura: 書き出しが一致しない"
            diff "$TMP/d.json" "$TMP/e.json" | head -8
            ng=$((ng + 1))
        fi
    done
done

if [ "$ng" -eq 0 ]; then
    echo "OK  $n 面 (表15 + 裏15) すべて往復した"
else
    echo "NG  $ng 件"
    exit 1
fi
