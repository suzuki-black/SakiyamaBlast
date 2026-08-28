/* SakiyamaBlast - 盤面 (掘り地図・火薬・ステージ構築と描画) */
#include "game.h"
#include "vdp.h"
#include "art.h"
#include "text.h"

uint8_t  dug[NCELL];
uint8_t  powder[NCELL];
uint8_t  bombdrop[NCELL];
uint8_t  coal[NCELL];
uint8_t  coal_left;
uint8_t  cell_link[NCELL];
uint16_t dug_n;
uint8_t  dug_pct;
uint8_t  stage;
uint8_t  start_c, start_r;
uint8_t  nest_c, nest_r;
uint8_t  bath_c, bath_r;
uint8_t  bath_on;

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
    return (uint8_t)(cell_link[(uint16_t)(uint8_t)cy * COLS + (uint8_t)cx] & mask);
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
            cell_link[k] |= LINK_RIGHT;
        }
        return;
    }
    if (m == 0) {
        if (n >= 3 && n <= 13 && r + 1 < ROWS) {
            cell_link[k] |= LINK_DOWN;
        }
    }
}

uint8_t cell_dug(int8_t cx, int8_t cy)
{
    if (cx < 0 || cx >= COLS || cy < 0 || cy >= ROWS) {
        return 0;
    }
    return dug[(uint8_t)cy * COLS + (uint8_t)cx];
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
        coal[i] = COAL_CRACKED;
        dig_sweep((uint8_t)cx, (uint8_t)cy);   /* 砕けたので通れる升になる */
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
void coal_take(uint8_t cx, uint8_t cy)
{
    uint16_t i = (uint16_t)cy * COLS + cx;
    if (coal[i] != COAL_CRACKED) {
        return;
    }
    coal[i] = 0;
    coal_left--;
    /* 隣の升の「土の膜」の形も変わるので、周囲ごと描き直す。 */
    field_redraw_cell((int8_t)cx, (int8_t)cy);
    field_redraw_cell((int8_t)cx, (int8_t)(cy - 1));
    field_redraw_cell((int8_t)cx, (int8_t)(cy + 1));
    field_redraw_cell((int8_t)(cx - 1), (int8_t)cy);
    field_redraw_cell((int8_t)(cx + 1), (int8_t)cy);
    score_add(PTS_COAL);
}

void dig_sweep(uint8_t cx, uint8_t cy)
{
    uint16_t i;
    if (cx >= COLS || cy >= ROWS) {
        return;
    }
    i = (uint16_t)cy * COLS + cx;
    if (dug[i] || cell_coal((int8_t)cx, (int8_t)cy)) {
        return;                     /* 黒ダイヤは掘れない。つるはしで割るしかない */
    }
    dug[i] = 1;
    dug_n++;
    dug_pct = (uint8_t)((dug_n * 100u) / NCELL);
    score_add(PTS_DIG);
}

/* 16x16 のタイルを1セルへ描く。inset=2 にすると外周2pxを残すので、
   坑道の「薄い土の膜」を消さずに火薬や風呂を重ねられる。 */
static void draw_tile(uint8_t cx, uint8_t cy, const uint8_t *tile, uint8_t inset)
{
    uint8_t y, i;
    uint8_t x0 = (uint8_t)(inset >> 1);
    uint8_t n  = (uint8_t)(8 - inset);
    uint16_t top = (uint16_t)cy << 4;
    for (y = inset; y < (uint8_t)(16 - inset); y++) {
        vdp_setwrite((uint16_t)(((top + y) << 7) + ((uint16_t)cx << 3) + x0));
        for (i = 0; i < n; i++) {
            vdp_wdata(tile[((uint16_t)y << 3) + x0 + i]);
        }
    }
}

void draw_cell_dirt(int8_t cx, int8_t cy)
{
    if (cx < 0 || cx >= COLS || cy < 0 || cy >= ROWS) {
        return;
    }
    vdp_fill((uint16_t)((uint8_t)cx << 4), (uint16_t)((uint8_t)cy << 4), 16, 16, COL_DIRT);
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
    draw_tile(cx, cy, art_tile_of(TILE_DIA), 0);
    if (hp == 0 || hp > 9) {
        return;                      /* 2桁は右上に収まらない */
    }
    text_u8(buf, hp);
    buf[1] = 0;
    text_draw((uint16_t)((uint16_t)cx << 4) + 8, (uint16_t)((uint16_t)cy << 4) + 1,
              buf, COL_TUNNEL, COL_DIA, 1);
}

/* 風呂 (温泉)。触れると体温が一気に下がる。 */
void draw_bath(uint8_t cx, uint8_t cy)
{
    draw_tile(cx, cy, art_tile_of(TILE_BATH), 2);
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

void bath_destroy_at(int8_t cx, int8_t cy)
{
    if (!bath_on) {
        return;
    }
    if ((int8_t)bath_c != cx || (int8_t)bath_r != cy) {
        return;
    }
    bath_on = 0;
    draw_cell_dirt(cx, cy);
    draw_dug_cell(cx, cy);
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
    if (powder[(uint16_t)(uint8_t)cy * COLS + (uint8_t)cx]) {
        draw_powder((uint8_t)cx, (uint8_t)cy);
    }
    if (bath_on && (uint8_t)cx == bath_c && (uint8_t)cy == bath_r) {
        draw_bath((uint8_t)cx, (uint8_t)cy);
    }
    if (bonus_on && (uint8_t)cx == bonus_c && (uint8_t)cy == bonus_r) {
        draw_bonus((uint8_t)cx, (uint8_t)cy, bonus_kind);
    }
    if (bombdrop[(uint16_t)(uint8_t)cy * COLS + (uint8_t)cx]) {
        draw_bombdrop((uint8_t)cx, (uint8_t)cy);
    }
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
                dug[(uint16_t)(DIGIT_TOP + r) * COLS + (uint8_t)(col0 + c)] = 1;
            }
        }
    }
}

/* 面番号の形に掘り抜く。1桁なら中央に1つ、2桁なら左右に並べる。 */
static void carve_stage_number(void)
{
    if (stage < 10) {
        carve_digit(stage, 5);
    } else {
        carve_digit((uint8_t)(stage / 10), 1);
        carve_digit((uint8_t)(stage % 10), 9);
    }
}

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
        return (uint8_t)(dug[i] && !used[i]);
    }
    if (dug[i] || coal[i] || rock_at((int8_t)c, (int8_t)r) >= 0) {
        return 0;
    }
    if (near_key_cell(i)) {
        return 0;
    }
    if (kind == 2) {
        if (r + 1 >= ROWS) {
            return 0;
        }
        if (dug[i + COLS] || coal[i + COLS]) {
            return 0;                                /* 岩の真下は土でなければならない */
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
            if (!dug[i] || used[i]) {
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

/* 掘り済みセルのうち (tc,tr) から一番遠いものを返す。自機の出発点を敵の巣から
   できるだけ離すのに使う (開始と同時に捕まる「出落ち」を避けるため)。 */
static uint16_t farthest_dug(uint8_t tc, uint8_t tr)
{
    uint8_t c, r;
    uint16_t best = 0xFFFF;
    uint8_t bestd = 0;
    for (r = 0; r < ROWS; r++) {
        for (c = 0; c < COLS; c++) {
            uint16_t i = (uint16_t)r * COLS + c;
            uint8_t d;
            int8_t dc, dr;
            if (!dug[i] || used[i]) {
                continue;
            }
            dc = (int8_t)(c - tc); if (dc < 0) { dc = (int8_t)-dc; }
            dr = (int8_t)(r - tr); if (dr < 0) { dr = (int8_t)-dr; }
            d = (uint8_t)(dc + dr);
            if (best == 0xFFFF || d > bestd) {
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
            if (!dug[i] || used[i]) {
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
        if (dug[i]) {
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
        powder[i] = 1;
        n--;
    }
}

/*
 * 黒ダイヤ (石炭) の面ごとの配置。**ここがステージ難度の主な調整箇所。**
 * 個数 x 耐久度 が、そのままつるはしを振る回数になる。1回 PICK_FRAMES (0.3秒)。
 *   面1  =  4個 x 2 =  8回 ( 2.4秒)
 *   面8  =  6個 x 5 = 30回 ( 9.0秒)
 *   面15 = 10個 x 8 = 80回 (24.0秒)
 * 耐久度は升の右上に出すので9まで (2桁は入らない)。
 */
static const uint8_t coal_n_tbl[MAX_STAGE] = {
    4, 4, 5, 5, 5, 6, 6, 6, 7, 7, 8, 8, 9, 9, 10
};
static const uint8_t coal_hp_tbl[MAX_STAGE] = {
    2, 2, 3, 3, 4, 4, 5, 5, 5, 6, 6, 6, 7, 7, 8
};
/* TODO: 絵が異なり耐久度がさらに高い「特殊な黒ダイヤ」。coal[] の値域はまだ余っている
   (1..COAL_HP_MAX と COAL_CRACKED しか使っていない) ので、種別は別配列か上位ビットで持つ。 */

static void place_coal(void)
{
    uint8_t want = coal_n_tbl[stage - 1];
    uint8_t hp   = coal_hp_tbl[stage - 1];
    uint16_t i;

    coal_left = 0;
    build_candidates(1);
    while (coal_left < want) {
        i = take_candidate();
        if (i == 0xFFFF) {
            return;                 /* 置ける升が尽きた (通常は起きない) */
        }
        coal[i] = hp;
        coal_left++;
    }
}

/* 岩を土の中へ。真下も土であること (置いた瞬間に落ち始めないように)。 */
static void place_rocks(void)
{
    uint8_t n = (uint8_t)(2 + (stage - 1) / 7);      /* 2〜4個 */
    uint16_t i;
    if (n > ROCK_MAX) {
        n = ROCK_MAX;
    }
    while (n) {
        build_candidates(2);        /* 岩は互いの真下条件に効くので毎回作り直す (最大4個) */
        i = take_candidate();
        if (i == 0xFFFF) {
            return;
        }
        rock_place((uint8_t)(i % COLS), (uint8_t)(i / COLS));
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
    if (dug[i]) {
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
            dug[i] = 1;                  /* 1ブロックだけ開ける */
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

/* 掘れていてまだ印を付けていなければ、印を付けて積む。 */
static uint8_t push_if(uint8_t sp, uint8_t *stk, uint8_t k)
{
    if (dug[k] && !reach[k]) {
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
    if (!dug[i]) {
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
        dug[(uint16_t)r * COLS + cc] = 1;
    }
    rr = r;
    while (rr != br) {
        rr = (uint8_t)((rr < br) ? rr + 1 : rr - 1);
        dug[(uint16_t)rr * COLS + bc] = 1;
    }
    dug[(uint16_t)r * COLS + c] = 1;
}

static void set_stage_layout(void)
{
    uint16_t i;
    uint8_t cc, cr;

    for (i = 0; i < NCELL; i++) {
        used[i] = 0;
    }

    /* 面ごとに土の色を変える */
    switch ((stage - 1) % 3) {
    case 0:  vdp_set_palette(COL_DIRT, 4, 2, 1); break;   /* 炭層まじりの茶 */
    case 1:  vdp_set_palette(COL_DIRT, 3, 3, 2); break;   /* 硬い岩盤の灰 */
    default: vdp_set_palette(COL_DIRT, 2, 2, 4); break;   /* 湧水を含む青みの層 */
    }

    carve_stage_number();

    /* 自機の出発点: 掘り抜きの一番上に近いところ (坑口から降りてくる感じ) */
    i = nearest_dug(8, 0);
    start_c = (uint8_t)(i % COLS);
    start_r = (uint8_t)(i / COLS);
    used[i] = 1;

    /* 敵の湧き出し口: 数字の掘り抜きとは別に、土の中へ独立した1ブロックの穴を開ける。
       掘り抜きの上に置くと、掘り進めた先から敵が飛び出してきて理不尽な死に方になる。
       自機の出発点からは十分に離す。 */
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

    place_powder(POWDER_PER_STAGE);
    place_rocks();
    place_coal();
}

void build_stage(void)
{
    uint16_t i;
    uint8_t c, r;

    for (i = 0; i < NCELL; i++) {
        dug[i] = 0;
        powder[i] = 0;
        bombdrop[i] = 0;
        coal[i] = 0;
        cell_link[i] = 0;
    }
    rock_clear();
    set_stage_layout();

    dug_n = 0;
    for (i = 0; i < NCELL; i++) {
        if (dug[i]) {
            dug_n++;
        }
    }
    dug_pct = (uint8_t)((dug_n * 100u) / NCELL);

    /* 面の最初から掘れている升どうしは、描画上も膜が無いので繋がっている扱いにする。
       (膜が残るのは「片方が後から掘られた」ときだけ) */
    for (r = 0; r < ROWS; r++) {
        for (c = 0; c < COLS; c++) {
            uint16_t k = (uint16_t)r * COLS + c;
            if (!dug[k]) {
                continue;
            }
            if (c + 1 < COLS && dug[k + 1])    { cell_link[k] |= LINK_RIGHT; }
            if (r + 1 < ROWS && dug[k + COLS]) { cell_link[k] |= LINK_DOWN; }
        }
    }

    /* 全面を土で塗ってから既掘セルを彫り、最後に火薬を置く */
    vdp_fill(0, 0, 256, FIELD_H, COL_DIRT);
    for (r = 0; r < ROWS; r++) {
        for (c = 0; c < COLS; c++) {
            if (coal[(uint16_t)r * COLS + c]) {
                draw_coal(c, r);
                continue;
            }
            if (c == nest_c && r == nest_r) {
                draw_cave(c, r);
                continue;
            }
            draw_dug_cell((int8_t)c, (int8_t)r);
            if (powder[(uint16_t)r * COLS + c]) {
                draw_powder(c, r);
            }
            if (bombdrop[(uint16_t)r * COLS + c]) {
                draw_bombdrop(c, r);
            }
        }
    }
    if (bath_on) {
        draw_bath(bath_c, bath_r);
    }
}
