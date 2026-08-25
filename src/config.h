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

void    config_init(void);
uint8_t konami_check(void);      /* タイトルで毎フレーム呼ぶ。揃ったら1 */
void    config_enter(void);      /* 設定画面を描く */

#define CFG_STAY   0
#define CFG_START  1             /* ゲームを始める */
#define CFG_ENDING 2             /* エンディングを見る */
uint8_t config_update(void);     /* 設定画面で毎フレーム呼ぶ */

#endif
