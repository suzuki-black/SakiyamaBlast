/*
 * SakiyamaBlast - 先山＋発破 / 常磐炭鉱ダイナマイト堀り
 *
 * 現段階: ビルド基盤の縦切り。SCREEN 5 初期化 → 土で塗りつぶし → 自機の
 * スムーズ移動＋掘り (Dig-Dug 流に「薄い土の膜」が残る内側2px掘り) まで。
 * 仕様の正は DESIGN.md。
 */
#include "msx.h"
#include "vdp.h"
#include "sprite.h"
#include "game.h"

uint8_t dug[ROWS * COLS];
uint8_t dug_n;

static uint8_t px, py;        /* 自機のピクセル座標 */
static uint8_t cdir, ldir;    /* 現在の向き / 最後に向いた向き */

/* 仮スプライト: 先山 (ヘルメット姿の炭鉱夫シルエット)。テーマ用の本番絵は後で差し替える。 */
static const uint16_t pat_miner[16] = {
    0x0000,   /* ................ */
    0x03C0,   /* ......####...... */
    0x0FF0,   /* ....########.... */
    0x1FF8,   /* ...##########... */
    0x3FFC,   /* ..############.. */
    0x07E0,   /* .....######..... */
    0x0DB0,   /* ....##.##.##.... */
    0x0FF0,   /* ....########.... */
    0x1FF8,   /* ...##########... */
    0x3FFC,   /* ..############.. */
    0x3FFC,   /* ..############.. */
    0x1FF8,   /* ...##########... */
    0x1C38,   /* ...###....###... */
    0x381C,   /* ..###......###.. */
    0x300C,   /* ..##........##.. */
    0x0000    /* ................ */
};

static void wait_vsync(void)
{
    uint8_t t = JIFFY;
    while (JIFFY == t) {
        /* 垂直帰線待ち */
    }
}

/* セル (cx,cy) を掘り済みとして記録する。DESIGN の要: セルに完全整列した時だけ立てる。 */
static void dig_sweep(uint8_t cx, uint8_t cy)
{
    uint16_t idx;
    if (cx >= COLS || cy >= ROWS) {
        return;
    }
    idx = (uint16_t)cy * COLS + cx;
    if (dug[idx]) {
        return;
    }
    dug[idx] = 1;
    dug_n++;
}

/* 掘り跡: 自機幅より内側2pxだけ抜く。これで隣接トンネルの間に薄い土の膜が残る。 */
static void carve(void)
{
    vdp_fill((uint16_t)(px + 2), (uint8_t)(py + 2), 12, 12, COL_TUNNEL);
}

/* 入力を4方向へ落とす。上を最優先 (プロトの READINPUT と同じ順序)。 */
static uint8_t read_dir(void)
{
    uint8_t j = gtstck(0);
    uint8_t d;
    if (j == 0) {
        j = gtstck(1);
    }
    d = DIR_NONE;
    if (j == 2 || j == 3 || j == 4) { d = DIR_RIGHT; }
    if (j == 6 || j == 7 || j == 8) { d = DIR_LEFT; }
    if (j == 4 || j == 5 || j == 6) { d = DIR_DOWN; }
    if (j == 1 || j == 2 || j == 8) { d = DIR_UP; }
    return d;
}

/* 半歩中に直交方向を押したら、その軸を最寄りの16px節点へスナップして曲がる。 */
static void resolve_dir(uint8_t qd)
{
    if (qd == DIR_NONE) {
        cdir = DIR_NONE;
        return;
    }
    if (qd == DIR_UP || qd == DIR_DOWN) {
        if (px & 15) {
            /* +8 で 8bit を溢れさせないよう 16bit で丸めてから戻す */
            uint16_t nx = (uint16_t)(((uint16_t)px + 8) & 0xFFF0);
            if (nx > XMAX) { nx = XMAX; }
            px = (uint8_t)nx;
            carve();
            dig_sweep((uint8_t)(px >> 4), (uint8_t)(py >> 4));
        }
    } else {
        if (py & 15) {
            uint16_t ny = (uint16_t)(((uint16_t)py + 8) & 0xFFF0);
            if (ny > YMAX) { ny = YMAX; }
            py = (uint8_t)ny;
            carve();
            dig_sweep((uint8_t)(px >> 4), (uint8_t)(py >> 4));
        }
    }
    cdir = qd;
    ldir = qd;
}

static void step_player(void)
{
    if (cdir == DIR_NONE) {
        return;
    }
    switch (cdir) {
    case DIR_UP:
        if (py >= PSPD) { py -= PSPD; } else { py = 0; }
        break;
    case DIR_DOWN:
        py = (py < YMAX) ? (uint8_t)(py + PSPD) : (uint8_t)YMAX;
        break;
    case DIR_LEFT:
        if (px >= PSPD) { px -= PSPD; } else { px = 0; }
        break;
    default:
        px = (px < XMAX) ? (uint8_t)(px + PSPD) : (uint8_t)XMAX;
        break;
    }
    carve();
    /* DUG は「丸々掘り切った」時だけ立てる (掠めただけで岩の支えを失わせない)。 */
    if (((px & 15) == 0) && ((py & 15) == 0)) {
        dig_sweep((uint8_t)(px >> 4), (uint8_t)(py >> 4));
    }
}

static void build_stage(void)
{
    uint16_t i;
    for (i = 0; i < ROWS * COLS; i++) {
        dug[i] = 0;
    }
    dug_n = 0;

    vdp_fill(0, 0, 256, FIELD_H, COL_DIRT);
    vdp_fill(0, HUD_Y, 256, SCR_H - HUD_Y, COL_HUD);

    px = 8 * CELL;
    py = 9 * CELL;
    cdir = DIR_NONE;
    ldir = DIR_DOWN;
    carve();
    dig_sweep((uint8_t)(px >> 4), (uint8_t)(py >> 4));
}

void main(void)
{
    vdp_screen5();

    /* 坑内の色。土=炭層まじりの茶、トンネル=黒。 */
    vdp_set_palette(COL_TUNNEL, 0, 0, 0);
    vdp_set_palette(COL_DIRT,   4, 2, 1);
    vdp_set_palette(COL_HUD,    1, 1, 2);
    vdp_set_palette(COL_BLAST,  7, 6, 1);
    vdp_set_palette(COL_WHITE,  7, 7, 7);

    spr_init();
    spr_pattern_rows(0, pat_miner);
    spr_color(0, COL_WHITE);

    build_stage();

    for (;;) {
        wait_vsync();
        resolve_dir(read_dir());
        step_player();
        spr_set(0, px, (uint8_t)(py - 1), 0);
        oam_flush();
    }
}
