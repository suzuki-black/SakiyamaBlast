/*
 * SakiyamaBlast - 曲データ
 *
 * ★ 現在の BGM は「炭坑節そのもの」ではない。常磐炭坑節・炭坑節とも、音符単位で
 *   正確に採譜できる資料が手元に無いため、同じ土俵の**オリジナル曲**を置いてある。
 *   ヨナ抜き長音階 (C D E G A) の2拍子で、盆踊り／民謡の運びに寄せてある。
 *   正しい旋律が手に入ったら、下の bgm_a / bgm_b / bgm_c を差し替えるだけでよい
 *   (ドライバは触らなくてよい)。経緯は DESIGN.md の権利方針を参照。
 *
 * 書式: (音程, 長さ) の並び。音程は sound.c の音程テーブルの番号。
 *       0=休符 / 0xFE=終わり / 0xFF=先頭へ戻る。長さは 60fps のフレーム数。
 *       四分音符=24フレーム (約150拍/分)、八分音符=12。
 */
#include "msx.h"

/* 音名 -> テーブル番号 */
#define C2   1
#define E2   5
#define G2   8
#define A2  10
#define C3  13
#define D3  15
#define E3  17
#define G3  20
#define A3  22
#define C4  25
#define D4  27
#define E4  29
#define G4  32
#define A4  34
#define C5  37
#define D5  39
#define E5  41
#define G5  44
#define C6  49

#define Q   12      /* 八分 */
#define CR  24      /* 四分 */
#define MI  48      /* 二分 */

/* 主旋律 */
const uint8_t bgm_a[] = {
    G4,Q, G4,Q, A4,Q, C5,Q,        /*  1 */
    C5,Q, A4,Q, G4,CR,             /*  2 */
    A4,Q, C5,Q, D5,Q, C5,Q,        /*  3 */
    A4,Q, G4,Q, E4,CR,             /*  4 */
    E4,Q, G4,Q, A4,Q, G4,Q,        /*  5 */
    E4,Q, D4,Q, C4,CR,             /*  6 */
    C4,Q, E4,Q, G4,Q, A4,Q,        /*  7 */
    G4,CR, 0,CR,                   /*  8 */
    C5,Q, C5,Q, D5,Q, E5,Q,        /*  9 */
    E5,Q, D5,Q, C5,CR,             /* 10 */
    A4,Q, A4,Q, C5,Q, A4,Q,        /* 11 */
    G4,Q, E4,Q, G4,CR,             /* 12 */
    A4,Q, G4,Q, E4,Q, D4,Q,        /* 13 */
    C4,Q, D4,Q, E4,Q, G4,Q,        /* 14 */
    A4,CR, G4,CR,                  /* 15 */
    C4,MI,                         /* 16 */
    0xFF
};

/* 和音 (旋律の下で支える) */
const uint8_t bgm_b[] = {
    E3,CR, G3,CR,
    E3,CR, C4,CR,
    E3,CR, A3,CR,
    D3,CR, G3,CR,
    E3,CR, G3,CR,
    E3,CR, A3,CR,
    D3,CR, G3,CR,
    E3,CR, C4,CR,
    G3,CR, E3,CR,
    A3,CR, E3,CR,
    G3,CR, D3,CR,
    E3,CR, C4,CR,
    C4,CR, E3,CR,
    D3,CR, G3,CR,
    D3,CR, G3,CR,
    C4,MI,
    0xFF
};

/* 低音 (1小節1音の根音) */
const uint8_t bgm_c[] = {
    C2,MI, C2,MI, A2,MI, G2,MI,
    C2,MI, A2,MI, G2,MI, C2,MI,
    C2,MI, A2,MI, G2,MI, C2,MI,
    A2,MI, G2,MI, G2,MI, C2,MI,
    0xFF
};

/* ステージクリアのファンファーレ (約1.7秒) */
const uint8_t fan_a[] = {
    G4,8, C5,8, E5,8, G5,16, 0,4, E5,8, G5,8, C6,40, 0xFE
};
const uint8_t fan_b[] = {
    E4,8, G4,8, C5,8, E5,16, 0,4, C5,8, E5,8, G5,40, 0xFE
};
const uint8_t fan_c[] = {
    C3,32, G3,28, C3,40, 0xFE
};
