/* SakiyamaBlast - SCREEN 5 (GRAPHIC 4) VDP 制御 */
#ifndef VDP_H
#define VDP_H

#include "msx.h"

/* VRAM 配置 (MSX-BASIC の SCREEN 5 と同じ) */
#define VRAM_PAGE0   0x0000    /* ビットマップ 256x212 */
#define VRAM_SPRCOL  0x7400    /* スプライト色テーブル (mode 2) */
#define VRAM_SPRATR  0x7600    /* スプライト属性テーブル (OAM) */
#define VRAM_SPRPAT  0x7800    /* スプライトパターンジェネレータ */

/* --- 低レベル (lowlevel.s) --- */
void vdp_wreg(uint16_t rv) __z88dk_fastcall;      /* H=値 L=レジスタ */
void vdp_setwrite(uint16_t addr) __z88dk_fastcall;
void vdp_wdata(uint8_t v) __z88dk_fastcall;
void vdp_wpal(uint8_t v) __z88dk_fastcall;
void vdp_cmd(const uint8_t *p) __z88dk_fastcall;  /* R#36..46 の11バイト */
void vdp_wait_cmd(void);
void oam_flush(void);

#define VDP_REG(r, v)  vdp_wreg((uint16_t)(((uint16_t)(uint8_t)(v) << 8) | (uint8_t)(r)))

/* --- 高レベル (vdp.c) --- */
void vdp_screen5(void);
void vdp_set_palette(uint8_t idx, uint8_t r, uint8_t g, uint8_t b);
void vdp_upload(uint16_t addr, const uint8_t *src, uint16_t len);
void vdp_memset(uint16_t addr, uint8_t val, uint16_t len);
/* 矩形塗り (LMMV)。座標はピクセル単位なので2px単位の掘りにも使える。 */
void vdp_fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t color);

#endif
