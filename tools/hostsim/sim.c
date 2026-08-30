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

/* その升にとって「一番近い敵が何歩で来られるか」。来られないなら大きい値。 */
static int danger_at(int c, int r)
{
    int k, best = 999;
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

int main(int argc, char **argv)
{
    int want_stage = 1, seed = 1, max_frames = 5400, greedy = 1, i, f;
    uint32_t prev_score = 0;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--stage")  && i + 1 < argc) { want_stage = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--seed")   && i + 1 < argc) { seed = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) { max_frames = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--bot") && i + 1 < argc) { greedy = !strcmp(argv[++i], "greedy"); }
        else if (!strcmp(argv[i], "--safe") && i + 1 < argc) { safe_steps = atoi(argv[++i]); }
    }

    /* 乱数の種。実機は起動からの経過で決まるので、ここでは明示的に回して合わせる。 */
    rnd_seed((uint8_t)seed);

    stage = (uint8_t)want_stage;
    lives = 3;
    build_stage();
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
            player_pick();          /* 中で「向いている先が黒ダイヤ」と「振っている最中」を見る */
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
