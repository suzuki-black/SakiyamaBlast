/*
 * SakiyamaBlast - 文字表示
 *
 * 本体 BIOS が持つ 8x8 フォントをそのまま使う (ROM にフォントデータを持たなくて済む)。
 * フォントの先頭アドレスは BIOS の 0x0004 に入っている (CGTBL)。ページ0は BIOS のままなので
 * カートリッジから起動していても普通に読める。漢字は無い (ASCII と半角カナのみ)。
 */
#include "text.h"
#include "vdp.h"

static uint16_t cgtbl;

void text_init(void)
{
    cgtbl = *(volatile uint16_t *)0x0004;
}

/* 1行につき VRAM のアドレス設定を1回だけ行い、文字列の横方向はそのまま流し込む。
   文字ごとにアドレスを設定し直すと、6桁のスコアで 48回 も設定が要る (実測でここが
   処理時間の約9%を占めていた)。行を外側にすると 8回 で済む。 */
void text_draw(uint16_t x, uint16_t y, const char *s,
               uint8_t fg, uint8_t bg, uint8_t scale)
{
    uint8_t row, sy, j;
    for (row = 0; row < 8; row++) {
        for (sy = 0; sy < scale; sy++) {
            const char *p = s;
            vdp_setwrite((uint16_t)((((uint16_t)(y + row * scale + sy)) << 7) + (x >> 1)));
            while (*p) {
                const uint8_t *g = (const uint8_t *)(cgtbl + ((uint16_t)(uint8_t)*p << 3));
                uint8_t bits = g[row];
                if (scale == 2) {
                    for (j = 0; j < 8; j++) {
                        uint8_t c = (uint8_t)((bits & (0x80 >> j)) ? fg : bg);
                        vdp_wdata((uint8_t)(c | (c << 4)));
                    }
                } else {
                    for (j = 0; j < 4; j++) {
                        uint8_t a = (uint8_t)((bits & (0x80 >> (j << 1))) ? fg : bg);
                        uint8_t b = (uint8_t)((bits & (0x40 >> (j << 1))) ? fg : bg);
                        vdp_wdata((uint8_t)((a << 4) | b));
                    }
                }
                p++;
            }
        }
    }
}

uint8_t text_u8(char *dst, uint8_t v)
{
    uint8_t n = 0;
    if (v >= 100) {
        dst[n++] = (char)('0' + v / 100);
        v = (uint8_t)(v % 100);
        dst[n++] = (char)('0' + v / 10);
    } else if (v >= 10) {
        dst[n++] = (char)('0' + v / 10);
    }
    dst[n++] = (char)('0' + v % 10);
    dst[n] = 0;
    return n;
}
