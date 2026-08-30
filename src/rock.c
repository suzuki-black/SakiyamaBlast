/*
 * SakiyamaBlast - 岩 (Mr. Do! の apple 準拠)
 *
 * 状態機械は APPLEWAIT / APPLEJIG / APPLEFALL / APPLESPLIT の4状態に対応する。
 * 根拠と出典は docs/MRDO_RESEARCH.md。要点:
 *  - 支え判定は「真下のセル(地形)」に加えて **自機そのもの**も見る (Dig Dug 準拠)。
 *    真下の升目に自機が居る間、岩は揺れも落下もしない。好きなだけ待てる。
 *    出典: "The rock will only start to fall once Dig Dug moves out from under it.
 *    Until such time, he can wait underneath the rock for as long as he likes."
 *  - 自機が真下から離れた瞬間に揺れ始め、揺れ切ると落下する。
 *  - 押しは横方向のみ。押し先が開いている(掘り済み)セルの時だけ。土へは押し込めない。
 *    **押し込んだ先に居る敵は潰れる**（落とさなくても岩を武器にできる）。押した岩は割れない。
 *  - 自身の高さ超 (2セル以上) 落ちるか、何かを潰すと割れて消える。1セルなら割れず存続。
 */
#include "game.h"
#include "sprite.h"

uint8_t rock_state[ROCK_MAX];
uint8_t rock_x[ROCK_MAX], rock_y[ROCK_MAX];

static uint8_t jig_t[ROCK_MAX];     /* 揺れの残りフレーム */
static uint8_t fall_r0[ROCK_MAX];   /* 落下を開始した行 (落下距離で割れ判定) */
static uint8_t crushed[ROCK_MAX];   /* 落下中に何かを潰したか */
static uint8_t sink[ROCK_MAX];      /* 落盤で沈み込む途中。支えの有無を無視して1升落ちる */

void rock_clear(void)
{
    uint8_t i;
    for (i = 0; i < ROCK_MAX; i++) {
        rock_state[i] = ROCK_NONE;
        spr_park((uint8_t)(SPR_ROCK + i));
    }
}

void rock_place(uint8_t c, uint8_t r)
{
    uint8_t i;
    for (i = 0; i < ROCK_MAX; i++) {
        if (rock_state[i] == ROCK_NONE) {
            rock_x[i] = (uint8_t)(c << 4);
            rock_y[i] = (uint8_t)(r << 4);
            rock_state[i] = ROCK_WAIT;
            jig_t[i] = 0;
            crushed[i] = 0;
            sink[i] = 0;
            return;
        }
    }
}

int8_t rock_at(int8_t cx, int8_t cy)
{
    uint8_t i;
    if (cx < 0 || cx >= COLS || cy < 0 || cy >= ROWS) {
        return -1;
    }
    for (i = 0; i < ROCK_MAX; i++) {
        if (rock_state[i] != ROCK_NONE) {
            if ((int8_t)(rock_x[i] >> 4) == cx && (int8_t)(rock_y[i] >> 4) == cy) {
                return (int8_t)i;
            }
        }
    }
    return -1;
}

/* 落下中の岩は支えにならない。静止している岩だけを支えとして数える。 */
static uint8_t rock_solid_at(int8_t cx, int8_t cy)
{
    int8_t i = rock_at(cx, cy);
    if (i < 0) {
        return 0;
    }
    return (uint8_t)(rock_state[i] != ROCK_FALL);
}

/* 真下が支えられているか。地形(未掘の土)・盤面の底・静止した岩に加えて、
   **真下の升目に自機が居る場合も支え**とする (Dig Dug 準拠)。 */
static uint8_t supported(uint8_t i)
{
    int8_t c = (int8_t)(rock_x[i] >> 4);
    int8_t r = (int8_t)(rock_y[i] >> 4);
    if (r + 1 >= ROWS) {
        return 1;
    }
    if (!cell_dug(c, (int8_t)(r + 1))) {
        return 1;
    }
    if ((int8_t)player_cell_c() == c && (int8_t)player_cell_r() == (int8_t)(r + 1)) {
        return 1;                       /* 自機が真下で支えている */
    }
    return rock_solid_at(c, (int8_t)(r + 1));
}

uint8_t rock_can_push(int8_t i, int8_t dx)
{
    int8_t c, r, nc;
    if (i < 0 || rock_state[i] == ROCK_NONE || rock_state[i] == ROCK_FALL) {
        return 0;
    }
    c = (int8_t)(rock_x[i] >> 4);
    r = (int8_t)(rock_y[i] >> 4);
    nc = (int8_t)(c + dx);
    if (nc < 0 || nc >= COLS) {
        return 0;                       /* 場外へは押せない */
    }
    if (!cell_dug(nc, r)) {
        return 0;                       /* 土へは押し込めない (Mr. Do! 準拠) */
    }
    if (rock_at(nc, r) >= 0) {
        return 0;                       /* 別の岩があれば押せない */
    }
    return 1;
}

uint8_t rock_push(int8_t i, int8_t dx)
{
    int8_t c, r, nc;
    if (!rock_can_push(i, dx)) {
        return 0;
    }
    c = (int8_t)(rock_x[i] >> 4);
    r = (int8_t)(rock_y[i] >> 4);
    nc = (int8_t)(c + dx);
    rock_x[i] = (uint8_t)((uint8_t)nc << 4);
    rock_state[i] = ROCK_WAIT;          /* 支えの有無は次の更新で見る */
    jig_t[i] = 0;
    /* 横へ押し込んだ先に居るものは潰れる。落とさなくても岩を武器にできるので、
       ダイナマイトを温存できる。押した岩は割れずに残る (何度でも押して使える)。 */
    {
        uint8_t killed = enemy_kill_at(nc, r, DROP_BY_ROCK);
        if (killed) {
            score_kills(killed, 1);
        }
    }
    if (!invuln && !dead
        && (int8_t)player_cell_c() == nc && (int8_t)player_cell_r() == r) {
        dead = 1;                       /* 敵が押した岩に潰されることもある */
    }
    return 1;
}

static void rock_break(uint8_t i)
{
    rock_state[i] = ROCK_NONE;
    spr_park((uint8_t)(SPR_ROCK + i));
    bonus_rock_broken();               /* 2個割るとボーナスが出る */
}

void rock_destroy_at(int8_t cx, int8_t cy)
{
    int8_t i = rock_at(cx, cy);
    if (i >= 0) {
        rock_break((uint8_t)i);
    }
}

/* 落下中の岩が自機に重なっているか (16px 同士のおおよその重なり) */
static uint8_t hits_player(uint8_t i)
{
    int16_t dx = (int16_t)rock_x[i] - (int16_t)px;
    int16_t dy = (int16_t)rock_y[i] - (int16_t)py;
    if (dx < 0) { dx = (int16_t)(-dx); }
    if (dy < 0) { dy = (int16_t)(-dy); }
    return (uint8_t)((dx < 13) && (dy < 13));
}

static void rock_fall_step(uint8_t i)
{
    int8_t c, r;

    rock_y[i] = (uint8_t)(rock_y[i] + ROCK_FALL_SPD);
    if (rock_y[i] > (uint8_t)YMAX) {
        rock_y[i] = (uint8_t)YMAX;
    }

    if (!invuln && !dead && hits_player(i)) {
        dead = 1;
        crushed[i] = 1;
    }
    /* 落下中の岩は縦一列の敵をまとめて潰す (Mr. Do! 準拠) */
    {
        uint8_t killed = enemy_kill_at((int8_t)(rock_x[i] >> 4),
                                       (int8_t)((rock_y[i] + 8) >> 4), DROP_BY_ROCK);
        if (killed) {
            crushed[i] = 1;
            score_kills(killed, 1);      /* 落下岩は爆風の倍 */
        }
    }

    if (rock_y[i] & 15) {
        return;                         /* セル境界でだけ着地判定 */
    }
    c = (int8_t)(rock_x[i] >> 4);
    r = (int8_t)(rock_y[i] >> 4);
    bath_destroy_at(c, r);              /* 落ちてきた岩は風呂を壊す */
    if (r + 1 < ROWS && cell_dug(c, (int8_t)(r + 1)) && !rock_solid_at(c, (int8_t)(r + 1))) {
        return;                         /* まだ落ちる */
    }
    /* 着地。自身の高さ超 (2セル以上) 落ちたか、何かを潰したなら割れて消える。 */
    if (crushed[i] || (uint8_t)(r - fall_r0[i]) >= 2) {
        rock_break(i);
    } else {
        rock_state[i] = ROCK_WAIT;
        jig_t[i] = 0;
        sink[i] = 0;                    /* 沈み込みはここまで */
        /* 岩がその升に居座る場合だけ、下敷きの割れた黒ダイヤを砕き出しておく。
           岩の升には自機が入れないので、放っておくと回収できずクリア不能になる。
           炭柱 (岩の真下の黒ダイヤ) を割ると必ずこの形になる。 */
        coal_crush_at(c, r);
    }
}

/*
 * 炭柱が壊れたときの落盤。残柱式で天盤を支えていた柱が無くなるので、
 * 盤面の**すべての岩**が揺れてから1升沈む。
 *  - 岩は地中に埋まっているものなので、真下が土でも沈む。抜けた升は土のまま
 *    (dug[] を触らない)。1升だけなら割れない
 *  - 沈んだ先の下が掘れていれば、そこからは通常の岩と同じ。落ちるところまで
 *    落ちて割れる (落下距離は元の位置から数えるので、1升沈む + 1升落ちるで割れる)
 *  - 一番下の行に居る岩は落ちる先が無いので消える
 */
void rock_collapse(void)
{
    uint8_t i;
    for (i = 0; i < ROCK_MAX; i++) {
        if (rock_state[i] == ROCK_NONE || rock_state[i] == ROCK_FALL) {
            continue;
        }
        if ((uint8_t)(rock_y[i] >> 4) + 1 >= ROWS) {
            rock_break(i);                  /* 一番下。落ちる先が無いので消える */
            continue;
        }
        sink[i] = 1;
        rock_state[i] = ROCK_JIG;
        jig_t[i] = ROCK_JIG_FRAMES;
    }
}

void rock_update(void)
{
    uint8_t i;
    for (i = 0; i < ROCK_MAX; i++) {
        if (rock_state[i] == ROCK_NONE) {
            continue;
        }
        if (rock_state[i] == ROCK_FALL) {
            rock_fall_step(i);
            continue;
        }
        if (supported(i) && !sink[i]) {
            rock_state[i] = ROCK_WAIT;
            jig_t[i] = 0;
            continue;
        }
        if (rock_state[i] != ROCK_JIG) {
            rock_state[i] = ROCK_JIG;
            jig_t[i] = ROCK_JIG_FRAMES;
        } else {
            jig_t[i]--;
            if (jig_t[i] == 0) {
                rock_state[i] = ROCK_FALL;
                fall_r0[i] = (uint8_t)(rock_y[i] >> 4);
                crushed[i] = 0;
            }
        }
    }
}

void rock_draw(void)
{
    uint8_t i;
    for (i = 0; i < ROCK_MAX; i++) {
        if (rock_state[i] == ROCK_NONE) {
            spr_park((uint8_t)(SPR_ROCK + i));
        } else {
            /* 揺れている間は1pxぶらす (落ちる前触れ)。列0で 0-1=255 へ
               回り込まないよう、引かずに足す向きだけにしておく。 */
            uint8_t x = rock_x[i];
            if (rock_state[i] == ROCK_JIG && (jig_t[i] & 2)) {
                x = (uint8_t)(x + 1);
            }
            spr_set((uint8_t)(SPR_ROCK + i), x, (uint8_t)(rock_y[i] - 1), PAT_ROCK);
        }
    }
}
