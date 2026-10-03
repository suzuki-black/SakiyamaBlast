/*
 * SakiyamaBlast - 岩 (Mr. Do! の apple 準拠)
 *
 * 状態機械は APPLEWAIT / APPLEJIG / APPLEFALL / APPLESPLIT の4状態に対応する。
 * 根拠と出典は docs/MRDO_RESEARCH.md。要点:
 *  - 支え判定は「真下のセル(地形)」に加えて **自機そのもの**も見る (Dig Dug 準拠)。
 *    真下の升目に自機が居る間、岩は揺れも落下もしない。好きなだけ待てる。
 *    出典: "The rock will only start to fall once Dig Dug moves out from under it.
 *    Until such time, he can wait underneath the rock for as long as he likes."
 *  - 自機が真下から離れた瞬間に揺れ始め、揺れ切ると落下する。
 *  - 押しは横方向のみ。押し先が開いている(掘り済み)セルの時だけ。土へは押し込めない。
 *    **押し込んだ先に居る敵は潰れる**（落とさなくても岩を武器にできる）。押した岩は割れない。
 *  - 自身の高さ超 (2セル以上) 落ちるか、何かを潰すと割れて消える。1セルなら割れず存続。
 *
 * 本作の追加は **樋 (とい)** だけ。落ちてきた岩が樋の升を通ると、板を壊して
 * 傾いている方向へ1升ずれ、そこから落下を続ける (docs/GAME_RULES.md §6)。
 * 岩に付いていた沈み込み・横滑り・衝撃波・倒れ込みは全部これに置き換えた。
 */
#include "game.h"
#include "sprite.h"
#include "sound.h"
#include "art.h"

uint8_t rock_state[ROCK_MAX];
uint8_t rock_x[ROCK_MAX], rock_y[ROCK_MAX];

static uint8_t jig_t[ROCK_MAX];     /* 揺れの残りフレーム */
static uint8_t pushed[ROCK_MAX];    /* 押されて動いた直後か。揺れの長さを変える */
static uint8_t chute_n[ROCK_MAX];   /* 樋で横へずれる残りドット。0 = ずれていない */
static int8_t  chute_d[ROCK_MAX];   /* ずれる向き (板の傾き) */
static uint8_t fall_r0[ROCK_MAX];   /* 落下を開始した行 (落下距離で割れ判定) */
static uint8_t crushed[ROCK_MAX];   /* 落下中に何かを潰したか */
/* 1回の落下で潰した敵の数。**升ごとに1体ずつ計上すると、縦一列をまとめて
   潰しても1体ずつ倒したのと同じ点数にしかならない。** 落ち切ってから
   まとめて score_kills() に渡すことで、倍々の表がやっと効く。 */
static uint8_t kills[ROCK_MAX];
static uint8_t kill_c[ROCK_MAX], kill_r[ROCK_MAX];   /* 最初に潰した升 (得点を出す場所) */
static uint8_t split_t[ROCK_MAX];   /* 割れた姿を出しておく残りフレーム */

static uint8_t collapse_pend;   /* 予約された落盤。説明は rock_collapse_later */

void rock_clear(void)
{
    uint8_t i;
    collapse_pend = 0;          /* **BSS は零で始まらない。** 面の頭で必ず落とす */
    for (i = 0; i < ROCK_MAX; i++) {
        rock_state[i] = ROCK_NONE;
        chute_n[i] = 0;
        spr_park((uint8_t)(SPR_ROCK + i));
    }
}

void rock_place(uint8_t c, uint8_t r)
{
    uint8_t i;
    for (i = 0; i < ROCK_MAX; i++) {
        if (rock_state[i] == ROCK_NONE) {
            rock_x[i] = (uint8_t)(c << 4);
            rock_y[i] = (uint8_t)(r << 4);
            rock_state[i] = ROCK_WAIT;
            jig_t[i] = 0;
            pushed[i] = 0;
            crushed[i] = 0;
            chute_n[i] = 0;
            spr_color_lines((uint8_t)(SPR_ROCK + i), art_colours(PAT_ROCK));
            return;
        }
    }
}

/* **岩は横にドット単位で動く。** だから升は中心で決める。半分を越えた時点で
   次の升の住人になり、支えの判定もそこで切り替わる
   (Mr. Do! の「縁からせり出しても、越えるまでは落ちない」がこれで出る)。 */
static int8_t rcx(uint8_t i)
{
    return (int8_t)(((uint16_t)rock_x[i] + 8) >> 4);
}

/* (x,y) に居るものが dx の向きへ進むとき、押すことになる岩。
   升ではなくドットで見る。**升で見ると、岩が升の半分を越えた瞬間に
   見失って押し手だけが進み、めり込む。** */
int8_t rock_ahead_of(uint8_t x, uint8_t y, int8_t dx)
{
    uint8_t i;
    int8_t r = (int8_t)(y >> 4);
    for (i = 0; i < ROCK_MAX; i++) {
        if (rock_state[i] != ROCK_WAIT && rock_state[i] != ROCK_JIG) {
            continue;
        }
        if ((int8_t)(rock_y[i] >> 4) != r) {
            continue;
        }
        if (dx > 0 && rock_x[i] > x && (uint8_t)(rock_x[i] - x) <= 16) {
            return (int8_t)i;
        }
        if (dx < 0 && rock_x[i] < x && (uint8_t)(x - rock_x[i]) <= 16) {
            return (int8_t)i;
        }
    }
    return -1;
}

int8_t rock_at(int8_t cx, int8_t cy)
{
    uint8_t i;
    if (cx < 0 || cx >= COLS || cy < 0 || cy >= ROWS) {
        return -1;
    }
    for (i = 0; i < ROCK_MAX; i++) {
        if (rock_state[i] != ROCK_NONE && rock_state[i] != ROCK_SPLIT) {
            if (rcx(i) == cx && (int8_t)(rock_y[i] >> 4) == cy) {
                return (int8_t)i;
            }
        }
    }
    return -1;
}

/* 落下中の岩は支えにならない。静止している岩だけを支えとして数える。 */
static uint8_t rock_solid_at(int8_t cx, int8_t cy)
{
    int8_t i = rock_at(cx, cy);
    if (i < 0) {
        return 0;
    }
    /* 落下中の岩は支えにならない。 */
    return (uint8_t)(rock_state[i] != ROCK_FALL);
}

/* 真下が支えられているか。地形(未掘の土)・盤面の底・静止した岩に加えて、
   **真下の升目に自機が居る場合も支え**とする (Dig Dug 準拠)。 */
static uint8_t supported(uint8_t i)
{
    int8_t c = rcx(i);
    int8_t r = (int8_t)(rock_y[i] >> 4);
    if (r + 1 >= ROWS) {
        return 1;
    }
    if (!cell_dug(c, (int8_t)(r + 1))) {
        return 1;
    }
    if ((int8_t)player_cell_c() == c && (int8_t)player_cell_r() == (int8_t)(r + 1)) {
        return 1;                       /* 自機が真下で支えている */
    }
    if (cart_at(c, (int8_t)(r + 1))) {
        return 1;                       /* **鉱車も支え。** 下を通っている間だけ持つ */
    }
    return rock_solid_at(c, (int8_t)(r + 1));
}

/* into_dirt=1 のときだけ土の中へ押し込める。**これを許すのは自機だけ。**
   敵にも許すと、敵が岩を土の中へ好き勝手に押し出してしまい、
   「自分で岩の X 座標を決めて連鎖を組む」という遊びが成立しなくなる。 */
uint8_t rock_can_push(int8_t i, int8_t dx, uint8_t into_dirt)
{
    int8_t r, nc;
    if (i < 0 || rock_state[i] == ROCK_NONE || rock_state[i] == ROCK_FALL) {
        return 0;                       /* 落ちている岩は押せない */
    }
    r = (int8_t)(rock_y[i] >> 4);
    /* **これから踏み込む升は「進む側の端」で決める。中心ではない。**
       中心で見ると、中心が隣の升へ入れないぶん**半升 (8ドット) 手前で止まり**、
       岩と壁のあいだに隙間が残る。支え (supported) は中心で見るのが正しいが
       (Mr. Do! の「縁を越えたら落ちる」)、ぶつかり判定は端で見る。 */
    nc = (dx > 0) ? (int8_t)(((uint16_t)rock_x[i] + 16) >> 4)
                  : (int8_t)(((int16_t)rock_x[i] - 1) >> 4);
    if (nc < 0 || nc >= COLS) {
        return 0;                       /* 場外へは押せない */
    }
    /* **土の中へも押し込める (Mr. Do! 準拠)。** 押し込んだ先は土のまま残り、
       岩が退いた升は自機が入って掘る。これで岩の X 座標を好きな位置へ運べる。 */
    if (cell_coal(nc, r)) {
        return 0;                       /* 黒ダイヤ・炭柱は押しのけられない */
    }
    if (!into_dirt && !cell_dug(nc, r)) {
        return 0;                       /* 敵は坑道の中でしか岩を動かせない */
    }
    {
        /* **自分自身を数えないこと。** 升は中心で決めるので、8ドット進んだ時点で
           自分の中心が nc の升へ入り、`rock_at(nc)` が**自分を返して**しまう。
           そのままだと、どの岩も半升だけ動いて止まる。 */
        int8_t o = rock_at(nc, r);
        if (o >= 0 && o != i) {
            return 0;                   /* 別の岩があれば押せない */
        }
    }
    if ((int8_t)player_cell_c() == nc && (int8_t)player_cell_r() == r) {
        return 0;                       /* 敵が押してきても、横押しでは潰されない */
    }
    return enemy_shove(nc, r, dx, 0);   /* 敵が居たら下がらせる。下がれなければ押せない */
}

uint8_t rock_push(int8_t i, int8_t dx, uint8_t into_dirt)
{
    int8_t r, nc;
    if (!rock_can_push(i, dx, into_dirt)) {
        return 0;
    }
    r = (int8_t)(rock_y[i] >> 4);
    nc = (int8_t)(rcx(i) + dx);
    /* **横押しでは何も潰れない (Mr. Do! 準拠)。** 敵は後ろへ下がる。
       潰せるのは落ちてきた岩だけで、押しは「岩を運ぶ」ためだけの操作。
       敵はもう nc に居なくなるので、毎ドット呼んでも二重には効かない。 */
    enemy_shove(nc, r, dx, 1);
    /* **1ドットだけ動かす。** 押し手 (自機・敵) が1ドット進むたびに呼ばれるので、
       升単位で瞬間移動せず、押し手と同じ速さで並んで動く。 */
    /* **中心が升の境目を越えた瞬間に「カチッ」。**
       支えは中心で判定する (rcx) ので、この一点が「押し出したら落ちる/落ちない」の
       分かれ目なのに、完全に不可視だった。しかも押して越えた場合の揺れは
       ROCK_JIG_PUSH 6フレームしかなく、目で捉える猶予がほとんど無い。
       耳で計れるようにすると、Mr. Do! 由来の「運んで仕掛ける」が成立しやすくなる。
       **落ちる場合も落ちない場合も鳴らす** (判定そのものの音であって結果の音ではない)。
       **自機の押しだけ。** 敵も岩を押すので、敵のぶんまで鳴らすと音が濁る。 */
    {
        int8_t c0 = rcx(i);
        rock_x[i] = (uint8_t)((uint8_t)rock_x[i] + (uint8_t)dx);
        if (into_dirt && rcx(i) != c0) {
            snd_sfx(SFX_CLICK);
        }
    }
    pushed[i] = 1;
    /* **状態には触らない。** 1ドットごとに ROCK_WAIT へ戻して jig_t を 0 にすると、
       押している間ずっと秒読みが振り出しに戻り、**支えの無い升の上を素通りして
       落ちなくなる。** 支えの有無は rock_update() が毎フレーム見ているので、
       縁を越えれば勝手に揺れ始めて落ちる (Mr. Do! の「押して落とす」がこれ)。 */
    return 1;
}

/* 割れる。**すぐには消さない。** 即座に消えると何が起きたのか読み取れないので、
   軽く画面を揺らし、二つに割れた姿を ROCK_SPLIT_FRAMES だけ見せてから消す。
   その間は支えにも障害物にもならない (rock_at が拾わない)。 */
/* 1回の落下で潰した数をまとめて計上する。まとめて潰すほど倍々になる。 */
static void cash_kills(uint8_t i)
{
    if (!kills[i]) {
        return;
    }
    score_kills(kills[i], 1);           /* 落下岩は爆風の倍 */
    score_popup(kill_c[i], kill_r[i], kill_value(kills[i], 1));
    kills[i] = 0;
}

static void rock_break(uint8_t i)
{
    cash_kills(i);
    rock_state[i] = ROCK_SPLIT;
    split_t[i] = ROCK_SPLIT_FRAMES;
    chute_n[i] = 0;
    field_shake(ROCK_LAND_SHAKE);
    /* 割れた絵は岩と行ごとの色が違うので、色表も差し替える。 */
    spr_color_lines((uint8_t)(SPR_ROCK + i), art_colours(PAT_SPLIT));
    bonus_rock_broken();               /* 2個割るとボーナスが出る */
}

void rock_destroy_at(int8_t cx, int8_t cy)
{
    int8_t i = rock_at(cx, cy);
    if (i >= 0) {
        rock_break((uint8_t)i);
    }
}

/* 落下中の岩が自機に重なっているか (16px 同士のおおよその重なり) */
static uint8_t hits_player(uint8_t i)
{
    int16_t dx = (int16_t)rock_x[i] - (int16_t)px;
    int16_t dy = (int16_t)rock_y[i] - (int16_t)py;
    if (dx < 0) { dx = (int16_t)(-dx); }
    if (dy < 0) { dy = (int16_t)(-dy); }
    return (uint8_t)((dx < 13) && (dy < 13));
}

/*
 * 樋 (とい) の升へ落ちてきた。**板は必ず壊れる。曲がれれば曲がる。**
 *
 * 曲がった先が塞がっている (場外・土・黒ダイヤ) なら、曲がらずそのまま真下へ
 * 落ち続ける。土で止めるのは「掘っておけば曲がる」を成立させるため
 * (掘るという既存の動詞だけで経路を作れる)。
 * 曲がった先に岩が居たら、規則どおり**落ちてきたほう**が割れる。
 *
 * 戻り値: 1 = 岩がもう居ない (割れた)。呼び出し側はそこで打ち切る。
 */
static void rock_enter_chute(uint8_t i, int8_t c, int8_t r)
{
    int8_t d = (cell_chute(c, r) == CHUTE_R) ? 1 : -1;
    int8_t nc = (int8_t)(c + d);
    chute_break(c, r);                  /* 通れば必ず壊れる */
    /* **曲がった先を塞ぐものに岩も含める。** 横から押しても岩は岩を通り抜けられない
       ので、押しの挙動と一致する。塞がっていれば曲がらず、そのまま真下へ落ちる
       (板は壊れたまま残るので、真っ直ぐ落ちた理由が画面に出ている)。 */
    if (nc < 0 || nc >= COLS || !cell_dug(nc, r) || cell_coal(nc, r)
        || rock_at(nc, r) >= 0) {
        return;
    }
    chute_d[i] = d;
    chute_n[i] = CELL;
}

static void rock_fall_step(uint8_t i)
{
    int8_t c, r;

    /* **樋を滑っている間は落ちない。** 板の上を横へ1升ぶん移ってから、
       同じフレームの続きで落下判定へ戻る。行は変わらないので、
       この1升は落下距離に数えられない (規則どおり)。 */
    if (chute_n[i]) {
        rock_x[i] = (uint8_t)((uint8_t)rock_x[i] + (uint8_t)(chute_d[i] * ROCK_TOI_SPD));
        chute_n[i] = (uint8_t)(chute_n[i] - ROCK_TOI_SPD);
    } else {
        rock_y[i] = (uint8_t)(rock_y[i] + ROCK_FALL_SPD);
        if (rock_y[i] > (uint8_t)YMAX) {
            rock_y[i] = (uint8_t)YMAX;
        }
    }

    if (!invuln && !dead && hits_player(i)) {
        dead = 1;
        crushed[i] = 1;
    }
    /* 落下中の岩は縦一列の敵をまとめて潰す (Mr. Do! 準拠) */
    {
        uint8_t killed = enemy_kill_at(rcx(i),
                                       (int8_t)((rock_y[i] + 8) >> 4), DROP_BY_ROCK);
        if (killed) {
            if (!kills[i]) {                     /* 得点は最初に潰した升へ出す */
                kill_c[i] = (uint8_t)rcx(i);
                kill_r[i] = (uint8_t)((rock_y[i] + 8) >> 4);
            }
            crushed[i] = 1;
            kills[i] = (uint8_t)(kills[i] + killed);
        }
    }

    if (chute_n[i]) {
        return;                         /* まだ板の上。着地判定はしない */
    }
    if (rock_y[i] & 15) {
        return;                         /* セル境界でだけ着地判定 */
    }
    c = rcx(i);
    r = (int8_t)(rock_y[i] >> 4);
    link_smash((uint8_t)c, (uint8_t)r); /* 通り抜けた升の間の膜を割る */
    /* **落ちてきた岩は水風呂を壊さない。** 押しでは風呂の升へ入れないのに
       落とせば壊れる、では筋が通らない。壊れると体温の逃げ場が無くなって面が
       一気に詰むうえ、壊した理由がプレイヤーに伝わらない。岩は風呂の上に
       乗るだけで、横に押しのければまた使える。 */
    /* **落ちている岩は黒ダイヤを突き抜け、その場で採ってしまう。**
       炭層の帯を縦に貫くので、1枚ぶんの手間 (岩の真下を割る) で帯ごと持っていける。
       採ったぶんは連鎖に乗るので、倍率が上から下へ伸びていく。 */
    /* **見るのは「真下」ではなく「いま入った升」。** 境界判定は升に着いてから走るので、
       真下を見ていると最初に飛び込む1升が検査されず、そこだけ採り損ねる。 */
    /* **ここで return しないこと。** 以前は「貫いたのだから落ち続ける」として
       即 return していたが、そうすると**真下が土でも1升めり込んで**しまい、
       炭層の下に土がある形で岩が土の中へ潜り込んでいた。
       炭を貫いても、続くかどうかは下の支えを見て決める (下の判定へ落とす)。 */
    if (coal_smash(c, r)) {
        crushed[i] = 1;             /* 仕事をしたので着地で割れて消える */
    }
    /* 樋。**升は面の最初から開いている**ので、落ちてきた岩は必ずここへ入る。 */
    if (cell_chute(c, r)) {
        rock_enter_chute(i, c, r);
        if (chute_n[i]) {
            return;                 /* これから横へ1升ずれる */
        }
    }
    /* **落ちている岩は黒ダイヤを突き抜ける (規則3。連鎖の心臓)。**
       支えとして黒ダイヤを数えるのは**静止している岩だけ** (supported)。
       落下中は貫きながら採るので、炭層の帯は**1枚ぶんの手間 (岩の真下を割る)**
       で帯ごと持っていける。「割る＝通す / 割らない＝支える」はこれで両立する。 */
    if (r + 1 < ROWS && (cell_dug(c, (int8_t)(r + 1)) || cell_coal(c, (int8_t)(r + 1)))
        && !rock_solid_at(c, (int8_t)(r + 1))) {
        return;                         /* まだ落ちる */
    }
    /* **落ちてきた岩が岩に着地したら、落ちてきたほうが割れる。**
       押して岩の上に乗せた場合は割れない (この判定は落下中にしか走らないので、
       押して積むのはそのまま通る)。積めることには使い道があって、
       下の岩の支えを外せば**時間差の縦連鎖**をプレイヤーが自分で仕込める。 */
    if (r + 1 < ROWS && rock_at(c, (int8_t)(r + 1)) >= 0) {
        rock_break(i);
        return;
    }
    /* 着地。自身の高さ超 (2セル以上) 落ちたか、何かを潰したなら割れて消える。 */
    if (crushed[i] || (uint8_t)(r - fall_r0[i]) >= 2) {
        rock_break(i);
    } else {
        cash_kills(i);
        rock_state[i] = ROCK_WAIT;
        jig_t[i] = 0;
        /* 岩がその升に居座る場合だけ、下敷きの割れた黒ダイヤを砕き出しておく。
           岩の升には自機が入れないので、放っておくと回収できずクリア不能になる。
           炭柱 (岩の真下の黒ダイヤ) を割ると必ずこの形になる。 */
        coal_crush_at(c, r);
    }
}

/*
 * **炭柱が消えたら、誰が壊しても天盤は落ちる。**
 * つるはしで割り切ったときだけでなく、**落ちてきた岩が炭柱を貫いて消したとき**も
 * 落盤にする。以前は後者を外してあり (岩の処理の最中に落盤を呼ぶと、落とす岩を
 * 数えている途中で状態が変わる)、**同じ「炭柱が無くなった」でも結果が違っていた。**
 * 面7では出水の引き金もこれなので、「炭柱を割ったのに水が出ない」ことがあった
 * (作者の指摘、2026-09-27)。
 *
 * **その場では呼ばず、1回ぶん遅らせる。** rock_update() が岩を1つずつ見ている
 * 最中に落盤を起こすと、まだ見ていない岩の状態を書き換えてしまう。
 * 予約しておいて、全部見終わってから起こす。
 * **BSS は零で始まらない**ので、面を組むたびに rock_clear() で落としている。
 */
void rock_collapse_later(void)
{
    collapse_pend = 1;
}

/*
 * 炭柱が壊れたときの落盤。残柱式で天盤を支えていた柱が無くなるので、
 * **黒ダイヤに載っている岩が一斉に支えを失って落ちる。**
 *
 * **岩は土を突き抜けない。** 「N升沈む」も「土を掘りながら落ちる」もどちらも
 * 廃止した。前者は数字が盤面に見えず、後者は**掘っていない場所を岩が落ちていく**
 * ので、爽快感より違和感になる (作者の指摘、2026-09-11)。
 * 落盤で動くのは**黒ダイヤの上に載っている岩だけ**で、土の中の岩はその場に残る。
 *
 * 岩は `keep_above_coal()` で黒ダイヤの真上へ寄せて置いてあるので、
 * ほとんどの岩がこれで落ちる。落ちた岩は炭層の帯を貫いて採るので、
 * 炭柱ひとつが「盤面のあちこちで縦の連鎖が同時に走る」合図になる。
 */
void rock_collapse(void)
{
    uint8_t i, c;
    collapse_pend = 0;
    for (i = 0; i < ROCK_MAX; i++) {
        uint8_t r;
        if (rock_state[i] != ROCK_WAIT && rock_state[i] != ROCK_JIG) {
            continue;                   /* 落下中・割れている岩は落盤の対象外 */
        }
        r = (uint8_t)(rock_y[i] >> 4);
        if (r + 1 >= ROWS) {
            continue;                   /* 一番下。盤面の底が支えなので動かない */
        }
        c = (uint8_t)rcx(i);
        if (!cell_coal((int8_t)c, (int8_t)(r + 1))) {
            continue;                   /* 土や坑道に載っている岩は支えを失わない */
        }
        /* **岩が載っている黒ダイヤを割る。** 支えを失うので普通に落ちる。
           落ちた距離で普通に割れ判定される (特別扱いはしない)。 */
        coal_crack_by_rock((int8_t)c, (int8_t)(r + 1));
        rock_state[i] = ROCK_JIG;
        /* **全部同じフレームに落ち始める。** 以前は上の行から順に落としていたが、
           上を先行させると下の岩に追いついてしまい、**盤面のどこにも書かれていない
           数フレームの差**で板の取り合いの勝敗が決まっていた。
           同時なら全員が同じ速さで降りるので、勝敗は**経路の升数**だけで決まる。 */
        jig_t[i] = ROCK_JIG_FRAMES;
    }
    snd_sfx(SFX_CAVEIN);   /* 盤面全体が動くので、通常の破砕音とは別の合図を鳴らす */
    water_start();         /* 第三坑では、天盤が落ちると同時に熱水が噴き出す */
    enemy_stun_all(STUN_CAVEIN);   /* 天盤が落ちれば敵も足を取られる。岩が当たりやすくなる */
    cavein_flash();        /* 画面全体を白く飛ばす。見ていない場所の事件を伝える */
}

/* 動いている岩があるか。**クリア判定を待たせるために要る。**
   炭柱を割ると45フレーム揺れてから沈むが、その間に最後の黒ダイヤを回収すると
   クリアが成立して rock_update() が止まり、**落盤が一度も完走しない**。
   実測で、揺れ始めてから落ち始めるまでの 0.75秒 の中でクリアしていた。 */
uint8_t rock_busy(void)
{
    uint8_t i;
    for (i = 0; i < ROCK_MAX; i++) {
        if (rock_state[i] == ROCK_JIG || rock_state[i] == ROCK_FALL
            || rock_state[i] == ROCK_SPLIT) {
            return 1;
        }
    }
    return 0;
}

/* **その岩をいますぐ落とす。** 鉱車が車止めで岩を放り出すときに使う
   (揺れを挟まない。放り出されたのだから、ためらわずに落ちる)。 */
void rock_drop_now(uint8_t i)
{
    if (rock_state[i] != ROCK_WAIT && rock_state[i] != ROCK_JIG) {
        return;
    }
    rock_x[i] = (uint8_t)((uint8_t)rcx(i) << 4);
    rock_state[i] = ROCK_FALL;
    fall_r0[i] = (uint8_t)(rock_y[i] >> 4);
    crushed[i] = 0;
    kills[i] = 0;
}

void rock_update(void)
{
    uint8_t i;
    for (i = 0; i < ROCK_MAX; i++) {
        if (rock_state[i] == ROCK_NONE) {
            continue;
        }
        if (rock_state[i] == ROCK_SPLIT) {
            split_t[i]--;
            if (split_t[i] == 0) {
                rock_state[i] = ROCK_NONE;
                spr_park((uint8_t)(SPR_ROCK + i));
            }
            continue;
        }
        if (rock_state[i] == ROCK_FALL) {
            rock_fall_step(i);
            continue;
        }
        if (supported(i)) {
            rock_state[i] = ROCK_WAIT;
            jig_t[i] = 0;
            pushed[i] = 0;
            continue;
        }
        if (rock_state[i] != ROCK_JIG) {
            rock_state[i] = ROCK_JIG;
            jig_t[i] = (uint8_t)(pushed[i] ? ROCK_JIG_PUSH : ROCK_JIG_FRAMES);
        } else {
            jig_t[i]--;
            if (jig_t[i] == 0) {
                /* 落ちる瞬間に升へ寄せる。落下中の判定はすべて升前提なので、
                   せり出したまま落とすと着地判定が合わなくなる。 */
                rock_x[i] = (uint8_t)((uint8_t)rcx(i) << 4);
                rock_state[i] = ROCK_FALL;
                fall_r0[i] = (uint8_t)(rock_y[i] >> 4);
                crushed[i] = 0;
                kills[i] = 0;
            }
        }
    }
    /* 岩を全部見終わってから、予約されていた落盤を起こす (rock_collapse_later) */
    if (collapse_pend) {
        rock_collapse();
    }
}

void rock_draw(void)
{
    uint8_t i;
    for (i = 0; i < ROCK_MAX; i++) {
        if (rock_state[i] == ROCK_NONE) {
            spr_park((uint8_t)(SPR_ROCK + i));
        } else {
            /* 揺れている間は1pxぶらす (落ちる前触れ)。列0で 0-1=255 へ
               回り込まないよう、引かずに足す向きだけにしておく。 */
            uint8_t x = rock_x[i];
            if (rock_state[i] == ROCK_JIG && (jig_t[i] & 2)) {
                x = (uint8_t)(x + 1);
            }
            spr_set((uint8_t)(SPR_ROCK + i), x, (uint8_t)(rock_y[i] - 1),
                    (uint8_t)(rock_state[i] == ROCK_SPLIT ? PAT_SPLIT : PAT_ROCK));
        }
    }
}
