/*
 * SakiyamaBlast - HUD (最下部の帯)
 * まだフォントを持たないので、残機・ダイナマイトのストック・採掘率を図形で出す。
 * 値が変わった時だけ塗り直す (毎フレーム描くと重いため)。
 */
#include "game.h"
#include "vdp.h"

#define BAR_X   150
#define BAR_W   96
#define BAR_Y   (HUD_Y + 13)
#define BAR_H   10

static uint8_t h_lives, h_stock, h_pct, h_stage;

void hud_reset(void)
{
    h_lives = 0xFF;
    h_stock = 0xFF;
    h_pct   = 0xFF;
    h_stage = 0xFF;
}

void hud_draw(void)
{
    uint8_t i, n;
    uint16_t w;

    if (lives == h_lives && stock == h_stock && dug_pct == h_pct && stage == h_stage) {
        return;
    }
    h_lives = lives;
    h_stock = stock;
    h_pct   = dug_pct;
    h_stage = stage;

    vdp_fill(0, HUD_Y, 256, SCR_H - HUD_Y, COL_HUD);

    /* 残機: 先山の頭を模した白い印 */
    n = (lives > 5) ? 5 : lives;
    for (i = 0; i < n; i++) {
        uint16_t x = (uint16_t)(4 + i * 11);
        vdp_fill(x + 1, HUD_Y + 8, 6, 2, COL_WHITE);
        vdp_fill(x, HUD_Y + 10, 8, 8, COL_WHITE);
    }

    /* ダイナマイトのストック: 赤い筒＋黄色い導火線 */
    n = (stock > 9) ? 9 : stock;
    for (i = 0; i < n; i++) {
        uint16_t x = (uint16_t)(72 + i * 8);
        vdp_fill(x + 1, HUD_Y + 6, 2, 4, COL_FUSE);
        vdp_fill(x, HUD_Y + 10, 5, 12, COL_RED);
    }

    /* 採掘率のバー。90% の位置に赤い目盛りを立てる (ここでクリア)。 */
    vdp_fill(BAR_X, BAR_Y, BAR_W, BAR_H, COL_TUNNEL);
    w = (uint16_t)(((uint16_t)dug_pct * BAR_W) / 100u);
    if (w) {
        vdp_fill(BAR_X, BAR_Y, w, BAR_H, COL_WHITE);
    }
    vdp_fill(BAR_X + ((WIN_PCT * BAR_W) / 100u), BAR_Y - 3, 2, BAR_H + 6, COL_RED);

    /* ステージ: バーの下に小さな点を並べる */
    n = (stage > 12) ? 12 : stage;
    for (i = 0; i < n; i++) {
        vdp_fill((uint16_t)(BAR_X + i * 6), BAR_Y + BAR_H + 5, 4, 3, COL_WHITE);
    }
}
