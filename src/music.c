/*
 * SakiyamaBlast - 曲データ
 *
 * BGM = **常磐炭坑節**（福島県浜通り南部〜茨城県北部の民謡。常磐炭田の坑員と選炭婦が
 * 歌い出したもの）。作者不詳の民謡で、旋律そのものに権利は無い。他人の編曲は写さず
 * **自前で PSG 用に編曲**している。
 *
 * **旋律の出典 (2026-09-19 に取り直した)**: 公益財団法人 日本民謡協会の
 * 「盆踊りレクチャー動画 常磐炭坑節」の**歌の旋律だけ**を採譜した。伴奏や録音そのものは
 * 使っていない。5番まで歌われているので、太鼓の拍で八分音符の枠を作り、枠ごとに
 * 鳴っている音を5つの番で多数決して決めた (ほとんどの枠が 5/5 で一致)。
 * 以前は國松竜次氏のギター編曲の音源から採っていたが、オクターブの置き方と間奏が
 * 民謡の節回しと違っていたので差し替えた (DESIGN.md「公開前 TODO」)。
 *
 * 採譜の結果: 主音 B の民謡音階 (B D E F# A)。録音は B♭ で、ここでは半音上げてある。
 * 2拍子。一節は八分音符 80 個 (20小節) = 歌 61 + 間奏 19。録音の八分音符は 0.32 秒。
 *
 * ゲーム向けの編曲:
 *   ch A = 旋律。**実際に鳴る高さは歌と同じ** (E3〜F#4)。音程テーブルの名前は実音より
 *          1オクターブ上なので、データ上は E4〜F#5 と書いてある。以前の旋律は実音で
 *          1オクターブ低く (B2〜)、ベースと近すぎた。
 *          間奏の 19 拍は自前で書いた (録音の三味線の合いの手は写していない)
 *   ch B = ベース (B/F#/D の分散。1小節 = 八分音符4つ)
 *   ch C = ノイズドラム (太鼓・ハイハット・スネア・ハイハット)
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
#define E3   17
#define Fs3  19
#define A3   22
#define B3   24
#define D4   27
#define E4   29
#define Fs4  31
#define A4   34
#define B4   36
#define D5   39
#define E5   41
#define Fs5  43
#define A5   46

#define U   18      /* 八分音符 (0.30秒。録音の 0.32秒より少し速い。16分が整数で割れる) */

/* 旋律。1行 = 1小節 (八分音符4つ)。コメントは録音での音 (B に移調。実音はこれと同じ高さ) */
const uint8_t bgm_a[] = {
    /* 間奏 (自前) */
    B4, U*2, D5, U,   E5, U,
    Fs5,U*2, E5, U,   D5, U,
    E5, U*2, D5, U,   B4, U,
    A4, U*2, B4, U*2,
    B4, U*2, D5, U,                                 /* 3拍 + 次の小節の弱起 */
    /* 歌 */
    E5, U,                                          /* E4 (弱起)            */
    Fs5,U*4,                                        /* F#4 ─               */
    E5, U,   Fs5,U,   D5, U*2,                      /* E4 F#4 D4 ─          */
    D5, U*4,                                        /* D4 ─                 */
    Fs5,U,   E5, U,   Fs5,U,   E5, U,               /* F#4 E4 F#4 E4        */
    B4, U,   A4, U,   B4, U,   D5, U,               /* B3 A3 B3 D4          */
    Fs5,U,   D5, U*2, A4, U,                        /* F#4 D4 ─ A3          */
    B4, U*4,                                        /* B3 ─                 */
    B4, U*2, A4, U,   B4, U,                        /* B3 ─ A3 B3           */
    E5, U,   B4, U*2, Fs4,U,                        /* E4 B3 ─ F#3          */
    D5, U,   E4, U,   Fs4,U,   A4, U,               /* D4 E3 F#3 A3         */
    D5, U*2, B4, U,   D5, U,                        /* D4 ─ B3 D4           */
    D5, U,   B4, U,   Fs4,U,   A4, U,               /* D4 B3 F#3 A3         */
    D5, U*2, B4, U,   Fs4,U,                        /* D4 ─ B3 F#3          */
    A4, U,   B4, U*3,                               /* A3 B3 ─              */
    B4, U*2, A4, U,   B4, U,                        /* B3 ─ A3 B3           */
    0xFF
};

/* ベース: 1小節 = B F# B D。20小節 */
#define BB   B2, U, Fs2,U, B2, U, D3, U
const uint8_t bgm_b[] = {
    BB, BB, BB, BB, BB,  BB, BB, BB, BB, BB,
    BB, BB, BB, BB, BB,  BB, BB, BB, BB, BB,
    0xFF
};

/* ドラム: 1小節 = 太鼓 ハイハット スネア ハイハット (陽気な2拍子)。20小節 */
#define DD   1, U, 3, U, 2, U, 3, U
const uint8_t bgm_c[] = {
    DD, DD, DD, DD, DD,  DD, DD, DD, DD, DD,
    DD, DD, DD, DD, DD,  DD, DD, DD, DD, DD,
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

/*
 * エンディング (約1分42秒で頭へ戻る)。**本作のための書き下ろし。**
 *
 * **ゲーム BGM と同じ5音のまま、主音だけ B から D へ移してある。**
 * 常磐炭坑節は B の民謡音階 (B D E F# A)。その5音をそのままに、落ち着く先を D に
 * すると同じ材料が長調の響きになる。**同じ坑の世界が、仕事を終えて暖かくなった**
 * ことを、新しい音を1つも足さずに言える。半音が無いので、どの音を重ねても濁らない。
 *
 * 速さは八分音符 0.4秒 (四分 = 75拍/分)。エンドロールを見ながら聴く前提で、
 * **1周 102秒**と長く取ってある。4つの8小節 = A / 中間部 / A' / 余韻。
 *
 * **太鼓 (PCM) は間引いて使う。** 刻むと行進になってしまうので、基本は
 * **1小節に1発**。中間部だけ裏の刻みを足して起伏を作り、余韻では1発を長く伸ばす。
 * 遠くの祭りの太鼓のつもり。
 */
#define EV   24           /* 八分音符 (0.4秒) */
#define QT   48           /* 四分 */
#define HF   96           /* 二分 */
#define DH  144           /* 付点二分 */
#define WH  192           /* 全音符 = 1小節 */

const uint8_t end_a[] = {
    /* A: 坑を出る (8小節) */
    A4,QT,  B4,QT,  D5,HF,
    B4,QT,  A4,QT,  Fs4,HF,
    Fs4,QT, A4,QT,  B4,EV,  A4,EV,  Fs4,QT,
    E4,HF,  D4,HF,
    Fs4,QT, A4,QT,  B4,HF,
    D5,QT,  B4,QT,  A4,HF,
    B4,QT,  A4,QT,  Fs4,EV, E4,EV,  D4,QT,
    D4,WH,
    /* 中間部: 空が開ける (8小節) */
    D5,QT,  E5,QT,  Fs5,HF,
    E5,QT,  D5,QT,  B4,HF,
    A4,QT,  B4,QT,  D5,EV,  E5,EV,  D5,QT,
    B4,HF,  A4,HF,
    D5,QT,  E5,QT,  Fs5,HF,
    E5,QT,  Fs5,QT, A5,HF,
    Fs5,QT, E5,QT,  D5,EV,  B4,EV,  A4,QT,
    B4,DH,  A4,QT,
    /* A': 同じ道を戻る (8小節) */
    A4,EV,  B4,EV,  D5,QT,  E5,HF,
    D5,QT,  B4,QT,  A4,HF,
    Fs4,QT, A4,QT,  B4,EV,  D5,EV,  B4,QT,
    A4,HF,  Fs4,HF,
    Fs4,QT, A4,QT,  B4,HF,
    D5,QT,  E5,QT,  D5,HF,
    B4,QT,  A4,QT,  Fs4,EV, E4,EV,  D4,QT,
    D4,WH,
    /* 余韻: 家へ帰る (8小節) */
    D5,HF,  B4,HF,
    A4,HF,  Fs4,HF,
    E4,QT,  Fs4,QT, A4,HF,
    B4,WH,
    D5,HF,  A4,HF,
    Fs4,HF, E4,HF,
    D4,HF,  Fs4,QT, A4,QT,
    D4,WH,
    0xFF
};

/* ベース。1小節に2つ、根音と5度を置くだけ。旋律の下で動きすぎない。 */
const uint8_t end_b[] = {
    D3,HF, A3,HF,   B3,HF, Fs3,HF,  D3,HF, A3,HF,   A3,HF, E3,HF,
    D3,HF, A3,HF,   B3,HF, Fs3,HF,  A3,HF, E3,HF,   D3,WH,
    D3,HF, A3,HF,   B3,HF, Fs3,HF,  D3,HF, A3,HF,   A3,HF, E3,HF,
    D3,HF, A3,HF,   D3,HF, Fs3,HF,  B3,HF, Fs3,HF,  A3,WH,
    D3,HF, A3,HF,   B3,HF, Fs3,HF,  D3,HF, A3,HF,   A3,HF, E3,HF,
    D3,HF, A3,HF,   B3,HF, Fs3,HF,  A3,HF, E3,HF,   D3,WH,
    D3,HF, A3,HF,   B3,HF, Fs3,HF,  A3,HF, E3,HF,   B3,WH,
    D3,HF, A3,HF,   B3,HF, Fs3,HF,  A3,HF, E3,HF,   D3,WH,
    0xFF
};

/* 太鼓。1=宮太鼓 2=締太鼓 3=縁打ち。1小節ぶんずつ。 */
#define EB   1,QT, 0,DH                 /* 1発だけ (基本) */
#define EB2  1,QT, 0,QT, 2,QT, 0,QT     /* 締太鼓を1つ添える (切れ目) */
#define EB3  1,QT, 3,QT, 2,QT, 3,QT     /* 中間部。縁打ちで裏を刻む */
#define EBW  1,WH                       /* 1発を長く伸ばす (余韻) */
const uint8_t end_c[] = {
    EB,  EB,  EB,  EB2,  EB,  EB,  EB2, EBW,
    EB3, EB3, EB3, EB2,  EB3, EB3, EB3, EB2,
    EB,  EB,  EB,  EB2,  EB,  EB,  EB2, EBW,
    EB,  EB,  EB,  EBW,  EB,  EB,  EB2, EBW,
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

/*
 * **残り30秒の合図 (約0.8秒)。** 同じ音を刻みながら上がっていく短いファンファーレで、
 * 「終わりが近い」を急かす形で言う。クリアのファンファーレ (上へ開いて伸びる) とは
 * 逆に、**伸ばさず切る**ので祝いには聞こえない。鳴り終わったら坑内の曲へ戻る。
 */
const uint8_t hurry_a[] = { Fs4,5, Fs4,5, B4,5, B4,5, D5,5, D5,5, Fs5,14, 0xFE };
const uint8_t hurry_b[] = { B3,5, B3,5, D4,5, D4,5, Fs4,5, Fs4,5, B4,14, 0xFE };
const uint8_t hurry_c[] = { B2,10, B2,10, B2,10, B2,14, 0xFE };

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
/* 黒ダイヤを手に入れた: 明るく駆け上がる「チャリーン」。
   割ったとき (低い破砕音) と対になるよう、こちらは高く澄ませる。
   採掘は面クリアに直結する行為なので、いちばん気持ちよく鳴らす。 */
const uint8_t sfx_coal[] = { 61,2, 68,2, 73,2, 80,3, 85,8, 0xFE };

/* 黒ダイヤが砕けた: 硬いものが割れる「ガシャン」。ノイズを短く強く、尾を引かせる。
   2〜8回叩いた報酬なので、掘る音や叩く音よりはっきり大きくする。 */
const uint8_t sfx_break[] = { 2,3, 8,4, 16,5, 26,10, 0xFE };

/* 落盤: 天盤が落ちてくる「ゴゴゴ…」。**盤面全体が動く事件なので、
   通常の破砕音と必ず違う音にする。** 同じ音だと、画面の別の場所を見ている間に
   落盤が起きたことに気づけず、避けられるはずの死が避けられなくなる。
   一番低いノイズから始めて長く尾を引かせ、揺れの0.75秒を覆う。 */
const uint8_t sfx_cavein[] = { 31,6, 29,6, 30,8, 28,10, 31,16, 0xFE };

/* 投げられないとき: 低く短い「コツン」。押しても何も起きないと
   ボタンが効いていないように見えるので、必ず何か鳴らす。 */
const uint8_t sfx_nope[] = { 13,3, 0xFE };
/* 押している岩の中心が升の境目を越えた合図。**短く高く、すぐ消える。**
   支えの判定が切り替わった一点だけを知らせる音なので、余韻を残さない。 */
const uint8_t sfx_click[] = { 64,2, 0xFE };
/* 樋の板が折れた: 乾いた「バキッ」。**黒ダイヤの破砕音 (ガシャン) と必ず変える。**
   同じ音だと、岩が炭を貫いたのか板を踏み抜いたのかが耳で区別できない。
   木なので高いところから一気に落として尾を引かせない。 */
const uint8_t sfx_wood[] = { 3,2, 9,3, 16,4, 0xFE };
/* 熱水が湧き出した: 「ゴボッ」。低いノイズで噴き出し、トーンの泡が2つ上がる。
   **落盤の「ゴゴゴ」と重ねない。** 落盤の1秒後に鳴らす (water.c) ので、
   原因 (天盤が落ちた) と結果 (湯が噴いた) が耳でも分かれる。 */
const uint8_t sfx_gush[] = { 30,4, 26,4, 0xFD,0, 13,3, 20,3, 27,6, 0xFE };
/* 熱水が1升広がった: 小さく短い「ポチャ」。**優先度は一番低い**ので、
   ほかの音を止めない。1秒に1回鳴り続けるので、水が動いていることが耳で分かる。 */
const uint8_t sfx_drip[] = { 44,1, 51,2, 0xFE };
/* 湧き出し口が止まった: 「シュウ…」。高いノイズから低いほうへ抜けて消える。
   **これ以上は増えない**ことの合図。噴き出しの「ゴボッ」と対になる。 */
const uint8_t sfx_seal[] = { 4,6, 8,8, 14,10, 22,14, 0xFE };
/* クリア時、残り時間を得点へ換えている間の刻み音。**短く高く、尾を引かせない。**
   2フレームで切れる音を4フレームに1回鳴らすので、速く減っていく数字と一緒に
   「チチチチ」と刻む (tlimit.c)。 */
const uint8_t sfx_tally[] = { 72,2, 0xFE };
/* 鉱車が升をひとつ越えた: 低く短い「ゴト」。**優先度は一番低い**ので他の音を止めない。
   等速で鳴り続けるので、画面を見ていなくても鉱車がどこに居るか分かる。 */
const uint8_t sfx_cart[] = { 0xFD,1, 20,2, 26,3, 0xFE };

/* つるはし: 高い音を2つ、短く硬く。金属が石を叩く「キンキン」。 */
/* つるはし: 高い矩形波にノイズを重ねた金属音。PSG は波形を変えられないので、
   トーンだけだとブザーになる。非調和成分をノイズで足して「キンッ」にする。 */
/* つるはし: **表のいちばん上に近い約4kHz** から一気に減衰する「キン！」。
   トーンだけだとブザーなので細かいノイズを重ね、非調和成分で金属らしくする。 */
const uint8_t sfx_mine[] = {
    0xFD, 1,  10,2,                      /* 出だし: 低めのノイズで「ッ」 */
    0xFD, 2,  84,2, 84,1, 80,4, 76,7,    /* 続けて 約4kHz から落ちる「キン」 */
    0xFE
};
const uint8_t sfx_miss[] = { B4,5, A4,5, Fs4,5, D4,5, B3,7, Fs3,16, 0xFE };  /* ミス: 下降 */
