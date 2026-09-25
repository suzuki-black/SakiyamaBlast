/*
 * SakiyamaBlast - 1面の制限時間
 *
 * **時間をかければ誰でも抜けられる盤面にしない。**
 * 敵は掘り方と岩でいくらでも牽制できるので、詰まったら「安全な隅で待つ」が
 * 常に正解になってしまう (作者の指摘、2026-09-26)。面ごとの持ち時間を置いて、
 * 時間の側から押し返す。
 *
 * 約束:
 *   - **持ち時間は面ごとに 3分** (`TLIMIT_SEC`)。ミスから戻ったら満タンに戻す
 *     (0秒のまま復帰すると、出た瞬間にまた時間切れで詰む)
 *   - **残り30秒でファンファーレ** (`TLIMIT_WARN`)。曲を一度止めて鳴らし、
 *     鳴り終わったら坑内の曲へ戻す。残り30秒からは数字が赤く点滅する
 *   - **0秒で1ミス。** 体温のオーバーヒートと同じ扱いで、画面の真ん中に
 *     大きく出す (`DEAD_TIME` -> main.c が "TIME OVER" を出す)
 *
 * 表示は HUD の下段の右端 (`hud.c` の並びを右へ詰めて空けてある)。
 * **描くのは変わった桁だけ** (理由は tlimit_draw の説明)。
 */
#include "game.h"
#include "vdp.h"
#include "text.h"
#include "sound.h"

#define TL_X     200          /* HUD 下段。COAL の右 */
#define TL_Y     194

/* 残り時間 (分, 秒)。表示のためだけでなく、tools/smoketest.sh がここを読んで
   「面の開始で 3:00 から始まる」ことを確かめている。 */
uint8_t tl_sec_hi, tl_sec_lo;

static uint16_t tl_left;               /* 残り秒 */
static uint8_t  tl_frame;              /* 次の1秒までのフレーム数 */
static uint8_t  tl_resume;             /* ファンファーレの後に曲を戻す */

static void tlimit_draw(uint8_t full);

/* **HUD を塗り直した後に呼ぶこと。** ここで表示まで済ませる。 */
void tlimit_reset(void)
{
    tl_left = TLIMIT_SEC;
    tl_frame = 60;
    tl_resume = 0;
    tlimit_draw(1);
}

/* 砂時計の印。数字だけでは何の数字か分からない。左右をくびれさせて作る。 */
static void tl_mark(uint8_t col)
{
    vdp_fill(TL_X, (uint16_t)(TL_Y + 1), 8, 8, col);
    vdp_fill(TL_X, (uint16_t)(TL_Y + 4), 3, 2, COL_HUD);
    vdp_fill((uint16_t)(TL_X + 5), (uint16_t)(TL_Y + 4), 3, 2, COL_HUD);
}

/*
 * 「分:秒」。**変わった桁だけ描く。**
 * 毎秒まるごと描き直していたときは、それだけで **60fps が 57.5 まで落ちた**
 * (openMSX で実測)。1秒に一度でも、帯を塗り直して4文字書くと1フレームぶん近く食う。
 * いま書き換えるのは普段は一の位の1文字だけで、十の位は10秒に一度、
 * 「分:」は1分に一度しか触らない。
 *
 * 残り TLIMIT_WARN 秒からは**数字が赤くなり、砂時計が1秒ごとに点滅する**。
 * full = 1 で全部描く (面の開始と、赤へ変わる瞬間)。
 */
static void tlimit_draw(uint8_t full)
{
    char buf[6];
    uint8_t s = (uint8_t)(tl_left % 60), m = (uint8_t)(tl_left / 60);
    uint8_t warn = (uint8_t)(tl_left <= TLIMIT_WARN);
    uint8_t col = (uint8_t)(warn ? COL_RED : COL_WHITE);

    tl_sec_hi = m;
    tl_sec_lo = s;
    buf[0] = (char)('0' + m);
    buf[1] = ':';
    buf[2] = (char)('0' + s / 10);
    buf[3] = (char)('0' + s % 10);
    buf[4] = 0;
    if (full || s == 59) {                       /* 分が変わった */
        text_draw((uint16_t)(TL_X + 12), (uint16_t)(TL_Y + 1), buf, col, COL_HUD, 1);
    } else if ((s % 10) == 9) {                  /* 十の位が変わった */
        text_draw((uint16_t)(TL_X + 28), (uint16_t)(TL_Y + 1), &buf[2], col, COL_HUD, 1);
    } else {
        text_draw((uint16_t)(TL_X + 36), (uint16_t)(TL_Y + 1), &buf[3], col, COL_HUD, 1);
    }
    if (full) {
        tl_mark(col);
    } else if (warn) {
        tl_mark((uint8_t)((s & 1) ? COL_RED : COL_HUD));   /* 点滅させて急かす */
    }
}

void tlimit_tick(void)
{
    /* ファンファーレが鳴り終わったら坑内の曲へ戻す */
    if (tl_resume && snd_idle()) {
        tl_resume = 0;
        snd_play_bgm();
    }
    if (tl_left == 0) {
        return;
    }
    if (--tl_frame) {
        return;
    }
    tl_frame = 60;
    tl_left--;
    tlimit_draw((uint8_t)(tl_left == TLIMIT_WARN));
    if (tl_left == TLIMIT_WARN) {
        /* **急かす。** 曲を一度止めて短いファンファーレを鳴らし、そのあと曲へ戻す。
           戻すときに頭から鳴り直すので、「後半が始まった」合図にもなる。 */
        snd_stop();
        snd_play_hurry();
        tl_resume = 1;
    }
    if (tl_left == 0) {
        /* **1ミス。** 無敵設定のときに取り消すのは main.c の役目 (他の死因と同じ)。 */
        dead = 1;
        dead_cause = DEAD_TIME;
    }
}
