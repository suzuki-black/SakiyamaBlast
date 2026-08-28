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

/*
 * つるはしを振る。向いている先の升が黒ダイヤなら耐久度を1削る。
 * 振り切るまで PICK_FRAMES のあいだ動けないので、そこへ敵が寄ってくる。
 * 使用回数は無制限 (回数制限やアイテムでの回数増加は今後の調整で入れるかもしれない)。
 */
void player_pick(void)
{
    int8_t cx, cy;
    if (swing) {
        return;                     /* 振っている最中は次を受け付けない */
    }
    cx = (int8_t)player_cell_c();
    cy = (int8_t)player_cell_r();
    switch (face) {
    case DIR_UP:    cy--; break;
    case DIR_DOWN:  cy++; break;
    case DIR_LEFT:  cx--; break;
    case DIR_RIGHT: cx++; break;
    default: return;                /* 向きが決まっていなければ振れない */
    }
    if (!cell_coal(cx, cy)) {
        return;                     /* 黒ダイヤに接していないと振れない */
    }
    swing = PICK_FRAMES;
    coal_hit(cx, cy);
    snd_sfx(SFX_MINE);
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

    for (r = r0; r <= r1 && r < ROWS; r++) {
        for (c = c0; c <= c1 && c < COLS; c++) {
            if (bath_on && c == bath_c && r == bath_r) {
                draw_bath(c, r);
            } else if (c == nest_c && r == nest_r) {
                draw_cave(c, r);
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
static void update_temp(void)
{
    if (bath_on && ((px & 15) == 0) && ((py & 15) == 0)
        && ((uint8_t)(px >> 4) == bath_c) && ((uint8_t)(py >> 4) == bath_r)) {
        temp = 0;
        temp_acc = 0;
        return;
    }
    temp_acc++;
    if (temp_acc < TEMP_TICK) {
        return;
    }
    temp_acc = 0;
    if (temp < TEMP_MAX) {
        temp++;
        if (temp >= TEMP_MAX && !invuln) {
            dead = 1;               /* オーバーヒート */
            dead_cause = DEAD_HEAT;
        }
    }
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
    if (powder[i]) {
        powder[i] = 0;
        draw_dug_cell((int8_t)c, (int8_t)r);   /* 火薬の絵を消して坑道へ戻す */
        player_carve();
        stock++;
        score_add(PTS_POWDER);
        snd_sfx(SFX_PICK);
    }
    if (bombdrop[i]) {                         /* 投げて落ちたダイナマイトを拾い直す */
        bombdrop[i] = 0;
        draw_dug_cell((int8_t)c, (int8_t)r);
        player_carve();
        stock++;
        snd_sfx(SFX_PICK);
    }
    if (coal[i] == COAL_CRACKED) {             /* 割れた黒ダイヤの上を通ると採掘 */
        coal_take(c, r);
        player_carve();
        snd_sfx(SFX_PICK);
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
    if (cdir == DIR_LEFT) {
        return (uint8_t)(rock_push(i, -1) ? 0 : 1);
    }
    if (cdir == DIR_RIGHT) {
        return (uint8_t)(rock_push(i, 1) ? 0 : 1);
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

    if (invuln) {
        invuln--;
    }
    update_temp();
    collect();

    /* つるはしを振り切るまでは動けない。ここが敵に迫られる緊張感になる。
       向きだけは受け付けて、次に振る方向を決められるようにしておく。 */
    if (swing) {
        swing--;
        q = read_dir();
        if (q != DIR_NONE) {
            face = q;               /* 振っている間も向きだけは変えられる */
        }
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
        if (px) { px--; }
        break;
    default:
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
