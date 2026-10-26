/*
 * SakiyamaBlast - 漢字 (面の開始表示)
 *
 * BIOS のフォントには漢字が無く、機械の漢字ROM は日本製の一部にしか載っていない。
 * 面の開始に出す5文字だけ自前で持つ。**全機種で同じ字が出る。**
 *
 * 字は 16x16 の 1bpp。線を太めにして、当時の写植のような重い字面を狙っている。
 * 絵と同じで**これは一次案**なので、気に入らなければ tools/mkkanji.py の
 * ドット絵を描き替えて make し直せばよい。
 */
#include "game.h"
#include "text.h"
#include "vdp.h"
#include "msx.h"

/* **字形は ROM の専用セグメントにある** (`KANJI_SEG`)。窓は満杯なので、
   576バイトの表をそこから追い出した (2026-10-23)。読むときだけ `blob_read` で
   32バイトを RAM へ写す。**窓を切り替えている間に走るのは固定領域のルーチンだけ**
   なので、このファイル自身が窓側にあっても安全。
   形の元は `tools/mkkanji.py` のドット絵 (`make` が build/kanji.bin を作る)。 */
static uint8_t glyph[32];

/* 1行ぶんを組み立ててから一括で流す。1バイトずつ書くと重い (draw_tile と同じ話)。 */
static uint8_t rowbuf[8];

void kanji_draw(uint16_t x, uint16_t y, uint8_t idx, uint8_t fg, uint8_t bg)
{
    const uint8_t *g = glyph;
    blob_seg = KANJI_SEG;
    blob_off = (uint16_t)(0xA000 + ((uint16_t)idx << 5));
    blob_dst = glyph;
    blob_len = 32;
    blob_read();
    uint8_t row, i;
    uint8_t pair_fg = (uint8_t)((fg << 4) | fg);
    uint8_t pair_bg = (uint8_t)((bg << 4) | bg);
    uint8_t mix_hi  = (uint8_t)((fg << 4) | bg);
    uint8_t mix_lo  = (uint8_t)((bg << 4) | fg);
    for (row = 0; row < 16; row++) {
        uint16_t bits = (uint16_t)(((uint16_t)g[row << 1] << 8) | g[(row << 1) + 1]);
        for (i = 0; i < 8; i++) {
            uint8_t two = (uint8_t)((bits >> 14) & 3);
            rowbuf[i] = (uint8_t)((two == 3) ? pair_fg :
                                  (two == 2) ? mix_hi  :
                                  (two == 1) ? mix_lo  : pair_bg);
            bits = (uint16_t)(bits << 2);
        }
        vdp_setwrite((uint16_t)(((y + row) << 7) + (x >> 1)));
        vdp_blklen = 8;
        vdp_wblock(rowbuf);
    }
}

/*
 * タイトルの副題「〜先山丹野3時〜」を描く。
 *
 * **1枚絵の中の字は崩れている** (生成画像の漢字で、「先」が「光」、「野」が「墅」に
 * 見える)。絵のほうはその帯を黒で塗り潰してあり (`Makefile` の `TITLE_OPTS`)、
 * ここで 16x16 の字を置き直す。**色は絵のパレットから選んだ番号を渡してもらう**
 * (1枚絵のパレットは絵ごとに変わるため。`tools/png2sc5.py` が番号を出す)。
 *
 * **窓側に置いてある。** 呼ぶのは `title_draw()` の末尾で、そこでは窓が
 * `DATA_SEG` へ戻っているので見える。RAM 実行の領域には余裕が無い。
 */
#define SUB_X 64        /* 8字 x 16px = 128px を画面の中央へ */
#define SUB_Y 50        /* ロゴの下、坑夫の上 (元絵の副題と同じ帯) */
#define URA_X 80        /* 倍角6字 = 96px を画面の中央へ */
#define URA_Y 176       /* `PRESS SPACE KEY` の真上 */

void kanji_subtitle(uint8_t fg, uint8_t bg)
{
    static const uint8_t sub[8] = {
        KJ_WAVE, KJ_SEN, KJ_YAMA, KJ_TAN, KJ_NO, KJ_3, KJ_JI, KJ_WAVE
    };
    uint8_t i;
    for (i = 0; i < 8; i++) {
        kanji_draw((uint16_t)(SUB_X + ((uint16_t)i << 4)), SUB_Y, sub[i], fg, bg);
    }
    /* **裏面が開いていることをタイトルで知らせる。** 15面をクリアすると出る。
       ここに出すのは、次にスペースを押すと裏面が始まるから (作者の指定、2026-10-25)。
       倍角6字 = 96px を中央へ。`PRESS SPACE KEY` の真上。 */
    if (loop_n) {
        text_draw(URA_X, URA_Y, "URAMEN", fg, bg, 2);
    }
}
