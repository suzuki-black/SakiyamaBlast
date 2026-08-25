/*
 * SakiyamaBlast - ボーナスアイテム
 *
 * 岩を2個割ると一定時間だけ出現し、上を通過すると獲得できる。
 * 出現位置は画面中央の巣の升目（掘り抜きの上で、取りに行くのに危険が伴う）。
 * 効果はアイテムによって異なる: 得点 / 体温全回復 / 残機+1。
 * ※ 爆風の強化や同時設置数の増加は、爆風と未掘部分の強弱関係の見極めが要るため TODO。
 */
#include "game.h"

uint8_t bonus_on;
uint8_t bonus_c, bonus_r;
uint8_t bonus_kind;

static uint16_t bonus_timer;
static uint8_t rocks_broken;

void bonus_stage_reset(void)
{
    bonus_on = 0;
    rocks_broken = 0;
}

/* 岩が割れるたびに呼ぶ。規定数に達したら出現させる。 */
void bonus_rock_broken(void)
{
    uint8_t r;
    rocks_broken++;
    if (rocks_broken < ROCKS_PER_BONUS) {
        return;
    }
    rocks_broken = 0;
    if (bonus_on) {
        return;                        /* まだ前のが出ている */
    }
    r = (uint8_t)(rnd8() % 10);
    bonus_kind = (uint8_t)((r < 5) ? BONUS_SCORE : (r < 8) ? BONUS_COOL : BONUS_LIFE);
    bonus_c = nest_c;
    bonus_r = nest_r;
    bonus_on = 1;
    bonus_timer = BONUS_FRAMES;
    draw_bonus(bonus_c, bonus_r, bonus_kind);
}

void bonus_update(void)
{
    if (!bonus_on) {
        return;
    }
    bonus_timer--;
    if (bonus_timer == 0) {
        bonus_on = 0;                  /* 時間切れで消える */
        field_redraw_cell((int8_t)bonus_c, (int8_t)bonus_r);
    }
}

/* 自機がその升目に乗ったら獲得。 */
void bonus_take(void)
{
    bonus_on = 0;
    field_redraw_cell((int8_t)bonus_c, (int8_t)bonus_r);
    switch (bonus_kind) {
    case BONUS_COOL:
        temp = 0;
        break;
    case BONUS_LIFE:
        if (lives < 9) {
            lives++;
        }
        break;
    default:
        score_add(PTS_BONUS);
        break;
    }
}
