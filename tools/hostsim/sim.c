/*
 * SakiyamaBlast - 面の数値化ツール (ホスト側シミュレータ)
 *
 * src/ のゲームロジックをそのまま gcc でビルドし、VDP/PSG だけスタブに差し替えて
 * ヘッドレスで走らせる。毎フレームの状態を JSONL で吐き、採点は Python 側で行う。
 *
 * 使い方:  hostsim --stage N [--seed S] [--frames F] [--bot greedy|idle]
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
 * 通行グラフ。cell_link[] をそのまま辿る。
 * 「1升隣でも土の膜で隔てられていれば敵は来られない」ので、危険度は
 * ユークリッド距離ではなく必ずこのグラフ上の距離で測る。
 * ------------------------------------------------------------------ */
static int16_t bfs_d[NCELL];

static int passable(int c, int r)
{
    uint16_t i = (uint16_t)r * COLS + c;
    if (c < 0 || c >= COLS || r < 0 || r >= ROWS) { return 0; }
    if (!dug[i]) { return 0; }
    if (coal[i] && coal[i] != COAL_CRACKED) { return 0; }
    /* 岩は塞ぐ。横なら押せることもあるが、経路として当てにすると
       押せない岩に延々ぶつかり続けるので、通れない扱いにしておく。 */
    if (rock_at((int8_t)c, (int8_t)r) >= 0) { return 0; }
    return 1;
}

/* a から b へ動けるか。膜 (cell_link) が張っていれば通れない。 */
static int linked(int c, int r, int nc, int nr)
{
    uint16_t i = (uint16_t)r * COLS + c;
    uint16_t j = (uint16_t)nr * COLS + nc;
    if (nc == c + 1) { return cell_link[i] & LINK_RIGHT; }
    if (nc == c - 1) { return cell_link[j] & LINK_RIGHT; }
    if (nr == r + 1) { return cell_link[i] & LINK_DOWN; }
    if (nr == r - 1) { return cell_link[j] & LINK_DOWN; }
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
    int hot = (bath_on && temp > (TEMP_MAX * 7 / 10));

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

    /* 3. 通路が届いていないので土を掘る。一番近い黒ダイヤの方角へ、進める向きを選ぶ。 */
    if (want < 0) {
        int gc = -1, gr = -1;
        bestd = 9999;
        for (i = 0; i < NCELL; i++) {
            int d;
            if (!coal[i]) { continue; }
            d = abs((i % COLS) - pc) + abs((i / COLS) - pr);
            if (d < bestd) { bestd = d; gc = i % COLS; gr = i / COLS; }
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
            if (pillar[q] && cell_coal((int8_t)(q % COLS), (int8_t)(q / COLS)) && --v == 0) {
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
        for (k = 0; k < NCELL; k++) { if (!coal[k]) { dug[k] = 1; } }
        for (k = 0; k < NCELL; k++) {
            int kc = k % COLS, kr = k / COLS;
            if (!dug[k]) { continue; }
            if (kc + 1 < COLS && dug[k + 1])    { cell_link[k] |= LINK_RIGHT; }
            if (kr + 1 < ROWS && dug[k + COLS]) { cell_link[k] |= LINK_DOWN; }
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
        for (q = 0; q < n; q++) {
            int hc = idx[q] % COLS, hr = idx[q] / COLS, fr, got, k, nb = 0, isp;
            uint8_t ox[ROCK_MAX], oy[ROCK_MAX];
            build_stage();
            isp = pillar[idx[q]] ? 1 : 0;
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

    printf("{\"stage\":%d,\"seed\":%u,\"coal\":%d,\"rocks\":%d,"
           "\"free\":%d,\"haul\":%d,\"colmax\":%d,\"veins\":%d,\"veinmax\":%d,"
           "\"chutes\":%d,\"reach\":%d,\"hand\":%d,\"hbend\":%d,\"hpick\":%d,"
           "\"bend\":%d,\"combo\":%d,\"bump\":%d,\"side\":%d,"
           "\"cavef\":%d,\"cbend\":%d,\"nfall\":%d,\"lost\":%d,\"maxline\":%d,"
           "\"taken\":%d,\"crack\":%d,\"dug0\":%u,"
           "\"noff\":%d,\"npush\":%d,\"hpush\":%d,\"hpbend\":%d,\"rowdup\":%d,"
           "\"hprep\":%d,\"hprepbend\":%d,\"cbal\":%d,\"rbal\":%d,\"hpick0\":%d,"
           "\"wet\":%d,\"wseal\":%d,\"pool\":%d,\"rail\":%d,\"railrock\":%d,\"railpay\":%d}\n",
           st, sd, coal0, nrock, free_max, haul, best, veins, vmax,
           chutes, reach, hand, hbend, hpick, bend, best_combo,
           bump, side, cavef, cbend, nfall, lost, maxline, taken, crack, dug_n0,
           noff, npush, hpush, hpbend, rowdup, hprep, hprepbend, cbal, rbal, hpick0,
           wet, wseal, pool, rail, railrock, railpay);
}

/*
 * **出水を試す (--water)。** 面を組んで落盤を起こし、熱水が広がる様子を升で出す。
 * 実機と同じ src/water.c を回すので、規則の確認はここでできる。
 *   . = 土 / 空白 = 坑道 / 数字 = 黒ダイヤ / O = 岩 / ~ = 熱水 / = = 溜まり湯
 *   = = 鉱車のレール / | = 車止め / S = 湧き出し口
 *   B = 水風呂 / N = 敵の湧き出し口 / @ = 自機の出発点
 */
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
            else if (dug[i])                       { putchar(' '); }
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
    int do_analyse = 0, sweep = 0, do_water = 0, do_cart = 0;
    unsigned lseed = 0;
    uint32_t prev_score = 0;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--stage")  && i + 1 < argc) { want_stage = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--seed")   && i + 1 < argc) { seed = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) { max_frames = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--bot") && i + 1 < argc) { greedy = !strcmp(argv[++i], "greedy"); }
        else if (!strcmp(argv[i], "--safe") && i + 1 < argc) { safe_steps = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--layout-seed") && i + 1 < argc) { lseed = (unsigned)atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--analyse")) { do_analyse = 1; }
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
                else if (powder[i2])                             { ch = 'p'; }
                else if (!dug[i2])                               { ch = '.'; }
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

        sim_stick = greedy ? bot_greedy() : 0;
        /* つるはしは「向いている先が黒ダイヤ」のときだけ振る (立ち上がり検出があるので隔フレーム) */
        {
            int cx = px >> 4, cy = py >> 4;
            uint8_t d = player_dir();
            if (d == DIR_UP) { cy--; } else if (d == DIR_DOWN) { cy++; }
            else if (d == DIR_LEFT) { cx--; } else if (d == DIR_RIGHT) { cx++; }
            sim_trig = (uint8_t)(cell_coal((int8_t)cx, (int8_t)cy) && (f & 1));
        }
        sim_trig_b = 0;

        if (!dead) {
            player_update();
            /* **押しっぱなしにしっぱなしにしない。** そうすると「押した瞬間の1振り」
               のあとずっと溜めたままになり、ボットが一度も掘らなくなる。
               最大まで溜めたら離す = 溜め打ちを繰り返す、という遊び方にする。 */
            player_pick((uint8_t)(swing_pow < CHARGE_MAX));
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
               "\"treach\":%d,\"esc\":%d,\"e\":[",
               f, pc, pr, temp, coal_left, dug_n,
               sim_score, sim_score - prev_score, dead, sim_stick, player_dir(),
               t_reach(pc, pr), escapes(pc, pr));
        prev_score = sim_score;
        for (k = 0; k < ENEMY_MAX; k++) {
            int ec = en_x[k] >> 4, er = en_y[k] >> 4;
            printf("%s{\"on\":%d,\"c\":[%d,%d],\"g\":%d,\"warm\":%d,\"d\":%d}",
                   k ? "," : "", en_state[k], ec, er, en_ghost[k],
                   en_morph[k] ? 0 : 0,
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
            printf("{\"type\":\"dead\",\"frames\":%d,\"cause\":%d}\n", f, dead_cause);
            return 0;
        }
    }
    printf("{\"type\":\"timeout\",\"frames\":%d,\"coal_left\":%d}\n", max_frames, coal_left);
    return 0;
}
