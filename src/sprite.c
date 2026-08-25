/* SakiyamaBlast - スプライト管理 */
#include "sprite.h"
#include "vdp.h"
#include "game.h"   /* SPR_ACTIVE (使う面の数) */

uint8_t oam[SPR_PLANES * 4];
uint8_t oam_len = SPR_PLANES * 4;

void spr_init(void)
{
    uint8_t i;
    for (i = 0; i < SPR_PLANES; i++) {
        oam[i * 4 + 0] = SPR_PARK_Y;
        oam[i * 4 + 1] = 0;
        oam[i * 4 + 2] = 0;
        oam[i * 4 + 3] = 0;
    }
    /* 色テーブルを一旦すべて 0 (透明) に */
    vdp_memset(VRAM_SPRCOL, 0, SPR_PLANES * 16);
    oam_len = SPR_PLANES * 4;      /* 初回だけ全32面を場外へ書き込む */
    oam_flush();
    oam_len = SPR_ACTIVE * 4;      /* 以後は使う面だけ */
}

void spr_pattern(uint8_t pat, const uint8_t *data)
{
    vdp_upload((uint16_t)(VRAM_SPRPAT + ((uint16_t)pat << 5)), data, 32);
}

void spr_pattern_rows(uint8_t pat, const uint16_t *rows)
{
    uint8_t i;
    vdp_setwrite((uint16_t)(VRAM_SPRPAT + ((uint16_t)pat << 5)));
    for (i = 0; i < 16; i++) {          /* 左半分 (行0-15) */
        vdp_wdata((uint8_t)(rows[i] >> 8));
    }
    for (i = 0; i < 16; i++) {          /* 右半分 (行0-15) */
        vdp_wdata((uint8_t)(rows[i] & 0xFF));
    }
}

void spr_color(uint8_t plane, uint8_t color)
{
    uint8_t i;
    vdp_setwrite((uint16_t)(VRAM_SPRCOL + ((uint16_t)plane << 4)));
    for (i = 0; i < 16; i++) {
        vdp_wdata(color);
    }
}

/* mode 2 のスプライトは1行に1色を持てる。16バイトの色表をそのまま焼く。 */
void spr_color_lines(uint8_t plane, const uint8_t *cols)
{
    uint8_t i;
    vdp_setwrite((uint16_t)(VRAM_SPRCOL + ((uint16_t)plane << 4)));
    for (i = 0; i < 16; i++) {
        vdp_wdata(cols[i]);
    }
}

void spr_set(uint8_t plane, uint8_t x, uint8_t y, uint8_t pat)
{
    uint8_t *p = &oam[plane * 4];
    p[0] = y;
    p[1] = x;
    p[2] = (uint8_t)(pat << 2);   /* 16x16 はパターン番号 x4 */
}

void spr_park(uint8_t plane)
{
    oam[plane * 4] = SPR_PARK_Y;
}

void spr_park_all(void)
{
    uint8_t i;
    for (i = 0; i < SPR_PLANES; i++) {
        oam[i * 4] = SPR_PARK_Y;
    }
}
