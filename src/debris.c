/*
 * SakiyamaBlast - 石炭のかけら
 *
 * 黒ダイヤを採ったとき、その升からかけらが飛び出して**自機のもとへ集まる**。
 *
 * これは飾りではなく説明である。岩を落として黒ダイヤを砕くと、その場に居なくても
 * 採れる。**この「岩でも採掘できる」という仕組みは言葉にしにくい。** かけらが
 * 自機へ吸い寄せられる絵を見せれば、「いま自分の手に入った」ことが一目で伝わる。
 *
 * スプライトで描くので VRAM をほとんど使わず、fps に響かない。位置の計算だけなので
 * CPU 側の負担も小さい。
 */
#include "game.h"
#include "sprite.h"
#include "art.h"

static uint8_t db_t[DEBRIS_MAX];               /* 残りフレーム。0 = 出ていない */
static uint8_t db_x[DEBRIS_MAX], db_y[DEBRIS_MAX];
static int8_t  db_vx[DEBRIS_MAX], db_vy[DEBRIS_MAX];   /* 最初に弾ける向き */

void debris_clear(void)
{
    uint8_t i;
    for (i = 0; i < DEBRIS_MAX; i++) {
        db_t[i] = 0;
        spr_park((uint8_t)(SPR_DEBRIS + i));
    }
}

/*
 * 採った升からかけらを出す。
 * **まず外へ弾けてから自機へ向かう。** まっすぐ吸い寄せるだけだと、採った升が
 * 自機の近くのときに一瞬で終わって見えない。一度散らすと「砕けた」ことも伝わる。
 */
void debris_spawn(uint8_t cx, uint8_t cy)
{
    static const int8_t vx[3] = { -2, 2, 0 };
    static const int8_t vy[3] = { -2, -1, -3 };
    uint8_t i, n = 0;
    for (i = 0; i < DEBRIS_MAX && n < 3; i++) {
        if (db_t[i]) {
            continue;                          /* 使用中 */
        }
        db_x[i] = (uint8_t)((cx << 4) + 6);
        db_y[i] = (uint8_t)((cy << 4) + 6);
        db_vx[i] = vx[n];
        db_vy[i] = vy[n];
        db_t[i] = DEBRIS_FRAMES;
        n++;
    }
}

/*
 * 毎フレーム、自機へ寄せる。残り距離の 1/4 ずつ詰めるので、最初は速く、
 * 近づくほど滑らかに吸い込まれる。**追いかける先は毎フレーム見る**ので、
 * 自機が動いていてもちゃんと追ってくる。
 */
void debris_tick(void)
{
    uint8_t i;
    for (i = 0; i < DEBRIS_MAX; i++) {
        int16_t d;
        if (!db_t[i]) {
            continue;
        }
        db_t[i]--;
        if (db_t[i] > (uint8_t)(DEBRIS_FRAMES - DEBRIS_POP)) {
            /* 最初は外へ弾ける。砕けた手応えを出しつつ、吸い寄せの距離も稼ぐ。 */
            db_x[i] = (uint8_t)((int16_t)db_x[i] + db_vx[i]);
            db_y[i] = (uint8_t)((int16_t)db_y[i] + db_vy[i]);
            continue;
        }
        /* 以後は自機へ。**1/8 ずつ**にしてあるので、近くで採っても一瞬では消えない。 */
        d = (int16_t)((int16_t)px - (int16_t)db_x[i]);
        db_x[i] = (uint8_t)((int16_t)db_x[i] + (d >> 3) + (d > 0 ? 1 : (d < 0 ? -1 : 0)));
        d = (int16_t)((int16_t)py - (int16_t)db_y[i]);
        db_y[i] = (uint8_t)((int16_t)db_y[i] + (d >> 3) + (d > 0 ? 1 : (d < 0 ? -1 : 0)));
    }
}

void debris_draw(void)
{
    uint8_t i;
    for (i = 0; i < DEBRIS_MAX; i++) {
        if (!db_t[i]) {
            spr_park((uint8_t)(SPR_DEBRIS + i));
        } else {
            spr_set((uint8_t)(SPR_DEBRIS + i), db_x[i], (uint8_t)(db_y[i] - 1), PAT_CHIP);
        }
    }
}
