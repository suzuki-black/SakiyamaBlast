/*
 * SakiyamaBlast - 1面の制限時間
 *
 * **時間をかければ誰でも抜けられる盤面にしない。**
 * 敵は掘り方と岩でいくらでも牽制できるので、詰まったら「安全な隅で待つ」が
 * 常に正解になってしまう (作者の指摘、2026-09-26)。面ごとの持ち時間を置いて、
 * 時間の側から押し返す。
 *
 * 約束:
 *   - **持ち時間は面ごとに決める** (`tlimit_tbl[]`)。ミスから戻ったら満タンに戻す
 *     (0秒のまま復帰すると、出た瞬間にまた時間切れで詰む)
 *   - **残り30秒でファンファーレ** (`TLIMIT_WARN`)。曲を一度止めて鳴らし、
 *     鳴り終わったら坑内の曲へ戻す。残り30秒からは数字が赤く点滅する
 *   - **0秒で1ミス。** 体温のオーバーヒートと同じ扱いで、画面の真ん中に
 *     大きく出す (`DEAD_TIME` -> main.c が "TIME OVER" を出す)
 *   - **クリアしたら残り時間を得点へ換える。** 数字が高速に減り、そのぶん
 *     スコアが上がる (`tlimit_bonus`)。速く抜けるほど点が高い
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

/*
 * **面ごとの持ち時間 (10秒単位)。** 18 = 180秒 = 3分。
 * 10秒単位なのは、1面ぶん1バイトに収めるため (180 は uint8_t に入らない)。
 *
 * **幕の頭で戻し、幕の峰で締める (2026-10-12)。** 以前は全面180秒だった。
 * 幕の頭 (面4・7・10・13) は新しい仕掛けを覚える面なので持ち時間を戻し、
 * 幕の最後 (面3・6・9・12・15) で10〜20秒締める。峰だけを見れば
 * 140 → 140 → 150 → 160 → 170 と**伸びている**が、その面の総耐久と掘る量は
 * もっと速く伸びるので、時計の余裕は幕が進むほど減る。
 *
 * **序盤は余らせる。** 時計は序盤では死因であってはならず、クリア時に得点へ
 * 換わる「ご褒美」として見せるだけでよい。面1を180→150 にしたのは、
 * 遅い人でも**残り30秒のファンファーレを一度は聞ける**長さに寄せるため。
 */
static const uint8_t tlimit_tbl[MAX_STAGE] = {
    /* 第一坑 */ 15, 15, 14,
    /* 第二坑 */ 16, 15, 14,
    /* 第三坑 */ 17, 16, 15,
    /* 第四坑 */ 18, 17, 16,
    /* 第五坑 */ 18, 18, 17,
};

static uint16_t tl_left;               /* 残り秒 */
static uint8_t  tl_frame;              /* 次の1秒までのフレーム数 */
static uint8_t  tl_resume;             /* ファンファーレの後に曲を戻す */

/* tlimit_draw の描き方 */
#define TD_DIGIT 0     /* 変わった桁だけ (毎秒の更新) */
#define TD_ALL   1     /* 「分:秒」と砂時計を全部 (面の開始、赤へ変わる瞬間) */
#define TD_FAST  2     /* 「分:秒」だけ (クリア時の高速な減り。砂時計は触らない) */

static void tlimit_draw(uint8_t how);

/* **HUD を塗り直した後に呼ぶこと。** ここで表示まで済ませる。 */
void tlimit_reset(void)
{
    /* **裏面 (2周目) は持ち時間が表の 2/3。** 表は「誰でもエンディングまで行ける」
       難度にしてあるので、締めるのは裏だけ (作者の指定、2026-10-25)。
       **1/3 は後半が成立しなかった** (hostsim の par で実測。面15 は最短の道順で
       54.5秒かかるのに持ち時間が56秒＝余り1.5秒で、敵も岩もある中では不可能)。
       2/3 なら余裕は面1で3.8倍・面15で2.1倍になり、表 (3.9〜10倍) の半分以下。
       **裏の圧力は時計だけで作らない** — 黒ダイヤが固く数も多い (par 自体が伸びる)、
       敵が速い、ガス、風呂は1回、残機が増えない、で締める。 */
    uint8_t k = (uint8_t)((stage >= 1 && stage <= MAX_STAGE) ? tlimit_tbl[stage - 1] : 18);
    tl_left = (uint16_t)((uint16_t)k * 10);
    if (loop_n) {
        tl_left = (uint16_t)(tl_left * 2 / 3);
    }
    tl_frame = 60;
    tl_resume = 0;
    tlimit_draw(TD_ALL);
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
 */
static void tlimit_draw(uint8_t how)
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
    if (how || s == 59) {                        /* 分が変わった */
        text_draw((uint16_t)(TL_X + 12), (uint16_t)(TL_Y + 1), buf, col, COL_HUD, 1);
    } else if ((s % 10) == 9) {                  /* 十の位が変わった */
        text_draw((uint16_t)(TL_X + 28), (uint16_t)(TL_Y + 1), &buf[2], col, COL_HUD, 1);
    } else {
        text_draw((uint16_t)(TL_X + 36), (uint16_t)(TL_Y + 1), &buf[3], col, COL_HUD, 1);
    }
    if (how == TD_ALL) {
        tl_mark(col);
    } else if (how == TD_DIGIT && warn) {
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
    tlimit_draw((uint8_t)((tl_left == TLIMIT_WARN) ? TD_ALL : TD_DIGIT));
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

/*
 * **クリアしたら残り時間を得点へ換える。** ファンファーレが鳴り終わってから、
 * main() が毎フレームこれを呼ぶ。1フレームに `TL_DRAIN` 秒ぶん減らして
 * そのぶん加点し、刻み音を鳴らす。**3分満タンでも1.5秒で換え終わる**ので、
 * 次の面へ行くまでの間延びにはならない。
 *
 * 数字が高速に減って点が上がるところを見せるのが目的 (作者の案、2026-09-26)。
 * 速く抜けるほど点が高いので、制限時間が「急かすだけのもの」で終わらない。
 *
 * 戻り値 1 = 換え終わった (次の面へ進んでよい)。
 */
uint8_t tlimit_bonus(void)
{
    uint8_t n;

    if (tl_left == 0) {
        return 1;
    }
    n = (uint8_t)((tl_left > TL_DRAIN) ? TL_DRAIN : tl_left);
    tl_left = (uint16_t)(tl_left - n);
    score_add((uint16_t)(n * PTS_TIME));
    tlimit_draw(TD_FAST);
    /* **鳴らしすぎない。** 毎フレーム鳴らすと1つ前が切れて「ジー」になる。
       2周に1回で「チチチチ」と刻む (数字の更新で1周が長くなるので、
       フレームで数えても実際は 1秒に 10回ほど)。
       **数えるのはフレーム。** 残り秒で数えると、残りが奇数のときに一度も
       条件に当たらず**音が一度も鳴らなかった** (実測で気付いた)。
       持ち時間の計数に使っている tl_frame は、ここでは空いているので借りる。 */
    if ((++tl_frame & 1) == 0) {
        snd_sfx(SFX_TALLY);
    }
    return (uint8_t)(tl_left == 0);
}
