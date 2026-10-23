/*
 * SakiyamaBlast - turbo R 内蔵 PCM で和太鼓を鳴らす
 *
 * **ドラムの拍に和太鼓を重ねる** (作者の指示、2026-10-22)。PSG のノイズドラムと
 * FM のリズム音源はそのまま鳴らし、その上に PCM の太鼓を足す。
 *
 * ポート:
 *   A4h 書込 … サンプル値 (8bit 符号なし、中心 0x80)
 *   A5h 書込 … bit0 BUFF (1 = 次の刻みで D/A へ) / **bit1 MUTE (0 にすると全音源が消える)**
 *
 * **出すのは走査線割り込み。** ゲームの待ち時間に出す作りでは、面中に出す機会が
 * ほとんど無くて波形が階段になる (姉妹作の実測: 面中の8〜9割の時間は機会が無い。
 * `docs/PCM調査_2026-10-07.md`)。本作は**4行ごとに割り込んで1サンプルずつ**出す
 * ので **3,925Hz**。姉妹作と同じ刻みだが、**長さが違う** — あちらは 32ms のスネア、
 * こちらは 78ms の宮太鼓で、胴の鳴りが残る長さを取ってある。
 *
 * **2行ごと (7,850Hz) は出せなかった。** 割り込みの入口と VDP のポート待ちで
 * 1回 100µs 以上かかり、127µs の間隔に間に合わずに**1回おきに取りこぼして**
 * 実効 3,925Hz になっていた (録音した太鼓の基音が 80Hz ではなく 40Hz になって気付いた。
 * 2026-10-23)。取りこぼして揺れるより、間に合う間隔で等間隔に出すほうが音が良い。
 *
 * **波形は ROM のセグメントに置き、割り込みの中で窓を向け直して1バイト読む。**
 * RAM に 1KB の置き場が無いため。窓は読んだ直後に元へ戻すので、描画の途中で
 * 割り込んでも壊れない (`bank_select` が今の向き先を控えている)。
 *
 * **鳴っている間は陽炎 (haze) が止まる。** 割り込みは1本しか張れないので、
 * 太鼓が優先する。78ms のことなので目には見えない。
 */
#include "game.h"
#include "msx.h"
#include "vdp.h"
#include "sound.h"
#include "config.h"
#include "haze.h"
#include "taiko.h"

uint8_t  pcm_hw;        /* turbo R か (PCM があるのは turbo R だけ) */
uint8_t  pcm_use;       /* 検出 && 設定 */
uint8_t  pcm_play;      /* **割り込みが見る。** 0 以外 = 再生中 */
uint16_t pcm_src;       /* 窓の中の読み出し位置 (0xA000-0xBFFF) */
uint16_t pcm_left;      /* 残りバイト数 */
uint8_t  pcm_line;      /* 次に割り込ませる行 */

/* 太鼓の種類。sound.c のドラム (1=バスドラ 2=スネア 3=ハイハット) に対応させる。 */
static const uint16_t taiko_off[4] = { 0, TAIKO_DON_OFF, TAIKO_TAN_OFF, TAIKO_KA_OFF };
static const uint16_t taiko_len[4] = { 0, TAIKO_DON_LEN, TAIKO_TAN_LEN, TAIKO_KA_LEN };

void pcm_apply(void)
{
    pcm_use = (uint8_t)(pcm_hw && cfg_pcm);
    if (!pcm_use) {
        pcm_play = 0;
    }
}

void pcm_detect(void)
{
    /* **PCM は turbo R にしかない。** MSX2/2+ で A4h を叩いても何も鳴らないが、
       余計なポートを触らないよう機種を見てから使う。 */
    pcm_hw = (uint8_t)(msxver() >= 3);
    pcm_play = 0;
    pcm_apply();
}

/*
 * 太鼓を1発鳴らす。前の音が残っていても差し替える (連打はこれで切り替わる)。
 * **呼ぶのは本体ループから** (snd_tick 経由)。割り込みの中からは呼ばない。
 */
void pcm_hit(uint8_t kind)
{
    if (!pcm_use || kind == 0 || kind > 3) {
        return;
    }
    /* **陽炎のずらしを中立へ戻してから奪う。** 鳴っている間は陽炎の割り込みが
       来ないので、ずれたままの値で固まってしまう。 */
    vdp_adjust(vdp_adj_dx, vdp_adj_dy);
    pcm_play = 0;                       /* 書き換え中に割り込まれないように */
    pcm_src = (uint16_t)(0xA000 + taiko_off[kind]);
    pcm_left = taiko_len[kind];
    pcm_line = 0;
    pcm_ctrl(0x03);                     /* BUFF = 1 / MUTE は解除のまま */
    VDP_REG(19, 1);                     /* 次の行で割り込む */
    VDP_REG(0, 0x16);                   /* IE1 = 1 */
    pcm_play = 1;
}
