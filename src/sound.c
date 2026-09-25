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

/* 音程テーブル。period = 1789772.5 / (16 * 周波数)。表記は実音より1オクターブ上の名。
   B6 (約990Hz) までだと効果音に使える上限が低すぎるので B8 (約4kHz) まで伸ばした。
   **index 1 = 32.7Hz の対応は変えていない** (変えると既存の曲が全部ずれる)。 */
static const uint16_t note_period[85] = {
    0,                        /* 0 = 休符 */
     3420, 3229, 3047, 2876, /* C2   Cs2  D2   Ds2  */
     2715, 2562, 2419, 2283, /* E2   F2   Fs2  G2   */
     2155, 2034, 1920, 1812, /* Gs2  A2   As2  B2   */
     1710, 1614, 1524, 1438, /* C3   Cs3  D3   Ds3  */
     1357, 1281, 1209, 1141, /* E3   F3   Fs3  G3   */
     1077, 1017,  960,  906, /* Gs3  A3   As3  B3   */
      855,  807,  762,  719, /* C4   Cs4  D4   Ds4  */
      679,  641,  605,  571, /* E4   F4   Fs4  G4   */
      539,  508,  480,  453, /* Gs4  A4   As4  B4   */
      428,  404,  381,  360, /* C5   Cs5  D5   Ds5  */
      339,  320,  302,  285, /* E5   F5   Fs5  G5   */
      269,  254,  240,  226, /* Gs5  A5   As5  B5   */
      214,  202,  190,  180, /* C6   Cs6  D6   Ds6  */
      170,  160,  151,  143, /* E6   F6   Fs6  G6   */
      135,  127,  120,  113, /* Gs6  A6   As6  B6   */
      107,  101,   95,   90, /* C7   Cs7  D7   Ds7  */
       85,   80,   76,   71, /* E7   F7   Fs7  G7   */
       67,   64,   60,   57, /* Gs7  A7   As7  B7   */
       53,   50,   48,   45, /* C8   Cs8  D8   Ds8  */
       42,   40,   38,   36, /* E8   F8   Fs8  G8   */
       34,   32,   30,   28  /* Gs8  A8   As8  B8   */
};

static const uint8_t *ch_base[3];
static const uint8_t *ch_p[3];
static uint8_t ch_left[3];
static uint8_t ch_t[3];
static uint8_t ch_note[3];
static uint8_t ch_vol0[3];
static uint8_t ch_dec[3];      /* 音量の減衰速度 (右シフト量。小さいほど速く減衰) */
static uint8_t ch_lastvol[3];  /* 直前に書いた音量。変わっていなければ書かない */
static uint8_t mode;
uint8_t snd_tempo = TEMPO_UNIT;   /* 曲のテンポ。TEMPO_UNIT で等倍 */
static uint8_t tempo_acc;         /* テンポの端数 */
static uint8_t mixer_base;
static uint8_t drum_on;        /* 1 = チャンネルC をノイズのドラムとして使う */

/* 効果音の状態。ch C を一時的に奪う。 */
static const uint8_t *sfx_p;
static uint8_t sfx_left, sfx_t, sfx_vol0, sfx_dec, sfx_noise, sfx_pri, sfx_on;
static uint8_t sfx_up;          /* 音程を持ち上げる半音数 (コンボ用) */

/* 効果音の定義: データ / ノイズか / 音量 / 減衰 / 優先度 */
static const uint8_t *const sfx_data[SFX_N] = { sfx_dig, sfx_pick, sfx_set, sfx_boom, sfx_miss, sfx_mine, sfx_nope, sfx_break, sfx_coal, sfx_cavein, sfx_click, sfx_wood, sfx_gush, sfx_drip, sfx_seal };
/* 0=トーン 1=ノイズ 2=トーン+ノイズ (金属音) */
static const uint8_t sfx_isnoise[SFX_N] = {       1,        0,       0,        1,        0,        2,        0,        1,        0,         1,         0,         1,        1,        0, 1 };
static const uint8_t sfx_volume[SFX_N]  = {       8,       13,      11,       15,       14,       15,        9,       15,       15,        15,        10,        14,       14,        9, 12 };
/* 大きいほど減衰が遅い。0 は1フレームに1ずつ落ちる = 打撃音向き。 */
static const uint8_t sfx_decay[SFX_N]   = {       1,        3,       2,        3,        3,        0,        4,        2,        1,         4,         0,         0, 1,        0, 3 };
/* トーンに重ねるノイズの粗さ (モード2 のときだけ使う)。小さいほど高い。 */
static const uint8_t sfx_noisep[SFX_N]  = {       0,        0,       0,        0,        0,        1,        0,        0,        0,         0,         0,         0, 0,        0, 0 };
static const uint8_t sfx_prio[SFX_N]    = {       0,        1,       1,        2,        2,        1,        0,        2,        2,         3,         0,         2, 2,        0, 2 };

/* ノイズドラム。PSG のノイズ周期を変えて太鼓・スネア・ハイハットを作り分ける。 */
#define DRUM_BD 1
#define DRUM_SN 2
#define DRUM_HH 3
static const uint8_t drum_period[4] = { 0, 28, 12,  2 };   /* R6 (大きいほど低い) */
static const uint8_t drum_vol[4]    = { 0, 14, 13,  9 };
static const uint8_t drum_dec[4]    = { 0,  2,  2,  1 };

/* ch C をノイズ(ドラム/爆発)にするかトーンにするかを切り替える */
/*
 * ch C の出し方を切り替える。R7 は bit0-2 がトーン、bit3-5 がノイズの
 * 「切る」ビット (1 で切れる)。
 *   0 = トーンのみ      0x38 (ノイズ A/B/C を切る)
 *   1 = ノイズのみ      0x1C (トーン C とノイズ A/B を切る)
 *   2 = トーン+ノイズ   0x18 (ノイズ A/B だけ切る)
 * **PSG は矩形波固定で波形は変えられない。**金属音のような濁った音は、
 * 同じチャンネルにトーンとノイズを重ねて非調和成分を混ぜて作る。
 */
static void set_mixer(uint8_t mode)
{
    uint8_t m = 0x38;
    if (mode == 1) { m = 0x1C; }
    else if (mode == 2) { m = 0x18; }
    PSG_W(7, (uint8_t)(mixer_base | m));
}

void snd_init(void)
{
    uint8_t i;
    /* R7 の bit6/7 はポートの入出力方向 (MSX ではジョイスティック等に使う)。
       勝手に変えると BIOS の入力が壊れるので、現在値を保って下位だけ触る。 */
    mixer_base = (uint8_t)(psg_read(7) & 0xC0);
    PSG_W(7, mixer_base | 0x38);      /* トーン3本を有効、ノイズは無効 */
    drum_on = 0;
    sfx_on = 0;
    for (i = 0; i < 3; i++) {
        PSG_W(8 + i, 0);
    }
    mode = SND_OFF;
}

void snd_sfx_up(uint8_t id, uint8_t n)
{
    snd_sfx(id);
    if (sfx_on) {
        sfx_up = n;
    }
}

void snd_sfx(uint8_t id)
{
    if (id >= SFX_N) {
        return;
    }
    if (sfx_on && sfx_prio[id] < sfx_pri) {
        return;                        /* 鳴っている音より優先度が低ければ捨てる */
    }
    sfx_p = sfx_data[id];
    sfx_left = 0;
    sfx_t = 0;
    sfx_vol0 = sfx_volume[id];
    sfx_dec = sfx_decay[id];
    sfx_noise = sfx_isnoise[id];
    sfx_up = 0;
    if (sfx_noise == 2) {
        PSG_W(6, sfx_noisep[id]);      /* 重ねるノイズの粗さは固定 */
    }
    sfx_pri = sfx_prio[id];
    sfx_on = 1;
    set_mixer(sfx_noise);
}

/* 効果音の1ティック。ch C のレジスタを直接書く。 */
static void sfx_tick(void)
{
    uint8_t v, d;
    if (!sfx_on) {
        return;
    }
    if (sfx_left == 0) {
        uint8_t val = *sfx_p++;
        /* 0xFD = 出し方の切り替え。次の1バイトが 0=トーン 1=ノイズ 2=トーン+ノイズ。
           打撃音は「低いノイズのアタック → 高いトーンの余韻」で作りたいので、
           音の途中で切り替えられるようにしてある。 */
        if (val == 0xFD) {
            sfx_noise = *sfx_p++;
            set_mixer(sfx_noise);
            val = *sfx_p++;
        }
        if (val == 0xFE) {
            sfx_on = 0;
            PSG_W(10, 0);
            set_mixer(drum_on);        /* ドラムへ戻す */
            if (drum_on) {
                PSG_W(6, drum_period[DRUM_BD]);
            }
            return;
        }
        sfx_left = *sfx_p++;
        sfx_t = 0;
        if (sfx_noise == 1) {
            PSG_W(6, val);             /* ノイズ周期 */
        } else {
            uint16_t per;
            uint8_t k = (uint8_t)(val + sfx_up);
            if (k > 84) { k = 84; }              /* 表の上限で頭打ち */
            per = note_period[k];
            PSG_W(4, (uint8_t)(per & 0xFF));
            PSG_W(5, (uint8_t)(per >> 8));
        }
    }
    d = (uint8_t)(sfx_t >> sfx_dec);
    v = (uint8_t)((d >= sfx_vol0) ? 0 : (sfx_vol0 - d));
    PSG_W(10, v);
    if (sfx_t < 255) {
        sfx_t++;
    }
    sfx_left--;
}

static void snd_start(const uint8_t *a, const uint8_t *b, const uint8_t *c, uint8_t m)
{
    snd_tempo = TEMPO_UNIT;      /* タイトルやファンファーレは常に等倍 */
    tempo_acc = 0;
    ch_base[0] = ch_p[0] = a;
    ch_base[1] = ch_p[1] = b;
    ch_base[2] = ch_p[2] = c;
    ch_left[0] = ch_left[1] = ch_left[2] = 0;
    ch_t[0] = ch_t[1] = ch_t[2] = 0;
    ch_note[0] = ch_note[1] = ch_note[2] = 0;
    ch_lastvol[0] = ch_lastvol[1] = ch_lastvol[2] = 0xFF;
    mode = m;
}

/* 旋律 + ベース + ノイズドラムの3声で曲を鳴らす。坑内の曲もタイトルの曲も同じ形。 */
static void play_song(const uint8_t *a, const uint8_t *b, const uint8_t *c)
{
    ch_vol0[0] = 13;      /* 主旋律 */
    ch_vol0[1] = 11;      /* ベース */
    ch_vol0[2] = 13;      /* ドラム (音ごとに上書きする) */
    ch_dec[0] = 3;
    ch_dec[1] = 3;
    ch_dec[2] = 2;
    drum_on = 1;
    if (!sfx_on) {
        set_mixer(1);                  /* トーン A/B のみ、ノイズは C のみ */
        PSG_W(6, drum_period[DRUM_BD]);
    }
    snd_start(a, b, c, SND_BGM);
}

void snd_play_bgm(void)
{
    play_song(bgm_a, bgm_b, bgm_c);
}

void snd_play_title(void)
{
    play_song(title_a, title_b, title_c);
}

/* ファンファーレやミスのジングル。BGM を止めてトーン3声で鳴らす。 */
static void play_jingle(const uint8_t *a, const uint8_t *b, const uint8_t *c,
                        uint8_t v0, uint8_t v1, uint8_t v2)
{
    ch_vol0[0] = v0;
    ch_vol0[1] = v1;
    ch_vol0[2] = v2;
    ch_dec[0] = ch_dec[1] = ch_dec[2] = 3;
    drum_on = 0;
    sfx_on = 0;                        /* 鳴っている間は効果音を止める */
    set_mixer(0);                      /* トーン3声 */
    snd_start(a, b, c, SND_SFX);
}

void snd_play_fanfare(void)
{
    play_jingle(fan_a, fan_b, fan_c, 14, 11, 11);
}

void snd_play_miss(void)
{
    play_jingle(miss_a, miss_b, miss_c, 14, 11, 11);
}

void snd_play_hurry(void)
{
    play_jingle(hurry_a, hurry_b, hurry_c, 14, 11, 11);
}

/* 何も鳴っていない (曲が終わって止まっている)。合図の後に曲を戻す判断に使う。 */
uint8_t snd_idle(void)
{
    return (uint8_t)(mode == SND_OFF);
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

/* 曲を1ステップ進める。全チャンネルが終わっていたら1を返す。
   **テンポを上げるときは、これを1フレームに複数回呼ぶ。**
   音長 (ch_left) も減衰 (ch_t) も一緒に進むので、速くしても音は破綻しない。 */
static uint8_t music_step(void)
{
    uint8_t i, note, ended = 0;
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
                    ch_vol0[2] = drum_vol[note];
                    ch_dec[2] = drum_dec[note];
                    if (!sfx_on) {
                        PSG_W(6, drum_period[note]);
                    }
                }
            } else if (note && !(i == 2 && sfx_on)) {
                uint16_t per = note_period[note];
                PSG_W(i << 1, (uint8_t)(per & 0xFF));
                PSG_W((i << 1) + 1, (uint8_t)(per >> 8));
            }
        }
        if (!(i == 2 && sfx_on)) {         /* 効果音が ch C を使っている間は触らない */
            uint8_t v = (uint8_t)(ch_note[i] ? ch_vol0[i] : 0);
            uint8_t d = (uint8_t)(ch_t[i] >> ch_dec[i]);
            v = (uint8_t)((d >= v) ? 0 : (v - d));
            if (v != ch_lastvol[i]) {      /* 同じ音量を毎フレーム書き直さない */
                ch_lastvol[i] = v;
                PSG_W(8 + i, v);
            }
        }
        if (ch_t[i] < 255) {
            ch_t[i]++;
        }
        ch_left[i]--;
    }
    return (uint8_t)(ended == 3);
}

/*
 * **体温が上がるほど曲を速くする。**
 *
 * 体温は本作の制限時間そのものだが、表現が16目盛りのメーターしかなかった。
 * テンポを上げると、**プレイ時間の全秒に緊張の勾配が付く**。水風呂で冷えれば
 * 戻るので、そのまま解放の合図にもなる。新しい音は1つも要らない。
 *
 * 1ティックぶんの進みを TEMPO_UNIT 分の n で数え、溜まったぶんだけ music_step()
 * を回す。**小節の頭を待つ必要はない。** 音長を速く消化するだけなので、
 * どこで倍率が変わっても曲は繋がったまま速くなる。
 */
void snd_set_tempo(uint8_t n)
{
    snd_tempo = n;
}

void snd_tick(void)
{
    if (mode == SND_OFF) {
        sfx_tick();
        return;
    }
    tempo_acc = (uint8_t)(tempo_acc + snd_tempo);
    while (tempo_acc >= TEMPO_UNIT) {
        tempo_acc = (uint8_t)(tempo_acc - TEMPO_UNIT);
        if (music_step()) {
            mode = SND_OFF;
            break;
        }
    }
    sfx_tick();
}
