/* SakiyamaBlast - 盤面 (掘り地図・火薬・ステージ構築と描画) */
#include "game.h"
#include "vdp.h"

uint8_t  dug[NCELL];
uint8_t  powder[NCELL];
uint16_t dug_n;
uint8_t  dug_pct;
uint8_t  stage;
uint8_t  start_c, start_r;

uint8_t cell_dug(int8_t cx, int8_t cy)
{
    if (cx < 0 || cx >= COLS || cy < 0 || cy >= ROWS) {
        return 0;
    }
    return dug[(uint8_t)cy * COLS + (uint8_t)cx];
}

void dig_sweep(uint8_t cx, uint8_t cy)
{
    uint16_t i;
    if (cx >= COLS || cy >= ROWS) {
        return;
    }
    i = (uint16_t)cy * COLS + cx;
    if (dug[i]) {
        return;
    }
    dug[i] = 1;
    dug_n++;
    dug_pct = (uint8_t)((dug_n * 100u) / NCELL);
}

void draw_cell_dirt(int8_t cx, int8_t cy)
{
    if (cx < 0 || cx >= COLS || cy < 0 || cy >= ROWS) {
        return;
    }
    vdp_fill((uint16_t)((uint8_t)cx << 4), (uint16_t)((uint8_t)cy << 4), 16, 16, COL_DIRT);
}

/* 掘り済みセルを描く。隣が土の辺だけ内側2px にして、隣接する平行トンネルの間に
   薄い土の膜を残す (本作の肝)。隣が掘り済みの辺は端まで塗って通路を繋げる。 */
void draw_dug_cell(int8_t cx, int8_t cy)
{
    uint8_t x1, y1, x2, y2;
    if (!cell_dug(cx, cy)) {
        return;
    }
    x1 = (uint8_t)((uint8_t)cx << 4);
    y1 = (uint8_t)((uint8_t)cy << 4);
    x2 = (uint8_t)(x1 + PW);
    y2 = (uint8_t)(y1 + PW);
    if (!cell_dug((int8_t)(cx - 1), cy)) { x1 = (uint8_t)(x1 + 2); }
    if (!cell_dug((int8_t)(cx + 1), cy)) { x2 = (uint8_t)(x2 - 2); }
    if (!cell_dug(cx, (int8_t)(cy - 1))) { y1 = (uint8_t)(y1 + 2); }
    if (!cell_dug(cx, (int8_t)(cy + 1))) { y2 = (uint8_t)(y2 - 2); }
    vdp_fill(x1, y1, (uint16_t)(x2 - x1 + 1), (uint16_t)(y2 - y1 + 1), COL_TUNNEL);
}

/* 火薬 (旧チェリー相当)。拾うと得点でなくダイナマイトのストックが1増える。 */
void draw_powder(uint8_t cx, uint8_t cy)
{
    uint16_t x = (uint16_t)cx << 4;
    uint16_t y = (uint16_t)cy << 4;
    vdp_fill(x + 4, y + 6, 8, 8, COL_RED);      /* 樽 */
    vdp_fill(x + 4, y + 9, 8, 2, COL_FUSE);     /* たが */
    vdp_fill(x + 9, y + 2, 2, 4, COL_FUSE);     /* 導火線 */
}

static void carve_rect(uint8_t c1, uint8_t r1, uint8_t c2, uint8_t r2)
{
    uint8_t c, r;
    for (r = r1; r <= r2; r++) {
        for (c = c1; c <= c2; c++) {
            dug[(uint16_t)r * COLS + c] = 1;
        }
    }
}

/* 既掘の通路を敷きつつ、その上に火薬を並べる (Mr. Do! のチェリーの房と同じ配置思想)。 */
static void place_powder_line(uint8_t c, uint8_t r, uint8_t n)
{
    uint8_t i;
    uint16_t idx;
    for (i = 0; i < n; i++) {
        idx = (uint16_t)r * COLS + (uint8_t)(c + i);
        dug[idx] = 1;
        powder[idx] = 1;
    }
}

static void set_stage_layout(void)
{
    uint8_t sidx = (uint8_t)((stage - 1) % 3);

    start_c = 8;
    start_r = 9;
    carve_rect(8, 1, 8, 9);            /* 縦の背骨 */

    if (sidx == 0) {
        vdp_set_palette(COL_DIRT, 4, 2, 1);      /* 炭層まじりの茶 */
        place_powder_line(2, 3, 6);
        place_powder_line(9, 7, 6);
    } else if (sidx == 1) {
        vdp_set_palette(COL_DIRT, 3, 3, 2);      /* 硬い岩盤の灰 */
        place_powder_line(1, 2, 7);
        place_powder_line(9, 5, 6);
        place_powder_line(2, 8, 6);
    } else {
        vdp_set_palette(COL_DIRT, 2, 2, 4);      /* 湧水を含む青みの層 */
        place_powder_line(2, 2, 6);
        place_powder_line(9, 4, 6);
        place_powder_line(1, 6, 7);
        place_powder_line(9, 8, 6);
    }
}

void build_stage(void)
{
    uint16_t i;
    uint8_t c, r;

    for (i = 0; i < NCELL; i++) {
        dug[i] = 0;
        powder[i] = 0;
    }
    set_stage_layout();

    dug_n = 0;
    for (i = 0; i < NCELL; i++) {
        if (dug[i]) {
            dug_n++;
        }
    }
    dug_pct = (uint8_t)((dug_n * 100u) / NCELL);

    /* 全面を土で塗ってから既掘セルを彫り、最後に火薬を置く */
    vdp_fill(0, 0, 256, FIELD_H, COL_DIRT);
    for (r = 0; r < ROWS; r++) {
        for (c = 0; c < COLS; c++) {
            draw_dug_cell((int8_t)c, (int8_t)r);
            if (powder[(uint16_t)r * COLS + c]) {
                draw_powder(c, r);
            }
        }
    }
}
