/*
 * SakiyamaBlast - 出水（熱水）
 *
 * **第三坑（面7〜）のギミック。** 炭柱を割って落盤が起きると、盤面に開いている
 * 湧き出し口から熱水が湧き、**坑道を伝って1秒に1升ずつ広がる。**
 *
 * 史実: 常磐は石炭1トンに40トンの湯が湧き、当時世界最大級の排水ポンプを並べていた。
 * 坑内水は温泉なので熱い（気温40度・湿度100%）。docs/HISTORY.md §2。
 *
 * **新しい死因を作らない。** 熱水の升に居ると体温が上がるだけで、失敗条件は既存の
 * オーバーヒートのまま。しかも「なぜ水風呂が要るのか」の説明になる。
 *
 * 設計の約束:
 *   - **掘った坑道しか通らない。** 掘るほど水が回る（切羽圧と同じ主題）
 *   - **下を優先**して広がる。水らしく見え、行き先も読める
 *   - **落ちた岩は水を止める**（既存の「岩は落ちて止まる」だけで説明が付く）
 *   - **採掘は妨げない。** 水で黒ダイヤが採れなくなると面が詰む
 *   - **水風呂と敵の巣へは広がらない。** 水風呂を奪うと立て直せず、
 *     巣を塞ぐと敵が一体も出て来られなくなって面が骨抜きになる
 *   - **`WATER_MAX` 升で止まる。** 際限なく広がると、掘るほど詰む盤面になる
 */
#include "game.h"

uint8_t water[NCELL];          /* 1 = 熱水 */
uint8_t water_on;              /* 湧き出しているか */
uint8_t spring_c, spring_r;    /* 湧き出し口 (面の開始から見えている) */
uint8_t spring_on;

static uint8_t wt;             /* 次に1升広がるまでのフレーム数 */
static uint8_t wn;             /* いま水になっている升の数 */
/* **水になった升の一覧。** 広がり先を探すのに盤面176升を3周すると、
   openMSX の実測で **1秒ごとに約80ms** 食って fps が 56→49 まで落ちた。
   水は最大 WATER_MAX 升しか無いので、その升だけを見れば足りる。 */
static uint8_t wlist[WATER_MAX];

void water_reset(void)
{
    uint16_t i;
    for (i = 0; i < NCELL; i++) {
        water[i] = 0;
    }
    water_on = 0;
    spring_on = 0;
    wt = 0;
    wn = 0;
}

uint8_t cell_water(int8_t cx, int8_t cy)
{
    if (cx < 0 || cx >= COLS || cy < 0 || cy >= ROWS) {
        return 0;
    }
    return water[(uint16_t)(uint8_t)cy * COLS + (uint8_t)cx];
}

/* そこへ広がれるか。**坑道で、黒ダイヤでも岩でも水風呂でもないこと。** */
static uint8_t flow_ok(int8_t cx, int8_t cy)
{
    uint16_t i;
    if (cx < 0 || cx >= COLS || cy < 0 || cy >= ROWS) {
        return 0;
    }
    i = (uint16_t)(uint8_t)cy * COLS + (uint8_t)cx;
    if (water[i] || !dug[i] || coal[i]) {
        return 0;
    }
    if (rock_at(cx, cy) >= 0) {
        return 0;                       /* 落ちた岩が堰き止める */
    }
    if (bath_on && (uint8_t)cx == bath_c && (uint8_t)cy == bath_r) {
        return 0;                       /* 水風呂は残す */
    }
    if ((uint8_t)cx == nest_c && (uint8_t)cy == nest_r) {
        return 0;                       /* **敵の巣は水没させない。** 塞ぐと敵が一体も
                                           出て来られなくなり、面が骨抜きになる */
    }
    return 1;
}

static void wet(int8_t cx, int8_t cy)
{
    uint16_t i = (uint16_t)(uint8_t)cy * COLS + (uint8_t)cx;
    water[i] = 1;
    if (wn < WATER_MAX) {
        wlist[wn] = (uint8_t)i;
    }
    wn++;
    /* **升をまるごと描き直さない。** そこは既に坑道として描けているので、
       上から水のタイルを重ねるだけでよい (外周2pxの土の膜も残る)。
       field_redraw_cell() だと土と坑道を塗り直すぶん VRAM への書き込みが3倍になる。 */
    draw_water((uint8_t)cx, (uint8_t)cy);
    /* **真下が水なら、そこはもう水面ではない。** 水中の姿へ描き直す。 */
    if (cy + 1 < ROWS && water[i + COLS]) {
        draw_water((uint8_t)cx, (uint8_t)(cy + 1));
    }
}

/* 落盤と同時に呼ぶ。湧き出し口の升が水になる。 */
void water_start(void)
{
    if (!spring_on || water_on) {
        return;
    }
    water_on = 1;
    wt = WATER_STEP;
    if (flow_ok((int8_t)spring_c, (int8_t)spring_r)) {
        wet((int8_t)spring_c, (int8_t)spring_r);
    }
}

/*
 * 毎フレーム呼ぶ。**WATER_STEP フレームに1升だけ**広げる。
 * 下 → 横 → 上 の順に探し、最初に見つかった1升を水にする。
 * 1升ずつなのは、読めるようにするためと、背景の書き換えを1枚に抑えるため。
 */
void water_tick(void)
{
    uint8_t k, pass, n;
    int8_t c, r;

    if (!water_on || wn >= WATER_MAX) {
        return;
    }
    wt--;
    if (wt) {
        return;
    }
    wt = WATER_STEP;
    n = (uint8_t)((wn < WATER_MAX) ? wn : WATER_MAX);   /* wlist の範囲に収める */
    for (pass = 0; pass < 3; pass++) {
        k = n;
        while (k--) {                  /* 後から水になった升 (=下や先) から見る */
            c = (int8_t)(wlist[k] % COLS);
            r = (int8_t)(wlist[k] / COLS);
            if (pass == 0) {
                if (flow_ok(c, (int8_t)(r + 1))) { wet(c, (int8_t)(r + 1)); return; }
            } else if (pass == 1) {
                if (flow_ok((int8_t)(c - 1), r)) { wet((int8_t)(c - 1), r); return; }
                if (flow_ok((int8_t)(c + 1), r)) { wet((int8_t)(c + 1), r); return; }
            } else {
                if (flow_ok(c, (int8_t)(r - 1))) { wet(c, (int8_t)(r - 1)); return; }
            }
        }
    }
    /* 行き先が無いときは、そのまま待つ。**自機が掘れば、そこへ流れ込む。** */
}
