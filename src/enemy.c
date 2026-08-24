/*
 * SakiyamaBlast - 敵 (坑内の小動物: ネズミ / コウモリ)
 *
 * 出現は Dig-Dug 式: 面開始時に既掘セルへ初期配置する (中央の巣からの湧きは無し)。
 * 移動は Mr. Do! 式。docs/MRDO_RESEARCH.md の通り:
 *  - 節点で通れる4方向のマスクを作る (FINDEXITS 相当)。
 *  - 通れるなら現在の向きを維持し、だめなら選び直す。さらに約1/8の確率で強制的に
 *    選び直す (WORKEXITS の RND2 & 7 == 0)。一本道での往復や完全な予測を防ぐ。
 *  - 選び直すときは自機に近づく向きを優先する。
 *  - 一定時間で掘り屋 (Driller Dino 相当) へ変異し、土を掘って自機へ向かう。
 *    変異中は一瞬止まり、掘っている間は自機と同じく減速する (EATWALL 相当)。
 */
#include "game.h"
#include "vdp.h"
#include "sprite.h"
#include "art.h"

uint8_t en_state[ENEMY_MAX];
uint8_t en_x[ENEMY_MAX], en_y[ENEMY_MAX];

static uint8_t  en_dir[ENEMY_MAX];
static uint8_t  en_digger[ENEMY_MAX];
static uint8_t  en_acc[ENEMY_MAX];
static uint8_t  en_chew[ENEMY_MAX];
static uint8_t  en_pause[ENEMY_MAX];
static uint16_t en_morph[ENEMY_MAX];
static uint8_t  en_col[ENEMY_MAX];      /* 色テーブルへ焼いた色 (変化時だけ書く) */
static uint8_t  en_quota;               /* この面で出す総数 */
static uint8_t  en_spawned;             /* これまでに出した数 */
static uint16_t spawn_timer;

/* 16bit LFSR。turbo R でも軽く、面ごとに種を振れば十分にばらける。 */
static uint16_t rnd_state = 0xACE1;

void rnd_seed(uint16_t s)
{
    rnd_state = s ? s : 0xACE1;
}

uint8_t rnd8(void)
{
    rnd_state ^= (uint16_t)(rnd_state << 7);
    rnd_state ^= (uint16_t)(rnd_state >> 9);
    rnd_state ^= (uint16_t)(rnd_state << 8);
    return (uint8_t)rnd_state;
}

void enemy_clear(void)
{
    uint8_t i;
    for (i = 0; i < ENEMY_MAX; i++) {
        en_state[i] = 0;
        en_col[i] = 0xFF;
        spr_park((uint8_t)(SPR_ENEMY + i));
    }
}

static uint16_t morph_frames(void)
{
    uint16_t t = MORPH_BASE;
    uint16_t d = (uint16_t)(stage - 1) * MORPH_STEP;
    if (d >= t || (uint16_t)(t - d) < MORPH_MIN) {
        return MORPH_MIN;
    }
    return (uint16_t)(t - d);
}

/* 面ごとの総出現数。面が進むほど増える。 */
static uint8_t quota_for_stage(void)
{
    uint16_t q = (uint16_t)(EN_QUOTA_BASE + (stage - 1));
    if (q > EN_QUOTA_MAX) {
        q = EN_QUOTA_MAX;
    }
    return (uint8_t)q;
}

static uint16_t spawn_interval(void)
{
    uint16_t t = SPAWN_BASE;
    uint16_t d = (uint16_t)(stage - 1) * SPAWN_STEP;
    if (d >= t || (uint16_t)(t - d) < SPAWN_MIN) {
        return SPAWN_MIN;
    }
    return (uint16_t)(t - d);
}

void enemy_stage_reset(void)
{
    enemy_clear();
    en_quota = quota_for_stage();
    en_spawned = 0;
    spawn_timer = spawn_interval();
}

/* 巣から1体出す。出せなければ何もしない。 */
static void enemy_spawn(void)
{
    uint8_t i;
    for (i = 0; i < ENEMY_MAX; i++) {
        if (en_state[i]) {
            continue;
        }
        en_x[i] = (uint8_t)(nest_c << 4);
        en_y[i] = (uint8_t)(nest_r << 4);
        en_state[i] = 1;
        en_dir[i] = (uint8_t)(DIR_UP + (rnd8() & 3));   /* 出た向きは適当に散らす */
        en_digger[i] = 0;
        en_acc[i] = 0;
        en_chew[i] = 0;
        en_pause[i] = 0;
        en_morph[i] = morph_frames();
        en_spawned++;
        return;
    }
}

/* 湧きの制御: 決められた総数まで、一定時間ごと、または全滅した瞬間に次を出す。 */
static void spawn_tick(void)
{
    uint8_t i, alive = 0;
    if (en_spawned >= en_quota) {
        return;                         /* 出し切ったらもう出ない */
    }
    for (i = 0; i < ENEMY_MAX; i++) {
        if (en_state[i]) {
            alive++;
        }
    }
    if (alive == 0 && en_spawned) {
        /* 画面から敵が消えた瞬間に次を出す。ただし面の1体目はタイマで出す
           (開始と同時に湧くと、自機が動く前に捕まってしまう)。 */
        enemy_spawn();
        spawn_timer = spawn_interval();
        return;
    }
    if (alive >= ENEMY_MAX) {
        return;
    }
    spawn_timer--;
    if (spawn_timer == 0) {
        enemy_spawn();
        spawn_timer = spawn_interval();
    }
}

uint8_t enemy_kill_at(int8_t cx, int8_t cy)
{
    uint8_t i, killed = 0;
    for (i = 0; i < ENEMY_MAX; i++) {
        if (en_state[i]) {
            if ((int8_t)((en_x[i] + 8) >> 4) == cx && (int8_t)((en_y[i] + 8) >> 4) == cy) {
                en_state[i] = 0;
                spr_park((uint8_t)(SPR_ENEMY + i));
                killed++;
            }
        }
    }
    return killed;
}

static int8_t dir_dc(uint8_t d) { return (int8_t)((d == DIR_LEFT) ? -1 : (d == DIR_RIGHT) ? 1 : 0); }
static int8_t dir_dr(uint8_t d) { return (int8_t)((d == DIR_UP) ? -1 : (d == DIR_DOWN) ? 1 : 0); }

static uint8_t dir_rev(uint8_t d)
{
    switch (d) {
    case DIR_UP:    return DIR_DOWN;
    case DIR_DOWN:  return DIR_UP;
    case DIR_LEFT:  return DIR_RIGHT;
    default:        return DIR_LEFT;
    }
}

/* その向きへ進めるか。掘り屋は土も通れる。岩は横向きなら押せる時だけ通れる。 */
static uint8_t can_go(uint8_t i, uint8_t d)
{
    int8_t cx = (int8_t)((en_x[i] >> 4) + dir_dc(d));
    int8_t cy = (int8_t)((en_y[i] >> 4) + dir_dr(d));
    int8_t ri;
    if (cx < 0 || cx >= COLS || cy < 0 || cy >= ROWS) {
        return 0;
    }
    if (cell_diamond(cx, cy)) {
        return 0;                        /* ダイヤは掘り屋でも抜けられない */
    }
    ri = rock_at(cx, cy);
    if (ri >= 0) {
        if (d == DIR_LEFT)  { return rock_can_push(ri, -1); }
        if (d == DIR_RIGHT) { return rock_can_push(ri, 1); }
        return 0;                        /* 縦には押せない = 岩を避ける */
    }
    if (en_digger[i]) {
        return 1;
    }
    return cell_dug(cx, cy);
}

/* 自機に近づく向きを優先度順に並べる (プロトの CHASEDIR と同じ考え方)。 */
static void chase_order(uint8_t i, uint8_t *out)
{
    int8_t dx = (int8_t)(((px + 8) >> 4) - (en_x[i] >> 4));
    int8_t dy = (int8_t)(((py + 8) >> 4) - (en_y[i] >> 4));
    uint8_t h = (uint8_t)((dx > 0) ? DIR_RIGHT : DIR_LEFT);
    uint8_t v = (uint8_t)((dy > 0) ? DIR_DOWN : DIR_UP);
    int8_t ax = (int8_t)((dx < 0) ? -dx : dx);
    int8_t ay = (int8_t)((dy < 0) ? -dy : dy);
    if (ax >= ay) {
        out[0] = h; out[1] = v; out[2] = dir_rev(v); out[3] = dir_rev(h);
    } else {
        out[0] = v; out[1] = h; out[2] = dir_rev(h); out[3] = dir_rev(v);
    }
}

static void decide_dir(uint8_t i)
{
    uint8_t order[4];
    uint8_t rev = dir_rev(en_dir[i]);
    uint8_t k;

    /* 通れるなら現在の向きを維持。ただし約1/8の確率で強制的に選び直す。 */
    if (can_go(i, en_dir[i]) && (rnd8() & 7)) {
        return;
    }
    chase_order(i, order);
    /* まず引き返さない方向から、自機に近づく順に選ぶ */
    for (k = 0; k < 4; k++) {
        if (order[k] != rev && can_go(i, order[k])) {
            en_dir[i] = order[k];
            return;
        }
    }
    /* 行き止まり: 引き返す */
    if (can_go(i, rev)) {
        en_dir[i] = rev;
    }
}

static void enemy_carve(uint8_t i)
{
    vdp_fill((uint16_t)(en_x[i] + 2), (uint16_t)(en_y[i] + 2), 12, 12, COL_TUNNEL);
}

static void enemy_step(uint8_t i)
{
    uint8_t at_node = (uint8_t)(((en_x[i] & 15) == 0) && ((en_y[i] & 15) == 0));
    int8_t cx, cy, ri;

    if (at_node) {
        decide_dir(i);
        cx = (int8_t)((en_x[i] >> 4) + dir_dc(en_dir[i]));
        cy = (int8_t)((en_y[i] >> 4) + dir_dr(en_dir[i]));
        if (cx < 0 || cx >= COLS || cy < 0 || cy >= ROWS) {
            return;
        }
        ri = rock_at(cx, cy);
        if (ri >= 0) {
            /* 岩を横へ押す。押せなければこのフレームは動かない。 */
            if (en_dir[i] == DIR_LEFT) {
                if (!rock_push(ri, -1)) { return; }
            } else if (en_dir[i] == DIR_RIGHT) {
                if (!rock_push(ri, 1)) { return; }
            } else {
                return;
            }
        }
        en_chew[i] = (uint8_t)(cell_dug(cx, cy) ? 0 : 1);
    }

    en_acc[i] = (uint8_t)(en_acc[i] + (en_chew[i] ? SPD_ENEMY_DIG : SPD_ENEMY));
    if (en_acc[i] < SPD_UNIT) {
        return;
    }
    en_acc[i] = (uint8_t)(en_acc[i] - SPD_UNIT);

    switch (en_dir[i]) {
    case DIR_UP:    if (en_y[i]) { en_y[i]--; } break;
    case DIR_DOWN:  if (en_y[i] < (uint8_t)YMAX) { en_y[i]++; } break;
    case DIR_LEFT:  if (en_x[i]) { en_x[i]--; } break;
    default:        if (en_x[i] < (uint8_t)XMAX) { en_x[i]++; } break;
    }

    if (en_chew[i]) {
        enemy_carve(i);                  /* 掘り屋が掘った跡も本物の坑道になる */
        if (((en_x[i] & 15) == 0) && ((en_y[i] & 15) == 0)) {
            dig_sweep((uint8_t)(en_x[i] >> 4), (uint8_t)(en_y[i] >> 4));
        }
    }
}

static uint8_t touches_player(uint8_t i)
{
    int16_t dx = (int16_t)en_x[i] - (int16_t)px;
    int16_t dy = (int16_t)en_y[i] - (int16_t)py;
    if (dx < 0) { dx = (int16_t)(-dx); }
    if (dy < 0) { dy = (int16_t)(-dy); }
    return (uint8_t)((dx < 12) && (dy < 12));
}

void enemy_update(void)
{
    uint8_t i;
    spawn_tick();
    for (i = 0; i < ENEMY_MAX; i++) {
        if (!en_state[i]) {
            continue;
        }
        if (en_pause[i]) {
            en_pause[i]--;               /* 変異中は一瞬止まる */
        } else {
            if (!en_digger[i]) {
                en_morph[i]--;
                if (en_morph[i] == 0) {
                    en_digger[i] = 1;
                    en_pause[i] = MORPH_PAUSE;
                }
            }
            enemy_step(i);
        }
        if (!invuln && !dead && touches_player(i)) {
            dead = 1;
        }
    }
}

void enemy_draw(void)
{
    uint8_t i, col;
    for (i = 0; i < ENEMY_MAX; i++) {
        if (!en_state[i]) {
            spr_park((uint8_t)(SPR_ENEMY + i));
            continue;
        }
        col = (uint8_t)(en_digger[i] ? PAT_BAT : PAT_RAT);
        if (col != en_col[i]) {
            en_col[i] = col;
            spr_color_lines((uint8_t)(SPR_ENEMY + i), art_colours(col));
        }
        spr_set((uint8_t)(SPR_ENEMY + i), en_x[i], (uint8_t)(en_y[i] - 1),
                (uint8_t)(en_digger[i] ? PAT_BAT : PAT_RAT));
    }
}
