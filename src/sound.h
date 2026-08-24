/* SakiyamaBlast - PSG (AY-3-8910) の音楽ドライバ */
#ifndef SOUND_H
#define SOUND_H

#include "msx.h"

void    snd_init(void);
void    snd_tick(void);          /* 毎フレーム1回呼ぶ */
void    snd_play_bgm(void);
void    snd_play_fanfare(void);
void    snd_stop(void);
uint8_t snd_busy(void);          /* ファンファーレが鳴っている間だけ 1 */

/* 曲データ (music.c)。3チャンネルぶんの (音程, 長さ) の並び。
   音程 0=休符 / 0xFE=終わり / 0xFF=先頭へ戻る。長さはフレーム数。 */
extern const uint8_t bgm_a[], bgm_b[], bgm_c[];
extern const uint8_t fan_a[], fan_b[], fan_c[];

#endif
