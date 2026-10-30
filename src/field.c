/* SakiyamaBlast - 盤面 (掘り地図・火薬・採掘と描画)
 *
 * **面の組み立ては `stagegen.c` に移した** (2026-10-30)。あちらは 0x8000 の窓に
 * 置いてあるので、固定領域に残すのは「遊んでいる間に使うもの」だけ。
 * ここの `draw_*` と `cell_*` は組み立て側からも呼ばれる (固定領域は常に見える)。 */
#include "game.h"
#include "vdp.h"
#include "art.h"
#include "text.h"
#include "sound.h"

uint8_t  cflag[NCELL];      /* 升ごとの真偽値 (CF_*)。game.h の説明を参照 */
uint8_t  chute[NCELL];
uint8_t  coal[NCELL];
uint8_t  coal_left;
uint8_t  combo;
uint8_t  hitstop;
uint8_t  combo_t;               /* HUD に連鎖数を出しておく残り時間 */
uint16_t dug_n;
uint16_t dug_n0;
uint8_t  dug_pct;
uint8_t  stage;
uint8_t  start_c, start_r;
uint8_t  nest_c, nest_r;
uint8_t  bath_c, bath_r;
uint8_t  bath_on;
uint8_t  bathing;

/* cx,cy から dir の向きへ、土の膜に邪魔されずに通れるか。
   左/上は「隣の升から見た右/下」と同じ境目なので、そちらへ読み替えて1箇所で読む。 */
uint8_t cell_linked(int8_t cx, int8_t cy, uint8_t dir)
{
    uint8_t mask = LINK_RIGHT;
    if (dir == DIR_LEFT) {
        cx = (int8_t)(cx - 1);
    } else if (dir == DIR_UP) {
        cy = (int8_t)(cy - 1);
        mask = LINK_DOWN;
    } else if (dir == DIR_DOWN) {
        mask = LINK_DOWN;
    }
    if (cx < 0 || cx >= COLS || cy < 0 || cy >= ROWS) {
        return 0;
    }
    return (uint8_t)(cf_on((uint16_t)(uint8_t)cy * COLS + (uint8_t)cx, mask));
}

/*
 * 自機の掘り跡が膜を抜いた分を記録する。掘り跡は px+2..px+13 の 12px なので、
 * 升の境目 (16の倍数) を跨ぐのは端から3..13px 入ったときだけ。
 */
void link_carve(uint8_t px_, uint8_t py_)
{
    uint8_t m = (uint8_t)(px_ & 15), n = (uint8_t)(py_ & 15);
    uint8_t c = (uint8_t)(px_ >> 4), r = (uint8_t)(py_ >> 4);
    uint16_t k = (uint16_t)r * COLS + c;
    /* px は XMAX、py は YMAX で頭打ちなので c<=15 / r<=10 は保証されている。
       境目そのものが無い右端・下端だけ除く。 */
    if (n == 0) {
        if (m >= 3 && m <= 13 && c + 1 < COLS) {
            cf_set(k, LINK_RIGHT);
        }
        return;
    }
    if (m == 0) {
        if (n >= 3 && n <= 13 && r + 1 < ROWS) {
            cf_set(k, LINK_DOWN);
        }
    }
}

/* 割れた黒ダイヤの升と、隣り合う掘れている升との膜を開く。
   割れた升を「本当に通れる通路」にするためのもの。これをやらないと、
   炭層を全部割っても敵は通れず、リスクの無いただの得点になってしまう。 */
/* 機構③の連続発動よけ。直前に埋め戻した升だけ覚えておく。
   割り直した直後にまた踏まれると理不尽になるため。 */
static uint8_t refill_cell = 0xFF;
static uint16_t refill_t;

/*
 * 敵が踏み抜けた「割れたまま未採掘の黒ダイヤ」を耐久 REFILL_HP で塞ぎ直す。
 *
 * **踏んでいる最中ではなく、踏み抜けたあとに呼ぶこと。** 敵が居る升を塞ぐと、
 * その敵は掘ってある升しか歩けないので永久に動けなくなる。
 * 自機が居る升も同じ理由で塞がない。
 *
 * **dug_n は減らさない。** 切羽圧は dug_n0 からの増分で測っているので、
 * ここで減らすと「埋め戻されるほど安全になる」という逆向きの力が働く。
 */
uint8_t coal_refill_at(int8_t cx, int8_t cy)
{
    uint16_t i;
    if (cx < 0 || cx >= COLS || cy < 0 || cy >= ROWS) {
        return 0;
    }
    i = (uint16_t)(uint8_t)cy * COLS + (uint8_t)cx;
    if (coal[i] != COAL_CRACKED) {
        return 0;                       /* 割れて採掘待ちの升だけが対象 */
    }
    if (cf_on(i, CF_PILLAR)) {
        return 0;                       /* 炭柱は耐久が高く、割り直しの代償が大きすぎる */
    }
    if (refill_t && refill_cell == (uint8_t)i) {
        return 0;                       /* 同じ升を続けて塞がない */
    }
    if ((int8_t)player_cell_c() == cx && (int8_t)player_cell_r() == cy) {
        return 0;                       /* 自機を土に埋めない */
    }
    if (enemy_home_at(cx, cy)) {
        return 0;                       /* 別の敵が居座っている升も塞がない */
    }
    coal[i] = REFILL_HP;
    cf_clr(i, CF_DUG);                         /* 通れなくなる。dug_n は据え置き */
    cf_clr(i, LINK_RIGHT | LINK_DOWN);  /* 土の膜も閉じる */
    if (cx > 0) {
        cf_clr(i - 1, LINK_RIGHT);
    }
    if (cy > 0) {
        cf_clr(i - COLS, LINK_DOWN);
    }
    refill_cell = (uint8_t)i;
    refill_t = REFILL_COOLDOWN;
    field_redraw_cell(cx, cy);
    field_redraw_cell(cx, (int8_t)(cy - 1));
    field_redraw_cell(cx, (int8_t)(cy + 1));
    field_redraw_cell((int8_t)(cx - 1), cy);
    field_redraw_cell((int8_t)(cx + 1), cy);
    /* **何が起きたか分からないまま仕込みが消えるのが最悪。**
       低い「コツン」で、盤面を取り返されたことを知らせる。優先度は最低なので
       採掘音や破砕音を潰さない。 */
    snd_sfx(SFX_NOPE);
    return 1;
}

void link_open(uint8_t cx, uint8_t cy)
{
    uint16_t k = (uint16_t)cy * COLS + cx;
    if (cx + 1 < COLS && cell_dug((int8_t)(cx + 1), (int8_t)cy)) {
        cf_set(k, LINK_RIGHT);
    }
    if (cx > 0 && cell_dug((int8_t)(cx - 1), (int8_t)cy)) {
        cf_set(k - 1, LINK_RIGHT);
    }
    if (cy + 1 < ROWS && cell_dug((int8_t)cx, (int8_t)(cy + 1))) {
        cf_set(k, LINK_DOWN);
    }
    if (cy > 0 && cell_dug((int8_t)cx, (int8_t)(cy - 1))) {
        cf_set(k - COLS, LINK_DOWN);
    }
}


/* 黒ダイヤが割れた瞬間、画面を縦へ揺らす。表示位置ずらし (R#18) を書くだけなので、
   盤面を描き直すより桁違いに安い。振幅は 2 -> 1 と減衰させる。 */
static uint8_t shake_t;
/* 採掘した升の閃き。**連鎖採掘で複数同時に採るので、升を覚えるのも複数要る。**
   1升ぶんしか持っていなかったため、連鎖すると最後の1つ以外が白いまま残っていた。 */
static uint8_t get_t, get_n;
static uint8_t get_cs[CHAIN_MAX], get_rs[CHAIN_MAX];
static void get_flash_clear(void);

/* **採掘音の順番待ち。** 落盤で岩が同時に落ちると、同じフレームに複数の升を採る。
   効果音は1つしか鳴らないので、そのままだと連鎖の階段が耳で聞こえない。
   **処理は同時のまま、音だけを COAL_SND_GAP フレームおきに1つずつ出す。**
   升が違えば表示 (白い閃き・得点) は重ならないので、ずらすのは音だけでよい。 */
static uint8_t csnd_p[8];       /* 順番待ちの音程 (リング) */
static uint8_t csnd_h, csnd_n, csnd_t;


/* 溜め中の汗。段が上がるほど滴が増える。体温を払っていることが絵で伝わる。 */
static uint8_t sweat_c, sweat_r, sweat_lv;

void clear_sweat(void)
{
    if (!sweat_lv) {
        return;
    }
    sweat_lv = 0;
    field_redraw_cell((int8_t)sweat_c, (int8_t)sweat_r);
    player_carve();
}

/*
 * 溜め中の見た目。**すべて背景へ描く。**
 * 自機のスプライトは16x16がほぼ埋まっていて1行1色なので、つるはしも汗も足す余地が
 * 無く、面を増やすと横一列8枚の制限に触れる。溜め中は必ず静止しているので、
 * 背景に描いて離したら消せる。構えの絵は上3行を空けてあり、そこへ振りかぶった
 * つるはしが入る。
 */
void draw_charge_fx(uint8_t cx, uint8_t cy, uint8_t level)
{
    uint16_t x = (uint16_t)((uint16_t)cx << 4);
    uint16_t y = (uint16_t)((uint16_t)cy << 4);
    if (sweat_lv == level && sweat_c == cx && sweat_r == cy) {
        return;                          /* 変わっていないので描き直さない */
    }
    clear_sweat();
    sweat_c = cx;
    sweat_r = cy;
    sweat_lv = level;
    if (!level) {
        return;
    }
    /* 振りかぶったつるはし。柄を斜めに、頭を横棒で。溜めるほど高く振り上げる。 */
    {
        uint8_t up = (uint8_t)(level - 1);          /* 0..2 */
        uint16_t hy = (uint16_t)(y + 3 - up);
        vdp_fill((uint16_t)(x + 4), hy, 8, 2, COL_WHITE);          /* 頭 */
        vdp_fill((uint16_t)(x + 7), (uint16_t)(hy + 2), 2, 3, COL_FUSE);  /* 柄 */
    }
    /* 汗。自機のスプライトが真ん中を覆うので端へ散らす。 */
    vdp_fill((uint16_t)(x + 1), (uint16_t)(y + 6), 2, 2, COL_WHITE);
    if (level >= 2) {
        vdp_fill((uint16_t)(x + 13), (uint16_t)(y + 7), 2, 2, COL_WHITE);
        vdp_fill((uint16_t)(x + 2), (uint16_t)(y + 13), 2, 2, COL_WHITE);
    }
    if (level >= 3) {
        vdp_fill((uint16_t)(x + 12), (uint16_t)(y + 13), 2, 2, COL_WHITE);
        vdp_fill((uint16_t)(x + 0), (uint16_t)(y + 10), 2, 2, COL_WHITE);
        vdp_fill((uint16_t)(x + 14), (uint16_t)(y + 10), 2, 2, COL_WHITE);
    }
}

/*
 * 振り下ろしの軌跡。振っている18フレームの間、つるはしが振り上げた位置から
 * 的の側へ降りていく。**背景に描く。** 自機は振っている間その場に固定されるので、
 * 溜めの絵と同じやり方で描いて消せる。
 * phase は 0(振り上げ) → 1(中間) → 2(振り切り)。
 */
void draw_swing_fx(uint8_t cx, uint8_t cy, uint8_t dir, uint8_t phase)
{
    uint16_t x = (uint16_t)((uint16_t)cx << 4);
    uint16_t y = (uint16_t)((uint16_t)cy << 4);
    int8_t hx, hy;
    if (sweat_lv == (uint8_t)(0x80 | phase) && sweat_c == cx && sweat_r == cy) {
        return;
    }
    clear_sweat();
    sweat_c = cx;
    sweat_r = cy;
    sweat_lv = (uint8_t)(0x80 | phase);   /* 溜めの汗と同じ枠で消す */
    /* 振り上げ (真上) から、向いている側の高さへ降ろす。 */
    hx = 4;
    hy = (int8_t)(1 + (phase << 1));
    if (phase >= 1) {
        if (dir == DIR_LEFT)  { hx = (int8_t)(hx - 3 * phase); }
        if (dir == DIR_RIGHT) { hx = (int8_t)(hx + 3 * phase); }
        if (dir == DIR_DOWN)  { hy = (int8_t)(hy + 4 * phase); }
        if (dir == DIR_UP)    { hy = (int8_t)(1 - phase); }
    }
    if (hx < 0)  { hx = 0; }
    if (hx > 8)  { hx = 8; }
    if (hy < 0)  { hy = 0; }
    if (hy > 12) { hy = 12; }
    vdp_fill((uint16_t)(x + (uint8_t)hx), (uint16_t)(y + (uint8_t)hy), 8, 2, COL_WHITE);
    vdp_fill((uint16_t)(x + (uint8_t)hx + 3), (uint16_t)(y + (uint8_t)hy + 2), 2, 3, COL_FUSE);
}

/*
 * 演出の状態を全部落とす。面の切り替えとミスの前に呼ぶ。
 * **揺れ (R#18) は面を組み直しても元に戻らない。** 揺れている最中に面がクリアに
 * なると、そのずれたまま次の面が始まる (実際にそうなっていた)。
 * 閃光のパレットや採掘の閃きも、残ると次の面へ持ち越される。
 */
void field_fx_reset(void)
{
    shake_t = 0;
    flash_reset();
    get_t = 0;
    get_n = 0;
    csnd_n = 0;
    csnd_h = 0;
    csnd_t = 0;
    refill_t = 0;
    refill_cell = 0xFF;
    sweat_lv = 0;
    combo = 0;
    combo_t = 0;
    hitstop = 0;
    vdp_adjust(0, 0);          /* 画面を中央へ戻す */
    popup_clear();
}

/* 外から画面を揺らす。すでに揺れているときは長いほうを残す。 */
void field_shake(uint8_t frames)
{
    if (frames > shake_t) {
        shake_t = frames;
    }
}

/* 落ちてきた岩が (cx,cy) と1つ上の升の間の膜を突き破る。
   岩は薄皮を残して通り過ぎるのではなく、割って落ちる。 */
void link_smash(uint8_t cx, uint8_t cy)
{
    if (cx >= COLS || cy == 0 || cy >= ROWS) {
        return;
    }
    cf_set((uint16_t)(cy - 1) * COLS + cx, LINK_DOWN);
}

void coal_shake_tick(void)
{
    int8_t amp;

    /* **盤面が動いている間は連鎖の窓を閉じない。**
       揺れている岩・落ちている岩・滑っている岩が1つでもあれば数え始めない。
       固定の 180フレームだけで測ると、落下と滑りが続いている最中に窓が閉じて
       **まだ盤面が動いているのに連鎖が切れる**。ぷよの「連鎖が終わるまで数える」
       と同じ扱いにする。全部止まってから CHAIN_WINDOW の計測が始まる。 */
    if (combo_t && !rock_busy()) {
        combo_t--;
        if (!combo_t) {
            combo = 0;                  /* 連鎖が途切れた */
        }
    }

    /* 採掘した升の閃き。白 -> 黒ダイヤの色 と2段で光らせてから元へ戻す。
       自機が升の上に乗っているので、周りがふちどられるように見える。 */
    if (refill_t) {
        refill_t--;
    }
    /* 順番待ちの採掘音を1つ出す。 */
    if (csnd_n) {
        if (csnd_t) {
            csnd_t--;
        } else {
            snd_sfx_up(SFX_COAL, csnd_p[csnd_h]);
            csnd_h = (uint8_t)((csnd_h + 1) & 7);
            csnd_n--;
            csnd_t = COAL_SND_GAP;
        }
    }
    if (get_t) {
        uint8_t k;
        get_t--;
        if (get_t == (COAL_GET_FRAMES * 2 / 3)) {
            for (k = 0; k < get_n; k++) {
                vdp_fill((uint16_t)((uint16_t)get_cs[k] << 4),
                         (uint16_t)((uint16_t)get_rs[k] << 4), 16, 16, COL_DIA);
            }
        } else if (get_t == 0) {
            get_flash_clear();
        }
    }

    if (!shake_t) {
        return;
    }
    shake_t--;
    if (!shake_t) {
        vdp_adjust(0, 0);            /* 元の位置へ戻す */
        return;
    }
    /* 強い揺れのときは横にも振る。R#18 は縦横とも動かせるので追加費用はゼロ。 */
    amp = (int8_t)((shake_t > (COAL_SHAKE_FRAMES / 2)) ? 2 : 1);
    {
        int8_t v = (int8_t)((shake_t & 1) ? amp : (int8_t)-amp);
        int8_t h = (int8_t)((shake_t > COAL_SHAKE_FRAMES) ? ((shake_t & 2) ? 1 : -1) : 0);
        vdp_adjust(h, v);
    }
}

uint8_t cell_dug(int8_t cx, int8_t cy)
{
    if (cx < 0 || cx >= COLS || cy < 0 || cy >= ROWS) {
        return 0;
    }
    return cf_on((uint8_t)cy * COLS + (uint8_t)cx, CF_DUG);
}

/* 立ちはだかっている黒ダイヤなら1。割れたもの (COAL_CRACKED) は通れるので0。 */
uint8_t cell_coal(int8_t cx, int8_t cy)
{
    uint8_t v;
    if (cx < 0 || cx >= COLS || cy < 0 || cy >= ROWS) {
        return 0;
    }
    v = coal[(uint8_t)cy * COLS + (uint8_t)cx];
    return (uint8_t)((v != 0 && v != COAL_CRACKED) ? 1 : 0);
}

/*
 * 樋 (とい)。坑内で石炭を上の坑道から下へ落とすために斜めに架けた木の板。
 *
 * **絵そのものが規則になっている。** 右下がりの板なら岩は右へ、左下がりなら左へ。
 * 落とす前に、盤面を見るだけで岩の通り道を最後まで指でなぞれる。
 * つるはしでは壊せない。壊すのは落ちてきた岩だけ。
 */
uint8_t cell_chute(int8_t cx, int8_t cy)
{
    uint8_t v;
    if (cx < 0 || cx >= COLS || cy < 0 || cy >= ROWS) {
        return 0;
    }
    v = chute[(uint16_t)(uint8_t)cy * COLS + (uint8_t)cx];
    /* **折れた板 (CHUTE_BR / CHUTE_BL) は働かない。** 絵だけが跡として残る。 */
    return (uint8_t)((v == CHUTE_R || v == CHUTE_L) ? v : 0);
}

/* 落ちてきた岩が板を踏み抜いた。**曲がれたかどうかに関わらず必ず壊れる。**
   「板は必ず壊れる。曲がれれば曲がる」の1文で済ませるため。 */
void chute_break(int8_t cx, int8_t cy)
{
    uint16_t i;
    uint8_t v = cell_chute(cx, cy);
    if (!v) {
        return;
    }
    i = (uint16_t)(uint8_t)cy * COLS + (uint8_t)cx;
    /* **消さずに「折れた板」として残す。** 通行にも落下にも影響しない、ただの跡。
       これが無いと、あとから来た岩が曲がらずに真っ直ぐ落ちた理由が
       **画面のどこにも書かれていない**ことになる。 */
    chute[i] = (uint8_t)((v == CHUTE_R) ? CHUTE_BR : CHUTE_BL);
    field_redraw_cell(cx, cy);
    snd_sfx(SFX_WOOD);
}


/* つるはし1回ぶん。耐久度が0になったら割れた絵へ差し替えて1を返す。 */
uint8_t coal_hit(int8_t cx, int8_t cy)
{
    uint16_t i;
    if (!cell_coal(cx, cy)) {
        return 0;
    }
    i = (uint16_t)(uint8_t)cy * COLS + (uint8_t)cx;
    coal[i]--;
    if (coal[i] == 0) {
        {
            uint8_t was_pillar = cf_on(i, CF_PILLAR);
            coal[i] = COAL_CRACKED;
            cf_clr(i, CF_PILLAR);                     /* 割れたらもう支えではない */
            if (was_pillar) {
                rock_collapse();               /* 天盤を支える柱が消えた = 落盤 */
            }
        }
        shake_t = COAL_SHAKE_FRAMES;           /* 砕けた瞬間、画面を揺らす */
        snd_sfx(SFX_BREAK);
        dig_sweep((uint8_t)cx, (uint8_t)cy);   /* 砕けたので通れる升になる */
        /* **膜も開く。** 割れた升は本当に通路になり、炭層を全部割ると
           最大 COAL_VEIN_MAX 升の直線が開通して敵の高速道路になる。
           これが「全部割ってから一気に採る」のリスクそのもの。 */
        link_open((uint8_t)cx, (uint8_t)cy);
        draw_coal((uint8_t)cx, (uint8_t)cy);
        field_redraw_cell((int8_t)cx, (int8_t)(cy - 1));
        field_redraw_cell((int8_t)cx, (int8_t)(cy + 1));
        field_redraw_cell((int8_t)(cx - 1), (int8_t)cy);
        field_redraw_cell((int8_t)(cx + 1), (int8_t)cy);
        score_add(PTS_COAL_CRACK);
        return 1;
    }
    draw_coal((uint8_t)cx, (uint8_t)cy);   /* 残り耐久度の数字も描き直す */
    return 0;
}

/* 割れた黒ダイヤの升に入ったときの採掘。ここで初めて coal_left が減る。 */
/* 落ちてきた岩が、割れた黒ダイヤの升に居座ったときの後始末。
   岩の升には自機が入れないので、放っておくとその黒ダイヤは永久に回収できず
   「全採掘」のクリア条件を満たせなくなる。岩が砕き出した扱いで回収する。
   残柱式でいう「炭柱を割ったら天盤が落ちて潰す」そのものでもある。 */
/*
 * 落盤で、岩の真下の黒ダイヤを割る。
 *
 * 落盤を「全部の岩がN升沈む」にしていたが、沈んだ先が土なら見た目にも結果にも
 * 何も残らず、しかも**岩を炭層の上から土の中へ移してしまう**ので、
 * 「岩を黒ダイヤの上に置く」仕掛けを自分で壊していた。
 * 天盤が落ちる = **岩が載っている黒ダイヤが一斉に割れる**、に変えた。
 * 岩は支えを失って自然に落ち、割れた黒ダイヤは回収待ちで残る。
 */
void coal_crack_by_rock(int8_t cx, int8_t cy)
{
    uint16_t i;
    if (cx < 0 || cx >= COLS || cy < 0 || cy >= ROWS) {
        return;
    }
    i = (uint16_t)(uint8_t)cy * COLS + (uint8_t)cx;
    if (!coal[i] || coal[i] == COAL_CRACKED) {
        return;
    }
    coal[i] = COAL_CRACKED;
    cf_clr(i, CF_PILLAR);                  /* 落盤を再帰させないため、ここで降ろす */
    dig_sweep((uint8_t)cx, (uint8_t)cy);
    link_open((uint8_t)cx, (uint8_t)cy);
    draw_coal((uint8_t)cx, (uint8_t)cy);
    field_redraw_cell(cx, (int8_t)(cy - 1));
    field_redraw_cell(cx, (int8_t)(cy + 1));
    field_redraw_cell((int8_t)(cx - 1), cy);
    field_redraw_cell((int8_t)(cx + 1), cy);
    score_add(PTS_COAL_CRACK);
}

static void take_one(uint8_t cx, uint8_t cy);

/*
 * 落ちてきた岩が黒ダイヤを突き抜ける。**割れた状態を経由せず、そのまま採る。**
 * 炭層の帯を岩が縦に貫くと、その場で連鎖が伸びていく。
 * 1枚ぶんの手間 (岩の真下を割る) で帯ごと持っていける、という報酬になる。
 */
uint8_t coal_smash(int8_t cx, int8_t cy)
{
    uint16_t i;
    if (cx < 0 || cx >= COLS || cy < 0 || cy >= ROWS) {
        return 0;
    }
    i = (uint16_t)(uint8_t)cy * COLS + (uint8_t)cx;
    /* **割れているものも採る。** cell_coal() は割れた黒ダイヤを除くので、
       そちらで判定すると「割れているぶんだけ素通りする」ことになる。 */
    if (!coal[i]) {
        return 0;
    }
    if (cf_on(i, CF_PILLAR)) {
        /* **炭柱が消えた = 天盤の支えが無くなった。** つるはしで割ったときと同じく
           落盤にする。ただし**いまは岩の処理の最中**なので、その場では起こさず
           rock_update() が終わってからにする (rock.c の rock_collapse_later)。 */
        cf_clr(i, CF_PILLAR);
        rock_collapse_later();
    }
    /* **先に「割れた」扱いにしてから掘る。** dig_sweep は cell_coal() を見て
       「黒ダイヤは掘れない」と撥ねるので、**割れていない升をそのまま渡すと
       掘られないまま take_one で炭だけ消え、採ったのに土のまま残る。**
       見た目には「鉱脈があった場所が手つかずの土に見える」になり、
       実際そこを通り抜けることもできない。
       落盤で先に割れた升 (coal_crack_by_rock) だけが掘られて穴になるので、
       縦2升の鉱脈を貫くと**上だけ穴・下は土**という食い違いが出ていた。 */
    coal[i] = COAL_CRACKED;
    dig_sweep((uint8_t)cx, (uint8_t)cy);
    link_open((uint8_t)cx, (uint8_t)cy);
    take_one((uint8_t)cx, (uint8_t)cy);
    return 1;
}

void coal_crush_at(int8_t cx, int8_t cy)
{
    if (cx < 0 || cx >= COLS || cy < 0 || cy >= ROWS) {
        return;
    }
    if (coal[(uint16_t)(uint8_t)cy * COLS + (uint8_t)cx] != COAL_CRACKED) {
        return;
    }
    coal_take((uint8_t)cx, (uint8_t)cy);
}

/* 光っている升をすべて元へ戻す。 */
static void get_flash_clear(void)
{
    uint8_t k;
    for (k = 0; k < get_n; k++) {
        field_redraw_cell((int8_t)get_cs[k], (int8_t)get_rs[k]);
    }
    get_n = 0;
    get_t = 0;
    player_carve();                 /* 掘り跡を戻す */
}

/* 1升ぶんの回収。連鎖の途中でも呼ばれるので、音と倍率は呼び元が持つ。 */
/*
 * 黒ダイヤを1個採る。**連鎖の計上もここでやる。**
 * 岩が突き抜けて採ったぶんも、自分で歩いて採ったぶんも、同じ道を通るので
 * 自動的に同じ連鎖になる。倍率は採った順。時間 (CHAIN_WINDOW) で途切れる。
 */
static void take_one(uint8_t cx, uint8_t cy)
{
    uint16_t i = (uint16_t)cy * COLS + cx;
    uint16_t pts = PTS_COAL;
    uint8_t k;
    coal[i] = 0;
    coal_left--;
    /* 連鎖を1つ進める。倍率は 200 -> 400 -> 800 -> ... で CHAIN_PTS_MAX 打ち止め。 */
    if (combo < 255) {
        combo++;
    }
    combo_t = CHAIN_WINDOW;
    for (k = 1; k < combo && pts < CHAIN_PTS_MAX; k++) {
        pts = (uint16_t)(pts * 2);
    }
    if (pts > CHAIN_PTS_MAX) {
        pts = CHAIN_PTS_MAX;
    }
    score_add(pts);
    debris_spawn(cx, cy);       /* かけらが自機へ集まる = 手に入ったことの説明 */
    /* **音程の刻みは3半音。** 2半音だと4連鎖でも8半音しか上がらず、
       階段になっていることが耳で分からなかった。表の上限で頭打ちするので
       連鎖が深くても破綻しない。 */
    if (csnd_n < 8) {
        csnd_p[(uint8_t)((csnd_h + csnd_n) & 7)] = (uint8_t)((combo - 1) * 3);
        csnd_n++;
    }
    /* 連鎖が伸びるほど強く手応えを返す。**どれも VDP をほとんど使わない。** */
    {
        uint8_t n = (uint8_t)(HITSTOP_BASE + combo);
        hitstop = (uint8_t)(n > HITSTOP_MAX ? HITSTOP_MAX : n);
    }
    field_shake((uint8_t)(COAL_SHAKE_FRAMES + (combo << 1)));
    if (combo >= 3) {
        /* **3連鎖から画面全体が光る。深いほど明るく、長く。**
           一定の強さで光らせると、貫通中の1回目も5回目も同じに見えて
           階段が読めない。明るさと長さの両方を段にしてある。 */
        uint8_t lv = (uint8_t)(2 + combo);
        uint8_t fr = (uint8_t)(FLASH_CAVEIN + combo - 3);
        if (lv > 7) { lv = 7; }
        if (fr > FLASH_CHAIN_MAX) { fr = FLASH_CHAIN_MAX; }
        field_flash(fr, lv);
    }
    /* 隣の升の「土の膜」の形も変わるので、周囲ごと描き直す。 */
    field_redraw_cell((int8_t)cx, (int8_t)cy);
    field_redraw_cell((int8_t)cx, (int8_t)(cy - 1));
    field_redraw_cell((int8_t)cx, (int8_t)(cy + 1));
    field_redraw_cell((int8_t)(cx - 1), (int8_t)cy);
    field_redraw_cell((int8_t)(cx + 1), (int8_t)cy);
    /* **描き直しのあとで**光らせる。先に塗ると、この直後の描き直しに消される。 */
    /* **溜まりきったら先に片付ける。** 記録できないまま塗ると、その升だけ
       誰にも描き直されずに白いまま残る。連鎖の窓は3秒あって get_t は採るたびに
       延びるので、**1回の連鎖で CHAIN_MAX を超えることは普通に起きる**
       (岩が4升貫いた直後に自分でも採りに走る、が狙いの遊びなので)。 */
    if (get_n >= CHAIN_MAX) {
        get_flash_clear();
    }
    get_cs[get_n] = cx;
    get_rs[get_n] = cy;
    get_n++;
    get_t = COAL_GET_FRAMES;
    vdp_fill((uint16_t)((uint16_t)cx << 4), (uint16_t)((uint16_t)cy << 4), 16, 16, COL_WHITE);
    /* **点数も描き直しのあとに出す。** 先に出すと、直後の field_redraw_cell に
       塗り潰されて一度も見えない (実際そうなっていた)。 */
    score_popup(cx, cy, pts);
}

/* 入った升から、繋がっている割れた黒ダイヤをすべて同時に採る。
   得点は1個目 PTS_COAL、以降は倍々。溜めて一歩で放出する形になる。 */
void coal_take(uint8_t cx, uint8_t cy)
{
    uint8_t qc[CHAIN_MAX], qr[CHAIN_MAX];
    uint8_t head = 0, tail = 0, n;

    if (coal[(uint16_t)cy * COLS + cx] != COAL_CRACKED) {
        return;
    }
    if (get_t) {
        get_flash_clear();          /* 前の閃きを片付けてから新しい連鎖を始める */
    }
    coal[(uint16_t)cy * COLS + cx] = 0;       /* 先に消して二重に積まないようにする */
    qc[tail] = cx; qr[tail] = cy; tail++;

    while (head < tail) {
        uint8_t c = qc[head], r = qr[head], k;
        head++;
        /* 四方の割れた黒ダイヤを列に足す */
        for (k = 0; k < 4; k++) {
            int8_t nc = (int8_t)c, nr = (int8_t)r;
            uint16_t j;
            if (k == 0) { nr--; } else if (k == 1) { nr++; }
            else if (k == 2) { nc--; } else { nc++; }
            if (nc < 0 || nc >= COLS || nr < 0 || nr >= ROWS || tail >= CHAIN_MAX) {
                continue;
            }
            j = (uint16_t)(uint8_t)nr * COLS + (uint8_t)nc;
            if (coal[j] != COAL_CRACKED) {
                continue;
            }
            coal[j] = 0;
            qc[tail] = (uint8_t)nc; qr[tail] = (uint8_t)nr; tail++;
        }
    }

    /* 採った順に放出する。得点・音・連鎖の計上は take_one が持つ。 */
    for (n = 0; n < tail; n++) {
        coal[(uint16_t)qr[n] * COLS + qc[n]] = COAL_CRACKED;   /* take_one の前提へ戻す */
        take_one(qc[n], qr[n]);
    }
    if (tail >= 2) {
        shake_t = (uint8_t)(COAL_SHAKE_FRAMES + tail);
    }
}

/* その升が熱水なら描き直す。 */
static void water_redraw(int8_t cx, int8_t cy)
{
    if (cell_water(cx, cy)) {
        draw_water((uint8_t)cx, (uint8_t)cy);
    }
}

void dig_sweep(uint8_t cx, uint8_t cy)
{
    uint16_t i;
    if (cx >= COLS || cy >= ROWS) {
        return;
    }
    i = (uint16_t)cy * COLS + cx;
    if (cf_on(i, CF_DUG) || cell_coal((int8_t)cx, (int8_t)cy)) {
        return;                     /* 黒ダイヤは掘れない。つるはしで割るしかない */
    }
    cf_set(i, CF_DUG);
    dug_n++;
    dug_pct = (uint8_t)((dug_n * 100u) / NCELL);
    score_add(PTS_DIG);
    /* **隣が熱水なら描き直す。** 掘ってその面が開いたので、「ここで止まる」の印を
       引き直す必要がある (掘る前は土で、印を出す面ではなかった)。 */
    water_redraw((int8_t)cx, (int8_t)(cy - 1));
    water_redraw((int8_t)cx, (int8_t)(cy + 1));
    water_redraw((int8_t)(cx - 1), (int8_t)cy);
    water_redraw((int8_t)(cx + 1), (int8_t)cy);
}

/* 16x16 のタイルを1セルへ描く。inset=2 にすると外周2pxを残すので、
   坑道の「薄い土の膜」を消さずに火薬や風呂を重ねられる。 */
/*
 * 16x16 のタイルを1セルへ描く。inset=2 にすると外周2pxを残すので、
 * 坑道の「薄い土の膜」を消さずに火薬や風呂を重ねられる。
 *
 * **1行ぶんを vdp_wblock() で一気に送る。** 以前は1バイトずつ vdp_wdata() を
 * 呼んでいて、1回の描画で 12回のアドレス設定 + 72回の関数呼び出しが走っていた。
 * 実測で、火薬タイル1枚の描画だけで掘っている間の fps が 32 -> 44 に変わるほど
 * 重かった。OTIR で流せば1行1回の呼び出しで済む。
 */
/* **タイルを読む間だけ窓を絵のセグメントへ向け、必ず DATA_SEG へ戻す。**
   渡される tile は art_tile_of() が返す窓の中の番地で、実体は ART_TILE_SEG にある。
   **この関数は固定領域にある**ので、窓を向けても自分が消えない。
   呼び出し元が窓側 (BANKED_SRC) でも、戻るときには DATA_SEG に戻っている。 */
static void draw_tile(uint8_t cx, uint8_t cy, const uint8_t *tile, uint8_t inset)
{
    uint8_t y;
    uint8_t x0 = (uint8_t)(inset >> 1);
    uint16_t top = (uint16_t)cy << 4;
    const uint8_t *p = tile + ((uint16_t)inset << 3) + x0;
    vdp_blklen = (uint16_t)(8 - inset);
    bank_select(ART_TILE_SEG);
    for (y = inset; y < (uint8_t)(16 - inset); y++) {
        vdp_setwrite((uint16_t)(((top + y) << 7) + ((uint16_t)cx << 3) + x0));
        vdp_wblock(p);
        p += 8;
    }
    bank_select(DATA_SEG);
}

void draw_cell_dirt(int8_t cx, int8_t cy)
{
    if (cx < 0 || cx >= COLS || cy < 0 || cy >= ROWS) {
        return;
    }
    vdp_fill((uint16_t)((uint8_t)cx << 4), (uint16_t)((uint8_t)cy << 4), 16, 16, COL_DIRT);
    draw_grain((uint8_t)cx, (uint8_t)cy);
}

/* 掘り済みセルを描く。隣が土の辺だけ内側2px にして、隣接する平行トンネルの間に
   薄い土の膜を残す (本作の肝)。隣が掘り済みの辺は端まで塗って通路を繋げる。 */
void draw_dug_cell(int8_t cx, int8_t cy)
{
    uint8_t x1, y1, x2, y2;
    if (!cell_dug(cx, cy)) {
        return;
    }
    x1 = (uint8_t)((uint8_t)cx << 4);
    y1 = (uint8_t)((uint8_t)cy << 4);
    x2 = (uint8_t)(x1 + PW);
    y2 = (uint8_t)(y1 + PW);
    if (!cell_dug((int8_t)(cx - 1), cy)) { x1 = (uint8_t)(x1 + 2); }
    if (!cell_dug((int8_t)(cx + 1), cy)) { x2 = (uint8_t)(x2 - 2); }
    if (!cell_dug(cx, (int8_t)(cy - 1))) { y1 = (uint8_t)(y1 + 2); }
    if (!cell_dug(cx, (int8_t)(cy + 1))) { y2 = (uint8_t)(y2 - 2); }
    vdp_fill(x1, y1, (uint16_t)(x2 - x1 + 1), (uint16_t)(y2 - y1 + 1), COL_TUNNEL);
}

/* 火薬 (旧チェリー相当)。拾うと得点でなくダイナマイトのストックが1増える。 */
void draw_powder(uint8_t cx, uint8_t cy)
{
    draw_tile(cx, cy, art_tile_of(TILE_POWDER), 2);
}

void draw_ume(uint8_t cx, uint8_t cy)
{
    draw_tile(cx, cy, art_tile_of(TILE_BCOOL), 2);   /* 冷却なので体温回復ボーナスと同じ絵 */
}

/* 掘り抜きの形に使う数字 (5列 x 9行)。1行の下位5ビットが左から右の升目に対応する。 */
static const uint8_t digit_rows[10][9] = {
    { 0x0E,0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x0E },   /* 0 */
    { 0x04,0x0C,0x14,0x04,0x04,0x04,0x04,0x04,0x1F },   /* 1 */
    { 0x0E,0x11,0x01,0x02,0x04,0x08,0x10,0x10,0x1F },   /* 2 */
    { 0x0E,0x11,0x01,0x06,0x01,0x01,0x01,0x11,0x0E },   /* 3 */
    { 0x02,0x06,0x0A,0x12,0x1F,0x02,0x02,0x02,0x02 },   /* 4 */
    { 0x1F,0x10,0x10,0x1E,0x01,0x01,0x01,0x11,0x0E },   /* 5 */
    { 0x06,0x08,0x10,0x1E,0x11,0x11,0x11,0x11,0x0E },   /* 6 */
    { 0x1F,0x01,0x01,0x02,0x04,0x04,0x08,0x08,0x08 },   /* 7 */
    { 0x0E,0x11,0x11,0x0E,0x11,0x11,0x11,0x11,0x0E },   /* 8 */
    { 0x0E,0x11,0x11,0x11,0x0F,0x01,0x01,0x02,0x0C },   /* 9 */
};

#define DIGIT_TOP 1        /* 数字を置く先頭行 */

static void carve_digit(uint8_t d, uint8_t col0)
{
    uint8_t r, c;
    for (r = 0; r < 9; r++) {
        uint8_t bits = digit_rows[d][r];
        for (c = 0; c < 5; c++) {
            if (bits & (0x10 >> c)) {
                cf_set((uint16_t)(DIGIT_TOP + r) * COLS + (uint8_t)(col0 + c), CF_DUG);
            }
        }
    }
}

/* 面番号の形に掘り抜く。1桁なら中央に1つ、2桁なら左右に並べる。
   **組み立て (stagegen.c) から呼ぶので大域。** 字形の表 (90B) ごと
   固定領域に置いてあるのは、0x8000 の窓に入りきらなかったため。 */
void carve_stage_number(void)
{
    if (stage < 10) {
        carve_digit(stage, 5);
    } else {
        carve_digit((uint8_t)(stage / 10), 1);
        carve_digit((uint8_t)(stage % 10), 9);
    }
}

/* ガス (裏面)。**面の組み立て (stagegen.c) からも呼ぶので大域にしてある。**
   draw_tile は static のままにしておきたい (窓側から呼べるものを増やさない)。 */
void draw_gas(uint8_t cx, uint8_t cy)
{
    draw_tile(cx, cy, art_tile_of(TILE_GAS), 2);
}

/* その升へ落とし物を置けるか。 */
static uint8_t can_drop(int8_t cx, int8_t cy)
{
    uint16_t i;
    if (cx < 0 || cx >= COLS || cy < 0 || cy >= ROWS) {
        return 0;
    }
    i = (uint16_t)(uint8_t)cy * COLS + (uint8_t)cx;
    if (!cf_on(i, CF_DUG) || coal[i] || chute[i] || cf_on(i, CF_POWDER) || cf_on(i, CF_BOMB) || cf_on(i, CF_UME)) {
        return 0;                       /* 樋の升は板が占めている */
    }
    if (bath_on && (uint8_t)cx == bath_c && (uint8_t)cy == bath_r) {
        return 0;
    }
    return 1;
}

/* 倒した敵がその場へ落とし物をする。
   倒した升が塞がっていることがある (落ちてきた岩が割れた黒ダイヤの升で潰した等)。
   そこで諦めると、いちばん報いたい「岩で潰した」場合に限って報酬が消えるので、
   真下と四方まで探す。どこにも置けなければ諦める。 */
void drop_item_at(int8_t cx, int8_t cy, uint8_t is_ume)
{
    uint16_t i;
    if (!can_drop(cx, cy)) {
        static const int8_t dx[4] = { 0, -1, 1, 0 };
        static const int8_t dy[4] = { 1, 0, 0, -1 };   /* 岩は落ちるので真下から */
        uint8_t k;
        for (k = 0; k < 4; k++) {
            if (can_drop((int8_t)(cx + dx[k]), (int8_t)(cy + dy[k]))) {
                cx = (int8_t)(cx + dx[k]);
                cy = (int8_t)(cy + dy[k]);
                break;
            }
        }
        if (k == 4) {
            return;
        }
    }
    i = (uint16_t)(uint8_t)cy * COLS + (uint8_t)cx;
    if (is_ume) {
        cf_set(i, CF_UME);
        draw_ume((uint8_t)cx, (uint8_t)cy);
    } else {
        cf_set(i, CF_POWDER);
        draw_powder((uint8_t)cx, (uint8_t)cy);
    }
}

/* 投げて落ちたダイナマイト。坑道の上に重ねるので外周2pxは残す。 */
void draw_bombdrop(uint8_t cx, uint8_t cy)
{
    draw_tile(cx, cy, art_tile_of(TILE_DYNA), 2);
}

/* 黒ダイヤ (石炭)。立ちはだかっている間は 16x16 全部を使い、
   割れたら坑道の上に破片だけを重ねる (通れるようになったことが見て分かる)。
   立ちはだかっている間は**右上に残りの耐久度**を出す。あと何回叩けばよいかが
   見えないと、敵に追われながら叩き続けるのが苦しいため。 */
void draw_coal(uint8_t cx, uint8_t cy)
{
    uint8_t hp = coal[(uint16_t)cy * COLS + cx];
    char buf[4];

    if (hp == COAL_CRACKED) {
        /* **升を土から塗り直してから**坑道を描く。黒ダイヤは 16x16 全部を使う絵
           なので、内側だけ塗る draw_dug_cell では外周2pxに黒ダイヤの枠が残る。 */
        draw_cell_dirt((int8_t)cx, (int8_t)cy);
        draw_dug_cell((int8_t)cx, (int8_t)cy);
        draw_tile(cx, cy, art_tile_of(TILE_DIACRK), 2);
        return;
    }
    {
        uint8_t is_p = cf_on((uint16_t)cy * COLS + cx, CF_PILLAR);
        uint8_t x = 8;
        draw_tile(cx, cy, art_tile_of(is_p ? TILE_PILLAR : TILE_DIA), 0);
        if (hp == 0) {
            return;
        }
        /* **2桁は升の上辺いっぱいを使う。** 字は BIOS の 8x8 なので右上の
           8x8 には1文字しか入らない。終盤の炭柱は耐久が10を越える
           (通常の黒ダイヤ +PILLAR_HP_ADD) ので、以前はそこだけ数字を
           出していなかった。**落盤は終盤の主役**なのに、あと何回振れば
           天井が落ちるかが読めなかった。
           2桁のときだけ左へ8px伸ばし、升の上辺を帯にして彫り込む。 */
        if (text_u8(buf, hp) == 2) {
            x = 0;
        }
        text_draw((uint16_t)((uint16_t)cx << 4) + x, (uint16_t)((uint16_t)cy << 4) + 1,
                  buf, COL_TUNNEL, is_p ? COL_FUSE : COL_DIA, 1);
    }
}

/*
 * 樋 (とい)。坑道の上へ斜めの板を重ねて描く。
 * **樋の升は面の最初から開いている**（板は土の塊ではなく落とし口なので）。
 * 使い終わった板は「折れた板」の絵に変わる。向きは残すので、
 * **その板がどちらへ落とすはずだったか**まで跡から読める。
 */
void draw_chute(uint8_t cx, uint8_t cy)
{
    static const uint8_t tbl[4] = { TILE_TOIR, TILE_TOIL, TILE_TOIBR, TILE_TOIBL };
    uint8_t k = chute[(uint16_t)cy * COLS + cx];
    if (!k || k > CHUTE_BL) {
        return;
    }
    draw_tile(cx, cy, art_tile_of(tbl[k - 1]), 2);
}

/*
 * 熱水 (出水)。**坑道の形どおりに塗る。** 升ごとのタイルにしていたときは、升と升の
 * 間に黒い隙間が残って「ブロックが1個ずつ現れる」ように見え、つながった水に見えなかった
 * (作者の指摘、2026-09-22)。
 *
 *   - 升の内側 12x12 を塗り、**隣も水で膜が抜けていれば**その側の縁 2px まで塗り広げる
 *     (両側の升が塗り合うので、つながった水は隙間なく1つの形になる)
 *   - **上も水で膜が抜けていれば「水中」**で升いっぱいを塗る。そうでなければ
 *     **水面**で、水位 (升の上から7px) より上は坑道の黒を残し、水面に赤い線、
 *     上に湯気を2つ描く (熱い湯であることの印。水風呂と見分けるため)
 *
 * タイルを使わず塗りつぶしだけで描くので、バンクの空きを使わない。
 */
static void draw_water_seal(uint8_t cx, uint8_t cy, uint16_t y1, uint16_t y2);

void draw_water(uint8_t cx, uint8_t cy)
{
    uint16_t x = (uint16_t)((uint16_t)cx << 4), y = (uint16_t)((uint16_t)cy << 4);
    uint16_t x1 = (uint16_t)(x + 2), x2 = (uint16_t)(x + 13);
    uint16_t y1, y2 = (uint16_t)(y + 13);
    uint8_t under;

    if (spring_on && cx == spring_c && cy == spring_r) {
        draw_spring(cx, cy);          /* **湧き出し口は水に沈めない。** いつも見えている */
        return;
    }
    under = (uint8_t)(cell_linked((int8_t)cx, (int8_t)cy, DIR_UP)
                      && cell_water((int8_t)cx, (int8_t)(cy - 1)));

    if (cell_linked((int8_t)cx, (int8_t)cy, DIR_LEFT) && cell_water((int8_t)(cx - 1), (int8_t)cy)) {
        x1 = x;
    }
    if (cell_linked((int8_t)cx, (int8_t)cy, DIR_RIGHT) && cell_water((int8_t)(cx + 1), (int8_t)cy)) {
        x2 = (uint16_t)(x + 15);
    }
    if (cell_linked((int8_t)cx, (int8_t)cy, DIR_DOWN) && cell_water((int8_t)cx, (int8_t)(cy + 1))) {
        y2 = (uint16_t)(y + 15);
    }
    if (under) {
        y1 = y;                                           /* 水中: 升いっぱい */
        vdp_fill(x1, y1, (uint16_t)(x2 - x1 + 1), (uint16_t)(y2 - y1 + 1), COL_BATH);
    } else {
        y1 = (uint16_t)(y + WATER_LEVEL);                 /* 水面 */
        vdp_fill(x1, (uint16_t)(y1 + 1), (uint16_t)(x2 - x1 + 1), (uint16_t)(y2 - y1), COL_BATH);
        vdp_fill(x1, y1, (uint16_t)(x2 - x1 + 1), 1, COL_RED);          /* 赤く熱した水面 */
        vdp_fill((uint16_t)(x + 4), (uint16_t)(y + 4), 1, 1, COL_WHITE); /* 湯気 */
        vdp_fill((uint16_t)(x + 10), (uint16_t)(y + 2), 1, 1, COL_WHITE);
    }
    draw_water_seal(cx, cy, y1, y2);
    /*
     * **拾い物は水に浮く。** 火薬・落ちたダイナマイト・梅干しの升へ水が来たら、
     * 水を塗ったあとで絵を描き直して**水の上に見せる**。沈めていたときは絵が
     * 水に隠れ、**見えないのに踏めば拾える物**がそこにあった (作者の指摘、2026-09-28)。
     *
     * **「水のほうが避ける」案は測ってやめた。** 火薬は1面に10個撒いてあるので、
     * 拾い物を水を止めるものにすると、面7の熱水が13升 -> 2升しか広がらず、
     * 出水が起きていないのと同じになった (hostsim で実測)。
     */
    {
        uint8_t i = (uint8_t)((uint8_t)(cy << 4) + cx);   /* 176升なので8ビットで足りる */
        if (cf_on(i, CF_POWDER)) {
            draw_powder(cx, cy);
        } else if (cf_on(i, CF_BOMB)) {
            draw_bombdrop(cx, cy);
        } else if (cf_on(i, CF_UME)) {
            draw_ume(cx, cy);
        }
    }
}

/*
 * **止まった水の縁に「せき止め」の印を出す。**
 * 坑道が続いているのに水が来ていない面へ、岩の色の帯を引く。止まった場所が
 * 湧き出し口だけでなく**どの向きでも分かる**ようにするため (作者の指摘、2026-09-23)。
 * 上の面にも引く (水はもう上がらない、の意味)。
 *
 * **溜まり湯 (WATER_POOL) は最初から止まっている**ので、湧き出した水が止まる前でも
 * その升には印を出す。印の意味が「もう増えない」で一貫するので、面7で覚えた読み方が
 * そのまま第四坑以降で効く。
 */
static void draw_water_seal(uint8_t cx, uint8_t cy, uint16_t y1, uint16_t y2)
{
    uint16_t x = (uint16_t)((uint16_t)cx << 4);
    uint16_t h = (uint16_t)(y2 - y1 + 1);

    if (!water_done && water[(uint16_t)cy * COLS + cx] != WATER_POOL) {
        return;
    }
    if (cell_dug((int8_t)(cx - 1), (int8_t)cy) && !cell_water((int8_t)(cx - 1), (int8_t)cy)) {
        vdp_fill(x, y1, 3, h, COL_ROCK);
    }
    if (cell_dug((int8_t)(cx + 1), (int8_t)cy) && !cell_water((int8_t)(cx + 1), (int8_t)cy)) {
        vdp_fill((uint16_t)(x + 13), y1, 3, h, COL_ROCK);
    }
    if (cell_dug((int8_t)cx, (int8_t)(cy + 1)) && !cell_water((int8_t)cx, (int8_t)(cy + 1))) {
        vdp_fill(x, (uint16_t)(((uint16_t)cy << 4) + 13), 16, 3, COL_ROCK);
    }
    if (cell_dug((int8_t)cx, (int8_t)(cy - 1)) && !cell_water((int8_t)cx, (int8_t)(cy - 1))) {
        vdp_fill(x, (uint16_t)((uint16_t)cy << 4), 16, 3, COL_ROCK);
    }
}

/* 湧き出し口。**面の開始から見えている**ので、どこから水が来るかを先に読める。
   **止まったら「塞がった絵」に変わる** (= これ以上は増えない)。 */
void draw_spring(uint8_t cx, uint8_t cy)
{
    draw_tile(cx, cy, art_tile_of(water_done ? TILE_SPRINGOFF : TILE_SPRING), 2);
}

/* 鉱車のレール。**升の端まで届く絵**なので inset 0 で描き、隣の升と繋がって見える。
   両端は車止め (ここで鉱車が折り返す)。 */
void draw_rail(uint8_t cx, uint8_t cy)
{
    uint8_t t = (uint8_t)((cx == cart_c0) ? TILE_RAILL
                        : (cx == cart_c1) ? TILE_RAILR : TILE_RAIL);
    draw_tile(cx, cy, art_tile_of(t), 0);
}

/* 風呂 (温泉)。触れると体温が一気に下がる。 */
void draw_bath(uint8_t cx, uint8_t cy)
{
    draw_tile(cx, cy, art_tile_of(TILE_BATH), 2);
}

/* 冷えている最中の水風呂。湯の中を泡が1つ上がっていく。
   **体温が下がっていることを画面で見せるためのもの。** これが無いと、
   触れているのか、触れても効かないバグなのかがプレイヤーに区別できない。 */
void draw_bath_active(uint8_t phase)
{
    uint16_t x = (uint16_t)((uint16_t)bath_c << 4);
    uint16_t y = (uint16_t)((uint16_t)bath_r << 4);
    uint8_t up = (uint8_t)(phase & 7);           /* 0..7 で上がって戻る */
    draw_bath(bath_c, bath_r);                   /* 前の泡を消す */
    vdp_fill((uint16_t)(x + 5 + ((up & 1) << 2)),
             (uint16_t)(y + 12 - (up << 1)), 2, 2, COL_WHITE);
}

/* 敵の湧き出し口。数字の掘り抜きとは別に、独立した1ブロックの横穴として置く。 */
void draw_cave(uint8_t cx, uint8_t cy)
{
    draw_tile(cx, cy, art_tile_of(TILE_CAVE), 0);
}

void draw_bonus(uint8_t cx, uint8_t cy, uint8_t kind)
{
    draw_tile(cx, cy, art_tile_of((uint8_t)(TILE_BSCORE + kind)), 2);
}


/* セル1枚を「土 → 坑道 → 火薬 → 風呂」の順で描き直す。文字を消すときや
   爆風の跡を戻すときに使う。 */
void field_redraw_cell(int8_t cx, int8_t cy)
{
    if (cx < 0 || cx >= COLS || cy < 0 || cy >= ROWS) {
        return;
    }
    if (coal[(uint16_t)(uint8_t)cy * COLS + (uint8_t)cx]) {
        draw_coal((uint8_t)cx, (uint8_t)cy);
        return;
    }
    if ((uint8_t)cx == nest_c && (uint8_t)cy == nest_r) {
        draw_cave((uint8_t)cx, (uint8_t)cy);
        return;
    }
    draw_cell_dirt(cx, cy);
    draw_dug_cell(cx, cy);
    if (cart_on && (uint8_t)cy == cart_r && (uint8_t)cx >= cart_c0 && (uint8_t)cx <= cart_c1) {
        draw_rail((uint8_t)cx, (uint8_t)cy);
    }
    /* **板を描いてから、置いてある物を上に重ねる。** 落とし物は置かれないように
       してあるが (can_drop)、投げたダイナマイトだけは板の升へ落ちうる。
       ここで return すると**そのダイナマイトが見えなくなる**ので、重ねて描く。
       拾えばこの関数がもう一度走って板が戻る。 */
    if (chute[(uint16_t)(uint8_t)cy * COLS + (uint8_t)cx]) {
        draw_chute((uint8_t)cx, (uint8_t)cy);
    }
    if (cf_on((uint16_t)(uint8_t)cy * COLS + (uint8_t)cx, CF_POWDER)) {
        draw_powder((uint8_t)cx, (uint8_t)cy);
    }
    if (bath_on && (uint8_t)cx == bath_c && (uint8_t)cy == bath_r) {
        draw_bath((uint8_t)cx, (uint8_t)cy);
    }
    if (bonus_on && (uint8_t)cx == bonus_c && (uint8_t)cy == bonus_r) {
        draw_bonus((uint8_t)cx, (uint8_t)cy, bonus_kind);
    }
    if (cf_on((uint16_t)(uint8_t)cy * COLS + (uint8_t)cx, CF_BOMB)) {
        draw_bombdrop((uint8_t)cx, (uint8_t)cy);
    }
    /* **ガス (裏面)。** 生きている間だけ出す。目抜きを掘って風が通ると消える
       (`gas_live` が隣の掘り具合で決めるので、ここも自動で消える)。 */
    if (gas_n && gas_at(cx, cy)) {
        draw_gas((uint8_t)cx, (uint8_t)cy);
    }
    /* **熱水は一番上に重ねる。** 湧き出し口も水に沈んだら水で描く。 */
    if (water[(uint16_t)(uint8_t)cy * COLS + (uint8_t)cx]) {
        draw_water((uint8_t)cx, (uint8_t)cy);   /* 湧き出し口の升なら湧き出し口を描く */
    } else if (spring_on && (uint8_t)cx == spring_c && (uint8_t)cy == spring_r) {
        draw_spring((uint8_t)cx, (uint8_t)cy);
    }
}

