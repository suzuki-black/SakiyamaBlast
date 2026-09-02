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
static uint8_t player_pat = 0xFF;   /* いま出している自機の絵 (変化時だけ色表を書く) */
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

/* 前回からいくつ垂直帰線を跨いだか。1 なら 60fps が出ている。 */
static uint8_t frames_passed;

static void wait_vsync(void)
{
    uint8_t j;
    while (JIFFY == last_j) {
        /* まだ次のフレームが来ていない */
    }
    j = JIFFY;
    frames_passed = (uint8_t)(j - last_j);
    if (frames_passed > 4) {
        frames_passed = 4;          /* 面の組み立てなど長い停止のあとは追いつかない */
    }
    last_j = j;
}

/*
 * 音を実時間に合わせる。
 *
 * snd_tick() をループ1周に1回だけ呼ぶと、**処理落ちがそのまま曲のテンポになる。**
 * 実測で、掘っている間は 43回/秒 しか回らず、曲が 72% の速さで鳴っていた。
 * 跨いだフレーム数だけ回して実時間に追いつかせる。
 */
static void snd_catch_up(void)
{
    uint8_t i;
    for (i = 0; i < frames_passed; i++) {
        snd_tick();
    }
}

/* A ボタン (スペース / ジョイスティックのトリガA) の立ち上がり。
   ゲーム中はつるはし、タイトルやコンフィグでは決定に使う。 */
static uint8_t fire_pressed(void)
{
    uint8_t t = (uint8_t)(gttrig(0) || gttrig(1));
    uint8_t edge = (uint8_t)(t && !prev_trig);
    prev_trig = t;
    return edge;
}

/* B ボタン (キーボードの B / ジョイスティック1のトリガB) の立ち上がり。ダイナマイトを投げる。
   キーマトリクスの行2は下位から : ` , . / _ A B なので、B はビット7。 */
static uint8_t prev_trig_b;
static uint8_t throw_pressed(void)
{
    uint8_t t = (uint8_t)(gttrig(3) || !(snsmat(2) & 0x80));
    uint8_t edge = (uint8_t)(t && !prev_trig_b);
    prev_trig_b = t;
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
    /* **先に音を止める。** ここから面を描き切るまで約1.1秒ブロックする間は
       snd_tick() が回らないので、鳴っていた音を PSG が保持し続けてしまう。
       止めておかないと「ピー」と鳴りっぱなしになり、固まったように聞こえる。 */
    snd_stop();
    field_fx_reset();      /* 揺れ・閃光・閃き・連鎖を落としてから組み直す */
    /* パレットの差し替えと面の描き起こしは見せない。描き切ってから幕を上げる。 */
    vdp_display(0);
    set_game_palette();
    build_stage();
    bomb_clear();
    player_start();
    enemy_stage_reset();
    bonus_stage_reset();
    debris_clear();
    hud_reset();
    hud_draw();
    show_stage_msg();        /* 面を描き切ってから開始表示を出す */
    vdp_display(1);
    timer = START_HOLD;
    state = ST_START;
}

/* 面クリア時に得点を入れた本数。ストックは次の面へ持ち越すので、これを憶えて
   おかないと同じ1本に毎面ボーナスが入ってしまう。1本につき1回だけ入れる。 */
static uint8_t scored_stock;

static void new_game(void)
{
    rnd_seed((uint16_t)((uint16_t)JIFFY << 8 | (uint16_t)JIFFY));
    lives = cfg_lives;
    stage = cfg_start_stage;
    score_reset();
    stock = 0;              /* 開始ストックは0。火薬を拾って貯める。 */
    scored_stock = 0;
    start_stage();
}

static void next_stage(void)
{
    if (scored_stock > stock) {
        scored_stock = stock;        /* 使った分は勘定から外す */
    }
    score_add((uint16_t)(stock - scored_stock) * PTS_LEFTOVER_DYN);
    scored_stock = stock;
    stage++;                /* ストックは持ち越す (リセットしない) */
    start_stage();
}

/* スプライトを OAM へ流し込む。停止中 (ST_START / ST_CLEAR) もその場に出したままにする。 */
static void draw_actors(void)
{
    if (invuln && (invuln & 4)) {
        spr_park(SPR_PLAYER);           /* 無敵中は点滅 */
    } else {
        /* 溜めている間は構えの絵に差し替える。色の帯の位置も変わるので色表も。 */
        {
            uint8_t pat = player_pattern();
            if (pat != player_pat) {
                player_pat = pat;
                spr_color_lines(SPR_PLAYER, art_colours(pat));
            }
            spr_set(SPR_PLAYER, px, (uint8_t)(py - 1), pat);
        }
    }
    rock_draw();
    enemy_draw();
    debris_draw();
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
    /* **「掘り尽くす」ではなく「帰る」で書く。** docs/HISTORY.md §8 の通り、
       この人物は炭鉱町がにぎわう中をまっすぐ家へ帰る側の人だった。勝利や達成では
       なく、番方を終えて帰るのが結びとして合う。
       名前も実名 (TANNO SANJI) ではなくキャラクター名にしてある。実名を公開物へ
       載せるかは作者が検討中のため、決まるまでは出さない。 */
    text_draw(48, 40, "CONGRATULATIONS", COL_FUSE, COL_TUNNEL, 1);
    text_draw(24, 72, "TANNO 3JI FINISHED HIS SHIFT", COL_WHITE, COL_TUNNEL, 1);
    text_draw(72, 88, "AND WENT HOME.", COL_WHITE, COL_TUNNEL, 1);
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
    snd_play_title();
    state = ST_TITLE;
}

void main(void)
{
    bank_select(DATA_SEG);      /* 0xA000 の窓を絵と曲へ向けておく */
    vdp_screen5();
    vdp_adjust_init();
    set_game_palette();
    text_init();
    snd_init();
    config_init();

    spr_init();
    {
        uint8_t i;
        /* 絵は assets の PNG から生成したものをそのまま使う */
        /* **枚数は art 側から取る。** ここを PAT_xxx で決め打ちにすると、
           絵を足したときに転送漏れが起きて VRAM のゴミが表示される
           (割れた岩を足したとき、実際にそうなった)。 */
        for (i = 0; i < art_pattern_count(); i++) {
            spr_pattern_rows(i, art_pattern(i));
        }
        spr_color_lines(SPR_PLAYER, art_colours(PAT_MINER));
        spr_color_lines(SPR_BOMB, art_colours(PAT_BOMB));
        for (i = 0; i < ROCK_MAX; i++) {
            spr_color_lines((uint8_t)(SPR_ROCK + i), art_colours(PAT_ROCK));
        }
        for (i = 0; i < DEBRIS_MAX; i++) {
            spr_color_lines((uint8_t)(SPR_DEBRIS + i), art_colours(PAT_CHIP));
        }
    }

    goto_title();

    for (;;) {
        wait_vsync();
        frame_n++;
        snd_catch_up();

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

        /* ヒットストップ。**止めるのは更新だけ**で、音と描画は進める。
           止まっている間に効果音が途切れると、逆に手応えが痩せる。 */
        if (hitstop) {
            hitstop--;
            coal_shake_tick();
            popup_tick();
            cavein_flash_tick();
            debris_tick();      /* 止めている間もかけらは動かす。吸い込まれる絵が要る */
            draw_actors();
            hud_draw();
            continue;
        }
        if (!dead) {
            player_update();
            /* **押しているかどうか**をそのまま渡す。押した瞬間に威力1で即座に振り、
               押したままなら振り終わるたびに自動で次を振って威力が上がる。 */
            fire_pressed();
            player_pick((uint8_t)(gttrig(0) || gttrig(1)));
            if (throw_pressed()) {
                bomb_throw(player_dir());
            }
        } else {
            fire_pressed();        /* 死亡中もエッジ検出の状態は進めておく */
            throw_pressed();
        }
        coal_shake_tick();
        popup_tick();
        cavein_flash_tick();
        debris_tick();
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
        } else if (coal_left == 0 && rock_busy()) {
            /* **落盤が終わるまでクリアを待つ。** 最後の炭柱を割ってすぐ回収すると、
               岩が揺れている最中にクリアが成立して落盤が一度も完走しなかった。
               待っている間は自機を無敵にしておく。自分で起こした落盤に潰されて
               「クリアしたのに死ぬ」のは理不尽なので。 */
            invuln = 2;
        } else if (coal_left == 0) {
            /* 盤面の黒ダイヤを全部掘り出した = ステージクリア。
               掘削率はクリア条件ではなく、ここで得点として加算する。 */
            score_add((uint16_t)dug_pct * PTS_DUG_PCT);
            snd_stop();
            snd_play_fanfare();
            state = ST_CLEAR;
        }

        draw_actors();
        hud_draw();
    }
}
