/*
 * SakiyamaBlast - 絵の参照口
 *
 * スプライトとタイルの絵は assets/sprites.png / assets/tiles.png が正で、
 * ビルド時に tools/png2art.py が C の配列へ変換する。**絵を差し替えるときは
 * PNG を描き替えて make し直すだけでよい**（このファイルもコードも触らなくてよい）。
 *
 * 生成データの実体は art.c だけが持つ。他のモジュールはここの関数から参照する。
 */
#ifndef ART_H
#define ART_H

#include "msx.h"

/* スプライトの番号は game.h の PAT_MINER..PAT_BAT と同じ順で、
   assets/sprites.png のセルの並びもそれに合わせてある。 */
const uint16_t *art_pattern(uint8_t i);   /* 16行 x 16bit の形 */
uint8_t art_pattern_count(void);          /* スプライトの枚数 (転送漏れ防止) */
const uint8_t  *art_colours(uint8_t i);   /* mode-2 の行ごとの色 (16バイト) */

/* 背景タイル (16x16 の 4bpp、1枚128バイト) */
#define TILE_POWDER 0
#define TILE_BATH   1
#define TILE_BSCORE 2      /* ボーナス: 得点 */
#define TILE_BCOOL  3      /* ボーナス: 体温全回復 */
#define TILE_BLIFE  4      /* ボーナス: 残機+1 */
#define TILE_CAVE   5      /* 敵の湧き出し口 (横穴) */
#define TILE_DIA    6      /* 黒ダイヤ (石炭)。つるはしで割る */
#define TILE_DYNA   7      /* 投げて落ちたダイナマイト */
#define TILE_DIACRK 8      /* 割れた黒ダイヤ。通れて、通ると採掘できる */
#define TILE_PILLAR 9      /* 炭柱。岩を支えている黒ダイヤ。三ツ枠を巻いてある */
/* 出水 (第三坑のギミック)。熱水そのものはタイルを使わず、坑道の形に塗って描く
   (field.c の draw_water)。絵が要るのは湧き出し口だけ。 */
#define TILE_SPRING 10     /* 湧き出し口。面の開始から見えている */
#define TILE_SPRINGOFF 11  /* 止まった湧き出し口。割れ目が塞がった絵 = もう増えない印 */
/* 樋 (とい)。石炭を上の坑道から下へ落とすために斜めに架けた木の板。
   **升は面の最初から開いている**ので、坑道の上へ板だけを重ねて描く。 */
#define TILE_TOIR   12     /* 右下がり */
#define TILE_TOIL   13     /* 左下がり */
/* 使い終わった板。通行にも落下にも影響しない跡で、**向きは残す**
   (その板がどちらへ落とすはずだったかまで読める)。 */
#define TILE_TOIBR  14     /* 折れた板 (もとは右下がり) */
#define TILE_TOIBL  15     /* 折れた板 (もとは左下がり) */
const uint8_t *art_tile_of(uint8_t i);

#endif
