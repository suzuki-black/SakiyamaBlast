/*
 * SakiyamaBlast - 面データの読み書き (ホスト側の道具だけ。ROM には入らない)
 *
 * 面エディタと数値化ツール (metrics.py / emotion.py) を繋ぐための層。
 *
 *   sim --stage 7 --dump          生成した面を面データとして書き出す
 *   sim --load foo.json           build_stage() の代わりに面データを流し込む
 *   sim --load foo.json --state   読み込んだ結果を一覧にする (往復の検証用)
 *
 * **ゲーム側 (src/) には何も足していない。** 盤面を決める変数はすべて game.h で
 * 公開されているので、ここから直接書けば足りる。ROM に面データを持たせるかどうかは
 * まだ決めていない (生成のままにするか、手で作った面に置き換えるか。
 * docs/HOSTSIM.md「未決の設計判断」)。決まるまでは ROM を1バイトも増やさない。
 *
 * **取りこぼしは `--state` で機械的に確かめる。** 盤面を決める変数を全部並べて
 * 出すので、`--stage N --state` と `--load <N のダンプ> --state` を diff すれば、
 * 面データが盤面を言い尽くせているかどうかが分かる (tools/hostsim/roundtrip.sh)。
 *
 * 形式の仕様は docs/STAGE_FORMAT.md。
 */
#include "game.h"
#include "stagefile.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 黒ダイヤの耐久 1..COAL_HP_MAX を1文字で表す。 */
static const char HP_CHARS[] = "123456789AB";

/* ================================================================== *
 * 書き出し
 * ================================================================== */

static void dump_die(const char *fmt, ...)
{
    va_list ap;
    fputs("面データの書き出し: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

/* 掘ってあるか。**中身とは別の板に分ける。**
   以前は岩を `O` の1文字で表して掘り済みを兼ねさせていたが、面10 で
   **樋が岩の居る升を開ける**ことが分かった (place_chutes が落とし口の隣も掘る)。
   1文字に2つの意味を持たせると、こういう組み合わせで静かに落ちる。 */
static char dug_ch(uint8_t c, uint8_t r)
{
    return cf_on((uint16_t)r * COLS + c, CF_DUG) ? ' ' : '.';
}

/* 升の中身の1文字。**岩・黒ダイヤ・樋は同じ升に同居しない**ので、
   どれか1つを選んで返せばよい (同居していたら下で叩く)。 */
static char cell_ch(uint8_t c, uint8_t r)
{
    uint16_t i = (uint16_t)r * COLS + c;
    int8_t  ri = rock_at((int8_t)c, (int8_t)r);
    int     n  = 0;
    char    ch = ' ';

    if (ri >= 0) {
        ch = rock_prop[(uint8_t)ri] ? 'o' : 'O';
        n++;
    }
    if (coal[i]) {
        if (coal[i] == COAL_CRACKED) {
            ch = 'x';
        } else if (coal[i] > COAL_HP_MAX) {
            dump_die("升 %u,%u の黒ダイヤの耐久が %u (上限 %u)", c, r, coal[i], COAL_HP_MAX);
        } else {
            ch = HP_CHARS[coal[i] - 1];
        }
        n++;
    }
    if (chute[i]) {
        if (chute[i] == CHUTE_R)      { ch = '>'; }
        else if (chute[i] == CHUTE_L) { ch = '<'; }
        else { dump_die("升 %u,%u の樋が折れている (組み立て直後の盤面ではない)", c, r); }
        n++;
    }
    if (n > 1) {
        dump_die("升 %u,%u に岩・黒ダイヤ・樋が同居している (面データで表せない)", c, r);
    }
    return ch;
}

/* 坑道・黒ダイヤの上に乗るものの1文字。
   **炭柱は未掘の黒ダイヤ、火薬と溜まり湯は坑道**なので互いに排他。
   火薬の升は place_powder() が used[] を立てるので、溜まり湯は来ない。 */
static char over_ch(uint8_t c, uint8_t r)
{
    uint16_t i = (uint16_t)r * COLS + c;
    int  n = 0;
    char ch = ' ';

    if (cf_on(i, CF_PILLAR)) { ch = '*'; n++; }
    if (cf_on(i, CF_POWDER)) { ch = 'p'; n++; }
    if (water[i]) {
        if (water[i] != WATER_POOL) {
            dump_die("升 %u,%u の水が溜まり湯ではない (広がっている最中の盤面)", c, r);
        }
        ch = '~';
        n++;
    }
    if (n > 1) {
        dump_die("升 %u,%u に炭柱・火薬・溜まり湯が同居している (面データで表せない)", c, r);
    }
    if (cf_on(i, CF_BOMB) || cf_on(i, CF_UME)) {
        dump_die("升 %u,%u にダイナマイトか梅干しが落ちている (組み立て直後の盤面ではない)", c, r);
    }
    return ch;
}

/* JSON の文字列として書く。**エスケープを通す。**
   `--load` で渡された名前をそのまま埋めていたので、`"` や `\` を含む名前の
   ファイルを読んで書き出すと、読めない JSON が出来ていた。 */
static void dump_jstr(FILE *fp, const char *s)
{
    fputc('"', fp);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\')       { fprintf(fp, "\\%c", c); }
        else if (c == '\n')              { fputs("\\n", fp); }
        else if (c == '\t')              { fputs("\\t", fp); }
        else if (c == '\r')              { fputs("\\r", fp); }
        else if (c < 0x20)               { fprintf(fp, "\\u%04X", c); }
        else                             { fputc((int)c, fp); }
    }
    fputc('"', fp);
}

static void dump_grid(FILE *fp, const char *key, char (*f)(uint8_t, uint8_t), int last)
{
    uint8_t c, r;
    fprintf(fp, "  \"%s\": [\n", key);
    for (r = 0; r < ROWS; r++) {
        fputs("    \"", fp);
        for (c = 0; c < COLS; c++) {
            fputc(f(c, r), fp);
        }
        fprintf(fp, "\"%s\n", (r + 1 < ROWS) ? "," : "");
    }
    fprintf(fp, "  ]%s\n", last ? "" : ",");
}

void stage_dump(FILE *fp, unsigned lseed, const char *src)
{
    uint8_t k, c, r;

    fputs("{\n", fp);
    fputs("  \"format\": \"sakiyama-stage\",\n", fp);
    fputs("  \"formatVersion\": 1,\n", fp);
    /* **出どころを正直に書く。** 読み込んだ盤面に `layoutSeed` を書くと、
       その種から生成したという嘘になる (手で直した面でも同じ値が出る)。 */
    if (src) {
        fputs("  \"generator\": { \"tool\": \"tools/hostsim/sim --load\", \"source\": ", fp);
        dump_jstr(fp, src);
        fputs(" },\n", fp);
    } else {
        fprintf(fp, "  \"generator\": { \"tool\": \"tools/hostsim/sim --dump\","
                    " \"layoutSeed\": %u },\n", lseed);
    }
    fprintf(fp, "  \"stage\": %u,\n", stage);
    fprintf(fp, "  \"loop\": %u,\n", loop_n);

    dump_grid(fp, "dug", dug_ch, 0);
    dump_grid(fp, "cell", cell_ch, 0);
    dump_grid(fp, "over", over_ch, 0);

    fprintf(fp, "  \"start\": [%u, %u],\n", start_c, start_r);
    fprintf(fp, "  \"nest\": [%u, %u],\n", nest_c, nest_r);
    if (bath_on) { fprintf(fp, "  \"bath\": [%u, %u],\n", bath_c, bath_r); }
    else         { fputs("  \"bath\": null,\n", fp); }
    if (spring_on) { fprintf(fp, "  \"spring\": [%u, %u],\n", spring_c, spring_r); }
    else           { fputs("  \"spring\": null,\n", fp); }
    if (cart_on) {
        fprintf(fp, "  \"cart\": { \"row\": %u, \"from\": %u, \"to\": %u },\n",
                cart_r, cart_c0, cart_c1);
    } else {
        fputs("  \"cart\": null,\n", fp);
    }

    fputs("  \"gas\": [", fp);
    for (k = 0; k < gas_n; k++) {
        fprintf(fp, "%s[%u, %u]", k ? ", " : "", gas_c[k], gas_r[k]);
    }
    fputs("],\n", fp);

    /* **岩の並び順。** `grid` の `O` を読む順 (左上から) と添字の順は一致しない。
       rock_* は添字で回すので、順が変わると当たりや落盤の見え方が変わりうる。 */
    fputs("  \"rockOrder\": [", fp);
    {
        int first = 1;
        for (k = 0; k < ROCK_MAX; k++) {
            if (rock_state[k] == ROCK_NONE) { continue; }
            c = (uint8_t)(rock_x[k] >> 4);
            r = (uint8_t)(rock_y[k] >> 4);
            if ((uint8_t)(c << 4) != rock_x[k] || (uint8_t)(r << 4) != rock_y[k]) {
                dump_die("岩 %u が升の境目に無い (組み立て直後の盤面ではない)", k);
            }
            if (rock_state[k] != ROCK_WAIT) {
                dump_die("岩 %u が静止していない (組み立て直後の盤面ではない)", k);
            }
            fprintf(fp, "%s[%u, %u]", first ? "" : ", ", c, r);
            first = 0;
        }
    }
    fputs("],\n", fp);

    /* **仕込んだ鉱脈。** 盤面には出ない覚書で、`--analyse` が「下ごしらえが
       要る鉱脈を何本仕込んだか」を言うのに使う。手で作った面には無くてよい。 */
    fputs("  \"veins\": [", fp);
    for (k = 0; k < gapv_n; k++) {
        fprintf(fp, "%s[%u, %u]", k ? ", " : "", gapv_c[k], gapv_r[k]);
    }
    fputs("]\n", fp);
    fputs("}\n", fp);
}

/* ================================================================== *
 * 読み込み (JSON の、この形式に要る分だけ)
 * ================================================================== */

typedef struct {
    const char *path;
    char       *buf;
    const char *p;
} JS;

static void jdie(JS *j, const char *fmt, ...)
{
    va_list ap;
    int line = 1;
    const char *q;
    for (q = j->buf; q < j->p; q++) {
        if (*q == '\n') { line++; }
    }
    fprintf(stderr, "%s:%d: ", j->path, line);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

static void ws(JS *j)
{
    while (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r') {
        j->p++;
    }
}

static int peek(JS *j) { ws(j); return (unsigned char)*j->p; }

static void want(JS *j, char c)
{
    if (peek(j) != (unsigned char)c) {
        jdie(j, "'%c' が要るところに '%c'", c, *j->p ? *j->p : '?');
    }
    j->p++;
}

static long jnum(JS *j)
{
    char *end;
    long v;
    ws(j);
    v = strtol(j->p, &end, 10);
    if (end == j->p) {
        jdie(j, "数が要る");
    }
    j->p = end;
    return v;
}

static void jstr(JS *j, char *out, size_t cap)
{
    size_t n = 0;
    want(j, '"');
    while (*j->p && *j->p != '"') {
        char c = *j->p++;
        if (c == '\\') {
            char e = *j->p;
            if (!e) { jdie(j, "文字列が閉じていない"); }
            j->p++;
            switch (e) {
            case '"': case '\\': case '/': c = e;    break;
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            default: jdie(j, "知らないエスケープ \\%c (\\u は未対応)", e);
            }
        }
        if (n + 1 >= cap) { jdie(j, "文字列が長すぎる"); }
        out[n++] = c;
    }
    if (*j->p != '"') { jdie(j, "文字列が閉じていない"); }
    j->p++;
    out[n] = '\0';
}

/* 中身を見ない読み飛ばし ("generator" に使う)。
   **入れ子に上限を付けてある。** `[[[[[...` を延々と書いたファイルで
   C のスタックを食い潰して落ちるのを防ぐ (読むのは人が書いたファイルだが、
   落ちるより「深すぎる」と言うほうがよい)。 */
static void jskip_d(JS *j, int depth)
{
    char t[1024];
    int c = peek(j);
    if (depth > 32) { jdie(j, "入れ子が深すぎる"); }
    if (c == '{' || c == '[') {
        char close = (c == '{') ? '}' : ']';
        j->p++;
        if (peek(j) == close) { j->p++; return; }
        for (;;) {
            if (close == '}') {
                jstr(j, t, sizeof t);
                want(j, ':');
            }
            jskip_d(j, depth + 1);
            if (peek(j) == ',') { j->p++; continue; }
            want(j, close);
            return;
        }
    }
    if (c == '"') { jstr(j, t, sizeof t); return; }
    if (c == 't' || c == 'f' || c == 'n') {
        while (*j->p >= 'a' && *j->p <= 'z') { j->p++; }
        return;
    }
    jnum(j);
}

static void jskip(JS *j) { jskip_d(j, 0); }

/* 11行×16列の板を読む。 */
static void jgrid(JS *j, char g[ROWS][COLS + 1], const char *name)
{
    char t[256];
    int r;
    want(j, '[');
    for (r = 0; r < ROWS; r++) {
        if (r) { want(j, ','); }
        jstr(j, t, sizeof t);
        if (strlen(t) != COLS) {
            jdie(j, "%s の %d 行目が %d 文字 (%d 文字でなければならない)",
                 name, r, (int)strlen(t), COLS);
        }
        memcpy(g[r], t, COLS + 1);
    }
    want(j, ']');
}

/* [c, r] を読む。null なら 0 を返す。 */
static int jcell(JS *j, uint8_t *c, uint8_t *r, const char *name)
{
    long cv, rv;
    if (peek(j) == 'n') {
        while (*j->p >= 'a' && *j->p <= 'z') { j->p++; }
        return 0;
    }
    want(j, '[');
    cv = jnum(j);
    want(j, ',');
    rv = jnum(j);
    want(j, ']');
    if (cv < 0 || cv >= COLS || rv < 0 || rv >= ROWS) {
        jdie(j, "%s の升 %ld,%ld が盤面の外", name, cv, rv);
    }
    *c = (uint8_t)cv;
    *r = (uint8_t)rv;
    return 1;
}

/* [[c,r], ...] を読む。返り値は個数。 */
static int jcells(JS *j, uint8_t *cs, uint8_t *rs, int cap, const char *name)
{
    int n = 0;
    want(j, '[');
    if (peek(j) == ']') { j->p++; return 0; }
    for (;;) {
        if (n >= cap) { jdie(j, "%s が %d 個を超えている", name, cap); }
        /* **null を弾く。** jcell() は null のとき何も書かずに 0 を返すので、
           返り値を見ないと `[null]` で**升を1つ数えたまま中身が未初期化**になり、
           あとで盤面の外を読む (`gas` なら cflag[] の外)。 */
        if (!jcell(j, &cs[n], &rs[n], name)) {
            jdie(j, "%s の一覧に null は書けない", name);
        }
        n++;
        if (peek(j) == ',') { j->p++; continue; }
        want(j, ']');
        return n;
    }
}

void stage_load(const char *path)
{
    JS j;
    FILE *fp;
    long  size;
    uint16_t i;
    uint8_t  c, r, k;

    /* 読んだ内容。省略された項目はここの既定値がそのまま効く。 */
    char dug[ROWS][COLS + 1], cell[ROWS][COLS + 1], over[ROWS][COLS + 1];
    int  have_dug = 0;
    long st = -1, lp = -1;
    uint8_t sc = 0, sr = 0, nc = 0, nr = 0;
    uint8_t bc = 0, br = 0, bon = 0, wc = 0, wr = 0, won = 0;
    int have_start = 0, have_nest = 0;
    long crow = -1, cfrom = 0, cto = 0;
    uint8_t gc[GAS_MAX], gr[GAS_MAX];
    int gn = 0;
    uint8_t roc[ROCK_MAX], ror[ROCK_MAX];
    int ron = -1;
    uint8_t vcs[2], vrs[2];
    int vn = 0;
    /* cell から読み取った岩 (左上からの順)。rockOrder が無ければこの順で置く。 */
    uint8_t rkc[ROCK_MAX], rkr[ROCK_MAX];
    int rkn = 0;

    memset(cell, ' ', sizeof cell);
    memset(over, ' ', sizeof over);
    for (r = 0; r < ROWS; r++) { cell[r][COLS] = '\0'; over[r][COLS] = '\0'; }

    fp = fopen(path, "rb");
    if (!fp) { fprintf(stderr, "%s を開けません\n", path); exit(1); }
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size < 0 || size > 1 << 20) { fprintf(stderr, "%s の大きさが変です\n", path); exit(1); }
    j.buf = malloc((size_t)size + 1);
    if (!j.buf) { fprintf(stderr, "記憶が足りません\n"); exit(1); }
    if (fread(j.buf, 1, (size_t)size, fp) != (size_t)size) {
        fprintf(stderr, "%s を読めません\n", path); exit(1);
    }
    j.buf[size] = '\0';
    j.path = path;
    j.p = j.buf;

    /* ---- 表書き ---- */
    want(&j, '{');
    for (;;) {
        char key[64], sv[64];
        jstr(&j, key, sizeof key);
        want(&j, ':');
        if (!strcmp(key, "format")) {
            jstr(&j, sv, sizeof sv);
            if (strcmp(sv, "sakiyama-stage")) { jdie(&j, "format が \"%s\" (sakiyama-stage でなければならない)", sv); }
        } else if (!strcmp(key, "formatVersion")) {
            if (jnum(&j) != 1) { jdie(&j, "formatVersion が 1 ではない"); }
        } else if (!strcmp(key, "generator")) {
            jskip(&j);
        } else if (!strcmp(key, "stage")) {
            st = jnum(&j);
            /* **1〜MAX_STAGE に限る。** 面番号は敵の強さ・持ち時間・土の色を
               引くのに使うので、無い面の番号を入れても意味を持たない。 */
            if (st < 1 || st > MAX_STAGE) { jdie(&j, "stage が %ld (1〜%d)", st, MAX_STAGE); }
        } else if (!strcmp(key, "loop")) {
            lp = jnum(&j);
            if (lp < 0 || lp > 1) { jdie(&j, "loop が %ld (0 か 1)", lp); }
        } else if (!strcmp(key, "dug")) {
            jgrid(&j, dug, "dug");
            have_dug = 1;
        } else if (!strcmp(key, "cell")) {
            jgrid(&j, cell, "cell");
        } else if (!strcmp(key, "over")) {
            jgrid(&j, over, "over");
        } else if (!strcmp(key, "start")) {
            have_start = jcell(&j, &sc, &sr, "start");
            if (!have_start) { jdie(&j, "start は省略できない"); }
        } else if (!strcmp(key, "nest")) {
            have_nest = jcell(&j, &nc, &nr, "nest");
            if (!have_nest) { jdie(&j, "nest は省略できない"); }
        } else if (!strcmp(key, "bath")) {
            bon = (uint8_t)jcell(&j, &bc, &br, "bath");
        } else if (!strcmp(key, "spring")) {
            won = (uint8_t)jcell(&j, &wc, &wr, "spring");
        } else if (!strcmp(key, "cart")) {
            if (peek(&j) == 'n') {
                while (*j.p >= 'a' && *j.p <= 'z') { j.p++; }
            } else {
                long got = 0;
                want(&j, '{');
                for (;;) {
                    char ck[32];
                    jstr(&j, ck, sizeof ck);
                    want(&j, ':');
                    if (!strcmp(ck, "row"))       { crow = jnum(&j); got |= 1; }
                    else if (!strcmp(ck, "from")) { cfrom = jnum(&j); got |= 2; }
                    else if (!strcmp(ck, "to"))   { cto = jnum(&j); got |= 4; }
                    else { jdie(&j, "cart に知らない項目 \"%s\"", ck); }
                    if (peek(&j) == ',') { j.p++; continue; }
                    want(&j, '}');
                    break;
                }
                if (got != 7) { jdie(&j, "cart は row / from / to の3つが要る"); }
                if (crow < 0 || crow >= ROWS || cfrom < 0 || cto >= COLS || cfrom > cto) {
                    jdie(&j, "cart の行か列が盤面の外");
                }
            }
        } else if (!strcmp(key, "gas")) {
            gn = jcells(&j, gc, gr, GAS_MAX, "gas");
        } else if (!strcmp(key, "rockOrder")) {
            ron = jcells(&j, roc, ror, ROCK_MAX, "rockOrder");
        } else if (!strcmp(key, "veins")) {
            vn = jcells(&j, vcs, vrs, 2, "veins");
        } else {
            jdie(&j, "知らない項目 \"%s\"", key);
        }
        if (peek(&j) == ',') { j.p++; continue; }
        want(&j, '}');
        break;
    }
    if (peek(&j) != 0) { jdie(&j, "object の後ろに余りがある"); }
    if (st < 0)      { jdie(&j, "stage が無い"); }
    if (!have_dug)   { jdie(&j, "dug が無い"); }
    if (!have_start) { jdie(&j, "start が無い"); }
    if (!have_nest)  { jdie(&j, "nest が無い"); }

    /* ---- 盤面をまっさらにする (build_stage() の冒頭と同じ) ---- */
    for (i = 0; i < NCELL; i++) {
        cflag[i] = 0;
        coal[i]  = 0;
        chute[i] = 0;
    }
    rock_clear();
    water_reset();
    cart_reset();
    stage = (uint8_t)st;
    if (lp >= 0) { loop_n = (uint8_t)lp; }   /* 省略時は --ura の指定をそのまま使う */
    set_dirt_palette();

    /* ---- 掘ってあるか ---- */
    for (r = 0; r < ROWS; r++) {
        for (c = 0; c < COLS; c++) {
            i = (uint16_t)r * COLS + c;
            if (dug[r][c] == ' ')      { cf_set(i, CF_DUG); }
            else if (dug[r][c] != '.') { jdie(&j, "dug の %u 行 %u 列に知らない文字 '%c' ('.' か空白)", r, c, dug[r][c]); }
        }
    }

    /* ---- 升の中身 ---- */
    for (r = 0; r < ROWS; r++) {
        for (c = 0; c < COLS; c++) {
            char ch = cell[r][c];
            const char *q;
            i = (uint16_t)r * COLS + c;
            switch (ch) {
            case ' ': break;                                      /* 何も無い */
            case 'x': coal[i] = COAL_CRACKED; break;              /* 割れた黒ダイヤ */
            case '>': chute[i] = CHUTE_R; break;
            case '<': chute[i] = CHUTE_L; break;
            case 'O': case 'o':
                if (rkn >= ROCK_MAX) { jdie(&j, "岩が %d 個を超えている", ROCK_MAX); }
                rkc[rkn] = c;
                rkr[rkn] = r;
                rkn++;
                break;
            default:
                q = strchr(HP_CHARS, ch);
                if (!q) { jdie(&j, "cell の %u 行 %u 列に知らない文字 '%c'", r, c, ch); }
                coal[i] = (uint8_t)(q - HP_CHARS + 1);
                break;
            }
        }
    }

    /* ---- 坑道・黒ダイヤの上に乗るもの ---- */
    for (r = 0; r < ROWS; r++) {
        for (c = 0; c < COLS; c++) {
            i = (uint16_t)r * COLS + c;
            switch (over[r][c]) {
            case ' ': break;
            case '*':
                if (!coal[i]) { jdie(&j, "over の %u 行 %u 列: 炭柱 '*' は黒ダイヤの升にだけ置ける", r, c); }
                cf_set(i, CF_PILLAR);
                break;
            case 'p':
                if (!cf_on(i, CF_DUG)) { jdie(&j, "over の %u 行 %u 列: 火薬 'p' は坑道にだけ置ける", r, c); }
                cf_set(i, CF_POWDER);
                break;
            case '~':
                if (!cf_on(i, CF_DUG)) { jdie(&j, "over の %u 行 %u 列: 溜まり湯 '~' は坑道にだけ置ける", r, c); }
                water[i] = WATER_POOL;
                break;
            default:
                jdie(&j, "over の %u 行 %u 列に知らない文字 '%c'", r, c, over[r][c]);
            }
        }
    }

    /* ---- 岩。並べる順は rockOrder が決める ---- */
    if (ron >= 0) {
        if (ron != rkn) {
            jdie(&j, "rockOrder が %d 個で cell の岩が %d 個", ron, rkn);
        }
        for (k = 0; k < (uint8_t)ron; k++) {
            char ch = cell[ror[k]][roc[k]];
            int m;
            if (ch != 'O' && ch != 'o') {
                jdie(&j, "rockOrder の升 %u,%u に cell の岩が無い", roc[k], ror[k]);
            }
            for (m = 0; m < (int)k; m++) {
                if (roc[m] == roc[k] && ror[m] == ror[k]) {
                    jdie(&j, "rockOrder に升 %u,%u が2回出ている", roc[k], ror[k]);
                }
            }
        }
        memcpy(rkc, roc, (size_t)ron);
        memcpy(rkr, ror, (size_t)ron);
    }
    for (k = 0; k < (uint8_t)rkn; k++) {
        int8_t ri;
        rock_place(rkc[k], rkr[k]);
        ri = rock_at((int8_t)rkc[k], (int8_t)rkr[k]);
        if (ri >= 0 && cell[rkr[k]][rkc[k]] == 'o') {
            rock_prop[(uint8_t)ri] = 1;        /* 支柱で止めてある岩 */
        }
    }

    /* ---- 一点物。どれも坑道の上にあること ---- */
    start_c = sc; start_r = sr;
    nest_c  = nc; nest_r  = nr;
    if (!cf_on((uint16_t)sr * COLS + sc, CF_DUG)) { jdie(&j, "start の升 %u,%u が坑道ではない", sc, sr); }
    if (!cf_on((uint16_t)nr * COLS + nc, CF_DUG)) { jdie(&j, "nest の升 %u,%u が坑道ではない", nc, nr); }
    bath_on = bon;
    if (bon) {
        bath_c = bc; bath_r = br;
        if (!cf_on((uint16_t)br * COLS + bc, CF_DUG)) { jdie(&j, "bath の升 %u,%u が坑道ではない", bc, br); }
    }
    spring_on = won;
    if (won) {
        spring_c = wc; spring_r = wr;
        if (!cf_on((uint16_t)wr * COLS + wc, CF_DUG)) { jdie(&j, "spring の升 %u,%u が坑道ではない", wc, wr); }
    }
    if (crow >= 0) {
        for (c = (uint8_t)cfrom; c <= (uint8_t)cto; c++) {
            if (!cf_on((uint16_t)crow * COLS + c, CF_DUG)) {
                jdie(&j, "cart のレールの升 %u,%ld が坑道ではない", c, crow);
            }
        }
        cart_place((uint8_t)cfrom, (uint8_t)cto, (uint8_t)crow);
    }
    gas_n = (uint8_t)gn;
    for (k = 0; k < (uint8_t)gn; k++) {
        gas_c[k] = gc[k];
        gas_r[k] = gr[k];
        if (!cf_on((uint16_t)gr[k] * COLS + gc[k], CF_DUG)) {
            jdie(&j, "gas の升 %u,%u が坑道ではない", gc[k], gr[k]);
        }
    }
    gapv_n = (uint8_t)vn;
    for (k = 0; k < (uint8_t)vn; k++) {
        gapv_c[k] = vcs[k];
        gapv_r[k] = vrs[k];
    }

    /* ---- 導出。build_stage() の後半とまったく同じ ---- */
    /* 膜: 面の最初から掘れている升どうしは繋がっている扱い。 */
    for (r = 0; r < ROWS; r++) {
        for (c = 0; c < COLS; c++) {
            uint16_t m = (uint16_t)r * COLS + c;
            if (!cf_on(m, CF_DUG)) { continue; }
            if (c + 1 < COLS && cf_on(m + 1, CF_DUG))    { cf_set(m, LINK_RIGHT); }
            if (r + 1 < ROWS && cf_on(m + COLS, CF_DUG)) { cf_set(m, LINK_DOWN); }
        }
    }
    dug_n = 0;
    coal_left = 0;
    for (i = 0; i < NCELL; i++) {
        if (cf_on(i, CF_DUG)) { dug_n++; }
        if (coal[i])          { coal_left++; }
    }
    dug_pct = (uint8_t)((dug_n * 100u) / NCELL);
    dug_n0  = dug_n;

    free(j.buf);
}

/* ================================================================== *
 * 盤面を決める変数の一覧 (往復の検証用)
 * ================================================================== */

void stage_state(FILE *fp)
{
    uint16_t i;
    uint8_t  k;

    fprintf(fp, "stage=%u loop=%u\n", stage, loop_n);
    fputs("cflag=", fp);
    for (i = 0; i < NCELL; i++) { fprintf(fp, "%02X", cflag[i]); }
    fputs("\ncoal=", fp);
    for (i = 0; i < NCELL; i++) { fprintf(fp, "%02X", coal[i]); }
    fputs("\nchute=", fp);
    for (i = 0; i < NCELL; i++) { fprintf(fp, "%X", chute[i]); }
    fputs("\nwater=", fp);
    for (i = 0; i < NCELL; i++) { fprintf(fp, "%X", water[i]); }
    fputc('\n', fp);
    for (k = 0; k < ROCK_MAX; k++) {
        if (rock_state[k] == ROCK_NONE) {
            fprintf(fp, "rock%u=none\n", k);
        } else {
            fprintf(fp, "rock%u=%u,%u,%u,%u\n", k,
                    rock_state[k], rock_x[k], rock_y[k], rock_prop[k]);
        }
    }
    fprintf(fp, "start=%u,%u\nnest=%u,%u\n", start_c, start_r, nest_c, nest_r);
    /* **消えているものの座標は見ない。** water_reset()/cart_reset() は旗だけを
       倒して座標を残すので、前の面の値がそのまま入っている。 */
    if (bath_on)   { fprintf(fp, "bath=%u,%u\n", bath_c, bath_r); }     else { fputs("bath=off\n", fp); }
    if (spring_on) { fprintf(fp, "spring=%u,%u\n", spring_c, spring_r); } else { fputs("spring=off\n", fp); }
    if (cart_on)   { fprintf(fp, "cart=%u,%u,%u\n", cart_r, cart_c0, cart_c1); } else { fputs("cart=off\n", fp); }
    fprintf(fp, "water_on=%u water_done=%u\n", water_on, water_done);
    fputs("gas=", fp);
    for (k = 0; k < gas_n; k++) { fprintf(fp, "%s%u,%u", k ? " " : "", gas_c[k], gas_r[k]); }
    fputs("\nveins=", fp);
    for (k = 0; k < gapv_n; k++) { fprintf(fp, "%s%u,%u", k ? " " : "", gapv_c[k], gapv_r[k]); }
    fprintf(fp, "\ndug_n=%u dug_n0=%u dug_pct=%u coal_left=%u\n",
            dug_n, dug_n0, dug_pct, coal_left);
}
