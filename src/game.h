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

/* 敵 (ネズミ / コウモリ)。
   出現: 画面中央の「巣」(ステージ開始時から掘られている升目) から湧く。ステージ毎に
   決められた総数まで、一定時間ごと、または画面上の敵が全滅した瞬間に次が出てくる。
   総数を出し切ったらそれ以上出ない。
   移動: Mr. Do! 式＝節点で通れる方向のマスクを作り、通れるなら現在の向きを維持、
   約1/8で強制的に選び直す (WORKEXITS / RND2 & 7 == 0)。 */
#define ENEMY_MAX     4       /* 同時に画面に出る上限 */
#define EN_QUOTA_BASE 2       /* 面1の総出現数 */
#define EN_QUOTA_MAX  8
#define SPAWN_BASE    300     /* 次の湧きまでのフレーム数 (面1 = 5秒) */
#define SPAWN_STEP    15      /* 面が進むごとに速くなる */
#define SPAWN_MIN     120
#define SPD_ENEMY     2       /* 坑道 40px/s (自機の 60px/s より遅い) */
#define SPD_ENEMY_DIG 1       /* 掘り屋が土を掘り進む間 20px/s */
#define MORPH_BASE    600     /* 掘り屋化までのフレーム数 (面1 = 10秒) */
#define MORPH_STEP    90      /* 面が進むごとに早くなる */
#define MORPH_MIN     180     /* 高次面での下限 (3秒) */
#define MORPH_PAUSE   30      /* 変異中は一瞬止まる */

/* 体温オーバーヒートと風呂。常磐炭鉱は地熱約40度＋湧水で過酷 (史実) という設定。
   体温は毎フレーム少しずつ上がり、上限に達すると1ミス。各面に1つある風呂を通過すると
   一瞬で下がる。風呂は敵には壊されないが、落下する岩とダイナマイトの爆風では壊れる。
   壊れても直ちにミスにはならないが、次にオーバーヒートするまでに面をクリアできないと
   1ミスになる (＝冷やす手段が無くなるので自然にそうなる)。 */
#define TEMP_MAX      240     /* 上限 = オーバーヒート */
#define TEMP_TICK     15      /* このフレーム数ごとに体温+1 (満タンまで約60秒) */

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
#define COL_RAT    9
#define COL_BAT    13
#define COL_BATH   5

/* --- 方向 --- */
#define DIR_NONE  0
#define DIR_UP    1
#define DIR_DOWN  2
#define DIR_LEFT  3
#define DIR_RIGHT 4

/* --- スプライト面 --- */
#define SPR_PLAYER 0
#define SPR_BOMB   1
#define SPR_ROCK   2                        /* 面 2..5 */
#define SPR_ENEMY  (SPR_ROCK + ROCK_MAX)    /* 面 6..9 */

/* スプライトの絵番号 */
#define PAT_MINER 0
#define PAT_BOMB  1
#define PAT_ROCK  2
#define PAT_RAT   3
#define PAT_BAT   4

/* --- field.c --- */
extern uint8_t  dug[NCELL];      /* 掘り済みフラグ */
extern uint8_t  powder[NCELL];   /* 火薬 (ダイナマイトの弾薬) */
extern uint16_t dug_n;           /* 掘ったセル数 */
extern uint8_t  dug_pct;         /* 採掘率 % */
extern uint8_t  stage;
extern uint8_t  start_c, start_r;
extern uint8_t  nest_c, nest_r;   /* 敵が湧く巣 = 画面中央の既掘セル */
extern uint8_t  bath_c, bath_r;   /* 風呂 = 自機の出現位置の近く (面ごとに違う) */
extern uint8_t  bath_on;          /* 風呂が健在か */

uint8_t cell_dug(int8_t cx, int8_t cy);
void    dig_sweep(uint8_t cx, uint8_t cy);
void    draw_dug_cell(int8_t cx, int8_t cy);
void    draw_cell_dirt(int8_t cx, int8_t cy);
void    draw_powder(uint8_t cx, uint8_t cy);
void    draw_bath(uint8_t cx, uint8_t cy);
void    bath_destroy_at(int8_t cx, int8_t cy);   /* 岩・爆風で壊れる (敵では壊れない) */
void    build_stage(void);

/* --- player.c --- */
extern uint8_t px, py;
extern uint8_t lives;
extern uint8_t dead;
extern uint8_t invuln;
extern uint8_t temp;          /* 体温 0..TEMP_MAX */

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
uint8_t rock_can_push(int8_t i, int8_t dx);    /* 押せるか調べるだけ */
uint8_t rock_push(int8_t i, int8_t dx);         /* 1=押せた 0=押せない */
uint8_t rock_crush_check(uint8_t cx, uint8_t cy);
void    rock_destroy_at(int8_t cx, int8_t cy);  /* 爆風で吹き飛ばす */
void    rock_update(void);
void    rock_draw(void);

/* --- enemy.c --- */
extern uint8_t en_state[ENEMY_MAX];
extern uint8_t en_x[ENEMY_MAX], en_y[ENEMY_MAX];

uint8_t rnd8(void);
void    rnd_seed(uint16_t s);
void    enemy_clear(void);
void    enemy_stage_reset(void);      /* 面開始時に出現数と湧きタイマを仕込む */
void    enemy_update(void);
void    enemy_draw(void);
uint8_t enemy_kill_at(int8_t cx, int8_t cy);   /* 爆風・落下岩で倒す。倒した数を返す */

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
