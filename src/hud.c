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

/* n体を同時に倒したときの点数。得点表示にも使うので値だけ返す口を分けてある。 */
uint16_t kill_value(uint8_t n, uint8_t by_rock)
{
    if (n == 0) {
        return 0;
    }
    if (n > 5) {
        n = 5;
    }
    return by_rock ? (uint16_t)(kill_pts[n] * 2) : kill_pts[n];
}

void score_kills(uint8_t n, uint8_t by_rock)
{
    uint16_t v;
    if (n == 0) {
        return;
    }
    if (n > 5) {
        n = 5;
    }
    v = kill_value(n, by_rock);
    score_add(v);
}

/* 倒した升へ得点を出す。ディグダグは倒した場所に点数が出るので、
   何をして何点入ったのかがその場で分かる。まとめて潰したときの倍率も伝わる。
   同時に出せるのは1つだけ。新しいものが古いものを上書きする。 */
static uint8_t  pop_t, pop_c, pop_r;
static char     pop_s[8];

static void popup_erase(void)
{
    int8_t k;
    /* 倍角だと縦にも1升ぶん広がるので、下の行も消す。 */
    for (k = 0; k < 4; k++) {
        field_redraw_cell((int8_t)(pop_c + k), (int8_t)pop_r);
        field_redraw_cell((int8_t)(pop_c + k), (int8_t)(pop_r + 1));
    }
    player_carve();
}

void score_popup(uint8_t cx, uint8_t cy, uint16_t v)
{
    uint8_t d[5], n = 0, i;
    if (pop_t) {
        popup_erase();                 /* 前のものを先に片付ける */
    }
    if (v == 0) {
        return;
    }
    while (v && n < 5) {
        d[n++] = (uint8_t)(v % 10);
        v = (uint16_t)(v / 10);
    }
    for (i = 0; i < n; i++) {
        pop_s[i] = (char)('0' + d[n - 1 - i]);
    }
    pop_s[n] = 0;
    /* 右端からはみ出さないよう寄せる。1文字8px = 半升。 */
    if (cx + ((n + 1) >> 1) >= COLS) {
        cx = (uint8_t)(COLS - ((n + 1) >> 1) - 1);
    }
    pop_c = cx;
    pop_r = cy;
    pop_t = POPUP_FRAMES;
    /* 大きい点数は倍角で出す。連鎖が伸びたことが数字の大きさでも分かる。 */
    text_draw((uint16_t)((uint16_t)cx << 4), (uint16_t)(((uint16_t)cy << 4) + 4),
              pop_s, COL_WHITE, COL_TUNNEL, (uint8_t)(v >= 800 ? 2 : 1));
}

void popup_tick(void)
{
    if (!pop_t) {
        return;
    }
    pop_t--;
    if (!pop_t) {
        popup_erase();
    }
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
#define COAL_X    112                  /* 残りの黒ダイヤ */
#define COAL_Y    (ROW_B + 1)
#define COMBO_X   200                  /* 連続採掘の倍率 */

static uint8_t h_lives, h_stock, h_coal, h_stage, h_temp, h_combo;
static uint8_t h_score[SCORE_DIGITS];

void hud_reset(void)
{
    uint8_t i;
    h_lives = 0xFF;
    h_stock = 0xFF;
    h_coal  = 0xFF;
    h_combo = 0xFF;
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
    /* "ST " + 2桁 + " " + 終端 = 7バイト。text_u8 は終端も書くので余裕を持たせる。
       ここを char[6] にしていたため、面10以降で1バイトはみ出してスタックを壊し、
       面の開始と同時にハングしていた。 */
    char buf[8];
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
    char buf[8];
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

/* 残りの黒ダイヤ。これが0になるとステージクリアなので、いちばん見たい数字。 */
static void draw_coal_left(void)
{
    char buf[16];
    uint8_t n;
    vdp_fill(COAL_X, ROW_B, 84, 14, COL_HUD);
    /* 黒ダイヤの目印 (石炭の塊) */
    vdp_fill(COAL_X + 1, COAL_Y + 1, 10, 8, COL_DIA);
    vdp_fill(COAL_X + 3, COAL_Y + 2, 3, 2, COL_WHITE);
    buf[0] = 'C'; buf[1] = 'O'; buf[2] = 'A'; buf[3] = 'L'; buf[4] = ' ';
    n = (uint8_t)(5 + text_u8(&buf[5], coal_left));
    buf[n] = 0;
    text_draw(COAL_X + 16, ROW_B + 2, buf,
              (uint8_t)(coal_left ? COL_WHITE : COL_FUSE), COL_HUD, 1);
    h_coal = coal_left;
}

/* 連続採掘の倍率。1倍のときは出さない (常時出ていると意味が薄れる)。 */
static void draw_combo(void)
{
    char buf[6];
    vdp_fill(COMBO_X, ROW_B, 40, 14, COL_HUD);
    if (combo >= 2) {
        buf[0] = 'X';
        buf[1] = (char)('0' + combo);
        buf[2] = 0;
        text_draw(COMBO_X, ROW_B + 2, buf, COL_FUSE, COL_HUD, 1);
    }
    h_combo = combo;
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
    if (coal_left != h_coal) { draw_coal_left(); }
    if (combo != h_combo)     { draw_combo(); }
}
