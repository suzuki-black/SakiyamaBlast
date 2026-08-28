/* SakiyamaBlast - 盤面・共有状態・モジュール間インタフェース */
#ifndef GAME_H
#define GAME_H

#include "msx.h"

/* --- 盤面 --- */
#define COLS      16          /* 升目列 (16px セル) */
#define ROWS      11          /* 升目行 */
#define NCELL     (COLS * ROWS)
#define CELL      16
#define FIELD_H   176         /* 盤面の高さ px (= ROWS * CELL) */
#define HUD_Y     176         /* HUD 帯の上端 */
#define SCR_H     212

#define PW        15          /* スプライト幅-1 */
#define XMAX      (255 - PW)
#define YMAX      (FIELD_H - CELL)

/* 速度は分子加算式。毎フレーム加算し SPD_UNIT に達するごとに 1px 進む。
   Mr. Do! は既掘の坑道は等速・未掘の土を食い進む間は減速する (EATWALL の速度ペナルティ)。
   60fps で 坑道 60px/s / 掘削 40px/s。
   分母を 12 にしてあるのは、敵の速度を 5px/s 刻みで調整できるようにするため。 */
#define SPD_UNIT   24
#define SPD_TUNNEL 24         /* 60px/s */
#define SPD_DIG    16         /* 40px/s */

/* 岩。押し・落下距離での割れ・潰しは Mr. Do! の apple 準拠だが、**支え判定は Dig Dug 準拠**で、
   真下の升目に自機が居る間は揺れも落下もしない (好きなだけ待てる)。自機が離れた瞬間に
   揺れ始め、揺れ切ると落ちる。 */
#define ROCK_MAX      4
#define ROCK_JIG_FRAMES 45    /* 揺れ (約0.75秒。DESIGN の当初値) */
#define ROCK_FALL_SPD 2       /* 落下 px/frame (1セル=8フレーム) */

/* 敵 (ネズミ / コウモリ)。
   出現: 画面中央の「巣」(ステージ開始時から掘られている升目) から湧く。ステージ毎に
   決められた総数まで、一定時間ごと、または画面上の敵が全滅した瞬間に次が出てくる。
   総数を出し切ったらそれ以上出ない。
   移動: 坑道の中は Mr. Do! 式＝節点で通れる方向のマスクを作り、通れるなら現在の向きを
   維持、約1/8で強制的に選び直す (WORKEXITS / RND2 & 7 == 0)。
   すり抜け: **敵は自分では掘らない** (Dig Dug 準拠)。一定時間で透明になって土の中を
   自機へ向かって斜めに直進し、トンネルに入ると実体化する。透明な間は倒せない。
   出典: "monsters can transform into ghostly forms to move through solid earth until they
   reach another tunnel" / "travel diagonally" / "rematerialize as soon as they get into a
   tunnel"。 */
#define ENEMY_MAX     4       /* 同時に画面に出る上限 */
#define EN_QUOTA_BASE 5       /* 面1の総出現数。以降 面ごとに+1 (面15で19) */
#define EN_QUOTA_MAX  19
#define SPAWN_BASE    300     /* 次の湧きまでのフレーム数 (面1 = 5秒) */
#define SPAWN_STEP    15      /* 面が進むごとに速くなる */
#define SPAWN_MIN     120
/* 敵の速さと性格。Dig Dug の Pooka / Fygar の違い (Pooka は執拗で速い、Fygar は
   気紛れで遅い) に倣って2種類に分けた。ただし**速さは原作より落としてある**:
   原作の Pooka は自機よりわずかに速いが、本作は敵の総数が多くポンプのような
   飛び道具も無いので、そのままでは追い詰められるだけになる。
   自機は掘削中 40px/s まで落ちるので、敵はそれより遅くしないと振り切れない。 */
#define EN_RAT        0       /* ネズミ: 執拗に追う */
#define EN_BAT        1       /* コウモリ: 気紛れ */
#define SPD_RAT      14       /* 35px/s */
#define SPD_BAT      10       /* 25px/s */
#define WANDER_RAT    7       /* rnd8() & これ が 0 のとき進路を選び直す (1/8) */
#define WANDER_BAT    1       /* コウモリは 1/2 の節点で気紛れに選び直す */
/* 追跡の「遊び」。毎回いちばん自機へ近づく方向を選ぶと坑道をなぞって一直線に
   詰めてくるだけになり、つるはしを振る余裕が無くなる。節点ごとに:
     STRAY_x の確率で自機を無視して通れる方向から適当に選ぶ (気まぐれ)
     そうでなくても SWAP_x の確率で主軸と副軸を入れ替える (回り込むように見える)
   値は rnd8() & 値 == 0 の確率なので、3 なら 1/4、1 なら 1/2。 */
#define STRAY_RAT     3       /* ネズミ 1/4 */
#define STRAY_BAT     1       /* コウモリ 1/2 */
#define SWAP_RAT      1       /* ネズミ 1/2 */
#define SWAP_BAT      1       /* コウモリ 1/2 */
/* 透明化して未掘の土をすり抜けている間は、**素の速さの半分以下**に落とす
   (Dig Dug 準拠)。土の中を突っ切るほうが速いと、坑道を掘る意味が薄れるため。 */
#define SPD_RAT_GHOST 3       /* 7.5px/s (素の 35px/s の 21%) */
#define SPD_BAT_GHOST 2       /* 5px/s  (素の 25px/s の 20%) */
#define GHOST_BASE    600     /* 透明化までのフレーム数 (面1 = 10秒) */
#define GHOST_STEP    20      /* 面が進むごとに早くなる */
#define GHOST_MIN     300     /* 高次面での下限 (5秒) */
#define GHOST_PAUSE   20      /* 透明になる瞬間は一拍止まる */
/* 湧いた直後は洞窟の口でこの間だけ点滅する。動かず、触れても当たらない。
   いきなり実体で出てくると出会い頭の事故になるため。 */
#define SPAWN_WARM   180      /* 3秒 */

/* 体温オーバーヒートと風呂。常磐炭鉱は地熱約40度＋湧水で過酷 (史実) という設定。
   体温は毎フレーム少しずつ上がり、上限に達すると1ミス。各面に1つある風呂を通過すると
   一瞬で下がる。風呂は敵には壊されないが、落下する岩とダイナマイトの爆風では壊れる。
   壊れても直ちにミスにはならないが、次にオーバーヒートするまでに面をクリアできないと
   1ミスになる (＝冷やす手段が無くなるので自然にそうなる)。 */
#define TEMP_MAX      240     /* 上限 = オーバーヒート */
#define TEMP_TICK     15      /* このフレーム数ごとに体温+1 (満タンまで約60秒) */

#define FUSE_FRAMES   120     /* ダイナマイトの導火 = 約2秒 */
#define FLASH_FRAMES  8       /* 爆風の閃光 */
#define INVULN_FRAMES 90      /* ミス後の無敵 */
/* ステージクリア = 盤面の黒ダイヤを全部採掘する (coal_left == 0)。
   掘削率はクリア条件ではなく、クリア時の得点ボーナスに使う。 */
#define PTS_DUG_PCT   20      /* クリア時、採掘率1%あたりの得点 */

/* つるはし。黒ダイヤに接触した状態で振ると耐久度を1削る。
   振っている間は動けないので、そこへ敵が寄ってくるのが駆け引きになる。 */
#define PICK_FRAMES   18      /* 1回振り切るまで (0.3秒) */
#define COAL_CRACKED  255     /* coal[] のこの値は「割れて採掘待ち」 */
#define COAL_HP_MAX   8

/* ダイナマイトは投げる武器。向いている方向へ最大5升飛ぶ。
   飛んだ先に敵が居ればそこで爆発 (爆風は1升だけ・地形は掘らない)、
   障害物があれば手前に落ち、何も無ければ5升先に落ちて拾い直せる。 */
#define BOMB_RANGE     5      /* 飛距離 (升) */
#define BOMB_FLY_FRAMES 4     /* 1升進むのに掛けるフレーム数 */
#define LIVES_INIT    3
#define MAX_STAGE     15      /* 全15面。掘り抜きが面番号の形になる */
#define POWDER_PER_STAGE 10   /* 盤面に置く火薬の数 (面によらず一定) */

/* --- パレット番号 --- */
#define COL_TUNNEL 1
#define COL_SKIN   2
#define COL_DARK   3
#define COL_DIRT   6
#define COL_HUD    4
#define COL_BODY   7
#define COL_BLAST  11
#define COL_WHITE  15
#define COL_RED    8
#define COL_FUSE   10
#define COL_ROCK   14
#define COL_RAT    9
#define COL_BAT    13
#define COL_BATH   5
#define COL_DIA    12

/* --- 方向 --- */
#define DIR_NONE  0
#define DIR_UP    1
#define DIR_DOWN  2
#define DIR_LEFT  3
#define DIR_RIGHT 4

/* --- スプライト面 --- */
#define SPR_PLAYER 0
#define SPR_BOMB   1
#define SPR_ROCK   2                        /* 面 2..5 */
#define SPR_ENEMY  (SPR_ROCK + ROCK_MAX)    /* 面 6..9 */
#define SPR_ACTIVE (SPR_ENEMY + ENEMY_MAX)  /* 実際に使う面の数。OAM 転送量を抑える */

/* スプライトの絵番号 */
#define PAT_MINER 0
#define PAT_BOMB  1
#define PAT_ROCK  2
#define PAT_RAT   3
#define PAT_BAT   4

/* --- field.c --- */
extern uint8_t  dug[NCELL];      /* 掘り済みフラグ */
extern uint8_t  powder[NCELL];   /* 火薬 (ダイナマイトの弾薬) */
extern uint8_t  bombdrop[NCELL]; /* 投げて落ちたダイナマイト。拾い直せる */
/* 黒ダイヤ (石炭)。土と違って自機の掘りでは進めず、障害物として立ちはだかる。
   つるはしで1回叩くごとに耐久度が1減り、0になると COAL_CRACKED (割れた状態) になる。
   割れると通れるようになり、その升に入ると採掘できる。
   値: 0=無し / 1..COAL_HP_MAX=残り耐久度 / COAL_CRACKED=割れて採掘待ち */
extern uint8_t  coal[NCELL];
extern uint8_t  coal_left;       /* まだ採掘していない黒ダイヤの数。0 でステージクリア */
extern uint16_t dug_n;           /* 掘ったセル数 */
extern uint8_t  dug_pct;         /* 採掘率 % */
extern uint8_t  stage;
extern uint8_t  start_c, start_r;
extern uint8_t  nest_c, nest_r;   /* 敵が湧く巣 = 画面中央の既掘セル */
extern uint8_t  bath_c, bath_r;   /* 風呂 = 自機の出現位置の近く (面ごとに違う) */
extern uint8_t  bath_on;          /* 風呂が健在か */

uint8_t cell_dug(int8_t cx, int8_t cy);
void    dig_sweep(uint8_t cx, uint8_t cy);
void    draw_dug_cell(int8_t cx, int8_t cy);
void    draw_cell_dirt(int8_t cx, int8_t cy);
void    draw_powder(uint8_t cx, uint8_t cy);
void    draw_bombdrop(uint8_t cx, uint8_t cy);
void    draw_bath(uint8_t cx, uint8_t cy);
void    draw_coal(uint8_t cx, uint8_t cy);       /* 耐久度に応じて絵を選ぶ */
void    draw_bonus(uint8_t cx, uint8_t cy, uint8_t kind);
void    draw_cave(uint8_t cx, uint8_t cy);
uint8_t cell_coal(int8_t cx, int8_t cy);         /* 立ちはだかる黒ダイヤなら1 (割れたものは0) */
uint8_t coal_hit(int8_t cx, int8_t cy);          /* つるはし1回。割れたら1を返す */
void    coal_take(uint8_t cx, uint8_t cy);       /* 割れた黒ダイヤを採掘する */
void    bath_destroy_at(int8_t cx, int8_t cy);   /* 岩・爆風で壊れる (敵では壊れない) */
void    field_redraw_cell(int8_t cx, int8_t cy);
void    build_stage(void);

/* --- player.c --- */
extern uint8_t px, py;
extern uint8_t lives;
extern uint8_t dead;
extern uint8_t dead_cause;    /* 0=接触や潰し 1=オーバーヒート */
#define DEAD_HEAT 1
extern uint8_t invuln;
extern uint8_t temp;          /* 体温 0..TEMP_MAX */

void player_start(void);
void player_update(void);
void player_respawn(void);
void player_carve(void);
void player_pick(void);          /* つるはしを振る (向いている先の黒ダイヤを削る) */
uint8_t player_dir(void);       /* いま向いている方向 */
uint8_t player_cell_c(void);
uint8_t player_cell_r(void);

/* --- rock.c --- */
#define ROCK_NONE 0
#define ROCK_WAIT 1
#define ROCK_JIG  2
#define ROCK_FALL 3

extern uint8_t rock_state[ROCK_MAX];
extern uint8_t rock_x[ROCK_MAX], rock_y[ROCK_MAX];

void    rock_clear(void);
void    rock_place(uint8_t c, uint8_t r);
int8_t  rock_at(int8_t cx, int8_t cy);          /* そのセルの岩の番号 / 無ければ -1 */
uint8_t rock_can_push(int8_t i, int8_t dx);    /* 押せるか調べるだけ */
uint8_t rock_push(int8_t i, int8_t dx);         /* 1=押せた 0=押せない */
uint8_t rock_crush_check(uint8_t cx, uint8_t cy);
void    rock_destroy_at(int8_t cx, int8_t cy);  /* 爆風で吹き飛ばす */
void    rock_update(void);
void    rock_draw(void);

/* --- enemy.c --- */
extern uint8_t en_state[ENEMY_MAX];
extern uint8_t en_x[ENEMY_MAX], en_y[ENEMY_MAX];

uint8_t rnd8(void);
void    rnd_seed(uint16_t s);
void    enemy_clear(void);
void    enemy_stage_reset(void);      /* 面開始時に出現数と湧きタイマを仕込む */
void    enemy_update(void);
void    enemy_draw(void);
uint8_t enemy_kill_at(int8_t cx, int8_t cy);   /* 爆風・落下岩で倒す。倒した数を返す */
uint8_t enemy_at(int8_t cx, int8_t cy);        /* その升に敵が居るか (投げたダイナマイトの判定) */

/* --- bomb.c --- */
extern uint8_t stock;        /* ダイナマイトのストック (次ステージへ持ち越し) */
extern uint8_t bomb_state;   /* 0=無 1=飛行中 2=爆風閃光中 */
extern uint8_t bomb_c, bomb_r;

void bomb_clear(void);
void bomb_throw(uint8_t dir);   /* 向いている方向へ投げる */
void bomb_update(void);

extern uint16_t frame_n;      /* 実フレーム数 (速度計測用) */

/* --- title.c --- */
void title_draw(void);

/* --- bonus.c --- */
/* 岩を2個割ると一定時間だけ出現し、上を通過すると取れる。効果は3種類。 */
#define ROCKS_PER_BONUS 2
#define BONUS_FRAMES  600      /* 出ている時間 (10秒) */
#define BONUS_SCORE 0
#define BONUS_COOL  1
#define BONUS_LIFE  2

extern uint8_t bonus_on;
extern uint8_t bonus_c, bonus_r;
extern uint8_t bonus_kind;

void bonus_stage_reset(void);
void bonus_rock_broken(void);
void bonus_update(void);
void bonus_take(void);

/* --- hud.c --- */
#define SCORE_DIGITS 6
extern uint8_t score_d[SCORE_DIGITS];   /* 10進の桁 (0 が最上位) */

void hud_reset(void);
void hud_draw(void);
void score_reset(void);
void score_add(uint16_t v);
void score_kills(uint8_t n, uint8_t by_rock);   /* 同時に倒した数で倍々 */

/* 得点表 */
#define PTS_POWDER  10
#define PTS_COAL   200            /* 黒ダイヤ1個を採掘 */
#define PTS_COAL_CRACK 50         /* 割った瞬間 */
#define PTS_DIG      5
#define PTS_BONUS 1000
#define PTS_LEFTOVER_DYN 100            /* 面クリア時に残ダイナマイト1本あたり */

#endif
