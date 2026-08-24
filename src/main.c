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

/* 仮スプライト: 先山 (ヘルメット姿の炭鉱夫)。テーマ用の本番絵は後で差し替える。 */
static const uint16_t pat_miner[16] = {
    0x0000,   /* ................ */
    0x03C0,   /* ......####...... */
    0x0FF0,   /* ....########.... */
    0x1FF8,   /* ...##########... */
    0x3FFC,   /* ..############.. */
    0x07E0,   /* .....######..... */
    0x0DB0,   /* ....##.##.##.... */
    0x0FF0,   /* ....########.... */
    0x1FF8,   /* ...##########... */
    0x3FFC,   /* ..############.. */
    0x3FFC,   /* ..############.. */
    0x1FF8,   /* ...##########... */
    0x1C38,   /* ...###....###... */
    0x381C,   /* ..###......###.. */
    0x300C,   /* ..##........##.. */
    0x0000    /* ................ */
};

/* 仮スプライト: 設置したダイナマイト (導火線つきの筒) */
static const uint16_t pat_bomb[16] = {
    0x0000,   /* ................ */
    0x0080,   /* ........#....... */
    0x0140,   /* .......#.#...... */
    0x0080,   /* ........#....... */
    0x0100,   /* .......#........ */
    0x03C0,   /* ......####...... */
    0x07E0,   /* .....######..... */
    0x07E0,   /* .....######..... */
    0x07E0,   /* .....######..... */
    0x07E0,   /* .....######..... */
    0x07E0,   /* .....######..... */
    0x07E0,   /* .....######..... */
    0x07E0,   /* .....######..... */
    0x03C0,   /* ......####...... */
    0x0000,   /* ................ */
    0x0000    /* ................ */
};

/* 仮スプライト: 落盤の岩 */
static const uint16_t pat_rock[16] = {
    0x0000,   /* ................ */
    0x07E0,   /* .....######..... */
    0x1FF8,   /* ...##########... */
    0x3FFC,   /* ..############.. */
    0x7FFE,   /* .##############. */
    0x7FFE,   /* .##############. */
    0xFFFF,   /* ################ */
    0xFFFF,   /* ################ */
    0xFFFF,   /* ################ */
    0xFFFF,   /* ################ */
    0x7FFE,   /* .##############. */
    0x7FFE,   /* .##############. */
    0x3FFC,   /* ..############.. */
    0x1FF8,   /* ...##########... */
    0x07E0,   /* .....######..... */
    0x0000    /* ................ */
};

/* 仮スプライト: 敵 = ネズミ (坑道を走る通常種) */
static const uint16_t pat_rat[16] = {
    0x0000,   /* ................ */
    0x0000,   /* ................ */
    0x0C30,   /* ....##....##.... */
    0x1E78,   /* ...####..####... */
    0x3FFC,   /* ..############.. */
    0x7FFE,   /* .##############. */
    0xFFFF,   /* ################ */
    0xFFFF,   /* ################ */
    0xFFFF,   /* ################ */
    0x7FFE,   /* .##############. */
    0x3FFC,   /* ..############.. */
    0x1FF8,   /* ...##########... */
    0x0DB0,   /* ....##.##.##.... */
    0x0000,   /* ................ */
    0x0000,   /* ................ */
    0x0000    /* ................ */
};

/* 仮スプライト: 敵 = コウモリ (掘り屋へ変異した姿。土を掘って迫る) */
static const uint16_t pat_bat[16] = {
    0x0000,   /* ................ */
    0x0000,   /* ................ */
    0xC003,   /* ##............## */
    0xE007,   /* ###..........### */
    0xF81F,   /* #####......##### */
    0xFFFF,   /* ################ */
    0x7FFE,   /* .##############. */
    0x3FFC,   /* ..############.. */
    0x1FF8,   /* ...##########... */
    0x0FF0,   /* ....########.... */
    0x07E0,   /* .....######..... */
    0x03C0,   /* ......####...... */
    0x0180,   /* .......##....... */
    0x0000,   /* ................ */
    0x0000,   /* ................ */
    0x0000    /* ................ */
};

#define ST_TITLE 0
#define ST_START 1        /* 面を描き切って "STAGE n START" を出している間 */
#define ST_PLAY  2
#define ST_CLEAR 3        /* クリア。自機も敵もその場で停止しファンファーレ */

#define START_HOLD  60    /* 開始表示を出しておくフレーム数 (1秒) */
#define MSG_ROW     5     /* 開始表示を出すセル行 */

static uint8_t state;
static uint8_t prev_trig;
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
    vdp_set_palette(COL_TUNNEL, 0, 0, 0);
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
    lives = LIVES_INIT;
    stage = 1;
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

    spr_init();
    spr_pattern_rows(PAT_MINER, pat_miner);
    spr_pattern_rows(PAT_BOMB, pat_bomb);
    spr_pattern_rows(PAT_ROCK, pat_rock);
    spr_pattern_rows(PAT_RAT, pat_rat);
    spr_pattern_rows(PAT_BAT, pat_bat);
    spr_color(SPR_PLAYER, COL_WHITE);
    spr_color(SPR_BOMB, COL_RED);
    {
        uint8_t i;
        for (i = 0; i < ROCK_MAX; i++) {
            spr_color((uint8_t)(SPR_ROCK + i), COL_ROCK);
        }
    }

    goto_title();

    for (;;) {
        wait_vsync();
        snd_tick();

        if (state == ST_TITLE) {
            if (fire_pressed()) {
                new_game();      /* start_stage() が ST_START にする */
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
                next_stage();
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
