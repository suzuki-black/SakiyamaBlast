/* SakiyamaBlast - エンドロールの文面 (置き場は曲と同じセグメント14) */
#ifndef ENDTEXT_H
#define ENDTEXT_H

#include "msx.h"

/* 1行 = 飾り1バイト + 文字32バイト (終端の 0 を含む)。
   **固定長にしてあるのは、窓から1行ずつ読み出すため** (`blob_read` の長さが決まる)。 */
#define EROW_ATTR_N  0    /* 白・等倍 (本文) */
#define EROW_ATTR_H  1    /* 黄・等倍 (見出し) */
#define EROW_ATTR_T  2    /* 黄・倍角 (題) */

/* **文字は32、器は33。** 終端の 0 を入れる場所が要る。s[32] にしていたときは
   32文字の行に終端が入らず、**次の行の飾りバイトが 0 であることに救われていた**
   (飾りが 0 でない行が続いたら、そこまで繋がって読まれる)。 */
typedef struct {
    uint8_t attr;
    char    s[33];
} roll_row;

#define EROW_SIZE  34     /* sizeof(roll_row)。blob_read の長さ */
#define END_ROLL_N 99     /* 行数。endtext.c と必ず一致させる (コンパイル時に検査) */

/* **物語は2本ある。** 0..ROLL_URA-1 が表、ROLL_URA..ROLL_CREDITS-1 が裏、
   ROLL_CREDITS 以降 (題・クレジット・献辞) は共通。
   表は ROLL_URA に達したら ROLL_CREDITS へ飛ばす (`src/ending.c`)。 */
#define ROLL_URA     25
#define ROLL_CREDITS 45

extern const roll_row end_roll[];

#endif
