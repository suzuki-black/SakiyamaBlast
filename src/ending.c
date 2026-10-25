/*
 * SakiyamaBlast - エンドロール (ゆっくり流れるクレジット)
 *
 * **画面ごと縦にずらす** (V9938 の R#23 = 垂直オフセット)。1フレームに1画素も
 * 動かさずに済むので、流す速さを変えても処理は増えない。**書き写しはしない。**
 *
 * **ページ1 (VRAM 0x8000-) に描く。** ページ0 の下のほう (0x7400-0x7FFF) には
 * スプライトの表が載っていて、R#23 は 256行を輪にして回すので、そのまま使うと
 * 表の上に文字を書いてしまう。ページ1 なら 256行まるごと空いている。
 * コマンドも文字描画も「y に 256 を足す」だけでページ1 を指す
 * (SCREEN 5 では VRAM 全体が 256x1024 の絵として見えているため)。
 *
 * 流れ方:
 *   見えているのは [px, px+211]。**その下の [px+224, px+239] に次の1行を置く。**
 *   16画素進むごとに1行ぶん用意するので、文字は画面の下から入ってくる。
 *   帯の位置は 16の倍数に揃えてあり、256 をまたがないので消しも1回で済む。
 *
 * 文面は `src/endtext.c` (曲と同じセグメント14)。窓を向けると**この関数自身が
 * 消える**ので、`blob_read()` で1行ぶんを RAM へ写してから描く。
 */
#include "game.h"
#include "vdp.h"
#include "msx.h"
#include "text.h"
#include "endtext.h"

#define PAGE1     256           /* y に足すとページ1 */
#define ROW_H      16           /* 1行の高さ (16の倍数。256 を割り切ること) */
#define LEAD      224           /* 画面の下から何画素先に用意するか */
#define ROLL_WAIT   4           /* 何フレームで1画素流すか (大きいほどゆっくり) */
/* **終わりの塊** (献辞5行 + 空き2 + 礼 + 空き + 案内 = 10行) を、止まったあと
   画面に残す。流れている途中と**同じ位置**で止めるので、切り替わりは目に見えない。 */
#define ROLL_TAIL  10
#define ROLL_TOP   26           /* 塊の上端 (10行 = 160画素を 212行の中央へ) */

/* **1行ぶんの受け皿はスタックに取る。** 変数 (0xC000-0xC97F) に 33バイトの配列を
   置いたら RAM 実行の領域へ食い込んだ (`tools/ihx2rom.py` が止めてくれた)。
   スタックは 0xF380 から下へ伸びて実測で 0xF30F までしか使っていないので余裕がある。 */
static uint16_t roll_px;           /* 流した画素数 */
static uint8_t  roll_i;            /* 次に用意する行 */
static uint8_t  roll_t;            /* 1画素ぶんの待ち */

#define ROLL_END  ((uint16_t)(END_ROLL_N - ROLL_TAIL) * ROW_H + LEAD - ROLL_TOP)
#define ROLL_OVER 0xFFFF        /* 置き直しまで済んだ印 (変数を増やさないため) */

/*
 * ページ1 の行番号から y を作る (256 を足すだけ)。
 *
 * **わざわざ関数にしてある。** 呼び出し元で `(uint16_t)(PAGE1 + band)` と書くと、
 * SDCC が **D レジスタに 0 が入っているという誤った前提**で `inc d` だけを吐き、
 * 直前の比較で D に残っていた値がそのまま上位バイトになる。書き込み先が
 * 0x8808 ではなく 0x0808 になって、**エンドロールが1文字も描かれなかった**
 * (2026-10-25。openMSX の `debug set_bp` で `vdp_setwrite` の引数を見て判明)。
 * 関数の頭ではレジスタの思い込みが無いので、素直に16ビットの値が組み立てられる。
 *
 * **ページ1 の y は必ずこれを通す。** 定数どうしの足し算 (`PAGE1` 単体) は畳まれるので
 * 安全だが、**8ビットの値と足すところは一つ残らずここへ**。規則を一つにしておけば、
 * 後から足した1行で踏み直すことがない。
 * **ここを触ったら `build/ending.asm` を目で確かめること。**
 */
static uint16_t page1_y(uint8_t line)
{
    return (uint16_t)(PAGE1 + (uint16_t)line);
}

/* 窓を曲のセグメントへ向けて1行ぶん写す。**窓はこの中で元へ戻る。** */
static void fetch(uint8_t i, uint8_t *dst)
{
    blob_seg = MUSIC_SEG;
    blob_off = (uint16_t)((uint16_t)end_roll - 0x2000 + 0xA000
                          + (uint16_t)i * EROW_SIZE);
    blob_dst = dst;
    blob_len = EROW_SIZE;
    blob_read();
}

/* 1行を帯の中へ描く。中央寄せ。 */
static void draw_row(uint8_t i, uint16_t y)
{
    uint8_t rbuf[EROW_SIZE];
    uint8_t n = 0;
    uint8_t sc, col;
    uint16_t x;
    fetch(i, rbuf);
    if (!rbuf[1]) {
        return;                              /* 空行 */
    }
    sc = (uint8_t)((rbuf[0] == EROW_ATTR_T) ? 2 : 1);
    col = (uint8_t)((rbuf[0] == EROW_ATTR_N) ? COL_WHITE : COL_FUSE);
    while (rbuf[1 + n]) {
        n++;
    }
    /* **x は偶数にする** (1バイトが2画素なので、奇数だと隣の画素を巻き込む)。 */
    x = (uint16_t)(((256 - (uint16_t)n * (uint16_t)(8 * sc)) >> 1) & 0xFFFE);
    text_draw(x, y, (const char *)(rbuf + 1), col, COL_TUNNEL, sc);
}

void ending_start(void)
{
    roll_px = 0;
    roll_i = 0;
    roll_t = 0;
    vdp_fill(0, PAGE1, 256, 256, COL_TUNNEL);   /* ページ1 を消す */
    /* **スプライトを止める。** R#23 でずらすと**スプライトも一緒にずれる**ので、
       場外 (Y=212) へ退避した絵がずらした量だけ画面内へ入ってくる
       (15面クリアから入ると自機も岩も残って見えた。作者の報告、2026-10-25)。
       エンドロールでスプライトは1枚も使わないので、面ごと止めるのが確実。 */
    VDP_REG(8, 0x0A);                           /* SPD=1 / 64K DRAM */
    VDP_REG(23, 0);
    VDP_REG(2, 0x3F);                           /* 表示をページ1 へ */
    draw_row(0, page1_y(LEAD));
    roll_i = 1;
}

/* 表示をページ0 へ戻す。**エンドロールから抜けるときに必ず呼ぶ。** */
void ending_stop(void)
{
    VDP_REG(23, 0);
    VDP_REG(2, 0x1F);
    VDP_REG(8, 0x08);       /* スプライトを戻す (SPD=0 / 64K DRAM) */
}

void ending_tick(void)
{
    uint16_t band;
    uint8_t i;
    if (roll_px == ROLL_OVER) {
        return;
    }
    if (roll_px >= ROLL_END) {
        /* **消して置き直す。** 256行を輪にして回しているので、止めただけだと
           画面の上側に1周前の行が残る (実際に残った)。オフセットを戻し、
           見えている範囲を消してから、終わりの塊を同じ位置へ描き直す。 */
        VDP_REG(23, 0);
        vdp_fill(0, PAGE1, 256, SCR_H, COL_TUNNEL);
        for (i = 0; i < ROLL_TAIL; i++) {
            draw_row((uint8_t)(END_ROLL_N - ROLL_TAIL + i),
                     page1_y((uint8_t)(ROLL_TOP + i * ROW_H)));
        }
        roll_px = ROLL_OVER;
        return;
    }
    if (++roll_t < ROLL_WAIT) {
        return;
    }
    roll_t = 0;
    roll_px++;
    VDP_REG(23, (uint8_t)roll_px);
    if ((uint8_t)(roll_px & (ROW_H - 1)) == 0) {
        /* y の作り方は `page1_y()` の説明を見ること (SDCC の都合で関数にしてある)。 */
        band = page1_y((uint8_t)(roll_px + LEAD));
        vdp_fill(0, band, 256, ROW_H, COL_TUNNEL);
        if (roll_i < END_ROLL_N) {
            draw_row(roll_i, band);
            roll_i++;
        }
    }
}
