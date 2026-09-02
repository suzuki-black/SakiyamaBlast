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

void debris_clear(void)
{
    uint8_t i;
    for (i = 0; i < DEBRIS_MAX; i++) {
        db_t[i] = 0;
        spr_park((uint8_t)(SPR_DEBRIS + i));
    }
}

/* 採った升からかけらを出す。四方へ少しずらして出すと、散ってから集まる形になる。 */
void debris_spawn(uint8_t cx, uint8_t cy)
{
    static const int8_t ox[DEBRIS_MAX] = { -4, 4, -4, 4 };
    static const int8_t oy[DEBRIS_MAX] = { -4, -4, 4, 4 };
    uint8_t i, n = 0;
    for (i = 0; i < DEBRIS_MAX && n < 2; i++) {
        if (db_t[i]) {
            continue;                          /* 使用中 */
        }
        db_x[i] = (uint8_t)((cx << 4) + 4 + ox[i]);
        db_y[i] = (uint8_t)((cy << 4) + 4 + oy[i]);
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
        d = (int16_t)((int16_t)px - (int16_t)db_x[i]);
        db_x[i] = (uint8_t)((int16_t)db_x[i] + (d >> 2));
        d = (int16_t)((int16_t)py - (int16_t)db_y[i]);
        db_y[i] = (uint8_t)((int16_t)db_y[i] + (d >> 2));
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
