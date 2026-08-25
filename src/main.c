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

#define ST_TITLE 0
#define ST_START 1        /* 面を描き切って "STAGE n START" を出している間 */
#define ST_PLAY  2
#define ST_CLEAR 3        /* クリア。自機も敵もその場で停止しファンファーレ */
#define ST_CONFIG 4       /* 隠しコンフィグ (タイトルで 上上下下左右左右BA) */
#define ST_ENDING 5       /* エンディング */

#define START_HOLD  60    /* 開始表示を出しておくフレーム数 (1秒) */
#define MSG_ROW     5     /* 開始表示を出すセル行 */

static uint8_t state;
static uint8_t prev_trig;
static uint8_t ending_to_ura;
static uint8_t loop_n;            /* 0=表 1以降=裏 (裏面の内容は TODO) */
static uint8_t timer;
static uint8_t msg_c0, msg_c1;    /* 開始表示が占めたセル列 (消すとき使う) */
static char    msg[16];

static void wait_vsync(void)
{
    uint8_t t = JIFFY;
    while (JIFFY == t) {
        /* 垂直帰線待ち */
    }
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
    /* パレットは tools/palette.py と必ず一致させること (絵の変換がこれを見る) */
    vdp_set_palette(COL_TUNNEL, 0, 0, 0);
    vdp_set_palette(COL_SKIN,   7, 4, 3);
    vdp_set_palette(COL_DARK,   2, 1, 1);
    vdp_set_palette(COL_BODY,   2, 3, 6);
    vdp_set_palette(COL_DIRT,   4, 2, 1);
    vdp_set_palette(COL_HUD,    1, 1, 2);
    vdp_set_palette(COL_BLAST,  7, 6, 1);
    vdp_set_palette(COL_RED,    6, 1, 1);
    vdp_set_palette(COL_FUSE,   7, 6, 2);
    vdp_set_palette(COL_WHITE,  7, 7, 7);
    vdp_set_palette(COL_ROCK,   5, 5, 5);
    vdp_set_palette(COL_RAT,    7, 3, 2);
    vdp_set_palette(COL_BAT,    7, 2, 7);
    vdp_set_palette(COL_BATH,   2, 5, 7);
    vdp_set_palette(COL_DIA,    3, 6, 7);
}

/* "STAGE n START" を面の上に出す。フォントは本体 BIOS のものを使う。 */
static void show_stage_msg(void)
{
    uint8_t n = 0, i;
    uint16_t x;
    const char *a = "STAGE ";
    const char *b = " START";
    for (i = 0; a[i]; i++) { msg[n++] = a[i]; }
    n = (uint8_t)(n + text_u8(&msg[n], stage));
    for (i = 0; b[i]; i++) { msg[n++] = b[i]; }
    msg[n] = 0;

    x = (uint16_t)((256 - (uint16_t)n * 16) >> 1) & 0xFFFE;
    text_draw(x, MSG_ROW * CELL, msg, COL_WHITE, COL_TUNNEL, 2);
    msg_c0 = (uint8_t)(x >> 4);
    msg_c1 = (uint8_t)((x + (uint16_t)n * 16 - 1) >> 4);
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
    set_game_palette();
    build_stage();
    bomb_clear();
    player_start();
    enemy_stage_reset();
    hud_reset();
    hud_draw();
    show_stage_msg();        /* 面を描き切ってから開始表示を出す */
    timer = START_HOLD;
    state = ST_START;
}

static void new_game(void)
{
    rnd_seed((uint16_t)((uint16_t)JIFFY << 8 | (uint16_t)JIFFY));
    lives = cfg_lives;
    stage = cfg_start_stage;
    stock = 0;              /* 開始ストックは0。火薬を拾って貯める。 */
    start_stage();
}

static void next_stage(void)
{
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
static void goto_ending(uint8_t to_ura)
{
    snd_stop();
    spr_park_all();
    oam_flush();
    set_game_palette();          /* タイトル1枚絵のパレットから戻す */
    vdp_fill(0, 0, 256, SCR_H, COL_TUNNEL);
    text_draw(48, 40, "CONGRATULATIONS", COL_FUSE, COL_TUNNEL, 1);
    text_draw(24, 72, "TANNO SANJI HAS DUG OUT", COL_WHITE, COL_TUNNEL, 1);
    text_draw(24, 88, "ALL THE BLACK DIAMONDS.", COL_WHITE, COL_TUNNEL, 1);
    text_draw(96, 128, "THE END", COL_WHITE, COL_TUNNEL, 2);
    text_draw(56, 176, "PUSH SPACE KEY", COL_DIA, COL_TUNNEL, 1);
    snd_play_fanfare();
    ending_to_ura = to_ura;
    state = ST_ENDING;
}

static void goto_title(void)
{
    snd_stop();
    spr_park_all();         /* 1枚絵の上にスプライトが残らないように */
    oam_flush();
    title_draw();
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
        snd_tick();

        if (state == ST_TITLE) {
            if (konami_check()) {
                set_game_palette();      /* タイトル1枚絵のパレットから戻す */
                config_enter();
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

        if (dead && cfg_invincible) {
            dead = 0;                /* 無敵設定 (動作確認用) */
            temp = 0;
        }

        if (dead) {
            /* 自爆の場合は爆風の閃光を見せ切ってからミス処理する。
               閃光中に片付けると finish_blast() が走らず黄色が残ってしまう。 */
            if (bomb_state != 2) {
                snd_sfx(SFX_MISS);
                if (lives <= 1) {
                    lives = 0;
                    goto_title();       /* 残機が尽きたらタイトルへ戻る */
                    continue;
                } else {
                    player_respawn();
                }
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
