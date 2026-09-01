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
#include "sound.h"
#include "art.h"

uint8_t rock_state[ROCK_MAX];
uint8_t rock_x[ROCK_MAX], rock_y[ROCK_MAX];

static uint8_t jig_t[ROCK_MAX];     /* 揺れの残りフレーム */
static uint8_t fall_r0[ROCK_MAX];   /* 落下を開始した行 (落下距離で割れ判定) */
static uint8_t crushed[ROCK_MAX];   /* 落下中に何かを潰したか */
/* 1回の落下で潰した敵の数。**升ごとに1体ずつ計上すると、縦一列をまとめて
   潰しても1体ずつ倒したのと同じ点数にしかならない。** 落ち切ってから
   まとめて score_kills() に渡すことで、倍々の表がやっと効く。 */
static uint8_t kills[ROCK_MAX];
static uint8_t kill_c[ROCK_MAX], kill_r[ROCK_MAX];   /* 最初に潰した升 (得点を出す場所) */
/* 落盤で沈み込む先の行。0xFF = 沈み込み中でない。
   ここへ着くまでは土でも突き抜けて落ち、着いた時点で落下距離を数え直す。 */
static uint8_t sink_to[ROCK_MAX];
static uint8_t split_t[ROCK_MAX];   /* 割れた姿を出しておく残りフレーム */

void rock_clear(void)
{
    uint8_t i;
    for (i = 0; i < ROCK_MAX; i++) {
        rock_state[i] = ROCK_NONE;
        sink_to[i] = 0xFF;
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
            sink_to[i] = 0xFF;
            spr_color_lines((uint8_t)(SPR_ROCK + i), art_colours(PAT_ROCK));
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
        if (rock_state[i] != ROCK_NONE && rock_state[i] != ROCK_SPLIT) {
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

/* 割れる。**すぐには消さない。** 即座に消えると何が起きたのか読み取れないので、
   軽く画面を揺らし、二つに割れた姿を ROCK_SPLIT_FRAMES だけ見せてから消す。
   その間は支えにも障害物にもならない (rock_at が拾わない)。 */
/* 1回の落下で潰した数をまとめて計上する。まとめて潰すほど倍々になる。 */
static void cash_kills(uint8_t i)
{
    if (!kills[i]) {
        return;
    }
    score_kills(kills[i], 1);           /* 落下岩は爆風の倍 */
    score_popup(kill_c[i], kill_r[i], kill_value(kills[i], 1));
    kills[i] = 0;
}

static void rock_break(uint8_t i)
{
    cash_kills(i);
    rock_state[i] = ROCK_SPLIT;
    split_t[i] = ROCK_SPLIT_FRAMES;
    sink_to[i] = 0xFF;
    field_shake(ROCK_LAND_SHAKE);
    /* 割れた絵は岩と行ごとの色が違うので、色表も差し替える。 */
    spr_color_lines((uint8_t)(SPR_ROCK + i), art_colours(PAT_SPLIT));
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
            if (!kills[i]) {                     /* 得点は最初に潰した升へ出す */
                kill_c[i] = (uint8_t)(rock_x[i] >> 4);
                kill_r[i] = (uint8_t)((rock_y[i] + 8) >> 4);
            }
            crushed[i] = 1;
            kills[i] = (uint8_t)(kills[i] + killed);
        }
    }

    if (rock_y[i] & 15) {
        return;                         /* セル境界でだけ着地判定 */
    }
    c = (int8_t)(rock_x[i] >> 4);
    r = (int8_t)(rock_y[i] >> 4);
    link_smash((uint8_t)c, (uint8_t)r); /* 通り抜けた升の間の膜を割る */
    bath_destroy_at(c, r);              /* 落ちてきた岩は風呂を壊す */
    /* 沈み込み中は土でも突き抜けて落ちる。沈み終わった時点で落下距離を数え直すので、
       **沈んだぶんは割れ判定に入らない** (炭柱に書いてある数だけ落ちても割れない)。 */
    if (sink_to[i] != 0xFF) {
        if ((uint8_t)r < sink_to[i]) {
            if (r + 1 >= ROWS) {
                sink_to[i] = 0xFF;
                rock_break(i);          /* 盤外へ抜けた */
            }
            return;
        }
        sink_to[i] = 0xFF;
        fall_r0[i] = (uint8_t)r;        /* ここから先だけを落下距離として数える */
    }
    if (r + 1 < ROWS && cell_dug(c, (int8_t)(r + 1)) && !rock_solid_at(c, (int8_t)(r + 1))) {
        return;                         /* まだ落ちる */
    }
    /* 着地。自身の高さ超 (2セル以上) 落ちたか、何かを潰したなら割れて消える。 */
    if (crushed[i] || (uint8_t)(r - fall_r0[i]) >= 2) {
        rock_break(i);
    } else {
        cash_kills(i);
        rock_state[i] = ROCK_WAIT;
        jig_t[i] = 0;
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
void rock_collapse(uint8_t blocks)
{
    uint8_t i;
    if (blocks == 0) {
        blocks = 1;
    }
    for (i = 0; i < ROCK_MAX; i++) {
        uint8_t r;
        if (rock_state[i] != ROCK_WAIT && rock_state[i] != ROCK_JIG) {
            continue;                   /* 落下中・割れている岩は落盤の対象外 */
        }
        r = (uint8_t)(rock_y[i] >> 4);
        if (r + 1 >= ROWS) {
            rock_break(i);              /* 一番下。落ちる先が無いので消える */
            continue;
        }
        sink_to[i] = (uint8_t)(r + blocks);
        rock_state[i] = ROCK_JIG;
        jig_t[i] = ROCK_JIG_FRAMES;
    }
    snd_sfx(SFX_CAVEIN);   /* 盤面全体が動くので、通常の破砕音とは別の合図を鳴らす */
    cavein_flash();        /* 画面全体を白く飛ばす。見ていない場所の事件を伝える */
}

/* 動いている岩があるか。**クリア判定を待たせるために要る。**
   炭柱を割ると45フレーム揺れてから沈むが、その間に最後の黒ダイヤを回収すると
   クリアが成立して rock_update() が止まり、**落盤が一度も完走しない**。
   実測で、揺れ始めてから落ち始めるまでの 0.75秒 の中でクリアしていた。 */
uint8_t rock_busy(void)
{
    uint8_t i;
    for (i = 0; i < ROCK_MAX; i++) {
        if (rock_state[i] == ROCK_JIG || rock_state[i] == ROCK_FALL
            || rock_state[i] == ROCK_SPLIT) {
            return 1;
        }
    }
    return 0;
}

void rock_update(void)
{
    uint8_t i;
    for (i = 0; i < ROCK_MAX; i++) {
        if (rock_state[i] == ROCK_NONE) {
            continue;
        }
        if (rock_state[i] == ROCK_SPLIT) {
            split_t[i]--;
            if (split_t[i] == 0) {
                rock_state[i] = ROCK_NONE;
                spr_park((uint8_t)(SPR_ROCK + i));
            }
            continue;
        }
        if (rock_state[i] == ROCK_FALL) {
            rock_fall_step(i);
            continue;
        }
        if (supported(i) && sink_to[i] == 0xFF) {
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
                kills[i] = 0;
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
            spr_set((uint8_t)(SPR_ROCK + i), x, (uint8_t)(rock_y[i] - 1),
                    (uint8_t)(rock_state[i] == ROCK_SPLIT ? PAT_SPLIT : PAT_ROCK));
        }
    }
}
