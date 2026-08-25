/*
 * SakiyamaBlast - HUD (最下部の帯) とスコア
 *
 * 帯は y=176..211 の36px。2段に分けて詰める。
 *   上段: スコア / 面番号 / 体温メーター
 *   下段: 残機 / ダイナマイト所持数 / 採掘率バー
 * 値が変わった部分だけ描き直す (全部描き直すと数フレーム掛かる)。
 */
#include "game.h"
#include "vdp.h"
#include "text.h"

/* --- スコア --- */
/* 10進の桁で持つ (0 が最上位)。32bit 演算より速く、そのまま表示できる。 */
uint8_t score_d[SCORE_DIGITS];

void score_reset(void)
{
    uint8_t i;
    for (i = 0; i < SCORE_DIGITS; i++) {
        score_d[i] = 0;
    }
}

void score_add(uint16_t v)
{
    int8_t i;
    uint8_t carry = 0;
    for (i = SCORE_DIGITS - 1; i >= 0; i--) {
        uint8_t d = (uint8_t)(score_d[i] + (uint8_t)(v % 10) + carry);
        v = (uint16_t)(v / 10);
        carry = 0;
        if (d >= 10) {
            d = (uint8_t)(d - 10);
            carry = 1;
        }
        score_d[i] = d;
        if (!v && !carry) {
            break;
        }
    }
    if (carry) {                       /* 桁あふれは 999999 で止める */
        for (i = 0; i < SCORE_DIGITS; i++) {
            score_d[i] = 9;
        }
    }
}

/* 同時に倒した数で増える得点 (Mr. Do! のリンゴ潰しと同じ考え方) */
static const uint16_t kill_pts[6] = { 0, 100, 200, 400, 600, 800 };

void score_kills(uint8_t n, uint8_t by_rock)
{
    uint16_t v;
    if (n == 0) {
        return;
    }
    if (n > 5) {
        n = 5;
    }
    v = kill_pts[n];
    score_add(by_rock ? (uint16_t)(v * 2) : v);
}

/* --- 画面配置 --- */
#define ROW_A     178                  /* 上段の文字 */
#define ROW_B     192                  /* 下段 */
#define SCORE_X   4
#define STAGE_X   60
#define TEMP_X    96                   /* 体温メーター */
#define TEMP_Y    177
#define TEMP_BLK  16                   /* 目盛りの数 */
#define TEMP_BW   8                    /* 目盛り1つの幅 */
#define TEMP_H    10
#define LIFE_X    4
#define DYN_X     60
#define BAR_X     112                  /* 採掘率バー */
#define BAR_Y     (ROW_B + 1)
#define BAR_W     128
#define BAR_H     10

static uint8_t h_lives, h_stock, h_pct, h_stage, h_temp;
static uint8_t h_score[SCORE_DIGITS];

void hud_reset(void)
{
    uint8_t i;
    h_lives = 0xFF;
    h_stock = 0xFF;
    h_pct   = 0xFF;
    h_stage = 0xFF;
    h_temp  = 0xFF;
    for (i = 0; i < SCORE_DIGITS; i++) {
        h_score[i] = 0xFF;
    }
    vdp_fill(0, HUD_Y, 256, SCR_H - HUD_Y, COL_HUD);
}

static void draw_score(void)
{
    char buf[SCORE_DIGITS + 1];
    uint8_t i;
    for (i = 0; i < SCORE_DIGITS; i++) {
        buf[i] = (char)('0' + score_d[i]);
        h_score[i] = score_d[i];
    }
    buf[SCORE_DIGITS] = 0;
    text_draw(SCORE_X, ROW_A, buf, COL_WHITE, COL_HUD, 1);
}

static void draw_stage(void)
{
    char buf[6];
    uint8_t n;
    buf[0] = 'S'; buf[1] = 'T'; buf[2] = ' ';
    n = (uint8_t)(3 + text_u8(&buf[3], stage));
    buf[n++] = ' ';
    buf[n] = 0;
    text_draw(STAGE_X, ROW_A, buf, COL_FUSE, COL_HUD, 1);
    h_stage = stage;
}

/* 体温メーター。増える方向のブロック目盛りで、高くなると赤へ変わる。 */
static void draw_temp(uint8_t blocks)
{
    uint8_t i;
    for (i = 0; i < TEMP_BLK; i++) {
        uint8_t col = (uint8_t)(i >= blocks ? COL_TUNNEL
                                : (i >= (TEMP_BLK * 3 / 4) ? COL_RED : COL_FUSE));
        vdp_fill((uint16_t)(TEMP_X + i * (TEMP_BW + 1)), TEMP_Y, TEMP_BW, TEMP_H, col);
    }
    h_temp = blocks;
}

static void draw_lives(void)
{
    uint8_t i, n = (uint8_t)((lives > 5) ? 5 : lives);
    vdp_fill(LIFE_X, ROW_B, 56, 14, COL_HUD);
    for (i = 0; i < n; i++) {
        uint16_t x = (uint16_t)(LIFE_X + i * 11);
        vdp_fill(x + 1, ROW_B, 6, 2, COL_WHITE);
        vdp_fill(x, ROW_B + 2, 8, 8, COL_WHITE);
    }
    h_lives = lives;
}

/* ダイナマイト所持数。上限が無いので絵1つ＋数字で出す。 */
static void draw_stock(void)
{
    char buf[6];
    uint8_t n = text_u8(buf, stock);
    buf[n++] = ' ';
    buf[n++] = ' ';
    buf[n] = 0;
    vdp_fill(DYN_X, ROW_B, 6, 14, COL_HUD);
    vdp_fill(DYN_X + 2, ROW_B, 2, 3, COL_FUSE);
    vdp_fill(DYN_X, ROW_B + 3, 6, 11, COL_RED);
    text_draw(DYN_X + 10, ROW_B + 2, buf, COL_WHITE, COL_HUD, 1);
    h_stock = stock;
}

static void draw_bar(void)
{
    uint16_t w = (uint16_t)(((uint16_t)dug_pct * BAR_W) / 100u);
    vdp_fill(BAR_X, BAR_Y, BAR_W, BAR_H, COL_TUNNEL);
    if (w) {
        vdp_fill(BAR_X, BAR_Y, w, BAR_H, COL_WHITE);
    }
    vdp_fill(BAR_X + ((WIN_PCT * BAR_W) / 100u), BAR_Y - 2, 2, BAR_H + 4, COL_RED);
    h_pct = dug_pct;
}

void hud_draw(void)
{
    uint8_t blocks = (uint8_t)(((uint16_t)temp * TEMP_BLK) / TEMP_MAX);
    uint8_t i, sc = 0;

    /* スコアは掘るたびに動く (敵の掘り屋も掘る) ので、毎回描き直すと重い。
       8フレームに1回だけ見に行く。表示が最大 0.13 秒遅れるだけで見た目は変わらない。 */
    if ((frame_n & 7) == 0) {
        for (i = 0; i < SCORE_DIGITS; i++) {
            if (score_d[i] != h_score[i]) {
                sc = 1;
                break;
            }
        }
    }
    if (sc)              { draw_score(); }
    if (stage != h_stage) { draw_stage(); }
    if (blocks != h_temp) { draw_temp(blocks); }
    if (lives != h_lives) { draw_lives(); }
    if (stock != h_stock) { draw_stock(); }
    if (dug_pct != h_pct) { draw_bar(); }
}
