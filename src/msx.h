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
/* 他スロットを1バイト覗く (BIOS の RDSLT)。**引数は変数で渡す** —
   SDCC は --sdcccall 1 で最初の引数をレジスタに載せるので、asm 側で
   スタックから取る書き方にすると黙って別の値を読む。 */
extern uint8_t  rd_slot;
extern uint16_t rd_addr;
uint8_t rdslt_go(void);
uint8_t expbyte(uint8_t i) __z88dk_fastcall; /* EXPTBL[i]。bit7 = 拡張スロット */
uint8_t gtstck(uint8_t dev) __z88dk_fastcall;
uint8_t gttrig(uint8_t dev) __z88dk_fastcall;
uint8_t snsmat(uint8_t row) __z88dk_fastcall;   /* キーマトリクス1行 (0=押下) */
/* 機種。0=MSX1 / 1=MSX2 / 2=MSX2+ / 3=turbo R */
uint8_t msxver(void);
/* turbo R の CPU 切り替え。**turbo R 以外で呼んではいけない** (0x0180 が別の意味になる)。
   mode: 0=Z80 / 1=R800 ROM / 2=R800 DRAM。0x80 を足すと本体の LED も変わる。 */
#define CPU_Z80    0
#define CPU_R800   1
#define CPU_R800D  2
void    chgcpu(uint8_t mode) __z88dk_fastcall;
uint8_t getcpu(void);

/* ROM データ窓 (0xA000-0xBFFF) のセグメント切り替え */
void bank_select(uint8_t seg) __z88dk_fastcall;
#define BANK_WINDOW ((const uint8_t *)0xA000)
/* 0xA000 の窓の既定の中身。絵 (art.c) と曲 (music.c) がここへリンクされている。
   タイトル画像を読むときだけ一時的に切り替え、読み終わったら必ずここへ戻すこと。 */
#define DATA_SEG 3
/* **RAM へ写して実行するコードを置いた ROM セグメント。**
   0x4000-0xBFFF の 32KB が埋まりきったので、はみ出したモジュール (Makefile の RAM_SRC) は
   起動時に 0xCD00 へ写して RAM で走らせる。写す手順は main() の冒頭。
   RAM (ページ3) は変数が 0xC000 から約3.2KB (0xCC8C まで)、**スタックは 0xF380 から
   0xF30F までしか伸びない** (openMSX で実測。タイトル・面の組み立て・3分半の実プレイで
   0xEF60 以降を 0xA5 で埋めてから最低到達点を読んだ)。0xCD00-0xF1FF が空いている。
   **0xD000 -> 0xCE00 -> 0xCD00 と下げ、上も 0xEDFF -> 0xEFFF -> 0xF1FF へ伸ばした
   (2026-09-26)。** 変数との間の余裕は約110バイト、スタックとの間は271バイト。
   変数が伸びて食い込んだら tools/ihx2rom.py が止める。 */
/* **絵は自前の ROM セグメントに置く。** 0xA000 の窓は 8KB しかなく、絵 (3.2KB) で
   埋めると他のモジュールを置けなくなる。タイトル画像と同じやり方で、読むときだけ
   窓をこのセグメントへ向け、**必ず DATA_SEG へ戻す** (src/sprite.c と draw_tile)。
   実体は tools/png2art.py が build/tiles.bin / build/sprites.bin に書き出し、
   tools/ihx2rom.py が ROM のこのセグメントへ入れる。 */
#define ART_TILE_SEG 10        /* 背景タイル。1枚128バイト、先頭から順に */
#define ART_SPR_SEG  11        /* スプライト: 形 (1枚32バイト) のあとに色 (1枚16バイト) */

extern uint8_t bank_cur;   /* いま窓が向いているセグメント (bank_select が控える) */
/* **2枚目の窓 (0x8000-0x9FFF)。** 同じ 0x8000 へリンクした領域を入れ替えるため。
   いまはまだ誰も入れ替えておらず、起動時に WIN2_PLAY_SEG を選んだままにしてある。
   仕組みは tools/ihx2rom.py の WIN2_BASE の説明を参照。 */
void    win2_select(uint8_t seg) __z88dk_fastcall;
extern uint8_t win2_cur;
#define WIN2_PLAY_SEG 2    /* 0x8000 の1枚目 (ihx2rom.py の WIN2_SEGS と揃える) */
#define TAIKO_SEG 12
#define KANJI_SEG 13
#define MUSIC_SEG 14   /* 曲データ (src/sound.c が読む前に窓を向ける) */   /* 漢字の字形 (src/kanji.c が blob_read で読む) */
/* **窓の中の表を RAM へ写す** (実体は src/lowlevel.s)。引数は変数で渡す。
   窓側のコードから呼んでよい (切り替えている間に走るのは固定領域のルーチンだけ)。 */
extern uint8_t  blob_seg;
extern uint16_t blob_off;    /* 窓の中の番地 (0xA000-0xBFFF) */
extern uint8_t *blob_dst;
extern uint8_t  blob_len;
void blob_read(void);   /* 和太鼓の波形 (src/lowlevel.s の TAIKO_SEG と合わせる) */
#define RAMCODE_SEG 8
#define RAMCODE_ADDR 0xC9A0
/* 写す量。**リンカの l_RAMCODE は C から参照できない**ので、領域の大きさを
   決め打ちで写す。0xCD00-0xF1FF の 9472 バイト = tools/ihx2rom.py の
   RAMCODE_BASE..RAMCODE_END と一致させること。余りは 0xFF で埋まっている。
   **8KB を超えるので ROM のセグメントは2枚 (RAMCODE_SEG と その次)。**
   写す手順も2回に分かれる (main() の冒頭)。 */
#define RAMCODE_LEN 0x2900

#endif
