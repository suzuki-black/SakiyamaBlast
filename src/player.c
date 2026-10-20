/* SakiyamaBlast - 自機 (先山) の移動と掘り */
#include "game.h"
#include "vdp.h"
#include "sprite.h"
#include "sound.h"

uint8_t px, py;
uint8_t lives;
uint8_t dead;
uint8_t dead_cause;
uint8_t invuln;
uint8_t temp;                 /* 体温 */

static uint8_t cdir;      /* 移動中の向き。キーを離すと DIR_NONE に戻る */
static uint8_t face;      /* 最後に向いた方向。**止まっていても保持する**。
                             つるはしも投擲もこちらを見る (静止中に使えないと困るため) */
static uint8_t chewing;   /* 未掘の土を食い進んでいるか (減速する) */
static uint8_t acc;       /* 速度の分子 */
static uint8_t temp_acc;  /* 体温上昇の分子 */
static uint8_t swing;     /* つるはしを振っている残りフレーム。0以外の間は動けない */

uint8_t player_cell_c(void) { return (uint8_t)((px + 8) >> 4); }
uint8_t player_cell_r(void) { return (uint8_t)((py + 8) >> 4); }
uint8_t player_dir(void)    { return face; }

/* 体温を1つ上げる。上限に達したらオーバーヒートで1ミス。
   時間経過だけでなく、**つるはしを1振りするごとにも**上がる。 */
static void heat_add(uint8_t n)
{
    if (temp >= TEMP_MAX) {
        return;
    }
    temp = (uint8_t)((TEMP_MAX - temp <= n) ? TEMP_MAX : temp + n);
    if (temp >= TEMP_MAX && !invuln) {
        dead = 1;
        dead_cause = DEAD_HEAT;
    }
}

/*
 * つるはしを振る。向いている先の升が黒ダイヤなら耐久度を1削る。
 * 振り切るまで PICK_FRAMES のあいだ動けないので、そこへ敵が寄ってくる。
 * 使用回数は無制限 (回数制限やアイテムでの回数増加は今後の調整で入れるかもしれない)。
 */
uint8_t swing_pow;              /* いまの威力 1..CHARGE_MAX。0 = 溜めていない (計測・描画用) */
static uint8_t moved;           /* この面で一度でも動いたか (正面は開始時だけ) */
static uint8_t walk_t, walk_f;  /* 歩きのコマ送り */

/* 向き -> 絵。DIR_UP=1 DOWN=2 LEFT=3 RIGHT=4 なので -1 して 0..3 で引く。 */
static const uint8_t pat_walk[4][2] = {
    { PAT_WU1, PAT_WU2 }, { PAT_WD1, PAT_WD2 },
    { PAT_WL1, PAT_WL2 }, { PAT_WR1, PAT_WR2 },
};
static const uint8_t pat_brace[4]  = { PAT_BRU, PAT_BRACE, PAT_BRL, PAT_BRR };
static const uint8_t pat_strike[4] = { PAT_STU, PAT_STD, PAT_STL, PAT_STR };

/*
 * いま出すべき自機の絵。
 *  - まだ一度も動いていない (面の開始直後) -> 正面
 *  - 振っている最中                        -> 振り下ろし
 *  - 溜めている                            -> 構え
 *  - それ以外                              -> 歩き (止まっていればコマ1で立ち姿)
 */
uint8_t player_pattern(void)
{
    uint8_t d;
    if (!moved || face == DIR_NONE) {
        return PAT_MINER;
    }
    d = (uint8_t)(face - 1);
    if (swing) {
        return pat_strike[d];
    }
    if (swing_pow >= 2) {
        return pat_brace[d];
    }
    return pat_walk[d][walk_f];
}
static uint8_t charging, charge_lv, charge_t, prev_held;

/*
 * つるはし。**押した瞬間は必ず威力1で即座に振る**ので、単発の手触りは変わらない。
 * 押したまま動かずにいると、振り終わるたびに自動でもう一度振り、威力が 1→2→3 と
 * 上がっていく。移動するかボタンを離すと1へ戻る。
 *
 * 威力は黒ダイヤなら「その回数ぶん叩いた」ことになり、敵ならその段数ぶん深くよろける。
 * **発熱は威力に比例する。** 比例させないと掘りは常に押しっぱなしが最適になり、
 * 体温という主資源が意味を失う。溜めが買っているのは時間であって熱ではない。
 */
/* 向いている先の升を返す。向きが決まっていなければ 0。 */
static int8_t fdx, fdy;         /* 向いている方向の増分 (つるはしの届く先を辿る) */

static uint8_t facing_cell(int8_t *ox, int8_t *oy)
{
    int8_t cx = (int8_t)player_cell_c();
    int8_t cy = (int8_t)player_cell_r();
    fdx = 0;
    fdy = 0;
    switch (face) {
    case DIR_UP:    fdy = -1; break;
    case DIR_DOWN:  fdy =  1; break;
    case DIR_LEFT:  fdx = -1; break;
    case DIR_RIGHT: fdx =  1; break;
    default: return 0;
    }
    *ox = (int8_t)(cx + fdx);
    *oy = (int8_t)(cy + fdy);
    return 1;
}

/*
 * 1振りする。**当たるものが無くても振る (空振り)。**
 * 何も無いところで押して何も起きないと、ボタンが効いていないように見える。
 * 空振りでも発熱はする — つるはしを振るのは同じだけ力を使うので、
 * むやみに振れば体温を損する。当たれば威力ぶん黒ダイヤを削り、敵はその段数
 * ぶん深くよろける。
 */
static void do_swing(uint8_t pow)
{
    int8_t cx, cy;
    uint8_t k, hit = 0;
    if (!facing_cell(&cx, &cy)) {
        return;                     /* 向きが決まっていない */
    }
    swing = PICK_FRAMES;
    heat_add((uint8_t)(HEAT_PER_SWING * pow));
    if (cell_coal(cx, cy)) {
        for (k = 0; k < pow; k++) {
            coal_hit(cx, cy);       /* 割れたあとは cell_coal が偽になる */
        }
        hit = 1;
    }
    if (enemy_stun_at(cx, cy, fdx, fdy, pow)) {
        hit = 1;
    }
    if (hit) {
        snd_sfx_up(SFX_MINE, (uint8_t)((pow - 1) * 4));   /* 溜めるほど高く鳴る */
    } else {
        snd_sfx(SFX_NOPE);          /* 空振り。当たった音とは別にする */
    }
}

void player_pick(uint8_t held)
{
    int8_t cx, cy;

    if (!held) {
        prev_held = 0;
        if (charging && charge_lv >= 2) {
            do_swing(charge_lv);    /* 離した瞬間に、溜めたぶんを打ち込む */
        }
        charging = 0;
        charge_lv = 0;
        charge_t = 0;
        swing_pow = 0;
        clear_sweat();
        return;
    }
    if (!prev_held) {               /* 押した瞬間: まず普通に1振り */
        prev_held = 1;
        charging = 1;
        charge_lv = 1;
        charge_t = 0;
        swing_pow = 1;
        do_swing(1);
        return;
    }
    if (swing) {
        return;                     /* 振り切るまでは溜め始めない */
    }
    /* 押しっぱなし。**振らずに溜める。** 空振りできる以上、対象の有無に関わらず
       溜まるほうが筋が通る (代償はその場に留まること)。 */
    if (!facing_cell(&cx, &cy)) {
        return;                     /* 向きが決まっていなければ溜まらない */
    }
    if (charge_lv < CHARGE_MAX) {
        charge_t++;
        if (charge_t >= CHARGE_TIME) {
            charge_t = 0;
            charge_lv++;
            snd_sfx(SFX_PICK);      /* 段が上がった合図 */
        }
    }
    swing_pow = charge_lv;
}

/* 掘り跡は自機幅より内側2px。セル幅いっぱいに抜くと膜が消えるので必ず内側で。
   掘り跡の黒は風呂や湧き出し口の絵の上にも乗ってしまうので、重なった升目は描き直す。
   風呂は何度でも入れる (絵が消えると使えなくなったように見えるため、ここが重要)。 */
void player_carve(void)
{
    uint8_t c0 = (uint8_t)(px >> 4), c1 = (uint8_t)((px + PW) >> 4);
    uint8_t r0 = (uint8_t)(py >> 4), r1 = (uint8_t)((py + PW) >> 4);
    uint8_t c, r;

    vdp_fill((uint16_t)(px + 2), (uint16_t)(py + 2), 12, 12, COL_TUNNEL);
    link_carve(px, py);        /* 抜いた膜を記録する (敵の通行判定に効く) */

    /* 掘り跡の 12x12 は**隣の升にもはみ出す**ので、そこに置いてある絵を消してしまう。
       (自機が升の途中で止まると、隣の升の黒ダイヤが半分だけ黒く塗り潰される)
       重なった升の絵はすべて描き直す。 */
    for (r = r0; r <= r1 && r < ROWS; r++) {
        for (c = c0; c <= c1 && c < COLS; c++) {
            uint16_t i = (uint16_t)r * COLS + c;
            /* **レールも描き直す。** 抜かすと、自機が通った升のレールだけが黒く消え、
               鉱車が何も無いところを走っているように見える (作者の指摘、2026-10-04)。
               置いてある物より先に描く (レールは床で、物はその上に載る)。 */
            if (cart_on && r == cart_r && c >= cart_c0 && c <= cart_c1) {
                draw_rail(c, r);
            }
            if (coal[i]) {
                draw_coal(c, r);
            } else if (bath_on && c == bath_c && r == bath_r) {
                draw_bath(c, r);
            } else if (c == nest_c && r == nest_r) {
                draw_cave(c, r);
            } else if (cf_on(i, CF_POWDER)) {
                draw_powder(c, r);
            } else if (cf_on(i, CF_BOMB)) {
                draw_bombdrop(c, r);
            } else if (bonus_on && c == bonus_c && r == bonus_r) {
                draw_bonus(c, r, bonus_kind);
            } else if (chute[i]) {
                draw_chute(c, r);
            }
            /* **熱水も描き直す。** 抜かすと、自機が通った升の水の絵だけが黒く消え、
               見た目は空の坑道なのに体温が上がり続ける「見えない熱水」になる
               (作者の指摘「何もないところに行っても体温上昇が早い。実質詰み」)。 */
            if (water[i]) {
                draw_water(c, r);
            } else if (spring_on && c == spring_c && r == spring_r) {
                /* **湧き出し口も描き直す。** 抜かすと自機が上を通っただけで絵が消え、
                   拾ったアイテムのように見えた (作者の指摘、2026-09-22)。 */
                draw_spring(c, r);
            }
        }
    }
}

/* 入力を4方向へ落とす。上を最優先 (プロトの READINPUT と同じ順序)。 */
static uint8_t read_dir(void)
{
    uint8_t j = gtstck(0);
    uint8_t d;
    if (j == 0) {
        j = gtstck(1);
    }
    d = DIR_NONE;
    if (j == 2 || j == 3 || j == 4) { d = DIR_RIGHT; }
    if (j == 6 || j == 7 || j == 8) { d = DIR_LEFT; }
    if (j == 4 || j == 5 || j == 6) { d = DIR_DOWN; }
    if (j == 1 || j == 2 || j == 8) { d = DIR_UP; }
    return d;
}

/* 体温。毎フレームわずかに上がり、上限でオーバーヒート = 1ミス。
   風呂の升目を通過すると一瞬で下がる (仕様は後で調整するかもしれない)。 */
/* 水風呂に触れているか。自機も升も16pxなので、両軸とも16px未満のずれなら
   どこかが重なっている。 */
static uint8_t in_bath(void)
{
    int16_t dx, dy;
    if (!bath_on) {
        return 0;
    }
    dx = (int16_t)((int16_t)px - (int16_t)((uint16_t)bath_c << 4));
    dy = (int16_t)((int16_t)py - (int16_t)((uint16_t)bath_r << 4));
    if (dx < 0) { dx = (int16_t)(-dx); }
    if (dy < 0) { dy = (int16_t)(-dy); }
    return (uint8_t)((dx < 16) && (dy < 16));
}

static uint8_t bath_t, bath_phase;

static void update_temp(void)
{
    /* 風呂。**触れている間だけ少しずつ下がる。**
       一瞬で全快にすると、出発点の近くの本通りに置かれる風呂を通りすがりに
       踏むだけで体温が0へ戻り、意識して入る必要がまったく無くなる。
       ただし判定は **1px でも重なっていれば** とする。升にぴったり整列して
       いる間だけだと、動きながらでは滅多に成立せず、当たっているのか
       効かないバグなのかがプレイヤーに区別できない。 */
    if (in_bath()) {
        temp = (uint8_t)((temp > BATH_COOL) ? (temp - BATH_COOL) : 0);
        temp_acc = 0;
        if (!bathing) {
            bathing = 1;
            bath_t = 0;
        }
        bath_t++;
        if (bath_t >= BATH_BUBBLE) {
            bath_t = 0;
            bath_phase++;
            draw_bath_active(bath_phase);
        }
        return;
    }
    if (bathing) {
        bathing = 0;
        draw_bath(bath_c, bath_r);       /* 泡を消して元の湯へ戻す */
    }
    /* **熱水 (出水)。** 浸かっている間は毎フレーム上がる。水風呂のちょうど裏返しで、
       死因は増えない (既存のオーバーヒート)。升の中心で見る。 */
    if (cell_water((int8_t)player_cell_c(), (int8_t)player_cell_r())) {
        heat_add(WATER_HEAT);
    }
    temp_acc++;
    if (temp_acc < TEMP_TICK) {
        return;
    }
    temp_acc = 0;
    heat_add(1);
}

/* 火薬を拾う。節点に乗った時だけ判定する。 */
static void collect(void)
{
    uint16_t i;
    uint8_t c, r;
    if ((px & 15) || (py & 15)) {
        return;
    }
    c = (uint8_t)(px >> 4);
    r = (uint8_t)(py >> 4);
    i = (uint16_t)r * COLS + c;
    if (bonus_on && c == bonus_c && r == bonus_r) {
        bonus_take();
        snd_sfx(SFX_PICK);
    }
    if (cf_on(i, CF_POWDER)) {
        cf_clr(i, CF_POWDER);
        draw_dug_cell((int8_t)c, (int8_t)r);   /* 火薬の絵を消して坑道へ戻す */
        player_carve();
        stock++;
        score_add(PTS_POWDER);
        snd_sfx(SFX_PICK);
    }
    if (cf_on(i, CF_UME)) {                              /* 梅干し。塩分補給で体温が下がる */
        cf_clr(i, CF_UME);
        draw_dug_cell((int8_t)c, (int8_t)r);
        player_carve();
        temp = (uint8_t)((temp > UME_COOL) ? (temp - UME_COOL) : 0);
        snd_sfx(SFX_PICK);
    }
    if (cf_on(i, CF_BOMB)) {                         /* 投げて落ちたダイナマイトを拾い直す */
        cf_clr(i, CF_BOMB);
        draw_dug_cell((int8_t)c, (int8_t)r);
        player_carve();
        stock++;
        snd_sfx(SFX_PICK);
    }
    if (coal[i] == COAL_CRACKED) {             /* 割れた黒ダイヤの上を通ると採掘 */
        coal_take(c, r);                       /* 音と閃きは coal_take() の中 */
    }
}

/* 進行先セルが未掘なら食い進み中 = 減速。節点でだけ判定を更新する。 */
static void update_chewing(void)
{
    int8_t cx = (int8_t)(px >> 4);
    int8_t cy = (int8_t)(py >> 4);
    switch (cdir) {
    case DIR_UP:    cy--; break;
    case DIR_DOWN:  cy++; break;
    case DIR_LEFT:  cx--; break;
    case DIR_RIGHT: cx++; break;
    default:
        chewing = 0;
        return;
    }
    if (cx < 0 || cx >= COLS || cy < 0 || cy >= ROWS) {
        chewing = 0;          /* 場外へは進めないので減速させない */
        return;
    }
    /* 割れた黒ダイヤの升は掘る手間なく通れる (もう砕けているため) */
    if (coal[(uint16_t)(uint8_t)cy * COLS + (uint8_t)cx] == COAL_CRACKED) {
        chewing = 0;
        return;
    }
    chewing = cell_dug(cx, cy) ? 0 : 1;
}

/* 目の前に岩があれば1ドットだけ一緒に動かす。動かせなければ自機も進まない。
   **自機と同じ速さで並んで動く**ので、押している間だけ岩が自機に付いてくる。 */
static uint8_t push_step(int8_t dx)
{
    int8_t i = rock_ahead_of(px, py, dx);
    if (i < 0) {
        return 1;
    }
    return rock_push(i, dx, 1);         /* 自機は土の中へも押し込める */
}

/* 進行先セルが通れないか。ダイヤモンドブロックは掘れも通れもしない。
   岩は横向きなら押しを試み、押せなければ止まる。縦向きには押せない (Mr. Do! 準拠)。 */
static uint8_t blocked_ahead(void)
{
    int8_t cx = (int8_t)(px >> 4);
    int8_t cy = (int8_t)(py >> 4);
    int8_t i;
    switch (cdir) {
    case DIR_UP:    cy--; break;
    case DIR_DOWN:  cy++; break;
    case DIR_LEFT:  cx--; break;
    default:        cx++; break;
    }
    if (cell_coal(cx, cy)) {
        return 1;
    }
    i = rock_at(cx, cy);
    if (i < 0) {
        return 0;
    }
    /* **ここでは押さない。可否を見るだけ。** 実際の押しは1ドット進むたびに
       push_step() が行う。ここで升ごと動かすと岩が瞬間移動してしまう。 */
    if (cdir == DIR_LEFT) {
        return (uint8_t)(rock_can_push(i, -1, 1) ? 0 : 1);
    }
    if (cdir == DIR_RIGHT) {
        return (uint8_t)(rock_can_push(i, 1, 1) ? 0 : 1);
    }
    return 1;
}

/* 最寄りの節点へ寄せる。寄せた先が岩やダイヤで塞がっていたら手前の節点へ寄せる。 */
static void snap_x(void)
{
    uint16_t nx = (uint16_t)(((uint16_t)px + 8) & 0xFFF0);
    if (nx > XMAX) {
        nx = XMAX;
    }
    if ((uint8_t)(nx >> 4) != (uint8_t)(px >> 4)) {
        int8_t c = (int8_t)(nx >> 4), r = (int8_t)(py >> 4);
        if (cell_coal(c, r) || rock_at(c, r) >= 0) {
            nx = (uint16_t)(px & 0xF0);          /* 前が塞がっていれば手前へ */
        }
    }
    px = (uint8_t)nx;
    player_carve();
    dig_sweep((uint8_t)(px >> 4), (uint8_t)(py >> 4));
}

static void snap_y(void)
{
    uint16_t ny = (uint16_t)(((uint16_t)py + 8) & 0xFFF0);
    if (ny > YMAX) {
        ny = YMAX;
    }
    if ((uint8_t)(ny >> 4) != (uint8_t)(py >> 4)) {
        int8_t c = (int8_t)(px >> 4), r = (int8_t)(ny >> 4);
        if (cell_coal(c, r) || rock_at(c, r) >= 0) {
            ny = (uint16_t)(py & 0xF0);
        }
    }
    py = (uint8_t)ny;
    player_carve();
    dig_sweep((uint8_t)(px >> 4), (uint8_t)(py >> 4));
}

void player_start(void)
{
    px = (uint8_t)(start_c << 4);
    py = (uint8_t)(start_r << 4);
    cdir = DIR_NONE;
    face = DIR_DOWN;            /* 坑口から降りてくるので、初期は下向き */
    bathing = 0;
    bath_t = 0;
    swing_pow = 0;
    moved = 0;
    walk_t = 0;
    walk_f = 0;
    charging = 0;
    charge_lv = 0;
    charge_t = 0;
    prev_held = 0;
    clear_sweat();
    chewing = 0;
    swing = 0;
    acc = 0;
    dead = 0;
    dead_cause = 0;
    invuln = INVULN_FRAMES;
    temp = 0;
    temp_acc = 0;
    dig_sweep(start_c, start_r);
    draw_dug_cell((int8_t)start_c, (int8_t)start_r);
    player_carve();
}

void player_respawn(void)
{
    dead = 0;
    if (lives) {
        lives--;
    }
    bomb_clear();
    player_start();
}

void player_update(void)
{
    uint8_t q;
    uint8_t at_node;
    uint8_t pre_x = px, pre_y = py;

    if (invuln) {
        invuln--;
    }
    update_temp();

    /* つるはしを振り切るまでは動けない。ここが敵に迫られる緊張感になる。
       向きだけは受け付けて、次に振る方向を決められるようにしておく。 */
    if (swing) {
        swing--;
        q = read_dir();
        if (q != DIR_NONE) {
            face = q;               /* 振っている間も向きだけは変えられる */
        }
        collect();
        return;
    }

    q = read_dir();
    /* Dig Dug 式の方向転換。曲がれるのはグリッドの節点だけだが、半歩ずれた位置で
       直交方向を押したときは**最寄りの節点へ寄せてから**曲がる。
       「整列するまで曲がらない」だけにすると、止まった位置がずれていると
       いつまでも曲がれず詰んでしまう。 */
    if (q == DIR_NONE) {
        cdir = DIR_NONE;
    } else {
        if (q == DIR_UP || q == DIR_DOWN) {
            if (px & 15) {
                snap_x();
            }
        } else {
            if (py & 15) {
                snap_y();
            }
        }
        cdir = q;
        face = q;
    }

    /* 溜めは「その場に留まる」ことが代償なので、動いたら切れる。 */
    if (px != pre_x || py != pre_y) {
        moved = 1;
        walk_t++;
        if (walk_t >= WALK_FRAMES) {
            walk_t = 0;
            walk_f = (uint8_t)!walk_f;
        }
        charging = 0;
        charge_lv = 0;
        charge_t = 0;
        swing_pow = 0;
        clear_sweat();
    } else if (swing) {
        /* 振っている間はつるはしを降ろしていく。18フレームを3段に割る。 */
        draw_swing_fx(player_cell_c(), player_cell_r(), face,
                      (uint8_t)((PICK_FRAMES - swing) / (PICK_FRAMES / 3)));
    } else if (swing_pow >= 2) {
        draw_charge_fx(player_cell_c(), player_cell_r(), swing_pow);
    } else {
        clear_sweat();
    }

    /* **拾う判定は方向転換の後に行う。**
       方向転換は snap_x()/snap_y() で升へ吸い寄せるが、吸い寄せた同じフレームに
       もう新しい向きへ動き出すので、判定が前にあると「升にぴったり乗った瞬間」が
       一度も見られないまま通り過ぎてしまう。
       (黒ダイヤを上から砕いて、乗った瞬間に横へ切り返すと拾えなかった) */
    collect();

    if (cdir == DIR_NONE) {
        return;
    }

    at_node = (uint8_t)(((px & 15) == 0) && ((py & 15) == 0));
    if (at_node) {
        if (blocked_ahead()) {
            return;
        }
        update_chewing();
    }

    acc = (uint8_t)(acc + (chewing ? SPD_DIG : SPD_TUNNEL));
    if (acc < SPD_UNIT) {
        return;                     /* 掘削中はこのフレーム進まない */
    }
    acc = (uint8_t)(acc - SPD_UNIT);

    switch (cdir) {
    case DIR_UP:
        if (py) { py--; }
        break;
    case DIR_DOWN:
        if (py < YMAX) { py++; }
        break;
    case DIR_LEFT:
        if (!push_step(-1)) { return; }
        if (px) { px--; }
        break;
    default:
        if (!push_step(1)) { return; }
        if (px < XMAX) { px++; }
        break;
    }

    player_carve();
    /* DUG は「丸々掘り切った」時だけ立てる (掠めただけでは掘れた扱いにしない)。 */
    if (((px & 15) == 0) && ((py & 15) == 0)) {
        dig_sweep((uint8_t)(px >> 4), (uint8_t)(py >> 4));
        if (chewing) {
            snd_sfx(SFX_DIG);          /* 1セル掘り切るごとに鳴らす */
        }
    }
}
