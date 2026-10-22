/*
 * SakiyamaBlast - MSX-MUSIC (YM2413 / OPLL) を PSG に添える
 *
 * **曲データは1バイトも増やさない。** PSG の3声をそのまま FM で重ねるだけ
 * (作者の指示、2026-10-22)。旋律とベースを FM の音色で二重にし、PSG の
 * ノイズドラムと同じ拍で OPLL のリズム音源を叩く。
 *
 * **必ず検出してから鳴らす。** turbo R は MSX-MUSIC が規格で必須 (FS-A1ST /
 * FS-A1GT とも内蔵) だが、MSX2+ ではオプションで、載っていない機種がある。
 *
 *   ページ1 (0x4000) の 0x4018 に "APRLOPLL" … **内蔵 MSX-MUSIC**。
 *                                               I/O 0x7C/0x7D をそのまま叩いてよい
 *   同            0x4018 に "PAC2OPLL" … 外付け FM-PAC。I/O を使うには
 *                                        0x7FF6 の bit0 を立てる
 *
 * **内蔵が見つかったら 0x7FF6 には触らない。** 触ると Panasonic の MSX2+ で
 * 互換性が壊れる。**内蔵を先に探すこと。** 外付けだけを探す作りにすると、
 * 内蔵機では鳴らない。
 *
 * **書き込みにはウェイトが要る** (OPLL の仕様)。アドレス (0x7C) のあと 3.36us、
 * データ (0x7D) のあと 23.52us。R800 は速いので、入れないと音が化ける
 * (VDP へ OTIR を使わないのと同じ理由)。速いほうに合わせて空回りを入れてある。
 */
#include "game.h"
#include "msx.h"
#include "sound.h"
#include "config.h"

uint8_t opll_hw;        /* 0 = 無し / 1 = 内蔵 / 2 = 外付け FM-PAC */
uint8_t  rd_slot;       /* RDSLT の引数 (src/bios.s の rdslt_go) */
uint16_t rd_addr;
uint8_t opll_use;       /* いま FM を鳴らしてよいか (検出 && 設定) */

#define OPLL_ADDR_PORT 0x7C
#define OPLL_DATA_PORT 0x7D

static void out7c(uint8_t v) __z88dk_fastcall;
static void out7d(uint8_t v) __z88dk_fastcall;

void opll_w(uint8_t reg, uint8_t val)
{
    if (!opll_use) {
        return;
    }
    out7c(reg);
    out7d(val);
}

/* --- 検出 ---------------------------------------------------------- */

/* そのスロットのページ1 に署名があるか。*/
static uint8_t sig_at(uint8_t slot, uint16_t addr, const char *sig)
{
    uint8_t k;
    for (k = 0; k < 8; k++) {
        rd_slot = slot;
        rd_addr = (uint16_t)(addr + k);
        if (rdslt_go() != (uint8_t)sig[k]) {
            return 0;
        }
    }
    return 1;
}

/*
 * 全スロットを覗いて MSX-MUSIC を探す。**内蔵を先に**、見つからなければ外付け。
 * 起動時に1回だけ呼ぶ。
 */
void opll_detect(void)
{
    uint8_t pri, sub, slot, pass;
    opll_hw = 0;
    opll_use = 0;
    for (pass = 0; pass < 2 && !opll_hw; pass++) {
        for (pri = 0; pri < 4; pri++) {
            uint8_t ex = (uint8_t)(expbyte(pri) & 0x80);
            for (sub = 0; sub < (uint8_t)(ex ? 4 : 1); sub++) {
                slot = (uint8_t)(ex ? (0x80 | pri | (uint8_t)(sub << 2)) : pri);
                if (sig_at(slot, 0x4018, pass ? "PAC2OPLL" : "APRLOPLL")) {
                    opll_hw = (uint8_t)(pass ? 2 : 1);
                    break;
                }
            }
            if (opll_hw) {
                break;
            }
        }
    }
    if (opll_hw == 2) {
        /* 外付け FM-PAC は I/O を開けてやる必要がある。**内蔵のときは触らない。** */
        *(volatile uint8_t *)0x7FF6 = 0x01;
    }
    opll_apply();
}

#define RHY_REG   0x0E
#define RHY_ON    0x20       /* リズムモード。これだけだと全部 off */
#define RHY_BD    0x10
#define RHY_SD    0x08
#define RHY_HH    0x01

/* 設定 (FM SOUND) を反映する。載っていない機械では何をしても鳴らない。 */
void opll_apply(void)
{
    opll_use = (uint8_t)(opll_hw && cfg_fm);
    if (opll_use) {
        opll_reset();
    } else {
        /* 切ったら鳴っている音を消す。**opll_use を倒す前に書く** */
        uint8_t i;
        opll_use = 1;
        for (i = 0; i < 9; i++) {
            opll_w((uint8_t)(0x20 + i), 0);
        }
        opll_w(RHY_REG, RHY_ON);
        opll_use = 0;
    }
}

/* --- 音を出す ------------------------------------------------------ */

/*
 * 音程。OPLL は F-number と block で指定する。
 * f = fnum * 49716 / 2^19 * 2^(block-1) なので、fnum=172/block=1 が C0 (16.35Hz)。
 * 本作の音程番号は 1 = 32.7Hz なので block = 2 から始まる。
 */
static const uint16_t fnum[12] = {
    172, 183, 194, 205, 217, 230, 244, 258, 274, 290, 307, 326
};

/* 音色。OPLL の内蔵音色から選ぶ (1=バイオリン 2=ギター 3=ピアノ 4=フルート …)。
   **旋律はフルート**で篠笛に寄せ、**和音はギター**で三味線に寄せる。
   ベースはアコースティックベース。炭坑節と Down in a Coal Mine の両方に合う。 */
#define INST_MEL   4
#define INST_HARM  2
#define INST_BASS 14

/* 音量は 0 が最大で 15 が最小。**PSG より控えめにする** (添えるのが目的で、
   前に出すと PSG の旋律が負ける)。 */
#define VOL_MEL    4
#define VOL_HARM   8
#define VOL_BASS   5

static uint8_t rhy_t;        /* 叩いたリズムを落とすまでの残りフレーム */

void opll_reset(void)
{
    uint8_t i;
    if (!opll_use) {
        return;
    }
    for (i = 0; i < 9; i++) {
        opll_w((uint8_t)(0x20 + i), 0);          /* 全部キーオフ */
        opll_w((uint8_t)(0x30 + i), 0x0F);       /* 音量最小 */
    }
    opll_w(RHY_REG, RHY_ON);                     /* リズムモード (ch6,7,8 を占有) */
    opll_w(0x36, 0x02);                          /* バスドラの音量 */
    opll_w(0x37, 0x12);                          /* スネア / ハイハット */
    opll_w(0x38, 0x25);                          /* タム / シンバル (使わないので小さく) */
    rhy_t = 0;
}

/* ch へ音を置く。note は本作の音程番号 (1 = 32.7Hz)。0 は消音。 */
static void key(uint8_t ch, uint8_t note, uint8_t inst, uint8_t vol)
{
    uint16_t f;
    uint8_t blk;
    if (!opll_use) {
        return;
    }
    opll_w((uint8_t)(0x20 + ch), 0);             /* いったん切って打ち直す */
    if (!note) {
        return;
    }
    note--;
    blk = (uint8_t)(2 + note / 12);
    if (blk > 7) {
        blk = 7;
    }
    f = fnum[note % 12];
    opll_w((uint8_t)(0x30 + ch), (uint8_t)((inst << 4) | vol));
    opll_w((uint8_t)(0x10 + ch), (uint8_t)(f & 0xFF));
    opll_w((uint8_t)(0x20 + ch), (uint8_t)(0x30 | (uint8_t)(blk << 1) | (uint8_t)(f >> 8)));
}

/* PSG の3声がそのまま届く。ch 0 = 旋律 / 1 = ベース / 2 = 和音。 */
void opll_note(uint8_t ch, uint8_t note)
{
    if (ch == 0) {
        key(0, note, INST_MEL, VOL_MEL);
    } else if (ch == 1) {
        key(1, note, INST_BASS, VOL_BASS);
    } else {
        key(2, note, INST_HARM, VOL_HARM);
    }
}

/* PSG のノイズドラムと**同じ拍で**リズム音源を叩く。曲データは増やさない。 */
void opll_drum(uint8_t kind)
{
    uint8_t bits = RHY_ON;
    if (!opll_use) {
        return;
    }
    if (kind == 1) {
        bits |= RHY_BD;
    } else if (kind == 2) {
        bits |= RHY_SD;
    } else {
        bits |= RHY_HH;
    }
    opll_w(RHY_REG, RHY_ON);                     /* 前の打ちを落とす */
    opll_w(RHY_REG, bits);
    rhy_t = 3;
}

/* 毎フレーム呼ぶ。叩いたリズムを少し後で落とす (鳴りっぱなしにしない)。 */
void opll_tick(void)
{
    if (rhy_t && !--rhy_t) {
        opll_w(RHY_REG, RHY_ON);
    }
}

void opll_all_off(void)
{
    uint8_t i;
    if (!opll_use) {
        return;
    }
    for (i = 0; i < 3; i++) {
        opll_w((uint8_t)(0x20 + i), 0);
    }
    opll_w(RHY_REG, RHY_ON);
}

/*
 * **ウェイトつきの書き込み。** OPLL はアドレスの後に 3.36us、データの後に
 * 23.52us 待つ必要がある。R800 (7.16MHz 相当) で足りる空回りを入れてある。
 */
static void out7c(uint8_t v) __z88dk_fastcall
{
    v;
__asm
    ld      a, l
    out     (#0x7C), a
    ld      b, #6
00001$:
    djnz    00001$
__endasm;
}

static void out7d(uint8_t v) __z88dk_fastcall
{
    v;
__asm
    ld      a, l
    out     (#0x7D), a
    ld      b, #40
00002$:
    djnz    00002$
__endasm;
}
