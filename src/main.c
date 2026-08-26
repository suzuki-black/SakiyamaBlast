/*
 * SakiyamaBlast - 先山＋発破 / 常磐炭鉱ダイナマイト堀り
 *
 * 仕様の正は DESIGN.md、Mr. Do! 由来の挙動の根拠は docs/MRDO_RESEARCH.md。
 */
#include "msx.h"
#include "vdp.h"
#include "sprite.h"
#include "game.h"
#include "text.h"
#include "sound.h"
#include "art.h"
#include "config.h"
#include "palette.h"   /* tools/palette.py から自動生成 */

#define ST_TITLE 0
#define ST_START 1        /* 面を描き切って "STAGE n START" を出している間 */
#define ST_PLAY  2
#define ST_CLEAR 3        /* クリア。自機も敵もその場で停止しファンファーレ */
#define ST_CONFIG 4       /* 隠しコンフィグ (タイトルで 上上下下左右左右BA) */
#define ST_ENDING 5       /* エンディング */
#define ST_MISS   6       /* ミス。短いジングルの間、画面をその場で止める */
#define ST_OVER   7       /* ゲームオーバー */

#define START_HOLD  60    /* 開始表示を出しておくフレーム数 (1秒) */
#define MSG_ROW     5     /* 開始表示を出すセル行 */

static uint8_t state;
static uint8_t prev_trig;
uint16_t frame_n;                 /* 実フレーム数。速度計測用 (1フレーム1増) */
static uint8_t ending_to_ura;
static uint8_t loop_n;            /* 0=表 1以降=裏 (裏面の内容は TODO) */
static uint8_t timer;
static uint8_t msg_c0, msg_c1;    /* 開始表示が占めたセル列 (消すとき使う) */
static char    msg[16];

/* 垂直帰線待ち。
   「次に JIFFY が変わるまで待つ」だけにすると、1フレームの処理が 16.7ms を
   わずかでも超えたときに次の次まで待ってしまい、60fps から 30fps へ一気に半減する
   (移動は1フレーム1pxなので、そのまま速度が半分になる)。
   既に遅れている場合は待たずに進めることで、落ち方をなだらかにする。 */
static uint8_t last_j;

static void wait_vsync(void)
{
    while (JIFFY == last_j) {
        /* まだ次のフレームが来ていない */
    }
    last_j = JIFFY;
}

/* ボタンの立ち上がりだけを拾う (押しっぱなしで連射しない) */
static uint8_t fire_pressed(void)
{
    uint8_t t = (uint8_t)(gttrig(0) || gttrig(1));
    uint8_t edge = (uint8_t)(t && !prev_trig);
    prev_trig = t;
    return edge;
}

/* 坑内の色。タイトルの1枚絵がパレットを上書きするので、面を組む前に必ず焼き直す。 */
static void set_game_palette(void)
{
    uint8_t i;
    /* 色の値は tools/palette.py が正。build/palette.h はそこから自動生成される。
       0番は背景色レジスタ (R#7) の色で出るので触らない。 */
    for (i = 1; i < 16; i++) {
        vdp_set_palette(i, game_palette[i][0], game_palette[i][1], game_palette[i][2]);
    }
}

/* 面の中央に文字を出す。フォントは本体 BIOS のものを使う。 */
static void show_msg(const char *t, uint8_t n, uint8_t col)
{
    uint16_t x = (uint16_t)((256 - (uint16_t)n * 16) >> 1) & 0xFFFE;
    text_draw(x, MSG_ROW * CELL, t, col, COL_TUNNEL, 2);
    msg_c0 = (uint8_t)(x >> 4);
    msg_c1 = (uint8_t)((x + (uint16_t)n * 16 - 1) >> 4);
}

static void show_stage_msg(void)
{
    uint8_t n = 0, i;
    const char *a = "STAGE ";
    const char *b = " START";
    for (i = 0; a[i]; i++) { msg[n++] = a[i]; }
    n = (uint8_t)(n + text_u8(&msg[n], stage));
    for (i = 0; b[i]; i++) { msg[n++] = b[i]; }
    msg[n] = 0;
    show_msg(msg, n, COL_WHITE);
}

static void hide_stage_msg(void)
{
    uint8_t c;
    for (c = msg_c0; c <= msg_c1 && c < COLS; c++) {
        field_redraw_cell((int8_t)c, (int8_t)MSG_ROW);
    }
    player_carve();          /* 表示が自機の掘り跡に掛かっていた場合に備えて */
}

static void start_stage(void)
{
    /* パレットの差し替えと面の描き起こしは見せない。描き切ってから幕を上げる。 */
    vdp_display(0);
    set_game_palette();
    build_stage();
    bomb_clear();
    player_start();
    enemy_stage_reset();
    bonus_stage_reset();
    hud_reset();
    hud_draw();
    show_stage_msg();        /* 面を描き切ってから開始表示を出す */
    vdp_display(1);
    timer = START_HOLD;
    state = ST_START;
}

static void new_game(void)
{
    rnd_seed((uint16_t)((uint16_t)JIFFY << 8 | (uint16_t)JIFFY));
    lives = cfg_lives;
    stage = cfg_start_stage;
    score_reset();
    stock = 0;              /* 開始ストックは0。火薬を拾って貯める。 */
    start_stage();
}

static void next_stage(void)
{
    score_add((uint16_t)stock * PTS_LEFTOVER_DYN);   /* 残ダイナマイトを得点に */
    stage++;                /* ストックは持ち越す (リセットしない) */
    start_stage();
}

/* スプライトを OAM へ流し込む。停止中 (ST_START / ST_CLEAR) もその場に出したままにする。 */
static void draw_actors(void)
{
    if (invuln && (invuln & 4)) {
        spr_park(SPR_PLAYER);           /* 無敵中は点滅 */
    } else {
        spr_set(SPR_PLAYER, px, (uint8_t)(py - 1), PAT_MINER);
    }
    rock_draw();
    enemy_draw();
    oam_flush();
}

/* エンディング。中身は仮。15面クリア後は裏面へ、コンフィグから見た場合はタイトルへ戻る。 */
/* ゲームオーバー。コンティニューが ON なら、その面から続きを遊べる。 */
static void goto_gameover(void)
{
    snd_stop();
    dead = 0;
    show_msg("GAME OVER", 9, COL_RED);
    state = ST_OVER;
}

static void goto_ending(uint8_t to_ura)
{
    snd_stop();
    spr_park_all();
    oam_flush();
    vdp_display(0);
    set_game_palette();          /* タイトル1枚絵のパレットから戻す */
    vdp_fill(0, 0, 256, SCR_H, COL_TUNNEL);
    text_draw(48, 40, "CONGRATULATIONS", COL_FUSE, COL_TUNNEL, 1);
    text_draw(24, 72, "TANNO SANJI HAS DUG OUT", COL_WHITE, COL_TUNNEL, 1);
    text_draw(24, 88, "ALL THE BLACK DIAMONDS.", COL_WHITE, COL_TUNNEL, 1);
    text_draw(96, 128, "THE END", COL_WHITE, COL_TUNNEL, 2);
    text_draw(56, 176, "PUSH SPACE KEY", COL_DIA, COL_TUNNEL, 1);
    vdp_display(1);
    snd_play_fanfare();
    ending_to_ura = to_ura;
    state = ST_ENDING;
}

static void goto_title(void)
{
    snd_stop();
    spr_park_all();         /* 1枚絵の上にスプライトが残らないように */
    oam_flush();
    vdp_display(0);              /* 1枚絵のパレットに切り替わるところを見せない */
    title_draw();
    vdp_display(1);
    state = ST_TITLE;
}

void main(void)
{
    vdp_screen5();
    set_game_palette();
    text_init();
    snd_init();
    config_init();

    spr_init();
    {
        uint8_t i;
        /* 絵は assets の PNG から生成したものをそのまま使う */
        for (i = 0; i <= PAT_BAT; i++) {
            spr_pattern_rows(i, art_pattern(i));
        }
        spr_color_lines(SPR_PLAYER, art_colours(PAT_MINER));
        spr_color_lines(SPR_BOMB, art_colours(PAT_BOMB));
        for (i = 0; i < ROCK_MAX; i++) {
            spr_color_lines((uint8_t)(SPR_ROCK + i), art_colours(PAT_ROCK));
        }
    }

    goto_title();

    for (;;) {
        wait_vsync();
        frame_n++;
        snd_tick();

        if (state == ST_TITLE) {
            if (konami_check()) {
                vdp_display(0);
                set_game_palette();      /* タイトル1枚絵のパレットから戻す */
                config_enter();
                vdp_display(1);
                state = ST_CONFIG;
            } else if (fire_pressed()) {
                loop_n = 0;
                new_game();      /* start_stage() が ST_START にする */
            }
            continue;
        }

        if (state == ST_CONFIG) {
            uint8_t r = config_update();
            if (r == CFG_START) {
                loop_n = 0;
                new_game();
            } else if (r == CFG_ENDING) {
                goto_ending(0);
            }
            continue;
        }

        if (state == ST_ENDING) {
            if (fire_pressed()) {
                if (ending_to_ura) {
                    loop_n++;                  /* 裏面へ。内容の作り込みは TODO */
                    stage = 1;
                    start_stage();
                } else {
                    goto_title();
                }
            }
            continue;
        }

        if (state == ST_MISS) {
            /* ミスした瞬間はその場で止め、短いジングルを鳴らし切る */
            draw_actors();
            if (!snd_busy()) {
                hide_stage_msg();
                if (lives <= 1) {
                    lives = 0;
                    hud_draw();
                    goto_gameover();
                } else {
                    player_respawn();
                    hud_draw();
                    snd_play_bgm();
                    state = ST_PLAY;
                }
            }
            continue;
        }

        if (state == ST_OVER) {
            if (fire_pressed()) {
                if (cfg_continue) {
                    lives = cfg_lives;      /* その面から続きを遊ぶ */
                    score_reset();
                    start_stage();
                } else {
                    goto_title();
                }
            }
            continue;
        }

        if (state == ST_START) {
            timer--;
            if (timer == 0) {
                hide_stage_msg();
                snd_play_bgm();
                state = ST_PLAY;
            }
            draw_actors();
            continue;
        }

        if (state == ST_CLEAR) {
            /* 自機も敵もその場で停止したまま、ファンファーレが終わるのを待つ */
            draw_actors();
            if (!snd_busy()) {
                if (stage >= MAX_STAGE) {
                    goto_ending(1);            /* 15面クリア -> エンディング -> 裏1面 */
                } else {
                    next_stage();
                }
            }
            continue;
        }

        if (!dead) {
            player_update();
            if (fire_pressed()) {
                bomb_fire();
            }
        } else {
            fire_pressed();        /* 死亡中もエッジ検出の状態は進めておく */
        }
        bomb_update();
        rock_update();
        enemy_update();
        bonus_update();

        if (dead && cfg_invincible) {
            dead = 0;                /* 無敵設定 (動作確認用) */
            if (temp >= TEMP_MAX) {
                temp = 0;            /* オーバーヒートのときだけ冷ます */
            }
        }

        if (dead) {
            /* 自爆の場合は爆風の閃光を見せ切ってからミス処理する。
               閃光中に片付けると finish_blast() が走らず黄色が残ってしまう。 */
            if (bomb_state != 2) {
                snd_stop();
                snd_play_miss();
                if (dead_cause == DEAD_HEAT) {
                    show_msg("HEATSTROKE", 10, COL_RED);
                } else {
                    msg_c0 = 1;         /* 表示は無いので消す範囲も空に */
                    msg_c1 = 0;
                }
                state = ST_MISS;
            }
        } else if (dug_pct >= WIN_PCT) {
            snd_stop();
            snd_play_fanfare();
            state = ST_CLEAR;
        }

        draw_actors();
        hud_draw();
    }
}
