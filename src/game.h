/* SakiyamaBlast - 盤面と定数 */
#ifndef GAME_H
#define GAME_H

#include "msx.h"

#define COLS      16          /* 升目列 (16px セル) */
#define ROWS      11          /* 升目行 */
#define CELL      16
#define FIELD_H   176         /* 盤面の高さ px (= ROWS * CELL) */
#define HUD_Y     176         /* HUD 帯の上端 */
#define SCR_H     212

#define PW        15          /* スプライト幅-1 */
#define PSPD      1           /* 自機速度 px/frame (60fps で約60px/s) */
#define XMAX      (255 - PW)
#define YMAX      (FIELD_H - CELL)

/* パレット番号 */
#define COL_TUNNEL 1          /* 掘り済み (黒) */
#define COL_DIRT   6          /* 土 */
#define COL_HUD    4
#define COL_BLAST  11
#define COL_WHITE  15

/* 方向 */
#define DIR_NONE  0
#define DIR_UP    1
#define DIR_DOWN  2
#define DIR_LEFT  3
#define DIR_RIGHT 4

extern uint8_t dug[ROWS * COLS];   /* 掘り済みフラグ */
extern uint8_t dug_n;              /* 掘ったセル数 */

#endif
