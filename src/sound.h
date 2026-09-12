/* SakiyamaBlast - PSG (AY-3-8910) の音楽ドライバ */
#ifndef SOUND_H
#define SOUND_H

#include "msx.h"

void    snd_init(void);
void    snd_tick(void);          /* 毎フレーム1回呼ぶ */

/* 曲のテンポ。TEMPO_UNIT が等倍で、大きいほど速い。
   体温が上がるほど速くする (main.c)。曲を始めると等倍へ戻る。 */
#define TEMPO_UNIT 16
extern uint8_t snd_tempo;
void    snd_set_tempo(uint8_t n);
void    snd_play_bgm(void);
void    snd_play_title(void);    /* タイトル画面の曲 */
void    snd_play_fanfare(void);
void    snd_play_miss(void);
void    snd_stop(void);
uint8_t snd_busy(void);          /* ファンファーレが鳴っている間だけ 1 */

/* 効果音。BGM は3声とも使っているので、効果音は ch C (ドラム) を一時的に奪って鳴らし、
   鳴り終わったらドラムへ戻す。優先度の低い音は、鳴っている音を止めない。 */
#define SFX_DIG  0       /* 土を掘る */
#define SFX_PICK 1       /* 火薬を拾う */
#define SFX_SET  2       /* ダイナマイトを置く */
#define SFX_BOOM 3       /* 爆発 */
#define SFX_MISS 4       /* ミス */
#define SFX_MINE 5       /* つるはしで黒ダイヤを叩く (キンキン) */
#define SFX_NOPE 6       /* 投げられないときの空振り (コツン) */
#define SFX_BREAK 7      /* 黒ダイヤが砕けた (ガシャン) */
#define SFX_COAL  8      /* 黒ダイヤを採掘した (チャリーン) */
#define SFX_CAVEIN 9     /* 落盤。盤面の岩が全部落ちる合図 (ゴゴゴ…) */
#define SFX_CLICK 10     /* 押している岩の中心が升の境目を越えた (カチッ) */
#define SFX_WOOD  11     /* 樋の板を岩が踏み抜いた (バキッ) */
/* **効果音の数。表の要素数と必ず一致させること。**
   入口の範囲判定をこれで書く。以前は `id > SFX_COAL` と特定の音の名前で
   書いてあり、あとから足した SFX_CAVEIN が**一度も鳴らないまま**になっていた。 */
#define SFX_N     12
void snd_sfx(uint8_t id);
/* 音程を n 半音ぶん上げて鳴らす。コンボで音が上がっていく演出に使う。 */
void snd_sfx_up(uint8_t id, uint8_t n);

/* 曲データ (music.c)。3チャンネルぶんの (音程, 長さ) の並び。
   音程 0=休符 / 0xFE=終わり / 0xFF=先頭へ戻る。長さはフレーム数。 */
extern const uint8_t bgm_a[], bgm_b[], bgm_c[];
extern const uint8_t title_a[], title_b[], title_c[];
extern const uint8_t fan_a[], fan_b[], fan_c[];
extern const uint8_t miss_a[], miss_b[], miss_c[];
extern const uint8_t sfx_dig[], sfx_pick[], sfx_set[], sfx_boom[], sfx_miss[], sfx_mine[], sfx_nope[], sfx_break[], sfx_coal[], sfx_cavein[], sfx_click[], sfx_wood[];

#endif
