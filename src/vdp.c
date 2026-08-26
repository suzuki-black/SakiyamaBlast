/* SakiyamaBlast - SCREEN 5 初期化と描画プリミティブ */
#include "vdp.h"

/* R#1 は書き込み専用なので、いま入れている値を憶えておく */
static uint8_t reg1 = 0x62;

/* 画面の表示 ON/OFF (R#1 bit6)。OFF の間は R#7 の色 (黒) だけになる。
   パレットの差し替えや面の描き起こしを見せないための幕。
   垂直帰線割り込み (bit5) は落とさないので JIFFY は進み続ける。 */
void vdp_display(uint8_t on)
{
    reg1 = on ? (uint8_t)(reg1 | 0x40) : (uint8_t)(reg1 & 0xBF);
    VDP_REG(1, reg1);
}

void vdp_screen5(void)
{
    /* GRAPHIC 4 (M3=1, M4=1) */
    VDP_REG(0, 0x06);
    /* 表示ON + 垂直帰線割り込みON + 16x16スプライト */
    reg1 = 0x62;
    VDP_REG(1, reg1);
    VDP_REG(2, 0x1F);            /* ビットマップ = 0x00000 (ページ0) */
    VDP_REG(3, 0xFF);
    VDP_REG(4, 0x03);
    VDP_REG(5, 0xEF);            /* スプライト属性 = 0x7600 (色テーブルは -512 = 0x7400) */
    VDP_REG(6, 0x0F);            /* スプライトパターン = 0x7800 */
    VDP_REG(7, 0x00);            /* ボーダー色 */
    VDP_REG(8, 0x08);            /* 64K DRAM / スプライト有効 */
    /* 212ライン表示。PAL/NTSC ビット(bit1)は BIOS の保存値を引き継ぐ */
    VDP_REG(9, (uint8_t)(0x80 | (RG9SAV & 0x02)));
    VDP_REG(10, 0x00);
    VDP_REG(11, 0x00);           /* スプライト属性の A16-A15 */
    VDP_REG(23, 0x00);           /* 垂直オフセット */
}

/* r/g/b は 0..7 */
void vdp_set_palette(uint8_t idx, uint8_t r, uint8_t g, uint8_t b)
{
    VDP_REG(16, idx);
    vdp_wpal((uint8_t)((r << 4) | b));
    vdp_wpal(g);
}

void vdp_upload(uint16_t addr, const uint8_t *src, uint16_t len)
{
    vdp_setwrite(addr);
    while (len--) {
        vdp_wdata(*src++);
    }
}

void vdp_memset(uint16_t addr, uint8_t val, uint16_t len)
{
    vdp_setwrite(addr);
    while (len--) {
        vdp_wdata(val);
    }
}

uint16_t vdp_blklen;   /* vdp_wblock() が読む転送バイト数 (lowlevel.s から参照) */

/* R#36..R#46: DX, DY, NX, NY, CLR, ARG, CMD */
static uint8_t cmdbuf[11];

void vdp_fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t color)
{
    cmdbuf[0] = (uint8_t)(x & 0xFF);
    cmdbuf[1] = (uint8_t)(x >> 8);
    cmdbuf[2] = (uint8_t)(y & 0xFF);
    cmdbuf[3] = (uint8_t)(y >> 8);
    cmdbuf[4] = (uint8_t)(w & 0xFF);
    cmdbuf[5] = (uint8_t)(w >> 8);
    cmdbuf[6] = (uint8_t)(h & 0xFF);
    cmdbuf[7] = (uint8_t)(h >> 8);
    cmdbuf[8] = color;
    cmdbuf[9] = 0x00;            /* ARG: X,Y とも増加方向・ページ0 */
    cmdbuf[10] = 0x80;           /* LMMV (論理演算 IMP)。0xC0 は HMMV=バイト塗りなので誤り */
    vdp_wait_cmd();
    vdp_cmd(cmdbuf);
}
