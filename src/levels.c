/*
 * SakiyamaBlast - 面ごとの敵の表（難易度の階段）
 *
 * **幕ごとののこぎり波にする (2026-10-12)。**
 * もとは面番号に対する式3本（プール数・静養・透明化）で、**面が進むほど
 * 一直線に厳しくなる**作りだった。幕の頭 (面4・7・10・13) では新しい仕掛け
 * (樋・出水・鉱車) が増えるので、**覚える面が同時に一番難しい面**になっていた。
 *
 * 式をやめて面ごとの表にしてある。読み方:
 *
 *   幕の頭 (▼) … 敵を1体減らし、静養と透明化を戻す = その幕の道具を覚える面
 *   2面目     … 前の幕の峰と同じ数へ戻す = 「前にやった」と感じる
 *   幕の峰 (★) … 前の峰を越える = 試される面
 *
 * 峰だけを見れば 面3 → 6 → 9 → 12 → 15 と単調に上がり、谷は直前の峰より必ず低い。
 *
 * **プールは4を割らない (面1〜4 を除く)。** 敵の性格は `i % 4` の順
 * (赤→桃→水→橙) に配るので、プールが3になると**橙が盤面から消える**。
 * 色はアルゴリズムの印なので、一度出た性格が1面だけ消えると嘘になる。
 * そこで橙のお披露目を面5に置き、以後プールは4以上に保つ
 * (面3の峰はプールではなく静養で作る。作者の判断、2026-10-12)。
 *
 * **第五坑 (面13〜15) は一直線の最終坂。** 新しい仕掛けが無いので幕の型が
 * 当てはまらない。プールの上限 (6体 = スプライト面の割り当て) にも当たるので、
 * 差は**切羽圧の税率**で付ける。「掘るほど早く透けてくる」が終盤の歯応え。
 *
 * **置き場はバンク窓 (`BANKED_SRC`)。** 表は 15バイト x 5 = 75バイトで、
 * enemy.c のある RAM 実行領域には入らない (空きは数十バイトしかない)。
 * 窓は常に `DATA_SEG` を向いているので、RAM 側のコードからそのまま読める。
 */
#include "game.h"
#include "vdp.h"

/* 10フレーム単位で持つ。1面1バイトに収めるため (600 は uint8_t に入らない)。 */
#define F10(sec) (uint8_t)((sec) * 6)      /* 秒 -> 10フレーム単位 (60fps) */

/*            面:  1   2   3★  4▼  5   6★  7▼  8   9★ 10▼ 11  12★ 13▼ 14  15★ */
static const uint8_t pool_tbl[MAX_STAGE] = {
              3,  3,  3,  3,  4,  5,  4,  5,  5,  4,  5,  6,  5,  6,  6 };

/* 倒された敵が洞窟にこもる時間。短いほど早く戻ってくる。 */
static const uint8_t rest_tbl[MAX_STAGE] = {
       F10(10.0), F10(9.5), F10(9.0), F10(9.0), F10(8.5), F10(8.0), F10(8.0),
       F10(7.0), F10(6.0), F10(6.5), F10(5.5), F10(4.5), F10(5.0), F10(4.0),
       F10(4.0) };

/* 透明化までの時間 (**掘る前**)。掘るほど切羽圧で縮む。 */
static const uint8_t ghost_tbl[MAX_STAGE] = {
       F10(10.0), F10(9.5), F10(9.0), F10(9.5), F10(9.0), F10(8.5), F10(9.0),
       F10(8.0), F10(7.5), F10(8.0), F10(7.0), F10(6.5), F10(6.5), F10(6.0),
       F10(5.5) };

/* 切羽圧。1升掘るごとに透明化が縮むフレーム数 = **掘ることへの税率**。 */
static const uint8_t press_tbl[MAX_STAGE] = {
              3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  4,  4,  5 };

/* 透明化までの下限。どれだけ掘ってもこれより短くはならない。 */
static const uint8_t gmin_tbl[MAX_STAGE] = {
       F10(5.0), F10(5.0), F10(5.0), F10(5.0), F10(5.0), F10(5.0), F10(5.0),
       F10(5.0), F10(5.0), F10(5.0), F10(5.0), F10(5.0), F10(5.0), F10(4.5),
       F10(4.0) };

static uint8_t lv_at(const uint8_t *tbl)
{
    uint8_t s = (uint8_t)((stage >= 1 && stage <= MAX_STAGE) ? stage : 1);
    return tbl[s - 1];
}

uint8_t lv_pool(void)
{
    uint8_t n = lv_at(pool_tbl);
    return (uint8_t)((n > ENEMY_MAX) ? ENEMY_MAX : n);
}

uint16_t lv_rest(void)
{
    return (uint16_t)((uint16_t)lv_at(rest_tbl) * 10);
}

uint16_t lv_ghost(void)
{
    return (uint16_t)((uint16_t)lv_at(ghost_tbl) * 10);
}

uint16_t lv_ghost_min(void)
{
    return (uint16_t)((uint16_t)lv_at(gmin_tbl) * 10);
}

uint8_t lv_press(void)
{
    return lv_at(press_tbl);
}


/*
 * --- 色 (土の市松と HUD の帯、落盤の閃光) ---
 *
 * **固定領域から逃がしてここへ置いた** (2026-10-24)。土を市松の2色にしたら
 * `vdp_set_palette()` の呼び出しが5組に増え、固定領域 (残り74バイト) が溢れた。
 * 面ごとの表と同じ性質のものなので、置き場もここが素直。
 * **窓は常に `DATA_SEG` を向いている**ので、固定領域からも RAM 側からも呼べる。
 */
/* 落盤の閃光。土の色を白へ飛ばし、FLASH_CAVEIN フレームで元へ戻す。
   炭柱を割ると離れた場所の岩まで動くが、プレイヤーは1箇所しか見ていないので
   何が起きたか分からない。画面全体の色を変えれば、見ていなくても伝わる。 */
static uint8_t flash_t;

/* 土の色を lvl の明るさへ飛ばし、frames かけて元へ戻す。
   **明るさを段階にできるようにしてある。** 連鎖は同じ強さで光らせると
   1回目も5回目も同じに見えて、せっかくの階段が読めない。 */
void field_flash(uint8_t frames, uint8_t lvl)
{
    flash_t = frames;
    vdp_set_palette(COL_DIRT, lvl, lvl, lvl);
    vdp_set_palette(COL_DIRT2, lvl, lvl, lvl);   /* 粒も一緒に飛ばす */
}

/* 閃光を取り消す。面の切り替えとミスで呼ぶ (`field_fx_reset`)。
   **土の色を戻すのは呼び手の仕事** (面が変われば `set_dirt_palette()` が走る)。 */
void flash_reset(void)
{
    flash_t = 0;
}

void cavein_flash(void)
{
    field_flash(FLASH_CAVEIN, 7);      /* 落盤は常に全開 */
}

void cavein_flash_tick(void)
{
    if (!flash_t) {
        return;
    }
    flash_t--;
    if (!flash_t) {
        set_dirt_palette();          /* 面ごとの土の色へ戻す */
    }
}

/*
 * 面ごとの色。閃光から戻すときにも使う。
 *
 * **土の色は幕 (坑の深さ) で5段階。** 以前は3面ごとに茶・灰・青を繰り返していたので、
 * 面11 と面2 が同じ色だった。深くなるほど暗く、青から紫へ寄せて「降りている」ことを
 * 色で出す。常磐の炭層は海側へ傾斜し、深いほど条件が悪かった (docs/HISTORY.md §5.3)。
 *
 * **HUD の帯の色は番方 (一番方・二番方・三番方) で変える。** 坑外の時間帯 (朝・昼・夜)
 * のつもりで、どれも暗いままにして白と黄の文字が読めるようにしてある。
 * パレットを書くだけなので、どちらも fps には影響しない。
 */
void set_dirt_palette(void)
{
    /* **土は2色。** 地色の上に粒 (`draw_grain()`) を散らして、一色のべた塗りに
       しないためのもの。
       **差は明度で付ける。** はじめ第一坑を (4,2,1) と (3,2,1) にしたが、違うのは
       赤の1段だけで**明るさがほぼ同じ**だった。色相だけの差は、見る人の色覚や画面に
       よっては出ない (作者には見えなかった)。2色目は RGB をまとめて落として
       **明度で 0.9〜1.0 の差**を付けてある (明度 = 0.3R + 0.59G + 0.11B)。
       **第五坑だけ逆に明るいほう**を使う。あそこの土はもう闇に近く、暗いほうへ
       振ると掘った坑道 (黒) と見分けがつかなくなる。 */
    switch (STAGE_ACT(stage)) {
    case 1:  vdp_set_palette(COL_DIRT, 4, 2, 1);          /* 第一坑 炭層まじりの茶 */
             vdp_set_palette(COL_DIRT2, 3, 1, 1); break;
    case 2:  vdp_set_palette(COL_DIRT, 3, 3, 2);          /* 第二坑 硬い岩盤の灰 */
             vdp_set_palette(COL_DIRT2, 2, 2, 1); break;
    case 3:  vdp_set_palette(COL_DIRT, 2, 2, 4);          /* 第三坑 湧水を含む青 */
             vdp_set_palette(COL_DIRT2, 1, 1, 3); break;
    case 4:  vdp_set_palette(COL_DIRT, 2, 1, 3);          /* 第四坑 深い藍 */
             vdp_set_palette(COL_DIRT2, 1, 0, 2); break;
    default: vdp_set_palette(COL_DIRT, 1, 1, 2);          /* 第五坑 ほとんど闇 */
             vdp_set_palette(COL_DIRT2, 2, 2, 3); break;
    }
    switch (STAGE_SHIFT(stage)) {
    case 1:  vdp_set_palette(COL_HUD, 1, 2, 2); break;    /* 一番方 朝 */
    case 2:  vdp_set_palette(COL_HUD, 2, 2, 1); break;    /* 二番方 昼 */
    default: vdp_set_palette(COL_HUD, 1, 1, 3); break;    /* 三番方 夜 */
    }
}

/*
 * 土の肌。**一色のべた塗りだと面がのっぺりする**ので、升の中に濃い粒を散らす
 * (炭層まじりの土のつもり)。粒は x, y, 幅, 高さ の4つ組で、**並びを2通り持って
 * 升ごとに替える** — 176升に同じ粒が並ぶと、模様そのものが目に付いてしまう。
 *
 * **塗りは VDP に任せる** (`vdp_fill` = LMMV)。1升あたり 地色1回 + 粒3回で、
 * 面の組み立てでは全面を1回塗ってから粒だけを置く。
 */
#define GRAIN_N 3
static const uint8_t grain[2][GRAIN_N * 4] = {
    {  2,  3, 4, 2,    9,  8, 3, 3,    5, 12, 2, 2 },
    { 11,  2, 3, 2,    3,  7, 2, 3,    8, 11, 4, 2 }
};

/* 升の中に粒だけを置く (地色は塗ってある前提)。
   **固定領域から逃がしてここへ置いた** (2026-10-24)。表も塗りも面の見た目の話で、
   窓は塗っている間ずっと `DATA_SEG` を向いているので固定領域から呼べる。 */
void draw_grain(uint8_t cx, uint8_t cy)
{
    uint8_t i;
    const uint8_t *g = grain[(uint8_t)((cx ^ cy) & 1)];
    uint16_t x = (uint16_t)(cx << 4);
    uint16_t y = (uint16_t)(cy << 4);
    for (i = 0; i < GRAIN_N * 4; i += 4) {
        vdp_fill((uint16_t)(x + g[i]), (uint16_t)(y + g[i + 1]),
                 g[i + 2], g[i + 3], COL_DIRT2);
    }
}

