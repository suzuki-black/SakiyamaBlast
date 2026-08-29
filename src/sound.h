/* SakiyamaBlast - PSG (AY-3-8910) の音楽ドライバ */
#ifndef SOUND_H
#define SOUND_H

#include "msx.h"

void    snd_init(void);
void    snd_tick(void);          /* 毎フレーム1回呼ぶ */
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
void snd_sfx(uint8_t id);

/* 曲データ (music.c)。3チャンネルぶんの (音程, 長さ) の並び。
   音程 0=休符 / 0xFE=終わり / 0xFF=先頭へ戻る。長さはフレーム数。 */
extern const uint8_t bgm_a[], bgm_b[], bgm_c[];
extern const uint8_t title_a[], title_b[], title_c[];
extern const uint8_t fan_a[], fan_b[], fan_c[];
extern const uint8_t miss_a[], miss_b[], miss_c[];
extern const uint8_t sfx_dig[], sfx_pick[], sfx_set[], sfx_boom[], sfx_miss[], sfx_mine[], sfx_nope[], sfx_break[];

#endif
