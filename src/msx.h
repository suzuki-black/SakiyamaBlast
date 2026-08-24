/* SakiyamaBlast - MSX 環境の基本定義 */
#ifndef MSX_H
#define MSX_H

typedef unsigned char  uint8_t;
typedef signed char    int8_t;
typedef unsigned int   uint16_t;
typedef signed int     int16_t;

/* BIOS ワークエリア */
#define JIFFY   (*(volatile uint8_t *)0xFC9E)   /* 割り込みごとに増える (下位だけ見れば足りる) */
#define RG9SAV  (*(volatile uint8_t *)0xFFE8)   /* VDP R#9 のミラー (PAL/NTSC ビット保存用) */

/* 入力 (BIOS) : 0=カーソルキー, 1/2=ジョイスティック */
uint8_t gtstck(uint8_t dev) __z88dk_fastcall;
uint8_t gttrig(uint8_t dev) __z88dk_fastcall;

/* ROM データ窓 (0xA000-0xBFFF) のセグメント切り替え */
void bank_select(uint8_t seg) __z88dk_fastcall;
#define BANK_WINDOW ((const uint8_t *)0xA000)

#endif
