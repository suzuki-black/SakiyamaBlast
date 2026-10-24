/*
 * SakiyamaBlast - タイトル画面
 *
 * タイトルは1枚絵 (SCREEN 5 / 256x212 / 16色) を ROM の可変バンクから VRAM へ展開する。
 * ROM のセグメント TITLE_SEG..+3 が VRAM の 8KB ブロック 0..3 にそのまま対応するので、
 * バンクを切り替えながら順に流し込むだけでよい。
 * 画像は assets/title.png から tools/png2sc5.py が生成する。
 */
#include "msx.h"
#include "vdp.h"
#include "game.h"
#include "title_pal.h"

#define TITLE_SEG   4

void title_draw(void)
{
    uint8_t i, blk;
    uint16_t left = 27136;          /* 256 * 212 / 2 */

    for (i = 0; i < 16; i++) {
        VDP_REG(16, i);
        vdp_wpal(title_palette[i * 2]);
        vdp_wpal(title_palette[i * 2 + 1]);
    }
    VDP_REG(7, 0);                  /* 背景色は黒。パレット0番は背景色として出る。 */

    for (blk = 0; blk < 4 && left; blk++) {
        vdp_blklen = (left > 8192) ? 8192 : left;
        bank_select((uint8_t)(TITLE_SEG + blk));
        vdp_setwrite((uint16_t)((uint16_t)blk << 13));
        vdp_wblock(BANK_WINDOW);
        left = (uint16_t)(left - vdp_blklen);
    }
    bank_select(DATA_SEG);      /* 窓を絵と曲へ戻す。戻し忘れると音も絵も壊れる */
    /* **副題はゲーム側の字で描く。** 絵の中の「〜先山丹野3時〜」は崩れているので
       黒で消してある (`Makefile` の `TITLE_OPTS`)。**窓を戻した後に呼ぶこと** —
       `kanji_subtitle()` は窓側にあるので、絵へ向けている間は見えない。 */
    kanji_subtitle(TITLE_COL_SUB, TITLE_COL_BG);
}
