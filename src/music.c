/*
 * SakiyamaBlast - 曲データ
 *
 * BGM = **常磐炭坑節**（福島県浜通り南部〜茨城県北部の民謡。常磐炭田の坑員と選炭婦が
 * 歌い出したもの）。民謡なので旋律そのものの著作権は切れている前提で、他人の編曲は
 * 写さず **自前で PSG 用に編曲**している。旋律は無償公開されている音源から採譜した。
 *
 * 採譜の結果: 主音 B の民謡音階 (B D E F# A)、八分音符 0.236 秒 = 四分音符 約127 BPM。
 * 曲は A-BCD-BCD-AB の形で、BCD の一節がまるごと2回繰り返される。その一節
 * (41音 / 108単位 / 約25秒) をループとして使う。
 *
 * ゲーム向けの編曲:
 *   ch A = 旋律 (採譜そのまま)
 *   ch B = ベース (B/F#/D の分散。原曲の B2 ドローンを動く低音に置き換えた)
 *   ch C = ノイズドラム (太鼓・スネア・ハイハット)
 *
 * 書式: (音程, 長さ) の並び。音程は sound.c の音程テーブル番号 (C4=25)。
 *       ch C だけは音程でなく打種 (1=太鼓 2=スネア 3=ハイハット)。
 *       0=休符 / 0xFE=終わり / 0xFF=先頭へ戻る。長さは 60fps のフレーム数。
 */
#include "msx.h"

/* 音名 -> sound.c の音程テーブル番号 (MIDI - 35) */
#define Fs2   7
#define B2   12
#define D3   15
#define Fs3  19
#define B3   24
#define D4   27
#define E4   29
#define Fs4  31
#define A4   34
#define B4   36
#define D5   39
#define E5   41
#define Fs5  43

#define U   14      /* 八分音符 */

/* 旋律 (常磐炭坑節・採譜) */
const uint8_t bgm_a[] = {
    B4,   U*2  , D4,   U*3  , B3,   U*2  , Fs4,  U*3  ,   /* B4    D4    B3    F#4   */
    B3,   U*2  , D4,   U*3  , B3,   U*2  , D4,   U*1  ,   /* B3    D4    B3    D4    */
    B3,   U*10 , E4,   U*1  , Fs4,  U*5  , B3,   U*2  ,   /* B3    E4    F#4   B3    */
    E4,   U*1  , D4,   U*5  , E4,   U*2  , D4,   U*1  ,   /* E4    D4    E4    D4    */
    B3,   U*2  , Fs4,  U*1  , E4,   U*2  , D4,   U*1  ,   /* B3    F#4   E4    D4    */
    B4,   U*1  , B3,   U*1  , D4,   U*2  , E4,   U*1  ,   /* B4    B3    D4    E4    */
    B3,   U*16 , B4,   U*2  , A4,   U*1  , B4,   U*2  ,   /* B3    B4    A4    B4    */
    D5,   U*1  , E5,   U*3  , Fs5,  U*2  , D4,   U*2  ,   /* D5    E5    F#5   D4    */
    B3,   U*1  , D4,   U*2  , B3,   U*3  , D4,   U*1  ,   /* B3    D4    B3    D4    */
    B3,   U*2  , D4,   U*1  , A4,   U*2  , B4,   U*3  ,   /* B3    D4    A4    B4    */
    B3,   U*8  ,   /* B3    */
    0xFF
};

/* ベース: 1小節 = B2 F#2 B2 D3 の八分。原曲のドローンを動く低音に。 */
const uint8_t bgm_b[] = {
    B2, U,   Fs2,U,   B2, U,   D3, U, B2, U,   Fs2,U,   B2, U,   D3, U, B2, U,   Fs2,U,   B2, U,   D3, U, B2, U,   Fs2,U,   B2, U,   D3, U,
    B2, U,   Fs2,U,   B2, U,   D3, U, B2, U,   Fs2,U,   B2, U,   D3, U, B2, U,   Fs2,U,   B2, U,   D3, U, B2, U,   Fs2,U,   B2, U,   D3, U,
    B2, U,   Fs2,U,   B2, U,   D3, U, B2, U,   Fs2,U,   B2, U,   D3, U, B2, U,   Fs2,U,   B2, U,   D3, U, B2, U,   Fs2,U,   B2, U,   D3, U,
    B2, U,   Fs2,U,   B2, U,   D3, U, B2, U,   Fs2,U,   B2, U,   D3, U, B2, U,   Fs2,U,   B2, U,   D3, U, B2, U,   Fs2,U,   B2, U,   D3, U,
    B2, U,   Fs2,U,   B2, U,   D3, U, B2, U,   Fs2,U,   B2, U,   D3, U, B2, U,   Fs2,U,   B2, U,   D3, U, B2, U,   Fs2,U,   B2, U,   D3, U,
    B2, U,   Fs2,U,   B2, U,   D3, U, B2, U,   Fs2,U,   B2, U,   D3, U, B2, U,   Fs2,U,   B2, U,   D3, U, B2, U,   Fs2,U,   B2, U,   D3, U,
    B2, U,   Fs2,U,   B2, U,   D3, U, B2, U,   Fs2,U,   B2, U,   D3, U, B2, U,   Fs2,U,   B2, U,   D3, U,
    0xFF
};

/* ドラム: 1小節 = 太鼓 ハイハット スネア ハイハット (陽気な2拍子) */
const uint8_t bgm_c[] = {
    1,U, 3,U, 2,U, 3,U, 1,U, 3,U, 2,U, 3,U, 1,U, 3,U, 2,U, 3,U, 1,U, 3,U, 2,U, 3,U,
    1,U, 3,U, 2,U, 3,U, 1,U, 3,U, 2,U, 3,U, 1,U, 3,U, 2,U, 3,U, 1,U, 3,U, 2,U, 3,U,
    1,U, 3,U, 2,U, 3,U, 1,U, 3,U, 2,U, 3,U, 1,U, 3,U, 2,U, 3,U, 1,U, 3,U, 2,U, 3,U,
    1,U, 3,U, 2,U, 3,U, 1,U, 3,U, 2,U, 3,U, 1,U, 3,U, 2,U, 3,U, 1,U, 3,U, 2,U, 3,U,
    1,U, 3,U, 2,U, 3,U, 1,U, 3,U, 2,U, 3,U, 1,U, 3,U, 2,U, 3,U, 1,U, 3,U, 2,U, 3,U,
    1,U, 3,U, 2,U, 3,U, 1,U, 3,U, 2,U, 3,U, 1,U, 3,U, 2,U, 3,U, 1,U, 3,U, 2,U, 3,U,
    1,U, 3,U, 2,U, 3,U, 1,U, 3,U, 2,U, 3,U, 1,U, 3,U, 2,U, 3,U,
    0xFF
};

/*
 * タイトル BGM = **Down in a Coal Mine** (J. B. Geoghegan 作曲 / 詞 H.E.K.、1872年)。
 * 作曲者は1889年に没しており著作権は切れている。他人の編曲は写さず、公開されている
 * 譜面 (ハ長調・6/8・16小節) から旋律を読み取って自前で PSG 用に編曲した。
 *
 * 八分音符 = 12フレーム = 0.2秒 (付点四分 100 BPM)。16小節で約19秒のループ。
 *   ch A = 旋律 (譜面そのまま)
 *   ch B = ベース (小節ごとに根音3つ + 5度3つ)
 *   ch C = ノイズドラム (6/8 の 太鼓・スネア・ハイハット)
 */

/* ハ長調。音名 -> 音程テーブル番号 (MIDI - 35) */
#define F2t   6
#define G2t   8
#define A2t  10
#define B2t  12
#define C3t  13
#define D3t  15
#define E3t  17
/* 旋律。名前は譜面の音名だが、値は**1オクターブ上**の音程番号にしてある。
   ベース (C3t = 約65Hz) と2オクターブ離さないと低音が濁るため。 */
#define mC4  37
#define mD4  39
#define mE4  41
#define mF4  42
#define mG4  44
#define mA4  46
#define mB4  48
#define mC5  49
#define mD5  51

#define T   12      /* 八分音符 */

/* 旋律。1行 = 1小節 (八分音符6つ分)。 */
const uint8_t title_a[] = {
    mC5,T*3, mC5,T,   mB4,T,   mA4,T,          /* Down     in__ a       */
    mG4,T*3, mE4,T*3,                          /* coal     mine         */
    mF4,T*2, mF4,T,   mF4,T*2, mG4,T,          /* un-der-neath the      */
    mE4,T*5, 0,  T,                            /* ground,               */
    mA4,T*2, mA4,T,   mC5,T,   mB4,T,   mA4,T, /* Where a  gleam of     */
    mG4,T*3, mE4,T*3,                          /* sun-shine             */
    mA4,T*2, mG4,T,   mE4,T*2, mC4,T,          /* nev-er   can be       */
    mD4,T*5, 0,  T,                            /* found,                */
    mE4,T*2, mD4,T,   mC4,T*2, mE4,T,          /* Dig-ging dus-ky       */
    mG4,T*3, mG4,T*3,                          /* dia-monds             */
    mA4,T,   mB4,T,   mC5,T,   mD5,T*2, mC5,T, /* all__ the sea-son     */
    mB4,T*5, 0,  T,                            /* round,                */
    mC5,T*3, mC5,T,   mB4,T,   mA4,T,          /* Down     in__ a       */
    mG4,T*3, mC5,T*3,                          /* coal     mine         */
    mD5,T*2, mA4,T,   mB4,T*2, mA4,T,          /* un-der-neath the      */
    mC5,T*5, 0,  T,                            /* ground.               */
    0xFF
};

/*
 * ベース: 6/8 が跳ねるように、1拍を「根音(2) + 5度(1)」で刻む。
 * 2拍目の裏は3度にして動きを出す。和音は
 *   C C F C / F C C G / C C G G / C C G C
 * 4小節ごとの区切り (8/12/15小節目) は次の和音へ歩いて入る。
 */
#define BC   C3t,T*2, G2t,T,  C3t,T*2, E3t,T          /* C */
#define BF   F2t,T*2, C3t,T,  F2t,T*2, A2t,T          /* F */
#define BG   G2t,T*2, D3t,T,  G2t,T*2, B2t,T          /* G */
#define BGw  G2t,T*2, D3t,T,  G2t,T,   A2t,T, B2t,T   /* G -> C へ歩く */
const uint8_t title_b[] = {
    BC,  BC,  BF,  BC,
    BF,  BC,  BC,  BGw,
    BC,  BC,  BG,  BGw,
    BC,  BC,  BGw, BC,
    0xFF
};

/*
 * ドラム: 基本は 6/8 の 太鼓-ハット-ハット-スネア-ハット-ハット。
 * 4小節ごとの終わりはスネアのフィルにして、16小節目はループ頭へ向けて大きく入れる。
 */
#define DR   1,T, 3,T, 3,T, 2,T, 3,T, 3,T      /* 基本 */
#define DF   1,T, 3,T, 3,T, 2,T, 2,T, 2,T      /* 4小節ごとの切れ目 */
#define DFE  1,T, 2,T, 2,T, 2,T, 2,T, 2,T      /* ループ直前 */
const uint8_t title_c[] = {
    DR, DR, DR, DF,
    DR, DR, DR, DF,
    DR, DR, DR, DF,
    DR, DR, DR, DFE,
    0xFF
};

/* ステージクリアのファンファーレ (約1.7秒)。曲と同じ B マイナーで、トーン3声。 */
const uint8_t fan_a[] = {
    B3,8, D4,8, Fs4,8, B4,16, 0,4, Fs4,8, B4,8, D5,40, 0xFE
};
const uint8_t fan_b[] = {
    Fs3,8, B3,8, D4,8, Fs4,16, 0,4, D4,8, Fs4,8, B4,40, 0xFE
};
const uint8_t fan_c[] = {
    B2,32, Fs3,28, B2,40, 0xFE
};

/* ミスのジングル (約1秒)。曲と同じ B マイナーで下降する。 */
const uint8_t miss_a[] = { B4,6, A4,6, Fs4,6, D4,8, B3,10, 0,4, Fs3,20, 0xFE };
const uint8_t miss_b[] = { Fs4,6, E4,6, D4,6, B3,8, Fs3,10, 0,4, D3,20, 0xFE };
const uint8_t miss_c[] = { B2,18, Fs2,18, B2,24, 0xFE };

/*
 * 効果音。ch C を一時的に奪って鳴らす。
 * ノイズの音は値がノイズ周期 (0-31, 大きいほど低い)、トーンの音は音程テーブル番号。
 */
const uint8_t sfx_dig[]  = { 14,3, 8,4, 0xFE };                     /* 掘る: 短い擦り音 */
const uint8_t sfx_pick[] = { B4,4, D5,4, Fs5,8, 0xFE };             /* 拾う: 上がる3音 */
const uint8_t sfx_set[]  = { B2,5, Fs2,6, 0xFE };                   /* 置く: 低い2音 */
const uint8_t sfx_boom[] = { 6,4, 12,5, 18,6, 24,8, 30,14, 0xFE };  /* 爆発: 低く落ちる轟音 */
/* 黒ダイヤが砕けた: 硬いものが割れる「ガシャン」。ノイズを短く強く、尾を引かせる。
   2〜8回叩いた報酬なので、掘る音や叩く音よりはっきり大きくする。 */
const uint8_t sfx_break[] = { 2,3, 8,4, 16,5, 26,10, 0xFE };

/* 投げられないとき: 低く短い「コツン」。押しても何も起きないと
   ボタンが効いていないように見えるので、必ず何か鳴らす。 */
const uint8_t sfx_nope[] = { 13,3, 0xFE };

/* つるはし: 高い音を2つ、短く硬く。金属が石を叩く「キンキン」。 */
const uint8_t sfx_mine[] = { 55,2, 0,1, 57,3, 0xFE };
const uint8_t sfx_miss[] = { B4,5, A4,5, Fs4,5, D4,5, B3,7, Fs3,16, 0xFE };  /* ミス: 下降 */
