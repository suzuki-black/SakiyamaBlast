/* SakiyamaBlast - 面の組み立て (掘り抜き・鉱脈・岩・樋・水の配置)
 *
 * **ここは 0x8000 の窓 (2枚目) に置く。** 面を作るのは1面に1回だけなので、
 * 遊んでいる間ずっと見えている必要がない。窓を切り替えて使う:
 *
 *     win2_select(WIN2_GEN_SEG);   build_stage();   win2_select(WIN2_PLAY_SEG);
 *
 * ↑ この切り替えは `main.c` (固定領域) がやる。**ここから呼べるのは固定領域と
 * 1枚目の窓 (0xA000) と RAM だけ**で、`player.c` などの 0x8000 側は見えない。
 *
 * **field.c から切り出した** (2026-10-30)。境目は「組み立てからだけ辿れる関数」で、
 * static な変数は一つも跨がない。描画 (`draw_*`) と升の問い合わせ (`cell_*`) は
 * 遊んでいる間も使うので field.c (固定領域) に残してある。
 */
#include "game.h"
#include "vdp.h"
#include "art.h"
#include "text.h"
#include "sound.h"

/* 掘り済みセルのうち、(tc,tr) に一番近いものを返す。すでに使った升目は避ける。
   used は「その升目を使ってよいか」を返す関数の代わりに、単純な除外表で持つ。 */
static uint8_t used[NCELL];

/* pick_cell() の判定。自機の出発点と巣のすぐ隣は空けておく (開始直後に詰まないように)。 */
static uint8_t near_key_cell(uint16_t i)
{
    int8_t dc, dr;
    uint8_t c = (uint8_t)(i % COLS), r = (uint8_t)(i / COLS);
    dc = (int8_t)(c - start_c); if (dc < 0) { dc = (int8_t)-dc; }
    dr = (int8_t)(r - start_r); if (dr < 0) { dr = (int8_t)-dr; }
    if (dc + dr < 2) {
        return 1;
    }
    dc = (int8_t)(c - nest_c); if (dc < 0) { dc = (int8_t)-dc; }
    dr = (int8_t)(r - nest_r); if (dr < 0) { dr = (int8_t)-dr; }
    return (uint8_t)(dc + dr < 2);
}

static uint8_t cell_ok(uint16_t i, uint8_t kind)
{
    uint8_t c = (uint8_t)(i % COLS), r = (uint8_t)(i / COLS);
    if (kind == 0) {
        return (uint8_t)(cf_on(i, CF_DUG) && !used[i]);
    }
    if (cf_on(i, CF_DUG) || coal[i] || chute[i] || rock_at((int8_t)c, (int8_t)r) >= 0) {
        return 0;
    }
    if (used[i] == ROCK_OFF_RESV || used[i] == VEIN_GAP_RESV) {
        return 0;                  /* 岩を押して運ぶ通り道と、掘らせる隙間。塞がない */
    }
    if (near_key_cell(i)) {
        return 0;
    }
    if (kind == 2) {
        if (r + 1 >= ROWS) {
            return 0;
        }
        if (cf_on(i + COLS, CF_DUG)) {
            return 0;              /* 真下が坑道では支えが無い */
        }
        /* **真下が黒ダイヤでもよい。** 黒ダイヤは未掘の升に置かれるので、そのまま
           支えになる (支え判定は「真下が掘れていなければ支え」)。
           割れば岩が落ちるので、**必ずやる行動がそのまま岩の引き金**になる。 */
        if (bath_on && (uint8_t)(r + 1) == bath_r && c == bath_c) {
            return 0;              /* 水風呂の真上は避ける。壊されると理不尽 */
        }
    }
    return 1;
}

static uint16_t nearest_dug(uint8_t tc, uint8_t tr)
{
    uint8_t c, r;
    uint16_t best = 0xFFFF;
    uint8_t bestd = 255;
    for (r = 0; r < ROWS; r++) {
        for (c = 0; c < COLS; c++) {
            uint16_t i = (uint16_t)r * COLS + c;
            uint8_t d;
            int8_t dc, dr;
            if (!cf_on(i, CF_DUG) || used[i]) {
                continue;
            }
            dc = (int8_t)(c - tc); if (dc < 0) { dc = (int8_t)-dc; }
            dr = (int8_t)(r - tr); if (dr < 0) { dr = (int8_t)-dr; }
            d = (uint8_t)(dc + dr);
            if (d < bestd) {
                bestd = d;
                best = i;
            }
        }
    }
    return best;
}


/* (tc,tr) に近い掘り抜きのうち、(ac,ar) から mind 以上離れているものを返す。
   条件を満たすものが無ければ単に一番近いものを返す。 */
static uint16_t nearest_dug_away(uint8_t tc, uint8_t tr, uint8_t ac, uint8_t ar, uint8_t mind)
{
    uint8_t c, r;
    uint16_t best = 0xFFFF;
    uint8_t bestd = 255;
    for (r = 0; r < ROWS; r++) {
        for (c = 0; c < COLS; c++) {
            uint16_t i = (uint16_t)r * COLS + c;
            uint8_t d;
            int8_t dc, dr;
            if (!cf_on(i, CF_DUG) || used[i]) {
                continue;
            }
            dc = (int8_t)(c - ac); if (dc < 0) { dc = (int8_t)-dc; }
            dr = (int8_t)(r - ar); if (dr < 0) { dr = (int8_t)-dr; }
            if ((uint8_t)(dc + dr) < mind) {
                continue;
            }
            dc = (int8_t)(c - tc); if (dc < 0) { dc = (int8_t)-dc; }
            dr = (int8_t)(r - tr); if (dr < 0) { dr = (int8_t)-dr; }
            d = (uint8_t)(dc + dr);
            if (d < bestd) {
                bestd = d;
                best = i;
            }
        }
    }
    return (best == 0xFFFF) ? nearest_dug(tc, tr) : best;
}

/* 掘り済みセルの重心。風呂を「掘ってある箇所の比較的真ん中」へ置くのに使う。 */
static void dug_centre(uint8_t *oc, uint8_t *orr)
{
    uint16_t sc = 0, sr = 0, n = 0, i;
    for (i = 0; i < NCELL; i++) {
        if (cf_on(i, CF_DUG)) {
            sc = (uint16_t)(sc + (i % COLS));
            sr = (uint16_t)(sr + (i / COLS));
            n++;
        }
    }
    *oc = (uint8_t)(n ? (sc / n) : 8);
    *orr = (uint8_t)(n ? (sr / n) : 5);
}

/* 条件に合う升目をあらかじめ一覧にしておき、そこから乱数で1つ取り出す。
   「置くたびに全升目を走査する」方式だと、ダイヤが40個になる高次面で面の構築に
   2秒以上かかってしまう (実測)。一覧を作るのは1回だけにする。
   kind: 0 = 掘り抜きの上で未使用 / 1 = 土の中 / 2 = 土の中で真下も土 */
static uint8_t cand[NCELL];
static uint8_t cand_n;

static void build_candidates(uint8_t kind)
{
    uint16_t i;
    cand_n = 0;
    for (i = 0; i < NCELL; i++) {
        if (cell_ok(i, kind)) {
            cand[cand_n++] = (uint8_t)i;
        }
    }
}

/* 一覧から1つ取り出す。取り出した枠には末尾を詰めるので O(1)。 */
static uint16_t take_candidate(void)
{
    uint8_t k, v;
    if (cand_n == 0) {
        return 0xFFFF;
    }
    k = (uint8_t)(rnd8() % cand_n);
    v = cand[k];
    cand_n--;
    cand[k] = cand[cand_n];
    return v;
}

/*
 * **左右を交互に置く。** side = 0 なら左半分 (列 0-7)、1 なら右半分 (列 8-15) の候補から取る。
 * その側に候補が無ければ、どこからでも取る。
 *
 * 何も指定しないと、炭層と岩が片側に固まる盤面が多かった (出荷していた15面で、
 * 少ない側の黒ダイヤが平均24%。面1・3・4・10 は左半分に1つも無い)。
 * 固まっているほど少ない手で連鎖が伸びるので、種選びがそれをさらに選んでいた。
 * 作者の指摘 (2026-09-19)「右側だけ、左側だけのような密集系が多い」。
 */
static uint16_t take_candidate_side(uint8_t side)
{
    uint8_t k, m = 0, pick, v;
    for (k = 0; k < cand_n; k++) {
        if ((uint8_t)((cand[k] % COLS) >= COLS / 2) == side) {
            m++;
        }
    }
    if (m == 0) {
        return take_candidate();
    }
    pick = (uint8_t)(rnd8() % m);
    for (k = 0; k < cand_n; k++) {
        if ((uint8_t)((cand[k] % COLS) >= COLS / 2) != side) {
            continue;
        }
        if (pick-- == 0) {
            break;
        }
    }
    v = cand[k];
    cand_n--;
    cand[k] = cand[cand_n];
    return v;
}

/* 火薬を掘り抜きの上へ n 個ばら撒く。 */
static void place_powder(uint8_t n)
{
    uint16_t i;
    build_candidates(0);
    while (n) {
        i = take_candidate();
        if (i == 0xFFFF) {
            return;                                  /* 置ける升目が尽きた */
        }
        used[i] = 1;
        cf_set(i, CF_POWDER);
        n--;
    }
}

/*
 * 黒ダイヤ (石炭) の面ごとの配置。**ここがステージ難度の主な調整箇所。**
 * 個数 x 耐久度 が、そのままつるはしを振る回数になる。1回 PICK_FRAMES (0.3秒)。
 *   面1  =  4個 x 2 =  8回 ( 2.4秒)
 *   面8  =  6個 x 5 = 30回 ( 9.0秒)
 *   面15 = 10個 x 8 = 80回 (24.0秒)
 * 炭柱2本 (PILLAR_HP_ADD を上乗せ) を足したものが面の総耐久で、
 * **振るたびに発熱する**ので、そのまま体温の階段になる (面1=72, 面15=408)。
 *
 * **幕の頭 (面5・7・10・13) で耐久を一段寝かせてある (2026-10-12)。**
 * 新しい仕掛けを覚える面に、振りの負担まで乗せない。峰 (面6・9・12・15) の
 * 値は変えていないので、到達点は同じまま。
 */
static const uint8_t coal_n_tbl[MAX_STAGE] = {
    4, 4, 5, 5, 5, 6, 6, 6, 7, 7, 8, 8, 9, 9, 10
};
static const uint8_t coal_hp_tbl[MAX_STAGE] = {
    2, 2, 3, 3, 3, 4, 4, 5, 5, 5, 6, 6, 6, 7, 8
};
/* TODO: 絵が異なり耐久度がさらに高い「特殊な黒ダイヤ」。coal[] の値域はまだ余っている
   (1..COAL_HP_MAX と COAL_CRACKED しか使っていない) ので、種別は別配列か上位ビットで持つ。 */

/*
 * **裏面 (2周目) は黒ダイヤが固く、数が多く、岩も増える。**
 * 表の表をそのまま使い、裏のときだけ下駄を履かせる (表をもう1組持つ余裕が無い)。
 * 耐久は `COAL_HP_MAX` で頭打ち。数は置ける升の都合で `place_coal` 側が吸収する。
 */
static uint8_t coal_hp_at(void)
{
    uint8_t hp = (uint8_t)(coal_hp_tbl[stage - 1] + (loop_n ? 2 : 0));
    return (uint8_t)((hp > COAL_HP_MAX) ? COAL_HP_MAX : hp);
}

static uint8_t coal_n_at(void)
{
    return (uint8_t)(coal_n_tbl[stage - 1] + (loop_n ? 1 : 0));
}


/* (c,r) の隣で黒ダイヤを置ける升を1つ返す。無ければ 0xFFFF。 */
static uint16_t vein_next(uint16_t from)
{
    static const int8_t dc[4] = { 1, -1, 0, 0 };
    static const int8_t dr[4] = { 0, 0, 1, -1 };
    uint8_t k, o = (uint8_t)(rnd8() & 3);
    int8_t c = (int8_t)(from % COLS), r = (int8_t)(from / COLS);
    for (k = 0; k < 4; k++) {
        uint8_t j = (uint8_t)((o + k) & 3);
        int8_t nc = (int8_t)(c + dc[j]), nr = (int8_t)(r + dr[j]);
        if (nc < 0 || nc >= COLS || nr < 0 || nr >= ROWS) {
            continue;
        }
        {
            uint16_t i = (uint16_t)(uint8_t)nr * COLS + (uint8_t)nc;
            if (cell_ok(i, 1)) {
                return i;
            }
        }
    }
    return 0xFFFF;
}

/*
 * **掘らないと繋がらない鉱脈 (2026-10-17)。**
 *
 * 縦の鉱脈に**土を1升ずつ挟んで**置く。落ちる岩が通れるのは「掘った升」と
 * 「割れた黒ダイヤ」だけなので (rock.c の supported)、隙間を先に掘っておかないと
 * 岩はそこで止まり、**一段ぶんしか採れない**。
 *
 *       .O.   岩          岩を支えているのは**一番上の黒ダイヤ**なので、
 *       .4.   黒ダイヤ    そこを割った瞬間に岩が落ちる。だから段取りは
 *       ...   ← 掘る      「下の段から割る → 隙間を掘る → 最後に一番上を割る」。
 *       .4.   黒ダイヤ    順番を間違えると一段で終わる。
 *       ...   ← 掘る
 *       .4.   黒ダイヤ
 *
 * **普通にクリアするだけなら難しくない** (1枚ずつ割って歩いて採ればよい)。
 * 連鎖させたいなら段取りが要る、という形にするための仕掛け (作者の案、2026-10-17)。
 *
 * 面ごとの段数。**第一坑の面1は教材なので仕込まない。** 第四坑からは2本置き、
 * 一度の落盤では片付かない盤面にする。
 */
static const uint8_t vgap_tbl[MAX_STAGE] = {   /* 1本目の隙間の数 */
    0, 1, 1, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3
};
static const uint8_t vgap2_tbl[MAX_STAGE] = {  /* 2本目 (第四坑から) */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 3, 3, 3
};

uint8_t gapv_c[2], gapv_r[2], gapv_n;

/*
 * (c,r) を一番上として、隙間 g 個の鉱脈が置けるか。place なら置く。
 * **真上は岩の升**として予約する (そこが連鎖の引き金になる)。
 */
static uint8_t gap_vein_at(uint8_t c, uint8_t r, uint8_t g, uint8_t hp, uint8_t place)
{
    uint8_t k;
    uint16_t i;
    if (r < 1 || (uint8_t)(r + 2 * g) >= ROWS) {
        return 0;
    }
    if (!cell_ok((uint16_t)(r - 1) * COLS + c, 2)) {
        return 0;                   /* 真上に岩を置けない (坑道・端・風呂の上など) */
    }
    for (k = 0; k <= (uint8_t)(2 * g); k++) {
        if (!cell_ok((uint16_t)(r + k) * COLS + c, 1)) {
            return 0;               /* 段も隙間も、土のままの升でなければならない */
        }
    }
    if (!place) {
        return 1;
    }
    for (k = 0; k <= (uint8_t)(2 * g); k++) {
        i = (uint16_t)(r + k) * COLS + c;
        if (k & 1) {
            used[i] = VEIN_GAP_RESV;        /* 隙間。板も炭柱も置かせない */
            /* **横から入れる道を1本空けておく。** 隙間は上下が黒ダイヤなので、
               左右も黒ダイヤで塞がると**掘りに行けない**（上を割ると岩が落ちて
               その升に居座るので、上から降りることもできない）。隣がすでに
               坑道ならそれが道になる。そうでなければ片側を予約して土のまま残す。 */
            if (!cell_dug((int8_t)(c - 1), (int8_t)(r + k))
                && !cell_dug((int8_t)(c + 1), (int8_t)(r + k))) {
                uint8_t nc = (uint8_t)((c < COLS / 2) ? (c + 1) : (c - 1));
                if (nc < COLS) {
                    used[(uint16_t)(r + k) * COLS + nc] = VEIN_GAP_RESV;
                }
            }
        } else {
            coal[i] = hp;
            coal_left++;
        }
    }
    used[(uint16_t)(r - 1) * COLS + c] = VEIN_GAP_RESV;   /* 岩を載せる升 */
    gapv_c[gapv_n] = c;
    gapv_r[gapv_n] = r;
    gapv_n++;
    return 1;
}

/* 仕込む鉱脈を左右へ1本ずつ。端から内側へ探すので、2本は自然に離れる。 */
static void place_gap_veins(void)
{
    uint8_t hp = coal_hp_at();
    uint8_t side = (uint8_t)(rnd8() & 1);
    uint8_t g[2], k;

    gapv_n = 0;
    g[0] = vgap_tbl[stage - 1];
    g[1] = vgap2_tbl[stage - 1];
    for (k = 0; k < 2; k++) {
        uint8_t o = (uint8_t)(rnd8() % 3);      /* 端にべったり張り付かせない */
        uint8_t r0 = (uint8_t)(1 + (rnd8() & 1));
        uint8_t t, placed = 0, want = g[k];
        if (!want) {
            continue;
        }
        /* **入らなければ浅いほうへ落とす。** 掘り抜きが広い面では、1列に
           土が8行続く場所が無いことがある (面12 で実際に起きた)。段数を
           減らしてでも必ず1本は仕込む。狙いの段数に乗っているかは
           tools/hostsim/pick_seeds.py が種を選ぶときに見る。 */
        while (want && !placed) {
            for (t = o; t < COLS && !placed; t++) {
                uint8_t c = (uint8_t)(side ? (COLS - 1 - t) : t), r;
                for (r = r0; (uint8_t)(r + 2 * want) < ROWS; r++) {
                    if (gap_vein_at(c, r, want, hp, 1)) {
                        placed = 1;
                        break;
                    }
                }
            }
            want--;
        }
        side ^= 1;
    }
}

/*
 * 黒ダイヤを**繋がった炭層**として置く。1個ずつばらまくと、1個掘るたびに次を探して
 * 長く掘る羽目になり、そこが遊びの大半を占めてしまう。鉱脈にすると、掘り当ててから
 * 一息に採れるので、掘る時間が減って手応えが出る。
 */
static void place_coal(void)
{
    uint8_t want = coal_n_at();
    uint8_t hp   = coal_hp_at();
    uint16_t i;
    uint8_t len;

    uint8_t side = (uint8_t)(rnd8() & 1);

    coal_left = 0;
    /* **下ごしらえの要る鉱脈を先に置く。** 残りをいつもの鉱脈で埋める。 */
    place_gap_veins();
    while (coal_left < want) {
        build_candidates(1);            /* 置くたびに作り直す (coal[] が増えるため) */
        i = take_candidate_side(side);  /* 炭層ごとに左右を入れ替える */
        side ^= 1;
        if (i == 0xFFFF) {
            return;                     /* 置ける升が尽きた */
        }
        coal[i] = hp;
        coal_left++;
        len = (uint8_t)(1 + (rnd8() % COAL_VEIN_MAX));   /* この鉱脈の長さ */
        while (--len && coal_left < want) {
            uint16_t n = vein_next(i);
            if (n == 0xFFFF) {
                break;                  /* これ以上伸ばせない */
            }
            coal[n] = hp;
            coal_left++;
            i = n;
        }
    }
}

/* 岩を土の中へ。真下も土であること (置いた瞬間に落ち始めないように)。 */
/* 炭柱。耐久の高い黒ダイヤを、岩とは無関係な場所へ置く。
   割ると天盤を支える柱が消えたことになり、盤面の**すべての岩**が落ちる (rock_collapse)。
   岩の真上・真下に置く必要はない。むしろ離れた場所にあるほうが、
   「黄枠 = 天井全部が落ちる」という合図として正確に伝わる。 */
static void place_pillars(void)
{
    uint8_t made, hp = coal_hp_at();
    uint8_t side = (uint8_t)(rnd8() & 1);
    uint16_t i;
    hp = (uint8_t)(hp + PILLAR_HP_ADD);
    if (hp > COAL_HP_MAX) {
        hp = COAL_HP_MAX;
    }
    for (made = 0; made < PILLAR_MAX; made++) {
        build_candidates(1);
        i = take_candidate_side(side);  /* 2本を左右へ分ける */
        side ^= 1;
        if (i == 0xFFFF) {
            return;
        }
        coal[i] = hp;
        cf_set(i, CF_PILLAR);
        coal_left++;
    }
}

/* 候補のうち「真下が黒ダイヤ」のものだけ残す。
   岩が土の中に散らばっていると、掘る動線は黒ダイヤへ向かって伸びるので
   **岩と掘る場所が原理的に交わらない**。炭層の上へ寄せると、割る行為が
   そのまま岩の引き金になり、しかも割る前から落ちる先が見えている。 */
static void keep_above_coal(void)
{
    uint8_t k = 0, m = 0;
    while (k < cand_n) {
        uint16_t i = cand[k];
        if (i + COLS < NCELL && coal[i + COLS]) {
            cand[m++] = cand[k];
        }
        k++;
    }
    cand_n = m;
}

/*
 * 列ごとの「一番上に居る岩の行」。0xFF = その列に岩が無い。
 *
 * **候補を絞るたびに rock_at() で上の行を全部探すと、面の組み立てが約3秒伸びる**
 * (実測。R キーの面リセットを計測していて見つけた)。候補は最大176升あり、
 * その1つごとに 行数 x 岩8個 を走査していた。岩は8個しかないので、
 * 先に列ごとの表を作れば1回の照合で済む。
 */
static uint8_t rock_top[COLS];

static void build_rock_top(void)
{
    uint8_t c, i;
    for (c = 0; c < COLS; c++) {
        rock_top[c] = 0xFF;
    }
    for (i = 0; i < ROCK_MAX; i++) {
        uint8_t rc, rr;
        if (rock_state[i] == ROCK_NONE || rock_state[i] == ROCK_SPLIT) {
            continue;
        }
        rc = (uint8_t)(((uint16_t)rock_x[i] + 8) >> 4);
        rr = (uint8_t)(rock_y[i] >> 4);
        if (rock_top[rc] == 0xFF || rr < rock_top[rc]) {
            rock_top[rc] = rr;
        }
    }
}

/* (c,r) の上の同じ列に岩が居るか。樋は「岩が通る列」に置かないと意味が無い。 */
static uint8_t rock_above(uint8_t c, uint8_t r)
{
    return (uint8_t)(rock_top[c] != 0xFF && rock_top[c] < r);
}

/* (c,r) に置く板が d の向きへ曲げられるか。need_coal なら、曲がった先の列の
   下方に黒ダイヤがあることまで求める (通せば必ず何かを採れる置き方)。 */
static uint8_t chute_dir_ok(uint8_t c, uint8_t r, int8_t d, uint8_t need_coal)
{
    int8_t nc = (int8_t)((int8_t)c + d);
    uint8_t rr;
    if (nc < 0 || nc >= COLS) {
        return 0;
    }
    {
        uint16_t j = (uint16_t)r * COLS + (uint8_t)nc;
        if (coal[j] || chute[j]) {
            return 0;               /* 曲がった先が黒ダイヤ / 板が隣り合うと読めない */
        }
        if (used[j] == ROCK_OFF_RESV) {
            return 0;               /* 押す通り道の下を開けると、押している途中で落ちる */
        }
        if (used[j] == VEIN_GAP_RESV) {
            /* **掘らせるための隙間を開けてしまわない。** 板は落とし先の升を
               開けるので、そこが仕込んだ隙間だと下ごしらえがタダになる
               (面15で実際に1段ぶんタダになっていた。2026-10-17 の実測)。 */
            return 0;
        }
    }
    /* **落とす先の真上に岩を残さない。** 落とす先を開けるので、真上の岩が
       面の開始と同時に落ちてしまう。 */
    if (r > 0 && rock_at(nc, (int8_t)(r - 1)) >= 0) {
        return 0;
    }
    if (!need_coal) {
        return 1;
    }
    for (rr = (uint8_t)(r + 1); rr < ROWS; rr++) {
        if (coal[(uint16_t)rr * COLS + (uint8_t)nc]) {
            return 1;
        }
    }
    return 0;
}

/*
 * 樋を置く。**低次面2枚、高次面4枚。**
 *
 * 3段構えで探す。
 *   1. 上の列に岩が居て、曲がった先の下に黒ダイヤがある升 (通せば必ず採れる)
 *   2. 上の列に岩が居る升
 *   3. どこでも
 * 1 が本命で、これが取れると「岩の下を掘る → 曲がる → 炭層を貫く」が
 * 掘る前から目で追える形になる。
 */
static void place_chutes(void)
{
    uint8_t want;

    if (stage < CHUTE_START_ST) {
        return;                 /* 第一坑には樋を置かない */
    }
    want = (uint8_t)((stage >= CHUTE_HIGH_ST) ? CHUTE_HIGH : CHUTE_LOW);
    uint8_t made = 0, need;

    build_rock_top();               /* 岩は置き終わっているので1回で足りる */
    /* **候補の一覧は段ごとに1回だけ作る。** 板1枚ごとに作り直すと
       176升 x cell_ok を最大12回繰り返すことになり、面の組み立てが目に見えて遅れる。
       選んだ升は一覧から抜けるので、作り直さなくても二重には置かない。 */
    for (need = 0; need < 3 && made < want; need++) {
        build_candidates(1);
        while (made < want && cand_n) {
            uint16_t i = take_candidate();
            uint8_t c = (uint8_t)(i % COLS), r = (uint8_t)(i / COLS);
            int8_t d0 = (int8_t)((rnd8() & 1) ? 1 : -1);
            uint8_t dir;
            if (need < 2 && !rock_above(c, r)) {
                continue;           /* 岩の通らない列に置いても意味が無い */
            }
            if (r > 0 && rock_at((int8_t)c, (int8_t)(r - 1)) >= 0) {
                continue;           /* 真上の岩が開始と同時に落ちてしまう */
            }
            if (chute_dir_ok(c, r, d0, (uint8_t)(need == 0))) {
                dir = (uint8_t)((d0 > 0) ? CHUTE_R : CHUTE_L);
            } else if (chute_dir_ok(c, r, (int8_t)-d0, (uint8_t)(need == 0))) {
                dir = (uint8_t)((d0 > 0) ? CHUTE_L : CHUTE_R);
            } else {
                continue;
            }
            /* **板の升と、板が落とす先の升を開ける。**
               樋は土の塊ではなく落とし口なので、最初から通れている。
               こうしないと「黒ダイヤを4枚貫いた岩が土に止められる」ことになり、
               しかも曲がった先も土だと板が一度も働かない (作者の指摘、2026-09-12)。
               開けておけば**落とす前に岩の通り道を最後まで目で追える**。 */
            {
                uint8_t nc = (uint8_t)((int8_t)c + ((dir == CHUTE_R) ? 1 : -1));
                cf_set(i, CF_DUG);
                cf_set((uint16_t)r * COLS + nc, CF_DUG);
                link_open(c, r);
                link_open(nc, r);
            }
            chute[i] = dir;
            used[i] = 1;
            made++;
        }
    }
}

/* 升と升の間の距離 (縦横の歩数)。 */
static uint8_t mdist(uint8_t c, uint8_t r, uint8_t c2, uint8_t r2)
{
    return (uint8_t)((uint8_t)((c > c2) ? (c - c2) : (c2 - c))
                     + (uint8_t)((r > r2) ? (r - r2) : (r2 - r)));
}

/* 指定した行の候補を落とす。
   スプライトは16px高なので、別の升行に置けば走査線が全く重ならない。
   MSX2 は横一列に9枚目以降が消えるので、**1行に岩は1個まで**にしておけば、
   岩が走査線を食う量を確定的に抑えられる。 */
static void drop_row(uint8_t row)
{
    uint8_t k = 0, m = 0;
    while (k < cand_n) {
        if ((uint8_t)(cand[k] / COLS) != row) {
            cand[m++] = cand[k];
        }
        k++;
    }
    cand_n = m;
}

/*
 * **ずらして置く岩。** (c,r) の岩を d の向きへ k 升押したとき、
 * 炭層の真上で止まるか。途中で落ちたり引っかかったりしないこと。
 *
 *   押し手の升   (c-d, r) … 盤面の中で、黒ダイヤ・板でない
 *   通り道       (c+d*j, r) j=1..k … 黒ダイヤ・板・予約済みでない
 *   通り道の下   (c+d*j, r+1) j<k … 土 (坑道なら途中で落ちる。黒ダイヤならそこで揃ってしまう)
 *   行き先の下   (c+d*k, r+1) … 黒ダイヤ
 *
 * **岩が居るかは見ない。** 候補は岩を置くたびにその行ごと落としてあるので、
 * この行に岩は居ない (1行に岩1個)。rock_at() を升ごとに呼ぶと岩8個の走査が
 * 候補 x 向き x 距離 だけ積み重なり、面13の暗転が約1秒伸びた (openMSX で実測)。
 */
static uint8_t off_path_ok(uint8_t c, uint8_t r, int8_t d, uint8_t k)
{
    int8_t x = (int8_t)((int8_t)c - d);
    uint16_t i;
    uint8_t j;
    if (x < 0 || x >= COLS) {
        return 0;
    }
    i = (uint16_t)r * COLS + (uint8_t)x;
    if (coal[i] || chute[i]) {
        return 0;
    }
    for (j = 1; j <= k; j++) {
        x = (int8_t)((int8_t)c + d * (int8_t)j);
        if (x < 0 || x >= COLS) {
            return 0;
        }
        i = (uint16_t)r * COLS + (uint8_t)x;
        if (coal[i] || chute[i] || used[i] == ROCK_OFF_RESV) {
            return 0;
        }
        if (((uint8_t)x == bath_c && r == bath_r) || ((uint8_t)x == nest_c && r == nest_r)) {
            return 0;               /* 水風呂を塞ぐ / 湧き出し口に岩が居座る */
        }
        i += COLS;
        if (j < k) {
            if (cf_on(i, CF_DUG) || coal[i]) {
                return 0;
            }
        } else if (!coal[i]) {
            return 0;
        }
    }
    return 1;
}

/* 押して揃えられる向きと距離を探す。見つかれば d と k を返す (k=0 なら無し)。
   **遠いほうから試す**ので、2列ずらせる面では2列ずれた岩が多くなる。 */
static uint8_t off_find(uint8_t c, uint8_t r, uint8_t far, int8_t *dp)
{
    int8_t d0 = (int8_t)((rnd8() & 1) ? 1 : -1);
    uint8_t k;
    for (k = far; k; k--) {
        if (off_path_ok(c, r, d0, k)) { *dp = d0; return k; }
        if (off_path_ok(c, r, (int8_t)-d0, k)) { *dp = (int8_t)-d0; return k; }
    }
    return 0;
}

/* 候補のうち「真下が黒ダイヤでなく、押せば黒ダイヤの真上へ行ける」ものだけ残す。 */
static void keep_offset(uint8_t far)
{
    uint8_t k = 0, m = 0;
    while (k < cand_n) {
        uint16_t i = cand[k];
        uint8_t c = (uint8_t)(i % COLS), r = (uint8_t)(i / COLS);
        if (!coal[i + COLS] && (off_path_ok(c, r, 1, 1) || off_path_ok(c, r, -1, 1)
                                || (far > 1 && (off_path_ok(c, r, 1, far)
                                                || off_path_ok(c, r, -1, far))))) {
            cand[m++] = cand[k];
        }
        k++;
    }
    cand_n = m;
}

/* 置いた岩の行を候補から全部落とす (1行に岩1個まで)。 */
static void drop_rock_rows(void)
{
    uint8_t i;
    for (i = 0; i < ROCK_MAX; i++) {
        if (rock_state[i] != ROCK_NONE) {
            drop_row((uint8_t)(rock_y[i] >> 4));
        }
    }
}

/* 押し手の升と通り道を予約する。後から置く炭柱・樋がここを塞がないように。 */
static void off_reserve(uint8_t c, uint8_t r, int8_t d, uint8_t k)
{
    uint8_t j;
    uint16_t i;
    used[(uint16_t)r * COLS + (uint8_t)((int8_t)c - d)] = ROCK_OFF_RESV;
    for (j = 1; j <= k; j++) {
        i = (uint16_t)r * COLS + (uint8_t)((int8_t)c + d * (int8_t)j);
        used[i] = ROCK_OFF_RESV;
        if (j < k) {
            used[i + COLS] = ROCK_OFF_RESV;
        }
    }
}

/* 面ごとの「ずらして置く岩」の数。岩の総数は 4,4,4,5,5,5,...,8。 */
static const uint8_t rock_off_tbl[MAX_STAGE] = {
    0, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4
};

/* 裏面は岩を1つ増やす (表の下駄。`coal_hp_at` の説明を参照)。 */
static uint8_t rock_off_at(void)
{
    return (uint8_t)(rock_off_tbl[stage - 1] + (loop_n ? 1 : 0));
}

static uint8_t cand_keep[NCELL];
static uint8_t keep_n;

static void place_rocks(void)
{
    uint8_t n = (uint8_t)(4 + (stage - 1) / 3);      /* 4〜8個 */
    uint8_t off = rock_off_at();
    uint8_t far = (uint8_t)((stage >= ROCK_OFF_FAR_ST) ? 2 : 1);
    uint8_t side = (uint8_t)(rnd8() & 1);   /* 岩も左右を交互に置く */
    uint16_t i;
    if (n > ROCK_MAX) {
        n = ROCK_MAX;
    }
    if (off > n) {
        off = n;
    }
    /*
     * **置き方は3段構え。**
     *   1. 押せば黒ダイヤの真上へ行ける升 (ずらした岩)。off 個
     *   2. 黒ダイヤの真上 (割ればそのまま落ちて縦に貫く)。残り
     *   3. どこでも (1 や 2 の候補が尽きたとき)
     *
     * **ずらす岩を先に置く。** 置ける形 (隣の列の真下が黒ダイヤ、途中は土) が限られるので、
     * 真上の岩に炭層の上の升と行を先に取られると、ほとんど置けなくなる (実測で候補0が95%)。
     *
     * 候補は段ごとに一度だけ作り、置くたびにその行を落としていく。
     * 毎回作り直すと 176升 x cell_ok を岩の数だけ繰り返すことになり、
     * 面の組み立てが目に見えて遅くなる (画面が暗いままの時間が伸びる)。
     *
     * 横方向は樋が引き受けるので、真上の岩に横の条件は要らない
     * (樋は岩を置いたあとで、岩の下の列へ置く)。
     */
    build_candidates(2);
    /* **仕込んだ鉱脈の真上には必ず岩を載せる。** ここが下ごしらえの引き金になる
       (割った瞬間に落ちる)。候補任せにすると載らない盤面が出て、隙間がただの
       穴になってしまう。升は place_gap_veins() が予約してあるので空いている。 */
    {
        uint8_t k;
        for (k = 0; k < gapv_n && n; k++) {
            uint8_t rr = (uint8_t)(gapv_r[k] - 1);
            int8_t ri;
            if (rock_at((int8_t)gapv_c[k], (int8_t)rr) >= 0) {
                continue;
            }
            rock_place(gapv_c[k], rr);
            /* **支柱で止めておく。** この岩は仕込んだ鉱脈の引き金なので、
               一番上を割った拍子に落ちてしまうと段取りが崩れる。支柱があれば
               順番を気にせず準備でき、**折る1手が引き金**になる (rock.c)。 */
            ri = rock_at((int8_t)gapv_c[k], (int8_t)rr);
            if (ri >= 0) {
                rock_prop[(uint8_t)ri] = 1;
            }
            drop_row(rr);
            n--;
        }
        if (off > n) {
            off = n;
        }
    }
    if (off) {
        /* **一覧は1回だけ作って控えておく。** 真上の岩の段で作り直すと
           176升 x cell_ok をもう一度回すことになり、面13の暗転が約0.4秒伸びた
           (openMSX で実測)。 */
        uint8_t k;
        for (k = 0; k < cand_n; k++) {
            cand_keep[k] = cand[k];
        }
        keep_n = cand_n;
        keep_offset(far);
        while (off && cand_n) {
            uint8_t c, r, k;
            int8_t d = 1;
            i = take_candidate_side(side);
            side ^= 1;
            c = (uint8_t)(i % COLS);
            r = (uint8_t)(i / COLS);
            /* 前の岩の予約で塞がれていることがあるので、取り出した時点で確かめ直す */
            k = off_find(c, r, far, &d);
            if (!k) {
                continue;
            }
            off_reserve(c, r, d, k);
            rock_place(c, r);
            drop_row(r);
            n--;
            off--;
        }
    }
    if (rock_off_at()) {                /* ずらす岩が無い面では一覧は作ったまま */
        /* 控えた一覧を戻し、置いた岩の行と予約した升を落とす。
           予約は押し手・通り道 (岩と同じ行なので行ごと落ちる) と、通り道の真下。 */
        uint8_t k, m = 0;
        for (k = 0; k < keep_n; k++) {
            if (used[cand_keep[k]] != ROCK_OFF_RESV) {
                cand[m++] = cand_keep[k];
            }
        }
        cand_n = m;
        drop_rock_rows();
    }
    keep_above_coal();
    while (n) {
        if (cand_n == 0) {
            /* 真上が尽きたらどこでも。置いた岩の行は落とす (1行に岩1個)。 */
            build_candidates(2);
            drop_rock_rows();
            if (cand_n == 0) {
                return;
            }
        }
        i = take_candidate_side(side);
        side ^= 1;
        rock_place((uint8_t)(i % COLS), (uint8_t)(i / COLS));
        drop_row((uint8_t)(i / COLS));
        n--;
    }
}

/* 湧き出し口を1ブロックだけ土に開ける。自機の出発点から離れた場所を選ぶ。 */
/*
 * 敵の湧き出し口。数字の掘り抜きとは別の1ブロックの横穴。
 *
 * **既掘の坑道に face する升を選ぶ**のが肝。ここを孤立した穴にすると、湧いた敵が
 * 四方を土に囲まれて boxed_in になり、その場で透明化して土を斜めに突っ切ってしまう。
 * すると穴から出てくるように見えず、盤面のどこからともなく現れる形になる。
 * 坑道に面していれば、敵は実体のまま横穴から歩いて出てくる。
 */
static uint8_t cave_ok(uint16_t i, uint8_t need_open)
{
    uint8_t c = (uint8_t)(i % COLS), r = (uint8_t)(i / COLS);
    int8_t dc, dr;
    if (cf_on(i, CF_DUG)) {
        return 0;
    }
    dc = (int8_t)(c - start_c); if (dc < 0) { dc = (int8_t)-dc; }
    dr = (int8_t)(r - start_r); if (dr < 0) { dr = (int8_t)-dr; }
    if (dc + dr < 6) {
        return 0;                        /* 出発点の近くには置かない */
    }
    if (!need_open) {
        return 1;
    }
    return (uint8_t)(cell_dug((int8_t)c, (int8_t)(r - 1))
                     || cell_dug((int8_t)c, (int8_t)(r + 1))
                     || cell_dug((int8_t)(c - 1), (int8_t)r)
                     || cell_dug((int8_t)(c + 1), (int8_t)r));
}

static void place_cave(void)
{
    uint16_t i, n, hit;
    uint8_t need_open;

    /* まず坑道に面した升から探し、見つからなければ条件を落とす */
    for (need_open = 1; ; need_open = 0) {
        n = 0;
        for (i = 0; i < NCELL; i++) {
            if (cave_ok(i, need_open)) {
                n++;
            }
        }
        if (n) {
            break;
        }
        if (!need_open) {
            nest_c = start_c;            /* 保険 */
            nest_r = start_r;
            return;
        }
    }

    hit = (uint16_t)(((uint16_t)rnd8() << 8 | rnd8()) % n);
    for (i = 0; i < NCELL; i++) {
        if (!cave_ok(i, need_open)) {
            continue;
        }
        if (hit == 0) {
            nest_c = (uint8_t)(i % COLS);
            nest_r = (uint8_t)(i / COLS);
            cf_set(i, CF_DUG);                  /* 1ブロックだけ開ける */
            used[i] = 1;
            return;
        }
        hit--;
    }
}

/*
 * 出発点・水風呂・湧き出し口が、掘り抜きだけで行き来できるようにする。
 *
 * 面番号の掘り抜きは数字の形なので、そのままでは**ばらばらの島に分かれる**
 * (面2は7つ、面12は9つの島になる)。水風呂や洞窟がその島の一つに乗ると、
 * 自分で掘らないと辿り着けない。そこで、届いていなければ最寄りの到達済みの升まで
 * L字に掘って繋ぐ。最短である必要はないので、横→縦の単純な形でよい。
 */
static uint8_t reach[NCELL];

/* 1 = **熱水の通れる升だけ**を辿る (湧き出し口を選ぶときだけ立てる)。
   黒ダイヤ・岩・水風呂・敵の巣で止まるところまで含めて数えたいので、
   水の判定そのもの (water.c の flow_ok) を使う。
   **BSS は零で始まらない**ので、面を組む頭で必ず落とす (set_stage_layout)。
   立てっぱなしだと、通路を繋ぐときの塗り潰しまで水の規則で走って盤面が変わる。 */
static uint8_t flood_wet;

/* 掘れていてまだ印を付けていなければ、印を付けて積む。
   **熱水の升は通れない扱い** (敵が入れないので、溜まり湯を置く前の見積もりに使う)。
   面を組む途中では水はまだ1升も無いので、他の呼び出しの結果は変わらない。 */
static uint8_t push_if(uint8_t sp, uint8_t *stk, uint8_t k)
{
    if (flood_wet && !flow_ok((int8_t)(k % COLS), (int8_t)(k / COLS))) {
        return sp;
    }
    if (cf_on(k, CF_DUG) && !reach[k] && !water[k]) {
        reach[k] = 1;
        stk[sp++] = k;
    }
    return sp;
}

static void flood_from(uint8_t c, uint8_t r)
{
    static uint8_t stk[NCELL];
    uint8_t sp = 0, k, kc, kr;
    uint16_t i = (uint16_t)r * COLS + c;

    for (k = 0; k < NCELL; k++) {
        reach[k] = 0;
    }
    if (!cf_on(i, CF_DUG)) {
        return;
    }
    reach[i] = 1;
    stk[sp++] = (uint8_t)i;
    while (sp) {
        k = stk[--sp];
        kc = (uint8_t)(k % COLS);
        kr = (uint8_t)(k / COLS);
        if (kc > 0)        { sp = push_if(sp, stk, (uint8_t)(k - 1)); }
        if (kc + 1 < COLS) { sp = push_if(sp, stk, (uint8_t)(k + 1)); }
        if (kr > 0)        { sp = push_if(sp, stk, (uint8_t)(k - COLS)); }
        if (kr + 1 < ROWS) { sp = push_if(sp, stk, (uint8_t)(k + COLS)); }
    }
}

/* (c,r) から最寄りの到達済みの升まで L字に掘る。掘った升は坑道になる。 */
static void connect_to_reach(uint8_t c, uint8_t r)
{
    uint16_t i, best = 0xFFFF;
    uint8_t bd = 255, bc = 0, br = 0, cc, rr, d;

    if (reach[(uint16_t)r * COLS + c]) {
        return;                         /* すでに繋がっている */
    }
    for (i = 0; i < NCELL; i++) {
        if (!reach[i]) {
            continue;
        }
        cc = (uint8_t)(i % COLS);
        rr = (uint8_t)(i / COLS);
        d = (uint8_t)((cc > c ? cc - c : c - cc) + (rr > r ? rr - r : r - rr));
        if (d < bd) {
            bd = d; best = i; bc = cc; br = rr;
        }
    }
    if (best == 0xFFFF) {
        return;
    }
    /* まず横へ、次に縦へ */
    cc = c;
    while (cc != bc) {
        cc = (uint8_t)((cc < bc) ? cc + 1 : cc - 1);
        cf_set((uint16_t)r * COLS + cc, CF_DUG);
    }
    rr = r;
    while (rr != br) {
        rr = (uint8_t)((rr < br) ? rr + 1 : rr - 1);
        cf_set((uint16_t)rr * COLS + bc, CF_DUG);
    }
    cf_set((uint16_t)r * COLS + c, CF_DUG);
}

/*
 * 熱水の湧き出し口。**掘り抜きの上の升を1つ選ぶ**ので、面の開始から見えている。
 * 自機の出発点の近くだと落盤の直後に逃げ場が無くなるので、少し離す。
 */
static void place_spring(void)
{
    uint8_t try_n, best_n = 0;
    uint16_t i, k, best = 0xFFFF;

    /* **表は第三坑 (面7〜9) だけ。裏は全面で湧く。**
       常磐の炭層は海側へ傾斜していて、深いほど湧水が増えた (docs/HISTORY.md §5.3)。
       裏面＝さらに深部へ降りる、という筋にそのまま乗せてある。 */
    if (!loop_n && (stage < WATER_ST || stage > WATER_ST_END)) {
        return;
    }
    build_candidates(0);                /* 掘り抜きの上 (dug かつ未使用) */
    for (try_n = 0; try_n < 12; try_n++) {
        uint8_t c, r, n = 0;
        i = take_candidate();
        if (i == 0xFFFF) {
            break;
        }
        c = (uint8_t)(i % COLS);
        r = (uint8_t)(i / COLS);
        if (mdist(c, r, start_c, start_r) < 4) {
            continue;                   /* 出発点の近くは避ける */
        }
        if (!flow_ok((int8_t)c, (int8_t)r)) {
            continue;                   /* **自分の升から水が出られること。** 岩が
                                           載っていると一滴も出ないまま終わる */
        }
        /* **水の行き場がある場所を選ぶ。** 行き止まりに置くと数升で止まり、
           「水が湧いた」ことが遊びに関与しない (面9 が3升で止まっていた)。
           **実際に熱水が通れる升だけ**を数える (黒ダイヤ・岩・水風呂・敵の巣で
           止まるぶんも勘定に入る)。**盤面そのものは動かない**ので面の並びは変わらない。 */
        flood_wet = 1;
        flood_from(c, r);
        flood_wet = 0;
        for (k = 0; k < NCELL; k++) {
            if (reach[k]) {
                n++;
            }
        }
        if (n > best_n) {               /* **一番ましな候補を憶えておく。** */
            best_n = n;
            best = i;
        }
        if (n >= WATER_ROOM) {
            break;                      /* 十分な行き場がある。これでよい */
        }
    }
    if (best == 0xFFFF) {
        return;                         /* 置ける升が無い */
    }
    used[best] = 1;
    spring_c = (uint8_t)(best % COLS);
    spring_r = (uint8_t)(best / COLS);
    spring_on = 1;
}

/*
 * **隣の黒ダイヤを「湯に浸からないと叩けない」状態にしないこと。**
 * 四方が水と黒ダイヤだけになった黒ダイヤは、浸かり続けないと採れない。
 * 「採掘は妨げない」(src/water.c の設計の約束) を溜まり湯にも課す。
 */
static uint8_t coal_dry_ok(uint8_t c, uint8_t r)
{
    static const int8_t dc[4] = { -1, 1, 0, 0 }, dr[4] = { 0, 0, -1, 1 };
    uint8_t k, j;

    for (k = 0; k < 4; k++) {
        int8_t nc = (int8_t)((int8_t)c + dc[k]), nr = (int8_t)((int8_t)r + dr[k]);
        uint16_t ni;
        uint8_t ok = 0;
        if (nc < 0 || nc >= COLS || nr < 0 || nr >= ROWS) {
            continue;
        }
        ni = (uint16_t)(uint8_t)nr * COLS + (uint8_t)nc;
        if (!coal[ni]) {
            continue;
        }
        for (j = 0; j < 4; j++) {
            int8_t mc = (int8_t)(nc + dc[j]), mr = (int8_t)(nr + dr[j]);
            uint16_t mi;
            if (mc < 0 || mc >= COLS || mr < 0 || mr >= ROWS) {
                continue;
            }
            mi = (uint16_t)(uint8_t)mr * COLS + (uint8_t)mc;
            if (!water[mi] && !coal[mi]) {
                ok = 1;                 /* 土でもよい。掘ればそこに立てる */
                break;
            }
        }
        if (!ok) {
            return 0;
        }
    }
    return 1;
}

/*
 * **鉱車のレール。** 水平に CART_LEN 升ぶん、まとめて坑道にする
 * (樋の升と同じで、**面の開始から開いている**。鉱車が走れないと意味が無い)。
 *
 * 置き方の約束:
 *   - **盤面の上下の端は避ける。** 上に岩を載せられて、下へ落とせる行であること
 *   - 出発点・水風呂・敵の巣とは重ねない (used[] が立っている升は使わない)
 *   - **置いた升は used[] にする。** 黒ダイヤも岩も炭柱も湧き出し口も溜まり湯も
 *     レールの上には来ない (鉱車の通り道を塞がせない)
 *
 * **炭と岩より先に置く。** 後から置くと、せっかく組んだ炭層や岩の上を
 * レールが踏み潰してしまう。
 */
static void place_rail(void)
{
    uint8_t try_n, c, r, k;
    uint16_t i;

    if (stage < CART_ST) {
        return;
    }
    for (try_n = 0; try_n < 24; try_n++) {
        r = (uint8_t)(2 + rnd8() % (ROWS - 4));          /* 上下の端は避ける */
        c = (uint8_t)(rnd8() % (COLS - CART_LEN + 1));
        for (k = 0; k < CART_LEN; k++) {
            i = (uint16_t)r * COLS + (uint16_t)(c + k);
            if (used[i]) {
                break;                                   /* 出発点・風呂・巣など */
            }
        }
        if (k < CART_LEN) {
            continue;
        }
        for (k = 0; k < CART_LEN; k++) {
            i = (uint16_t)r * COLS + (uint16_t)(c + k);
            cf_set(i, CF_DUG);
            used[i] = 1;
            link_open((uint8_t)(c + k), r);
        }
        cart_place(c, (uint8_t)(c + CART_LEN - 1), r);
        return;
    }
}

/*
 * **溜まり湯。** 面の開始から坑道に溜まっている熱水で、**最初から止まっている。**
 * 湧き出し口の水 (面7) と違って広がらないが、縁にはいつも「せき止め」の印が出ている
 * ので、**面7で覚えた「この印より先へは来ない」がそのまま読める。**
 * 浸かれば体温が上がるだけで、失敗条件は相変わらずオーバーヒートのまま
 * ——**新しい死に方を増やさずに後半面を締める置き物**（作者の案、2026-09-26）。
 *
 * 置き方の約束:
 *   - **出発点から離す。** 開始と同時に体温が上がるのは理不尽
 *   - **水風呂と敵の巣の隣には置かない。** 水風呂へ行くのに湯を渡らせない。
 *     巣の口を塞がない
 *   - **敵が巣から出発点まで来られなくなる升には置かない。** 敵は熱水に入れないので、
 *     坑道の一本道に置くと敵が出て来られず、面が骨抜きになる (置いてから
 *     巣を起点に塗り潰して確かめ、駄目なら取り消す)
 */
static void place_pool(void)
{
    uint8_t want, try_n;

    if (stage < WPOOL_ST) {
        return;
    }
    want = (uint8_t)(1 + (uint8_t)(stage - WPOOL_ST) / 2);   /* 10,11:1 12,13:2 14,15:3 */
    if (want > WPOOL_MAX) {
        want = WPOOL_MAX;
    }
    build_candidates(0);                /* 掘り抜きの上 (dug かつ未使用) */
    for (try_n = 0; try_n < 24 && want; try_n++) {
        uint16_t i = take_candidate();
        uint8_t c, r;
        if (i == 0xFFFF) {
            return;
        }
        c = (uint8_t)(i % COLS);
        r = (uint8_t)(i / COLS);
        if (mdist(c, r, start_c, start_r) < 4) {
            continue;
        }
        if (mdist(c, r, bath_c, bath_r) < 3 || mdist(c, r, nest_c, nest_r) < 3) {
            continue;
        }
        if (chute[i]) {
            continue;                   /* 樋の落とし口は塞がない */
        }
        water[i] = WATER_POOL;
        if (!coal_dry_ok(c, r)) {
            water[i] = 0;               /* 隣の黒ダイヤが採れなくなる */
            continue;
        }
        flood_from(nest_c, nest_r);     /* 熱水を壁として塗り潰す */
        if (!reach[(uint16_t)start_r * COLS + start_c]) {
            water[i] = 0;               /* 敵が出発点まで来られなくなる */
            continue;
        }
        used[i] = 1;
        want--;
    }
}

/*
 * **置く工程ごとに種を振り直す。**
 *
 * 乱数は1本の流れなので、途中の工程が食う回数が変わると**それより後の工程が
 * 全部ずれる**。実際に樋の置き方を書き直したとき、樋しか触っていないのに
 * 面11の盤面が必須条件を割った (2026-09-11)。
 * 工程ごとに独立した種から始めれば、**ある工程のコードを触っても他の工程の配置は
 * 動かない。** 樋の置き方を変えても黒ダイヤと岩はそのまま残る。
 *
 * 種は面ごとの種から決めるので、「盤面は面番号だけで決まる」ことは変わらない。
 * 0x9E37 を掛けているのは工程ごとに離れた種にするため (xorshift は近い種でも
 * 数回で散るが、念のため)。rnd_seed() は 0 を弾くので零状態にはならない。
 */
#define PH_CAVE   1
#define PH_POWDER 2
#define PH_COAL   3
#define PH_ROCK   4
#define PH_PILLAR 5
#define PH_CHUTE  6
#define PH_WATER  7
#define PH_POOL   8
#define PH_RAIL   9

static void phase_seed(uint8_t k)
{
    rnd_seed((uint16_t)(layout_seed() + (uint16_t)k * 0x9E37u));
}

static void set_stage_layout(void)
{
    uint16_t i;
    uint8_t cc, cr;

    for (i = 0; i < NCELL; i++) {
        used[i] = 0;
    }
    water_reset();
    cart_reset();
    flood_wet = 0;          /* **BSS は零で始まらない。** 通路を繋ぐ塗り潰しは水の規則で走らせない */

    set_dirt_palette();

    carve_stage_number();

    /* 自機の出発点: 掘り抜きの一番上に近いところ (坑口から降りてくる感じ) */
    i = nearest_dug(8, 0);
    start_c = (uint8_t)(i % COLS);
    start_r = (uint8_t)(i / COLS);
    used[i] = 1;

    /* 敵の湧き出し口: 数字の掘り抜きとは別に、土の中へ独立した1ブロックの穴を開ける。
       掘り抜きの上に置くと、掘り進めた先から敵が飛び出してきて理不尽な死に方になる。
       自機の出発点からは十分に離す。 */
    phase_seed(PH_CAVE);
    place_cave();

    /* 風呂: 掘ってある箇所の重心に近く、かつ自機の出発点の隣にはならないところ */
    dug_centre(&cc, &cr);
    i = nearest_dug_away(cc, cr, start_c, start_r, 3);
    bath_c = (uint8_t)(i % COLS);
    bath_r = (uint8_t)(i / COLS);
    bath_on = 1;
    used[i] = 1;

    /* 出発点から水風呂と湧き出し口まで、掘らずに行けるようにする。
       数字の掘り抜きは島に分かれるので、届いていなければ通路を掘って繋ぐ。 */
    flood_from(start_c, start_r);
    connect_to_reach(bath_c, bath_r);
    flood_from(start_c, start_r);
    connect_to_reach(nest_c, nest_r);

    /* **鉱車のレールは炭と岩より先。** 後から置くと炭層や岩を踏み潰す。 */
    phase_seed(PH_RAIL);
    place_rail();

    phase_seed(PH_POWDER);
    place_powder(POWDER_PER_STAGE);
    /* **黒ダイヤを先に置く。** 岩を炭層の真上へ寄せるので、置く対象が先に要る。 */
    phase_seed(PH_COAL);
    place_coal();
    phase_seed(PH_ROCK);
    place_rocks();
    phase_seed(PH_PILLAR);
    place_pillars();
    /* **樋は最後。** 岩の列と炭層の位置が決まってからでないと、
       「岩が通って、曲がった先で炭を貫く」置き方を選べない。 */
    phase_seed(PH_CHUTE);
    place_chutes();
    /* **湧き出し口は最後。** ここで種を分けてあるので、出水の無い面では
       乱数を1回も使わず、盤面はこの機構を入れる前とまったく同じになる。 */
    phase_seed(PH_WATER);
    place_spring();
    /* **溜まり湯も最後。** ここも種を分けてあるので、置かない面 (面9まで) の盤面は
       この機構を入れる前とまったく同じままになる。 */
    phase_seed(PH_POOL);
    place_pool();
    place_gas();   /* 裏面のガス (src/water.c) */
}

/* **面ごとの盤面の種。** ここを変えるとその面の並びが丸ごと変わる。
   0 のものはまだ選んでいないので `0x9E37 * 面番号 + 0x1234` を使う。
   選び方は tools/hostsim/pick_seeds.py（盤面を静的に測って総当たりする）。 */
/* **全面に「樋を通した1回の落下で4個以上採れる」ことを課してある** (pick_seeds の
   REQUIRE)。板が飾りにならないよう「実際に曲がる岩が1つ以上」も必須。
   測り方は tools/hostsim/sim.c の analyse: 炭以外の升を全部掘ってから、岩を1つだけ
   残して落とし、採れた数を数える (reach)。**掘る手間を無視した上限**なので、
   「掘りさえすれば大連鎖の余地がある盤面か」を測っていることになる。

   落盤で採れる数 (taken) は 面1〜3 で4個、面15 で0個。難易度の軸はこちら。 */
const uint16_t stage_seed[15] = {
    17612, 24442,  4491,  6972, 35089,
    29123, 29237, 52263,  1123, 24137,
    12328,  6459, 45399, 27511, 28965,
};
uint16_t layout_seed_force;      /* 0 以外なら強制的にこの種を使う (道具用) */

uint16_t layout_seed(void)
{
    uint16_t s;
    if (layout_seed_force) {
        return layout_seed_force;
    }
    s = (stage >= 1 && stage <= 15) ? stage_seed[stage - 1] : 0;
    if (!s) {
        s = (uint16_t)(0x9E37u * (uint16_t)stage + 0x1234u);
    }
    /* **裏面は同じ面番号でも配置を変える。** 同じ15面をなぞるだけだと
       「覚えた盤面を速くやるだけ」になり、難度ではなく作業になる。
       種をずらすので、下ごしらえの鉱脈も岩も別の位置に出る。 */
    return (uint16_t)(s + (uint16_t)loop_n * 0x5BF3u);
}

void build_stage(void)
{
    uint16_t i;
    uint8_t c, r;

    for (i = 0; i < NCELL; i++) {
        cf_clr(i, CF_DUG);
        cf_clr(i, CF_POWDER);
        cf_clr(i, CF_BOMB);
        cf_clr(i, CF_UME);
        cf_clr(i, CF_PILLAR);
        chute[i] = 0;
        coal[i] = 0;
        cf_clr(i, LINK_RIGHT | LINK_DOWN);
    }
    rock_clear();

    /* **面の配置は面番号だけで決まる。毎回同じ盤面になる。**
       以前は起動時のタイマーを種にしていたので、同じ面でも遊ぶたびに黒ダイヤも岩も
       炭柱も並びが変わり、**覚えて組み立てるという遊びが成立しなかった**
       (作者の指摘「毎回変わったら戦略性もくそもない」)。
       配置を組むあいだだけ種を差し替え、終わったら元へ戻す。**戻すのが要点で**、
       戻さないと敵の揺らぎやボーナスの抽選まで固定され、
       ツール側 (tools/hostsim の --seed) も効かなくなる。 */
    {
        uint16_t keep = rnd_get();
        rnd_seed(layout_seed());
        set_stage_layout();
        rnd_seed(keep);
    }

    dug_n = 0;
    for (i = 0; i < NCELL; i++) {
        if (cf_on(i, CF_DUG)) {
            dug_n++;
        }
    }
    dug_pct = (uint8_t)((dug_n * 100u) / NCELL);
    dug_n0 = dug_n;              /* 切羽圧はここからの増分で測る */

    /* 面の最初から掘れている升どうしは、描画上も膜が無いので繋がっている扱いにする。
       (膜が残るのは「片方が後から掘られた」ときだけ) */
    for (r = 0; r < ROWS; r++) {
        for (c = 0; c < COLS; c++) {
            uint16_t k = (uint16_t)r * COLS + c;
            if (!cf_on(k, CF_DUG)) {
                continue;
            }
            if (c + 1 < COLS && cf_on(k + 1, CF_DUG))    { cf_set(k, LINK_RIGHT); }
            if (r + 1 < ROWS && cf_on(k + COLS, CF_DUG)) { cf_set(k, LINK_DOWN); }
        }
    }

    /* 全面を土で塗ってから既掘セルを彫り、最後に火薬を置く。
       **地色は全面1回で塗り、粒は土が残る升だけに置く** (掘り済みの升は坑道で
       塗り潰すので、そこへ粒を置くのは無駄)。 */
    vdp_fill(0, 0, 256, FIELD_H, COL_DIRT);
    for (r = 0; r < ROWS; r++) {
        for (c = 0; c < COLS; c++) {
            if (!cf_on((uint16_t)r * COLS + c, CF_DUG)) {
                draw_grain(c, r);
            }
            if (coal[(uint16_t)r * COLS + c]) {
                draw_coal(c, r);
                continue;
            }
            if (c == nest_c && r == nest_r) {
                draw_cave(c, r);
                continue;
            }
            draw_dug_cell((int8_t)c, (int8_t)r);
            if (cart_on && r == cart_r && c >= cart_c0 && c <= cart_c1) {
                draw_rail(c, r);
            }
            if (chute[(uint16_t)r * COLS + c]) {
                draw_chute(c, r);
                continue;
            }
            if (cf_on((uint16_t)r * COLS + c, CF_POWDER)) {
                draw_powder(c, r);
            }
            if (cf_on((uint16_t)r * COLS + c, CF_BOMB)) {
                draw_bombdrop(c, r);
            }
        }
    }
    if (bath_on) {
        draw_bath(bath_c, bath_r);
    }
    if (spring_on) {
        draw_spring(spring_c, spring_r);   /* 熱水の湧き出し口。開始から見えている */
    }
    /* **ガス (裏面) も開始から見せる。** 見えない即死は理不尽なので、
       溜まっている升は最初から分かるようにする (消し方は目抜きを掘ること)。 */
    for (i = 0; i < gas_n; i++) {
        if (gas_live((uint8_t)i)) {
            draw_gas(gas_c[i], gas_r[i]);
        }
    }
    /* **溜まり湯。** 坑道を描いた後から上に塗る (draw_water が縁の印まで描く)。 */
    for (i = 0; i < NCELL; i++) {
        if (water[i]) {
            draw_water((uint8_t)(i % COLS), (uint8_t)(i / COLS));
        }
    }
}
