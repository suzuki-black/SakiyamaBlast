/* SakiyamaBlast - 自機 (先山) の移動と掘り */
#include "game.h"
#include "vdp.h"
#include "sprite.h"

uint8_t px, py;
uint8_t lives;
uint8_t dead;
uint8_t invuln;

static uint8_t cdir;      /* 現在の向き */
static uint8_t chewing;   /* 未掘の土を食い進んでいるか (減速する) */
static uint8_t acc;       /* 速度の分子 */

uint8_t player_cell_c(void) { return (uint8_t)((px + 8) >> 4); }
uint8_t player_cell_r(void) { return (uint8_t)((py + 8) >> 4); }

/* 掘り跡は自機幅より内側2px。セル幅いっぱいに抜くと膜が消えるので必ず内側で。 */
void player_carve(void)
{
    vdp_fill((uint16_t)(px + 2), (uint16_t)(py + 2), 12, 12, COL_TUNNEL);
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

/* 火薬を拾う。節点に乗った時だけ判定する。 */
static void collect(void)
{
    uint16_t i;
    uint8_t c, r;
    if ((px & 15) || (py & 15)) {
        return;
    }
    c = (uint8_t)(px >> 4);
    r = (uint8_t)(py >> 4);
    i = (uint16_t)r * COLS + c;
    if (powder[i]) {
        powder[i] = 0;
        draw_dug_cell((int8_t)c, (int8_t)r);   /* 火薬の絵を消して坑道へ戻す */
        player_carve();
        stock++;
    }
}

/* 進行先セルが未掘なら食い進み中 = 減速。節点でだけ判定を更新する。 */
static void update_chewing(void)
{
    int8_t cx = (int8_t)(px >> 4);
    int8_t cy = (int8_t)(py >> 4);
    switch (cdir) {
    case DIR_UP:    cy--; break;
    case DIR_DOWN:  cy++; break;
    case DIR_LEFT:  cx--; break;
    case DIR_RIGHT: cx++; break;
    default:
        chewing = 0;
        return;
    }
    if (cx < 0 || cx >= COLS || cy < 0 || cy >= ROWS) {
        chewing = 0;          /* 場外へは進めないので減速させない */
        return;
    }
    chewing = cell_dug(cx, cy) ? 0 : 1;
}

/* 進行先セルに岩があるか。横向きなら押しを試み、押せなければ止まる。
   縦向きには押せない (Mr. Do! 準拠)。 */
static uint8_t rock_blocks(void)
{
    int8_t cx = (int8_t)(px >> 4);
    int8_t cy = (int8_t)(py >> 4);
    int8_t i;
    switch (cdir) {
    case DIR_UP:    cy--; break;
    case DIR_DOWN:  cy++; break;
    case DIR_LEFT:  cx--; break;
    default:        cx++; break;
    }
    i = rock_at(cx, cy);
    if (i < 0) {
        return 0;
    }
    if (cdir == DIR_LEFT) {
        return (uint8_t)(rock_push(i, -1) ? 0 : 1);
    }
    if (cdir == DIR_RIGHT) {
        return (uint8_t)(rock_push(i, 1) ? 0 : 1);
    }
    return 1;
}

void player_start(void)
{
    px = (uint8_t)(start_c << 4);
    py = (uint8_t)(start_r << 4);
    cdir = DIR_NONE;
    chewing = 0;
    acc = 0;
    dead = 0;
    invuln = INVULN_FRAMES;
    dig_sweep(start_c, start_r);
    draw_dug_cell((int8_t)start_c, (int8_t)start_r);
    player_carve();
}

void player_respawn(void)
{
    dead = 0;
    if (lives) {
        lives--;
    }
    bomb_clear();
    player_start();
}

void player_update(void)
{
    uint8_t q;
    uint8_t at_node;

    if (invuln) {
        invuln--;
    }
    collect();

    q = read_dir();
    /* Mr. Do! の曲がり角ゲート: 方向転換は直交軸がセルに整列している時だけ。
       整列するまでは現在の向きで進み続ける (スナップ=ワープはしない)。 */
    if (q == DIR_NONE) {
        cdir = DIR_NONE;
    } else if (q == DIR_UP || q == DIR_DOWN) {
        if ((px & 15) == 0) { cdir = q; }
    } else {
        if ((py & 15) == 0) { cdir = q; }
    }
    if (cdir == DIR_NONE) {
        return;
    }

    at_node = (uint8_t)(((px & 15) == 0) && ((py & 15) == 0));
    if (at_node) {
        if (rock_blocks()) {
            return;
        }
        update_chewing();
    }

    acc = (uint8_t)(acc + (chewing ? SPD_DIG : SPD_TUNNEL));
    if (acc < SPD_UNIT) {
        return;                     /* 掘削中はこのフレーム進まない */
    }
    acc = (uint8_t)(acc - SPD_UNIT);

    switch (cdir) {
    case DIR_UP:
        if (py) { py--; }
        break;
    case DIR_DOWN:
        if (py < YMAX) { py++; }
        break;
    case DIR_LEFT:
        if (px) { px--; }
        break;
    default:
        if (px < XMAX) { px++; }
        break;
    }

    player_carve();
    /* DUG は「丸々掘り切った」時だけ立てる (掠めただけでは掘れた扱いにしない)。 */
    if (((px & 15) == 0) && ((py & 15) == 0)) {
        dig_sweep((uint8_t)(px >> 4), (uint8_t)(py >> 4));
    }
}
