/* SakiyamaBlast - BIOS フォントによる文字表示 (SCREEN 5 のビットマップへ直接描く) */
#ifndef TEXT_H
#define TEXT_H

#include "msx.h"

void text_init(void);
/* x は偶数、scale は 1 か 2。bg に 0xFF を渡すと背景を塗らない…のではなく、
   本作では常に bg で塗り潰す (消すときは盤面を描き直す)。 */
void text_draw(uint16_t x, uint16_t y, const char *s,
               uint8_t fg, uint8_t bg, uint8_t scale);
uint8_t text_u8(char *dst, uint8_t v);   /* 数値を10進で書き出す。戻り値=桁数 */

#endif
