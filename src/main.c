/*
 * SakiyamaBlast - 先山＋発破 / 常磐炭鉱ダイナマイト堀り
 *
 * 仕様の正は DESIGN.md、Mr. Do! 由来の挙動の根拠は docs/MRDO_RESEARCH.md。
 */
#include "msx.h"
#include "vdp.h"
#include "sprite.h"
#include "game.h"

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

static uint8_t prev_trig;

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

static void start_stage(void)
{
    build_stage();
    bomb_clear();
    player_start();
    hud_reset();
    hud_draw();
}

static void new_game(void)
{
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

void main(void)
{
    vdp_screen5();

    /* 坑内の色 */
    vdp_set_palette(COL_TUNNEL, 0, 0, 0);
    vdp_set_palette(COL_DIRT,   4, 2, 1);
    vdp_set_palette(COL_HUD,    1, 1, 2);
    vdp_set_palette(COL_BLAST,  7, 6, 1);
    vdp_set_palette(COL_RED,    6, 1, 1);
    vdp_set_palette(COL_FUSE,   7, 6, 2);
    vdp_set_palette(COL_WHITE,  7, 7, 7);

    spr_init();
    spr_pattern_rows(SPR_PLAYER, pat_miner);
    spr_pattern_rows(SPR_BOMB, pat_bomb);
    spr_color(SPR_PLAYER, COL_WHITE);
    spr_color(SPR_BOMB, COL_RED);

    new_game();

    for (;;) {
        wait_vsync();

        if (!dead) {
            player_update();
            if (fire_pressed()) {
                bomb_fire();
            }
        } else {
            fire_pressed();        /* 死亡中もエッジ検出の状態は進めておく */
        }
        bomb_update();

        if (dead) {
            /* 自爆の場合は爆風の閃光を見せ切ってからミス処理する。
               閃光中に片付けると finish_blast() が走らず黄色が残ってしまう。 */
            if (bomb_state != 2) {
                if (lives <= 1) {
                    lives = 0;
                    new_game();
                } else {
                    player_respawn();
                }
            }
        } else if (dug_pct >= WIN_PCT) {
            next_stage();
        }

        /* 無敵中は自機を点滅させる */
        if (invuln && (invuln & 4)) {
            spr_park(SPR_PLAYER);
        } else {
            spr_set(SPR_PLAYER, px, (uint8_t)(py - 1), SPR_PLAYER);
        }
        oam_flush();
        hud_draw();
    }
}
