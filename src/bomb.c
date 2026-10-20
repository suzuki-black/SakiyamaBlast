/*
 * SakiyamaBlast - ダイナマイト (投げる武器) と火薬 (弾薬)
 *
 * 仕様:
 *  - 弾薬制。開始ストック0。火薬を拾うとストック+1、次ステージへ持ち越す。
 *  - B ボタンで、自機が向いている方向へ投げる。最大 BOMB_RANGE 升飛ぶ。
 *  - 飛んでいる途中に敵が居ればそこで爆発。**爆風は1升だけで、地形は掘らない。**
 *  - 敵以外の障害物 (未掘の土・黒ダイヤ・岩・風呂) があればその手前に落ちる。
 *  - 何にも当たらなければ BOMB_RANGE 升先に落ちる。
 *  - 落ちたダイナマイトはアイテム。拾い直せる。障害物にはならず、敵は素通りする。
 */
#include "game.h"
#include "vdp.h"
#include "sprite.h"
#include "sound.h"
#include "art.h"

uint8_t stock;
uint8_t bomb_state;
uint8_t bomb_c, bomb_r;

static uint8_t bomb_dir;
static uint8_t bomb_left;      /* あと何升飛べるか */
static uint8_t fly;            /* 次の升へ移るまでのフレーム数 */
static uint8_t flash;

/* ステージ構築時とミス時の後始末。飛行中のダイナマイトは消える (ストックは戻さない)。 */
void bomb_clear(void)
{
    bomb_state = 0;
    spr_park(SPR_BOMB);
}

/* その升へ飛び込めるか。敵は「当たり」なので別に見る。 */
static uint8_t flyable(int8_t cx, int8_t cy)
{
    if (cx < 0 || cx >= COLS || cy < 0 || cy >= ROWS) {
        return 0;
    }
    if (!cell_dug(cx, cy)) {
        return 0;                   /* 未掘の土は抜けられない */
    }
    if (cell_coal(cx, cy)) {
        return 0;                   /* 黒ダイヤは硬い */
    }
    if (rock_at(cx, cy) >= 0) {
        return 0;
    }
    if (bath_on && (uint8_t)cx == bath_c && (uint8_t)cy == bath_r) {
        return 0;                   /* 風呂は壊したくないので手前で止まる */
    }
    return 1;
}

void bomb_throw(uint8_t dir)
{
    int8_t nc, nr;
    if (bomb_state != 0 || dir == DIR_NONE) {
        return;
    }
    if (stock == 0) {
        snd_sfx(SFX_NOPE);          /* 持っていない。無反応だと壊れて見える */
        return;
    }
    /* 投げる先が塞がっているなら投げない。**そのまま投げると足元に落ちて即座に
       拾い直すだけ**になり、押しても何も起きていないように見える。
       掘っている間は常に土を向いているので、これが無いと B が死んでいると感じる。
       ただし隣に敵が居るときは、ゼロ距離で当てられるように投げさせる。 */
    nc = (int8_t)player_cell_c();
    nr = (int8_t)player_cell_r();
    switch (dir) {
    case DIR_UP:    nr--; break;
    case DIR_DOWN:  nr++; break;
    case DIR_LEFT:  nc--; break;
    default:        nc++; break;
    }
    if (!enemy_at(nc, nr) && !flyable(nc, nr)) {
        snd_sfx(SFX_NOPE);          /* 向いている先が土や岩。投げる隙間が無い */
        return;
    }
    stock--;
    bomb_c = player_cell_c();
    bomb_r = player_cell_r();
    bomb_dir = dir;
    bomb_left = BOMB_RANGE;
    fly = BOMB_FLY_FRAMES;
    bomb_state = 1;
    spr_color_lines(SPR_BOMB, art_colours(PAT_BOMB));
    snd_sfx(SFX_SET);
}

/* 敵に当たった1升だけの爆発。地形は掘らず、岩も風呂も壊さない。 */
static void explode_on_enemy(void)
{
    uint8_t killed = enemy_kill_at((int8_t)bomb_c, (int8_t)bomb_r, 0);
    score_kills(killed, 0);
    score_popup(bomb_c, bomb_r, kill_value(killed, 0));
    vdp_fill((uint16_t)((uint16_t)bomb_c << 4), (uint16_t)((uint16_t)bomb_r << 4),
             16, 16, COL_BLAST);
    if (!invuln && player_cell_c() == bomb_c && player_cell_r() == bomb_r) {
        dead = 1;                   /* 自分の升で爆発したら巻き添え */
    }
    bomb_state = 2;
    flash = FLASH_FRAMES;
    spr_park(SPR_BOMB);
    snd_sfx(SFX_BOOM);
}

/* 閃光の跡を元の見た目へ戻す。膜の形は隣の升にも影響するので周囲も塗り直す。 */
static void finish_blast(void)
{
    int8_t dc, dr, c, r;
    for (dr = -1; dr <= 1; dr++) {
        for (dc = -1; dc <= 1; dc++) {
            c = (int8_t)(bomb_c + dc);
            r = (int8_t)(bomb_r + dr);
            if (c < 0 || c >= COLS || r < 0 || r >= ROWS) {
                continue;
            }
            field_redraw_cell(c, r);
        }
    }
    player_carve();
}


/* 飛べなくなったので、いまの升へ落として拾えるようにする。 */
static void bomb_land(void)
{
    cf_set((uint16_t)bomb_r * COLS + bomb_c, CF_BOMB);
    draw_bombdrop(bomb_c, bomb_r);
    bomb_state = 0;
    spr_park(SPR_BOMB);
}

static void fly_step(void)
{
    int8_t nc = (int8_t)bomb_c;
    int8_t nr = (int8_t)bomb_r;

    if (bomb_left == 0) {
        bomb_land();
        return;
    }
    switch (bomb_dir) {
    case DIR_UP:    nr--; break;
    case DIR_DOWN:  nr++; break;
    case DIR_LEFT:  nc--; break;
    default:        nc++; break;
    }
    if (enemy_at(nc, nr)) {         /* 敵が居たらそこで爆発 */
        bomb_c = (uint8_t)nc;
        bomb_r = (uint8_t)nr;
        explode_on_enemy();
        return;
    }
    if (!flyable(nc, nr)) {         /* 障害物: いまの升に落ちる */
        bomb_land();
        return;
    }
    bomb_c = (uint8_t)nc;
    bomb_r = (uint8_t)nr;
    bomb_left--;
    if (bomb_left == 0) {           /* 飛距離を使い切った先に落ちる */
        bomb_land();
    }
}

void bomb_update(void)
{
    if (bomb_state == 1) {
        fly--;
        if (fly == 0) {
            fly = BOMB_FLY_FRAMES;
            fly_step();
            if (bomb_state != 1) {
                return;
            }
        }
        spr_set(SPR_BOMB, (uint8_t)(bomb_c << 4), (uint8_t)((bomb_r << 4) - 1), PAT_BOMB);
    } else if (bomb_state == 2) {
        flash--;
        if (flash == 0) {
            bomb_state = 0;
            finish_blast();
        }
    }
}
