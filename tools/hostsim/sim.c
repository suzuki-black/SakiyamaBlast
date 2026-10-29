/*
 * SakiyamaBlast - 面の数値化ツール (ホスト側シミュレータ)
 *
 * src/ のゲームロジックをそのまま gcc でビルドし、VDP/PSG だけスタブに差し替えて
 * ヘッドレスで走らせる。毎フレームの状態を JSONL で吐き、採点は Python 側で行う。
 *
 * 使い方:  hostsim --stage N [--seed S] [--frames F] [--bot smart|greedy|idle] [--ura]
 *
 * 実機との一致は「同じ乱数の種で同じ盤面が出るか」で確認する (tools/hostsim/verify.sh)。
 */
#include "game.h"
#include "sound.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern uint8_t sim_stick, sim_trig, sim_trig_b;
extern uint32_t sim_score;
extern uint8_t sfx_fired[16];

/* ------------------------------------------------------------------ *
 * 通行グラフ。膜 (cflag[] の LINK_*) をそのまま辿る。
 * 「1升隣でも土の膜で隔てられていれば敵は来られない」ので、危険度は
 * ユークリッド距離ではなく必ずこのグラフ上の距離で測る。
 * ------------------------------------------------------------------ */
static int16_t bfs_d[NCELL];

static int passable(int c, int r)
{
    uint16_t i = (uint16_t)r * COLS + c;
    if (c < 0 || c >= COLS || r < 0 || r >= ROWS) { return 0; }
    if (!cf_on(i, CF_DUG)) { return 0; }
    if (coal[i] && coal[i] != COAL_CRACKED) { return 0; }
    /* 岩は塞ぐ。横なら押せることもあるが、経路として当てにすると
       押せない岩に延々ぶつかり続けるので、通れない扱いにしておく。 */
    if (rock_at((int8_t)c, (int8_t)r) >= 0) { return 0; }
    return 1;
}

/* a から b へ動けるか。膜が張っていれば通れない。 */
static int linked(int c, int r, int nc, int nr)
{
    uint16_t i = (uint16_t)r * COLS + c;
    uint16_t j = (uint16_t)nr * COLS + nc;
    if (nc == c + 1) { return cflag[i] & LINK_RIGHT; }
    if (nc == c - 1) { return cflag[j] & LINK_RIGHT; }
    if (nr == r + 1) { return cflag[i] & LINK_DOWN; }
    if (nr == r - 1) { return cflag[j] & LINK_DOWN; }
    return 0;
}

static void bfs_into(int sc, int sr, int16_t *out)
{
    static uint16_t q[NCELL];
    int head = 0, tail = 0, k;
    static const int dc[4] = { 0, -1, 0, 1 };
    static const int dr[4] = { -1, 0, 1, 0 };
    for (k = 0; k < NCELL; k++) { out[k] = -1; }
    if (sc < 0 || sc >= COLS || sr < 0 || sr >= ROWS) { return; }
    /* 始点は無条件に入れる。自機は掘りかけの升に立っていることがあり、そこを
       「通れない」と扱うと経路が一切出なくなってボットが固まる。 */
    out[sr * COLS + sc] = 0;
    q[tail++] = (uint16_t)(sr * COLS + sc);
    while (head < tail) {
        uint16_t cur = q[head++];
        int c = cur % COLS, r = cur / COLS;
        for (k = 0; k < 4; k++) {
            int nc = c + dc[k], nr = r + dr[k];
            if (!passable(nc, nr)) { continue; }
            if (!linked(c, r, nc, nr)) { continue; }
            if (out[nr * COLS + nc] >= 0) { continue; }
            out[nr * COLS + nc] = (int16_t)(out[cur] + 1);
            q[tail++] = (uint16_t)(nr * COLS + nc);
        }
    }
}

static void bfs(int sc, int sr) { bfs_into(sc, sr, bfs_d); }

/* 敵ごとの通行グラフ上の距離表。透明化中の敵は土を斜めに抜けるのでグラフに乗らず、
   マンハッタン距離で近似する (実際の移動は SPD_GHOST で遅い)。 */
static int16_t en_d[ENEMY_MAX][NCELL];
static uint8_t en_live[ENEMY_MAX];

static void update_enemy_dist(void)
{
    int k, i;
    for (k = 0; k < ENEMY_MAX; k++) {
        int ec = en_x[k] >> 4, er = en_y[k] >> 4;
        en_live[k] = (uint8_t)(en_state[k] == 1);
        if (!en_live[k]) { continue; }
        if (en_ghost[k]) {
            for (i = 0; i < NCELL; i++) {
                int dc2 = (i % COLS) - ec, dr2 = (i / COLS) - er;
                if (dc2 < 0) { dc2 = -dc2; }
                if (dr2 < 0) { dr2 = -dr2; }
                en_d[k][i] = (int16_t)(dc2 > dr2 ? dc2 : dr2);   /* 斜め移動できるので長い辺が歩数 */
            }
        } else {
            bfs_into(ec, er, en_d[k]);
        }
    }
}

static int can_step(int c, int r);

/* 敵iがその升へ来るのに掛かるフレーム数。
   速度は分子加算式 (SPD_UNIT に達するごとに1px) なので、1升16pxを渡る時間は
   16 * SPD_UNIT / 速度 フレーム。透明化中は SPD_GHOST で這う。 */
static const uint8_t kind_spd[EN_KINDS] = { SPD_RED, SPD_PINK, SPD_CYAN, SPD_ORANGE };

static int reach_frames(int k, int c, int r)
{
    int16_t d = en_d[k][(uint16_t)r * COLS + c];
    int spd;
    if (d < 0) { return 99999; }
    spd = en_ghost[k] ? SPD_GHOST : kind_spd[en_kind[k]];
    return (int)d * 16 * SPD_UNIT / (spd ? spd : 1);
}

/* 到達余裕: 一番早く自機の升へ来られる敵の所要フレーム数。 */
static int t_reach(int c, int r)
{
    int k, best = 99999;
    for (k = 0; k < ENEMY_MAX; k++) {
        int t;
        if (!en_live[k] || en_warm[k]) { continue; }   /* 点滅中はまだ当たらない */
        t = reach_frames(k, c, r);
        if (t < best) { best = t; }
    }
    return best;
}

/* 退路数: 隣の4方向のうち、そこへ動くと到達余裕が増える向きの数。
   0 か 1 なら追い詰められている。 */
static int escapes(int c, int r)
{
    static const int dc[4] = { 0, -1, 0, 1 };
    static const int dr[4] = { -1, 0, 1, 0 };
    int k, n = 0, here = t_reach(c, r);
    for (k = 0; k < 4; k++) {
        int nc = c + dc[k], nr = r + dr[k];
        if (!can_step(nc, nr)) { continue; }
        if (t_reach(nc, nr) > here) { n++; }
    }
    return n;
}

/* 揺れている / 落ちてくる岩の真下か。
   人間は0.75秒の揺れを見て避けるので、ボットにも見せないと岩の危険を
   過大に評価してしまう (岩を一切見ないボットは面の評価にならない)。 */
static int under_falling_rock(int c, int r)
{
    uint8_t k;
    for (k = 0; k < ROCK_MAX; k++) {
        int rr;
        /* **揺れている岩 (ROCK_JIG) は危険としない。** 自機がその真下に入ると
           支えになって落ちなくなる (Dig Dug 準拠)。揺れを避けさせると、
           炭層の上に岩がある配置で「掘った黒ダイヤを取りに行かない」ボットになる。
           本当に危ないのは既に落ちている岩だけ。 */
        if (rock_state[k] != ROCK_FALL) { continue; }
        if ((rock_x[k] >> 4) != c) { continue; }
        rr = rock_y[k] >> 4;
        if (rr <= r && r - rr <= 4) { return 1; }   /* 同じ列の上にある */
    }
    return 0;
}

/* その升にとって「一番近い敵が何歩で来られるか」。来られないなら大きい値。 */
static int danger_at(int c, int r)
{
    int k, best = 999;
    if (under_falling_rock(c, r)) { return 0; }   /* 岩の下は最悪 */
    uint16_t i = (uint16_t)r * COLS + c;
    for (k = 0; k < ENEMY_MAX; k++) {
        if (!en_live[k]) { continue; }
        if (en_d[k][i] >= 0 && en_d[k][i] < best) { best = en_d[k][i]; }
    }
    return best;
}

/* ------------------------------------------------------------------ *
 * 敵の先読み (2026-10-28)
 *
 * **升までの「歩数」ではなく「来るまでのフレーム数」で測る。**
 * 歩数で測ると、25px/s の橙と 35px/s の赤が同じ危険度になってしまう。
 * 実際には3升先から来るのに 114 フレームと 81 フレームの差がある。
 *
 * 先読みに入れるもの:
 *   - **種ごとの速さ** (`kind_spd`)。
 *   - **引き返さない。** `decide_dir` は引き返しを候補から外すので、
 *     いま通り過ぎた敵は最初の一歩では戻ってこられない。行き止まりだけは
 *     引き返すので、他に道が無いときは許す。
 *   - **よろけ** (`en_stun`)。潰れている間は動かないので、そのぶん遅れて来る。
 *     これを入れると「先頭を潰してその横を通る」が経路として選べるようになる。
 *   - **湧き口の点滅** (`en_warm`)。実体化するまでは動かないし当たらない。
 *
 * **入れてみて効かなかったもの** (どれも 720回で測った。docs/HOSTSIM.md §先読み):
 *   - **透明化の予兆** (`en_morph`)。あと N フレームで土をすり抜けてくる敵を
 *     直線距離で見る項。SPD_GHOST は這う速さ (1升 128 フレーム) なので、
 *     坑道を歩いて来るほうがほとんど常に早く、入れると地平が埋まって
 *     かえって勾配が消えた (241 -> 234)。**いま透明な敵は直線で見る**
 *     (坑道を歩いていないので、そうしないと見えない)。
 *   - **落ちてくる岩** (`under_falling_rock`)。歩数で測っていた頃の
 *     `danger_at` は岩の真下を 0 にしていたが、`bot_cost` が岩そのものに
 *     罰則を持っているので二重だった (241 -> 240)。
 *
 * `danger_at` (歩数) は残してある。毎フレームの記録に出す `treach` / `esc` が
 * そちらの尺度で、採点側 (`metrics.py` / `emotion.py`) の値を動かさないため。
 * ------------------------------------------------------------------ */
#define FC_FAR     99999        /* 来られない */
/* 重みの効き方は実測で決める (下の3つ)。掃くときは -DFC_HORIZON=... で上書きする。 */
#ifndef FC_HORIZON
#define FC_HORIZON   128        /* 先読みの地平。これより先に来る敵は気にしない */
#endif
#ifndef FC_DIV
#define FC_DIV         1        /* 罰則 = (地平 - 到着フレーム) / これ */
#endif
#ifndef FC_PANIC
#define FC_PANIC      32        /* これより早く来られる升は踏み込まない (1升ぶん) */
#endif

static int16_t en_hop[ENEMY_MAX][NCELL];   /* 引き返しを除いた歩数 */
static int threat[NCELL];                  /* 一番早い敵が来るまでのフレーム数 */

/* 1升 (16px) を渡るフレーム数。速度は分子加算式 (SPD_UNIT ごとに1px)。 */
static int cross_frames(int spd)
{
    return 16 * SPD_UNIT / (spd ? spd : 1);
}

/* 敵kの歩数表。**最初の一歩だけ引き返しを除く。** */
static void hops_from_enemy(int k, int16_t *out)
{
    static const int dc[4] = { 0, -1, 0, 1 };
    static const int dr[4] = { -1, 0, 1, 0 };
    static const int dir_k[5] = { -1, 0, 2, 1, 3 };   /* DIR_* -> dc/dr の添字 */
    static uint16_t q[NCELL];
    int head = 0, tail = 0, i, d, rev = -1, n;
    int ec = en_x[k] >> 4, er = en_y[k] >> 4;
    uint16_t home;

    for (i = 0; i < NCELL; i++) { out[i] = -1; }
    if (ec < 0 || ec >= COLS || er < 0 || er >= ROWS) { return; }
    home = (uint16_t)(er * COLS + ec);

    if (en_dir[k] >= DIR_UP && en_dir[k] <= DIR_RIGHT) {
        rev = (dir_k[en_dir[k]] + 2) & 3;
        for (d = 0, n = 0; d < 4; d++) {
            int nc = ec + dc[d], nr = er + dr[d];
            if (d == rev) { continue; }
            if (passable(nc, nr) && linked(ec, er, nc, nr)) { n++; }
        }
        if (n == 0) { rev = -1; }       /* 行き止まり: 引き返す */
    }

    out[home] = 0;
    q[tail++] = home;
    while (head < tail) {
        uint16_t cur = q[head++];
        int c = cur % COLS, r = cur / COLS;
        for (d = 0; d < 4; d++) {
            int nc = c + dc[d], nr = r + dr[d], j;
            if (cur == home && d == rev) { continue; }
            if (!passable(nc, nr) || !linked(c, r, nc, nr)) { continue; }
            j = nr * COLS + nc;
            if (out[j] >= 0) { continue; }
            out[j] = (int16_t)(out[cur] + 1);
            q[tail++] = (uint16_t)j;
        }
    }
}

/* 盤面ぜんぶの「一番早い敵が来るまでのフレーム数」を引き直す。
   升の判断のたびに呼ぶ (16フレームに1回ほど)。 */
static void forecast(void)
{
    int k, i;
    for (i = 0; i < NCELL; i++) { threat[i] = FC_FAR; }
    for (k = 0; k < ENEMY_MAX; k++) {
        int ec, er, walk, gwait, gstep, delay;
        if (en_state[k] != 1) { continue; }
        ec = en_x[k] >> 4; er = en_y[k] >> 4;

        /* 動き出すまでの待ち。点滅中とよろけ中はその場で止まっている。 */
        delay = (int)en_warm[k];
        if (en_stun[k]) {
            delay += (en_stun[k] - 1) * STUN_STEP + (int)en_stun_t[k];
        }

        /* **直線で来る道は、いま透明な敵だけ。** まだ透明でない敵に
           「あと en_morph で透明になって直線で来る」を足すと成績が落ちた。 */
        gwait = en_ghost[k] ? 0 : FC_FAR;
        gstep = cross_frames(SPD_GHOST);

        /* 坑道を歩いて来る道。透明化中の敵は盤面を歩いていない。 */
        walk = en_ghost[k] ? -1 : cross_frames(kind_spd[en_kind[k]]);
        if (walk > 0) { hops_from_enemy(k, en_hop[k]); }

        for (i = 0; i < NCELL; i++) {
            int dc2 = (i % COLS) - ec, dr2 = (i / COLS) - er, t;
            if (dc2 < 0) { dc2 = -dc2; }
            if (dr2 < 0) { dr2 = -dr2; }
            /* 斜めに進めるので、直線では長い辺がそのまま升数になる */
            t = (gwait >= FC_FAR) ? FC_FAR
                : gwait + (dc2 > dr2 ? dc2 : dr2) * gstep;
            if (walk > 0 && en_hop[k][i] >= 0) {
                int wt = (int)en_hop[k][i] * walk;
                if (wt < t) { t = wt; }
            }
            t += delay;
            if (t < threat[i]) { threat[i] = t; }
        }
    }
}

/* ------------------------------------------------------------------ *
 * ボット
 * ------------------------------------------------------------------ */
/* 貪欲ボット。
   狙いは「面が理不尽でないか」を測ることなので、上手く遊ぶ必要はないが、
   敵に一直線に突っ込んで即死すると面の評価にならない。最低限の回避だけ入れる。

   1. 体温が7割を超えたら風呂を最優先
   2. そうでなければ一番近い黒ダイヤ (割れていれば升そのもの、未割りなら隣) へ
   3. 進みたい方向が危険 (敵が SAFE_STEPS 歩以内) なら、一番安全な方向へ逃げる */
static int safe_steps = 4;   /* --safe で変えられる */
#define SAFE_STEPS safe_steps

static int first_step_to(int goal, int *tc, int *tr)
{
    static const int dc[4] = { 0, -1, 0, 1 };
    static const int dr[4] = { -1, 0, 1, 0 };
    int cur = goal, k;
    if (bfs_d[goal] < 0) { return 0; }
    while (bfs_d[cur] > 1) {
        int c = cur % COLS, r = cur / COLS, nxt = -1;
        for (k = 0; k < 4; k++) {
            int nc = c + dc[k], nr = r + dr[k];
            if (nc < 0 || nc >= COLS || nr < 0 || nr >= ROWS) { continue; }
            if (!passable(nc, nr) || !linked(c, r, nc, nr)) { continue; }
            if (bfs_d[nr * COLS + nc] == bfs_d[cur] - 1) { nxt = nr * COLS + nc; break; }
        }
        if (nxt < 0) { return 0; }
        cur = nxt;
    }
    *tc = cur % COLS; *tr = cur / COLS;
    return 1;
}

/* 自機がその方向へ動けるか (黒ダイヤと岩は通れない。土は掘って進める) */
static int can_step(int c, int r)
{
    uint16_t i;
    if (c < 0 || c >= COLS || r < 0 || r >= ROWS) { return 0; }
    i = (uint16_t)r * COLS + c;
    if (coal[i] && coal[i] != COAL_CRACKED) { return 0; }
    if (rock_at((int8_t)c, (int8_t)r) >= 0) { return 0; }
    return 1;
}

/* 升の途中では決め直さない。毎フレーム考え直すと、升をまたぐたびに評価が
   ひっくり返って左右にがたつき、その場で足踏みしたまま敵に轢かれる。
   人間も升の境目でしか方向を決めないので、そこに合わせる。 */
static uint8_t bot_greedy_decide(void);

static uint8_t bot_greedy(void)
{
    static uint8_t held;
    if ((px & 15) || (py & 15)) {
        return held;                  /* 升の途中。前の判断を押し続ける */
    }
    held = bot_greedy_decide();
    return held;
}

static uint8_t bot_greedy_decide(void)
{
    static const int dc[4] = { 0, -1, 0, 1 };
    static const int dr[4] = { -1, 0, 1, 0 };
    static const uint8_t stick[4] = { 1, 7, 5, 3 };   /* 上 左 下 右 */
    int pc = px >> 4, pr = py >> 4;
    int best = -1, bestd = 9999, i, k;
    int face_coal = -1, tc = pc, tr = pr, want = -1;
    /* **風呂は「行く」だけでなく「浸かり続ける」。**
       以前は着いた時点で目標が消え、次の手を探して**すぐ出ていた**ので、
       体温が下がりきらず面3以降は16回中16回オーバーヒートで死んでいた。
       **冷めるまで入力を止める** (0 = 動かない = その場で浸かる)。 */
    /* **早めに切り上げる。** 半分 (120) で向かうと、掘りながらの移動では
       間に合わずに風呂の手前で茹だっていた (面3 は16回中16回)。1/3 で立つ。 */
    int hot = (bath_on && temp > (TEMP_MAX / 3));

    if (bath_on && pc == bath_c && pr == bath_r && temp > (TEMP_MAX / 5)) {
        return 0;
    }

    bfs(pc, pr);
    update_enemy_dist();

    /* 1. 隣が未割りの黒ダイヤなら、まずそれを叩く。常に前進になる。
          (体温が危ないときだけ後回しにして風呂へ向かう) */
    if (!hot) {
        for (k = 0; k < 4; k++) {
            int nc = pc + dc[k], nr = pr + dr[k];
            if (nc < 0 || nc >= COLS || nr < 0 || nr >= ROWS) { continue; }
            if (cell_coal((int8_t)nc, (int8_t)nr)) { return stick[k]; }
        }
    }

    /* 2. 目標を選ぶ。風呂が要るなら風呂、でなければ一番近い黒ダイヤ。 */
    if (hot) {
        if (bfs_d[bath_r * COLS + bath_c] >= 0) { best = bath_r * COLS + bath_c; }
    } else {
        for (i = 0; i < NCELL; i++) {
            if (!coal[i]) { continue; }
            if (coal[i] == COAL_CRACKED) {
                if (bfs_d[i] >= 0 && bfs_d[i] < bestd) { bestd = bfs_d[i]; best = i; face_coal = -1; }
            } else {
                int c = i % COLS, r = i / COLS;
                for (k = 0; k < 4; k++) {
                    int nc = c + dc[k], nr = r + dr[k], nb;
                    if (nc < 0 || nc >= COLS || nr < 0 || nr >= ROWS) { continue; }
                    nb = nr * COLS + nc;
                    if (bfs_d[nb] >= 0 && bfs_d[nb] < bestd) { bestd = bfs_d[nb]; best = nb; face_coal = i; }
                }
            }
        }
    }

    if (best >= 0 && bfs_d[best] == 0) {
        if (face_coal >= 0) {
            for (k = 0; k < 4; k++) {
                if (pc + dc[k] == face_coal % COLS && pr + dr[k] == face_coal / COLS) { return stick[k]; }
            }
        }
        best = -1;                       /* もう着いている。次の手を探す */
    }
    if (best >= 0 && first_step_to(best, &tc, &tr)) {
        for (k = 0; k < 4; k++) {
            if (pc + dc[k] == tc && pr + dr[k] == tr) { want = k; break; }
        }
    }

    /* 3. 通路が届いていないので土を掘る。目標の方角へ、進める向きを選ぶ。
          **体温が危ないときの目標は風呂。** 以前はここが黒ダイヤ決め打ちで、
          風呂へ行きたいのに黒ダイヤの方へ掘っていた。 */
    if (want < 0) {
        int gc = -1, gr = -1;
        bestd = 9999;
        if (hot) {
            gc = bath_c; gr = bath_r;
        } else {
            for (i = 0; i < NCELL; i++) {
                int d;
                if (!coal[i]) { continue; }
                d = abs((i % COLS) - pc) + abs((i / COLS) - pr);
                if (d < bestd) { bestd = d; gc = i % COLS; gr = i / COLS; }
            }
        }
        if (gc >= 0) {
            int cand[4], n = 0;
            if (gr > pr) { cand[n++] = 2; } else if (gr < pr) { cand[n++] = 0; }
            if (gc > pc) { cand[n++] = 3; } else if (gc < pc) { cand[n++] = 1; }
            cand[n++] = 2; cand[n++] = 3;          /* 行き詰まったときの逃げ道 */
            for (k = 0; k < n; k++) {
                if (can_step(pc + dc[cand[k]], pr + dr[cand[k]])) { want = cand[k]; break; }
            }
        }
    }

    /* 4. 進みたい方向が危険なら、一番安全な方向へ逃げる。 */
    if (want >= 0 && danger_at(pc + dc[want], pr + dr[want]) > SAFE_STEPS) {
        return stick[want];
    }
    {
        int bestk = -1, bestsafe = danger_at(pc, pr);
        for (k = 0; k < 4; k++) {
            int nc = pc + dc[k], nr = pr + dr[k], d;
            if (!can_step(nc, nr)) { continue; }
            d = danger_at(nc, nr);
            if (d > bestsafe) { bestsafe = d; bestk = k; }
        }
        if (bestk >= 0) { return stick[bestk]; }
    }
    return want >= 0 ? stick[want] : 0;
}


/* ------------------------------------------------------------------ *
 * 賢いボット (2026-10-26)
 *
 * **貪欲ボットは次の一升しか見ない。** 風呂へ向かう判断が遅れ、途中で蛇行し、
 * 面3以降はほとんどクリアできない (240回中16回)。
 *
 * ここでは**掘る時間込みの最短路**を毎升引き直してから歩く (240回中70回)。
 *
 *   升の重み (`bot_cost`) は**すべて「何升ぶん遠回りしてよいか」の尺度**。
 *   基準は 掘り済み16 / 土24 で、黒ダイヤは 耐久 x PICK_FRAMES。
 *   熱水・岩・鉱車のレール・敵の近さを、この尺度の罰則として足す。
 *   ガスの升は通らない (裏面。入ると引火して死ぬ)。
 *
 *   **風呂は到着時の体温の見積もりで立つ** (`bot_h` を経路に沿って足す)。
 *   風呂に入ったら冷めるまで出ない (0 を返して動かない)。
 *
 *   **敵は経路の重みで迂回する。** 最後の一歩で避ける作りにすると、
 *   逃げる→寄られる→戻る の往復になってその場で茹だる。
 *   隣まで来られたら離れ、逃げ場が無いときだけ振ってよろけさせる。
 * ------------------------------------------------------------------ */
#define BOT_INF (1 << 28)
/* 風呂へ立つときの余裕 (体温)。敵をよけた遠回りと、風呂の升へ入るまでの数歩ぶん。 */
#define BOT_SLACK 24
/* **敵を避ける半径はボット専用に持つ** (`FC_HORIZON`)。`--safe` は採点側
   (`metrics.py`) の「ヒヤリ」のしきい値で、そちらを 2 にするとボットまで
   無警戒になってしまう。 */

static const int bot_dc[4] = { 0, -1, 0, 1 };
static const int bot_dr[4] = { -1, 0, 1, 0 };
static int bot_d[NCELL], bot_from[NCELL];
/* **ボットが「いまつるはしを振りたい」と言うための旗。**
   黒ダイヤを割るためだけでなく、**寄ってきた敵をよろけさせる**ために振る
   (`PICK_REACH` = 2升届く)。これが無かったときは、どのボットも敵に触れて死ぬのが
   いちばん多い死因だった (面2・3・6・8〜15 で16回中14〜16回。2026-10-26 実測)。 */
static uint8_t bot_want_pick;
/* **その升へ着いたときに体温がどれだけ上がっているか** (経路に沿って足した見積もり)。
   `bot_d` と一緒に伝播させる。これが無かったときは、つるはしの熱
   (`HEAT_PER_SWING` = 1振り4) が丸ごと抜けていて、風呂へ立つ判断が手遅れになり、
   全面で16回中16回オーバーヒートで死んだ (2026-10-26 実測)。 */
static int bot_h[NCELL];

/* その升を通るあいだに上がる体温。 */
static int bot_heat(int j)
{
    int c = j % COLS, r = j / COLS;
    if (coal[j] && coal[j] != COAL_CRACKED) {
        /* 耐久度のぶん振る。1振り = HEAT_PER_SWING + 振り切る時間ぶん。 */
        return (int)coal[j] * (HEAT_PER_SWING + PICK_FRAMES / TEMP_TICK)
               + 16 / TEMP_TICK;
    }
    if (cell_water((int8_t)c, (int8_t)r)) {
        return 16 * WATER_HEAT;          /* 熱水は浸かっている間ずっと上がる */
    }
    return 16 / TEMP_TICK;               /* 歩くだけ (16フレームで1) */
}

/*
 * その升へ入るのに払う額。**罰則は升1つぶん (16〜24) の尺度に揃えてある。**
 * 以前は 200〜1400 を足していて、実費を完全に押し潰し、経路が「敵から最も遠い道」に
 * なって延々と遠回りし、面3以降は16回中16回オーバーヒートで死んだ (実測)。
 * 罰則を足すときは「何升ぶん遠回りしてよいか」で考えること。
 */
static int bot_cost(int j)
{
    int c = j % COLS, r = j / COLS, w;
    if (gas_n && gas_at((int8_t)c, (int8_t)r)) {
        return BOT_INF / 4;                       /* ガスは通らない (入ると死ぬ) */
    }
    if (coal[j] && coal[j] != COAL_CRACKED) {
        /* **黒ダイヤには下の罰則を足さない。** 「岩の真下や敵の横の黒ダイヤは高い」
           と正直に値付けしたら、240回中のクリアが 70 から 44 へ落ちた。
           黒ダイヤは目的そのもので、そこを高くすると目的を避けて歩き回り、
           かえって敵に当たる。**危ない場所を掘るのがこのゲームなので、
           そこは割り引かない。** (2026-10-26 実測) */
        return (int)coal[j] * PICK_FRAMES + 24;   /* 割ってから入る */
    }
    w = cell_dug((int8_t)c, (int8_t)r) ? 16 : 24;
    if (cell_water((int8_t)c, (int8_t)r)) {
        w += 96;                                  /* 熱水 = 4升ぶん遠回りしてよい */
    }
    /* **鉱車のレール行に長居しない。** 鉱車は押し出してくるので、行き止まりへ
       押し込まれると潰される (`src/cart.c`)。 */
    if (cart_on && r == cart_r) {
        w += cart_at((int8_t)c, (int8_t)r) ? 400 : 64;
    }
    /* **岩の升と岩の真下は高くつく。** 押すと時間がかかり、落ちてくれば潰される。
       素の最短路で突っ込ませたら、面1 でも16回中16回潰されて死んだ (実測)。 */
    if (rock_at((int8_t)c, (int8_t)r) >= 0) {
        w += 48;
    }
    if (r > 0 && rock_at((int8_t)c, (int8_t)(r - 1)) >= 0) {
        w += 96;
    }
    /* **敵は「最後の一歩で避ける」のでは間に合わない。**
       道を塞がれると、安全な隣へ逃げる→また寄られる→元の升へ戻る、を繰り返して
       その場で往復し、茹だって死んだ (面1、seed 0。(8,0) と (8,1) の間で 56 回)。
       **重みに入れておけば Dijkstra が最初から迂回する** ので往復が起きない。
       早く来られる升ほど高くつけるので、遠い敵のそばは通ってよい。
       **尺度が合うのは偶然ではない。** 自機は SPD_TUNNEL == SPD_UNIT なので
       1升 16px を 16 フレームで渡る = 掘り済みの升の重み 16 とちょうど同じ。
       つまりこの重みは「フレーム」と読み替えられる。 */
    {
        int t = threat[j];
        if (t < FC_HORIZON) { w += (FC_HORIZON - t) / FC_DIV; }
    }
    return w;
}

static void bot_dijkstra(int sc, int sr)
{
    static uint8_t done[NCELL];
    int k, n;
    for (k = 0; k < NCELL; k++) {
        bot_d[k] = BOT_INF; bot_h[k] = BOT_INF; bot_from[k] = -1; done[k] = 0;
    }
    bot_d[sr * COLS + sc] = 0;
    bot_h[sr * COLS + sc] = 0;
    for (n = 0; n < NCELL; n++) {
        int bestk = -1, bestv = BOT_INF, c, r;
        for (k = 0; k < NCELL; k++) {
            if (!done[k] && bot_d[k] < bestv) { bestv = bot_d[k]; bestk = k; }
        }
        if (bestk < 0) { break; }
        done[bestk] = 1;
        c = bestk % COLS; r = bestk / COLS;
        for (k = 0; k < 4; k++) {
            int nc = c + bot_dc[k], nr = r + bot_dr[k], j, w;
            if (nc < 0 || nc >= COLS || nr < 0 || nr >= ROWS) { continue; }
            j = nr * COLS + nc;
            w = bot_cost(j);
            if (bot_d[bestk] + w < bot_d[j]) {
                bot_d[j] = bot_d[bestk] + w;
                bot_h[j] = bot_h[bestk] + bot_heat(j);
                bot_from[j] = bestk;
            }
        }
    }
}

/* 目標までの道の「最初の一升」を返す。戻り値 <0 なら道が無い。 */
static int bot_first_step(int to, int from_cell)
{
    int prev = -1;
    if (to < 0 || bot_d[to] >= BOT_INF) { return -1; }
    while (to >= 0 && to != from_cell) { prev = to; to = bot_from[to]; }
    return (to == from_cell) ? prev : -1;
}

/* ------------------------------------------------------------------ *
 * ダイナマイト (2026-10-29)
 *
 * **投げる条件は「当たるか」だけでなく「ストックを払う価値があるか」。**
 * 弾は火薬を拾うぶんしか無く、開始は0。ただし**爆風で倒した敵は半々で火薬を
 * 落とす** (`enemy_kill_at` の by_rock=0) ので、当て続けるかぎり枯れにくい。
 *
 * 飛び方は `src/bomb.c` のとおり:
 *   - 向いている方へ最大 `BOMB_RANGE` 升。掘り済みで黒ダイヤ・岩・風呂が
 *     無い升しか抜けられない (**熱水は抜ける**)。
 *   - 最初に当たった敵の升で爆発し、**その升の敵を全部倒す**。
 *   - 爆心から1升以内のガスに引火する。**自機がそのガスの1升以内に居ると死ぬ。**
 *
 * 投げるには**その方向を向く**必要がある (`main.c` は `player_dir()` を渡す)。
 * ボットは投げたい向きへスティックを倒して返し、harness がその後で投げる。
 * 人がやることと同じで、1フレームぶん自機もそちらへ動く。
 *
 * **溜め込まずに、届いたら撃つ。** 「危ないときだけ投げる」を入れると 720回中の
 * クリアが 319 から 283 へ落ちた (2026-10-29 実測)。倒した敵は `lv_rest()` のあいだ
 * 洞窟へ引っ込むうえ、**半々で火薬を落として弾が戻ってくる**ので、
 * 温存する理由がない。狙う距離を 2〜4升に絞っても変わらなかった。
 * ------------------------------------------------------------------ */
static uint8_t bot_bomb_dir;        /* DIR_NONE なら投げない */

/* `bomb.c` の flyable() と同じ条件。 */
static int bomb_flyable(int c, int r)
{
    if (c < 0 || c >= COLS || r < 0 || r >= ROWS) { return 0; }
    if (!cell_dug((int8_t)c, (int8_t)r)) { return 0; }
    if (cell_coal((int8_t)c, (int8_t)r)) { return 0; }
    if (rock_at((int8_t)c, (int8_t)r) >= 0) { return 0; }
    if (bath_on && c == bath_c && r == bath_r) { return 0; }
    return 1;
}

/* その升で爆発させたとき、自分がガスの巻き添えになるか (`gas_ignite`)。 */
static int bomb_gas_risk(int bc, int br, int pc, int pr)
{
    int i;
    for (i = 0; i < gas_n; i++) {
        int gc = gas_c[i], gr = gas_r[i];
        if (abs(bc - gc) + abs(br - gr) > 1) { continue; }   /* 引火しない */
        if (!gas_live((uint8_t)i)) { continue; }
        if (abs(pc - gc) + abs(pr - gr) <= 1) { return 1; }  /* 自分も巻き添え */
    }
    return 0;
}

/* その升に重なっている敵の数。多いほど1発の値打ちが上がる。 */
static int bomb_targets(int c, int r)
{
    int k, n = 0;
    if (!enemy_at((int8_t)c, (int8_t)r)) { return 0; }   /* 当たるかは本家で見る */
    for (k = 0; k < ENEMY_MAX; k++) {
        if (en_state[k] != 1) { continue; }
        if (abs((int)en_x[k] - (c << 4)) < 12
            && abs((int)en_y[k] - (r << 4)) < 12) { n++; }
    }
    return n ? n : 1;
}

/* 投げる向きを決める。DIR_NONE なら投げない。 */
static uint8_t bomb_choose(int pc, int pr)
{
    static const uint8_t dir_of[4] = { DIR_UP, DIR_LEFT, DIR_DOWN, DIR_RIGHT };
    int k, best = -1, bestn = 0, bestd = BOMB_RANGE + 1;

    if (!stock || bomb_state) { return DIR_NONE; }
    for (k = 0; k < 4; k++) {
        int c = pc, r = pr, step;
        for (step = 1; step <= BOMB_RANGE; step++) {
            int n;
            c += bot_dc[k]; r += bot_dr[k];
            n = bomb_targets(c, r);
            if (n) {
                if (bomb_gas_risk(c, r, pc, pr)) { break; }
                /* まとめて倒せるほうを選び、同数なら近いほう */
                if (n > bestn || (n == bestn && step < bestd)) {
                    bestn = n; bestd = step; best = k;
                }
                break;
            }
            if (!bomb_flyable(c, r)) { break; }   /* 土・黒ダイヤ・岩・風呂で止まる */
        }
    }
    return (best < 0) ? DIR_NONE : dir_of[best];
}

static uint8_t bot_smart(void)
{
    static const uint8_t stick[4] = { 1, 7, 5, 3 };   /* 上 左 下 右 */
    static uint8_t held;
    int pc, pr, me, i, k, step, bathcell = -1, target = -1, bestd = BOT_INF;
    int arrive_temp;

    /* **ダイナマイトが最優先。** 届くなら、寄られる前に片付けるほうが安い。
       **升の途中では投げない。** 人のように「狙いが通った瞬間に押す」を
       許すと、向きを変えるたびに歩みが寄せ直されて経路が崩れ、
       720回中のクリアが 319 から 303 へ落ちた (2026-10-29 実測)。 */
    bot_bomb_dir = ((px & 15) || (py & 15)) ? DIR_NONE
                   : bomb_choose(px >> 4, py >> 4);
    if (bot_bomb_dir != DIR_NONE) {
        held = stick[bot_bomb_dir == DIR_UP ? 0
                     : bot_bomb_dir == DIR_LEFT ? 1
                     : bot_bomb_dir == DIR_DOWN ? 2 : 3];
        return held;                                  /* その向きを向いてから投げる */
    }
    if ((px & 15) || (py & 15)) { return held; }      /* 升の途中では決め直さない */
    pc = px >> 4; pr = py >> 4;
    me = pr * COLS + pc;

    /* **敵が隣まで来たら、逃げ道を探す。無ければ振ってよろけさせる。**
       間合い (`PICK_REACH` = 2升) から振らせたら全面で死んだ: 振っている
       `PICK_FRAMES` のあいだ動けないので、2升先の敵はそのまま寄って触ってくる。
       よろけさせるのは**逃げ場が無いときの最後の手**で、第一の答えは離れること。 */
    bot_want_pick = 0;
    {
        int ek = -1, j;
        for (k = 0; k < 4 && ek < 0; k++) {
            int nc = pc + bot_dc[k], nr = pr + bot_dr[k];
            for (j = 0; j < ENEMY_MAX; j++) {
                if (en_state[j] == 1
                    && (en_x[j] >> 4) == nc && (en_y[j] >> 4) == nr) { ek = k; break; }
            }
        }
        if (ek >= 0) {
            int bk = -1, bs = FC_PANIC;
            forecast();                  /* 逃げ先を先読みで選ぶ */
            for (j = 0; j < 4; j++) {
                int nc = pc + bot_dc[j], nr = pr + bot_dr[j], d;
                if (!can_step(nc, nr)) { continue; }
                d = threat[nr * COLS + nc];
                if (d > bs) { bs = d; bk = j; }
            }
            if (bk >= 0) { held = stick[bk]; return held; }
            bot_want_pick = 1;
            held = stick[ek];
            return held;
        }
    }

    /* 風呂に入っていて熱いなら、冷めるまで浸かる */
    if (bath_on && pc == bath_c && pr == bath_r && temp > (TEMP_MAX / 5)) {
        held = 0;
        return held;
    }

    update_enemy_dist();            /* 記録に出す treach / esc のため */
    forecast();                     /* **bot_cost が threat を見るので先に** */
    bot_dijkstra(pc, pr);

    if (bath_on) {
        bathcell = bath_r * COLS + bath_c;
        /* **着いたときの体温の見積もりで立つ。** 毎升で引き直すので、
           「いま向かえば間に合う」最後の瞬間まで黒ダイヤを掘れる。 */
        arrive_temp = temp + bot_h[bathcell] + BOT_SLACK;
        if (bot_d[bathcell] < BOT_INF && arrive_temp >= TEMP_MAX) {
            target = bathcell;
        }
    }
    if (target < 0) {                                  /* 一番「近い」黒ダイヤ */
        for (i = 0; i < NCELL; i++) {
            if (!coal[i]) { continue; }
            if (bot_d[i] < bestd) { bestd = bot_d[i]; target = i; }
        }
        /* **火薬をわざわざ拾いに行かせるのは損だった。** 弾切れのとき火薬を
           目標に加えると、720回中のクリアが 319 から 287〜312 へ落ちる
           (遠回りの量を 0〜256 まで振っても、どれも素のほうが良い。2026-10-29 実測)。
           掘る時間と体温を払って1発買うより、通りがかりに拾うぶんで足りる。 */
    }
    if (target < 0) { held = 0; return held; }

    step = bot_first_step(target, me);
    if (step < 0) { held = 0; return held; }

    for (k = 0; k < 4; k++) {
        if (pc + bot_dc[k] == step % COLS && pr + bot_dr[k] == step / COLS) {
            /* **引き返すのは、踏み込んだら即死のときだけ。** 迂回は経路の重みで
               済んでいるので、ここで毎升やり直すと往復に戻る。 */
            if (threat[step] < FC_PANIC && threat[me] >= FC_PANIC) {
                int bk = -1, bs = threat[me], j;
                for (j = 0; j < 4; j++) {
                    int nc = pc + bot_dc[j], nr = pr + bot_dr[j], d;
                    if (!can_step(nc, nr)) { continue; }
                    d = threat[nr * COLS + nc];
                    if (d > bs) { bs = d; bk = j; }
                }
                if (bk >= 0) { held = stick[bk]; return held; }
            }
            held = stick[k];
            return held;
        }
    }
    held = 0;
    return held;
}

/* 穂先の升に自機が重なっているか (死因の切り分け用)。
   **敵の体からの距離で近似してはいけない。** 槍は1升先を突くので、
   敵が升の端に居ると体からは 32px 離れていても当たる (それで取りこぼした)。
   `src/spear.c` の `player_ahead` と同じ見方にしてある。 */
static int spear_near(void)
{
    static const int dc[5] = { 0, 0, 0, -1, 1 };
    static const int dr[5] = { 0, -1, 1, 0, 0 };
    int k;
    for (k = 0; k < ENEMY_MAX; k++) {
        int d, tc, tr;
        if (en_state[k] != 1 || en_spear[k] <= SPEAR_REST) { continue; }
        d = en_dir[k];
        if (d < DIR_UP || d > DIR_RIGHT) { continue; }
        tc = (en_x[k] >> 4) + dc[d];
        tr = (en_y[k] >> 4) + dr[d];
        if (tc < 0 || tc >= COLS || tr < 0 || tr >= ROWS) { continue; }
        if (abs((int)px - (tc << 4)) < 12 && abs((int)py - (tr << 4)) < 12) {
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ *
 * 本体
 * ------------------------------------------------------------------ */
extern uint16_t en_morph[ENEMY_MAX];
extern uint8_t  en_ghost[ENEMY_MAX];

/*
 * 岩 i を d の向きへ k 升押し、黒ダイヤの真上で止まったら一手を打って採れた数を返す。
 * v=0 なら真下の黒ダイヤを割る、v>=1 なら v 本目の炭柱を割る。
 * 押し切れない / 途中で落ちた / 行き先の真下が黒ダイヤでない / 炭柱が無い なら -1。
 * *nb に岩が樋を通った回数を返す。
 */
/* 岩 i を d の向きへ k 升押す。黒ダイヤの真上で止まれば 1。盤面は組み直さない。 */
static int push_rock(int i, int d, int k)
{
    int rc, rr, tc, s, fr;
    if (!rock_state[i]) { return 0; }
    rc = rock_x[i] >> 4; rr = rock_y[i] >> 4; tc = rc + d * k;
    if (tc < 0 || tc >= COLS || rc - d < 0 || rc - d >= COLS) { return 0; }
    if (coal[rr * COLS + rc - d]) { return 0; }         /* 押し手が立てない */
    for (s = 0; s < 16 * k; s++) {
        if (!rock_push((int8_t)i, (int8_t)d, 1)) { return 0; }
        rock_update();
    }
    for (fr = 0; fr < 60; fr++) { rock_update(); }
    return rock_state[i] == ROCK_WAIT && rock_x[i] == tc * 16 && rock_y[i] == rr * 16
        && cell_coal((int8_t)tc, (int8_t)(rr + 1));
}

/* 一手打って、採れた数を返す。v=0 なら (hc,hr) の黒ダイヤ、v>=1 なら v 本目の炭柱を割る。 */
static int finish(int v, int hc, int hr, int coal0, int *nb)
{
    int s, fr, q;
    uint8_t ox[ROCK_MAX], oy[ROCK_MAX];
    if (v > 0) {
        hc = -1;
        for (q = 0; q < NCELL; q++) {
            if (cf_on(q, CF_PILLAR) && cell_coal((int8_t)(q % COLS), (int8_t)(q / COLS)) && --v == 0) {
                hc = q % COLS; hr = q / COLS; break;
            }
        }
        if (hc < 0) { return -1; }
    }
    while (cell_coal((int8_t)hc, (int8_t)hr)) { coal_hit((int8_t)hc, (int8_t)hr); }
    *nb = 0;
    for (s = 0; s < ROCK_MAX; s++) { ox[s] = rock_x[s]; oy[s] = rock_y[s]; }
    for (fr = 0; fr < 400; fr++) {
        rock_update();
        coal_shake_tick();
        for (s = 0; s < ROCK_MAX; s++) {
            if (rock_state[s] && rock_y[s] == oy[s] && rock_x[s] != ox[s]
                && (rock_x[s] & 15) == 0) { (*nb)++; }
            ox[s] = rock_x[s]; oy[s] = rock_y[s];
        }
    }
    return coal0 - coal_left;
}

static int push_try(int i, int d, int k, int v, int coal0, int *nb)
{
    int rc, rr, tc, hc = -1, hr = -1;
    build_stage();
    rc = rock_x[i] >> 4; rr = rock_y[i] >> 4; tc = rc + d * k;
    if (!push_rock(i, d, k)) { return -1; }
    if (v == 0) {
        hc = tc; hr = rr + 1;
    }
    return finish(v, hc, hr, coal0, nb);
}

/*
 * **鉱車の見返りを測る (--cart)。** 車止めから岩を落として、黒ダイヤを何個
 * 持っていけるかを数える。**実際に岩を落として物理を回す**ので、途中の土や
 * 別の岩で止まるぶんもそのまま出る。
 *   そのまま     : 面の開始の盤面のまま落とした場合 (下が土なら止まる)
 *   下を掘れば   : 車止めの下の列の土を掘ってから落とした場合 (プレイヤーの手入れ後)
 */
static int drop_from(int c, int r)
{
    /* **盤面にある岩を1つ借りて**、車止めの升から落とす (鉱車が運んでくるのと同じ)。
       新しく置こうとすると、岩の枠を使い切っている面で置けない。 */
    int i, f, before = coal_left, use = -1;
    for (i = 0; i < ROCK_MAX; i++) {
        if (rock_state[i] == ROCK_WAIT) { use = i; break; }
    }
    if (use < 0) { return -1; }
    rock_x[use] = (uint8_t)(c << 4);
    rock_y[use] = (uint8_t)(r << 4);
    rock_state[use] = ROCK_WAIT;
    rock_drop_now((uint8_t)use);
    for (f = 0; f < 200; f++) { rock_update(); coal_shake_tick(); }
    return before - coal_left;
}

/*
 * ---- par: 「邪魔されずに掘ったら何秒か」の見積もり (2026-10-13) ----
 *
 * **持ち時間の表を、見積もりではなく盤面から出すための指標。**
 * 出発点から、盤面の黒ダイヤ全部と水風呂を**近いものから順に**回り、
 * かかるフレーム数を数える。コストはゲームの実値そのもの。
 *
 *   未掘の土へ1升入る  24フレーム (SPD_DIG   40px/s)
 *   掘った升へ1升入る  16フレーム (SPD_TUNNEL 60px/s)
 *   黒ダイヤを1枚割る  耐久 x PICK_FRAMES (1振り18フレーム)
 *
 * **下限寄りの見積もり。** 敵に追われて引き返す時間、岩が落ちるのを待つ時間、
 * 二度目の風呂は入っていない。逆に、連鎖で岩がまとめて採ってくれるぶんも
 * 引いていない。「この面を何にも邪魔されずに片付けたら何秒か」の床として読む。
 *
 * 膜 (LINK_*) は見ない。自機は歩くだけで膜を破るので速度に関係しない
 * (src/player.c の chewing は cell_dug だけを見る)。岩は押せる想定で素通りさせる。
 *
 *   par   : 合計フレーム数
 *   dpar  : その道中で新しく掘る升の数
 *   bathd : 出発点から水風呂までのフレーム数 (風呂の遠さ = 体温の階段の軸)
 */
#define PAR_INF (1 << 28)

static uint8_t par_dug[NCELL];
static int par_d[NCELL], par_from[NCELL];
static int par_w_dug = 16, par_w_dirt = 24;

static const int par_dc[4] = { 0, -1, 0, 1 };
static const int par_dr[4] = { -1, 0, 1, 0 };

static void par_dijkstra(int sc, int sr)
{
    static uint8_t done[NCELL];
    int k, n;
    for (k = 0; k < NCELL; k++) { par_d[k] = PAR_INF; par_from[k] = -1; done[k] = 0; }
    par_d[sr * COLS + sc] = 0;
    for (n = 0; n < NCELL; n++) {
        int bestk = -1, bestv = PAR_INF, c, r;
        for (k = 0; k < NCELL; k++) {
            if (!done[k] && par_d[k] < bestv) { bestv = par_d[k]; bestk = k; }
        }
        if (bestk < 0) { break; }
        done[bestk] = 1;
        c = bestk % COLS; r = bestk / COLS;
        for (k = 0; k < 4; k++) {
            int nc = c + par_dc[k], nr = r + par_dr[k], j, w;
            if (nc < 0 || nc >= COLS || nr < 0 || nr >= ROWS) { continue; }
            j = nr * COLS + nc;
            /* 黒ダイヤは割るまで通れない (割った升は par_dug で印を付ける) */
            if (coal[j] && coal[j] != COAL_CRACKED && !par_dug[j]) { continue; }
            w = par_dug[j] ? par_w_dug : par_w_dirt;
            if (par_d[bestk] + w < par_d[j]) {
                par_d[j] = par_d[bestk] + w;
                par_from[j] = bestk;
            }
        }
    }
}

/* 通った道を掘った跡にする (次の行き先はそのぶん速くなる)。新しく掘った升の数を返す。 */
static int par_mark(int to)
{
    int n = 0;
    while (to >= 0) {
        if (!par_dug[to]) { par_dug[to] = 1; n++; }
        to = par_from[to];
    }
    return n;
}

static int par_route(int *dpar_out, int *bathd_out, int *left_out)
{
    uint8_t want[NCELL];
    int k, frames = 0, dug_cells = 0, cur, nleft = 0;

    for (k = 0; k < NCELL; k++) {
        par_dug[k] = (uint8_t)(cf_on(k, CF_DUG) ? 1 : 0);
        want[k] = 0;
    }
    for (k = 0; k < NCELL; k++) {
        if (coal[k] && coal[k] != COAL_CRACKED) { want[k] = 1; nleft++; }
    }
    if (bath_on) {
        k = bath_r * COLS + bath_c;
        if (!want[k]) { want[k] = 1; nleft++; }
    }
    cur = start_r * COLS + start_c;

    /* **風呂の遠さはフレームで測る。** 升の数ではない: 風呂は必ず掘ってある升に
       置かれる (`nearest_dug_away`) ので、「掘る升の数」で測ると全面0になって
       指標にならなかった (2026-10-13 の実測)。体温の階段に使えるのは
       「冷やしに行って戻るのに何秒かかるか」のほう。 */
    *bathd_out = -1;
    if (bath_on) {
        par_w_dug = 16; par_w_dirt = 24;
        par_dijkstra(start_c, start_r);
        k = bath_r * COLS + bath_c;
        if (par_d[k] < PAR_INF) { *bathd_out = par_d[k]; }
    }

    par_w_dug = 16; par_w_dirt = 24;
    while (nleft > 0) {
        int best = -1, bestcost = PAR_INF, target = -1;
        par_dijkstra(cur % COLS, cur / COLS);
        for (k = 0; k < NCELL; k++) {
            if (!want[k]) { continue; }
            if (coal[k] && coal[k] != COAL_CRACKED && !par_dug[k]) {
                int j;
                for (j = 0; j < 4; j++) {          /* 隣に立って耐久ぶん振り、1歩入る */
                    int nc = (k % COLS) + par_dc[j], nr = (k / COLS) + par_dr[j], nb, cost;
                    if (nc < 0 || nc >= COLS || nr < 0 || nr >= ROWS) { continue; }
                    nb = nr * COLS + nc;
                    if (par_d[nb] >= PAR_INF) { continue; }
                    cost = par_d[nb] + coal[k] * PICK_FRAMES + par_w_dug;
                    if (cost < bestcost) { bestcost = cost; best = nb; target = k; }
                }
            } else if (par_d[k] < bestcost) {
                bestcost = par_d[k]; best = k; target = k;
            }
        }
        if (best < 0) { break; }                   /* 残りへは届かない */
        dug_cells += par_mark(best);
        frames += bestcost;
        if (target != best && !par_dug[target]) { par_dug[target] = 1; dug_cells++; }
        want[target] = 0; nleft--;
        cur = target;
    }
    *dpar_out = dug_cells;
    *left_out = nleft;
    return frames;
}

/*
 * 盤面を**静的に**測る。ボットを走らせない。
 *
 * 貪欲ボットは面1すらクリアできないので、盤面の良し悪しの判定には使えない。
 * 代わりに「岩と炭層の位置関係」だけを見る。ここが遊びの手応えを決めている:
 *
 *   free  : 岩の真下（同じ列）にある黒ダイヤの数。落ちる岩は列を貫いて全部採るので、
 *           **これがそのまま「何もしなくても手に入る連鎖の長さ」**になる
 *   haul  : 一番長い列へ岩を寄せるのに要る横移動の升数。**岩の移動の面倒さ**
 *   colmax: 一番長い縦の炭層。一気の連鎖の上限
 *   veins : 繋がった炭層の塊の数。多いほど散らばっていて一気に採りにくい
 *   reach : **樋を通した1回の落下で採れる黒ダイヤの最大数。**
 *           掘る手間を無視した上限で、樋の置き方の良し悪しはこれで測る
 *   bend  : 実際に樋で曲がった岩の数。0 なら板が飾りになっている
 *   hand  : **つるはしを1本入れるだけで採れる最大数（掘らない）。**
 *           遊んだ感触に一番近い指標。reach と違って上限ではなく実際の取れ高
 *   hbend : **その最善手の中で岩が樋を通った回数。** 0 なら、一番おいしい手が
 *           板を一度も使っていない = 盤面が約束している連鎖が嘘になっている
 *   bump  : 落盤中、落ちてきた岩が**止まっていた岩に追突**した回数 (規則6)
 *   side  : 落盤中、**樋で横から入ってきた岩**に当たった回数 (相談書の (C))
 *   cavef : 落盤が終わるまでのフレーム数
 *   cbend : 落盤中に岩が樋を通った回数
 *   hpick : **炭柱以外を1枚割って、岩が樋を通って採れた数。**
 *           炭柱は盤面全体が動くので原因が追えない。普通の黒ダイヤ1枚なら
 *           「この岩が落ちて、この板で曲がって、ここを採る」の一本の線になる
 */
static void analyse(int st, unsigned sd)
{
    int col[COLS], c, r, i, best = 0, bestc = 0, free_max = 0, haul = 99, nrock = 0;
    int best_combo = 0, coal0 = 0;
    int taken = 0, crack = 0, chutes = 0, reach = 0, bend = 0, hand = 0, hbend = 0, hpick = 0;
    int bump = 0, side = 0, cavef = 0, cbend = 0, nfall = 0, lost = 0, maxline = 0;
    int noff = 0, npush = 0, hpush = 0, hpbend = 0, rowdup = 0;
    int hprep = 0, hprepbend = 0, cbal = 0, rbal = 0, hpick0 = 0, wet = 0, wseal = 0;
    int pool = 0, rail = 0, railrock = 0, railpay = 0;
    int par = 0, dpar = 0, bathd = -1, parleft = 0;
    int gapn = 0, digpay = 0, digmin = 99, digcells = 0;
    int bi_d[ROCK_MAX], bi_k[ROCK_MAX], bi_got[ROCK_MAX];
    int seen[NCELL], veins = 0, vmax = 0;
    for (c = 0; c < COLS; c++) { col[c] = 0; }
    for (r = 0; r < ROWS; r++) {
        for (c = 0; c < COLS; c++) {
            if (coal[r * COLS + c]) { col[c]++; }
        }
    }
    for (c = 0; c < COLS; c++) { if (col[c] > best) { best = col[c]; bestc = c; } }

    for (i = 0; i < ROCK_MAX; i++) {
        int rc, rr, n = 0, d;
        if (rock_state[i] == 0) { continue; }
        nrock++;
        rc = rock_x[i] >> 4; rr = rock_y[i] >> 4;
        for (r = rr + 1; r < ROWS; r++) { if (coal[r * COLS + rc]) { n++; } }
        if (n > free_max) { free_max = n; }
        d = rc - bestc; if (d < 0) { d = -d; }
        if (d < haul) { haul = d; }
    }
    if (haul == 99) { haul = 0; }
    for (i = 0; i < NCELL; i++) { if (chute[i]) { chutes++; } }

    /* 繋がった炭層の塊を数える (4近傍) */
    for (i = 0; i < NCELL; i++) { seen[i] = 0; }
    for (i = 0; i < NCELL; i++) {
        int stack[NCELL], sp = 0, n = 0;
        if (!coal[i] || seen[i]) { continue; }
        stack[sp++] = i; seen[i] = 1;
        while (sp) {
            int q = stack[--sp], qc = q % COLS, qr = q / COLS, k;
            const int dc[4] = { -1, 1, 0, 0 }, dr[4] = { 0, 0, -1, 1 };
            n++;
            for (k = 0; k < 4; k++) {
                int nc = qc + dc[k], nr = qr + dr[k], ni;
                if (nc < 0 || nc >= COLS || nr < 0 || nr >= ROWS) { continue; }
                ni = nr * COLS + nc;
                if (coal[ni] && !seen[ni]) { seen[ni] = 1; stack[sp++] = ni; }
            }
        }
        veins++;
        if (n > vmax) { vmax = n; }
    }

    coal0 = coal_left;      /* 盤面を動かす前に控える */

    /* **落盤を実際に起こす。** 静的には数えられない。
       沈み込みブロック数は廃止したので、炭柱はどれを壊しても結果が同じになった
       (真下の升を抜いて全部の岩が落ちるだけ)。試すのは1回でよい。 */
    {
        int fr, k, cr = 0, last = 0;
        uint8_t was[ROCK_MAX], ox[ROCK_MAX], oy[ROCK_MAX], side_t[ROCK_MAX];
        build_stage();
        rock_collapse();
        for (k = 0; k < ROCK_MAX; k++) {
            was[k] = rock_state[k]; ox[k] = rock_x[k]; oy[k] = rock_y[k]; side_t[k] = 0;
            if (rock_state[k] == 2) { nfall++; }   /* ROCK_JIG = 落盤で落ちる岩 */
        }
        for (fr = 0; fr < 500; fr++) {
            static uint8_t pre[NCELL];
            if (combo > best_combo) { best_combo = combo; }
            /* **板の状態をこのフレームの前に控える。** 自分が壊した板を
               「もう壊れていた」と数えないため。 */
            for (k = 0; k < NCELL; k++) { pre[k] = chute[k]; }
            rock_update();
            coal_shake_tick();
            for (k = 0; k < ROCK_MAX; k++) {
                /* **板を取り損ねた瞬間。** 落下中の岩が升の境目に着いたとき、
                   そこの板がこのフレームより前から折れていたら「取り損ね」。
                   作者が「優先順位がわからない」と言った現象そのもの。 */
                if ((rock_state[k] == 3 || rock_state[k] == 4)
                    && (rock_y[k] & 15) == 0 && rock_y[k] != oy[k]) {
                    int ci = (rock_y[k] >> 4) * COLS + ((rock_x[k] + 8) >> 4);
                    if (pre[ci] == CHUTE_BR || pre[ci] == CHUTE_BL) { lost++; }
                }
                if (side_t[k]) { side_t[k]--; }
                /* 樋で列を移った直後の岩を覚えておく (この岩に当たったら (C)) */
                if (rock_state[k] && rock_y[k] == oy[k] && rock_x[k] != ox[k]
                    && (rock_x[k] & 15) == 0) { side_t[k] = 24; cbend++; }
                /* **落下中の岩が割れた = 何かに着地した。** 真下に岩が居れば接触。
                   その岩が直前に樋で横から入ってきたなら (C)、そうでなければ追突 (A)。 */
                if (was[k] == ROCK_FALL && rock_state[k] == ROCK_SPLIT) {
                    int c2 = (rock_x[k] + 8) >> 4, r2 = rock_y[k] >> 4;
                    int b = (r2 + 1 < ROWS) ? rock_at((int8_t)c2, (int8_t)(r2 + 1)) : -1;
                    if (b >= 0) { if (side_t[b]) { side++; } else { bump++; } }
                }
                was[k] = rock_state[k]; ox[k] = rock_x[k]; oy[k] = rock_y[k];
            }
            if (rock_busy()) { last = fr; }
            /* **同じ走査線に岩が何枚かかるか** (横一列8枚制限の確認)。
               スプライトは16px高なので、y の差が16未満なら重なる。 */
            for (k = 0; k < ROCK_MAX; k++) {
                int m = 0, k2;
                if (!rock_state[k] || rock_state[k] == 4) { continue; }
                for (k2 = 0; k2 < ROCK_MAX; k2++) {
                    int d;
                    if (!rock_state[k2] || rock_state[k2] == 4) { continue; }
                    d = (int)rock_y[k2] - (int)rock_y[k];
                    if (d < 0) { d = -d; }
                    if (d < 16) { m++; }
                }
                if (m > maxline) { maxline = m; }
            }
        }
        cavef = last;
        taken = coal0 - coal_left;
        for (k = 0; k < NCELL; k++) { if (coal[k] == COAL_CRACKED) { cr++; } }
        crack = cr;
    }

    /*
     * **reach: 樋を通した1回の落下で採れる黒ダイヤの最大数。**
     *
     * これが樋の面を測る唯一の指標。静的には数えられない (板の向き・曲がった先が
     * 掘れるか・その列に炭があるか、が全部絡む) ので、実際に落として数える。
     *
     * 測り方は「プレイヤーが掘りたいだけ掘れたら」の上限。**炭以外の升を全部
     * 掘ってから、岩を1つだけ残して落とす。** 掘る手間は別の指標 (haul) が持つ。
     */
    for (i = 0; i < ROCK_MAX; i++) {
        uint8_t ox[ROCK_MAX], oy[ROCK_MAX];
        int fr, k, got, bn = 0;
        build_stage();
        if (rock_state[i] == 0) { continue; }
        for (k = 0; k < ROCK_MAX; k++) { if (k != i) { rock_state[k] = 0; } }
        for (k = 0; k < NCELL; k++) { if (!coal[k]) { cf_set(k, CF_DUG); } }
        for (k = 0; k < NCELL; k++) {
            int kc = k % COLS, kr = k / COLS;
            if (!cf_on(k, CF_DUG)) { continue; }
            if (kc + 1 < COLS && cf_on(k + 1, CF_DUG))    { cf_set(k, LINK_RIGHT); }
            if (kr + 1 < ROWS && cf_on(k + COLS, CF_DUG)) { cf_set(k, LINK_DOWN); }
        }
        /* **真下が黒ダイヤなら1枚だけ割っておく。** 黒ダイヤは静止した岩の支えなので、
           割らないと岩は一歩も動かない。これがゲーム側で言う「1枚ぶんの手間」。 */
        {
            int rc = (rock_x[i] + 8) >> 4, rr = (rock_y[i] >> 4);
            if (rr + 1 < ROWS && coal[(rr + 1) * COLS + rc]) {
                coal_crack_by_rock((int8_t)rc, (int8_t)(rr + 1));
            }
        }
        ox[i] = rock_x[i]; oy[i] = rock_y[i];
        for (fr = 0; fr < 500; fr++) {
            rock_update();
            coal_shake_tick();
            /* 行が変わらないまま横へ動いたら、樋で曲がった瞬間。 */
            if (rock_y[i] == oy[i] && rock_x[i] != ox[i]) { bn = 1; }
            ox[i] = rock_x[i]; oy[i] = rock_y[i];
        }
        got = coal0 - coal_left;
        if (got > reach) { reach = got; }
        if (bn) { bend++; }
    }

    /*
     * **hand: つるはしを1本入れるだけで採れる最大数。掘らない。**
     *
     * `reach` は「炭以外の升を全部掘ったら」の上限なので、**遊んだときに実際に
     * 起きることを測っていない。** 盤面が「岩→炭層→樋→炭柱」と並んでいても、
     * 途中が土で塞がっていれば岩は止まる（作者の指摘で発覚、2026-09-12）。
     *
     * こちらは**盤面をそのまま使い、黒ダイヤ1枚だけを耐久ぶん叩いて**engine を回す。
     * 盤面上の全部の黒ダイヤを順に試して一番良い結果を採る（どれを叩くかは
     * プレイヤーが選べるため）。炭柱を叩けば落盤もそこに含まれる。
     */
    {
        int idx[NCELL], n = 0, q;
        build_stage();                  /* 前の実験で盤面が荒れているので組み直す */
        for (i = 0; i < NCELL; i++) {
            if (coal[i] && coal[i] != COAL_CRACKED) { idx[n++] = i; }
        }
        /* **支柱を折るのも「つるはし1本」。** 仕込んだ鉱脈の岩は支柱で止めてあるので、
           黒ダイヤを割る手だけを数えると最善手を見落とす (2026-10-20)。 */
        for (q = 0; q < ROCK_MAX; q++) {
            int fr, got, k, nb = 0;
            uint8_t ox[ROCK_MAX], oy[ROCK_MAX];
            build_stage();
            if (!rock_prop[q]) { continue; }
            rock_prop[q] = 0;
            for (k = 0; k < ROCK_MAX; k++) { ox[k] = rock_x[k]; oy[k] = rock_y[k]; }
            for (fr = 0; fr < 400; fr++) {
                rock_update();
                coal_shake_tick();
                for (k = 0; k < ROCK_MAX; k++) {
                    if (rock_state[k] && rock_y[k] == oy[k] && rock_x[k] != ox[k]
                        && (rock_x[k] & 15) == 0) { nb++; }
                    ox[k] = rock_x[k]; oy[k] = rock_y[k];
                }
            }
            got = coal0 - coal_left;
            if (got > hand || (got == hand && nb > hbend)) {
                if (got > hand) { hand = got; hbend = nb; } else { hbend = nb; }
            }
            if (got > hpick0) { hpick0 = got; }
        }
        build_stage();
        for (q = 0; q < n; q++) {
            int hc = idx[q] % COLS, hr = idx[q] / COLS, fr, got, k, nb = 0, isp;
            uint8_t ox[ROCK_MAX], oy[ROCK_MAX];
            build_stage();
            isp = cf_on(idx[q], CF_PILLAR) ? 1 : 0;
            while (cell_coal((int8_t)hc, (int8_t)hr)) { coal_hit((int8_t)hc, (int8_t)hr); }
            for (k = 0; k < ROCK_MAX; k++) { ox[k] = rock_x[k]; oy[k] = rock_y[k]; }
            for (fr = 0; fr < 400; fr++) {
                rock_update();
                coal_shake_tick();
                /* 行が変わらないまま横へ動いたら、樋で曲がった瞬間 */
                for (k = 0; k < ROCK_MAX; k++) {
                    if (rock_state[k] && rock_y[k] == oy[k] && rock_x[k] != ox[k]
                        && (rock_x[k] & 15) == 0) { nb++; }
                    ox[k] = rock_x[k]; oy[k] = rock_y[k];
                }
            }
            got = coal0 - coal_left;
            /* **同じ取れ高なら、樋を通る手を採る。**
               取れ高だけで選ぶと「炭柱を叩いて落盤」が最善手になり、
               板が一度も使われない面が出荷される (実際にそうなった)。 */
            if (got > hand || (got == hand && nb > hbend)) {
                if (got > hand) { hand = got; hbend = nb; }
                else { hbend = nb; }
            }
            /* **hpick: 炭柱以外を1枚割って、岩が樋を通って採れた数。**
               炭柱を割ると盤面全体が動くので、樋を通っても**原因が目で追えない**。
               普通の黒ダイヤ1枚なら「その岩が落ちて、この板で曲がって、ここを採る」
               という一本の線になり、落とす前に指でなぞれる。
               作者が示した「岩→炭層→樋→炭柱」の絵が、そのまま遊べる形。 */
            if (!isp && nb > 0 && got > hpick) { hpick = got; }
            /* **hpick0: 炭柱以外を1枚割って採れた数 (樋は問わない)。**
               樋を置かない第一坑 (面1〜3) では、これが「指でなぞれる一本の線」になる。 */
            if (!isp && got > hpick0) { hpick0 = got; }
        }
    }

    /*
     * **hpush: ずらして置いた岩を1つ押して黒ダイヤの真上へ揃え、一手打って採れる最大数。**
     *
     * 面が進むと岩の一部を炭層の真上からずらして置く (field.c の rock_off_tbl)。
     * その岩は hand では1個も採れないので、**揃える手間を払ったぶんだけ伸びるか**を別に測る。
     * 押しは engine の rock_push() を1ドットずつ呼び、毎ドット rock_update() を回す
     * (途中で支えを失えば実際に落ちる)。押し手が土を掘る手間は数えない。
     * 揃えたあとの一手は「真下を割る」と「炭柱を割る (揃えた岩も落盤に加わる)」の良いほう。
     *
     *   noff   : 真下が黒ダイヤでない岩の数 (押さないと炭層を貫かない岩)
     *   npush  : そのうち、左右どちらかへ1〜2升押せば黒ダイヤの真上で止まる岩の数
     *   hpbend : hpush の最善手で岩が樋を通った回数
     *   rowdup : 岩が2個以上ある行の数。**0 が必須** (横一列8枚のスプライト制限)
     */
    {
        int rows[ROWS];
        for (r = 0; r < ROWS; r++) { rows[r] = 0; }
        build_stage();
        for (i = 0; i < ROCK_MAX; i++) {
            if (rock_state[i]) { rows[rock_y[i] >> 4]++; }
        }
        for (r = 0; r < ROWS; r++) { if (rows[r] > 1) { rowdup++; } }
        for (i = 0; i < ROCK_MAX; i++) { bi_got[i] = -1; bi_d[i] = 0; bi_k[i] = 0; }
        for (i = 0; i < ROCK_MAX; i++) {
            int rc, rr, d, k, can = 0;
            build_stage();
            if (!rock_state[i]) { continue; }
            rc = rock_x[i] >> 4; rr = rock_y[i] >> 4;
            if (rr + 1 >= ROWS || coal[(rr + 1) * COLS + rc]) { continue; }
            noff++;
            for (d = -1; d <= 1; d += 2) {
                for (k = 1; k <= 2; k++) {
                    int tc = rc + d * k, v, got, nb;
                    if (tc < 0 || tc >= COLS || rc - d < 0 || rc - d >= COLS) { continue; }
                    /* 揃えたあとの一手は2通り: 真下の黒ダイヤを割る / 炭柱を割る (落盤に巻き込む) */
                    for (v = 0; v <= PILLAR_MAX; v++) {
                        got = push_try(i, d, k, v, coal0, &nb);
                        if (got < 0) { if (v == 0) { break; } continue; }
                        can = 1;
                        if (got > hpush || (got == hpush && nb > hpbend)) { hpush = got; hpbend = nb; }
                        if (got > bi_got[i]) { bi_got[i] = got; bi_d[i] = d; bi_k[i] = k; }
                    }
                }
            }
            if (can) { npush++; }
        }
        /*
         * **hprep: ずらした岩を全部揃えてから一手打って採れる数。**
         * 岩ごとに hpush で一番よかった押し方をまとめて行い、そのあと炭柱を割る
         * (揃えた岩が全部いっせいに落ちる)。「何個も動かして準備してから一気に」の
         * 見返りがあるかを測る。作者の指摘 (2026-09-19)「岩を動かして準備してからの
         * 爽快感であるカタルシス」。
         */
        {
            int v, ok = 1, nb;
            for (v = 1; v <= PILLAR_MAX; v++) {
                int got;
                build_stage();
                ok = 1;
                for (i = 0; i < ROCK_MAX && ok; i++) {
                    if (bi_k[i]) { ok = push_rock(i, bi_d[i], bi_k[i]); }
                }
                if (!ok) { break; }        /* 押し順で塞がる組み合わせ */
                got = finish(v, 0, 0, coal0, &nb);
                if (got > hprep || (got == hprep && nb > hprepbend)) { hprep = got; hprepbend = nb; }
            }
        }
    }

    /*
     * **出水 (第三坑)。** 落盤を起こして60秒ぶん回し、熱水が何升まで広がるかと、
     * 四方が全部水になって叩けなくなる黒ダイヤが出ないかを見る。
     *   wet   : 水になった升の数。少なすぎると「水が湧いた」ことが遊びに関与しない
     *   wseal : 四方が全部水の黒ダイヤの数。**0 でないと詰む**
     * 出水のある面だけ測る (他の面は 0 のまま)。
     */
    if (st >= WATER_ST && st <= WATER_ST_END) {
        int fr;
        build_stage();
        rock_collapse();
        for (fr = 0; fr < 60 * 60; fr++) {
            rock_update();
            coal_shake_tick();
            water_tick();
        }
        for (i = 0; i < NCELL; i++) { if (water[i]) { wet++; } }
        for (i = 0; i < NCELL; i++) {
            const int dc[4] = { -1, 1, 0, 0 }, dr[4] = { 0, 0, -1, 1 };
            int k, ok = 0;
            if (!coal[i] || coal[i] == COAL_CRACKED) { continue; }
            for (k = 0; k < 4; k++) {
                int nc = i % COLS + dc[k], nr = i / COLS + dr[k];
                if (nc < 0 || nc >= COLS || nr < 0 || nr >= ROWS) { continue; }
                if (!water[nr * COLS + nc] && !coal[nr * COLS + nc]) { ok = 1; }
            }
            if (!ok) { wseal++; }
        }
    }

    /* **溜まり湯。** 面の開始から止まっている熱水の升数 (WPOOL_ST 以降の面だけ)。 */
    build_stage();
    for (i = 0; i < NCELL; i++) { if (water[i] == WATER_POOL) { pool++; } }

    /* **鉱車。** レールが置けたか / レールの真上の列に岩が何個あるか。
       岩が1つも無いと鉱車に載せる物が無く、ただ走っているだけの飾りになる。 */
    rail = cart_on;
    if (cart_on) {
        int e;
        for (i = 0; i < ROCK_MAX; i++) {
            int rc = rock_x[i] >> 4, rr = rock_y[i] >> 4;
            if (rock_state[i] && rr < cart_r && rc >= cart_c0 && rc <= cart_c1) { railrock++; }
        }
        /* **車止めから岩を落として、実際に採れる黒ダイヤを数える。**
           列に黒ダイヤが「ある」だけでは届かない (途中の岩や水風呂で止まる)。
           机上で数えていたときは、6面中3面が**実際には0個**だった
           (作者の問い「実際に鉱脈を潰す面は何面か」で分かった、2026-10-09)。
           **盤面にある岩を1つ借りる** (鉱車が運んでくるのと同じ)。 */
        for (e = 0; e < 2; e++) {
            int c = e ? cart_c1 : cart_c0, got;
            build_stage();
            got = drop_from(c, cart_r);
            if (got > railpay) { railpay = got; }
        }
        build_stage();
    }

    /* **左右の散らばり。** 少ない側の割合 (百分率)。0 = 片側だけ、50 = 均等 */
    {
        int cl = 0, cn = 0, rl = 0, rn = 0;
        build_stage();
        for (i = 0; i < NCELL; i++) {
            if (coal[i]) { cn++; if (i % COLS < COLS / 2) { cl++; } }
        }
        for (i = 0; i < ROCK_MAX; i++) {
            if (rock_state[i]) { rn++; if ((rock_x[i] >> 4) < COLS / 2) { rl++; } }
        }
        cbal = cn ? 100 * (cl < cn - cl ? cl : cn - cl) / cn : 50;
        rbal = rn ? 100 * (rl < rn - rl ? rl : rn - rl) / rn : 50;
    }

    /*
     * **digpay: 下ごしらえ (隙間を掘る) の見返り。**
     *
     * 仕込んだ鉱脈 (field.c の place_gap_veins) は縦に土を1升ずつ挟んである。
     * 隙間を掘らずに一番上を割ると、岩はそこで止まって**一段ぶんしか採れない**。
     * 先に隙間を掘ってから落とすと全段を貫く。その差を測る。
     *
     *   digpay   : 掘ったぶん増えた黒ダイヤの数 (仕込んだ鉱脈の合計)
     *   digmin   : 1本あたりの最小。2本仕込む面で「片方が死んでいる」を弾く
     *   digcells : そのために掘る升の数 (段取りの重さ)
     *
     * どちらの場合も**その列の黒ダイヤは全部割ってから**落とす。差が出るのは
     * 掘ったかどうかだけになる。割る順番 (下から割って最後に一番上) は
     * 遊ぶ側の話で、ここでは測らない。
     */
    {
        int q;
        for (q = 0; q < gapv_n; q++) {
            int c, rtop, rlow, rr, f, before, got_a, got_b, cells = 0;
            build_stage();
            c = gapv_c[q]; rtop = gapv_r[q];
            for (rlow = ROWS - 1; rlow > rtop; rlow--) {
                if (coal[rlow * COLS + c] && coal[rlow * COLS + c] != COAL_CRACKED) { break; }
            }
            /* (A) 掘らずに落とす */
            before = coal_left;
            for (rr = rtop; rr <= rlow; rr++) {
                while (cell_coal((int8_t)c, (int8_t)rr)) { coal_hit((int8_t)c, (int8_t)rr); }
            }
            rock_prop_break((int8_t)c, (int8_t)(rtop - 1));   /* 支柱を折る = 引き金 */
            for (f = 0; f < 400; f++) { rock_update(); coal_shake_tick(); }
            got_a = before - coal_left;
            /* (B) 隙間を掘ってから落とす。**掘るのは cf_on(, CF_DUG) を直に立てる** —
               岩の支えは cell_dug しか見ないので、膜は要らない。 */
            build_stage();
            before = coal_left;
            for (rr = rtop; rr <= rlow; rr++) {
                int j = rr * COLS + c;
                if (!coal[j] && !cf_on(j, CF_DUG)) { cf_set(j, CF_DUG); dug_n++; cells++; }
            }
            for (rr = rtop; rr <= rlow; rr++) {
                while (cell_coal((int8_t)c, (int8_t)rr)) { coal_hit((int8_t)c, (int8_t)rr); }
            }
            rock_prop_break((int8_t)c, (int8_t)(rtop - 1));
            for (f = 0; f < 400; f++) { rock_update(); coal_shake_tick(); }
            got_b = before - coal_left;
            if (got_b - got_a < digmin) { digmin = got_b - got_a; }
            digpay += got_b - got_a;
            digcells += cells;
        }
        if (!gapv_n) { digmin = 0; }
    }

    /* **所要時間の見積もり。** 盤面を回って黒ダイヤを全部割るまでのフレーム数
       (par)、その道中で掘る升の数 (dpar)、風呂の遠さ (bathd)。持ち時間の表は
       これを基準に決める。parleft > 0 は「届かない黒ダイヤが残った」の印。 */
    build_stage();                  /* **前の実験で盤面が荒れている。** 組み直してから測る
                                       (digpay の実験のあとに測って、面7の par が
                                       3.1秒になっていた。2026-10-18) */
    par = par_route(&dpar, &bathd, &parleft);
    gapn = gapv_n;

    printf("{\"stage\":%d,\"seed\":%u,\"coal\":%d,\"rocks\":%d,"
           "\"free\":%d,\"haul\":%d,\"colmax\":%d,\"veins\":%d,\"veinmax\":%d,"
           "\"chutes\":%d,\"reach\":%d,\"hand\":%d,\"hbend\":%d,\"hpick\":%d,"
           "\"bend\":%d,\"combo\":%d,\"bump\":%d,\"side\":%d,"
           "\"cavef\":%d,\"cbend\":%d,\"nfall\":%d,\"lost\":%d,\"maxline\":%d,"
           "\"taken\":%d,\"crack\":%d,\"dug0\":%u,"
           "\"noff\":%d,\"npush\":%d,\"hpush\":%d,\"hpbend\":%d,\"rowdup\":%d,"
           "\"hprep\":%d,\"hprepbend\":%d,\"cbal\":%d,\"rbal\":%d,\"hpick0\":%d,"
           "\"wet\":%d,\"wseal\":%d,\"pool\":%d,\"rail\":%d,\"railrock\":%d,\"railpay\":%d,"
           "\"par\":%d,\"dpar\":%d,\"bathd\":%d,\"parleft\":%d,\"gapn\":%d,"
           "\"digpay\":%d,\"digmin\":%d,\"digcells\":%d}\n",
           st, sd, coal0, nrock, free_max, haul, best, veins, vmax,
           chutes, reach, hand, hbend, hpick, bend, best_combo,
           bump, side, cavef, cbend, nfall, lost, maxline, taken, crack, dug_n0,
           noff, npush, hpush, hpbend, rowdup, hprep, hprepbend, cbal, rbal, hpick0,
           wet, wseal, pool, rail, railrock, railpay,
           par, dpar, bathd, parleft, gapn, digpay, digmin, digcells);
}

/*
 * **出水を試す (--water)。** 面を組んで落盤を起こし、熱水が広がる様子を升で出す。
 * 実機と同じ src/water.c を回すので、規則の確認はここでできる。
 *   . = 土 / 空白 = 坑道 / 数字 = 黒ダイヤ / O = 岩 / ~ = 熱水 / = = 溜まり湯
 *   = = 鉱車のレール / | = 車止め / S = 湧き出し口
 *   B = 水風呂 / N = 敵の湧き出し口 / @ = 自機の出発点
 */
/* 仕込んだ鉱脈の隙間か (上下が黒ダイヤの土升)。盤面を見るためだけの判定。 */
static int gap_reserved(int c, int r)
{
    int i = r * COLS + c;
    if (cf_on(i, CF_DUG) || coal[i]) { return 0; }
    if (r < 1 || r + 1 >= ROWS) { return 0; }
    return coal[i - COLS] && coal[i + COLS];
}


/* **組み立て直後の盤面をそのまま出す (--board)。** --water は落盤を起こした
   あとの姿なので、仕込みの形 (掘らせる隙間) を見るにはこちらを使う。
   `*` = 掘らせる隙間 (used[] の予約) */
static void board_demo(int st)
{
    int c, r, i;
    build_stage();
    printf("面%d 仕込んだ鉱脈 %d本", st, gapv_n);
    for (i = 0; i < gapv_n; i++) { printf(" (列%d 行%d)", gapv_c[i], gapv_r[i]); }
    putchar('\n');
    for (r = 0; r < ROWS; r++) {
        for (c = 0; c < COLS; c++) {
            i = r * COLS + c;
            if (water[i])                                { putchar('='); }
            else if (coal[i] && coal[i] != COAL_CRACKED) { putchar('0' + (coal[i] % 10)); }
            else if (rock_at((int8_t)c, (int8_t)r) >= 0) { putchar('O'); }
            else if (bath_on && c == bath_c && r == bath_r) { putchar('B'); }
            else if (c == nest_c && r == nest_r)         { putchar('N'); }
            else if (c == start_c && r == start_r)       { putchar('@'); }
            else if (cf_on(i, CF_DUG))                             { putchar(' '); }
            else if (gap_reserved(c, r))                 { putchar('*'); }
            else                                         { putchar('.'); }
        }
        putchar('\n');
    }
}

static void water_demo(int st)
{
    int fr, c, r, i, seal = 0, dry = 0, wetn = 0;
    build_stage();
    printf("面%d 湧き出し口 %s", st, spring_on ? "" : "無し\n");
    if (spring_on) { printf("(%d,%d)\n", spring_c, spring_r); }
    rock_collapse();
    {
        int prev = 0, sec = 0, q;
        for (q = 0; q < NCELL; q++) { if (water[q]) { prev++; } }   /* 溜まり湯は最初から */
        for (fr = 0; fr < 60 * 60; fr++) {      /* 60秒ぶん回す */
            rock_update();
            coal_shake_tick();
            water_tick();
            if ((fr % 60) == 59) {
                int now = 0;
                sec++;
                for (q = 0; q < NCELL; q++) { if (water[q]) { now++; } }
                if (now != prev) {
                    printf("  %2d秒: +%d 升 (計 %d)%s\n", sec, now - prev, now,
                           (now - prev > 1) ? "  <- 分岐で同時に流れた" : "");
                    prev = now;
                }
            }
        }
    }
    for (r = 0; r < ROWS; r++) {
        for (c = 0; c < COLS; c++) {
            i = r * COLS + c;
            if (cart_on && r == cart_r && c >= cart_c0 && c <= cart_c1
                && !water[i] && !coal[i] && rock_at((int8_t)c, (int8_t)r) < 0) {
                putchar((c == cart_c0 || c == cart_c1) ? '|' : '=');   /* レールと車止め */
            }
            else if (water[i] == WATER_POOL)       { putchar('='); }
            else if (water[i])                     { putchar('~'); }
            else if (spring_on && c == spring_c && r == spring_r) { putchar('S'); }
            else if (coal[i] && coal[i] != COAL_CRACKED) { putchar('0' + (coal[i] % 10)); }
            else if (rock_at((int8_t)c, (int8_t)r) >= 0) { putchar('O'); }
            else if (bath_on && c == bath_c && r == bath_r) { putchar('B'); }
            else if (c == nest_c && r == nest_r)   { putchar('N'); }
            else if (c == start_c && r == start_r) { putchar('@'); }
            else if (cf_on(i, CF_DUG))                       { putchar(' '); }
            else                                   { putchar('.'); }
        }
        putchar('\n');
    }
    for (i = 0; i < NCELL; i++) { if (water[i]) { wetn++; } }
    /* **残った黒ダイヤを、水に浸からずに叩けるか。** 土はこれから掘れるので「叩ける」
       側に数える。**四方が全部水の黒ダイヤだけが詰み。** */
    for (i = 0; i < NCELL; i++) {
        int ok = 0;
        const int dc[4] = { -1, 1, 0, 0 }, dr[4] = { 0, 0, -1, 1 };
        int k;
        if (!coal[i] || coal[i] == COAL_CRACKED) { continue; }
        for (k = 0; k < 4; k++) {
            int nc = i % COLS + dc[k], nr = i / COLS + dr[k], ni;
            if (nc < 0 || nc >= COLS || nr < 0 || nr >= ROWS) { continue; }
            ni = nr * COLS + nc;
            if (!water[ni] && !coal[ni]) { ok = 1; }    /* 土でもよい (掘れば立てる) */
        }
        if (ok) { dry++; } else { seal++; }
    }
    printf("水になった升 %d / 水に浸からず叩ける黒ダイヤ %d / 叩けない黒ダイヤ %d\n",
           wetn, dry, seal);
    printf("水風呂 %s / 敵の巣 %s\n",
           (bath_on && water[bath_r * COLS + bath_c]) ? "水没(不可)" : "無事",
           water[nest_r * COLS + nest_c] ? "水没" : "無事");
}

static void cart_demo(int st)
{
    int e;
    build_stage();
    if (!cart_on) { printf("面%d 鉱車なし\n", st); return; }
    printf("面%d レール 行%d 列%d..%d\n", st, cart_r, cart_c0, cart_c1);
    for (e = 0; e < 2; e++) {
        int c = e ? cart_c1 : cart_c0, k, raw, dug_;
        build_stage();
        raw = drop_from(c, cart_r);
        build_stage();
        for (k = cart_r + 1; k < ROWS; k++) {
            if (!coal[k * COLS + c]) { dig_sweep((uint8_t)c, (uint8_t)k); }
        }
        dug_ = drop_from(c, cart_r);
        printf("  車止め 列%-2d : そのまま %d個 / 下を掘れば %d個\n", c, raw, dug_);
    }
}

int main(int argc, char **argv)
{
    int want_stage = 1, seed = 1, max_frames = 5400, greedy = 1, i, f;
    uint8_t face_coal_now = 0;       /* いま向いている先が黒ダイヤか (つるはしを押す条件) */
    int do_analyse = 0, sweep = 0, do_water = 0, do_cart = 0, do_board = 0;
    unsigned lseed = 0;
    uint32_t prev_score = 0;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--stage")  && i + 1 < argc) { want_stage = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--seed")   && i + 1 < argc) { seed = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) { max_frames = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--bot") && i + 1 < argc) {
            const char *b = argv[++i];
            greedy = !strcmp(b, "greedy") ? 1 : (!strcmp(b, "smart") ? 2 : 0);
        }
        else if (!strcmp(argv[i], "--safe") && i + 1 < argc) { safe_steps = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--layout-seed") && i + 1 < argc) { lseed = (unsigned)atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--ura")) { loop_n = 1; }   /* 裏面の規則で測る */
        else if (!strcmp(argv[i], "--analyse")) { do_analyse = 1; }
        else if (!strcmp(argv[i], "--board")) { do_board = 1; }
        else if (!strcmp(argv[i], "--sweep") && i + 1 < argc) { sweep = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--water")) { do_water = 1; }
        else if (!strcmp(argv[i], "--cart")) { do_cart = 1; }
    }

    /* 乱数の種。実機は起動からの経過で決まるので、ここでは明示的に回して合わせる。 */
    rnd_seed((uint8_t)seed);

    stage = (uint8_t)want_stage;
    lives = 3;
    if (sweep) {                 /* 種を総当たりして盤面だけ測る */
        unsigned sd;
        for (sd = 1; sd <= (unsigned)sweep; sd++) {
            layout_seed_force = (uint16_t)sd;
            build_stage();
            analyse(want_stage, sd);
        }
        return 0;
    }
    layout_seed_force = (uint16_t)lseed;
    if (do_water) {
        water_demo(want_stage);
        return 0;
    }
    if (do_cart) {
        stage = want_stage;
        cart_demo(want_stage);
        return 0;
    }
    build_stage();
    if (do_board) {
        board_demo(want_stage);
        return 0;
    }
    if (do_analyse) {
        analyse(want_stage, lseed ? lseed : layout_seed());
        return 0;
    }
    bomb_clear();
    player_start();
    enemy_stage_reset();
    bonus_stage_reset();
    combo = 0;

    printf("{\"type\":\"stage\",\"stage\":%d,\"seed\":%d,\"coal\":%d,\"dug0\":%u,"
           "\"start\":[%d,%d],\"bath\":[%d,%d],\"nest\":[%d,%d],\"grid\":[",
           want_stage, seed, coal_left, dug_n0,
           start_c, start_r, bath_c, bath_r, nest_c, nest_r);
    /* 盤面を1行1文字列で。 . =土 空白=坑道 数字=黒ダイヤの耐久 O=岩 B=風呂 N=湧き出し口
       火薬は p、ダイナマイトは d。面エディタ側でもこの表記をそのまま使う。 */
    {
        int r2, c2;
        for (r2 = 0; r2 < ROWS; r2++) {
            printf("%s\"", r2 ? "," : "");
            for (c2 = 0; c2 < COLS; c2++) {
                uint16_t i2 = (uint16_t)r2 * COLS + c2;
                char ch;
                if (bath_on && c2 == bath_c && r2 == bath_r)      { ch = 'B'; }
                else if (c2 == nest_c && r2 == nest_r)            { ch = 'N'; }
                else if (rock_at((int8_t)c2, (int8_t)r2) >= 0)    { ch = 'O'; }
                else if (coal[i2] == COAL_CRACKED)               { ch = 'x'; }
                else if (coal[i2])                               { ch = (char)('0' + (coal[i2] > 9 ? 9 : coal[i2])); }
                else if (cf_on(i2, CF_POWDER))                             { ch = 'p'; }
                else if (!cf_on(i2, CF_DUG))                               { ch = '.'; }
                else                                             { ch = ' '; }
                putchar(ch);
            }
            printf("\"");
        }
    }
    printf("]}\n");

    for (f = 0; f < max_frames; f++) {
        int pc, pr, k;
        frame_n++;
        memset(sfx_fired, 0, sizeof sfx_fired);

        sim_stick = (greedy == 2) ? bot_smart() : (greedy ? bot_greedy() : 0);
        /* つるはしは「向いている先が黒ダイヤ」のときだけ振る (立ち上がり検出があるので隔フレーム) */
        {
            int cx = px >> 4, cy = py >> 4;
            uint8_t d = player_dir();
            if (d == DIR_UP) { cy--; } else if (d == DIR_DOWN) { cy++; }
            else if (d == DIR_LEFT) { cx--; } else if (d == DIR_RIGHT) { cx++; }
            face_coal_now = cell_coal((int8_t)cx, (int8_t)cy);
            sim_trig = (uint8_t)(face_coal_now && (f & 1));
        }
        sim_trig_b = 0;

        if (!dead) {
            player_update();
            /* **黒ダイヤを向いているときだけ押す。**
               以前は `swing_pow < CHARGE_MAX` だけで押していたので、**歩いている間も
               ずっと溜め続け**、78フレームごとに威力3の空振りを打っていた
               (体温 +12 と +4 が位置に関係なく周期的に入る)。歩くだけで毎秒12ほど
               余計に発熱していて、どのボットも面3以降は16回中16回オーバーヒートで
               死んでいた。空振りでも発熱する仕様 (`do_swing`) は正しく、
               **押し方のほうが人の遊び方と違っていた** (2026-10-26)。
               最大まで溜めたら離す = 溜め打ちを繰り返す、という遊び方にする。

               **弱く連打するほうが得ではない。** 「敵が来るまでの時間で段を決め、
               間に合わないなら弱く叩く」を入れたら 720回中のクリアが 241 から
               216 へ落ちた (2026-10-28 実測)。溜めと振りで**中断できるかが違う**
               のが理由で、振り (`swing` = PICK_FRAMES) のあいだは動けないが、
               溜め (`charge_t`) のあいだは動ける (動けば溜めが切れるだけ)。
               威力3まで溜めると 96 フレームのうち動けないのは2回の振りの 36 だけ。
               弱く連打すると 18 フレームごとに振りが入って**ずっと動けない**。 */
            player_pick((uint8_t)((face_coal_now || bot_want_pick)
                                  && swing_pow < CHARGE_MAX));
            /* **`main.c` と同じ順で投げる。** `player_update` が向きを決めた
               あとなので、ボットが倒したスティックの向きへ飛ぶ。 */
            if (bot_bomb_dir != DIR_NONE) {
                bomb_throw(player_dir());
                bot_bomb_dir = DIR_NONE;
            }
        }
        coal_shake_tick();
        bomb_update();
        rock_update();
        enemy_update();
        bonus_update();

        pc = px >> 4; pr = py >> 4;
        bfs(pc, pr);

        printf("{\"f\":%d,\"p\":[%d,%d],\"temp\":%d,\"coal\":%d,\"dug\":%u,"
               "\"score\":%u,\"dscore\":%u,\"dead\":%d,\"stick\":%d,\"face\":%d,"
               "\"treach\":%d,\"esc\":%d,\"combo\":%d,\"stun\":%d,\"stock\":%d,\"e\":[",
               f, pc, pr, temp, coal_left, dug_n,
               sim_score, sim_score - prev_score, dead, sim_stick, player_dir(),
               t_reach(pc, pr), escapes(pc, pr), (int)combo,
               (int)(en_stun[0] + en_stun[1] + en_stun[2] + en_stun[3]),
               (int)stock);
        prev_score = sim_score;
        for (k = 0; k < ENEMY_MAX; k++) {
            int ec = en_x[k] >> 4, er = en_y[k] >> 4;
            printf("%s{\"on\":%d,\"c\":[%d,%d],\"g\":%d,\"warm\":%d,\"d\":%d}",
                   k ? "," : "", en_state[k], ec, er, en_ghost[k],
                   en_warm[k] ? 1 : 0,
                   (ec >= 0 && ec < COLS && er >= 0 && er < ROWS) ? bfs_d[er * COLS + ec] : -1);
        }
        printf("],\"r\":[");
        for (k = 0; k < ROCK_MAX; k++) {
            printf("%s[%d,%d,%d]", k ? "," : "",
                   rock_state[k], rock_x[k] >> 4, rock_y[k] >> 4);
        }
        printf("],\"sfx\":%d}\n", (int)(sfx_fired[0] + sfx_fired[1] + sfx_fired[2] +
               sfx_fired[3] + sfx_fired[4] + sfx_fired[5] + sfx_fired[6] +
               sfx_fired[7] + sfx_fired[8]));

        if (coal_left == 0) {
            printf("{\"type\":\"clear\",\"frames\":%d}\n", f);
            return 0;
        }
        if (dead) {
            /* **死因 0 は「それ以外」が全部入る。** 岩か敵かガスかを現場の様子で分ける
               (ゲーム側は 0 のまま区別していないので、ここで見るしかない)。 */
            {
                const char *why = "?";
                int c0 = px >> 4, r0 = py >> 4, k0;
                if (dead_cause == DEAD_HEAT)      { why = "heat"; }
                else if (dead_cause == DEAD_TIME) { why = "time"; }
                else if (gas_n && gas_at((int8_t)c0, (int8_t)r0)) { why = "gas"; }
                /* 自分の升で爆発した (`explode_on_enemy`)。**敵は先に消えている**ので
                   下の重なり判定には掛からず、放っておくと「不明」になる。 */
                else if (bomb_state == 2 && bomb_c == (uint8_t)((px + 8) >> 4)
                         && bomb_r == (uint8_t)((py + 8) >> 4)) { why = "self"; }
                else if (cart_on && r0 == cart_r) { why = "cart"; }
                /* 槍 (赤の突き)。**1升離れている**ので下の重なり判定には掛からない。 */
                else if (spear_near()) { why = "spear"; }
                else {
                    /* **重なりは画素で見る。** 升で見ると、斜めに隣り合って画素では
                       重なっている当たりを取りこぼして「不明」になる。
                       岩も同じで、`hits_player` は 13px の重なりで潰すので、
                       升で見ていたときは**升をまたいで潰された1件を取りこぼした**
                       (面6 seed1。岩が升 [7,7]、自機が升 [6,8] で画素では重なっていた)。 */
                    /* **落下中に限ってはいけない。** 潰した岩はその場で着地して
                       状態が変わるので、死んだフレームには既に ROCK_FALL ではない。
                       止まっている岩は自機と重なれない (通れないので) から、
                       13px 重なっていればそれが死因でよい。 */
                    for (k0 = 0; k0 < ROCK_MAX; k0++) {
                        if (rock_state[k0] == ROCK_NONE) { continue; }
                        if (abs((int)rock_x[k0] - (int)px) < 13
                            && abs((int)rock_y[k0] - (int)py) < 13) {
                            why = "rock"; break;
                        }
                    }
                    for (k0 = 0; why[0] == '?' && k0 < ENEMY_MAX; k0++) {
                        if (en_state[k0] == 1
                            && abs((int)en_x[k0] - (int)px) < 16
                            && abs((int)en_y[k0] - (int)py) < 16) {
                            why = "enemy"; break;
                        }
                    }
                }
                printf("{\"type\":\"dead\",\"frames\":%d,\"cause\":%d,\"why\":\"%s\"}\n",
                       f, dead_cause, why);
            }
            return 0;
        }
    }
    printf("{\"type\":\"timeout\",\"frames\":%d,\"coal_left\":%d}\n", max_frames, coal_left);
    return 0;
}
