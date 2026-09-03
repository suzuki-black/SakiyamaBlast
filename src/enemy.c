/*
 * SakiyamaBlast - 敵 (坑内の小動物: ネズミ / コウモリ)
 *
 * 出現は Dig-Dug 式: 面開始時に既掘セルへ初期配置する (中央の巣からの湧きは無し)。
 * 移動は Mr. Do! 式。docs/MRDO_RESEARCH.md の通り:
 *  - 節点で通れる4方向のマスクを作る (FINDEXITS 相当)。
 *  - 通れるなら現在の向きを維持し、だめなら選び直す。さらに約1/8の確率で強制的に
 *    選び直す (WORKEXITS の RND2 & 7 == 0)。一本道での往復や完全な予測を防ぐ。
 *  - 選び直すときは自機に近づく向きを優先する。
 *  - 一定時間で掘り屋 (Driller Dino 相当) へ変異し、土を掘って自機へ向かう。
 *    変異中は一瞬止まり、掘っている間は自機と同じく減速する (EATWALL 相当)。
 */
#include "game.h"
#include "vdp.h"
#include "sprite.h"
#include "art.h"

uint8_t en_state[ENEMY_MAX];
uint8_t en_x[ENEMY_MAX], en_y[ENEMY_MAX];

static uint8_t  en_dir[ENEMY_MAX];
uint8_t  en_ghost[ENEMY_MAX];             /* 1 = 透明で土をすり抜けている (計測用に公開) */
uint8_t  en_kind[ENEMY_MAX];       /* EN_RAT / EN_BAT */
static uint8_t  en_gdirt[ENEMY_MAX];      /* 透明化してから一度でも土の中へ入ったか */
static uint8_t  en_acc[ENEMY_MAX];
static uint8_t  en_pause[ENEMY_MAX];
uint16_t en_morph[ENEMY_MAX];             /* 透明化までの残りフレーム (計測用に公開) */
static uint16_t scatter_t;                /* 散開/追跡の切り替えタイマ */
static uint8_t  scattering;               /* 1 = 散開中 (各自の隅へ向かう) */
uint16_t en_rest[ENEMY_MAX];              /* 洞窟で静養している残りフレーム。0 で出られる */
static uint8_t  en_pool;                  /* この面のプール数 (先頭から en_pool 体を使う) */
uint8_t  en_warm[ENEMY_MAX];              /* 湧いて点滅中 (まだ当たらない)。計測用に公開 */
uint8_t  en_stun[ENEMY_MAX];              /* よろけの深さ 0..STUN_MAX */
static uint16_t en_stun_t[ENEMY_MAX];     /* 次に1段戻るまで */
static uint8_t  en_col[ENEMY_MAX];      /* 色テーブルへ焼いた色 (変化時だけ書く) */
static uint16_t spawn_timer;

/* 16bit LFSR。turbo R でも軽く、面ごとに種を振れば十分にばらける。 */
static uint16_t rnd_state = 0xACE1;

void rnd_seed(uint16_t s)
{
    rnd_state = s ? s : 0xACE1;
}

uint8_t rnd8(void)
{
    rnd_state ^= (uint16_t)(rnd_state << 7);
    rnd_state ^= (uint16_t)(rnd_state >> 9);
    rnd_state ^= (uint16_t)(rnd_state << 8);
    return (uint8_t)rnd_state;
}

void enemy_clear(void)
{
    uint8_t i;
    for (i = 0; i < ENEMY_MAX; i++) {
        en_state[i] = 0;
        en_warm[i] = 0;
        en_stun[i] = 0;
        en_stun_t[i] = 0;
        en_col[i] = 0xFF;
        spr_park((uint8_t)(SPR_ENEMY + i));
    }
}

/* 透明化までのフレーム数。面が進むほど短く、さらに**自分が掘った升の数だけ**短くなる。
   後者が切羽圧で、「掘るほど危険が増すが掘らないとクリアできない」を成立させる項。 */
static uint16_t ghost_frames(void)
{
    uint16_t t = GHOST_BASE;
    uint16_t d = (uint16_t)(stage - 1) * GHOST_STEP;
    d = (uint16_t)(d + (uint16_t)(dug_n - dug_n0) * GHOST_PRESSURE);
    if (d >= t || (uint16_t)(t - d) < GHOST_MIN) {
        return GHOST_MIN;
    }
    return (uint16_t)(t - d);
}

/* 面ごとの総出現数。面が進むほど増える。 */
/* この面のプール数。面が進むと増える。 */
static uint8_t pool_size(void)
{
    uint16_t n = (uint16_t)(POOL_BASE + (stage - 1) / POOL_STEP);
    if (n > ENEMY_MAX) {
        n = ENEMY_MAX;
    }
    return (uint8_t)n;
}

/* 倒された敵が洞窟にこもる時間。面が進むほど短い＝早く戻ってくる。 */
static uint16_t respawn_frames(void)
{
    uint16_t t = RESPAWN_BASE;
    uint16_t d = (uint16_t)(stage - 1) * RESPAWN_STEP;
    if (d >= t || (uint16_t)(t - d) < RESPAWN_MIN) {
        return RESPAWN_MIN;
    }
    return (uint16_t)(t - d);
}

void enemy_stage_reset(void)
{
    uint8_t i;
    enemy_clear();
    en_pool = pool_size();
    for (i = 0; i < ENEMY_MAX; i++) {
        en_rest[i] = 0;
        en_kind[i] = (uint8_t)(i % EN_KINDS);   /* 赤→桃→水→橙 を順に配る */
    }
    scattering = 1;                  /* 面の頭は散開から始める (いきなり詰めてこない) */
    scatter_t = SCATTER_FRAMES;
    spawn_timer = SPAWN_GAP;
}

/* 巣から1体出す。出せなければ何もしない。 */
/* プールの i 番を洞窟から出す。色は面の頭で決めてあるので触らない。 */
static void enemy_spawn(uint8_t i)
{
    {
        en_x[i] = (uint8_t)(nest_c << 4);
        en_y[i] = (uint8_t)(nest_r << 4);
        en_state[i] = 1;
        en_dir[i] = (uint8_t)(DIR_UP + (rnd8() & 3));   /* 出た向きは適当に散らす */
        en_ghost[i] = 0;
        en_acc[i] = 0;
        en_pause[i] = 0;
        en_morph[i] = ghost_frames();
        en_warm[i] = SPAWN_WARM;         /* 洞窟の口で点滅してから実体化する */
    }
}

/* 湧きの制御: 決められた総数まで、一定時間ごと、または全滅した瞬間に次を出す。 */
/*
 * 洞窟から1体ずつ出す。出られるのは「画面に居ない」かつ「静養が明けた」敵だけ。
 * 面の頭は全員の静養が0なので、SPAWN_GAP ごとに次々と出てくる。
 * 倒された敵は respawn_frames() ぶん洞窟にこもるので、戻ってくるまで間が空く。
 */
static void spawn_tick(void)
{
    uint8_t i;

    for (i = 0; i < en_pool; i++) {
        if (!en_state[i] && en_rest[i]) {
            en_rest[i]--;               /* 洞窟で静養中 */
        }
    }
    if (spawn_timer) {
        spawn_timer--;
        return;                         /* 湧き口は1体ずつ。間隔を空ける */
    }
    for (i = 0; i < en_pool; i++) {
        if (!en_state[i] && !en_rest[i]) {
            enemy_spawn(i);
            spawn_timer = SPAWN_GAP;
            return;
        }
    }
}

/* 透明でも「トンネルの中に居る間」は倒せる (Dig Dug 準拠:
   "once a ghost enters a tunnel, it can be killed")。土の中に居る間だけ無敵。 */
/*
 * 敵 i がその升に掛かっているか。
 *
 * **中心の升が一致するかで見てはいけない。** 敵は升の途中にも居るので、
 * 半分はみ出している敵をダイナマイトがすり抜けてしまう。16x16 同士の
 * 重なりで見て、4px 以上重なっていれば当たりとする。
 */
/* その升に重なっているか。**土の中に居るかは見ない。**
   つるはしは土ごと叩くので、透明化している敵にも届く (Dig Dug 準拠)。 */
static uint8_t near_cell(uint8_t i, int8_t cx, int8_t cy)
{
    int16_t dx = (int16_t)((int16_t)en_x[i] - ((int16_t)cx << 4));
    int16_t dy = (int16_t)((int16_t)en_y[i] - ((int16_t)cy << 4));
    if (dx < 0) { dx = (int16_t)-dx; }
    if (dy < 0) { dy = (int16_t)-dy; }
    return (uint8_t)((dx < 12 && dy < 12) ? 1 : 0);
}

/* こちらは坑道の中だけ。飛んでいるダイナマイトの当たり判定に使う。 */
static uint8_t hits_cell(uint8_t i, int8_t cx, int8_t cy)
{
    if (!en_state[i] || (en_ghost[i] && !cell_dug(cx, cy))) {
        return 0;
    }
    return near_cell(i, cx, cy);
}

/* その升に敵が居るか。投げたダイナマイトが当たるかどうかの判定に使う。
   飛んでいるのは坑道の中だけなので、土をすり抜けている敵はそもそも当たらない。 */
uint8_t enemy_at(int8_t cx, int8_t cy)
{
    uint8_t i;
    if (cx < 0 || cx >= COLS || cy < 0 || cy >= ROWS) {
        return 0;
    }
    for (i = 0; i < ENEMY_MAX; i++) {
        if (hits_cell(i, cx, cy)) {
            return 1;
        }
    }
    return 0;
}

/* つるはしで敵をよろけさせる。叩くほど深く潰れ、時間で1段ずつ戻る。
   潰れている間は動かず、触れてもミスにならないので、先頭の敵をよろけさせて
   通り抜け、奥の敵も潰して岩の下へ並べる、という組み立てができる。
   **透明化中の敵にも当たる** (下の enemy_stun_at を参照)。 */
/*
 * つるはしで敵をよろけさせる。(cx,cy) から (dx,dy) の向きへ PICK_REACH 升ぶん探し、
 * **一番手前の敵1体だけ**に効かせる。
 *
 * ただし既に最深までよろけている敵は飛ばして、その後ろの敵を狙う。よろけた敵は
 * その場で止まるので、飛ばさないと目の前の1体に無駄打ちし続けることになり、
 * 「先頭を潰して次を潰す」という組み立てができない。
 * 全員が最深なら、一番手前へ当てたことにして空振り扱いにはしない。
 */
/*
 * 落盤で盤面の敵をまとめてよろけさせる。
 * 「並べた場所へ岩を持ってくる」より「岩の下で敵を止める」ほうが安い、という発想。
 * 位置合わせが要らないので、落ちてくる岩の命中率が跳ね上がる。
 */
void enemy_stun_all(uint8_t power)
{
    uint8_t i;
    for (i = 0; i < ENEMY_MAX; i++) {
        if (!en_state[i] || en_warm[i]) {
            continue;
        }
        en_stun[i] = (uint8_t)(en_stun[i] + power);
        if (en_stun[i] > STUN_MAX) {
            en_stun[i] = STUN_MAX;
        }
        en_stun_t[i] = STUN_STEP;
        en_acc[i] = 0;
    }
}

/*
 * 岩に押された敵を1升ぶん後ろへ下がらせる。doit=0 なら下がれるかを見るだけ。
 *
 * **Mr. Do! では、リンゴを押し始めるとモンスターが後ずさりする。横押しでは潰れない。**
 * 潰せるのは落ちてきた岩だけ。押しは「岩を運ぶ」ためだけの操作にしてある。
 * 下がる先が無ければ 0 を返し、岩はそこで止まる (敵が壁の役をする)。
 */
uint8_t enemy_shove(int8_t cx, int8_t cy, int8_t dx, uint8_t doit)
{
    uint8_t i;
    int8_t nx = (int8_t)(cx + dx);
    for (i = 0; i < ENEMY_MAX; i++) {
        if (!en_state[i] || en_ghost[i]) {
            continue;                    /* 土の中の敵は岩に押されない */
        }
        if (!hits_cell(i, cx, cy)) {
            continue;
        }
        if (nx < 0 || nx >= COLS) {
            return 0;
        }
        if (cell_coal(nx, cy) || rock_at(nx, cy) >= 0) {
            return 0;
        }
        if (!cell_dug(nx, cy)) {
            return 0;                    /* 敵は土の中へは下がれない (自分では掘らない) */
        }
        if (doit) {
            en_x[i] = (uint8_t)((uint8_t)nx << 4);
            en_y[i] = (uint8_t)((uint8_t)cy << 4);
            en_dir[i] = (uint8_t)((dx < 0) ? DIR_LEFT : DIR_RIGHT);
            en_acc[i] = 0;
        }
    }
    return 1;
}

uint8_t enemy_stun_at(int8_t cx, int8_t cy, int8_t dx, int8_t dy, uint8_t power)
{
    uint8_t i, k;
    int8_t best = -1, any = -1;
    int8_t x = cx, y = cy;

    for (k = 0; k < PICK_REACH && best < 0; k++) {
        for (i = 0; i < ENEMY_MAX; i++) {
            /* **透明化している敵にも当たる (Dig Dug 準拠)。**
               透明の間まで無敵だと、こちらは手も足も出ずにただ待つしかない。
               つるはしは土ごと叩くもの、と考えれば筋も通る。
               ダイナマイトは坑道の中しか飛ばないので、そちらは当たらないまま。 */
            if (!en_state[i] || en_warm[i]) {
                continue;
            }
            if (!near_cell(i, x, y)) {
                continue;
            }
            if (any < 0) {
                any = (int8_t)i;
            }
            if (en_stun[i] < STUN_MAX) {
                best = (int8_t)i;      /* まだ潰しきっていない一番手前 */
                break;
            }
        }
        x = (int8_t)(x + dx);
        y = (int8_t)(y + dy);
    }
    if (best < 0) {
        best = any;
    }
    if (best < 0) {
        return 0;
    }
    i = (uint8_t)best;
    en_stun[i] = (uint8_t)(en_stun[i] + power);
    if (en_stun[i] > STUN_MAX) {
        en_stun[i] = STUN_MAX;
    }
    en_stun_t[i] = STUN_STEP;
    en_acc[i] = 0;
    return 1;
}

uint8_t enemy_kill_at(int8_t cx, int8_t cy, uint8_t by_rock)
{
    uint8_t i, killed = 0;
    if (cx < 0 || cx >= COLS || cy < 0 || cy >= ROWS) {
        return 0;
    }
    for (i = 0; i < ENEMY_MAX; i++) {
        if (hits_cell(i, cx, cy)) {
            en_state[i] = 0;
            en_rest[i] = respawn_frames();   /* 倒された敵は洞窟で静養してから戻る */
            spr_park((uint8_t)(SPR_ENEMY + i));
            killed++;
            /* 落とし物。岩で潰したら梅干し (体温を下げる = 時間を買える)、
               爆風なら火薬を半々で。岩は資源を使わないぶん報われる。 */
            if (by_rock) {
                drop_item_at(cx, cy, 1);
            } else if (rnd8() & 1) {
                drop_item_at(cx, cy, 0);
            }
        }
    }
    return killed;
}

static int8_t dir_dc(uint8_t d) { return (int8_t)((d == DIR_LEFT) ? -1 : (d == DIR_RIGHT) ? 1 : 0); }
static int8_t dir_dr(uint8_t d) { return (int8_t)((d == DIR_UP) ? -1 : (d == DIR_DOWN) ? 1 : 0); }

static uint8_t dir_rev(uint8_t d)
{
    switch (d) {
    case DIR_UP:    return DIR_DOWN;
    case DIR_DOWN:  return DIR_UP;
    case DIR_LEFT:  return DIR_RIGHT;
    default:        return DIR_LEFT;
    }
}

/* その向きへ進めるか。掘り屋は土も通れる。岩は横向きなら押せる時だけ通れる。 */
static uint8_t can_go(uint8_t i, uint8_t d)
{
    int8_t cx = (int8_t)((en_x[i] >> 4) + dir_dc(d));
    int8_t cy = (int8_t)((en_y[i] >> 4) + dir_dr(d));
    int8_t ri;
    if (cx < 0 || cx >= COLS || cy < 0 || cy >= ROWS) {
        return 0;
    }
    if (cell_coal(cx, cy)) {
        return 0;                        /* ダイヤは掘り屋でも抜けられない */
    }
    ri = rock_at(cx, cy);
    if (ri >= 0) {
        if (d == DIR_LEFT)  { return rock_can_push(ri, -1, 0); }
        if (d == DIR_RIGHT) { return rock_can_push(ri, 1, 0); }
        return 0;                        /* 縦には押せない = 岩を避ける */
    }
    if (!cell_dug(cx, cy)) {
        return 0;                        /* 敵は自分では掘らない (Dig Dug 準拠) */
    }
    /* 隣り合う坑道でも、間に土の膜が残っていれば歩いては抜けられない。
       抜けたければ透明化するしかない (=遅くなる)。 */
    return cell_linked((int8_t)(en_x[i] >> 4), (int8_t)(en_y[i] >> 4), d);
}

/* 自機に近づく向きを優先度順に並べる (プロトの CHASEDIR と同じ考え方)。 */
/* --- 色ごとの狙う升 (パックマンのゴーストに倣う) --- */

static const uint8_t kind_speed[EN_KINDS] = { SPD_RED, SPD_PINK, SPD_CYAN, SPD_ORANGE };

/* 色ごとのスプライト色。mode 2 は1行1色なので、行ごとの色表を色数ぶん持つ。
   **姿は4種とも同じで色だけが違う**ので、色を見れば動きの癖が分かる。
   5〜6行目だけ白くして目に見せている。 */
#define KCOL(c) { (c),(c),(c),(c), COL_WHITE, COL_WHITE, (c),(c),(c),(c),(c),(c),(c),(c),(c),(c) }
static const uint8_t kind_colours[EN_KINDS][16] = {
    KCOL(COL_RED),      /* 赤: 追いかけ */
    KCOL(COL_BAT),      /* 桃: 待ち伏せ */
    KCOL(COL_DIA),      /* 水: 気まぐれ (赤の位置に連動) */
    KCOL(COL_RAT)       /* 橙: お惚け */
};


/* 各色の「自分の隅」。散開中と、橙が自機に近づきすぎたときに向かう。 */
static const uint8_t corner_c[EN_KINDS] = { COLS - 1, 0, COLS - 1, 0 };
static const uint8_t corner_r[EN_KINDS] = { 0,        0, ROWS - 1, ROWS - 1 };

static void step_cell(int8_t *c, int8_t *r, uint8_t dir, int8_t n)
{
    switch (dir) {
    case DIR_UP:    *r = (int8_t)(*r - n); break;
    case DIR_DOWN:  *r = (int8_t)(*r + n); break;
    case DIR_LEFT:  *c = (int8_t)(*c - n); break;
    case DIR_RIGHT: *c = (int8_t)(*c + n); break;
    default: break;
    }
}

/* 赤の位置。水色が狙いを決めるのに使う (敵同士の連携)。居なければ 0 を返す。 */
static uint8_t red_cell(int8_t *c, int8_t *r)
{
    uint8_t k;
    for (k = 0; k < ENEMY_MAX; k++) {
        if (en_state[k] && en_kind[k] == EN_RED && !en_warm[k]) {
            *c = (int8_t)((en_x[k] + 8) >> 4);
            *r = (int8_t)((en_y[k] + 8) >> 4);
            return 1;
        }
    }
    return 0;
}

static void target_cell(uint8_t i, int8_t *tc, int8_t *tr)
{
    uint8_t kind = en_kind[i];
    int8_t pc = (int8_t)((px + 8) >> 4);
    int8_t pr = (int8_t)((py + 8) >> 4);
    int8_t rc, rr;

    if (scattering) {
        *tc = (int8_t)corner_c[kind];
        *tr = (int8_t)corner_r[kind];
        return;
    }
    switch (kind) {
    case EN_PINK:                       /* 待ち伏せ: 向いている先の4升先 */
        *tc = pc; *tr = pr;
        step_cell(tc, tr, player_dir(), PINK_AHEAD);
        return;
    case EN_CYAN:                       /* 気まぐれ: 自機の2升先を赤から見て倍に伸ばす */
        *tc = pc; *tr = pr;
        step_cell(tc, tr, player_dir(), CYAN_AHEAD);
        if (red_cell(&rc, &rr)) {
            *tc = (int8_t)(*tc + (*tc - rc));
            *tr = (int8_t)(*tr + (*tr - rr));
        }
        return;
    case EN_ORANGE: {                   /* お惚け: 近づきすぎたら自分の隅へ引く */
        int8_t dc = (int8_t)(pc - (int8_t)((en_x[i] + 8) >> 4));
        int8_t dr = (int8_t)(pr - (int8_t)((en_y[i] + 8) >> 4));
        if (dc < 0) { dc = (int8_t)-dc; }
        if (dr < 0) { dr = (int8_t)-dr; }
        if (dc + dr <= CLYDE_NEAR) {
            *tc = (int8_t)corner_c[EN_ORANGE];
            *tr = (int8_t)corner_r[EN_ORANGE];
            return;
        }
        break;
    }
    default:                            /* 赤: 自機の升をそのまま */
        break;
    }
    *tc = pc;
    *tr = pr;
}

/* 狙う升までの距離の2乗。小さいほうへ曲がる (パックマンの交差点規則)。 */
static uint16_t dist2_to(int8_t c, int8_t r, int8_t tc, int8_t tr)
{
    int16_t dc = (int16_t)(c - tc);
    int16_t dr = (int16_t)(r - tr);
    return (uint16_t)(dc * dc + dr * dr);
}

/*
 * 進路を決める。パックマンの交差点規則に倣い、**引き返し以外で、狙う升までの
 * 距離がいちばん短くなる方向**を選ぶ。同点のときは 上→左→下→右 の順。
 * 本作は盤面が毎回違ってパターンを覚えられないので、ごく低い確率だけ揺らす。
 */
static void decide_dir(uint8_t i)
{
    static const uint8_t pref[4] = { DIR_UP, DIR_LEFT, DIR_DOWN, DIR_RIGHT };
    uint8_t rev = dir_rev(en_dir[i]);
    int8_t tc, tr;
    int8_t cx = (int8_t)(en_x[i] >> 4);
    int8_t cy = (int8_t)(en_y[i] >> 4);
    uint8_t k, best = DIR_NONE, cand[4], n = 0;
    uint16_t bestd = 0xFFFF;

    for (k = 0; k < 4; k++) {
        if (pref[k] != rev && can_go(i, pref[k])) {
            cand[n++] = pref[k];
        }
    }
    if (n == 0) {
        if (can_go(i, rev)) {
            en_dir[i] = rev;             /* 行き止まり: 引き返す */
        }
        return;
    }
    if (n == 1) {
        en_dir[i] = cand[0];             /* 一本道: 選ぶ余地なし */
        return;
    }
    if ((rnd8() & STRAY_ALL) == 0) {
        en_dir[i] = cand[rnd8() % n];    /* たまに気まぐれ */
        return;
    }

    target_cell(i, &tc, &tr);
    for (k = 0; k < n; k++) {
        int8_t nc = cx, nr = cy;
        uint16_t d;
        step_cell(&nc, &nr, cand[k], 1);
        d = dist2_to(nc, nr, tc, tr);
        if (d < bestd) {
            bestd = d;
            best = cand[k];
        }
    }
    en_dir[i] = best;
}

/* 四方を土で塞がれているか。湧き出し口は1ブロックの穴なので、湧いた敵はここに当たる。 */
static uint8_t boxed_in(uint8_t i)
{
    if (((en_x[i] & 15) != 0) || ((en_y[i] & 15) != 0)) {
        return 0;                        /* 升目の途中では判定しない */
    }
    return (uint8_t)(!can_go(i, DIR_UP) && !can_go(i, DIR_DOWN)
                     && !can_go(i, DIR_LEFT) && !can_go(i, DIR_RIGHT));
}

static void enemy_step(uint8_t i)
{
    uint8_t at_node = (uint8_t)(((en_x[i] & 15) == 0) && ((en_y[i] & 15) == 0));
    int8_t cx, cy, ri;

    if (at_node) {
        decide_dir(i);
        cx = (int8_t)((en_x[i] >> 4) + dir_dc(en_dir[i]));
        cy = (int8_t)((en_y[i] >> 4) + dir_dr(en_dir[i]));
        if (cx < 0 || cx >= COLS || cy < 0 || cy >= ROWS) {
            return;
        }
        ri = rock_at(cx, cy);
        if (ri >= 0 && en_dir[i] != DIR_LEFT && en_dir[i] != DIR_RIGHT) {
            return;                      /* 縦には押せない */
        }
        if (!cell_dug(cx, cy)) {
            return;                      /* 敵は掘れない。坑道の中だけを歩く。 */
        }
    }

    en_acc[i] = (uint8_t)(en_acc[i] + kind_speed[en_kind[i]]);
    if (en_acc[i] < SPD_UNIT) {
        return;
    }
    en_acc[i] = (uint8_t)(en_acc[i] - SPD_UNIT);

    /* 岩は1ドットずつ押す (自機と同じ)。押せなければこのフレームは動かない。 */
    if (en_dir[i] == DIR_LEFT || en_dir[i] == DIR_RIGHT) {
        int8_t d = (int8_t)((en_dir[i] == DIR_LEFT) ? -1 : 1);
        int8_t ri2 = rock_ahead_of(en_x[i], en_y[i], d);
        if (ri2 >= 0 && !rock_push(ri2, d, 0)) {
            return;                      /* 敵は坑道の中でしか岩を動かせない */
        }
    }

    switch (en_dir[i]) {
    case DIR_UP:    if (en_y[i]) { en_y[i]--; } break;
    case DIR_DOWN:  if (en_y[i] < (uint8_t)YMAX) { en_y[i]++; } break;
    case DIR_LEFT:  if (en_x[i]) { en_x[i]--; } break;
    default:        if (en_x[i] < (uint8_t)XMAX) { en_x[i]++; } break;
    }
}

/* 透明ですり抜けている間: 自機へ向かって斜めに直進し、トンネルに入ったら実体化する。
   土もダイヤも岩も素通りする。倒せないかわりに動きは遅い (Dig Dug 準拠)。 */
/* 透明化中の敵がいま岩の升に重なっているか。 */
static uint8_t ghost_blocked(uint8_t i)
{
    return (uint8_t)(rock_at((int8_t)((en_x[i] + 8) >> 4),
                             (int8_t)((en_y[i] + 8) >> 4)) >= 0);
}

static void ghost_step(uint8_t i)
{
    uint8_t c, r;

    en_acc[i] = (uint8_t)(en_acc[i] + SPD_GHOST);
    if (en_acc[i] < SPD_UNIT) {
        return;
    }
    en_acc[i] = (uint8_t)(en_acc[i] - SPD_UNIT);

    /* **岩は土の中でも岩なので、透明化していても通り抜けられない。**
       軸ごとに戻すので、塞がれていない側へは回り込める。 */
    {
        uint8_t ox = en_x[i], oy = en_y[i];
        if (en_x[i] < px)      { if (en_x[i] < (uint8_t)XMAX) { en_x[i]++; } }
        else if (en_x[i] > px) { if (en_x[i]) { en_x[i]--; } }
        if (ghost_blocked(i)) { en_x[i] = ox; }
        if (en_y[i] < py)      { if (en_y[i] < (uint8_t)YMAX) { en_y[i]++; } }
        else if (en_y[i] > py) { if (en_y[i]) { en_y[i]--; } }
        if (ghost_blocked(i)) { en_y[i] = oy; }
    }

    c = (uint8_t)((en_x[i] + 8) >> 4);
    r = (uint8_t)((en_y[i] + 8) >> 4);
    if (!cell_dug((int8_t)c, (int8_t)r)) {
        en_gdirt[i] = 1;                 /* 土の中へ入った */
        return;
    }
    /* 一度も土へ入っていないうちは実体化しない。そうしないと、トンネルの中で
       透明化した敵がすぐ戻ってしまい、すり抜けがほとんど発動しない。 */
    if (en_gdirt[i]) {
        en_x[i] = (uint8_t)(c << 4);     /* トンネルに入った -> グリッドへ寄せて実体化 */
        en_y[i] = (uint8_t)(r << 4);
        en_ghost[i] = 0;
        en_acc[i] = 0;
        en_morph[i] = ghost_frames();
    }
}

static uint8_t touches_player(uint8_t i)
{
    int16_t dx = (int16_t)en_x[i] - (int16_t)px;
    int16_t dy = (int16_t)en_y[i] - (int16_t)py;
    if (dx < 0) { dx = (int16_t)(-dx); }
    if (dy < 0) { dy = (int16_t)(-dy); }
    return (uint8_t)((dx < 12) && (dy < 12));
}

void enemy_update(void)
{
    uint8_t i;
    spawn_tick();
    /* 散開と追跡を交互に切り替える。散開の間は各自の隅へ向かうので固まらない。 */
    scatter_t--;
    if (scatter_t == 0) {
        scattering = (uint8_t)!scattering;
        scatter_t = (uint16_t)(scattering ? SCATTER_FRAMES : CHASE_FRAMES);
    }
    for (i = 0; i < ENEMY_MAX; i++) {
        if (!en_state[i]) {
            continue;
        }
        if (en_warm[i]) {
            en_warm[i]--;                /* 湧き出し口で点滅中。動かないし当たらない */
            continue;
        }
        /* よろけている間は動かない。時間で1段ずつ戻り、戻る姿がそのまま
           「あと何秒で復活するか」の表示になる。 */
        if (en_stun[i]) {
            /* 0 で入ると引き算が回り込んで事実上戻らなくなるので、先に見る。 */
            if (en_stun_t[i] == 0 || --en_stun_t[i] == 0) {
                en_stun[i]--;
                en_stun_t[i] = STUN_STEP;
            }
            continue;                    /* 動かないし、触れても当たらない */
        }
        if (en_pause[i]) {
            en_pause[i]--;               /* 変異中は一瞬止まる */
        } else {
            if (en_ghost[i]) {
                ghost_step(i);
            } else {
                if (en_morph[i]) {
                    en_morph[i]--;
                }
                /* 湧き出し口から出られない敵は、待たずに透明になってすり抜ける。
                   そうしないと穴の中で渋滞する。 */
                if (en_morph[i] == 0 || boxed_in(i)) {
                    en_ghost[i] = 1;     /* 透明になって土をすり抜ける */
                    en_pause[i] = GHOST_PAUSE;
                    en_gdirt[i] = 0;
                    en_acc[i] = 0;
                } else {
                    enemy_step(i);
                }
            }
        }
        if (!invuln && !dead && touches_player(i)) {
            dead = 1;
        }
    }
}

/* よろけの段ごとの色表を組み立てる。潰れると体が下がるので、白い帯もそこへ合わせる。
   段ごとに表を持つと 4種 x 4段 x 16行 = 256バイト要るので、その場で組む。 */
static uint8_t stun_col[16];
static const uint8_t stun_band[4] = { 4, 6, 8, 11 };   /* 白い帯の開始行 */

static const uint8_t *stun_colours(uint8_t kind, uint8_t lv)
{
    uint8_t k, top;
    if (lv == 0) {
        return kind_colours[kind];
    }
    top = stun_band[lv];
    for (k = 0; k < 16; k++) {
        stun_col[k] = (uint8_t)((k == top || k == (uint8_t)(top + 1))
                                ? COL_WHITE : kind_colours[kind][0]);
    }
    return stun_col;
}

/* 透明化中の色。16行ぜんぶ同じ淡い色にして、輪郭だけが見える状態にする。 */
static const uint8_t ghost_colours[16] = {
    COL_BATH, COL_BATH, COL_BATH, COL_BATH, COL_BATH, COL_BATH, COL_BATH, COL_BATH,
    COL_BATH, COL_BATH, COL_BATH, COL_BATH, COL_BATH, COL_BATH, COL_BATH, COL_BATH
};

void enemy_draw(void)
{
    uint8_t i, col;
    for (i = 0; i < ENEMY_MAX; i++) {
        if (!en_state[i]) {
            spr_park((uint8_t)(SPR_ENEMY + i));
            continue;
        }
        /* 湧き出し口で点滅している間は、8フレームおきに消す。
           まだ実体化していない = 触れても当たらない、という合図。 */
        if (en_warm[i] && (frame_n & 8)) {
            spr_park((uint8_t)(SPR_ENEMY + i));
            continue;
        }
        /* 透明化中は色を抜く。よろけているときは潰れた絵に差し替え、
           白い帯の位置も体に合わせて下げる。**色そのものは変えない** —
           色は敵のアルゴリズムを表す情報なので、状態表示に使ってはいけない。 */
        /* **よろけている間は透明でも実体の色で描く。** そうしないと、叩いた手応えが
           画面に出ず「効いていない」と見える。潰れた姿 (PAT_STUN*) は元から出る。 */
        col = (uint8_t)((en_ghost[i] && !en_stun[i])
                        ? 0xFF : (uint8_t)(en_kind[i] + (en_stun[i] << 2)));
        if (col != en_col[i]) {
            en_col[i] = col;
            if (col == 0xFF) {
                spr_color_lines((uint8_t)(SPR_ENEMY + i), ghost_colours);
            } else {
                spr_color_lines((uint8_t)(SPR_ENEMY + i),
                                stun_colours(en_kind[i], en_stun[i]));
            }
        }
        spr_set((uint8_t)(SPR_ENEMY + i), en_x[i], (uint8_t)(en_y[i] - 1),
                (uint8_t)(en_stun[i] ? (PAT_STUN1 + en_stun[i] - 1) : PAT_RAT));
    }
}
