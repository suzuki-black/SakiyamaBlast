/*
 * SDCC が誤った機械語を吐く条件の最小再現。**ビルドには入らない。**
 *
 *   sdcc -mz80 --std-c99 --sdcccall 1 --opt-code-speed -S tools/sdcc_regalloc_repro.c -o /tmp/a.asm
 *
 * 出てくる機械語の、`band = ...` の直後を見る。
 *
 *   正しい: ld e, a / ld d, #0x00 / inc d / push de     (DE = 256 + band)
 *   誤り  : ld e, a /              inc d / push de     (**D は前の処理の残り**)
 *
 * **誤りが出るのは `--sdcccall 1` のとき** (SDCC 4.1.12 以降の既定)。
 * `--opt-code-speed` / `--opt-code-size` / `--no-peep` のどれでも出る。
 * `--sdcccall 0` にすると正しくなるが、本プロジェクトは asm のラッパを
 * `--sdcccall 1` で書いているので戻せない (`src/lowlevel.s`)。
 * 確認した版: SDCC 4.6.0 #16555 (Mac OS X ppc)。
 *
 * **引き金**: 早い段階の `return` を持つ分岐の中で 16ビットの計算 (ここでは
 * `PAGE1 + TOP + i * STEP`) をしていると、後ろの経路で「上位バイトは 0 のはず」と
 * 誤って覚えたまま `inc d` だけを吐く。
 *
 * **実害**: エンドロールの描画先が 0x8808 ではなく 0x0808 になり、ページ1 へ
 * 1文字も描かれなかった (2026-10-25)。回避は `src/ending.c` の `page1_y()` のように
 * **16ビットの組み立てを別の関数へ出す**こと。関数の頭ではレジスタの思い込みが無い。
 *
 * 新しい SDCC へ上げたときは、まずこれを通して直っているか確かめる。
 */
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
    if (px >= END) {                       /* ここの中の16ビット計算が引き金 */
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
        sink(0, (uint16_t)(PAGE1 + band), 256, STEP, 1);   /* ここが壊れる */
        draw(idx, (uint16_t)(PAGE1 + band));
    }
}
