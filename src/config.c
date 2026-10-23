/*
 * SakiyamaBlast - 隠しコンフィグ
 *
 * 入り方: タイトルで 上上下下左右左右BA。
 * 方向キーはキーマトリクスの行8、A/B は行2 から直接読む（ジョイスティックと混ざらない）。
 */
#include "config.h"
#include "sound.h"
#include "vdp.h"
#include "game.h"
#include "text.h"

uint8_t cfg_lives;
uint8_t cfg_continue;
uint8_t cfg_invincible;
uint8_t cfg_start_stage;
uint8_t cfg_reset;
uint8_t cfg_haze;
uint8_t cfg_fm;
uint8_t cfg_pcm;       /* 和太鼓 (turbo R の PCM)。載っていない機械では効かない */        /* FM 音源 (MSX-MUSIC) を添えるか。検出できた機械でだけ効く */

/* 入力の種類 */
#define TK_NONE  0
#define TK_UP    1
#define TK_DOWN  2
#define TK_LEFT  3
#define TK_RIGHT 4
#define TK_B     5
#define TK_A     6
#define TK_SPACE 7

static const uint8_t konami[10] = {
    TK_UP, TK_UP, TK_DOWN, TK_DOWN, TK_LEFT, TK_RIGHT, TK_LEFT, TK_RIGHT, TK_B, TK_A
};

static uint8_t k_idx;
static uint8_t prev_tok;
static uint8_t prev_r;           /* R キーの前フレームの状態 (押した瞬間だけ拾う) */
static uint8_t cur;              /* 設定画面のカーソル位置 */
static uint8_t dirty;

#define N_ITEMS 9
#define ITEM_Y  72
#define ITEM_DY 16
#define VAL_X   168

void config_init(void)
{
    cfg_lives = LIVES_INIT;
    cfg_continue = 0;
    cfg_invincible = 0;
    cfg_start_stage = 1;
    cfg_reset = 0;
    cfg_haze = 1;
    cfg_fm = 1;
    cfg_pcm = 1;
    k_idx = 0;
    prev_tok = TK_NONE;
    prev_r = 0;
}

/* いま押されているキーを1つだけ返す。A/B を先に見る。 */
static uint8_t token(void)
{
    uint8_t r2 = snsmat(2);
    uint8_t r8 = snsmat(8);
    if (!(r2 & 0x80)) { return TK_B; }
    if (!(r2 & 0x40)) { return TK_A; }
    if (!(r8 & 0x20)) { return TK_UP; }
    if (!(r8 & 0x40)) { return TK_DOWN; }
    if (!(r8 & 0x10)) { return TK_LEFT; }
    if (!(r8 & 0x80)) { return TK_RIGHT; }
    if (!(r8 & 0x01)) { return TK_SPACE; }
    return TK_NONE;
}

/* 押した瞬間だけを拾う。押しっぱなしは1回として数える。 */
static uint8_t token_edge(void)
{
    uint8_t t = token();
    uint8_t e = (uint8_t)((t != prev_tok) ? t : TK_NONE);
    prev_tok = t;
    return e;
}

uint8_t konami_check(void)
{
    uint8_t t = token_edge();
    if (t == TK_NONE) {
        return 0;
    }
    if (t == konami[k_idx]) {
        k_idx++;
        if (k_idx >= 10) {
            k_idx = 0;
            return 1;
        }
        return 0;
    }
    k_idx = (uint8_t)((t == konami[0]) ? 1 : 0);
    return 0;
}

static void put_onoff(uint16_t y, uint8_t v)
{
    text_draw(VAL_X, y, v ? "ON " : "OFF", COL_WHITE, COL_TUNNEL, 1);
}

static void put_num(uint16_t y, uint8_t v)
{
    char buf[6];
    uint8_t n = text_u8(buf, v);
    buf[n++] = ' ';
    buf[n] = 0;
    text_draw(VAL_X, y, buf, COL_WHITE, COL_TUNNEL, 1);
}

/* 値は1行ぶんだけ描き直す。全部描き直すと数フレーム掛かり、その間の入力を取りこぼす。 */
static void draw_value(uint8_t i)
{
    uint16_t y = (uint16_t)(ITEM_Y + i * ITEM_DY);
    switch (i) {
    case 0: put_num(y, cfg_lives); break;
    case 1: put_onoff(y, cfg_continue); break;
    case 2: put_onoff(y, cfg_invincible); break;
    case 3: put_num(y, cfg_start_stage); break;
    case 4: put_onoff(y, cfg_reset); break;
    case 5: put_onoff(y, cfg_haze); break;
    case 6: put_onoff(y, cfg_fm); break;
    case 7: put_onoff(y, cfg_pcm); break;
    default: break;
    }
}

static void draw_cursor(uint8_t i, uint8_t on)
{
    text_draw(32, (uint16_t)(ITEM_Y + i * ITEM_DY),
              on ? ">" : " ", COL_FUSE, COL_TUNNEL, 1);
}

static void draw_items(void)
{
    uint8_t i;
    for (i = 0; i < N_ITEMS; i++) {
        draw_cursor(i, (uint8_t)(i == cur));
        draw_value(i);
    }
}

void config_enter(void)
{
    vdp_fill(0, 0, 256, SCR_H, COL_TUNNEL);
    text_draw(80, 24, "CONFIG", COL_FUSE, COL_TUNNEL, 2);

    text_draw(48, ITEM_Y,                  "LIVES",       COL_WHITE, COL_TUNNEL, 1);
    text_draw(48, ITEM_Y + ITEM_DY,        "CONTINUE",    COL_WHITE, COL_TUNNEL, 1);
    text_draw(48, ITEM_Y + ITEM_DY * 2,    "INVINCIBLE",  COL_WHITE, COL_TUNNEL, 1);
    text_draw(48, ITEM_Y + ITEM_DY * 3,    "START STAGE", COL_WHITE, COL_TUNNEL, 1);
    /* **キーを名前に入れておく。** 「面リセット ON」だけでは何を押すのか分からない。 */
    text_draw(48, ITEM_Y + ITEM_DY * 4,    "RETRY BY R",  COL_WHITE, COL_TUNNEL, 1);
    text_draw(48, ITEM_Y + ITEM_DY * 5,    "HEAT HAZE",   COL_WHITE, COL_TUNNEL, 1);
    /* **FM は載っている機械でだけ効く。** 無い機械でも項目は出すが、
       切り替えても何も変わらない (opll_hw が 0 なら opll_use は立たない)。 */
    text_draw(48, ITEM_Y + ITEM_DY * 6,    "FM SOUND",    COL_WHITE, COL_TUNNEL, 1);
    text_draw(48, ITEM_Y + ITEM_DY * 7,    "TAIKO (PCM)", COL_WHITE, COL_TUNNEL, 1);
    text_draw(48, ITEM_Y + ITEM_DY * 8,    "SEE ENDING",  COL_WHITE, COL_TUNNEL, 1);

    text_draw(24, 180, "UP/DOWN    SELECT",  COL_DIA, COL_TUNNEL, 1);
    text_draw(24, 192, "LEFT/RIGHT CHANGE",  COL_DIA, COL_TUNNEL, 1);
    text_draw(24, 204, "SPACE      DECIDE",  COL_DIA, COL_TUNNEL, 1);

    cur = 0;
    prev_tok = TK_NONE;
    draw_items();
}

uint8_t config_update(void)
{
    uint8_t t = token_edge();
    if (t == TK_NONE) {
        return CFG_STAY;
    }
    dirty = 0;
    switch (t) {
    case TK_UP:
        draw_cursor(cur, 0);
        cur = (uint8_t)((cur == 0) ? (N_ITEMS - 1) : (cur - 1));
        draw_cursor(cur, 1);
        break;
    case TK_DOWN:
        draw_cursor(cur, 0);
        cur = (uint8_t)((cur + 1) % N_ITEMS);
        draw_cursor(cur, 1);
        break;
    case TK_LEFT:
        if (cur == 0 && cfg_lives > 1) { cfg_lives--; }
        else if (cur == 1) { cfg_continue = (uint8_t)!cfg_continue; }
        else if (cur == 2) { cfg_invincible = (uint8_t)!cfg_invincible; }
        else if (cur == 3 && cfg_start_stage > 1) { cfg_start_stage--; }
        else if (cur == 4) { cfg_reset = (uint8_t)!cfg_reset; }
        else if (cur == 5) { cfg_haze = (uint8_t)!cfg_haze; }
        else if (cur == 6) { cfg_fm = (uint8_t)!cfg_fm; opll_apply(); }
        else if (cur == 7) { cfg_pcm = (uint8_t)!cfg_pcm; pcm_apply(); }
        dirty = 1;
        break;
    case TK_RIGHT:
        if (cur == 0 && cfg_lives < 9) { cfg_lives++; }
        else if (cur == 1) { cfg_continue = (uint8_t)!cfg_continue; }
        else if (cur == 2) { cfg_invincible = (uint8_t)!cfg_invincible; }
        else if (cur == 3 && cfg_start_stage < MAX_STAGE) { cfg_start_stage++; }
        else if (cur == 4) { cfg_reset = (uint8_t)!cfg_reset; }
        else if (cur == 5) { cfg_haze = (uint8_t)!cfg_haze; }
        else if (cur == 6) { cfg_fm = (uint8_t)!cfg_fm; opll_apply(); }
        else if (cur == 7) { cfg_pcm = (uint8_t)!cfg_pcm; pcm_apply(); }
        dirty = 1;
        break;
    case TK_SPACE:
        return (uint8_t)((cur == N_ITEMS - 1) ? CFG_ENDING : CFG_START);
    default:
        break;
    }
    if (dirty) {
        draw_value(cur);
    }
    return CFG_STAY;
}

/*
 * 面リセット。**キーマトリクスの行4 bit7 = R** を直接読む。
 *
 * **押した瞬間だけを拾う。** 押しっぱなしで毎フレーム組み直すと、面が延々と
 * 描き直されて操作を受け付けなくなる。
 * **cfg_reset が OFF のときも前の状態は更新する。** 更新しないと、R を押したまま
 * 設定を ON にした瞬間に押しっぱなしが「押した瞬間」として通ってしまう。
 */
uint8_t reset_pressed(void)
{
    uint8_t now = (uint8_t)((snsmat(4) & 0x80) ? 0 : 1);
    uint8_t hit = (uint8_t)(now && !prev_r);
    prev_r = now;
    return (uint8_t)(cfg_reset ? hit : 0);
}
