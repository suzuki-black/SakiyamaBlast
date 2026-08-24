/* SakiyamaBlast - 盤面 (掘り地図・火薬・ステージ構築と描画) */
#include "game.h"
#include "vdp.h"

uint8_t  dug[NCELL];
uint8_t  powder[NCELL];
uint16_t dug_n;
uint8_t  dug_pct;
uint8_t  stage;
uint8_t  start_c, start_r;
uint8_t  nest_c, nest_r;
uint8_t  bath_c, bath_r;
uint8_t  bath_on;

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

/* 風呂 (温泉)。触れると体温が一気に下がる。 */
void draw_bath(uint8_t cx, uint8_t cy)
{
    uint16_t x = (uint16_t)cx << 4;
    uint16_t y = (uint16_t)cy << 4;
    vdp_fill(x + 2, y + 8, 12, 6, COL_BATH);     /* 湯 */
    vdp_fill(x + 1, y + 6, 14, 2, COL_WHITE);    /* 縁 */
    vdp_fill(x + 4, y + 2, 2, 3, COL_WHITE);     /* 湯気 */
    vdp_fill(x + 9, y + 1, 2, 4, COL_WHITE);
}

void bath_destroy_at(int8_t cx, int8_t cy)
{
    if (!bath_on) {
        return;
    }
    if ((int8_t)bath_c != cx || (int8_t)bath_r != cy) {
        return;
    }
    bath_on = 0;
    draw_cell_dirt(cx, cy);
    draw_dug_cell(cx, cy);
}

/* セル1枚を「土 → 坑道 → 火薬 → 風呂」の順で描き直す。文字を消すときや
   爆風の跡を戻すときに使う。 */
void field_redraw_cell(int8_t cx, int8_t cy)
{
    if (cx < 0 || cx >= COLS || cy < 0 || cy >= ROWS) {
        return;
    }
    draw_cell_dirt(cx, cy);
    draw_dug_cell(cx, cy);
    if (powder[(uint16_t)(uint8_t)cy * COLS + (uint8_t)cx]) {
        draw_powder((uint8_t)cx, (uint8_t)cy);
    }
    if (bath_on && (uint8_t)cx == bath_c && (uint8_t)cy == bath_r) {
        draw_bath((uint8_t)cx, (uint8_t)cy);
    }
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
    nest_c = 8;                        /* 敵の巣 = 画面のほぼ中央 */
    nest_r = 5;
    carve_rect(8, 1, 8, 9);            /* 縦の背骨。巣と自機の出現位置を含む */

    /* 岩は土の中 (真下も土) に置く。真下を掘り抜くと揺れて落ちてくる。 */
    /* 風呂は自機の出現位置の近くに、面ごとに違う場所へ置く */
    bath_c = 8;
    bath_r = (uint8_t)(8 - sidx);
    bath_on = 1;

    if (sidx == 0) {
        vdp_set_palette(COL_DIRT, 4, 2, 1);      /* 炭層まじりの茶 */
        place_powder_line(2, 3, 6);
        place_powder_line(9, 7, 6);
        rock_place(4, 5);
        rock_place(12, 4);
    } else if (sidx == 1) {
        vdp_set_palette(COL_DIRT, 3, 3, 2);      /* 硬い岩盤の灰 */
        place_powder_line(1, 2, 7);
        place_powder_line(9, 5, 6);
        place_powder_line(2, 8, 6);
        rock_place(4, 4);
        rock_place(13, 2);
        rock_place(11, 7);
    } else {
        vdp_set_palette(COL_DIRT, 2, 2, 4);      /* 湧水を含む青みの層 */
        place_powder_line(2, 2, 6);
        place_powder_line(9, 4, 6);
        place_powder_line(1, 6, 7);
        place_powder_line(9, 8, 6);
        rock_place(4, 3);
        rock_place(12, 2);
        rock_place(4, 7);
        rock_place(12, 6);
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
    rock_clear();
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
    if (bath_on) {
        draw_bath(bath_c, bath_r);
    }
}
