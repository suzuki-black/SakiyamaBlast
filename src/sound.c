/*
 * SakiyamaBlast - PSG 音楽ドライバ
 *
 * MSX 標準の PSG (AY-3-8910) を直接叩く。3本ともトーンに使い、ノイズは使わない。
 * 曲は「(音程, 長さ) の並び」を3本ぶん、毎フレーム1ティックずつ進めるだけの単純な方式。
 * BGM は 0xFF でループ、効果音 (ファンファーレ) は 0xFE で終わる。
 */
#include "sound.h"
#include "vdp.h"

void psg_write(uint16_t rv) __z88dk_fastcall;
uint8_t psg_read(uint8_t r) __z88dk_fastcall;

#define PSG_W(r, v) psg_write((uint16_t)(((uint16_t)(uint8_t)(v) << 8) | (uint8_t)(r)))

#define SND_OFF 0
#define SND_BGM 1
#define SND_SFX 2

/* 音程テーブル (C2..B6)。period = 1789772.5 / (16 * 周波数) */
static const uint16_t note_period[61] = {
    0,                        /* 0 = 休符 */
    3420, 3229, 3047, 2876,   /* C2   Cs2  D2   Ds2  */
    2715, 2562, 2419, 2283,   /* E2   F2   Fs2  G2   */
    2155, 2034, 1920, 1812,   /* Gs2  A2   As2  B2   */
    1710, 1614, 1524, 1438,   /* C3   Cs3  D3   Ds3  */
    1357, 1281, 1209, 1141,   /* E3   F3   Fs3  G3   */
    1077, 1017,  960,  906,   /* Gs3  A3   As3  B3   */
     855,  807,  762,  719,   /* C4   Cs4  D4   Ds4  */
     679,  641,  605,  571,   /* E4   F4   Fs4  G4   */
     539,  508,  480,  453,   /* Gs4  A4   As4  B4   */
     428,  404,  381,  360,   /* C5   Cs5  D5   Ds5  */
     339,  320,  302,  285,   /* E5   F5   Fs5  G5   */
     269,  254,  240,  226,   /* Gs5  A5   As5  B5   */
     214,  202,  190,  180,   /* C6   Cs6  D6   Ds6  */
     170,  160,  151,  143,   /* E6   F6   Fs6  G6   */
     135,  127,  120,  113    /* Gs6  A6   As6  B6   */
};

static const uint8_t *ch_base[3];
static const uint8_t *ch_p[3];
static uint8_t ch_left[3];
static uint8_t ch_t[3];
static uint8_t ch_note[3];
static uint8_t ch_vol0[3];
static uint8_t ch_dec[3];      /* 音量の減衰速度 (右シフト量。小さいほど速く減衰) */
static uint8_t mode;
static uint8_t mixer_base;
static uint8_t drum_on;        /* 1 = チャンネルC をノイズのドラムとして使う */

/* ノイズドラム。PSG のノイズ周期を変えて太鼓・スネア・ハイハットを作り分ける。 */
#define DRUM_BD 1
#define DRUM_SN 2
#define DRUM_HH 3
static const uint8_t drum_period[4] = { 0, 28, 12,  2 };   /* R6 (大きいほど低い) */
static const uint8_t drum_vol[4]    = { 0, 14, 13,  9 };
static const uint8_t drum_dec[4]    = { 0,  2,  2,  1 };

void snd_init(void)
{
    uint8_t i;
    /* R7 の bit6/7 はポートの入出力方向 (MSX ではジョイスティック等に使う)。
       勝手に変えると BIOS の入力が壊れるので、現在値を保って下位だけ触る。 */
    mixer_base = (uint8_t)(psg_read(7) & 0xC0);
    PSG_W(7, mixer_base | 0x38);      /* トーン3本を有効、ノイズは無効 */
    drum_on = 0;
    for (i = 0; i < 3; i++) {
        PSG_W(8 + i, 0);
    }
    mode = SND_OFF;
}

static void snd_start(const uint8_t *a, const uint8_t *b, const uint8_t *c, uint8_t m)
{
    ch_base[0] = ch_p[0] = a;
    ch_base[1] = ch_p[1] = b;
    ch_base[2] = ch_p[2] = c;
    ch_left[0] = ch_left[1] = ch_left[2] = 0;
    ch_t[0] = ch_t[1] = ch_t[2] = 0;
    ch_note[0] = ch_note[1] = ch_note[2] = 0;
    mode = m;
}

void snd_play_bgm(void)
{
    ch_vol0[0] = 13;      /* 主旋律 */
    ch_vol0[1] = 11;      /* ベース */
    ch_vol0[2] = 13;      /* ドラム (音ごとに上書きする) */
    ch_dec[0] = 3;
    ch_dec[1] = 3;
    ch_dec[2] = 2;
    drum_on = 1;
    /* トーン A/B のみ、ノイズは C のみ */
    PSG_W(7, mixer_base | 0x1C);
    PSG_W(6, drum_period[DRUM_BD]);
    snd_start(bgm_a, bgm_b, bgm_c, SND_BGM);
}

void snd_play_fanfare(void)
{
    ch_vol0[0] = 14;
    ch_vol0[1] = 11;
    ch_vol0[2] = 11;
    ch_dec[0] = ch_dec[1] = ch_dec[2] = 3;
    drum_on = 0;
    PSG_W(7, mixer_base | 0x38);      /* ファンファーレはトーン3声 */
    snd_start(fan_a, fan_b, fan_c, SND_SFX);
}

void snd_stop(void)
{
    uint8_t i;
    mode = SND_OFF;
    for (i = 0; i < 3; i++) {
        PSG_W(8 + i, 0);
    }
}

uint8_t snd_busy(void)
{
    return (uint8_t)(mode == SND_SFX);
}

void snd_tick(void)
{
    uint8_t i, note, ended = 0;
    if (mode == SND_OFF) {
        return;
    }
    for (i = 0; i < 3; i++) {
        if (ch_left[i] == 0) {
            note = *ch_p[i]++;
            if (note == 0xFF) {                 /* ループ */
                ch_p[i] = ch_base[i];
                note = *ch_p[i]++;
            }
            if (note == 0xFE) {                 /* 終わり */
                ch_p[i]--;
                ch_note[i] = 0;
                PSG_W(8 + i, 0);
                ended++;
                continue;
            }
            ch_left[i] = *ch_p[i]++;
            ch_t[i] = 0;
            ch_note[i] = note;
            if (drum_on && i == 2) {
                if (note) {                       /* ドラムは音程でなく打種 */
                    PSG_W(6, drum_period[note]);
                    ch_vol0[2] = drum_vol[note];
                    ch_dec[2] = drum_dec[note];
                }
            } else if (note) {
                uint16_t per = note_period[note];
                PSG_W(i << 1, (uint8_t)(per & 0xFF));
                PSG_W((i << 1) + 1, (uint8_t)(per >> 8));
            }
        }
        {
            uint8_t v = (uint8_t)(ch_note[i] ? ch_vol0[i] : 0);
            uint8_t d = (uint8_t)(ch_t[i] >> ch_dec[i]);
            PSG_W(8 + i, (uint8_t)((d >= v) ? 0 : (v - d)));
        }
        if (ch_t[i] < 255) {
            ch_t[i]++;
        }
        ch_left[i]--;
    }
    if (ended == 3) {
        mode = SND_OFF;
    }
}
