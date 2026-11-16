/*
 * SakiyamaBlast - 文字表示
 *
 * 本体 BIOS が持つ 8x8 フォントをそのまま使う (ROM にフォントデータを持たなくて済む)。
 * フォントの先頭アドレスは BIOS の 0x0004 に入っている (CGTBL)。ページ0は BIOS のままなので
 * カートリッジから起動していても普通に読める。漢字は無い (ASCII と半角カナのみ)。
 */
#include "text.h"
#include "vdp.h"

static uint16_t cgtbl;

/* **疑似ボールド。** 1ドット右へずらした自分自身を重ねて太らせる。
   BIOS のフォントは 8x8 の**右2列を使っていない**（openMSX で 0-9 A-Z を吸い出して
   確認済み）ので、右へずらしても隣の字とくっつかない。ROM に太字のフォントを持たずに
   字面だけ重くできる。0 にすれば元の細い字に戻る。 */
uint8_t text_bold = 1;

/* **描く走査行を絞る。** 既定は 0 から 8本 = 文字まるごと。
   1本だけにすれば1回の呼び出しが8分の1で済むので、**長い文字列を何フレームかに
   分けて描ける**。エンドロールがこれを使う (まとめて描くと主ループが 0.12秒 止まり、
   そのぶん音が伸びる)。**使ったら必ず 0 / 8 へ戻すこと。** */
uint8_t text_row0 = 0;
uint8_t text_rows = 8;

void text_init(void)
{
    cgtbl = *(volatile uint16_t *)0x0004;
}

/* 1行につき VRAM のアドレス設定を1回だけ行い、文字列の横方向はそのまま流し込む。
   文字ごとにアドレスを設定し直すと、6桁のスコアで 48回 も設定が要る (実測でここが
   処理時間の約9%を占めていた)。行を外側にすると 8回 で済む。
 *
 * **ビットは左へ送って毎回 0x80 を見る。** 以前は `bits & (0x80 >> (j << 1))` と
 * 書いていたが、**SDCC は「何ビット動かすか分からないシフト」をループで作る**ので、
 * 1文字あたり数千サイクルかかっていた。桁の位置を変えずにビットのほうを動かせば、
 * 定数シフト (`add a,a` 1回) で済む。
 *
 * **どれだけ遅かったか** (openMSX で実測、2026-11-16):
 *   - エンドロールの1行 (28文字) で **0.26秒**。主ループが止まるので
 *     `snd_tick()` が回らず、**音がそのぶん間延びしていた** (作者の報告)
 *   - コンフィグ画面を描き切るのに **2.1秒**。コナミコマンドの直後にキーを
 *     打っても拾われないのはこのため
 * **1文字あたり約1ミリ秒**かかっていた計算になる。直したあとの実測は
 * `docs/PITFALLS.md` にある。
 */
void text_draw(uint16_t x, uint16_t y, const char *s,
               uint8_t fg, uint8_t bg, uint8_t scale)
{
    uint8_t row, sy, j;
    /* **1バイトぶんの色は4通りしかない。** 2画素 = 2ビットなので、
       (背景,背景) (背景,前景) (前景,背景) (前景,前景) を先に作っておき、
       ビット2本を番号にして引くだけにする。条件分岐とシフトが消える。 */
    uint8_t pair[4];
    pair[0] = (uint8_t)((bg << 4) | bg);
    pair[1] = (uint8_t)((bg << 4) | fg);
    pair[2] = (uint8_t)((fg << 4) | bg);
    pair[3] = (uint8_t)((fg << 4) | fg);
    for (row = text_row0; row < (uint8_t)(text_row0 + text_rows); row++) {
        for (sy = 0; sy < scale; sy++) {
            const char *p = s;
            vdp_setwrite((uint16_t)((((uint16_t)(y + row * scale + sy)) << 7) + (x >> 1)));
            while (*p) {
                const uint8_t *g = (const uint8_t *)(cgtbl + ((uint16_t)(uint8_t)*p << 3));
                uint8_t bits = g[row];
                if (text_bold) {
                    bits = (uint8_t)(bits | (uint8_t)(bits >> 1));
                }
                if (scale == 2) {
                    /* 倍角は1ビットが1バイトまるごと。両方同じ色の2通りだけ引く。 */
                    for (j = 0; j < 8; j++) {
                        vdp_wdata(pair[(bits & 0x80) ? 3 : 0]);
                        bits = (uint8_t)(bits << 1);
                    }
                } else {
                    for (j = 0; j < 4; j++) {
                        vdp_wdata(pair[(uint8_t)(bits >> 6)]);
                        bits = (uint8_t)(bits << 2);
                    }
                }
                p++;
            }
        }
    }
}

uint8_t text_u8(char *dst, uint8_t v)
{
    uint8_t n = 0;
    if (v >= 100) {
        dst[n++] = (char)('0' + v / 100);
        v = (uint8_t)(v % 100);
        dst[n++] = (char)('0' + v / 10);
    } else if (v >= 10) {
        dst[n++] = (char)('0' + v / 10);
    }
    dst[n++] = (char)('0' + v % 10);
    dst[n] = 0;
    return n;
}
