/*
 * SakiyamaBlast - 隠しコンフィグ
 *
 * タイトル画面で「上上下下左右左右BA」を入れると設定画面に入る。
 * 設定は電源を切るまで保持する（ROM なので保存はしない）。
 */
#ifndef CONFIG_H
#define CONFIG_H

#include "msx.h"

extern uint8_t cfg_lives;        /* 残機数 1..9 */
extern uint8_t cfg_continue;     /* コンティニュー可否 0/1 */
extern uint8_t cfg_invincible;   /* 無敵 0/1 (動作確認用) */
extern uint8_t cfg_start_stage;  /* 開始面 1..MAX_STAGE */
extern uint8_t cfg_reset;        /* 面リセット 0/1。ON なら遊んでいる最中に R で面をやり直せる */
/* 陽炎 0/1。体温が危険域のとき盤面をラスタごとに揺らす。
   **掘っているときの fps が 55 -> 44 に落ちる**ので切れるようにしてある。 */
extern uint8_t cfg_haze;
extern uint8_t cfg_fm;
extern uint8_t cfg_pcm;          /* 和太鼓 (PCM) を鳴らすか 0/1 */           /* FM 音源 (MSX-MUSIC) を添えるか 0/1 */

void    config_init(void);
uint8_t konami_check(void);      /* タイトルで毎フレーム呼ぶ。揃ったら1 */
void    config_enter(void);      /* 設定画面を描く */
/* 遊んでいる最中に毎フレーム呼ぶ。R を押した瞬間だけ1 (cfg_reset が OFF なら常に0)。 */
uint8_t reset_pressed(void);

#define CFG_STAY   0
#define CFG_START  1             /* ゲームを始める */
#define CFG_ENDING 2             /* エンディングを見る */
uint8_t config_update(void);     /* 設定画面で毎フレーム呼ぶ */

#endif
