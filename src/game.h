/* SakiyamaBlast - 盤面・共有状態・モジュール間インタフェース */
#ifndef GAME_H
#define GAME_H

#include "msx.h"

/* --- 盤面 --- */
#define COLS      16          /* 升目列 (16px セル) */
#define ROWS      11          /* 升目行 */
#define NCELL     (COLS * ROWS)
#define CELL      16
#define FIELD_H   176         /* 盤面の高さ px (= ROWS * CELL) */
#define HUD_Y     176         /* HUD 帯の上端 */
#define SCR_H     212

#define PW        15          /* スプライト幅-1 */
#define XMAX      (255 - PW)
#define YMAX      (FIELD_H - CELL)

/* 速度は分子加算式。毎フレーム加算し SPD_UNIT に達するごとに 1px 進む。
   Mr. Do! は既掘の坑道は等速・未掘の土を食い進む間は減速する (EATWALL の速度ペナルティ)。
   60fps で 坑道 60px/s / 掘削 40px/s。 */
#define SPD_UNIT    3
#define SPD_TUNNEL  3
#define SPD_DIG     2

/* 岩 (Mr. Do! の apple 準拠)。支え判定は真下のセル(地形)だけを見る = 自機は支えにならない。
   支えを失うと 60フレーム揺れてから落下する (APPLEJIG の "60-frame wobble")。 */
#define ROCK_MAX      4
#define ROCK_JIG_FRAMES 60    /* 揺れ (約1秒) */
#define ROCK_FALL_SPD 2       /* 落下 px/frame (1セル=8フレーム) */

#define FUSE_FRAMES   120     /* ダイナマイトの導火 = 約2秒 */
#define FLASH_FRAMES  8       /* 爆風の閃光 */
#define INVULN_FRAMES 90      /* ミス後の無敵 */
#define WIN_PCT       90      /* 採掘率がこれ以上でステージクリア */
#define LIVES_INIT    3

/* --- パレット番号 --- */
#define COL_TUNNEL 1
#define COL_DIRT   6
#define COL_HUD    4
#define COL_BLAST  11
#define COL_WHITE  15
#define COL_RED    8
#define COL_FUSE   10
#define COL_ROCK   14

/* --- 方向 --- */
#define DIR_NONE  0
#define DIR_UP    1
#define DIR_DOWN  2
#define DIR_LEFT  3
#define DIR_RIGHT 4

/* --- スプライト面 --- */
#define SPR_PLAYER 0
#define SPR_BOMB   1
#define SPR_ROCK   2          /* 面 2..2+ROCK_MAX-1 */

/* --- field.c --- */
extern uint8_t  dug[NCELL];      /* 掘り済みフラグ */
extern uint8_t  powder[NCELL];   /* 火薬 (ダイナマイトの弾薬) */
extern uint16_t dug_n;           /* 掘ったセル数 */
extern uint8_t  dug_pct;         /* 採掘率 % */
extern uint8_t  stage;
extern uint8_t  start_c, start_r;

uint8_t cell_dug(int8_t cx, int8_t cy);
void    dig_sweep(uint8_t cx, uint8_t cy);
void    draw_dug_cell(int8_t cx, int8_t cy);
void    draw_cell_dirt(int8_t cx, int8_t cy);
void    draw_powder(uint8_t cx, uint8_t cy);
void    build_stage(void);

/* --- player.c --- */
extern uint8_t px, py;
extern uint8_t lives;
extern uint8_t dead;
extern uint8_t invuln;

void player_start(void);
void player_update(void);
void player_respawn(void);
void player_carve(void);
uint8_t player_cell_c(void);
uint8_t player_cell_r(void);

/* --- rock.c --- */
#define ROCK_NONE 0
#define ROCK_WAIT 1
#define ROCK_JIG  2
#define ROCK_FALL 3

extern uint8_t rock_state[ROCK_MAX];
extern uint8_t rock_x[ROCK_MAX], rock_y[ROCK_MAX];

void    rock_clear(void);
void    rock_place(uint8_t c, uint8_t r);
int8_t  rock_at(int8_t cx, int8_t cy);          /* そのセルの岩の番号 / 無ければ -1 */
uint8_t rock_push(int8_t i, int8_t dx);         /* 1=押せた 0=押せない */
void    rock_destroy_at(int8_t cx, int8_t cy);  /* 爆風で吹き飛ばす */
void    rock_update(void);
void    rock_draw(void);

/* --- bomb.c --- */
extern uint8_t stock;        /* ダイナマイトのストック (次ステージへ持ち越し) */
extern uint8_t bomb_state;   /* 0=無 1=導火中 2=爆風閃光中 */
extern uint8_t bomb_c, bomb_r;

void bomb_clear(void);
void bomb_fire(void);
void bomb_update(void);

/* --- hud.c --- */
void hud_reset(void);
void hud_draw(void);

#endif
