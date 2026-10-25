# 相談: SDCC が 16ビット値の上位バイトを作らずに `inc d` を吐く

**要点**: `--sdcccall 1`（SDCC 4.1.12 以降の既定）で、**16ビットのローカル変数の上位バイトを
初期化しないまま `inc d` で 256 を足す**機械語が出る。D には直前の処理の値が残っているため、
結果が 0x01xx ではなく 0x**??**xx になる。`--no-peep` でも出る。`--sdcccall 0` では出ない。

この文書だけで再現・検証できるように書いてある。

---

## 1. 環境

| | |
|---|---|
| コンパイラ | `SDCC : mcs51/z80/... 4.6.0 #16555 (Mac OS X ppc)` ※ `sdcc --version` の原文 |
| 入手元 | Homebrew `sdcc 4.6.0`（`brew info sdcc` の stable は `4.6.0_1`。**リリース版の最新**） |
| ホスト | macOS (Apple Silicon), Darwin 25.6.0 |
| 対象 | z80（MSX2 / MSX turbo R 用の ROM） |
| 指定 | `-mz80 --std-c99 --sdcccall 1 --opt-code-speed`（モジュールにより `--codeseg` / `--constseg` を追加） |

プロジェクトは **`--sdcccall 1` 前提**。手書きの asm ラッパ（`src/lowlevel.s`、`src/bios.s`）が
この規約で引数を受けており、`--sdcccall 0` へ戻す選択肢は実質ない。

## 2. 最小再現

```c
#include <stdint.h>

extern void sink(uint16_t a, uint16_t b, uint16_t c, uint8_t d, uint8_t e);
extern void draw(uint8_t i, uint16_t y);

static uint16_t px;
static uint8_t  idx, tick;

#define PAGE1 256
#define END   0x0516
#define LEAD  224
#define STEP  16
#define TAIL  10
#define TOP   26
#define OVER  0xFFFF

void tickfn(void)
{
    uint16_t band;
    uint8_t i;

    if (px == OVER) {
        return;
    }
    if (px >= END) {                       /* ← この分岐の中の16ビット計算が引き金 */
        sink(0, PAGE1, 256, 212, 1);
        for (i = 0; i < TAIL; i++) {
            draw((uint8_t)(79 - TAIL + i),
                 (uint16_t)(PAGE1 + TOP + (uint16_t)i * STEP));
        }
        px = OVER;
        return;
    }
    if (++tick < 4) {
        return;
    }
    tick = 0;
    px++;
    if ((uint8_t)(px & (STEP - 1)) == 0) {
        band = (uint16_t)((uint8_t)(px + LEAD));
        sink(0, (uint16_t)(PAGE1 + band), 256, STEP, 1);   /* ← ここが壊れる */
        draw(idx, (uint16_t)(PAGE1 + band));
    }
}
```

```
sdcc -mz80 --std-c99 --sdcccall 1 --opt-code-speed -S repro.c -o repro.asm
```

## 3. 出てくる機械語

**誤り（`--sdcccall 1`）**

```asm
;repro.c:33: band = (uint16_t)((uint8_t)(px + LEAD));
	ld	a, l
	add	a, #0xe0
;repro.c:34: sink(0, (uint16_t)(PAGE1 + band), 256, STEP, 1);
	ld	e, a          ; E = band の下位バイト
	inc	d             ; ← D を 0 にしないまま「+256」している
	push	de
```

D はこの関数の中で一度も初期化されていない。実際の値は直前の 16ビット比較
（`ld de, #0x0516` → `sbc hl, de`）や、その先の関数呼び出しが残したもの。

**正しい（`--sdcccall 0`、または引き金の分岐を削った場合）**

```asm
	ld	e, a
	ld	d, #0x00      ; ← これが出る
	inc	d
	push	de
```

**`--no-peep` を付けた場合（これも誤り。ただし形が違う）**

```asm
	ld	e, a
	ld	c, #0x00      ; ← 0 を置くのは C。使うのは D
	inc	d
	push	de
```

上位バイトを **C** に置いて **D** を使っているので、ピープホールが消したのではなく
**レジスタ割り当ての段階**でずれている、と読める。

## 4. 指定ごとの結果

| 指定 | 結果 |
|---|---|
| `--sdcccall 1 --opt-code-speed` | **誤り** |
| `--sdcccall 1`（最適化指定なし） | **誤り** |
| `--sdcccall 1 --opt-code-size` | **誤り** |
| `--sdcccall 1 --opt-code-speed --no-peep` | **誤り**（上の C/D のずれ） |
| `--sdcccall 1 --opt-code-speed --max-allocs-per-node 10000` | **誤り** |
| `--sdcccall 0 --opt-code-speed` | 正しい |

**引き金**: 早い `return` を持つ分岐（上の `if (px >= END) { ... return; }`）の中に
16ビットの計算があること。この分岐を削ると、後ろの経路でも `ld d, #0x00` が出るようになる。

## 5. 実際に起きたこと

MSX2 の VRAM は SCREEN 5 で 256×1024 の絵として見える。`PAGE1 = 256` を足して
「ページ1（VRAM 0x8000-）の y 座標」を作っていた。上位バイトが壊れた結果、
**書き込み先が 0x8808 ではなく 0x0808**（ページ0）になり、表示しているページ1 には
**1文字も描かれなかった**。スクロール自体は動くので「画面が真っ黒」に見える。

追い方（openMSX）:

```tcl
debug set_bp 0x405D {} { say [format "setwrite HL=%04X" [reg hl]] }   ;# _vdp_setwrite
```

→ `HL=0x0808` が並び、期待の `0x8808` と**ビット15だけが違う**ことから上位バイトの
問題と特定できた。

## 6. 上流の状況（2026-10-25 に確認）

同じ内容の報告は見当たらない。近いもの:

- [#4103](https://sourceforge.net/p/sdcc/bugs/4103/)
  `z80: peephole 36 drops "ld a, #n" although a is still used (wrong byte argument with __sdcccall(1))`
  — **同じ 4.6.0 #16555 への報告**。症状も「必要なロードが消える」で近いが、
  **ピープホールの規則が原因で `--no-peep` では出ない**。こちらは `--no-peep` でも出るので別物。
  r16955 で修正済みだが**リリースには入っていない**。
- [#3532](https://sourceforge.net/p/sdcc/bugs/3532/) z80 `address lost (register overwritten) prior to function call` — 修正済み・古い。
- z80 の未解決: [#4229](https://sourceforge.net/p/sdcc/bugs/4229/)（IY の上書き）、[#4206](https://sourceforge.net/p/sdcc/bugs/4206/)（gen.c の内部エラー）など。症状が別。

## 7. いまの回避

16ビットの組み立てを**別の関数へ出す**。関数の頭ではレジスタの思い込みが無いので、
素直に `ld d, #0x00` が出る（生成された `.asm` で確認済み）。

```c
static uint16_t page1_y(uint8_t line)
{
    return (uint16_t)(PAGE1 + (uint16_t)line);
}
```

```asm
_page1_y:
	ld	e, a
	ld	d, #0x00
	inc	d
	ret
```

## 8. 聞きたいこと

1. **これは既知の不具合か。** 別の番号で報告されていないか（検索語の見落としの可能性）。
2. **trunk（r16955 以降）で直っているか。** 直っているなら、どの修正で直ったか。
   直っていないなら報告する価値があるか。
3. **もっと確実な書き方はあるか。** いまは「別関数へ出す」で回避しているが、
   同じ地雷を踏まない一般的な書き方（例: 16ビット値を必ずローカルへ代入してから渡す、
   特定の構文を避ける等）があれば知りたい。
4. **同種の箇所を機械的に洗う方法。** いまは生成された `.asm` を
   「`inc d` / `inc h` / `inc b` の直前に上位バイトを置く命令があるか」で目視している
   （ループ変数の増加と紛らわしい）。もっと確実な検査方法があれば。
