/*
 * ホスト計測用のスタブ。
 *
 * ゲームのロジック (field/enemy/player/rock/bomb/bonus) はそのまま gcc で
 * コンパイルできるので、VDP・PSG・スプライト・文字・絵データだけを差し替える。
 * 描画と音は計測に関係しないので何もしない。ただし「何回鳴ったか」は
 * 爽快感の指標 (同時に鳴った効果音の種類数) に要るので数えておく。
 */
#include "game.h"
#include "vdp.h"
#include "sprite.h"
#include "sound.h"
#include "art.h"

/* --- 効果音: 種類ごとに鳴った回数を数えるだけ --- */
uint8_t sfx_fired[16];
void snd_sfx(uint8_t id)                 { if (id < 16) { sfx_fired[id]++; } }
void snd_sfx_up(uint8_t id, uint8_t n)   { (void)n; snd_sfx(id); }
void snd_stop(void)                      { }
void snd_tick(void)                      { }
void snd_play_bgm(void)                  { }
void snd_play_fanfare(void)              { }

/* --- VDP: 描かない --- */
void vdp_wreg(uint16_t rv)               { (void)rv; }
void vdp_setwrite(uint16_t a)            { (void)a; }
void vdp_wdata(uint8_t v)                { (void)v; }
void vdp_wpal(uint8_t v)                 { (void)v; }
void vdp_fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t c)
{ (void)x; (void)y; (void)w; (void)h; (void)c; }
void vdp_display(uint8_t on)             { (void)on; }
void vdp_adjust(int8_t dx, int8_t dy)    { (void)dx; (void)dy; }
void vdp_backdrop(uint8_t c)             { (void)c; }
void vdp_palette(const uint8_t *p)       { (void)p; }

/* --- スプライト --- */
void spr_set(uint8_t n, uint8_t x, uint8_t y, uint8_t pat) { (void)n;(void)x;(void)y;(void)pat; }
void spr_park(uint8_t n)                 { (void)n; }
void spr_park_all(void)                  { }
void spr_color_lines(uint8_t n, const uint8_t *c) { (void)n; (void)c; }
void oam_flush(void)                     { }

/* --- 文字 --- */
void text_draw(uint16_t x, uint16_t y, const char *s, uint8_t f, uint8_t b, uint8_t z)
{ (void)x; (void)y; (void)s; (void)f; (void)b; (void)z; }
void text_u8(char *dst, uint8_t v)       { (void)dst; (void)v; }

/* --- 絵データ: 中身は要らないので空の板を返す --- */
static const uint8_t blank[128];
const uint8_t *art_tile_of(uint8_t i)    { (void)i; return blank; }
const uint8_t *art_colours(uint8_t i)    { (void)i; return blank; }
void draw_tile(uint8_t cx, uint8_t cy, const uint8_t *t, uint8_t inset)
{ (void)cx; (void)cy; (void)t; (void)inset; }

/* --- MSX BIOS: 入力はドライバ (sim.c) が毎フレーム書き込む --- */
uint8_t sim_stick;      /* GTSTCK と同じ 0=なし 1=上 3=右 5=下 7=左 */
uint8_t sim_trig;       /* A = つるはし */
uint8_t sim_trig_b;     /* B = ダイナマイト */
uint8_t gtstck(uint8_t d)                { return d == 0 ? sim_stick : 0; }
uint8_t gttrig(uint8_t d)                { return (d == 0) ? sim_trig : (d == 3 ? sim_trig_b : 0); }
uint8_t snsmat(uint8_t r)                { (void)r; return 0xFF; }
void bank_select(uint8_t s)              { (void)s; }

/* --- main.c / hud.c が持っている状態のうち、ロジックが参照するもの --- */
uint16_t frame_n;
uint8_t  score_d[SCORE_DIGITS];

/* 得点は桁配列ではなく素の数で持つ。指標の「放出量」で得点差分を使うため。 */
uint32_t sim_score;
void score_add(uint16_t v) { sim_score += v; }

/* hud.c の kill_pts と同じ表 (1..5体で倍々)。落下岩は爆風の倍。 */
uint16_t kill_value(uint8_t n, uint8_t by_rock);
void score_kills(uint8_t n, uint8_t by_rock) { sim_score += kill_value(n, by_rock); }

void vdp_set_palette(uint8_t i, uint8_t r, uint8_t g, uint8_t b)
{ (void)i; (void)r; (void)g; (void)b; }

/* --- 得点表示。計測では位置と値だけ拾えれば足りる --- */
uint16_t pop_last_v;          /* 直近に出た点数 (指標で使えるように残す) */
void score_popup(uint8_t cx, uint8_t cy, uint16_t v) { (void)cx; (void)cy; pop_last_v = v; }
void popup_tick(void) { }

/* hud.c の kill_pts と同じ表。同時に倒すほど倍率が上がる。 */
uint16_t kill_value(uint8_t n, uint8_t by_rock)
{
    static const uint16_t kill_pts[6] = { 0, 100, 200, 400, 600, 800 };
    if (n == 0) { return 0; }
    if (n > 5)  { n = 5; }
    return by_rock ? (uint16_t)(kill_pts[n] * 2) : kill_pts[n];
}
