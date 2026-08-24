/*
 * SakiyamaBlast - ダイナマイト (武器) と火薬 (弾薬)
 *
 * DESIGN.md より:
 *  - 弾薬制。開始ストック0。火薬を拾うとストック+1、次ステージへ持ち越す。
 *  - 設置はストック>0のとき足元セルに1個 (1消費)。v1 は同時1個・連鎖なし。
 *  - 静止・重量無関係。約2秒で十字 (中心＋上下左右1マス) に爆発。
 *  - 爆風は未掘の土を掘り、敵も自機も殺す (自爆で1ミス)。
 */
#include "game.h"
#include "vdp.h"
#include "sprite.h"
#include "sound.h"
#include "art.h"

uint8_t stock;
uint8_t bomb_state;
uint8_t bomb_c, bomb_r;

static uint16_t fuse;
static uint8_t  flash;
static uint8_t  blink_col;

/* ステージ構築時とミス時の後始末。導火中のダイナマイトはミスと同時に取り除く
   (リスポーン直後に見えない爆風で即死するのを避けるため。ストックは戻さない)。 */
void bomb_clear(void)
{
    bomb_state = 0;
    spr_park(SPR_BOMB);
}

void bomb_fire(void)
{
    if (bomb_state != 0) {      /* v1: 爆発するまで次を置けない */
        return;
    }
    if (stock == 0) {
        return;
    }
    stock--;
    bomb_c = player_cell_c();
    bomb_r = player_cell_r();
    bomb_state = 1;
    fuse = FUSE_FRAMES;
    snd_sfx(SFX_SET);
}

/* 爆風1セル: 掘って閃光を出し、そこに居る自機を殺す。 */
static void blast_one(int8_t cx, int8_t cy)
{
    if (cx < 0 || cx >= COLS || cy < 0 || cy >= ROWS) {
        return;
    }
    diamond_blast(cx, cy);          /* ダイヤはダイナマイトだけが壊せる */
    dig_sweep((uint8_t)cx, (uint8_t)cy);
    rock_destroy_at(cx, cy);        /* 爆風にかかった岩は吹き飛ぶ */
    enemy_kill_at(cx, cy);          /* 爆風は敵も倒す */
    bath_destroy_at(cx, cy);        /* 風呂は爆風で壊れる (敵には壊されない) */
    vdp_fill((uint16_t)((uint8_t)cx << 4), (uint16_t)((uint8_t)cy << 4), 16, 16, COL_BLAST);
    if (player_cell_c() == (uint8_t)cx && player_cell_r() == (uint8_t)cy) {
        dead = 1;               /* 自爆も1ミス */
    }
}

static void explode(void)
{
    int8_t c = (int8_t)bomb_c;
    int8_t r = (int8_t)bomb_r;
    blast_one(c, r);
    blast_one(c, (int8_t)(r - 1));
    blast_one(c, (int8_t)(r + 1));
    blast_one((int8_t)(c - 1), r);
    blast_one((int8_t)(c + 1), r);
    bomb_state = 2;
    flash = FLASH_FRAMES;
    spr_park(SPR_BOMB);
    snd_sfx(SFX_BOOM);
}

/* 閃光の跡を正規のトンネル描画 (膜つき) へ差し替える。
   十字の5セルは状態が変わり、その隣接セルは膜の形が変わるので、
   |dc|+|dr| <= 2 の13セルを塗り直す。 */
static void finish_blast(void)
{
    int8_t dc, dr, c, r;
    for (dr = -2; dr <= 2; dr++) {
        for (dc = -2; dc <= 2; dc++) {
            int8_t ad = (int8_t)((dc < 0 ? -dc : dc) + (dr < 0 ? -dr : dr));
            if (ad > 2) {
                continue;
            }
            c = (int8_t)(bomb_c + dc);
            r = (int8_t)(bomb_r + dr);
            if (c < 0 || c >= COLS || r < 0 || r >= ROWS) {
                continue;
            }
            field_redraw_cell(c, r);
        }
    }
    player_carve();             /* 塗り直しで消えた自機の掘り跡を戻す */
}

void bomb_update(void)
{
    if (bomb_state == 1) {
        fuse--;
        if (fuse == 0) {
            explode();
            return;
        }
        /* 導火線が燃えている見た目: 8フレームごとに色を入れ替える */
        {
            uint8_t col = (uint8_t)((fuse & 8) ? 0 : 1);
            if (col != blink_col) {
                blink_col = col;
                if (col) {
                    spr_color(SPR_BOMB, COL_FUSE);          /* 導火線が燃える閃き */
                } else {
                    spr_color_lines(SPR_BOMB, art_colours(PAT_BOMB));
                }
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
