/* SakiyamaBlast - スプライト (SC5 / mode 2 / 16x16) */
#ifndef SPRITE_H
#define SPRITE_H

#include "msx.h"

#define SPR_PLANES  32
#define SPR_PARK_Y  212      /* 画面外。216 は mode 2 の行終端マーカーなので使わない */

/* OAM の複製 (Y, X, パターン, 予備) x 32面。oam_flush() で VRAM へ一括転送する。 */
extern uint8_t oam[SPR_PLANES * 4];
/* oam_flush() が毎フレーム転送するバイト数。使っている面のぶんだけにする。 */
extern uint8_t oam_len;

void spr_init(void);
void spr_pattern(uint8_t pat, const uint8_t *data);   /* VRAM 並びそのまま (32バイト) */
/* 16行 x 16bit で書いた絵を登録する。VRAM は「左半分16バイト→右半分16バイト」の
   並びなので、作画しやすい行指向の表からこちらで並べ替える。 */
void spr_pattern_rows(uint8_t pat, const uint16_t *rows);
void spr_color(uint8_t plane, uint8_t color);         /* 全16行を同じ色にする */
void spr_color_lines(uint8_t plane, const uint8_t *cols);  /* mode 2 の行ごとの色 */
void spr_set(uint8_t plane, uint8_t x, uint8_t y, uint8_t pat);
void spr_park(uint8_t plane);
void spr_park_all(void);

#endif
