/*
 * SakiyamaBlast - 鉱車 (第四坑のギミック)
 *
 * **岩を水平に運ぶ装置。** 樋は岩を横へ1升ずらすだけだが、鉱車は数升運ぶ。
 * 同じ横移動でも、距離と、**拍を待つ必要がある**点が違う
 * (docs/STAGE_ACTS_ROADMAP.md §5)。
 *
 * 史実: 坑内の運搬はレールと鉱車で行われた。歯止めを失って逸走することを
 * 「ヒコーキ」と呼んだ (docs/GAME_RULES.md §6)。
 *
 * 規則:
 *   - **レールは面の開始から見えている。** 水平に CART_LEN 升、端に車止め
 *   - 鉱車はその上を**等速で往復する**。自機より遅いので歩いて逃げられる
 *   - **鉱車の升は塞がっている扱い** = 岩の支えになる (`cart_at`)。
 *     真上の岩は鉱車と一緒に運ばれ、**車止めで放り出されて落ちる**
 *   - **自機は轢かれると押される** (岩と同じ)。壁や岩に挟まれたときだけミス。
 *     新しい死因を作らない (作者の判断、2026-10-03)
 *   - **敵は轢かれると倒せる。** 岩で潰したのと同じ扱い (得点も同じ)
 *
 * **落ちる場所が固定されているから読める。** 車止めは面の開始から見えていて
 * 動かないので、「岩をどこへ届けられるか」は掘る前から分かる。プレイヤーが
 * 判断するのは**いつ落とすか**だけで、鉱車は等速なのでメトロノームになる。
 */
#include "game.h"
#include "art.h"
#include "sprite.h"
#include "sound.h"

uint8_t cart_on;                  /* 1 = この面に鉱車がある */
uint8_t cart_r;                   /* レールの行 */
uint8_t cart_c0, cart_c1;         /* レールの左端・右端の列 */

static uint8_t cart_x;            /* 鉱車の x (px) */
static uint8_t cart_dir;          /* 0 = 右へ / 1 = 左へ */
static uint8_t cart_acc;          /* 速度の分子 */
static uint8_t cart_cell;         /* いま居る升 (音を鳴らす変わり目を見るため) */
static int8_t  cart_load;         /* 載せている岩の番号。-1 = 空 */
static uint8_t cart_pat;          /* いま出している絵 (色表を張り替える変わり目を見る) */

void cart_reset(void)
{
    cart_on = 0;
    cart_load = -1;
    cart_dir = 0;
    cart_acc = 0;
    cart_x = 0;
    cart_cell = 0;
    cart_pat = 0xFF;              /* 次の cart_draw で必ず色表を張り直す */
}

/* 面を組むときに呼ぶ (src/field.c の place_rail)。 */
void cart_place(uint8_t c0, uint8_t c1, uint8_t r)
{
    cart_c0 = c0;
    cart_c1 = c1;
    cart_r = r;
    cart_x = (uint8_t)(c0 << 4);
    cart_cell = c0;
    cart_on = 1;
}

/* その升に鉱車が居るか。**岩の支え**と、敵の経路の判定に使う。 */
uint8_t cart_at(int8_t c, int8_t r)
{
    if (!cart_on || r != (int8_t)cart_r) {
        return 0;
    }
    return (uint8_t)(c == (int8_t)((cart_x + 8) >> 4));
}

/* いま載せている岩を放す (車止めで放り出す)。 */
static void cart_release(void)
{
    if (cart_load >= 0) {
        rock_drop_now((uint8_t)cart_load);
        cart_load = -1;
    }
}

/* 真上の升に載っている岩を拾う。鉱車が支えているので岩は落ちずに待っている。 */
static void cart_pickup(void)
{
    int8_t c = (int8_t)((cart_x + 8) >> 4);
    int8_t i;
    if (cart_load >= 0 || cart_r == 0) {
        return;
    }
    i = rock_at(c, (int8_t)(cart_r - 1));
    if (i >= 0 && rock_state[(uint8_t)i] == ROCK_WAIT) {
        cart_load = i;
    }
}

/*
 * **轢かれた自機は押される。** 岩と同じで、横へ押しのけられるだけ。
 * ただし**行き先が塞がっていれば挟まれてミス**になる (既存の潰しと同じ死因)。
 */
static void cart_hit_player(uint8_t right)
{
    uint8_t nx;
    int8_t c;

    if ((uint8_t)(py >> 4) != cart_r) {
        return;                          /* 行が違う */
    }
    if ((uint8_t)((px > cart_x) ? (px - cart_x) : (cart_x - px)) >= 12) {
        return;                          /* 重なっていない */
    }
    if (right) {
        if (px >= (uint8_t)XMAX) {
            dead = 1; dead_cause = 0; return;    /* 盤面の端に挟まれた */
        }
        nx = (uint8_t)(px + 1);
        c = (int8_t)((uint8_t)(nx + 15) >> 4);
    } else {
        if (px == 0) {
            dead = 1; dead_cause = 0; return;
        }
        nx = (uint8_t)(px - 1);
        c = (int8_t)(nx >> 4);
    }
    /* 押し出す先の升が通れること。塞がっていれば挟まれる。 */
    if (!cell_dug(c, (int8_t)cart_r) || rock_at(c, (int8_t)cart_r) >= 0) {
        dead = 1;
        dead_cause = 0;                  /* 潰し。既存の死因と同じ */
        return;
    }
    px = nx;
}

void cart_update(void)
{
    uint8_t c;

    if (!cart_on) {
        return;
    }
    cart_acc = (uint8_t)(cart_acc + CART_SPD);
    if (cart_acc < SPD_UNIT) {
        return;
    }
    cart_acc = (uint8_t)(cart_acc - SPD_UNIT);

    /* 1px 進む。**車止めに着いたら折り返し、載せている岩を放す。** */
    if (cart_dir) {
        if (cart_x <= (uint8_t)(cart_c0 << 4)) {
            cart_dir = 0;
            cart_release();
        } else {
            cart_x--;
        }
    } else {
        if (cart_x >= (uint8_t)(cart_c1 << 4)) {
            cart_dir = 1;
            cart_release();
        } else {
            cart_x++;
        }
    }

    /* 載せている岩は一緒に動く。鉱車が真下から外れたら手を放す
       (そのぶんは rock.c の支え判定が落としてくれる)。 */
    cart_pickup();
    if (cart_load >= 0) {
        if (rock_state[(uint8_t)cart_load] != ROCK_WAIT) {
            cart_load = -1;              /* 揺れ始めた・落ち始めた */
        } else {
            rock_x[(uint8_t)cart_load] = cart_x;
        }
    }

    cart_hit_player((uint8_t)(cart_dir ? 0 : 1));

    c = (uint8_t)((cart_x + 8) >> 4);
    if (c != cart_cell) {                /* 升をひとつ越えた */
        cart_cell = c;
        snd_sfx(SFX_CART);               /* ガタン。優先度は一番低い */
        enemy_kill_at((int8_t)c, (int8_t)cart_r, 1);   /* 轢いた敵は倒れる */
    }
}

/* 毎フレーム、ほかのスプライトと一緒に置く。 */
void cart_draw(void)
{
    uint8_t pat;

    if (!cart_on) {
        spr_park(SPR_CART);
        return;
    }
    /* **岩を載せているときは合体した絵にする。** 鉱車と岩を別のスプライトにすると
       その行に2枚並び、「1行に岩は1個まで」(走査線あたり8枚の制限) と競合する。 */
    pat = (uint8_t)((cart_load >= 0) ? PAT_CARTROCK : PAT_CART);
    if (pat != cart_pat) {
        /* **色の帯も差し替える。** mode-2 は1行1色なので、絵が変われば色表も変わる
           (岩を載せると上の行が岩の色になる)。自機の構えと同じ扱い。 */
        cart_pat = pat;
        spr_color_lines(SPR_CART, art_colours(pat));
    }
    spr_set(SPR_CART, cart_x, (uint8_t)((cart_r << 4) - 1), pat);
}
