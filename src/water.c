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
 *   - **土の膜で仕切られた坑道へは行かない。** 湧き出し口から**つながった**坑道だけを
 *     満たす。膜を無視していたときは、壁の向こうの坑道へ水がしみ出して見えた
 *   - **下を優先**して広がる。水らしく見え、行き先も読める
 *   - **落ちた岩は水を止める**（既存の「岩は落ちて止まる」だけで説明が付く）
 *   - **採掘は妨げない。** 水で黒ダイヤが採れなくなると面が詰む
 *   - **水風呂と敵の巣へは広がらない。** 水風呂を奪うと立て直せず、
 *     巣を塞ぐと敵が一体も出て来られなくなって面が骨抜きになる
 *   - **拾い物 (火薬・落ちたダイナマイト・梅干し) は水に浮く。** 水は止めないが、
 *     絵は水の上へ描き直す (field.c の draw_water)。沈めると見えないまま
 *     そこにあることになる
 *   - **`WATER_MAX` 升で止まる。** 際限なく広がると、掘るほど詰む盤面になる
 */
#include "game.h"
#include "sound.h"

uint8_t water[NCELL];          /* WATER_FLOW = 広がる熱水 / WATER_POOL = 溜まり湯 (field.c が置く) */
uint8_t water_on;              /* 湧き出しているか */
uint8_t water_done;            /* 止まったか。止まったら二度と増えない */
uint8_t spring_c, spring_r;    /* 湧き出し口 (面の開始から見えている) */
uint8_t spring_on;

static uint8_t wt;             /* 次に1升広がるまでのフレーム数 */
static uint8_t wn;             /* いま水になっている升の数 */
static uint8_t wstall;         /* 広がれなかった回数 (続けて) */
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
    water_done = 0;
    spring_on = 0;
    wt = 0;
    wn = 0;
    wstall = 0;
}

uint8_t cell_water(int8_t cx, int8_t cy)
{
    if (cx < 0 || cx >= COLS || cy < 0 || cy >= ROWS) {
        return 0;
    }
    return water[(uint16_t)(uint8_t)cy * COLS + (uint8_t)cx];
}

/* そこへ広がれるか。**坑道で、黒ダイヤでも岩でも水風呂でもないこと。**
   湧き出し口を置くとき、水の行き場を数えるのに field.c からも使う。 */
uint8_t flow_ok(int8_t cx, int8_t cy)
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
    water[i] = WATER_FLOW;
    if (wn < WATER_MAX) {
        wlist[wn] = (uint8_t)i;
    }
    wn++;
    /* **升をまるごと描き直さない。** そこは既に坑道として描けているので、水を塗るだけ。
       **つながっている隣の水も描き直す。** 隣が水になったぶん縁の 2px が塗り広がり、
       真下の升は水面から水中の姿に変わる。 */
    draw_water((uint8_t)cx, (uint8_t)cy);
    if (cell_linked(cx, cy, DIR_DOWN) && cell_water(cx, (int8_t)(cy + 1))) {
        draw_water((uint8_t)cx, (uint8_t)(cy + 1));
    }
    if (cell_linked(cx, cy, DIR_UP) && cell_water(cx, (int8_t)(cy - 1))) {
        draw_water((uint8_t)cx, (uint8_t)(cy - 1));
    }
    if (cell_linked(cx, cy, DIR_LEFT) && cell_water((int8_t)(cx - 1), cy)) {
        draw_water((uint8_t)(cx - 1), (uint8_t)cy);
    }
    if (cell_linked(cx, cy, DIR_RIGHT) && cell_water((int8_t)(cx + 1), cy)) {
        draw_water((uint8_t)(cx + 1), (uint8_t)cy);
    }
}

/*
 * **止める。二度と増えない。** 湧き出し口の絵が「塞がった絵」に変わり、「シュウ…」と鳴る。
 * 以前は止まっても何の印も無く、24升に達して止まったのか、行き先が無くて待っている
 * だけ (掘ればまた流れ込む) なのかが分からなかった (作者の指摘、2026-09-22)。
 */
static void water_stop(void)
{
    uint8_t k = (uint8_t)((wn < WATER_MAX) ? wn : WATER_MAX);
    water_on = 0;
    water_done = 1;
    /* **縁に「せき止め」の印を出す。** 描き直すと draw_water が印を付ける。
       止まった場所が湧き出し口だけでなく**全方位で分かる**ようにするため。 */
    while (k--) {
        draw_water((uint8_t)(wlist[k] % COLS), (uint8_t)(wlist[k] / COLS));
    }
    draw_spring(spring_c, spring_r);
    snd_sfx(SFX_SEAL);
}

/*
 * 落盤と同時に呼ぶ。**水はここでは出さず、1秒後に湧き出し口から出す。**
 * 落盤の「ゴゴゴ」と閃光に紛れて、離れた場所で静かに始まると気付けない
 * (作者の指摘「よくわからなかった」)。1拍置いて「ゴボッ」と鳴らせば、
 * 原因 (天盤が落ちた) と結果 (湯が噴いた) が分かれる。
 */
void water_start(void)
{
    if (!spring_on || water_on) {
        return;
    }
    water_on = 1;
    wt = WATER_STEP;
}

/*
 * 毎フレーム呼ぶ。**WATER_STEP フレームに1升だけ**広げる。
 * 下 → 横 → 上 の順に探し、最初に見つかった1升を水にする。
 * 1升ずつなのは、読めるようにするためと、背景の書き換えを1枚に抑えるため。
 */
/*
 * **1回の増水で、その向きへ行ける升を全部いっぺんに満たす。**
 * 以前は1升ずつだったので、左右に分かれた坑道で「なぜ先に左だけ流れるのか」が
 * プレイヤーに読めなかった (作者の指摘、2026-09-23)。水は分かれ道では両方へ流れる。
 *
 * 向きの優先は残す。**下へ行ける升があれば下だけ、無ければ横、最後に上。**
 * 候補は water[] に 2 を立てて印を付ける (二重に数えない。この升からさらに次の升へ
 * 広がることもない)。数えてから、入れるか取り消すかを決める。
 */
static uint8_t mark_class(uint8_t pass)
{
    uint8_t k, n = 0, lim = (uint8_t)((wn < WATER_MAX) ? wn : WATER_MAX);
    int8_t c, r;

    k = lim;
    while (k--) {
        c = (int8_t)(wlist[k] % COLS);
        r = (int8_t)(wlist[k] / COLS);
        if (pass == 0) {
            if (cell_linked(c, r, DIR_DOWN) && flow_ok(c, (int8_t)(r + 1))) {
                water[(uint16_t)(uint8_t)(r + 1) * COLS + (uint8_t)c] = 2; n++;
            }
        } else if (pass == 1) {
            if (cell_linked(c, r, DIR_LEFT) && flow_ok((int8_t)(c - 1), r)) {
                water[(uint16_t)(uint8_t)r * COLS + (uint8_t)(c - 1)] = 2; n++;
            }
            if (cell_linked(c, r, DIR_RIGHT) && flow_ok((int8_t)(c + 1), r)) {
                water[(uint16_t)(uint8_t)r * COLS + (uint8_t)(c + 1)] = 2; n++;
            }
        } else {
            if (cell_linked(c, r, DIR_UP) && flow_ok(c, (int8_t)(r - 1))) {
                water[(uint16_t)(uint8_t)(r - 1) * COLS + (uint8_t)c] = 2; n++;
            }
        }
    }
    return n;
}

/* 印を消す (増水を取り消す)。 */
static void unmark(void)
{
    uint16_t i;
    for (i = 0; i < NCELL; i++) {
        if (water[i] == 2) {
            water[i] = 0;
        }
    }
}

/* 印の付いた升を実際に水にする。 */
static void commit(void)
{
    uint16_t i;
    for (i = 0; i < NCELL; i++) {
        if (water[i] == 2) {
            water[i] = 0;                      /* wet() が数と絵を面倒みる */
            wet((int8_t)(i % COLS), (int8_t)(i / COLS));
        }
    }
}

void water_tick(void)
{
    uint8_t pass, n = 0;

    if (!water_on) {
        return;
    }
    wt--;
    if (wt) {
        return;
    }
    wt = WATER_STEP;
    if (wn == 0) {                     /* 最初の1升 = 湧き出し口から噴き出す */
        /* **まだ一滴も出ていないうちは諦めない。** 湧き出し口に岩が載っていると
           出られないが、岩は押せるし落盤でも動くので、**待っていればいつか出る。**
           ここで止めてしまうと、岩が1つ載っているだけで**その面の出水が丸ごと
           無かったことになっていた** (面9 が実際にそうだった。実機で見つけた)。 */
        if (flow_ok((int8_t)spring_c, (int8_t)spring_r)) {
            wet((int8_t)spring_c, (int8_t)spring_r);
            snd_sfx(SFX_GUSH);
        }
        return;
    }
    for (pass = 0; pass < 3; pass++) {
        n = mark_class(pass);
        if (n) {
            break;
        }
    }
    if (n == 0) {
        /* 行き先が無い。**WATER_STALL 回続いたら止める。** それまでに自機が掘れば流れ込む。 */
        if (++wstall >= WATER_STALL) {
            water_stop();
        }
        return;
    }
    /* **次の増水で上限を超えるならやらずに止める。** 分かれ道で同時に流れるので
       ちょうど WATER_MAX 升にはならない。「ここで止まる」は縁の印で見せる。 */
    if ((uint8_t)(wn + n) > WATER_MAX) {
        unmark();
        water_stop();
        return;
    }
    wstall = 0;
    commit();
    snd_sfx(SFX_DRIP);                 /* 増水1回につき1つ鳴らす */
}
