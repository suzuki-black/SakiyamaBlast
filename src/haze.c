/* SakiyamaBlast - 陽炎 (docs/GAME_RULES.md §13) */
#include "haze.h"
#include "game.h"
#include "vdp.h"

uint8_t haze_row[HAZE_BANDS + 1];
uint8_t haze_adj[HAZE_BANDS + 1];
uint8_t haze_n;
uint8_t haze_i;
uint16_t haze_hits;      /* ハンドラが走った回数 (計測用) */

static uint8_t ph;               /* 位相。1フレームで1進む */
static uint8_t on;               /* いまライン割り込みを有効にしているか */
static uint8_t bands;            /* 分割数。機種で変える */
static uint8_t pstep;            /* 1バンドあたりの位相の進み (1/16周期単位) */


/* 縦方向の揺らぎの形。振幅2の正弦を16段で持つ。 */
static const int8_t sn[16] = { 0, 1, 1, 2, 2, 2, 1, 1, 0,-1,-1,-2,-2,-2,-1,-1 };

/* 分割線の表を作り直す。最後の1つは HUD の手前へ戻すためのもの
   (**HUD は揺らさない**)。FH は R#19 + 1 行の頭で立つので、
   8行目で切り替えたければ R#19 = 7。 */
static void build_rows(void)
{
    uint8_t i, step = (uint8_t)(FIELD_H / bands);
    for (i = 0; i < bands; i++) {
        haze_row[i] = (uint8_t)(7 + i * step);
        haze_adj[i] = vdp_adj_neutral;
    }
    /* **最後の分割は HUD の直前。** ここで中立へ戻すので HUD は揺れない。
       分割を増やしすぎて最後のバンドがここへ届くと、同じ行に割り込みが2つ並んで
       バンドの番号がずれる。`7 + (分割-1) * (176/分割) < 175` を満たす範囲で選ぶこと
       (16分割なら最後が 172 行目なので余裕がある)。 */
    haze_row[bands] = (uint8_t)(FIELD_H - 1);
    haze_adj[bands] = vdp_adj_neutral;
}

void haze_init(void)
{
    /* **turbo R だけ分割を細かくする。** R800 DRAM モードでは割り込みがほぼタダ
       になるので、帯の継ぎ目を細かくして陽炎に近づけられる (実機で確認済み)。 */
    if (msxver() >= 3) {
        bands = HAZE_BANDS_TR;
        pstep = 2;
    } else {
        bands = HAZE_BANDS_Z80;
        pstep = 3;
    }
    build_rows();
    haze_n = 0;
    haze_i = 0;
    on = 0;
    haze_hook();
}

void haze_tick(uint8_t want)
{
    uint8_t i;
    if (!want) {
        if (on) {
            on = 0;
            haze_n = 0;
            VDP_REG(0, 0x06);            /* IE1 を切る (GRAPHIC4 のまま) */
            vdp_adjust(vdp_adj_dx, vdp_adj_dy);   /* 揺れの値へ戻す */
        }
        return;
    }
    ph++;
    /*
     * 縦は 1バンドあたり 3/16 周期ずつ進めるので、盤面 11バンドで約2周。
     * 時間は 8フレームで 1/16 周期、つまり **1周約2.1秒**。
     * 陽炎は「ゆっくり縦に流れる」のが本物に近く、速いと画面酔いになる。
     */
    for (i = 0; i < bands; i++) {
        uint8_t k = (uint8_t)((uint8_t)(i * pstep + (uint8_t)(ph >> 3)) & 15);
        int8_t d = sn[k];
        /* **画面揺れ (vdp_adjust) のぶんを足す。** 足さないと、揺れている最中に
           陽炎がレジスタを上書きして揺れが消える。 */
        {
            uint8_t h = (uint8_t)(((uint8_t)(vdp_adj_neutral & 0x0F)
                                   + (uint8_t)vdp_adj_dx + (uint8_t)d) & 0x0F);
            uint8_t v = (uint8_t)(((uint8_t)((vdp_adj_neutral >> 4) & 0x0F)
                                   + (uint8_t)vdp_adj_dy) & 0x0F);
            haze_adj[i] = (uint8_t)((v << 4) | h);
        }
    }
    {
        uint8_t h = (uint8_t)(((uint8_t)(vdp_adj_neutral & 0x0F)
                               + (uint8_t)vdp_adj_dx) & 0x0F);
        uint8_t v = (uint8_t)(((uint8_t)((vdp_adj_neutral >> 4) & 0x0F)
                               + (uint8_t)vdp_adj_dy) & 0x0F);
        haze_adj[bands] = (uint8_t)((v << 4) | h);
    }
    if (!on) {
        on = 1;
        haze_i = 0;
        haze_n = (uint8_t)(bands + 1);
        VDP_REG(19, haze_row[0]);
        VDP_REG(0, 0x16);                /* IE1 = 1 (bit4) */
    }
}
