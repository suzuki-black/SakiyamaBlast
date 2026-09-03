/* SakiyamaBlast - MSX 環境の基本定義 */
#ifndef MSX_H
#define MSX_H

#ifdef HOSTSIM
/* ホスト計測ビルド (tools/hostsim) では本物の 8/16bit 型を使う。
   MSX 側は int が16bitなので、ここを合わせないと桁あふれの挙動が変わり、
   シミュレーション結果が実機と食い違ってしまう。 */
#include <stdint.h>
#else
typedef unsigned char  uint8_t;
typedef signed char    int8_t;
typedef unsigned int   uint16_t;
typedef signed int     int16_t;
#endif

/* BIOS ワークエリア */
#define JIFFY   (*(volatile uint8_t *)0xFC9E)   /* 割り込みごとに増える (下位だけ見れば足りる) */
#define RG9SAV  (*(volatile uint8_t *)0xFFE8)   /* VDP R#9 のミラー (PAL/NTSC ビット保存用) */

/* 入力 (BIOS) : 0=カーソルキー, 1/2=ジョイスティック */
uint8_t gtstck(uint8_t dev) __z88dk_fastcall;
uint8_t gttrig(uint8_t dev) __z88dk_fastcall;
uint8_t snsmat(uint8_t row) __z88dk_fastcall;   /* キーマトリクス1行 (0=押下) */

/* ROM データ窓 (0xA000-0xBFFF) のセグメント切り替え */
void bank_select(uint8_t seg) __z88dk_fastcall;
#define BANK_WINDOW ((const uint8_t *)0xA000)
/* 0xA000 の窓の既定の中身。絵 (art.c) と曲 (music.c) がここへリンクされている。
   タイトル画像を読むときだけ一時的に切り替え、読み終わったら必ずここへ戻すこと。 */
#define DATA_SEG 3
/* **RAM へ写して実行するコードを置いた ROM セグメント。**
   0x4000-0xBFFF の 32KB が埋まりきったので、はみ出したモジュール (enemy / rock) は
   起動時に 0xD000 へ写して RAM で走らせる。写す手順は main() の冒頭。
   RAM (ページ3) は変数が 0xC000 から約2.5KB、スタックが 0xF380 から下へ約350バイト
   (openMSX で実測) しか使っておらず、0xD000-0xEDFF が丸ごと空いている。 */
#define RAMCODE_SEG 8
#define RAMCODE_ADDR 0xD000
/* 写す量。**リンカの l_RAMCODE は C から参照できない**ので、領域の大きさを
   決め打ちで写す。0xD000-0xEDFF の 7680 バイト = tools/ihx2rom.py の
   RAMCODE_BASE..RAMCODE_END と一致させること。余りは 0xFF で埋まっている。 */
#define RAMCODE_LEN 0x1E00

#endif
